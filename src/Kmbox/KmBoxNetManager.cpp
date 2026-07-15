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
#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <vector>

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
        if (!OW::Config::kmboxDebugLog)
            return;

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
        if (!OW::Config::kmboxDebugLog)
            return;

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
            type == KmBoxCommandType::Keyboard ||
            type == KmBoxCommandType::SafetyReleaseAll;
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

    bool IsOrderedSafetyCommand(const KmBoxQueuedNetCommand& command)
    {
        return command.outputIntent == KmBoxOutputIntent::SafetyRelease &&
            command.type != KmBoxCommandType::SafetyReleaseAll;
    }

    bool IsSameOutputStateDomain(KmBoxCommandType left, KmBoxCommandType right)
    {
        if (left == KmBoxCommandType::Keyboard ||
            right == KmBoxCommandType::Keyboard) {
            return left == KmBoxCommandType::Keyboard &&
                right == KmBoxCommandType::Keyboard;
        }
        if (left == KmBoxCommandType::MouseButton ||
            right == KmBoxCommandType::MouseButton) {
            return left == KmBoxCommandType::MouseButton &&
                right == KmBoxCommandType::MouseButton;
        }
        const bool leftMask = left == KmBoxCommandType::MouseMask ||
            left == KmBoxCommandType::MouseUnmask;
        const bool rightMask = right == KmBoxCommandType::MouseMask ||
            right == KmBoxCommandType::MouseUnmask;
        return leftMask && rightMask;
    }

    bool IsRequiredOrderedSafetyPredecessor(
        const std::deque<KmBoxQueuedNetCommand>& queue,
        std::deque<KmBoxQueuedNetCommand>::const_iterator candidate,
        KmBoxCommandType incomingSafetyDomain)
    {
        const auto hasLaterNormalInDomain = [&](auto end) {
            return std::any_of(std::next(candidate), end,
                [candidate](const KmBoxQueuedNetCommand& queued) {
                    return queued.priority == KmBoxCommandPriority::Normal &&
                        IsSameOutputStateDomain(candidate->type, queued.type);
                });
        };

        for (auto safety = std::next(candidate); safety != queue.end(); ++safety) {
            if (!IsOrderedSafetyCommand(*safety) ||
                !IsSameOutputStateDomain(candidate->type, safety->type)) {
                continue;
            }
            if (!hasLaterNormalInDomain(safety))
                return true;
        }

        return incomingSafetyDomain != KmBoxCommandType::Unknown &&
            IsSameOutputStateDomain(candidate->type, incomingSafetyDomain) &&
            !hasLaterNormalInDomain(queue.end());
    }

    bool HasDependentOrderedSafety(
        const std::deque<KmBoxQueuedNetCommand>& queue,
        std::deque<KmBoxQueuedNetCommand>::const_iterator candidate)
    {
        return std::any_of(std::next(candidate), queue.end(),
            [candidate](const KmBoxQueuedNetCommand& queued) {
                return IsOrderedSafetyCommand(queued) &&
                    IsSameOutputStateDomain(candidate->type, queued.type);
            });
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
                return queued.priority == KmBoxCommandPriority::Normal &&
                    IsMouseMoveCommand(queued.type);
            });
        if (item == queue.end())
            return false;

        dropped = *item;
        queue.erase(item);
        return true;
    }

    bool DropOldestNormalCommand(
        std::deque<KmBoxQueuedNetCommand>& queue,
        KmBoxQueuedNetCommand& dropped,
        KmBoxCommandType incomingSafetyDomain = KmBoxCommandType::Unknown)
    {
        auto item = queue.begin();
        for (; item != queue.end(); ++item) {
            if (item->priority != KmBoxCommandPriority::Normal)
                continue;
            if (incomingSafetyDomain == KmBoxCommandType::Unknown
                    ? HasDependentOrderedSafety(queue, item)
                    : IsRequiredOrderedSafetyPredecessor(
                        queue, item, incomingSafetyDomain)) {
                continue;
            }
            break;
        }
        if (item == queue.end())
            return false;

        dropped = *item;
        queue.erase(item);
        return true;
    }

    void CompleteCommand(
        const std::shared_ptr<KmBoxCommandCompletion>& completion,
        int status)
    {
        if (!completion)
            return;

        {
            std::lock_guard<std::mutex> lock(completion->mutex);
            if (completion->completed)
                return;
            completion->status = status;
            completion->completed = true;
        }
        completion->cv.notify_all();
    }

    int FlushIntervalForCommand(KmBoxCommandType type)
    {
        if (type == KmBoxCommandType::MouseMove ||
            type == KmBoxCommandType::MouseAutoMove) {
            return KmBoxRuntimeConfig::MouseMoveFlushIntervalMs;
        }

        const bool latencySensitive =
            type == KmBoxCommandType::MouseButton ||
            type == KmBoxCommandType::MouseMask ||
            type == KmBoxCommandType::MouseUnmask ||
            type == KmBoxCommandType::Keyboard ||
            type == KmBoxCommandType::SafetyReleaseAll;

        return latencySensitive
            ? KmBoxRuntimeConfig::MouseButtonFlushIntervalMs
            : KmBoxRuntimeConfig::CommandFlushIntervalMs;
    }

    void RecordFirstFailure(int& status, int result)
    {
        if (status == success && result != success)
            status = result;
    }

    bool KeyboardReportIsSubset(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        unsigned char currentModifierMask,
        const std::array<unsigned char, 10>& currentUsages)
    {
        if ((modifierMask & static_cast<unsigned char>(~currentModifierMask)) != 0)
            return false;

        return std::all_of(usages.begin(), usages.end(),
            [&currentUsages](unsigned char usage) {
                return std::find(
                    currentUsages.begin(), currentUsages.end(), usage) !=
                    currentUsages.end();
            });
    }

    bool KeyboardPacketIsSubset(
        const soft_keyboard_t& report,
        unsigned char currentModifierMask,
        const std::array<unsigned char, 10>& currentUsages)
    {
        const auto modifierMask = static_cast<unsigned char>(report.ctrl);
        if ((modifierMask & static_cast<unsigned char>(~currentModifierMask)) != 0)
            return false;

        for (char rawUsage : report.button) {
            const auto usage = static_cast<unsigned char>(rawUsage);
            if (usage == 0)
                continue;
            if (std::find(currentUsages.begin(), currentUsages.end(), usage) ==
                currentUsages.end()) {
                return false;
            }
        }
        return true;
    }
}

KmBoxNetManager::KmBoxNetManager()
    : randomEngine(static_cast<unsigned int>(
          std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

KmBoxNetManager::~KmBoxNetManager()
{
    (void)Shutdown();
}

KmBoxConnectionState KmBoxNetManager::GetConnectionState() const
{
    return connectionState.load(std::memory_order_acquire);
}

KmBoxNetManager::LifecycleState KmBoxNetManager::GetLifecycleState() const
{
    return lifecycleState.load(std::memory_order_acquire);
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
    if (!LifecycleAllowsReconnect())
        return err_queue_stopped;

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

    int status = SendPacketWithRetry(packet, sizeof(cmd_head_t), KmBoxCommandType::Connect);
    if (status == success && LifecycleAllowsReconnect()) {
        SetConnectionState(KmBoxConnectionState::Connected);
    } else {
        if (status == success)
            status = err_queue_stopped;
        Diagnostics::Error("[KMBOX-NET] Device connect failed or stopped. status=%d", status);
        CloseSocket();
        SetConnectionState(KmBoxConnectionState::Error);
    }

    return status;
}

bool KmBoxNetManager::EnsureConnected()
{
    if (!LifecycleAllowsReconnect())
        return false;
    if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Connected)
        return true;

    std::lock_guard<std::mutex> lock(reconnectMutex);
    if (!LifecycleAllowsReconnect())
        return false;
    if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Connected)
        return true;

    const int status = ConnectLocked();
    if (status != success) {
        if (LifecycleAllowsReconnect()) {
            std::unique_lock<std::mutex> waitLock(heartbeatMutex);
            heartbeatCv.wait_for(waitLock,
                std::chrono::milliseconds(KmBoxRuntimeConfig::ReconnectBackoffMs),
                [this]() { return !LifecycleAllowsReconnect(); });
        }
        return false;
    }

    return true;
}

bool KmBoxNetManager::LifecycleAllowsReconnect() const
{
    const LifecycleState state = lifecycleState.load(std::memory_order_acquire);
    return state == LifecycleState::Starting || state == LifecycleState::Running;
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
    queueRunning.store(false, std::memory_order_release);
    heartbeatRunning.store(false, std::memory_order_release);
    queueCv.notify_all();
    heartbeatCv.notify_all();

    const std::thread::id current = std::this_thread::get_id();
    if (queueThread.joinable() && queueThread.get_id() != current)
        queueThread.join();
    if (heartbeatThread.joinable() && heartbeatThread.get_id() != current)
        heartbeatThread.join();
}

void KmBoxNetManager::CompletePendingCommands(int Status)
{
    std::deque<KmBoxQueuedNetCommand> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pending.swap(commandQueue);
    }
    for (const auto& command : pending)
        CompleteCommand(command.completion, Status);
}

void KmBoxNetManager::ClearDesiredKeyboardReport()
{
    std::lock_guard<std::mutex> lock(keyboardStateMutex);
    desiredKeyboardModifierMask = 0;
    desiredKeyboardUsages.fill(0);
}

void KmBoxNetManager::ClearLastSentKeyboardReport()
{
    std::lock_guard<std::mutex> lock(keyboardStateMutex);
    lastSentKeyboardModifierMask = 0;
    lastSentKeyboardUsages.fill(0);
}

int KmBoxNetManager::EnqueueCommand(const KmBoxQueuedNetCommand& Command)
{
    if (Command.outputIntent == KmBoxOutputIntent::SafetyRelease &&
        Command.priority != KmBoxCommandPriority::Safety) {
        KmBoxQueuedNetCommand promoted = Command;
        promoted.priority = KmBoxCommandPriority::Safety;
        return EnqueueCommand(promoted);
    }

    const bool releaseAllBarrier =
        Command.type == KmBoxCommandType::SafetyReleaseAll;
    const bool orderedSafety = IsOrderedSafetyCommand(Command);
    if (orderedSafety &&
        Command.type != KmBoxCommandType::MouseButton &&
        Command.type != KmBoxCommandType::MouseUnmask &&
        Command.type != KmBoxCommandType::Keyboard) {
        CompleteCommand(Command.completion, err_net_cmd);
        return err_net_cmd;
    }

    if (configuredIp.empty()) {
        if (IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue early_return reason=missing_configured_ip type=%s x=%d y=%d button=%d",
                ToString(Command.type),
                Command.data.cmd_mouse.x,
                Command.data.cmd_mouse.y,
                Command.data.cmd_mouse.button);
        }
        CompleteCommand(Command.completion, err_creat_socket);
        return err_creat_socket;
    }

    if ((Command.completion || Command.priority == KmBoxCommandPriority::Safety) &&
        !queueRunning.load(std::memory_order_acquire)) {
        CompleteCommand(Command.completion, err_queue_stopped);
        return err_queue_stopped;
    }

    std::shared_ptr<KmBoxCommandCompletion> droppedCompletion;
    std::vector<std::shared_ptr<KmBoxCommandCompletion>> cancelledCompletions;
    int enqueueStatus = success;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const LifecycleState lifecycle = lifecycleState.load(std::memory_order_acquire);
        const bool offlineTestSeam =
            lifecycle == LifecycleState::Stopped &&
            queueRunning.load(std::memory_order_acquire) &&
            static_cast<bool>(safetySendOverride);
        if (Command.priority == KmBoxCommandPriority::Normal &&
            lifecycle != LifecycleState::Running && !offlineTestSeam) {
            enqueueStatus = err_queue_stopped;
        }
        if ((Command.completion || Command.priority == KmBoxCommandPriority::Safety) &&
            !queueRunning.load(std::memory_order_acquire)) {
            enqueueStatus = err_queue_stopped;
        }
        if (IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue request type=%s queue_size_before=%zu x=%d y=%d button=%d len=%d",
                ToString(Command.type),
                commandQueue.size(),
                Command.data.cmd_mouse.x,
                Command.data.cmd_mouse.y,
                Command.data.cmd_mouse.button,
                Command.length);
        }

        if (enqueueStatus == success && releaseAllBarrier) {
            for (auto pending = commandQueue.begin(); pending != commandQueue.end();) {
                if (pending->type != KmBoxCommandType::SafetyReleaseAll &&
                    IsOutputCommand(pending->type)) {
                    cancelledCompletions.push_back(pending->completion);
                    pending = commandQueue.erase(pending);
                } else {
                    ++pending;
                }
            }
        }

        if (enqueueStatus == success &&
            Command.priority == KmBoxCommandPriority::Normal &&
            !Command.completion &&
            (Command.type == KmBoxCommandType::MouseMove ||
             Command.type == KmBoxCommandType::MouseAutoMove) &&
            !commandQueue.empty()) {
            KmBoxQueuedNetCommand& back = commandQueue.back();
            if (back.priority == KmBoxCommandPriority::Normal &&
                !back.completion &&
                back.type == Command.type) {
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

        if (enqueueStatus == success &&
            commandQueue.size() >= KmBoxRuntimeConfig::CommandQueueMaxSize) {
            KmBoxQueuedNetCommand dropped{};
            bool droppedMove = false;
            if ((Command.priority == KmBoxCommandPriority::Normal || orderedSafety) &&
                ShouldDropMouseMoveFirst(Command.type)) {
                droppedMove = DropOldestMouseMove(commandQueue, dropped);
            }
            const bool droppedNormal = droppedMove ||
                DropOldestNormalCommand(
                    commandQueue,
                    dropped,
                    orderedSafety ? Command.type : KmBoxCommandType::Unknown);
            if (!droppedNormal) {
                enqueueStatus = err_queue_full;
                Diagnostics::Error(
                    "[KMBOX-NET] Command queue full; no normal command can be displaced for %s.",
                    ToString(Command.type));
            } else {
                droppedCompletion = dropped.completion;
                Diagnostics::Error("[KMBOX-NET] Command queue full; dropping oldest normal command.");
                Diagnostics::Aim("udp.enqueue drop_oldest reason=queue_full prefer_move=%d dropped_type=%s dropped_x=%d dropped_y=%d dropped_button=%d queue_size=%zu",
                    Command.priority == KmBoxCommandPriority::Normal &&
                        ShouldDropMouseMoveFirst(Command.type) ? 1 : 0,
                    ToString(dropped.type),
                    dropped.data.cmd_mouse.x,
                    dropped.data.cmd_mouse.y,
                    dropped.data.cmd_mouse.button,
                    commandQueue.size());
            }
        }

        if (enqueueStatus != success) {
            // The incoming command was never queued; completion is resolved below.
        } else if (releaseAllBarrier) {
            const auto insertAt = std::find_if(commandQueue.begin(), commandQueue.end(),
                [](const KmBoxQueuedNetCommand& queued) {
                    return queued.priority == KmBoxCommandPriority::Normal;
                });
            commandQueue.insert(insertAt, Command);
            Diagnostics::Aim(
                "udp.enqueue prioritized_safety type=%s queue_size_after=%zu cancelled_normal_outputs=%zu",
                ToString(Command.type),
                commandQueue.size(),
                cancelledCompletions.size());
        } else if (ShouldPrioritizeAheadOfQueuedMoves(Command.type)) {
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

        if (enqueueStatus == success && IsOutputCommand(Command.type)) {
            Diagnostics::Aim("udp.enqueue pushed type=%s queue_size_after=%zu",
                ToString(Command.type),
                commandQueue.size());
        }
    }

    for (const auto& completion : cancelledCompletions)
        CompleteCommand(completion, err_queue_dropped);
    CompleteCommand(droppedCompletion, err_queue_dropped);
    if (enqueueStatus != success) {
        CompleteCommand(Command.completion, enqueueStatus);
        return enqueueStatus;
    }

    queueCv.notify_one();
    return success;
}

int KmBoxNetManager::ExecuteSafetyReleaseAll()
{
    const auto restoreFailedDesiredDomains =
        [this](bool restoreMouse, bool restoreKeyboard) {
            if (restoreMouse) {
                std::lock_guard<std::mutex> lock(mouseStateMutex);
                if (Mouse.MouseData.button == 0) {
                    Mouse.MouseData.button = static_cast<int>(
                        lastSentMouseButtonStateMask & 0x7u);
                }
            }
            if (restoreKeyboard) {
                std::lock_guard<std::mutex> lock(keyboardStateMutex);
                const bool desiredIsZero =
                    desiredKeyboardModifierMask == 0 &&
                    std::all_of(
                        desiredKeyboardUsages.begin(),
                        desiredKeyboardUsages.end(),
                        [](unsigned char usage) { return usage == 0; });
                if (desiredIsZero) {
                    desiredKeyboardModifierMask = lastSentKeyboardModifierMask;
                    desiredKeyboardUsages = lastSentKeyboardUsages;
                }
            }
        };

    int status = success;
    SOCKET releaseSocket = INVALID_SOCKET;
    SOCKADDR_IN releaseTarget{};
    std::unique_lock<std::mutex> sendLock;

    if (!safetySendOverride) {
        sendLock = std::unique_lock<std::mutex>(sendMutex);
        std::string ip;
        WORD port = 0;
        {
            std::lock_guard<std::mutex> dataLock(dataMutex);
            ip = configuredIp;
            port = configuredPort;
        }
        if (ip.empty() || port == 0) {
            restoreFailedDesiredDomains(true, true);
            return err_creat_socket;
        }

        releaseSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (!IsValidSocket(releaseSocket)) {
            restoreFailedDesiredDomains(true, true);
            return err_creat_socket;
        }
        if (!ConfigureSocketTimeouts(releaseSocket)) {
            closesocket(releaseSocket);
            restoreFailedDesiredDomains(true, true);
            return err_creat_socket;
        }
        DisableUdpConnectionReset(releaseSocket, "safety-release");
        releaseTarget.sin_family = AF_INET;
        releaseTarget.sin_port = htons(port);
        releaseTarget.sin_addr.S_un.S_addr = inet_addr(ip.c_str());
        if (releaseTarget.sin_addr.S_un.S_addr == INADDR_NONE) {
            closesocket(releaseSocket);
            restoreFailedDesiredDomains(true, true);
            return err_creat_socket;
        }
    }

    const auto send = [&](client_data packet, int length, KmBoxCommandType type) {
        StampPacketForSend(packet);
        int sendStatus = success;
        if (safetySendOverride) {
            sendStatus = safetySendOverride(packet.head.cmd, packet, length);
        } else {
            const int sent = sendto(
                releaseSocket,
                reinterpret_cast<const char*>(&packet),
                length,
                0,
                reinterpret_cast<sockaddr*>(&releaseTarget),
                sizeof(releaseTarget));
            if (sent == SOCKET_ERROR || sent != length)
                sendStatus = err_net_tx;
        }
        RecordFirstFailure(status, sendStatus);
        Diagnostics::Aim(
            "udp.safety_release sendto type=%s cmd=0x%08X len=%d status=%d",
            ToString(type), packet.head.cmd, length, sendStatus);
        return sendStatus;
    };

    const auto sendMouseUp = [&](unsigned int cmd, unsigned int buttonMask) {
        client_data packet = BuildPacket(cmd, NextRandom());
        packet.cmd_mouse = {};
        const int sendStatus = send(
            packet,
            sizeof(cmd_head_t) + sizeof(soft_mouse_t),
            KmBoxCommandType::MouseButton);
        if (sendStatus == success) {
            std::lock_guard<std::mutex> lock(mouseStateMutex);
            lastSentMouseButtonStateMask &= ~buttonMask;
        }
        return sendStatus;
    };

    const int leftStatus = sendMouseUp(cmd_mouse_left, 0x01u);
    const int rightStatus = sendMouseUp(cmd_mouse_right, 0x02u);
    const int middleStatus = sendMouseUp(cmd_mouse_middle, 0x04u);

    client_data keyboard = BuildPacket(cmd_keyboard_all, NextRandom());
    keyboard.cmd_keyboard = {};
    const int keyboardStatus = send(
        keyboard,
        sizeof(cmd_head_t) + sizeof(soft_keyboard_t),
        KmBoxCommandType::Keyboard);
    if (keyboardStatus == success)
        ClearLastSentKeyboardReport();

    client_data unmask = BuildPacket(cmd_unmask_all, NextRandom());
    send(unmask, sizeof(cmd_head_t), KmBoxCommandType::MouseUnmask);

    if (IsValidSocket(releaseSocket))
        closesocket(releaseSocket);

    const bool mouseReleaseFailed =
        leftStatus != success ||
        rightStatus != success ||
        middleStatus != success;
    restoreFailedDesiredDomains(
        mouseReleaseFailed,
        keyboardStatus != success);

    Diagnostics::Info(
        "[KMBOX-NET] Safety release sendto sequence completed. status=%d",
        status);
    return status;
}

void KmBoxNetManager::QueueWorkerLoop()
{
    queueThreadId.store(GetCurrentThreadId(), std::memory_order_release);
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

        const bool offlineTestSeam =
            lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopped &&
            static_cast<bool>(safetySendOverride);
        if (command.priority == KmBoxCommandPriority::Normal &&
            lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running &&
            !offlineTestSeam) {
            CompleteCommand(command.completion, err_queue_stopped);
            continue;
        }

        if (IsOutputCommand(command.type) &&
            ShouldSuppressOutputForMenu(
                OW::Config::KmboxOutputSuppressedByMenu(), command.outputIntent)) {
            Diagnostics::Aim("udp.queue drop reason=menu_open_suppressed type=%s intent=%s",
                ToString(command.type),
                ToString(command.outputIntent));
            CompleteCommand(command.completion, err_output_suppressed);
            continue;
        }

        if (command.type == KmBoxCommandType::SafetyReleaseAll) {
            Diagnostics::Aim("udp.queue safety_release_all transport_start");
            const int status = ExecuteSafetyReleaseAll();
            if (status != success) {
                Diagnostics::Error(
                    "[KMBOX-NET] Safety release sendto sequence failed. status=%d",
                    status);
            }
            CompleteCommand(command.completion, status);
            lastFlush = std::chrono::steady_clock::now();
            continue;
        }

        if (IsOrderedSafetyCommand(command)) {
            bool releaseOnly = true;
            if (command.type == KmBoxCommandType::Keyboard) {
                std::lock_guard<std::mutex> lock(keyboardStateMutex);
                releaseOnly = KeyboardPacketIsSubset(
                    command.data.cmd_keyboard,
                    lastSentKeyboardModifierMask,
                    lastSentKeyboardUsages);
            } else if (command.type == KmBoxCommandType::MouseButton) {
                std::lock_guard<std::mutex> lock(mouseStateMutex);
                releaseOnly = command.mouseButtonStateMask >= 0 &&
                    (static_cast<unsigned int>(command.mouseButtonStateMask) &
                     ~lastSentMouseButtonStateMask) == 0;
            }
            if (!releaseOnly) {
                Diagnostics::Warn(
                    "[KMBOX-NET] Rejected safety output that would add unsent state. type=%s",
                    ToString(command.type));
                CompleteCommand(command.completion, err_net_cmd);
                continue;
            }
        }

        const bool useOutputOverride =
            static_cast<bool>(safetySendOverride) && IsOutputCommand(command.type);
        if (!useOutputOverride && !EnsureConnected()) {
            Diagnostics::Error("[KMBOX-NET] Dropping %s command; device is not connected.",
                ToString(command.type));
            if (IsOutputCommand(command.type)) {
                Diagnostics::Aim("udp.queue drop reason=not_connected type=%s x=%d y=%d button=%d",
                    ToString(command.type),
                    command.data.cmd_mouse.x,
                     command.data.cmd_mouse.y,
                     command.data.cmd_mouse.button);
            }
            CompleteCommand(command.completion, err_creat_socket);
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
        const int status = useOutputOverride
            ? safetySendOverride(
                command.data.head.cmd,
                command.data,
                command.length)
            : SendPacketWithRetry(command.data, command.length, command.type);

        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] Dropped %s command after retries. status=%d",
                ToString(command.type), status);
            if (IsOutputCommand(command.type)) {
                Diagnostics::Aim("udp.queue drop reason=send_failed type=%s status=%d",
                    ToString(command.type),
                    status);
            }
            if (!useOutputOverride) {
                CloseSocket();
                EnsureConnected();
            }
        } else if (IsOutputCommand(command.type)) {
            if (command.type == KmBoxCommandType::Keyboard) {
                std::lock_guard<std::mutex> lock(keyboardStateMutex);
                lastSentKeyboardModifierMask =
                    static_cast<unsigned char>(command.data.cmd_keyboard.ctrl);
                for (std::size_t index = 0;
                     index < lastSentKeyboardUsages.size();
                     ++index) {
                    lastSentKeyboardUsages[index] = static_cast<unsigned char>(
                        command.data.cmd_keyboard.button[index]);
                }
            } else if (command.type == KmBoxCommandType::MouseButton ||
                       command.type == KmBoxCommandType::MouseMove ||
                       command.type == KmBoxCommandType::MouseAutoMove) {
                std::lock_guard<std::mutex> lock(mouseStateMutex);
                lastSentMouseButtonStateMask = command.mouseButtonStateMask >= 0
                    ? static_cast<unsigned int>(command.mouseButtonStateMask) & 0x7u
                    : static_cast<unsigned int>(command.data.cmd_mouse.button) & 0x7u;
            }
            const unsigned long long count =
                outputSendCount.fetch_add(1, std::memory_order_acq_rel) + 1;
            if (OW::Config::kmboxDebugLog) {
                Diagnostics::Info("[KMBOX-NET] output send count=%llu type=%s",
                    count, ToString(command.type));
            }
            Diagnostics::Aim("udp.queue send_success count=%llu type=%s",
                count,
                ToString(command.type));
        }

        CompleteCommand(command.completion, status);
        lastFlush = std::chrono::steady_clock::now();
    }
    queueThreadId.store(0, std::memory_order_release);
}

void KmBoxNetManager::HeartbeatLoop()
{
    heartbeatThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    while (heartbeatRunning.load(std::memory_order_acquire)) {
        {
            std::unique_lock<std::mutex> lock(heartbeatMutex);
            heartbeatCv.wait_for(lock,
                std::chrono::milliseconds(KmBoxRuntimeConfig::HeartbeatIntervalMs),
                [this]() {
                    return !heartbeatRunning.load(std::memory_order_acquire);
                });
        }
        if (!heartbeatRunning.load(std::memory_order_acquire))
            break;
        if (lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running)
            continue;

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
    heartbeatThreadId.store(0, std::memory_order_release);
}

bool KmBoxNetManager::IsOwnedWorkerThread() const
{
    const DWORD current = GetCurrentThreadId();
    return current != 0 &&
        (current == queueThreadId.load(std::memory_order_acquire) ||
         current == heartbeatThreadId.load(std::memory_order_acquire) ||
         current == KeyBoard.listenerThreadId.load(std::memory_order_acquire));
}

int KmBoxNetManager::Shutdown(std::chrono::milliseconds Timeout)
{
    if (IsOwnedWorkerThread()) {
        lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
        queueRunning.store(false, std::memory_order_release);
        heartbeatRunning.store(false, std::memory_order_release);
        queueCv.notify_all();
        heartbeatCv.notify_all();
        if (GetCurrentThreadId() ==
            KeyBoard.listenerThreadId.load(std::memory_order_acquire)) {
            KeyBoard.ListenerRuned.store(false, std::memory_order_release);
            if (IsValidSocket(KeyBoard.s_ListenSocket))
                closesocket(KeyBoard.s_ListenSocket);
            KeyBoard.s_ListenSocket = 0;
            KeyBoard.MonitorPort = 0;
        }
        Diagnostics::Warn("[KMBOX-NET] Shutdown deferred from an owned worker thread.");
        return err_queue_stopped;
    }

    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopped)
        return success;

    lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
    heartbeatCv.notify_all();

    int cleanupStatus = success;
    if (queueRunning.load(std::memory_order_acquire))
        cleanupStatus = ReleaseAllOutputAndWait(Timeout);

    StopWorkers();
    KeyBoard.EndMonitor();
    CompletePendingCommands(err_queue_stopped);
    CloseSocket();
    SetConnectionState(KmBoxConnectionState::Disconnected);

    if (wsaStarted) {
        WSACleanup();
        wsaStarted = false;
    }

    safetySendOverride = {};
    lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
    Diagnostics::Info("[KMBOX-NET] lifecycle stopped. cleanup_status=%d", cleanupStatus);
    return cleanupStatus;
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
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    const LifecycleState previousLifecycle = lifecycleState.load(std::memory_order_acquire);
    if (previousLifecycle == LifecycleState::Stopping)
        return err_queue_stopped;

    if (previousLifecycle == LifecycleState::Stopped)
        lifecycleState.store(LifecycleState::Starting, std::memory_order_release);

    WORD wVersionRequested = MAKEWORD(2, 2);
    WSADATA wsaData{};

    if (!wsaStarted) {
        const int status = WSAStartup(wVersionRequested, &wsaData);
        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] WSAStartup failed. status=%d", status);
            SetConnectionState(KmBoxConnectionState::Error);
            if (previousLifecycle == LifecycleState::Stopped)
                lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
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
        if (previousLifecycle == LifecycleState::Stopped) {
            CloseSocket();
            if (wsaStarted) {
                WSACleanup();
                wsaStarted = false;
            }
            lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
        }
        return err_net_cmd;
    }

    std::lock_guard<std::mutex> reconnectLock(reconnectMutex);
    const int status = ConnectLocked();
    if (status == success) {
        ClearDesiredKeyboardReport();
        ClearLastSentKeyboardReport();
        {
            std::lock_guard<std::mutex> lock(mouseStateMutex);
            Mouse.MouseData = {};
            lastSentMouseButtonStateMask = 0;
        }
        StartWorkers();
        lifecycleState.store(LifecycleState::Running, std::memory_order_release);
    } else if (previousLifecycle == LifecycleState::Stopped) {
        CloseSocket();
        if (wsaStarted) {
            WSACleanup();
            wsaStarted = false;
        }
        lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
    }
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
    std::unique_lock<std::mutex> stateLock(mouseStateMutex);
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
    payload = Mouse.MouseData;
    payload.button = next;

    const int stateMask = payload.button;
    payload.button = stateMask;
    payload.x = 0;
    payload.y = 0;
    payload.wheel = 0;

    client_data packet = BuildPacket(Cmd, NextRandom());
    Diagnostics::Aim("udp.mouse.button build cmd=0x%08X down=%d prevMask=0x%02X stateMask=0x%02X payloadButton=%d fullState=1 force=%d",
        Cmd,
        Down ? 1 : 0,
        previousStateMask,
        stateMask,
        payload.button,
        Force ? 1 : 0);

    memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    command.type = KmBoxCommandType::MouseButton;
    command.outputIntent = OutputIntentForState(Down);
    command.mouseButtonStateMask = stateMask;
    command.enqueuedAt = std::chrono::steady_clock::now();
    const int status = EnqueueCommand(command);
    if (status == success)
        Mouse.MouseData.button = next;
    return status;
}

int KmBoxNetManager::RecoverMousePassthrough()
{
    if (!EnsureConnected())
        return err_creat_socket;

    Diagnostics::Info("[KMBOX-NET] Recovering mouse passthrough state.");
    Diagnostics::Aim("kmbox.recover_mouse start");

    int status = success;
    const auto record = [&](int result) {
        RecordFirstFailure(status, result);
    };

    const auto shortPause = []() {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    };

    const auto sendHeaderOnly = [&](unsigned int cmd, KmBoxCommandType type, const char* label) {
        client_data packet = BuildPacket(cmd, NextRandom());
        Diagnostics::Aim("kmbox.recover_mouse %s cmd=0x%08X payload=header", label, cmd);
        record(SendSynchronousCommand(cmd, packet.head.rand, sizeof(cmd_head_t), type, &packet));
    };

    const auto sendShortButtonUp = [&](unsigned int cmd, const char* label) {
        client_data packet = BuildPacket(cmd, NextRandom());
        packet.cmd_mouse.button = 0;
        Diagnostics::Aim("kmbox.recover_mouse %s cmd=0x%08X payload=int0", label, cmd);
        record(SendSynchronousCommand(cmd, packet.head.rand,
            sizeof(cmd_head_t) + sizeof(packet.cmd_mouse.button),
            KmBoxCommandType::MouseButton,
            &packet));
    };

    const auto sendMoveZeroShort = [&]() {
        client_data packet = BuildPacket(cmd_mouse_move, NextRandom());
        packet.cmd_mouse.button = 0;
        packet.cmd_mouse.x = 0;
        packet.cmd_mouse.y = 0;
        packet.cmd_mouse.wheel = 0;
        Diagnostics::Aim("kmbox.recover_mouse move_zero_short cmd=0x%08X payload=mouse16", cmd_mouse_move);
        record(SendSynchronousCommand(cmd_mouse_move, packet.head.rand,
            sizeof(cmd_head_t) + (sizeof(int) * 4),
            KmBoxCommandType::MouseMove,
            &packet));
    };

    const auto sendSoftMouseZero = [&](unsigned int cmd, KmBoxCommandType type, const char* label) {
        client_data packet = BuildPacket(cmd, NextRandom());
        soft_mouse_t payload{};
        memcpy_s(&packet.cmd_mouse, sizeof(packet.cmd_mouse), &payload, sizeof(payload));
        Diagnostics::Aim("kmbox.recover_mouse %s cmd=0x%08X payload=soft0", label, cmd);
        record(SendSynchronousCommand(cmd, packet.head.rand,
            sizeof(cmd_head_t) + sizeof(payload),
            type,
            &packet));
    };

    sendHeaderOnly(cmd_unmask_all, KmBoxCommandType::MouseUnmask, "unmask_begin");
    shortPause();
    sendShortButtonUp(cmd_mouse_left, "left_short_up");
    sendShortButtonUp(cmd_mouse_right, "right_short_up");
    sendShortButtonUp(cmd_mouse_middle, "middle_short_up");
    shortPause();
    sendMoveZeroShort();
    shortPause();
    sendSoftMouseZero(cmd_mouse_left, KmBoxCommandType::MouseButton, "left_soft_up");
    sendSoftMouseZero(cmd_mouse_right, KmBoxCommandType::MouseButton, "right_soft_up");
    sendSoftMouseZero(cmd_mouse_middle, KmBoxCommandType::MouseButton, "middle_soft_up");
    shortPause();
    sendSoftMouseZero(cmd_mouse_move, KmBoxCommandType::MouseMove, "move_zero_soft");
    shortPause();
    sendHeaderOnly(cmd_unmask_all, KmBoxCommandType::MouseUnmask, "unmask_end");

    if (status == success) {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        Mouse.MouseData = {};
        lastSentMouseButtonStateMask = 0;
    }

    if (status == success) {
        Diagnostics::Info("[KMBOX-NET] Mouse passthrough recovery succeeded.");
        Diagnostics::Aim("kmbox.recover_mouse success");
    } else {
        Diagnostics::Warn("[KMBOX-NET] Mouse passthrough recovery completed with status=%d.", status);
        Diagnostics::Aim("kmbox.recover_mouse partial_failure status=%d", status);
    }

    return status;
}

int KmBoxNetManager::ForceReleaseMouseButtons()
{
    int status = SetMouseButton(0x01, false, cmd_mouse_left, true);
    const int rightStatus = SetMouseButton(0x02, false, cmd_mouse_right, true);
    const int middleStatus = SetMouseButton(0x04, false, cmd_mouse_middle, true);
    if (status == success && rightStatus != success)
        status = rightStatus;
    if (status == success && middleStatus != success)
        status = middleStatus;
    return status;
}

int KmBoxNetManager::ReleaseAllOutputAndWait(std::chrono::milliseconds Timeout)
{
    auto completion = std::make_shared<KmBoxCommandCompletion>();

    KmBoxQueuedNetCommand command{};
    command.type = KmBoxCommandType::SafetyReleaseAll;
    command.outputIntent = KmBoxOutputIntent::SafetyRelease;
    command.priority = KmBoxCommandPriority::Safety;
    command.completion = completion;
    command.enqueuedAt = std::chrono::steady_clock::now();

    int enqueueStatus = success;
    {
        std::scoped_lock stateLock(mouseStateMutex, keyboardStateMutex);
        const soft_mouse_t previousMouseData = Mouse.MouseData;
        const unsigned char previousModifierMask = desiredKeyboardModifierMask;
        const auto previousUsages = desiredKeyboardUsages;

        Mouse.MouseData = {};
        desiredKeyboardModifierMask = 0;
        desiredKeyboardUsages.fill(0);

        enqueueStatus = EnqueueCommand(command);
        if (enqueueStatus != success) {
            Mouse.MouseData = previousMouseData;
            desiredKeyboardModifierMask = previousModifierMask;
            desiredKeyboardUsages = previousUsages;
        }
    }
    if (enqueueStatus != success)
        return enqueueStatus;

    std::unique_lock<std::mutex> lock(completion->mutex);
    if (!completion->cv.wait_for(lock, Timeout, [&completion]() {
            return completion->completed;
        })) {
        Diagnostics::Warn(
            "[KMBOX-NET] Timed out waiting for safety release transport completion. timeout_ms=%lld",
            static_cast<long long>(Timeout.count()));
        return err_completion_timeout;
    }

    return completion->status;
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

    unsigned int cmd = 0;
    switch (releaseMask) {
    case 0x01:
        cmd = cmd_mouse_left;
        break;
    case 0x02:
        cmd = cmd_mouse_right;
        break;
    case 0x04:
        cmd = cmd_mouse_middle;
        break;
    default:
        return err_net_cmd;
    }

    return SetMouseButton(releaseMask, false, cmd, true);
}

int KmBoxNetManager::SetMouseButtonStateMask(unsigned int StateMask, bool Force)
{
    const unsigned int next = StateMask & 0x07u;
    int previousStateMask = 0;
    {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        previousStateMask = Mouse.MouseData.button;
    }

    if (!Force && next == static_cast<unsigned int>(previousStateMask)) {
        Diagnostics::Trace("[KMBOX-NET] coalesced redundant mouse button state mask=0x%02X",
            next);
        return success;
    }

    Diagnostics::Aim("udp.mouse.button_state diff prevMask=0x%02X stateMask=0x%02X changed=0x%02X force=%d",
        previousStateMask,
        next,
        (previousStateMask ^ static_cast<int>(next)) & 0x07,
        Force ? 1 : 0);

    int status = success;
    const auto applyButton = [&](unsigned int mask, unsigned int cmd) {
        const bool down = (next & mask) != 0;
        const bool wasDown = (static_cast<unsigned int>(previousStateMask) & mask) != 0;
        if (!Force && down == wasDown)
            return;

        const int result = SetMouseButton(mask, down, cmd, Force);
        if (status == success && result != success)
            status = result;
    };

    applyButton(0x01, cmd_mouse_left);
    applyButton(0x02, cmd_mouse_right);
    applyButton(0x04, cmd_mouse_middle);
    return status;
}

int KmBoxNetManager::MaskMouse(unsigned int Mask)
{
    return MaskMouseTracked(Mask, {});
}

int KmBoxNetManager::MaskMouseTracked(
    unsigned int Mask,
    const std::shared_ptr<KmBoxCommandCompletion>& Completion)
{
    const unsigned int effectiveMask = Mask & 0x7Fu;
    if (effectiveMask == 0) {
        CompleteCommand(Completion, success);
        return success;
    }

    client_data packet = BuildPacket(cmd_mask_mouse, effectiveMask);

    Diagnostics::Aim("udp.mouse.mask build mask=0x%02X", effectiveMask);

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t);
    command.type = KmBoxCommandType::MouseMask;
    command.completion = Completion;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

bool KmBoxNetManager::CancelQueuedMouseMask(
    const std::shared_ptr<KmBoxCommandCompletion>& Completion)
{
    if (!Completion)
        return false;

    bool cancelled = false;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const auto command = std::find_if(
            commandQueue.begin(),
            commandQueue.end(),
            [&Completion](const KmBoxQueuedNetCommand& queued) {
                return queued.type == KmBoxCommandType::MouseMask &&
                    queued.completion == Completion;
            });
        if (command != commandQueue.end()) {
            commandQueue.erase(command);
            cancelled = true;
        }
    }
    if (cancelled)
        CompleteCommand(Completion, err_completion_timeout);
    return cancelled;
}

int KmBoxNetManager::QueueMouseUnmaskCleanup(
    const std::shared_ptr<KmBoxCommandCompletion>& Completion)
{
    if (!Completion)
        return err_net_cmd;

    client_data packet = BuildPacket(cmd_unmask_all, NextRandom());
    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t);
    command.type = KmBoxCommandType::MouseUnmask;
    command.outputIntent = KmBoxOutputIntent::SafetyRelease;
    command.priority = KmBoxCommandPriority::Safety;
    command.completion = Completion;
    command.enqueuedAt = std::chrono::steady_clock::now();

    int status = success;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const LifecycleState lifecycle =
            lifecycleState.load(std::memory_order_acquire);
        const bool offlineTestSeam =
            lifecycle == LifecycleState::Stopped &&
            queueRunning.load(std::memory_order_acquire) &&
            static_cast<bool>(safetySendOverride);
        if (configuredIp.empty()) {
            status = err_creat_socket;
        } else if (!queueRunning.load(std::memory_order_acquire) ||
                   (lifecycle != LifecycleState::Running && !offlineTestSeam)) {
            status = err_queue_stopped;
        } else {
            // A paired cleanup may temporarily exceed the normal queue bound;
            // it must not be rejected behind an in-flight mask.
            commandQueue.push_front(std::move(command));
        }
    }
    if (status != success) {
        CompleteCommand(Completion, status);
        return status;
    }
    queueCv.notify_one();
    return success;
}

int KmBoxNetManager::UnmaskAll()
{
    return UnmaskAllTracked({});
}

int KmBoxNetManager::UnmaskAllTracked(
    const std::shared_ptr<KmBoxCommandCompletion>& Completion)
{
    client_data packet = BuildPacket(cmd_unmask_all, NextRandom());

    Diagnostics::Aim("udp.mouse.unmask_all build");

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t);
    command.type = KmBoxCommandType::MouseUnmask;
    command.outputIntent = KmBoxOutputIntent::SafetyRelease;
    command.completion = Completion;
    command.enqueuedAt = std::chrono::steady_clock::now();
    return EnqueueCommand(command);
}

bool KmBoxNetManager::BuildKeyboardReport(
    unsigned char modifierMask,
    const std::vector<unsigned char>& usages,
    soft_keyboard_t& report)
{
    report = {};
    if (usages.size() > std::size(report.button))
        return false;

    std::array<bool, 256> seen{};
    for (std::size_t index = 0; index < usages.size(); ++index) {
        const unsigned char usage = usages[index];
        if (usage < KEY_A || usage >= KEY_LEFTCONTROL || seen[usage]) {
            report = {};
            return false;
        }
        seen[usage] = true;
        report.button[index] = static_cast<char>(usage);
    }

    report.ctrl = static_cast<char>(modifierMask);
    return true;
}

bool KmBoxNetManager::BuildKeyboardReport(
    unsigned char hidCode,
    bool down,
    soft_keyboard_t& report)
{
    report = {};
    const bool isModifier =
        hidCode >= KEY_LEFTCONTROL && hidCode <= KEY_RIGHT_GUI;
    const bool isUsage = hidCode >= KEY_A && hidCode < KEY_LEFTCONTROL;
    if (!isModifier && !isUsage)
        return false;

    if (!down)
        return BuildKeyboardReport(
            0, std::vector<unsigned char>{}, report);

    if (isModifier) {
        const auto modifierBit = static_cast<unsigned char>(
            1u << (hidCode - KEY_LEFTCONTROL));
        return BuildKeyboardReport(
            modifierBit, std::vector<unsigned char>{}, report);
    }
    return BuildKeyboardReport(
        0, std::vector<unsigned char>{ hidCode }, report);
}

int KmBoxNetManager::SendKeyboardReport(
    unsigned char modifierMask,
    const std::vector<unsigned char>& usages,
    KmBoxOutputIntent intent)
{
    soft_keyboard_t kb{};
    if (!BuildKeyboardReport(modifierMask, usages, kb))
        return err_net_cmd;

    std::unique_lock<std::mutex> stateLock(keyboardStateMutex);
    if (intent == KmBoxOutputIntent::SafetyRelease &&
        !KeyboardReportIsSubset(
            modifierMask,
            usages,
            desiredKeyboardModifierMask,
            desiredKeyboardUsages)) {
        return err_net_cmd;
    }

    const unsigned char previousModifierMask = desiredKeyboardModifierMask;
    const auto previousUsages = desiredKeyboardUsages;
    desiredKeyboardModifierMask = modifierMask;
    desiredKeyboardUsages.fill(0);
    std::copy(usages.begin(), usages.end(), desiredKeyboardUsages.begin());

    client_data packet = BuildPacket(cmd_keyboard_all, NextRandom());

    memcpy_s(&packet.cmd_keyboard, sizeof(soft_keyboard_t), &kb, sizeof(soft_keyboard_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_keyboard_t);
    command.type = KmBoxCommandType::Keyboard;
    command.outputIntent = intent;
    command.enqueuedAt = std::chrono::steady_clock::now();
    const int status = EnqueueCommand(command);
    if (status != success) {
        desiredKeyboardModifierMask = previousModifierMask;
        desiredKeyboardUsages = previousUsages;
    }
    return status;
}

int KmBoxNetManager::SendKeyboardReport(
    unsigned char modifierMask,
    const std::vector<unsigned char>& usages)
{
    return SendKeyboardReport(
        modifierMask,
        usages,
        modifierMask == 0 && usages.empty()
            ? KmBoxOutputIntent::SafetyRelease
            : KmBoxOutputIntent::Normal);
}

int KmBoxNetManager::SendKeyboardKey(unsigned char hidCode, bool down)
{
    const bool isModifier =
        hidCode >= KEY_LEFTCONTROL && hidCode <= KEY_RIGHT_GUI;
    const bool isUsage = hidCode >= KEY_A && hidCode < KEY_LEFTCONTROL;
    if (!isModifier && !isUsage)
        return err_net_cmd;

    if (!down)
        return SendKeyboardReport(0, {});
    if (isModifier) {
        const auto modifierBit = static_cast<unsigned char>(
            1u << (hidCode - KEY_LEFTCONTROL));
        return SendKeyboardReport(modifierBit, {});
    }
    return SendKeyboardReport(0, { hidCode });
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
    this->listenerThreadId.store(GetCurrentThreadId(), std::memory_order_release);
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
        this->listenerThreadId.store(0, std::memory_order_release);
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
        this->listenerThreadId.store(0, std::memory_order_release);
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
        this->listenerThreadId.store(0, std::memory_order_release);
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
    this->listenerThreadId.store(0, std::memory_order_release);
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

    if (this->t_Listen.joinable() &&
        this->t_Listen.get_id() != std::this_thread::get_id()) {
        this->t_Listen.join();
    }
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
