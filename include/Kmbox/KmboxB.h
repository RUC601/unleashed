#pragma once

#include <Windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "Kmbox/KmBoxConfig.h"

class KmBoxBManager {
public:
    enum class LifecycleState : int {
        Stopped = 0,
        Starting,
        Running,
        Stopping
    };

private:
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    std::string serialPortName;

    std::atomic<KmBoxConnectionState> connectionState{ KmBoxConnectionState::Disconnected };
    std::atomic<LifecycleState> lifecycleState{ LifecycleState::Stopped };
    std::atomic<bool> workerRunning{ false };
    std::atomic<bool> heartbeatRunning{ false };

    std::mutex lifecycleMutex;
    std::mutex serialMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::mutex heartbeatMutex;
    std::condition_variable heartbeatCv;
    std::deque<KmBoxQueuedSerialCommand> commandQueue;
    std::thread queueThread;
    std::thread heartbeatThread;
    std::atomic<DWORD> queueThreadId{ 0 };
    std::atomic<DWORD> heartbeatThreadId{ 0 };
    std::function<bool(const std::string&)> writeOverride;

    void SetConnectionState(KmBoxConnectionState state);
    bool OpenConfiguredPort(bool AllowDuringStopping = false);
    void ClosePort();
    bool SendCommandWithRetry(
        const std::string& command,
        KmBoxCommandType type,
        bool AllowReconnectDuringStopping = false);
    bool SendCommandOnce(const std::string& command);
    int EnqueueCommand(
        const std::string& command,
        KmBoxCommandType type,
        KmBoxOutputIntent outputIntent = KmBoxOutputIntent::Normal,
        KmBoxCommandPriority priority = KmBoxCommandPriority::Normal,
        std::shared_ptr<KmBoxCommandCompletion> completion = {});
    int ExecuteSafetyReleaseAll();
    bool IsOwnedWorkerThread() const;
    void StartWorkers();
    void StopWorkers();
    void CompletePendingCommands(int Status);
    void QueueWorkerLoop();
    void HeartbeatLoop();
public:
    friend class KmboxQueueSelfTestAccess;
    friend class KmboxLifecycleSelfTestAccess;
    ~KmBoxBManager();

    KmBoxConnectionState GetConnectionState() const;
    LifecycleState GetLifecycleState() const;
    bool IsConnected() const;

    int init();
    int init(const std::string& portName);

    void km_move(int X, int Y);

    void km_move_auto(int X, int Y, int runtime);

    void km_click();

    void km_left(bool down);
    void km_right(bool down);
    void km_middle(bool down);
    int ReleaseAllOutputAndWait(std::chrono::milliseconds timeout);
    int Shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

};

namespace kmbox
{
    inline KmBoxBManager kmBoxBMgr;
}
