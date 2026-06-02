#pragma once

#include "KmBoxConfig.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <atomic>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <Windows.h>
#include <math.h>
#include <random>
#include <string>
#include <thread>

class KmBoxMouse
{
public:
    soft_mouse_t MouseData{};
public:
    // Move mouse by relative offset
    int Move(int x, int y);
    // Move mouse with automatic trajectory over given runtime
    int Move_Auto(int x, int y, int Runtime);
    // Left mouse button
    int Left(bool Down);
    // Right mouse button
    int Right(bool Down);
    // Middle mouse button
    int Middle(bool Down);
};

class KmBoxKeyBoard
{
public:
    std::thread t_Listen;
    WORD MonitorPort;
    SOCKET s_ListenSocket = 0;
    std::atomic<bool> ListenerRuned{ false };
    std::mutex monitorMutex;
    std::condition_variable monitorStartCv;
    bool monitorStartResolved = false;
    int monitorStartStatus = err_creat_socket;
    std::atomic<unsigned long long> inputPacketCount{ 0 };
    std::atomic<unsigned char> lastLoggedMouseButtons{ 0 };
public:
    standard_keyboard_report_t hw_Keyboard;
    standard_mouse_report_t hw_Mouse;
public:
    ~KmBoxKeyBoard();
    void ListenThread();
    int StartMonitor(WORD Port);
    void EndMonitor();
    unsigned long long InputPacketCount() const;
public:
    bool IsMouseButtonPressed(int vkButton);
    bool GetKeyState(WORD vKey);
};

class KmBoxNetManager
{
private:
    SOCKADDR_IN AddrServer;
    SOCKET s_Client = 0;
    client_data ReceiveData;
    client_data PostData;
    std::string configuredIp;
    WORD configuredPort = 0;
    std::string configuredMac;
    std::mt19937 randomEngine;
    bool wsaStarted = false;

    std::atomic<KmBoxConnectionState> connectionState{ KmBoxConnectionState::Disconnected };
    std::atomic<bool> queueRunning{ false };
    std::atomic<bool> heartbeatRunning{ false };

    std::mutex sendMutex;
    std::mutex dataMutex;
    std::mutex mouseStateMutex;
    std::mutex reconnectMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<KmBoxQueuedNetCommand> commandQueue;
    std::thread queueThread;
    std::thread heartbeatThread;
    std::atomic<unsigned long long> outputSendCount{ 0 };
private:
    int NetHandler();
    int SendData(int DataLength);
    int SendPacketWithRetry(client_data& packet, int DataLength, KmBoxCommandType Type);
    int SendPacketOnce(client_data& packet, int DataLength, KmBoxCommandType Type);
    int EnqueueCommand(const KmBoxQueuedNetCommand& Command);
    int SendSynchronousCommand(unsigned int Cmd, unsigned int RandValue, int DataLength,
        KmBoxCommandType Type, client_data* PacketOverride = nullptr);
    client_data BuildPacket(unsigned int Cmd, unsigned int RandValue);
    void StampPacketForSend(client_data& Packet);
    unsigned int NextRandom();
    void SetConnectionState(KmBoxConnectionState State);
    bool ConfigureSocketTimeouts(SOCKET SocketHandle);
    void CloseSocket();
    bool OpenSocket();
    int ConnectLocked();
    bool EnsureConnected();
    void StartWorkers();
    void StopWorkers();
    void QueueWorkerLoop();
    void HeartbeatLoop();
    int SetMouseButton(unsigned int Mask, bool Down, unsigned int Cmd, bool Force = false);
public:
    KmBoxNetManager();
    ~KmBoxNetManager();
    int ForceReleaseMouseButton(int button);
    int ForceReleaseMouseButtons();
    int SetMouseButtonStateMask(unsigned int StateMask, bool Force = false);
    int SendKeyboardKey(unsigned char hidCode, bool down);
    KmBoxConnectionState GetConnectionState() const;
    bool IsConnected() const;
    // Initialize device connection
    int InitDevice(const std::string& IP, WORD Port, const std::string& Mac);
    // Reboot the device
    int RebootDevice();
    // Set network configuration
    int SetConfig(const std::string& IP, WORD Port);
public:
    friend class KmBoxMouse;
    KmBoxMouse Mouse;
    friend class KmBoxKeyBoard;
    KmBoxKeyBoard KeyBoard;
};

namespace kmbox
{
    inline KmBoxNetManager KmBoxMgr;
}
