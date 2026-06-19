// WinSock2 must be included before Windows.h to avoid version conflicts
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <mstcpip.h>
#pragma comment(lib, "ws2_32.lib")

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include "Kmbox/KmBoxNetManager.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>

namespace
{
    bool IsValidSocket(SOCKET socketHandle)
    {
        return socketHandle != 0 && socketHandle != INVALID_SOCKET;
    }

    void DisableUdpConnectionReset(SOCKET socketHandle, const char* label)
    {
        BOOL resetEnabled = FALSE;
        DWORD bytesReturned = 0;
        const int status = WSAIoctl(
            socketHandle,
            SIO_UDP_CONNRESET,
            &resetEnabled,
            sizeof(resetEnabled),
            nullptr,
            0,
            &bytesReturned,
            nullptr,
            nullptr);
        if (status == SOCKET_ERROR) {
            Diagnostics::Warn("[KMBOX-NET] Failed to disable UDP connection reset on %s socket. WSA=%d",
                label ? label : "unknown",
                WSAGetLastError());
        }
    }

    KmBoxCommandType CommandTypeForCmd(unsigned int cmd)
    {
        switch (cmd) {
        case cmd_connect:        return KmBoxCommandType::Connect;
        case cmd_mouse_move:     return KmBoxCommandType::MouseMove;
        case cmd_mouse_automove: return KmBoxCommandType::MouseAutoMove;
        case cmd_mouse_left:
        case cmd_mouse_right:
        case cmd_mouse_middle:   return KmBoxCommandType::MouseButton;
        case cmd_mask_mouse:     return KmBoxCommandType::MouseMask;
        case cmd_unmask_all:     return KmBoxCommandType::MouseUnmask;
        case cmd_monitor:        return KmBoxCommandType::Monitor;
        case cmd_reboot:         return KmBoxCommandType::Reboot;
        case cmd_setconfig:      return KmBoxCommandType::SetConfig;
        default:                 return KmBoxCommandType::Unknown;
        }
    }

    WORD VirtualKeyToHidKey(WORD vKey)
    {
        if (vKey >= 'A' && vKey <= 'Z')
            return static_cast<WORD>(KEY_A + (vKey - 'A'));

        if (vKey >= '1' && vKey <= '9')
            return static_cast<WORD>(KEY_1_EXCLAMATION_MARK + (vKey - '1'));

        if (vKey == '0')
            return KEY_0_CPARENTHESIS;

        if (vKey >= VK_F1 && vKey <= VK_F12)
            return static_cast<WORD>(KEY_F1 + (vKey - VK_F1));

        switch (vKey) {
        case VK_CONTROL:
        case VK_LCONTROL: return KEY_LEFTCONTROL;
        case VK_RCONTROL: return KEY_RIGHTCONTROL;
        case VK_SHIFT:
        case VK_LSHIFT:   return KEY_LEFTSHIFT;
        case VK_RSHIFT:   return KEY_RIGHTSHIFT;
        case VK_MENU:
        case VK_LMENU:    return KEY_LEFTALT;
        case VK_RMENU:    return KEY_RIGHTALT;
        case VK_LWIN:     return KEY_LEFT_GUI;
        case VK_RWIN:     return KEY_RIGHT_GUI;
        case VK_SPACE:    return KEY_SPACEBAR;
        case VK_RETURN:   return KEY_ENTER;
        case VK_ESCAPE:   return KEY_ESCAPE;
        case VK_TAB:      return KEY_TAB;
        case VK_CAPITAL:  return 0x39; // HID keyboard Caps Lock
        default:          return 0;
        }
    }

    void LogMouseButtonChanges(unsigned char previous, unsigned char current)
    {
        struct ButtonMap {
            unsigned char mask;
            const char* name;
        };

        constexpr ButtonMap buttons[] = {
            { 0x01, "VK_LBUTTON" },
            { 0x02, "VK_RBUTTON" },
            { 0x04, "VK_MBUTTON" },
            { 0x08, "VK_XBUTTON1" },
            { 0x10, "VK_XBUTTON2" },
        };

        const unsigned char changed = previous ^ current;
        for (const ButtonMap& button : buttons) {
            if ((changed & button.mask) != 0) {
                Diagnostics::Info("[KMBOX-NET] input button state changed. button=%s down=%d mask=0x%02X",
                    button.name,
                    (current & button.mask) != 0 ? 1 : 0,
                    static_cast<unsigned int>(current));
            }
        }
    }

    void FormatKeyboardKeys(const standard_keyboard_report_t& keyboard, char* buffer, size_t bufferSize)
    {
        if (!buffer || bufferSize == 0)
            return;

        size_t offset = 0;
        buffer[0] = '\0';
        for (unsigned char key : keyboard.data) {
            if (key == 0)
                continue;

            const int written = std::snprintf(
                buffer + offset,
                bufferSize - offset,
                "%s%02X",
                offset == 0 ? "" : " ",
                static_cast<unsigned int>(key));
            if (written <= 0)
                break;

            offset += static_cast<size_t>(written);
            if (offset >= bufferSize)
                break;
        }

        if (offset == 0)
            std::snprintf(buffer, bufferSize, "none");
    }

    void LogKeyboardReportChange(
        const standard_keyboard_report_t& previous,
        const standard_keyboard_report_t& current)
    {
        if (previous.buttons == current.buttons &&
            std::memcmp(previous.data, current.data, sizeof(current.data)) == 0) {
            return;
        }

        char keys[64] = {};
        FormatKeyboardKeys(current, keys, sizeof(keys));
        Diagnostics::Info("[KMBOX-NET] keyboard report changed. modifiers=0x%02X keys=%s",
            static_cast<unsigned int>(current.buttons),
            keys);
    }

    bool IsOutputCommand(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseMove ||
            type == KmBoxCommandType::MouseAutoMove ||
            type == KmBoxCommandType::MouseButton ||
            type == KmBoxCommandType::MouseMask ||
            type == KmBoxCommandType::MouseUnmask ||
            type == KmBoxCommandType::Keyboard;
    }

    bool IsMouseMoveCommand(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseMove ||
            type == KmBoxCommandType::MouseAutoMove;
    }

    bool ShouldDropMouseMoveFirst(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseButton ||
            type == KmBoxCommandType::MouseMask ||
            type == KmBoxCommandType::MouseUnmask ||
            type == KmBoxCommandType::Keyboard;
    }

    bool ShouldPrioritizeAheadOfQueuedMoves(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseButton ||
            type == KmBoxCommandType::MouseUnmask ||
            type == KmBoxCommandType::Keyboard;
    }

    int UpdateQueuedMouseMoveButtonState(std::deque<KmBoxQueuedNetCommand>& queue,
                                         std::deque<KmBoxQueuedNetCommand>::iterator begin,
                                         int buttonState)
    {
        int updated = 0;
        for (auto it = begin; it != queue.end(); ++it) {
            if (!IsMouseMoveCommand(it->type))
                continue;
            it->data.cmd_mouse.button = buttonState;
            ++updated;
        }
        return updated;
    }

    bool DropOldestMouseMove(std::deque<KmBoxQueuedNetCommand>& queue,
                             KmBoxQueuedNetCommand& dropped)
    {
        const auto item = std::find_if(queue.begin(), queue.end(),
            [](const KmBoxQueuedNetCommand& queued) {
                return IsMouseMoveCommand(queued.type);
            });
        if (item == queue.end())
            return false;

        dropped = *item;
        queue.erase(item);
        return true;
    }

    int FlushIntervalForCommand(KmBoxCommandType type)
    {
        const bool latencySensitive =
            type == KmBoxCommandType::MouseButton ||
            type == KmBoxCommandType::MouseMask ||
            type == KmBoxCommandType::MouseUnmask ||
            type == KmBoxCommandType::Keyboard;

        return latencySensitive
            ? KmBoxRuntimeConfig::MouseButtonFlushIntervalMs
            : KmBoxRuntimeConfig::CommandFlushIntervalMs;
    }
}

KmBoxNetManager::KmBoxNetManager()
    : randomEngine(static_cast<unsigned int>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

KmBoxNetManager::~KmBoxNetManager()
{
    StopWorkers();
    KeyBoard.EndMonitor();
    CloseSocket();

    if (wsaStarted) {
        WSACleanup();
        wsaStarted = false;
    }
}

KmBoxConnectionState KmBoxNetManager::GetConnectionState() const
{
    return connectionState.load(std::memory_order_acquire);
}

bool KmBoxNetManager::IsConnected() const
{
    return GetConnectionState() == KmBoxConnectionState::Connected;
}

void KmBoxNetManager::SetConnectionState(KmBoxConnectionState State)
{
    const KmBoxConnectionState previous =
        connectionState.exchange(State, std::memory_order_acq_rel);
    if (previous != State) {
        Diagnostics::Info("[KMBOX-NET] connection state %s -> %s",
            ToString(previous), ToString(State));
    }
}

bool KmBoxNetManager::ConfigureSocketTimeouts(SOCKET SocketHandle)
{
    const int timeoutMs = KmBoxRuntimeConfig::CommandTimeoutMs;
    if (setsockopt(SocketHandle, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == SOCKET_ERROR) {
        Diagnostics::Error("[KMBOX-NET] Failed to set receive timeout. WSA=%d", WSAGetLastError());
        return false;
    }

    if (setsockopt(SocketHandle, SOL_SOCKET, SO_SNDTIMEO,
            reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs)) == SOCKET_ERROR) {
        Diagnostics::Error("[KMBOX-NET] Failed to set send timeout. WSA=%d", WSAGetLastError());
        return false;
    }

    return true;
}

void KmBoxNetManager::CloseSocket()
{
    std::lock_guard<std::mutex> lock(sendMutex);
    if (IsValidSocket(s_Client)) {
        closesocket(s_Client);
    }
    s_Client = 0;
    if (connectionState.load(std::memory_order_acquire) != KmBoxConnectionState::Error)
        SetConnectionState(KmBoxConnectionState::Disconnected);
}

bool KmBoxNetManager::OpenSocket()
{
    std::lock_guard<std::mutex> lock(sendMutex);
    std::string ip;
    WORD port = 0;
    {
        std::lock_guard<std::mutex> dataLock(dataMutex);
        ip = configuredIp;
        port = configuredPort;
    }

    if (IsValidSocket(s_Client)) {
        closesocket(s_Client);
        s_Client = 0;
    }

    s_Client = socket(AF_INET, SOCK_DGRAM, 0);
    if (!IsValidSocket(s_Client)) {
        Diagnostics::Error("[KMBOX-NET] Failed to create UDP socket. WSA=%d", WSAGetLastError());
        s_Client = 0;
        return false;
    }

    if (!ConfigureSocketTimeouts(s_Client)) {
        closesocket(s_Client);
        s_Client = 0;
        return false;
    }
    DisableUdpConnectionReset(s_Client, "command");

    AddrServer = {};
    AddrServer.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
    AddrServer.sin_family = AF_INET;
    AddrServer.sin_port = htons(port);

    if (AddrServer.sin_addr.S_un.S_addr == INADDR_NONE) {
        Diagnostics::Error("[KMBOX-NET] Invalid device IP address: %s", ip.c_str());
        closesocket(s_Client);
        s_Client = 0;
        return false;
    }

    return true;
}

unsigned int KmBoxNetManager::NextRandom()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return randomEngine();
}

client_data KmBoxNetManager::BuildPacket(unsigned int Cmd, unsigned int RandValue)
{
    client_data packet{};
    std::lock_guard<std::mutex> lock(dataMutex);
    packet.head.mac = PostData.head.mac;
    packet.head.rand = RandValue;
    packet.head.indexpts = 0;
    packet.head.cmd = Cmd;
    return packet;
}

void KmBoxNetManager::StampPacketForSend(client_data& Packet)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    Packet.head.mac = PostData.head.mac;
    Packet.head.indexpts = ++PostData.head.indexpts;
    PostData = Packet;
}

int KmBoxNetManager::SendPacketOnce(client_data& packet, int DataLength, KmBoxCommandType Type)
{
    std::lock_guard<std::mutex> lock(sendMutex);
    if (!IsValidSocket(s_Client)) {
        if (IsOutputCommand(Type)) {
            Diagnostics::Aim("udp.send early_return reason=invalid_socket type=%s cmd=0x%08X len=%d x=%d y=%d button=%d",
                ToString(Type),
                packet.head.cmd,
                DataLength,
                packet.cmd_mouse.x,
                packet.cmd_mouse.y,
                packet.cmd_mouse.button);
        }
        return err_creat_socket;
    }

    Diagnostics::Trace("[KMBOX-NET] send %s cmd=0x%08X pts=%u len=%d",
        ToString(Type), packet.head.cmd, packet.head.indexpts, DataLength);
    if (IsOutputCommand(Type)) {
        Diagnostics::Aim("udp.send fire type=%s cmd=0x%08X pts=%u len=%d x=%d y=%d button=%d",
            ToString(Type),
            packet.head.cmd,
            packet.head.indexpts,
            DataLength,
            packet.cmd_mouse.x,
            packet.cmd_mouse.y,
            packet.cmd_mouse.button);
    }

    const int sent = sendto(s_Client, reinterpret_cast<const char*>(&packet), DataLength, 0,
        reinterpret_cast<sockaddr*>(&AddrServer), sizeof(AddrServer));

    if (sent == SOCKET_ERROR || sent != DataLength) {
        Diagnostics::Error("[KMBOX-NET] sendto failed for %s. sent=%d expected=%d WSA=%d",
            ToString(Type), sent, DataLength, WSAGetLastError());
        if (IsOutputCommand(Type)) {
            Diagnostics::Aim("udp.send failure type=%s sent=%d expected=%d wsa=%d",
                ToString(Type),
                sent,
                DataLength,
                WSAGetLastError());
        }
        return err_net_tx;
    }
    if (IsOutputCommand(Type)) {
        Diagnostics::Aim("udp.send sent_bytes type=%s sent=%d expected=%d",
            ToString(Type),
            sent,
            DataLength);
    }

    client_data receiveData{};
    SOCKADDR_IN fromClient{};
    int fromLen = sizeof(fromClient);
    const int received = recvfrom(s_Client, reinterpret_cast<char*>(&receiveData),
        sizeof(receiveData), 0, reinterpret_cast<sockaddr*>(&fromClient), &fromLen);

    if (received < 0) {
        Diagnostics::Error("[KMBOX-NET] recvfrom timeout/failure for %s. WSA=%d",
            ToString(Type), WSAGetLastError());
        if (IsOutputCommand(Type)) {
            Diagnostics::Aim("udp.recv failure type=%s received=%d wsa=%d",
                ToString(Type),
                received,
                WSAGetLastError());
        }
        return err_net_rx_timeout;
    }

    {
        std::lock_guard<std::mutex> dataLock(dataMutex);
        ReceiveData = receiveData;
        PostData = packet;
    }

    if (receiveData.head.cmd != packet.head.cmd) {
        Diagnostics::Error("[KMBOX-NET] command echo mismatch for %s. sent=0x%08X recv=0x%08X",
            ToString(Type), packet.head.cmd, receiveData.head.cmd);
        if (IsOutputCommand(Type)) {
            Diagnostics::Aim("udp.recv mismatch type=%s reason=cmd sent=0x%08X recv=0x%08X",
                ToString(Type),
                packet.head.cmd,
                receiveData.head.cmd);
        }
        return err_net_cmd;
    }

    if (receiveData.head.indexpts != packet.head.indexpts) {
        Diagnostics::Error("[KMBOX-NET] packet sequence mismatch for %s. sent=%u recv=%u",
            ToString(Type), packet.head.indexpts, receiveData.head.indexpts);
        if (IsOutputCommand(Type)) {
            Diagnostics::Aim("udp.recv mismatch type=%s reason=pts sent=%u recv=%u",
                ToString(Type),
                packet.head.indexpts,
                receiveData.head.indexpts);
        }
        return err_net_pts;
    }

    if (IsOutputCommand(Type)) {
        Diagnostics::Aim("udp.recv ack type=%s received=%d cmd=0x%08X pts=%u",
            ToString(Type),
            received,
            receiveData.head.cmd,
            receiveData.head.indexpts);
    }
    return success;
}

int KmBoxNetManager::SendPacketWithRetry(client_data& packet, int DataLength, KmBoxCommandType Type)
{
    int status = err_net_tx;
    for (int attempt = 1; attempt <= KmBoxRuntimeConfig::CommandMaxRetries; ++attempt) {
        status = SendPacketOnce(packet, DataLength, Type);
        if (status == success)
            return success;

        Diagnostics::Error("[KMBOX-NET] %s command failed on attempt %d/%d. status=%d",
            ToString(Type), attempt, KmBoxRuntimeConfig::CommandMaxRetries, status);

        if (attempt < KmBoxRuntimeConfig::CommandMaxRetries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(
                KmBoxRuntimeConfig::CommandRetryBackoffMs * attempt));
        }
    }

    SetConnectionState(KmBoxConnectionState::Error);
    return status;
}

int KmBoxNetManager::ConnectLocked()
{
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        if (configuredIp.empty() || configuredPort == 0 || configuredMac.empty())
            return err_creat_socket;
    }

    SetConnectionState(KmBoxConnectionState::Connecting);
    if (!OpenSocket()) {
        SetConnectionState(KmBoxConnectionState::Error);
        return err_creat_socket;
    }

    client_data packet{};
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        PostData.head.rand = randomEngine();
        PostData.head.indexpts = 0;
        PostData.head.cmd = cmd_connect;
        packet = PostData;
    }

    const int status = SendPacketWithRetry(packet, sizeof(cmd_head_t), KmBoxCommandType::Connect);
    if (status == success) {
        SetConnectionState(KmBoxConnectionState::Connected);
    } else {
        Diagnostics::Error("[KMBOX-NET] Device connect failed. status=%d", status);
        CloseSocket();
        SetConnectionState(KmBoxConnectionState::Error);
    }

    return status;
}

bool KmBoxNetManager::EnsureConnected()
{
    if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Connected)
        return true;

    std::lock_guard<std::mutex> lock(reconnectMutex);
    if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Connected)
        return true;

    const int status = ConnectLocked();
    if (status != success) {
        std::this_thread::sleep_for(std::chrono::milliseconds(KmBoxRuntimeConfig::ReconnectBackoffMs));
        return false;
    }

    return true;
}

void KmBoxNetManager::StartWorkers()
{
    if (!queueRunning.exchange(true, std::memory_order_acq_rel)) {
        queueThread = std::thread(&KmBoxNetManager::QueueWorkerLoop, this);
    }
    if (!heartbeatRunning.exchange(true, std::memory_order_acq_rel)) {
        heartbeatThread = std::thread(&KmBoxNetManager::HeartbeatLoop, this);
    }
}

void KmBoxNetManager::StopWorkers()
{
    if (queueRunning.exchange(false, std::memory_order_acq_rel)) {
        queueCv.notify_all();
        if (queueThread.joinable())
            queueThread.join();
    }
    if (heartbeatRunning.exchange(false, std::memory_order_acq_rel)) {
        if (heartbeatThread.joinable())
            heartbeatThread.join();
    }
}

int KmBoxNetManager::EnqueueCommand(const KmBoxQueuedNetCommand& Command)
{
    if (configuredIp.empty()) {
        if (IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue early_return reason=missing_configured_ip type=%s x=%d y=%d button=%d",
                ToString(Command.type),
                Command.data.cmd_mouse.x,
                Command.data.cmd_mouse.y,
                Command.data.cmd_mouse.button);
        }
        return err_creat_socket;
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue request type=%s queue_size_before=%zu x=%d y=%d button=%d len=%d",
                ToString(Command.type),
                commandQueue.size(),
                Command.data.cmd_mouse.x,
                Command.data.cmd_mouse.y,
                Command.data.cmd_mouse.button,
                Command.length);
        }

        if ((Command.type == KmBoxCommandType::MouseMove ||
             Command.type == KmBoxCommandType::MouseAutoMove) &&
            !commandQueue.empty()) {
            KmBoxQueuedNetCommand& back = commandQueue.back();
            if (back.type == Command.type) {
                back.data.cmd_mouse.x += Command.data.cmd_mouse.x;
                back.data.cmd_mouse.y += Command.data.cmd_mouse.y;
                if (Command.type == KmBoxCommandType::MouseAutoMove)
                    back.data.head.rand = Command.data.head.rand;
                Diagnostics::Trace("[KMBOX-NET] coalesced %s dx=%d dy=%d",
                    ToString(Command.type), back.data.cmd_mouse.x, back.data.cmd_mouse.y);
                Diagnostics::Aim("udp.enqueue coalesced type=%s merged_x=%d merged_y=%d runtimeMs=%u queue_size=%zu",
                    ToString(Command.type),
                    back.data.cmd_mouse.x,
                    back.data.cmd_mouse.y,
                    Command.type == KmBoxCommandType::MouseAutoMove ? back.data.head.rand : 0,
                    commandQueue.size());
                return success;
            }
        }

        if (commandQueue.size() >= KmBoxRuntimeConfig::CommandQueueMaxSize) {
            Diagnostics::Error("[KMBOX-NET] Command queue full; dropping oldest command.");
            KmBoxQueuedNetCommand dropped{};
            bool droppedMove = false;
            if (ShouldDropMouseMoveFirst(Command.type))
                droppedMove = DropOldestMouseMove(commandQueue, dropped);
            if (!droppedMove && !commandQueue.empty()) {
                dropped = commandQueue.front();
                commandQueue.pop_front();
            }
            Diagnostics::Aim("udp.enqueue drop_oldest reason=queue_full prefer_move=%d dropped_type=%s dropped_x=%d dropped_y=%d dropped_button=%d queue_size=%zu",
                ShouldDropMouseMoveFirst(Command.type) ? 1 : 0,
                ToString(dropped.type),
                dropped.data.cmd_mouse.x,
                dropped.data.cmd_mouse.y,
                dropped.data.cmd_mouse.button,
                commandQueue.size());
        }

        if (ShouldPrioritizeAheadOfQueuedMoves(Command.type)) {
            auto insertAt = commandQueue.end();
            while (insertAt != commandQueue.begin()) {
                auto previous = insertAt;
                --previous;
                if (!IsMouseMoveCommand(previous->type))
                    break;
                insertAt = previous;
            }

            const auto inserted = commandQueue.insert(insertAt, Command);
            int updatedMoves = 0;
            int currentButtonState = Command.data.cmd_mouse.button;
            if (Command.type == KmBoxCommandType::MouseButton) {
                currentButtonState = Command.mouseButtonStateMask >= 0
                    ? Command.mouseButtonStateMask
                    : Command.data.cmd_mouse.button;
                updatedMoves = UpdateQueuedMouseMoveButtonState(
                    commandQueue, std::next(inserted), currentButtonState);
            }
            Diagnostics::Aim("udp.enqueue prioritized_state type=%s queue_size_after=%zu currentButtonState=0x%02X updatedTrailingMoves=%d",
                ToString(Command.type),
                commandQueue.size(),
                currentButtonState,
                updatedMoves);
        } else {
            commandQueue.push_back(Command);
        }

        if (IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue pushed type=%s queue_size_after=%zu",
                ToString(Command.type),
                commandQueue.size());
        }
    }

    queueCv.notify_one();
    return success;
}

void KmBoxNetManager::QueueWorkerLoop()
{
    auto lastFlush = std::chrono::steady_clock::now();
    while (queueRunning.load(std::memory_order_acquire)) {
        KmBoxQueuedNetCommand command{};
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock,
                std::chrono::milliseconds(KmBoxRuntimeConfig::CommandFlushIntervalMs),
                [this]() {
                    return !queueRunning.load(std::memory_order_acquire) || !commandQueue.empty();
                });

            if (!queueRunning.load(std::memory_order_acquire))
                break;
            if (commandQueue.empty())
                continue;

            command = commandQueue.front();
            commandQueue.pop_front();
            if (IsOutputCommand(command.type)) {
                Diagnostics::Aim("udp.queue pop type=%s queue_size_after_pop=%zu x=%d y=%d button=%d",
                    ToString(command.type),
                    commandQueue.size(),
                    command.data.cmd_mouse.x,
                    command.data.cmd_mouse.y,
                    command.data.cmd_mouse.button);
            }
        }

        const auto flushInterval = std::chrono::milliseconds(FlushIntervalForCommand(command.type));
        const auto elapsed = std::chrono::steady_clock::now() - lastFlush;
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (OW::Config::KmboxOutputSuppressedByMenu() && IsOutputCommand(command.type)) {
            Diagnostics::Aim("udp.queue drop reason=menu_open_suppressed type=%s",
                ToString(command.type));
            continue;
        }

        if (!EnsureConnected()) {
            Diagnostics::Error("[KMBOX-NET] Dropping %s command; device is not connected.",
                ToString(command.type));
            if (IsOutputCommand(command.type)) {
                Diagnostics::Aim("udp.queue drop reason=not_connected type=%s x=%d y=%d button=%d",
                    ToString(command.type),
                    command.data.cmd_mouse.x,
                    command.data.cmd_mouse.y,
                    command.data.cmd_mouse.button);
            }
            continue;
        }

        StampPacketForSend(command.data);
        if (IsOutputCommand(command.type)) {
            Diagnostics::Aim("udp.queue send_start type=%s pts=%u len=%d x=%d y=%d button=%d",
                ToString(command.type),
                command.data.head.indexpts,
                command.length,
                command.data.cmd_mouse.x,
                command.data.cmd_mouse.y,
                command.data.cmd_mouse.button);
        }
        const int status = SendPacketWithRetry(command.data, command.length, command.type);
        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] Dropped %s command after retries. status=%d",
                ToString(command.type), status);
            if (IsOutputCommand(command.type)) {
                Diagnostics::Aim("udp.queue drop reason=send_failed type=%s status=%d",
                    ToString(command.type),
                    status);
            }
            CloseSocket();
            EnsureConnected();
        } else if (IsOutputCommand(command.type)) {
            const unsigned long long count =
                outputSendCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            Diagnostics::Info("[KMBOX-NET] output send count=%llu type=%s",
                count, ToString(command.type));
            Diagnostics::Aim("udp.queue send_success count=%llu type=%s",
                count,
                ToString(command.type));
        }

        lastFlush = std::chrono::steady_clock::now();
    }
}

void KmBoxNetManager::HeartbeatLoop()
{
    while (heartbeatRunning.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(KmBoxRuntimeConfig::HeartbeatIntervalMs));
        if (!heartbeatRunning.load(std::memory_order_acquire))
            break;

        if (OW::Config::KmboxOutputSuppressedByMenu())
            continue;

        if (!EnsureConnected())
            continue;

        client_data packet = BuildPacket(cmd_connect, NextRandom());
        StampPacketForSend(packet);
        const int status = SendPacketWithRetry(packet, sizeof(cmd_head_t), KmBoxCommandType::Heartbeat);
        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] Heartbeat failed; attempting reconnect. status=%d", status);
            CloseSocket();
            EnsureConnected();
        }
    }
}

int KmBoxNetManager::SendSynchronousCommand(unsigned int Cmd, unsigned int RandValue, int DataLength,
    KmBoxCommandType Type, client_data* PacketOverride)
{
    if (Type != KmBoxCommandType::Connect && !EnsureConnected())
        return err_creat_socket;

    client_data packet = PacketOverride ? *PacketOverride : BuildPacket(Cmd, RandValue);
    if (Type != KmBoxCommandType::Connect)
        StampPacketForSend(packet);

    return SendPacketWithRetry(packet, DataLength, Type);
}

int KmBoxNetManager::InitDevice(const std::string& IP, WORD Port, const std::string& Mac)
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData{};

    if (!wsaStarted) {
        const int status = WSAStartup(wVersionRequested, &wsaData);
        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] WSAStartup failed. status=%d", status);
            SetConnectionState(KmBoxConnectionState::Error);
            return err_creat_socket;
        }
        wsaStarted = true;
    }

    try {
        std::lock_guard<std::mutex> lock(dataMutex);
        configuredIp = IP;
        configuredPort = Port;
        configuredMac = Mac;
        PostData = {};
        ReceiveData = {};
        PostData.head.mac = static_cast<unsigned int>(std::stoul(Mac, nullptr, 16));
        PostData.head.rand = randomEngine();
        PostData.head.indexpts = 0;
        PostData.head.cmd = cmd_connect;
    } catch (const std::exception&) {
        Diagnostics::Error("[KMBOX-NET] Invalid device MAC: %s", Mac.c_str());
        SetConnectionState(KmBoxConnectionState::Error);
        return err_net_cmd;
    }

    std::lock_guard<std::mutex> reconnectLock(reconnectMutex);
    const int status = ConnectLocked();
    if (status == success)
        StartWorkers();
    return status;
}

int KmBoxNetManager::SendData(int DataLength)
{
    client_data packet{};
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        packet = PostData;
    }

    return SendPacketWithRetry(packet, DataLength, CommandTypeForCmd(packet.head.cmd));
}

int KmBoxNetManager::RebootDevice()
{
    if (!EnsureConnected())
        return err_creat_socket;

    client_data packet = BuildPacket(cmd_reboot, NextRandom());
    const int status = SendSynchronousCommand(cmd_reboot, packet.head.rand,
        sizeof(cmd_head_t), KmBoxCommandType::Reboot, &packet);

    CloseSocket();
    SetConnectionState(KmBoxConnectionState::Disconnected);
    return status;
}

int KmBoxNetManager::SetConfig(const std::string& IP, WORD Port)
{
    if (!EnsureConnected())
        return err_creat_socket;

    client_data packet = BuildPacket(cmd_setconfig, inet_addr(IP.c_str()));
    packet.u8buff.buff[0] = static_cast<unsigned char>(Port >> 8);
    packet.u8buff.buff[1] = static_cast<unsigned char>(Port >> 0);

    const int status = SendSynchronousCommand(cmd_setconfig, packet.head.rand,
        sizeof(cmd_head_t) + 2, KmBoxCommandType::SetConfig, &packet);
    if (status == success) {
        std::lock_guard<std::mutex> lock(dataMutex);
        configuredIp = IP;
        configuredPort = Port;
    }
    return status;
}

int KmBoxNetManager::NetHandler()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    if (ReceiveData.head.cmd != PostData.head.cmd)
        return err_net_cmd;
    if (ReceiveData.head.indexpts != PostData.head.indexpts)
        return err_net_pts;
    return success;
}

int KmBoxMouse::Move(int x, int y)
{
    Diagnostics::Aim("udp.mouse.move build x=%d y=%d", x, y);
    client_data packet = kmbox::KmBoxMgr.BuildPacket(cmd_mouse_move, kmbox::KmBoxMgr.NextRandom());
    soft_mouse_t payload{};
    {
        std::lock_guard<std::mutex> lock(kmbox::KmBoxMgr.mouseStateMutex);
        payload = this->MouseData;
        payload.x = x;
        payload.y = y;
    }

    memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    command.type = KmBoxCommandType::MouseMove;
    command.enqueuedAt = std::chrono::steady_clock::now();
    const int status = kmbox::KmBoxMgr.EnqueueCommand(command);
    Diagnostics::Aim("udp.mouse.move enqueue_result x=%d y=%d status=%d", x, y, status);
    return status;
}

int KmBoxMouse::Move_Auto(int x, int y, int Runtime)
{
    client_data packet = kmbox::KmBoxMgr.BuildPacket(cmd_mouse_automove, Runtime);
    soft_mouse_t payload{};
    {
        std::lock_guard<std::mutex> lock(kmbox::KmBoxMgr.mouseStateMutex);
        payload = this->MouseData;
        payload.x = x;
        payload.y = y;
    }

    memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    command.type = KmBoxCommandType::MouseAutoMove;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return kmbox::KmBoxMgr.EnqueueCommand(command);
}

int KmBoxNetManager::SetMouseButton(unsigned int Mask, bool Down, unsigned int Cmd, bool Force)
{
    soft_mouse_t payload{};
    int previousStateMask = 0;
    {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        const int current = Mouse.MouseData.button;
        const int next = Down
            ? (current | static_cast<int>(Mask))
            : (current & ~static_cast<int>(Mask));

        if (!Force && next == current) {
            Diagnostics::Trace("[KMBOX-NET] coalesced redundant mouse button state cmd=0x%08X down=%d",
                Cmd, Down ? 1 : 0);
            return success;
        }

        previousStateMask = current;
        Mouse.MouseData.button = next;
        payload = Mouse.MouseData;
    }

    const int stateMask = payload.button;
    const bool rightStateInvolved =
        Cmd == cmd_mouse_right ||
        ((previousStateMask | stateMask) & 0x02) != 0;
    const unsigned int packetCmd = rightStateInvolved ? cmd_mouse_right : Cmd;
    client_data packet = BuildPacket(packetCmd, NextRandom());
    int commandLength = 0;

    if (rightStateInvolved) {
        payload.button = stateMask;
        payload.x = 0;
        payload.y = 0;
        payload.wheel = 0;
        Diagnostics::Aim("udp.mouse.button build cmd=0x%08X down=%d prevMask=0x%02X stateMask=0x%02X payloadButton=%d fullMask=1 force=%d",
            packetCmd,
            Down ? 1 : 0,
            previousStateMask,
            stateMask,
            payload.button,
            Force ? 1 : 0);

        memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));
        commandLength = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    } else {
        const int buttonState = Down ? 1 : 0;
        payload.button = buttonState;
        Diagnostics::Aim("udp.mouse.button build cmd=0x%08X down=%d prevMask=0x%02X stateMask=0x%02X payloadButton=%d fullMask=0 force=%d",
            packetCmd,
            Down ? 1 : 0,
            previousStateMask,
            stateMask,
            payload.button,
            Force ? 1 : 0);

        memcpy_s(&packet.cmd_mouse.button, sizeof(packet.cmd_mouse.button), &buttonState, sizeof(buttonState));
        commandLength = sizeof(cmd_head_t) + sizeof(buttonState);
    }

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = commandLength;
    command.type = KmBoxCommandType::MouseButton;
    command.mouseButtonStateMask = stateMask;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

int KmBoxNetManager::ForceReleaseMouseButtons()
{
    return SetMouseButtonStateMask(0x00, true);
}

int KmBoxNetManager::ForceReleaseMouseButton(int button)
{
    unsigned int releaseMask = 0;
    switch (button) {
    case 0:
        releaseMask = 0x01;
        break;
    case 1:
        releaseMask = 0x02;
        break;
    case 2:
        releaseMask = 0x04;
        break;
    default:
        return err_net_cmd;
    }

    unsigned int nextState = 0;
    {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        nextState = static_cast<unsigned int>(Mouse.MouseData.button) & ~releaseMask;
    }
    return SetMouseButtonStateMask(nextState, true);
}

int KmBoxNetManager::SetMouseButtonStateMask(unsigned int StateMask, bool Force)
{
    const int next = static_cast<int>(StateMask & 0x07u);
    soft_mouse_t payload{};
    int previousStateMask = 0;
    {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        previousStateMask = Mouse.MouseData.button;
        if (!Force && next == previousStateMask) {
            Diagnostics::Trace("[KMBOX-NET] coalesced redundant mouse button state mask=0x%02X",
                next);
            return success;
        }

        Mouse.MouseData.button = next;
        payload = Mouse.MouseData;
    }

    payload.button = next;
    payload.x = 0;
    payload.y = 0;
    payload.wheel = 0;

    client_data packet = BuildPacket(cmd_mouse_right, NextRandom());
    memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));

    Diagnostics::Aim("udp.mouse.button_state build prevMask=0x%02X stateMask=0x%02X payloadButton=%d force=%d",
        previousStateMask,
        next,
        payload.button,
        Force ? 1 : 0);

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    command.type = KmBoxCommandType::MouseButton;
    command.mouseButtonStateMask = next;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

int KmBoxNetManager::MaskMouse(unsigned int Mask)
{
    const unsigned int effectiveMask = Mask & 0x7Fu;
    if (effectiveMask == 0)
        return success;

    client_data packet = BuildPacket(cmd_mask_mouse, effectiveMask);

    Diagnostics::Aim("udp.mouse.mask build mask=0x%02X", effectiveMask);

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t);
    command.type = KmBoxCommandType::MouseMask;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

int KmBoxNetManager::UnmaskAll()
{
    client_data packet = BuildPacket(cmd_unmask_all, NextRandom());

    Diagnostics::Aim("udp.mouse.unmask_all build");

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t);
    command.type = KmBoxCommandType::MouseUnmask;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

int KmBoxNetManager::SendKeyboardKey(unsigned char hidCode, bool down)
{
    client_data packet = BuildPacket(cmd_keyboard_all, NextRandom());
    soft_keyboard_t kb{};
    if (down) {
        kb.button[0] = static_cast<char>(hidCode);
    }

    memcpy_s(&packet.cmd_keyboard, sizeof(soft_keyboard_t), &kb, sizeof(soft_keyboard_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_keyboard_t);
    command.type = KmBoxCommandType::Keyboard;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

int KmBoxMouse::Left(bool Down)
{
    return kmbox::KmBoxMgr.SetMouseButton(0x01, Down, cmd_mouse_left);
}

int KmBoxMouse::Right(bool Down)
{
    return kmbox::KmBoxMgr.SetMouseButton(0x02, Down, cmd_mouse_right);
}

int KmBoxMouse::Middle(bool Down)
{
    return kmbox::KmBoxMgr.SetMouseButton(0x04, Down, cmd_mouse_middle);
}

void KmBoxKeyBoard::ListenThread()
{
    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData{};
    int Status = WSAStartup(wVersionRequested, &wsaData);
    if (Status != success) {
        Diagnostics::Error("[KMBOX-NET] Monitor WSAStartup failed. status=%d", Status);
        {
            std::lock_guard<std::mutex> lock(this->monitorMutex);
            this->monitorStartStatus = err_creat_socket;
            this->monitorStartResolved = true;
        }
        this->monitorStartCv.notify_all();
        return;
    }

    this->s_ListenSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (!IsValidSocket(this->s_ListenSocket)) {
        Diagnostics::Error("[KMBOX-NET] Monitor socket creation failed. WSA=%d", WSAGetLastError());
        {
            std::lock_guard<std::mutex> lock(this->monitorMutex);
            this->monitorStartStatus = err_creat_socket;
            this->monitorStartResolved = true;
        }
        this->monitorStartCv.notify_all();
        WSACleanup();
        return;
    }
    DisableUdpConnectionReset(this->s_ListenSocket, "monitor");

    sockaddr_in AddrServer{};
    AddrServer.sin_family = PF_INET;
    AddrServer.sin_addr.S_un.S_addr = INADDR_ANY;
    AddrServer.sin_port = htons(this->MonitorPort);

    Status = bind(this->s_ListenSocket, reinterpret_cast<SOCKADDR*>(&AddrServer), sizeof(SOCKADDR));
    if (Status == SOCKET_ERROR) {
        Diagnostics::Error("[KMBOX-NET] Monitor bind failed on port %u. WSA=%d",
            this->MonitorPort, WSAGetLastError());
        closesocket(this->s_ListenSocket);
        this->s_ListenSocket = 0;
        {
            std::lock_guard<std::mutex> lock(this->monitorMutex);
            this->monitorStartStatus = err_net_rx_timeout;
            this->monitorStartResolved = true;
        }
        this->monitorStartCv.notify_all();
        WSACleanup();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(this->monitorMutex);
        this->monitorStartStatus = success;
        this->monitorStartResolved = true;
    }
    this->monitorStartCv.notify_all();
    Diagnostics::Info("[KMBOX-NET] monitor listener bound. port=%u", this->MonitorPort);

    SOCKADDR AddrClient{};
    char Buffer[1024]{};

    this->ListenerRuned.store(true, std::memory_order_release);
    while (this->ListenerRuned.load(std::memory_order_acquire)) {
        int FromLen = sizeof(SOCKADDR);
        const int Ret = recvfrom(this->s_ListenSocket, Buffer, sizeof(Buffer), 0, &AddrClient, &FromLen);
        if (Ret >= static_cast<int>(sizeof(this->hw_Mouse) + sizeof(this->hw_Keyboard))) {
            unsigned char previousButtons = 0;
            unsigned char currentButtons = 0;
            standard_keyboard_report_t previousKeyboard{};
            standard_keyboard_report_t currentKeyboard{};
            std::lock_guard<std::mutex> lock(this->monitorMutex);
            previousButtons = this->hw_Mouse.buttons;
            previousKeyboard = this->hw_Keyboard;
            memcpy(&this->hw_Mouse, Buffer, sizeof(this->hw_Mouse));
            memcpy(&this->hw_Keyboard, &Buffer[sizeof(this->hw_Mouse)], sizeof(this->hw_Keyboard));
            currentButtons = this->hw_Mouse.buttons;
            currentKeyboard = this->hw_Keyboard;

            const unsigned long long packetCount =
                this->inputPacketCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (packetCount == 1) {
                char keys[64] = {};
                FormatKeyboardKeys(currentKeyboard, keys, sizeof(keys));
                Diagnostics::Info("[KMBOX-NET] first input packet received. bytes=%d mouse_buttons=0x%02X keyboard_modifiers=0x%02X keyboard_keys=%s",
                    Ret,
                    static_cast<unsigned int>(currentButtons),
                    static_cast<unsigned int>(currentKeyboard.buttons),
                    keys);
            }

            if (previousButtons != currentButtons) {
                this->lastLoggedMouseButtons.store(currentButtons, std::memory_order_release);
                LogMouseButtonChanges(previousButtons, currentButtons);
            }
            LogKeyboardReportChange(previousKeyboard, currentKeyboard);
        } else if (Ret <= 0) {
            break;
        }
    }
    this->ListenerRuned.store(false, std::memory_order_release);
    if (IsValidSocket(this->s_ListenSocket))
        closesocket(this->s_ListenSocket);
    this->s_ListenSocket = 0;
    WSACleanup();
}

int KmBoxKeyBoard::StartMonitor(WORD Port)
{
    Diagnostics::Info("[KMBOX-NET] monitor start requested. port=%u", Port);

    if (Port == 0) {
        EndMonitor();
        Diagnostics::Info("[KMBOX-NET] monitor disabled by port 0.");
        return success;
    }

    if (!kmbox::KmBoxMgr.EnsureConnected()) {
        Diagnostics::Info("[KMBOX-NET] monitor start failed. port=%u status=%d",
            Port, err_creat_socket);
        return err_creat_socket;
    }

    EndMonitor();

    this->MonitorPort = Port;
    this->ListenerRuned.store(false, std::memory_order_release);
    this->inputPacketCount.store(0, std::memory_order_release);
    this->lastLoggedMouseButtons.store(0, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(this->monitorMutex);
        this->hw_Mouse = {};
        this->hw_Keyboard = {};
        this->monitorStartStatus = err_creat_socket;
        this->monitorStartResolved = false;
    }

    this->t_Listen = std::thread(&KmBoxKeyBoard::ListenThread, this);

    int listenerStatus = success;
    {
        std::unique_lock<std::mutex> lock(this->monitorMutex);
        if (!this->monitorStartCv.wait_for(lock, std::chrono::milliseconds(500),
            [this]() { return this->monitorStartResolved; })) {
            Diagnostics::Info("[KMBOX-NET] monitor start failed. port=%u status=%d",
                Port, err_net_rx_timeout);
            listenerStatus = err_net_rx_timeout;
        } else {
            listenerStatus = this->monitorStartStatus;
        }
    }

    if (listenerStatus != success) {
        Diagnostics::Info("[KMBOX-NET] monitor listener failed. port=%u status=%d",
            Port, listenerStatus);
        EndMonitor();
        return listenerStatus;
    }

    client_data packet = kmbox::KmBoxMgr.BuildPacket(
        cmd_monitor,
        static_cast<unsigned int>(Port) | (0xaa55u << 16));
    const int status = kmbox::KmBoxMgr.SendSynchronousCommand(cmd_monitor, packet.head.rand,
        sizeof(cmd_head_t), KmBoxCommandType::Monitor, &packet);
    if (status != success) {
        Diagnostics::Info("[KMBOX-NET] monitor command failed. port=%u status=%d", Port, status);
        EndMonitor();
        return status;
    }

    Diagnostics::Info("[KMBOX-NET] monitor start succeeded. port=%u", Port);
    return success;
}

void KmBoxKeyBoard::EndMonitor()
{
    this->ListenerRuned.store(false, std::memory_order_release);

    if (IsValidSocket(this->s_ListenSocket))
        closesocket(this->s_ListenSocket);

    this->s_ListenSocket = 0;
    this->MonitorPort = 0;

    if (this->t_Listen.joinable())
        this->t_Listen.join();
}

unsigned long long KmBoxKeyBoard::InputPacketCount() const
{
    return this->inputPacketCount.load(std::memory_order_acquire);
}

KmBoxKeyBoard::~KmBoxKeyBoard()
{
    this->EndMonitor();
}

bool KmBoxKeyBoard::GetKeyState(WORD vKey)
{
    const WORD hidKey = VirtualKeyToHidKey(vKey);
    if (hidKey == 0)
        return false;

    unsigned char KeyValue = hidKey & 0xff;
    if (!this->ListenerRuned.load(std::memory_order_acquire))
        return false;

    standard_keyboard_report_t keyboard{};
    {
        std::lock_guard<std::mutex> lock(this->monitorMutex);
        keyboard = this->hw_Keyboard;
    }

    if (KeyValue >= KEY_LEFTCONTROL && KeyValue <= KEY_RIGHT_GUI) {
        switch (KeyValue) {
        case KEY_LEFTCONTROL: return  keyboard.buttons & BIT0 ? 1 : 0;
        case KEY_LEFTSHIFT:   return  keyboard.buttons & BIT1 ? 1 : 0;
        case KEY_LEFTALT:     return  keyboard.buttons & BIT2 ? 1 : 0;
        case KEY_LEFT_GUI:    return  keyboard.buttons & BIT3 ? 1 : 0;
        case KEY_RIGHTCONTROL:return  keyboard.buttons & BIT4 ? 1 : 0;
        case KEY_RIGHTSHIFT:  return  keyboard.buttons & BIT5 ? 1 : 0;
        case KEY_RIGHTALT:    return  keyboard.buttons & BIT6 ? 1 : 0;
        case KEY_RIGHT_GUI:   return  keyboard.buttons & BIT7 ? 1 : 0;
        default:              return false;
        }
    }

    for (auto i : keyboard.data) {
        if (i == KeyValue)
            return true;
    }
    return false;
}

bool KmBoxKeyBoard::IsMouseButtonPressed(int vkButton)
{
    if (!this->ListenerRuned.load(std::memory_order_acquire))
        return false;

    standard_mouse_report_t mouse{};
    {
        std::lock_guard<std::mutex> lock(this->monitorMutex);
        mouse = this->hw_Mouse;
    }

    switch (vkButton) {
    case VK_LBUTTON:  return (mouse.buttons & 0x01) != 0;
    case VK_RBUTTON:  return (mouse.buttons & 0x02) != 0;
    case VK_MBUTTON:  return (mouse.buttons & 0x04) != 0;
    case VK_XBUTTON1: return (mouse.buttons & 0x08) != 0;
    case VK_XBUTTON2: return (mouse.buttons & 0x10) != 0;
    default:          return false;
    }
}
