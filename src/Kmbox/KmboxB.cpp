#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <setupapi.h>
#include <devguid.h>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")

namespace
{
    std::atomic<unsigned long long> g_KmBoxBOutputSendCount{ 0 };

    bool IsSerialOutputCommand(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseMove ||
            type == KmBoxCommandType::MouseAutoMove ||
            type == KmBoxCommandType::MouseButton;
    }

    bool IsSerialMouseMoveCommand(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseMove ||
            type == KmBoxCommandType::MouseAutoMove;
    }

    bool IsOrderedSerialSafety(const KmBoxQueuedSerialCommand& command)
    {
        return command.outputIntent == KmBoxOutputIntent::SafetyRelease &&
            command.type != KmBoxCommandType::SafetyReleaseAll;
    }

    int SerialMouseButtonDomain(const KmBoxQueuedSerialCommand& command)
    {
        if (command.type != KmBoxCommandType::MouseButton)
            return -1;
        if (command.command.starts_with("km.left("))
            return 0;
        if (command.command.starts_with("km.right("))
            return 1;
        if (command.command.starts_with("km.middle("))
            return 2;
        return -1;
    }

    bool HasDependentOrderedSerialSafety(
        const std::deque<KmBoxQueuedSerialCommand>& queue,
        std::deque<KmBoxQueuedSerialCommand>::const_iterator candidate)
    {
        const int domain = SerialMouseButtonDomain(*candidate);
        if (domain < 0)
            return false;

        for (auto queued = std::next(candidate); queued != queue.end(); ++queued) {
            if (SerialMouseButtonDomain(*queued) != domain)
                continue;
            return IsOrderedSerialSafety(*queued);
        }
        return false;
    }

    bool IsRequiredPredecessorForIncomingSafety(
        const std::deque<KmBoxQueuedSerialCommand>& queue,
        std::deque<KmBoxQueuedSerialCommand>::const_iterator candidate,
        const KmBoxQueuedSerialCommand& incoming)
    {
        const int domain = SerialMouseButtonDomain(incoming);
        if (domain < 0 || SerialMouseButtonDomain(*candidate) != domain)
            return false;

        return std::none_of(std::next(candidate), queue.end(),
            [domain](const KmBoxQueuedSerialCommand& queued) {
                return SerialMouseButtonDomain(queued) == domain;
            });
    }

    int SerialFlushIntervalForCommand(KmBoxCommandType type)
    {
        return type == KmBoxCommandType::MouseButton
            ? KmBoxRuntimeConfig::MouseButtonFlushIntervalMs
            : KmBoxRuntimeConfig::CommandFlushIntervalMs;
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

    int WaitForCommand(
        const std::shared_ptr<KmBoxCommandCompletion>& completion,
        std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(completion->mutex);
        if (!completion->cv.wait_for(lock, timeout, [&completion]() {
                return completion->completed;
            })) {
            return err_completion_timeout;
        }
        return completion->status;
    }
}

// Find a COM port by device description substring
static std::string find_port(const std::string& targetDescription)
{
    HDEVINFO hDevInfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, 0, 0, DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return "";

    SP_DEVINFO_DATA deviceInfoData;
    deviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &deviceInfoData); ++i) {
        char buf[512];
        DWORD nSize = 0;

        if (SetupDiGetDeviceRegistryPropertyA(hDevInfo, &deviceInfoData, SPDRP_FRIENDLYNAME, NULL, (PBYTE)buf, sizeof(buf), &nSize) && nSize > 0) {
            buf[(std::min)(nSize, static_cast<DWORD>(sizeof(buf) - 1))] = '\0';
            std::string deviceDescription = buf;

            size_t comPos = deviceDescription.find("COM");
            size_t endPos = deviceDescription.find(")", comPos);

            if (comPos != std::string::npos && endPos != std::string::npos && deviceDescription.find(targetDescription) != std::string::npos) {
                SetupDiDestroyDeviceInfoList(hDevInfo);
                return deviceDescription.substr(comPos, endPos - comPos);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    return "";
}

// Open a serial port handle with the given baud rate
static bool open_port(HANDLE& hSerial, const char* portName, DWORD baudRate)
{
    hSerial = CreateFileA(portName, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

    if (hSerial == INVALID_HANDLE_VALUE) return false;

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        CloseHandle(hSerial);
        return false;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutConstant = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = KmBoxRuntimeConfig::CommandTimeoutMs;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        Diagnostics::Error("[KMBOX-B] Error setting serial timeouts. GetLastError=%lu",
            static_cast<unsigned long>(GetLastError()));
        CloseHandle(hSerial);
        return false;
    }

    return true;
}

static std::string normalize_port_name(const std::string& portName)
{
    if (portName.empty() || portName.rfind("\\\\.\\", 0) == 0)
        return portName;

    if (portName.rfind("COM", 0) == 0 && portName.size() > 4)
        return "\\\\.\\" + portName;

    return portName;
}

KmBoxBManager::~KmBoxBManager()
{
    (void)Shutdown();
}

KmBoxConnectionState KmBoxBManager::GetConnectionState() const
{
    return connectionState.load(std::memory_order_acquire);
}

KmBoxBManager::LifecycleState KmBoxBManager::GetLifecycleState() const
{
    return lifecycleState.load(std::memory_order_acquire);
}

bool KmBoxBManager::IsConnected() const
{
    return GetConnectionState() == KmBoxConnectionState::Connected;
}

void KmBoxBManager::SetConnectionState(KmBoxConnectionState state)
{
    const KmBoxConnectionState previous = connectionState.exchange(state, std::memory_order_acq_rel);
    if (previous != state) {
        Diagnostics::Info("[KMBOX-B] connection state %s -> %s",
            ToString(previous), ToString(state));
    }
}

bool KmBoxBManager::OpenConfiguredPort(bool AllowDuringStopping)
{
    const LifecycleState lifecycle = lifecycleState.load(std::memory_order_acquire);
    if (lifecycle == LifecycleState::Stopping && !AllowDuringStopping)
        return false;
    if (lifecycle == LifecycleState::Stopped)
        return false;

    if (serialPortName.empty()) {
        SetConnectionState(KmBoxConnectionState::Disconnected);
        return false;
    }

    std::lock_guard<std::mutex> lock(serialMutex);
    if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopping &&
        !AllowDuringStopping) {
        return false;
    }
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }

    HANDLE newHandle = INVALID_HANDLE_VALUE;
    if (!open_port(newHandle, serialPortName.c_str(), CBR_115200)) {
        Diagnostics::Error("[KMBOX-B] Failed to open serial port %s. GetLastError=%lu",
            serialPortName.c_str(), static_cast<unsigned long>(GetLastError()));
        SetConnectionState(KmBoxConnectionState::Error);
        return false;
    }

    hSerial = newHandle;
    if (lifecycleState.load(std::memory_order_acquire) != LifecycleState::Stopping)
        SetConnectionState(KmBoxConnectionState::Connected);
    return true;
}

void KmBoxBManager::ClosePort()
{
    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial != INVALID_HANDLE_VALUE) {
        CloseHandle(hSerial);
        hSerial = INVALID_HANDLE_VALUE;
    }
    SetConnectionState(KmBoxConnectionState::Disconnected);
}

bool KmBoxBManager::SendCommandOnce(const std::string& command)
{
    if (writeOverride)
        return writeOverride(command);

    std::lock_guard<std::mutex> lock(serialMutex);
    if (hSerial == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD bytesWritten = 0;
    const BOOL ok = WriteFile(hSerial, command.c_str(),
        static_cast<DWORD>(command.length()), &bytesWritten, NULL);
    if (!ok || bytesWritten != command.length()) {
        Diagnostics::Error("[KMBOX-B] Serial write failed. bytes=%lu/%zu GetLastError=%lu command=%s",
            static_cast<unsigned long>(bytesWritten),
            command.length(),
            static_cast<unsigned long>(GetLastError()),
            command.c_str());
        return false;
    }

    Diagnostics::Trace("[KMBOX-B] sent command: %s", command.c_str());
    return true;
}

bool KmBoxBManager::SendCommandWithRetry(
    const std::string& command,
    KmBoxCommandType type,
    bool AllowReconnectDuringStopping)
{
    for (int attempt = 1; attempt <= KmBoxRuntimeConfig::CommandMaxRetries; ++attempt) {
        if (SendCommandOnce(command)) {
            if (lifecycleState.load(std::memory_order_acquire) != LifecycleState::Stopping &&
                connectionState.load(std::memory_order_acquire) != KmBoxConnectionState::Connected) {
                SetConnectionState(KmBoxConnectionState::Connected);
            }
            if (IsSerialOutputCommand(type)) {
                const unsigned long long count =
                    g_KmBoxBOutputSendCount.fetch_add(1, std::memory_order_acq_rel) + 1;
                if (OW::Config::kmboxDebugLog) {
                    Diagnostics::Info("[KMBOX-B] output send count=%llu type=%s command=%s",
                        count, ToString(type), command.c_str());
                }
            }
            return true;
        }

        SetConnectionState(KmBoxConnectionState::Error);
        Diagnostics::Error("[KMBOX-B] %s command failed on attempt %d/%d.",
            ToString(type), attempt, KmBoxRuntimeConfig::CommandMaxRetries);

        if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopping &&
            !AllowReconnectDuringStopping) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(
            KmBoxRuntimeConfig::CommandRetryBackoffMs * attempt));

        if (attempt < KmBoxRuntimeConfig::CommandMaxRetries)
            OpenConfiguredPort(AllowReconnectDuringStopping);
    }

    return false;
}

int KmBoxBManager::EnqueueCommand(
    const std::string& command,
    KmBoxCommandType type,
    KmBoxOutputIntent outputIntent,
    KmBoxCommandPriority priority,
    std::shared_ptr<KmBoxCommandCompletion> completion)
{
    if (outputIntent == KmBoxOutputIntent::SafetyRelease &&
        priority != KmBoxCommandPriority::Safety) {
        return EnqueueCommand(
            command,
            type,
            outputIntent,
            KmBoxCommandPriority::Safety,
            std::move(completion));
    }

    const bool releaseAllBarrier =
        type == KmBoxCommandType::SafetyReleaseAll;
    if (outputIntent == KmBoxOutputIntent::SafetyRelease &&
        !releaseAllBarrier &&
        (type != KmBoxCommandType::MouseButton ||
         (command != "km.left(0)\r\n" &&
          command != "km.right(0)\r\n" &&
          command != "km.middle(0)\r\n"))) {
        CompleteCommand(completion, err_net_cmd);
        return err_net_cmd;
    }

    KmBoxQueuedSerialCommand queued{};
    queued.command = command;
    queued.type = type;
    queued.outputIntent = outputIntent;
    queued.priority = priority;
    queued.completion = std::move(completion);
    queued.enqueuedAt = std::chrono::steady_clock::now();
    const bool orderedSafety = IsOrderedSerialSafety(queued);

    std::shared_ptr<KmBoxCommandCompletion> droppedCompletion;
    std::vector<std::shared_ptr<KmBoxCommandCompletion>> cancelledCompletions;
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        const LifecycleState lifecycle = lifecycleState.load(std::memory_order_acquire);
        const bool offlineTestSeam =
            lifecycle == LifecycleState::Stopped &&
            workerRunning.load(std::memory_order_acquire) &&
            static_cast<bool>(writeOverride);
        if (priority == KmBoxCommandPriority::Normal &&
            lifecycle != LifecycleState::Running && !offlineTestSeam) {
            const auto completionToFail = queued.completion;
            lock.unlock();
            CompleteCommand(completionToFail, err_queue_stopped);
            return err_queue_stopped;
        }
        if (priority == KmBoxCommandPriority::Safety &&
            !workerRunning.load(std::memory_order_acquire)) {
            const auto completionToFail = queued.completion;
            lock.unlock();
            CompleteCommand(completionToFail, err_queue_stopped);
            return err_queue_stopped;
        }

        if (releaseAllBarrier) {
            for (auto pending = commandQueue.begin(); pending != commandQueue.end();) {
                if (pending->type != KmBoxCommandType::SafetyReleaseAll &&
                    IsSerialOutputCommand(pending->type)) {
                    cancelledCompletions.push_back(pending->completion);
                    pending = commandQueue.erase(pending);
                } else {
                    ++pending;
                }
            }
        }

        if (commandQueue.size() >= KmBoxRuntimeConfig::CommandQueueMaxSize) {
            auto droppable = commandQueue.begin();
            for (; droppable != commandQueue.end(); ++droppable) {
                if (droppable->priority != KmBoxCommandPriority::Normal)
                    continue;
                if (orderedSafety && IsRequiredPredecessorForIncomingSafety(
                        commandQueue, droppable, queued)) {
                    continue;
                }
                if (HasDependentOrderedSerialSafety(commandQueue, droppable))
                    continue;
                break;
            }
            if (droppable == commandQueue.end()) {
                Diagnostics::Error("[KMBOX-B] Command queue full; no normal command can be displaced.");
                const auto completionToFail = queued.completion;
                lock.unlock();
                CompleteCommand(completionToFail, err_queue_full);
                return err_queue_full;
            } else {
                Diagnostics::Error("[KMBOX-B] Command queue full; dropping oldest normal command.");
                droppedCompletion = droppable->completion;
                commandQueue.erase(droppable);
            }
        }

        if (releaseAllBarrier) {
            const auto firstNormal = std::find_if(
                commandQueue.begin(), commandQueue.end(),
                [](const KmBoxQueuedSerialCommand& pending) {
                    return pending.priority == KmBoxCommandPriority::Normal;
                });
            commandQueue.insert(firstNormal, std::move(queued));
        } else if (type == KmBoxCommandType::MouseButton) {
            auto insertAt = commandQueue.end();
            while (insertAt != commandQueue.begin()) {
                auto previous = insertAt;
                --previous;
                if (!IsSerialMouseMoveCommand(previous->type))
                    break;
                insertAt = previous;
            }
            commandQueue.insert(insertAt, std::move(queued));
        } else {
            commandQueue.push_back(std::move(queued));
        }
    }
    for (const auto& completionToCancel : cancelledCompletions)
        CompleteCommand(completionToCancel, err_queue_dropped);
    CompleteCommand(droppedCompletion, err_queue_dropped);
    queueCv.notify_one();
    return success;
}

int KmBoxBManager::ExecuteSafetyReleaseAll()
{
    int aggregateStatus = success;
    const auto sendRelease = [this, &aggregateStatus](const char* command) {
        if (!SendCommandWithRetry(command, KmBoxCommandType::MouseButton, true) &&
            aggregateStatus == success) {
            aggregateStatus = err_net_tx;
        }
    };

    sendRelease("km.left(0)\r\n");
    sendRelease("km.right(0)\r\n");
    sendRelease("km.middle(0)\r\n");
    return aggregateStatus;
}

int KmBoxBManager::ReleaseAllOutputAndWait(std::chrono::milliseconds timeout)
{
    if (!workerRunning.load(std::memory_order_acquire))
        return err_queue_stopped;

    auto completion = std::make_shared<KmBoxCommandCompletion>();
    const int enqueueStatus = EnqueueCommand(
        {},
        KmBoxCommandType::SafetyReleaseAll,
        KmBoxOutputIntent::SafetyRelease,
        KmBoxCommandPriority::Safety,
        completion);
    if (enqueueStatus != success)
        return enqueueStatus;
    return WaitForCommand(completion, timeout);
}

void KmBoxBManager::StartWorkers()
{
    if (!workerRunning.exchange(true, std::memory_order_acq_rel)) {
        queueThread = std::thread(&KmBoxBManager::QueueWorkerLoop, this);
    }
    if (!heartbeatRunning.exchange(true, std::memory_order_acq_rel)) {
        heartbeatThread = std::thread(&KmBoxBManager::HeartbeatLoop, this);
    }
}

void KmBoxBManager::StopWorkers()
{
    workerRunning.store(false, std::memory_order_release);
    heartbeatRunning.store(false, std::memory_order_release);
    queueCv.notify_all();
    heartbeatCv.notify_all();

    const std::thread::id current = std::this_thread::get_id();
    if (queueThread.joinable() && queueThread.get_id() != current)
        queueThread.join();
    if (heartbeatThread.joinable() && heartbeatThread.get_id() != current)
        heartbeatThread.join();
}

void KmBoxBManager::CompletePendingCommands(int Status)
{
    std::deque<KmBoxQueuedSerialCommand> pending;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        pending.swap(commandQueue);
    }
    for (const auto& command : pending)
        CompleteCommand(command.completion, Status);
}

void KmBoxBManager::QueueWorkerLoop()
{
    queueThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    auto lastFlush = std::chrono::steady_clock::now();
    while (workerRunning.load(std::memory_order_acquire)) {
        KmBoxQueuedSerialCommand command{};
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCv.wait_for(lock,
                std::chrono::milliseconds(KmBoxRuntimeConfig::CommandFlushIntervalMs),
                [this]() {
                    return !workerRunning.load(std::memory_order_acquire) || !commandQueue.empty();
                });

            if (!workerRunning.load(std::memory_order_acquire))
                break;
            if (commandQueue.empty())
                continue;

            command = std::move(commandQueue.front());
            commandQueue.pop_front();
        }

        if (command.type == KmBoxCommandType::SafetyReleaseAll) {
            const int status = ExecuteSafetyReleaseAll();
            CompleteCommand(command.completion, status);
            lastFlush = std::chrono::steady_clock::now();
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = now - lastFlush;
        const auto flushInterval = std::chrono::milliseconds(SerialFlushIntervalForCommand(command.type));
        if (elapsed < flushInterval)
            std::this_thread::sleep_for(flushInterval - elapsed);

        if (command.priority == KmBoxCommandPriority::Normal &&
            lifecycleState.load(std::memory_order_acquire) != LifecycleState::Running) {
            CompleteCommand(command.completion, err_queue_stopped);
            continue;
        }

        if (IsSerialOutputCommand(command.type) &&
            ShouldSuppressOutputForMenu(
                OW::Config::KmboxOutputSuppressedByMenu(), command.outputIntent)) {
            Diagnostics::Aim("serial.queue drop reason=menu_open_suppressed type=%s intent=%s",
                ToString(command.type),
                ToString(command.outputIntent));
            continue;
        }

        if (connectionState.load(std::memory_order_acquire) != KmBoxConnectionState::Connected)
            OpenConfiguredPort();

        if (!SendCommandWithRetry(command.command, command.type)) {
            Diagnostics::Error("[KMBOX-B] Dropped %s command after retries.",
                ToString(command.type));
        }

        lastFlush = std::chrono::steady_clock::now();
    }
    queueThreadId.store(0, std::memory_order_release);
}

void KmBoxBManager::HeartbeatLoop()
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

        if (connectionState.load(std::memory_order_acquire) == KmBoxConnectionState::Disconnected) {
            OpenConfiguredPort();
            continue;
        }

        if (!SendCommandWithRetry("km.move(0,0)\r\n", KmBoxCommandType::Heartbeat)) {
            Diagnostics::Error("[KMBOX-B] heartbeat failed; attempting serial reconnect.");
            OpenConfiguredPort();
        }
    }
    heartbeatThreadId.store(0, std::memory_order_release);
}

bool KmBoxBManager::IsOwnedWorkerThread() const
{
    const DWORD current = GetCurrentThreadId();
    return current != 0 &&
        (current == queueThreadId.load(std::memory_order_acquire) ||
         current == heartbeatThreadId.load(std::memory_order_acquire));
}

int KmBoxBManager::Shutdown(std::chrono::milliseconds timeout)
{
    if (IsOwnedWorkerThread()) {
        lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
        workerRunning.store(false, std::memory_order_release);
        heartbeatRunning.store(false, std::memory_order_release);
        queueCv.notify_all();
        heartbeatCv.notify_all();
        Diagnostics::Warn("[KMBOX-B] Shutdown deferred from an owned worker thread.");
        return err_queue_stopped;
    }

    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    if (lifecycleState.load(std::memory_order_acquire) == LifecycleState::Stopped)
        return success;

    lifecycleState.store(LifecycleState::Stopping, std::memory_order_release);
    heartbeatCv.notify_all();

    int cleanupStatus = success;
    if (workerRunning.load(std::memory_order_acquire))
        cleanupStatus = ReleaseAllOutputAndWait(timeout);

    StopWorkers();
    CompletePendingCommands(err_queue_stopped);
    ClosePort();
    writeOverride = {};
    lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
    Diagnostics::Info("[KMBOX-B] lifecycle stopped. cleanup_status=%d", cleanupStatus);
    return cleanupStatus;
}

int KmBoxBManager::init()
{
    return init("");
}

int KmBoxBManager::init(const std::string& portName)
{
    std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex);
    const LifecycleState previousLifecycle = lifecycleState.load(std::memory_order_acquire);
    if (previousLifecycle == LifecycleState::Stopping)
        return err_queue_stopped;
    if (previousLifecycle == LifecycleState::Stopped)
        lifecycleState.store(LifecycleState::Starting, std::memory_order_release);

    SetConnectionState(KmBoxConnectionState::Connecting);
    std::string port = portName.empty() ? find_port("USB-SERIAL CH340") : normalize_port_name(portName);
    if (port.empty()) {
        Diagnostics::Error("[KMBOX-B] Serial port not found.");
        SetConnectionState(KmBoxConnectionState::Disconnected);
        if (previousLifecycle == LifecycleState::Stopped)
            lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
        return -1;
    }
    serialPortName = port;
    if (!OpenConfiguredPort()) {
        if (previousLifecycle == LifecycleState::Stopped)
            lifecycleState.store(LifecycleState::Stopped, std::memory_order_release);
        return -2;
    }
    StartWorkers();
    lifecycleState.store(LifecycleState::Running, std::memory_order_release);
    return 0;
}

void KmBoxBManager::km_move(int X, int Y)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseMove);
}

void KmBoxBManager::km_move_auto(int X, int Y, int runtime)
{
    std::string command = "km.move(" + std::to_string(X) + "," + std::to_string(Y) + "," + std::to_string(runtime) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseAutoMove);
}

void KmBoxBManager::km_click()
{
    std::string command = "km.left(" + std::to_string(1) + ")\r\n";
    std::string command1 = "km.left(" + std::to_string(0) + ")\r\n";
    EnqueueCommand(command, KmBoxCommandType::MouseButton);
    EnqueueCommand(
        command1,
        KmBoxCommandType::MouseButton,
        KmBoxOutputIntent::SafetyRelease);
}

int KmBoxBManager::km_left(bool down)
{
    std::string command = "km.left(" + std::to_string(down ? 1 : 0) + ")\r\n";
    if (OW::Config::kmboxDebugLog)
        Diagnostics::Info("[KMBOX-B] queue output command=km_left down=%d", down ? 1 : 0);
    return EnqueueCommand(command, KmBoxCommandType::MouseButton, OutputIntentForState(down));
}

int KmBoxBManager::km_right(bool down)
{
    std::string command = "km.right(" + std::to_string(down ? 1 : 0) + ")\r\n";
    if (OW::Config::kmboxDebugLog)
        Diagnostics::Info("[KMBOX-B] queue output command=km_right down=%d", down ? 1 : 0);
    return EnqueueCommand(command, KmBoxCommandType::MouseButton, OutputIntentForState(down));
}

int KmBoxBManager::km_middle(bool down)
{
    std::string command = "km.middle(" + std::to_string(down ? 1 : 0) + ")\r\n";
    return EnqueueCommand(command, KmBoxCommandType::MouseButton, OutputIntentForState(down));
}
