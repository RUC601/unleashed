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
private:
    HANDLE hSerial = INVALID_HANDLE_VALUE;
    std::string serialPortName;

    std::atomic<KmBoxConnectionState> connectionState{ KmBoxConnectionState::Disconnected };
    std::atomic<bool> workerRunning{ false };
    std::atomic<bool> heartbeatRunning{ false };

    std::mutex serialMutex;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<KmBoxQueuedSerialCommand> commandQueue;
    std::thread queueThread;
    std::thread heartbeatThread;
    std::function<bool(const std::string&)> writeOverride;

    void SetConnectionState(KmBoxConnectionState state);
    bool OpenConfiguredPort();
    void ClosePort();
    bool SendCommandWithRetry(const std::string& command, KmBoxCommandType type);
    bool SendCommandOnce(const std::string& command);
    int EnqueueCommand(
        const std::string& command,
        KmBoxCommandType type,
        KmBoxOutputIntent outputIntent = KmBoxOutputIntent::Normal,
        KmBoxCommandPriority priority = KmBoxCommandPriority::Normal,
        std::shared_ptr<KmBoxCommandCompletion> completion = {});
    int ExecuteSafetyReleaseAll();
    void StartWorkers();
    void StopWorkers();
    void QueueWorkerLoop();
    void HeartbeatLoop();
public:
    friend class KmboxQueueSelfTestAccess;
    ~KmBoxBManager();

    KmBoxConnectionState GetConnectionState() const;
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

};

namespace kmbox
{
    inline KmBoxBManager kmBoxBMgr;
}
