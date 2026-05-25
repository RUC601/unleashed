// WinSock2 must be included before Windows.h to avoid version conflicts
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")

#include "Kmbox/KmBoxNetManager.h"
#include "Utils/Diagnostics.hpp"

#include <chrono>
#include <cstring>
#include <ctime>

namespace
{
    bool IsValidSocket(SOCKET socketHandle)
    {
        return socketHandle != 0 && socketHandle != INVALID_SOCKET;
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
        case cmd_monitor:        return KmBoxCommandType::Monitor;
        case cmd_reboot:         return KmBoxCommandType::Reboot;
        case cmd_setconfig:      return KmBoxCommandType::SetConfig;
        default:                 return KmBoxCommandType::Unknown;
        }
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
    if (!IsValidSocket(s_Client))
        return err_creat_socket;

    Diagnostics::Trace("[KMBOX-NET] send %s cmd=0x%08X pts=%u len=%d",
        ToString(Type), packet.head.cmd, packet.head.indexpts, DataLength);

    const int sent = sendto(s_Client, reinterpret_cast<const char*>(&packet), DataLength, 0,
        reinterpret_cast<sockaddr*>(&AddrServer), sizeof(AddrServer));

    if (sent == SOCKET_ERROR || sent != DataLength) {
        Diagnostics::Error("[KMBOX-NET] sendto failed for %s. sent=%d expected=%d WSA=%d",
            ToString(Type), sent, DataLength, WSAGetLastError());
        return err_net_tx;
    }

    client_data receiveData{};
    SOCKADDR_IN fromClient{};
    int fromLen = sizeof(fromClient);
    const int received = recvfrom(s_Client, reinterpret_cast<char*>(&receiveData),
        sizeof(receiveData), 0, reinterpret_cast<sockaddr*>(&fromClient), &fromLen);

    if (received < 0) {
        Diagnostics::Error("[KMBOX-NET] recvfrom timeout/failure for %s. WSA=%d",
            ToString(Type), WSAGetLastError());
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
        return err_net_cmd;
    }

    if (receiveData.head.indexpts != packet.head.indexpts) {
        Diagnostics::Error("[KMBOX-NET] packet sequence mismatch for %s. sent=%u recv=%u",
            ToString(Type), packet.head.indexpts, receiveData.head.indexpts);
        return err_net_pts;
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
    if (configuredIp.empty())
        return err_creat_socket;

    {
        std::lock_guard<std::mutex> lock(queueMutex);

        if (Command.type == KmBoxCommandType::MouseMove && !commandQueue.empty()) {
            KmBoxQueuedNetCommand& back = commandQueue.back();
            if (back.type == KmBoxCommandType::MouseMove) {
                back.data.cmd_mouse.x += Command.data.cmd_mouse.x;
                back.data.cmd_mouse.y += Command.data.cmd_mouse.y;
                Diagnostics::Trace("[KMBOX-NET] coalesced mouse_move dx=%d dy=%d",
                    back.data.cmd_mouse.x, back.data.cmd_mouse.y);
                return success;
            }
        }

        if (commandQueue.size() >= KmBoxRuntimeConfig::CommandQueueMaxSize) {
            Diagnostics::Error("[KMBOX-NET] Command queue full; dropping oldest command.");
            commandQueue.pop_front();
        }

        commandQueue.push_back(Command);
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
        }

        const auto flushInterval = std::chrono::milliseconds(KmBoxRuntimeConfig::CommandFlushIntervalMs);
        const auto elapsed = std::chrono::steady_clock::now() - lastFlush;
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (!EnsureConnected()) {
            Diagnostics::Error("[KMBOX-NET] Dropping %s command; device is not connected.",
                ToString(command.type));
            continue;
        }

        StampPacketForSend(command.data);
        const int status = SendPacketWithRetry(command.data, command.length, command.type);
        if (status != success) {
            Diagnostics::Error("[KMBOX-NET] Dropped %s command after retries. status=%d",
                ToString(command.type), status);
            CloseSocket();
            EnsureConnected();
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
    return kmbox::KmBoxMgr.EnqueueCommand(command);
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

int KmBoxNetManager::SetMouseButton(unsigned int Mask, bool Down, unsigned int Cmd)
{
    client_data packet = BuildPacket(Cmd, NextRandom());
    soft_mouse_t payload{};
    {
        std::lock_guard<std::mutex> lock(mouseStateMutex);
        const int current = Mouse.MouseData.button;
        const int next = Down
            ? (current | static_cast<int>(Mask))
            : (current & ~static_cast<int>(Mask));

        if (next == current) {
            Diagnostics::Trace("[KMBOX-NET] coalesced redundant mouse button state cmd=0x%08X down=%d",
                Cmd, Down ? 1 : 0);
            return success;
        }

        Mouse.MouseData.button = next;
        payload = Mouse.MouseData;
    }

    memcpy_s(&packet.cmd_mouse, sizeof(soft_mouse_t), &payload, sizeof(soft_mouse_t));

    KmBoxQueuedNetCommand command{};
    command.data = packet;
    command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
    command.type = KmBoxCommandType::MouseButton;
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
        return;
    }

    this->s_ListenSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (!IsValidSocket(this->s_ListenSocket)) {
        Diagnostics::Error("[KMBOX-NET] Monitor socket creation failed. WSA=%d", WSAGetLastError());
        WSACleanup();
        return;
    }

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
        WSACleanup();
        return;
    }

    SOCKADDR AddrClient{};
    int FromLen = sizeof(SOCKADDR);
    char Buffer[1024]{};

    this->ListenerRuned.store(true, std::memory_order_release);
    while (this->ListenerRuned.load(std::memory_order_acquire)) {
        const int Ret = recvfrom(this->s_ListenSocket, Buffer, sizeof(Buffer), 0, &AddrClient, &FromLen);
        if (Ret >= static_cast<int>(sizeof(this->hw_Mouse) + sizeof(this->hw_Keyboard))) {
            std::lock_guard<std::mutex> lock(this->monitorMutex);
            memcpy(&this->hw_Mouse, Buffer, sizeof(this->hw_Mouse));
            memcpy(&this->hw_Keyboard, &Buffer[sizeof(this->hw_Mouse)], sizeof(this->hw_Keyboard));
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
    if (!kmbox::KmBoxMgr.EnsureConnected())
        return err_creat_socket;

    client_data packet = kmbox::KmBoxMgr.BuildPacket(cmd_monitor, Port | 0xaa55 << 16);
    const int status = kmbox::KmBoxMgr.SendSynchronousCommand(cmd_monitor, packet.head.rand,
        sizeof(cmd_head_t), KmBoxCommandType::Monitor, &packet);
    if (status != success)
        return status;

    this->MonitorPort = Port;

    if (IsValidSocket(this->s_ListenSocket)) {
        closesocket(this->s_ListenSocket);
        this->s_ListenSocket = 0;
    }

    if (this->t_Listen.joinable())
        this->t_Listen.join();

    this->t_Listen = std::thread(&KmBoxKeyBoard::ListenThread, this);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    return success;
}

void KmBoxKeyBoard::EndMonitor()
{
    if (this->ListenerRuned.load(std::memory_order_acquire)) {
        this->ListenerRuned.store(false, std::memory_order_release);

        if (IsValidSocket(this->s_ListenSocket))
            closesocket(this->s_ListenSocket);

        this->s_ListenSocket = 0;
        this->MonitorPort = 0;
    }

    if (this->t_Listen.joinable())
        this->t_Listen.join();
}

KmBoxKeyBoard::~KmBoxKeyBoard()
{
    this->EndMonitor();
}

bool KmBoxKeyBoard::GetKeyState(WORD vKey)
{
    unsigned char KeyValue = vKey & 0xff;
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
