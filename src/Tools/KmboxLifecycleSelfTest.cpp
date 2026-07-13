#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <utility>

using namespace std::chrono_literals;

class KmboxLifecycleSelfTestAccess
{
public:
    static void StartNetwork(
        KmBoxNetManager& manager,
        std::function<int(unsigned int, const client_data&, int)> sender)
    {
        manager.configuredIp = "127.0.0.1";
        manager.configuredPort = 12345;
        manager.safetySendOverride = std::move(sender);
        manager.lifecycleState.store(
            KmBoxNetManager::LifecycleState::Running, std::memory_order_release);
        manager.queueRunning.store(true, std::memory_order_release);
        manager.heartbeatRunning.store(true, std::memory_order_release);
        manager.queueThread = std::thread(&KmBoxNetManager::QueueWorkerLoop, &manager);
        manager.heartbeatThread = std::thread(&KmBoxNetManager::HeartbeatLoop, &manager);
    }

    static int QueueNetworkNormal(KmBoxNetManager& manager)
    {
        KmBoxQueuedNetCommand command{};
        command.type = KmBoxCommandType::MouseButton;
        command.outputIntent = KmBoxOutputIntent::Normal;
        command.priority = KmBoxCommandPriority::Normal;
        command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
        command.enqueuedAt = std::chrono::steady_clock::now();
        return manager.EnqueueCommand(command);
    }

    static bool NetworkWorkersRequestedToStop(const KmBoxNetManager& manager)
    {
        return !manager.queueRunning.load(std::memory_order_acquire) &&
            !manager.heartbeatRunning.load(std::memory_order_acquire);
    }

    static int QueueNetworkSafety(KmBoxNetManager& manager)
    {
        KmBoxQueuedNetCommand command{};
        command.type = KmBoxCommandType::SafetyReleaseAll;
        command.outputIntent = KmBoxOutputIntent::SafetyRelease;
        command.priority = KmBoxCommandPriority::Safety;
        command.enqueuedAt = std::chrono::steady_clock::now();
        return manager.EnqueueCommand(command);
    }

    static bool NetworkStoppedSnapshot(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return manager.commandQueue.empty() &&
            !manager.queueRunning.load(std::memory_order_acquire) &&
            !manager.heartbeatRunning.load(std::memory_order_acquire) &&
            !manager.queueThread.joinable() &&
            !manager.heartbeatThread.joinable();
    }

    static void StartSerial(
        KmBoxBManager& manager,
        std::function<bool(const std::string&)> writer)
    {
        manager.serialPortName = "COM-SELF-TEST";
        manager.writeOverride = std::move(writer);
        manager.lifecycleState.store(
            KmBoxBManager::LifecycleState::Running, std::memory_order_release);
        manager.workerRunning.store(true, std::memory_order_release);
        manager.heartbeatRunning.store(true, std::memory_order_release);
        manager.queueThread = std::thread(&KmBoxBManager::QueueWorkerLoop, &manager);
        manager.heartbeatThread = std::thread(&KmBoxBManager::HeartbeatLoop, &manager);
    }

    static int QueueSerialNormal(KmBoxBManager& manager)
    {
        return manager.EnqueueCommand(
            "km.left(1)\r\n",
            KmBoxCommandType::MouseButton,
            KmBoxOutputIntent::Normal);
    }

    static bool SerialWorkersRequestedToStop(const KmBoxBManager& manager)
    {
        return !manager.workerRunning.load(std::memory_order_acquire) &&
            !manager.heartbeatRunning.load(std::memory_order_acquire);
    }

    static bool SerialStoppedSnapshot(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return manager.commandQueue.empty() &&
            !manager.workerRunning.load(std::memory_order_acquire) &&
            !manager.heartbeatRunning.load(std::memory_order_acquire) &&
            !manager.queueThread.joinable() &&
            !manager.heartbeatThread.joinable();
    }
};

namespace
{
    int Fail(const char* message)
    {
        std::fprintf(stderr, "[KmboxLifecycleSelfTest] FAIL: %s\n", message);
        return 1;
    }

    int TestMockLifecycle()
    {
        kmbox::MockHardware mock;
        if (mock.GetLifecycleState() != kmbox::MockHardware::LifecycleState::Stopped ||
            mock.Initialize() != success ||
            mock.GetLifecycleState() != kmbox::MockHardware::LifecycleState::Running ||
            mock.RecordButton(0, true) != success) {
            return Fail("mock did not enter Running independently");
        }
        if (mock.Shutdown(50ms) != success ||
            mock.GetLifecycleState() != kmbox::MockHardware::LifecycleState::Stopped ||
            mock.RecordButton(0, true) != err_queue_stopped ||
            mock.Shutdown(50ms) != success) {
            return Fail("mock shutdown is not idempotent or did not reject post-stop output");
        }
        return 0;
    }

    int TestUninitializedShutdown()
    {
        KmBoxNetManager network;
        KmBoxBManager serial;
        if (network.Shutdown(20ms) != success ||
            serial.Shutdown(20ms) != success ||
            network.GetLifecycleState() != KmBoxNetManager::LifecycleState::Stopped ||
            serial.GetLifecycleState() != KmBoxBManager::LifecycleState::Stopped ||
            !KmboxLifecycleSelfTestAccess::NetworkStoppedSnapshot(network) ||
            !KmboxLifecycleSelfTestAccess::SerialStoppedSnapshot(serial)) {
            return Fail("uninitialized shutdown was not an idempotent no-op");
        }
        return 0;
    }

    int TestNetworkLifecycle()
    {
        KmBoxNetManager manager;
        KmboxLifecycleSelfTestAccess::StartNetwork(
            manager,
            [](unsigned int, const client_data&, int) { return success; });
        if (manager.GetLifecycleState() != KmBoxNetManager::LifecycleState::Running)
            return Fail("network did not enter Running");
        if (manager.Shutdown(100ms) != success ||
            manager.GetLifecycleState() != KmBoxNetManager::LifecycleState::Stopped ||
            KmboxLifecycleSelfTestAccess::QueueNetworkNormal(manager) != err_queue_stopped ||
            manager.Shutdown(100ms) != success ||
            !KmboxLifecycleSelfTestAccess::NetworkStoppedSnapshot(manager)) {
            return Fail("network shutdown is not idempotent or accepted post-stop output");
        }
        return 0;
    }

    int TestSerialLifecycle()
    {
        KmBoxBManager manager;
        KmboxLifecycleSelfTestAccess::StartSerial(
            manager,
            [](const std::string&) { return true; });
        if (manager.GetLifecycleState() != KmBoxBManager::LifecycleState::Running)
            return Fail("serial did not enter Running");
        if (manager.Shutdown(100ms) != success ||
            manager.GetLifecycleState() != KmBoxBManager::LifecycleState::Stopped ||
            KmboxLifecycleSelfTestAccess::QueueSerialNormal(manager) != err_queue_stopped ||
            manager.Shutdown(100ms) != success ||
            !KmboxLifecycleSelfTestAccess::SerialStoppedSnapshot(manager)) {
            return Fail("serial shutdown is not idempotent or accepted post-stop output");
        }
        return 0;
    }

    int TestSlowNetworkCleanupIsFinite()
    {
        KmBoxNetManager manager;
        std::atomic<bool> firstSendStarted{ false };
        KmboxLifecycleSelfTestAccess::StartNetwork(
            manager,
            [&](unsigned int, const client_data&, int) {
                if (!firstSendStarted.exchange(true, std::memory_order_acq_rel))
                    std::this_thread::sleep_for(40ms);
                return success;
            });
        if (KmboxLifecycleSelfTestAccess::QueueNetworkSafety(manager) != success)
            return Fail("could not queue slow network cleanup");

        const auto startDeadline = std::chrono::steady_clock::now() + 200ms;
        while (!firstSendStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < startDeadline) {
            std::this_thread::sleep_for(1ms);
        }
        if (!firstSendStarted.load(std::memory_order_acquire))
            return Fail("slow network transport did not start");

        const auto started = std::chrono::steady_clock::now();
        const int status = manager.Shutdown(5ms);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (status != err_completion_timeout || elapsed > 250ms ||
            manager.GetLifecycleState() != KmBoxNetManager::LifecycleState::Stopped ||
            !KmboxLifecycleSelfTestAccess::NetworkStoppedSnapshot(manager)) {
            return Fail("slow network cleanup did not time out and finish within its bound");
        }
        return 0;
    }

    int TestNetworkOwnedWorkerDefers()
    {
        KmBoxNetManager manager;
        std::atomic<int> deferredStatus{ success };
        std::atomic<bool> requested{ false };
        KmboxLifecycleSelfTestAccess::StartNetwork(
            manager,
            [&](unsigned int, const client_data&, int) {
                if (!requested.exchange(true, std::memory_order_acq_rel))
                    deferredStatus.store(manager.Shutdown(20ms), std::memory_order_release);
                return success;
            });

        if (manager.ReleaseAllOutputAndWait(200ms) != success)
            return Fail("network worker safety cleanup did not complete");
        if (deferredStatus.load(std::memory_order_acquire) != err_queue_stopped ||
            manager.GetLifecycleState() != KmBoxNetManager::LifecycleState::Stopping ||
            !KmboxLifecycleSelfTestAccess::NetworkWorkersRequestedToStop(manager)) {
            return Fail("network owned-worker shutdown did not request stop and defer");
        }
        if (manager.Shutdown(100ms) != success ||
            manager.GetLifecycleState() != KmBoxNetManager::LifecycleState::Stopped ||
            !KmboxLifecycleSelfTestAccess::NetworkStoppedSnapshot(manager)) {
            return Fail("network external shutdown did not finalize deferred stop");
        }
        return 0;
    }

    int TestSerialOwnedWorkerDefers()
    {
        KmBoxBManager manager;
        std::atomic<int> deferredStatus{ success };
        std::atomic<bool> requested{ false };
        KmboxLifecycleSelfTestAccess::StartSerial(
            manager,
            [&](const std::string&) {
                if (!requested.exchange(true, std::memory_order_acq_rel))
                    deferredStatus.store(manager.Shutdown(20ms), std::memory_order_release);
                return true;
            });

        if (manager.ReleaseAllOutputAndWait(200ms) != success)
            return Fail("serial worker safety cleanup did not complete");
        if (deferredStatus.load(std::memory_order_acquire) != err_queue_stopped ||
            manager.GetLifecycleState() != KmBoxBManager::LifecycleState::Stopping ||
            !KmboxLifecycleSelfTestAccess::SerialWorkersRequestedToStop(manager)) {
            return Fail("serial owned-worker shutdown did not request stop and defer");
        }
        if (manager.Shutdown(100ms) != success ||
            manager.GetLifecycleState() != KmBoxBManager::LifecycleState::Stopped ||
            !KmboxLifecycleSelfTestAccess::SerialStoppedSnapshot(manager)) {
            return Fail("serial external shutdown did not finalize deferred stop");
        }
        return 0;
    }
}

int main()
{
    OW::Config::Menu = false;
    OW::Config::kmboxSuppressOutputWhileMenuOpen = true;

    if (TestUninitializedShutdown() != 0 ||
        TestMockLifecycle() != 0 ||
        TestNetworkLifecycle() != 0 ||
        TestSerialLifecycle() != 0 ||
        TestSlowNetworkCleanupIsFinite() != 0 ||
        TestNetworkOwnedWorkerDefers() != 0 ||
        TestSerialOwnedWorkerDefers() != 0) {
        return 1;
    }

    std::puts("[KmboxLifecycleSelfTest] PASS");
    return 0;
}
