#pragma once

#include "KmBoxConfig.h"

#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <Windows.h>
#include <math.h>
#include <random>
#include <string>
#include <thread>
#include <vector>

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
    std::atomic<DWORD> listenerThreadId{ 0 };
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
public:
    enum class LifecycleState : int {
        Stopped = 0,
        Starting,
        Running,
        Stopping
    };

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
    std::atomic<LifecycleState> lifecycleState{ LifecycleState::Stopped };
    std::atomic<bool> queueRunning{ false };
    std::atomic<bool> heartbeatRunning{ false };

    std::mutex lifecycleMutex;
    std::mutex sendMutex;
    std::mutex dataMutex;
    std::mutex mouseStateMutex;
    std::mutex keyboardStateMutex;
    std::mutex reconnectMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::mutex heartbeatMutex;
    std::condition_variable heartbeatCv;
    std::deque<KmBoxQueuedNetCommand> commandQueue;
    std::thread queueThread;
    std::thread heartbeatThread;
    std::atomic<DWORD> queueThreadId{ 0 };
    std::atomic<DWORD> heartbeatThreadId{ 0 };
    std::atomic<unsigned long long> outputSendCount{ 0 };
    std::function<int(unsigned int, const client_data&, int)> safetySendOverride;
    unsigned char desiredKeyboardModifierMask = 0;
    std::array<unsigned char, 10> desiredKeyboardUsages{};
    unsigned char lastSentKeyboardModifierMask = 0;
    std::array<unsigned char, 10> lastSentKeyboardUsages{};
    unsigned int lastSentMouseButtonStateMask = 0;
private:
    int NetHandler();
    int SendData(int DataLength);
    int SendPacketWithRetry(client_data& packet, int DataLength, KmBoxCommandType Type);
    int SendPacketOnce(client_data& packet, int DataLength, KmBoxCommandType Type);
    int EnqueueCommand(const KmBoxQueuedNetCommand& Command);
    int ExecuteSafetyReleaseAll();
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
    bool LifecycleAllowsReconnect() const;
    bool IsOwnedWorkerThread() const;
    void StartWorkers();
    void StopWorkers();
    void CompletePendingCommands(int Status);
    void ClearDesiredKeyboardReport();
    void ClearLastSentKeyboardReport();
    void QueueWorkerLoop();
    void HeartbeatLoop();
    int SetMouseButton(unsigned int Mask, bool Down, unsigned int Cmd, bool Force = false);
public:
    friend class KmboxQueueSelfTestAccess;
    friend class KmboxLifecycleSelfTestAccess;
    KmBoxNetManager();
    ~KmBoxNetManager();
    int RecoverMousePassthrough();
    int ForceReleaseMouseButton(int button);
    int ForceReleaseMouseButtons();
    int ReleaseAllOutputAndWait(std::chrono::milliseconds Timeout);
    int Shutdown(std::chrono::milliseconds Timeout = std::chrono::milliseconds(500));
    int SetMouseButtonStateMask(unsigned int StateMask, bool Force = false);
    int MaskMouse(unsigned int Mask);
    // Queue a mask command with a completion token. The token is resolved by
    // the queue worker only after suppression/transport handling finishes.
    int MaskMouseTracked(
        unsigned int Mask,
        const std::shared_ptr<KmBoxCommandCompletion>& Completion);
    bool CancelQueuedMouseMask(
        const std::shared_ptr<KmBoxCommandCompletion>& Completion);
    // Emergency ordering for a timed-out, possibly in-flight mask: this
    // cleanup is placed directly behind the worker's current command even if
    // the bounded queue is otherwise full.
    int QueueMouseUnmaskCleanup(
        const std::shared_ptr<KmBoxCommandCompletion>& Completion);
    int UnmaskAll();
    int UnmaskAllTracked(
        const std::shared_ptr<KmBoxCommandCompletion>& Completion);
    static bool BuildKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        soft_keyboard_t& report);
    static bool BuildKeyboardReport(unsigned char HidCode, bool Down, soft_keyboard_t& Report);
    int SendKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        KmBoxOutputIntent intent);
    int SendKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages);
    int SendKeyboardKey(unsigned char hidCode, bool down);
    KmBoxConnectionState GetConnectionState() const;
    LifecycleState GetLifecycleState() const;
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
