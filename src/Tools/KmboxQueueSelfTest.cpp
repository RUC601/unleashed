#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

class KmboxQueueSelfTestAccess
{
public:
    static void PrepareNetwork(
        KmBoxNetManager& manager,
        std::function<int(unsigned int, const client_data&, int)> sender)
    {
        manager.configuredIp = "127.0.0.1";
        manager.configuredPort = 12345;
        manager.safetySendOverride = std::move(sender);
        manager.queueRunning.store(true, std::memory_order_release);
    }

    static int QueueNetworkNormal(KmBoxNetManager& manager, KmBoxCommandType type)
    {
        KmBoxQueuedNetCommand command{};
        command.type = type;
        command.outputIntent = KmBoxOutputIntent::Normal;
        command.priority = KmBoxCommandPriority::Normal;
        command.length = sizeof(cmd_head_t);
        command.enqueuedAt = std::chrono::steady_clock::now();
        return manager.EnqueueCommand(command);
    }

    static int QueueNetworkMove(KmBoxNetManager& manager, int x)
    {
        KmBoxQueuedNetCommand command{};
        command.type = KmBoxCommandType::MouseMove;
        command.outputIntent = KmBoxOutputIntent::Normal;
        command.priority = KmBoxCommandPriority::Normal;
        command.data.cmd_mouse.x = x;
        command.length = sizeof(cmd_head_t) + sizeof(soft_mouse_t);
        command.enqueuedAt = std::chrono::steady_clock::now();
        return manager.EnqueueCommand(command);
    }

    static std::vector<std::pair<KmBoxCommandType, int>> NetworkQueueSnapshot(
        KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        std::vector<std::pair<KmBoxCommandType, int>> snapshot;
        for (const auto& command : manager.commandQueue)
            snapshot.emplace_back(command.type, command.data.cmd_mouse.x);
        return snapshot;
    }

    static int QueueNetworkSafety(
        KmBoxNetManager& manager,
        const std::shared_ptr<KmBoxCommandCompletion>& completion)
    {
        KmBoxQueuedNetCommand command{};
        command.type = KmBoxCommandType::SafetyReleaseAll;
        command.outputIntent = KmBoxOutputIntent::SafetyRelease;
        command.priority = KmBoxCommandPriority::Safety;
        command.completion = completion;
        command.enqueuedAt = std::chrono::steady_clock::now();
        return manager.EnqueueCommand(command);
    }

    static void FillNetworkWithSafety(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            KmBoxQueuedNetCommand command{};
            command.type = KmBoxCommandType::SafetyReleaseAll;
            command.outputIntent = KmBoxOutputIntent::SafetyRelease;
            command.priority = KmBoxCommandPriority::Safety;
            manager.commandQueue.push_back(std::move(command));
        }
    }

    static std::size_t NetworkQueueSize(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return manager.commandQueue.size();
    }

    static void StartNetworkWorker(KmBoxNetManager& manager)
    {
        manager.queueThread = std::thread(&KmBoxNetManager::QueueWorkerLoop, &manager);
    }

    static void StopNetworkWorker(KmBoxNetManager& manager)
    {
        manager.queueRunning.store(false, std::memory_order_release);
        manager.queueCv.notify_all();
        if (manager.queueThread.joinable())
            manager.queueThread.join();
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
    }

    static void PrepareSerial(
        KmBoxBManager& manager,
        std::function<bool(const std::string&)> writer)
    {
        manager.writeOverride = std::move(writer);
        manager.workerRunning.store(true, std::memory_order_release);
    }

    static int QueueSerialNormal(KmBoxBManager& manager, const std::string& text)
    {
        return manager.EnqueueCommand(
            text,
            KmBoxCommandType::MouseButton,
            KmBoxOutputIntent::Normal);
    }

    static int QueueSerialMove(KmBoxBManager& manager, const std::string& text)
    {
        return manager.EnqueueCommand(
            text,
            KmBoxCommandType::MouseMove,
            KmBoxOutputIntent::Normal);
    }

    static std::vector<KmBoxCommandType> SerialQueueSnapshot(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        std::vector<KmBoxCommandType> snapshot;
        for (const auto& command : manager.commandQueue)
            snapshot.push_back(command.type);
        return snapshot;
    }

    static int QueueSerialSafety(
        KmBoxBManager& manager,
        const std::shared_ptr<KmBoxCommandCompletion>& completion)
    {
        return manager.EnqueueCommand(
            {},
            KmBoxCommandType::SafetyReleaseAll,
            KmBoxOutputIntent::SafetyRelease,
            KmBoxCommandPriority::Safety,
            completion);
    }

    static std::size_t SerialQueueSize(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return manager.commandQueue.size();
    }

    static void FillSerialWithSafety(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            KmBoxQueuedSerialCommand command{};
            command.type = KmBoxCommandType::SafetyReleaseAll;
            command.outputIntent = KmBoxOutputIntent::SafetyRelease;
            command.priority = KmBoxCommandPriority::Safety;
            manager.commandQueue.push_back(std::move(command));
        }
    }

    static void StartSerialWorker(KmBoxBManager& manager)
    {
        manager.queueThread = std::thread(&KmBoxBManager::QueueWorkerLoop, &manager);
    }

    static void StopSerialWorker(KmBoxBManager& manager)
    {
        manager.workerRunning.store(false, std::memory_order_release);
        manager.queueCv.notify_all();
        if (manager.queueThread.joinable())
            manager.queueThread.join();
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
    }
};

namespace
{
    int Fail(const char* message)
    {
        std::fprintf(stderr, "[KmboxQueueSelfTest] FAIL: %s\n", message);
        return 1;
    }

    int WaitForCompletion(
        const std::shared_ptr<KmBoxCommandCompletion>& completion,
        std::chrono::milliseconds timeout = 500ms)
    {
        std::unique_lock<std::mutex> lock(completion->mutex);
        if (!completion->cv.wait_for(lock, timeout, [&completion]() {
                return completion->completed;
            })) {
            return err_completion_timeout;
        }
        return completion->status;
    }

    bool CompletionEquals(
        const std::shared_ptr<KmBoxCommandCompletion>& completion,
        int expectedStatus)
    {
        std::lock_guard<std::mutex> lock(completion->mutex);
        return completion->completed && completion->status == expectedStatus;
    }

    int TestNormalQueueSemantics()
    {
        KmBoxNetManager network;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            network,
            [](unsigned int, const client_data&, int) { return success; });
        if (KmboxQueueSelfTestAccess::QueueNetworkMove(network, 2) != success ||
            KmboxQueueSelfTestAccess::QueueNetworkMove(network, 3) != success ||
            KmboxQueueSelfTestAccess::QueueNetworkNormal(
                network, KmBoxCommandType::MouseButton) != success) {
            return Fail("could not prepare normal network queue semantics test");
        }
        const auto networkQueue = KmboxQueueSelfTestAccess::NetworkQueueSnapshot(network);
        if (networkQueue != std::vector<std::pair<KmBoxCommandType, int>>{
                { KmBoxCommandType::MouseButton, 0 },
                { KmBoxCommandType::MouseMove, 5 } }) {
            return Fail("normal network coalescing or button-before-move ordering changed");
        }
        KmboxQueueSelfTestAccess::StopNetworkWorker(network);

        KmBoxBManager serial;
        KmboxQueueSelfTestAccess::PrepareSerial(
            serial, [](const std::string&) { return true; });
        if (KmboxQueueSelfTestAccess::QueueSerialMove(serial, "move-a") != success ||
            KmboxQueueSelfTestAccess::QueueSerialMove(serial, "move-b") != success ||
            KmboxQueueSelfTestAccess::QueueSerialNormal(serial, "button") != success) {
            return Fail("could not prepare normal serial queue semantics test");
        }
        if (KmboxQueueSelfTestAccess::SerialQueueSnapshot(serial) !=
            std::vector<KmBoxCommandType>{
                KmBoxCommandType::MouseButton,
                KmBoxCommandType::MouseMove,
                KmBoxCommandType::MouseMove }) {
            return Fail("normal serial button-before-move ordering changed");
        }
        KmboxQueueSelfTestAccess::StopSerialWorker(serial);
        return 0;
    }

    int TestNetworkPriorityAndSequence()
    {
        KmBoxNetManager manager;
        std::mutex eventsMutex;
        std::vector<unsigned int> events;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [&](unsigned int command, const client_data&, int) {
                std::lock_guard<std::mutex> lock(eventsMutex);
                events.push_back(command);
                return success;
            });

        if (KmboxQueueSelfTestAccess::QueueNetworkNormal(
                manager, KmBoxCommandType::MouseButton) != success ||
            KmboxQueueSelfTestAccess::QueueNetworkNormal(
                manager, KmBoxCommandType::Keyboard) != success ||
            KmboxQueueSelfTestAccess::QueueNetworkNormal(
                manager, KmBoxCommandType::MouseMask) != success) {
            return Fail("could not prepare network normal backlog");
        }

        auto first = std::make_shared<KmBoxCommandCompletion>();
        auto second = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueNetworkSafety(manager, first) != success ||
            KmboxQueueSelfTestAccess::QueueNetworkSafety(manager, second) != success) {
            return Fail("network safety command was not accepted");
        }
        if (KmboxQueueSelfTestAccess::NetworkQueueSize(manager) != 2)
            return Fail("network safety barrier did not cancel old normal outputs");

        KmboxQueueSelfTestAccess::StartNetworkWorker(manager);
        const int firstStatus = WaitForCompletion(first);
        const int secondStatus = WaitForCompletion(second);
        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        if (firstStatus != success || secondStatus != success)
            return Fail("network safety completion did not report send success");

        const std::vector<unsigned int> oneSequence{
            cmd_mouse_left,
            cmd_mouse_right,
            cmd_mouse_middle,
            cmd_keyboard_all,
            cmd_unmask_all
        };
        std::vector<unsigned int> expected = oneSequence;
        expected.insert(expected.end(), oneSequence.begin(), oneSequence.end());
        if (events != expected)
            return Fail("network safety commands were reordered, coalesced, or followed by stale output");
        return 0;
    }

    int TestNetworkFullQueueAndFailure()
    {
        KmBoxNetManager manager;
        std::atomic<int> attempts{ 0 };
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [&](unsigned int command, const client_data&, int) {
                attempts.fetch_add(1, std::memory_order_relaxed);
                return command == cmd_keyboard_all ? err_net_tx : success;
            });

        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            if (KmboxQueueSelfTestAccess::QueueNetworkNormal(
                    manager, KmBoxCommandType::MouseButton) != success) {
                return Fail("could not fill network normal queue");
            }
        }
        auto completion = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueNetworkSafety(manager, completion) != success ||
            KmboxQueueSelfTestAccess::NetworkQueueSize(manager) != 1) {
            return Fail("network safety command was rejected by a full normal queue");
        }

        KmboxQueueSelfTestAccess::StartNetworkWorker(manager);
        const int status = WaitForCompletion(completion);
        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        if (status != err_net_tx || attempts.load(std::memory_order_relaxed) != 5)
            return Fail("network send failure did not complete after all transport attempts");

        KmboxQueueSelfTestAccess::PrepareNetwork(manager, {});
        KmboxQueueSelfTestAccess::FillNetworkWithSafety(manager);
        auto rejected = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueNetworkSafety(manager, rejected) != err_queue_full ||
            !CompletionEquals(rejected, err_queue_full)) {
            return Fail("all-safety network queue did not fail closed");
        }
        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        return 0;
    }

    int TestNetworkTimeoutIsBounded()
    {
        KmBoxNetManager manager;
        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool release = false;
        std::atomic<bool> attempted{ false };
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [&](unsigned int, const client_data&, int) {
                attempted.store(true, std::memory_order_release);
                std::unique_lock<std::mutex> lock(gateMutex);
                gateCv.wait(lock, [&release]() { return release; });
                return success;
            });
        KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

        const auto started = std::chrono::steady_clock::now();
        const int status = manager.ReleaseAllOutputAndWait(20ms);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (status != err_completion_timeout || elapsed > 250ms)
            return Fail("network completion timeout was not bounded");
        if (!attempted.load(std::memory_order_acquire))
            return Fail("network waiter timed out before a transport attempt began");

        {
            std::lock_guard<std::mutex> lock(gateMutex);
            release = true;
        }
        gateCv.notify_all();
        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);

        KmBoxNetManager disconnected;
        const auto disconnectedStarted = std::chrono::steady_clock::now();
        if (disconnected.ReleaseAllOutputAndWait(20ms) != err_creat_socket ||
            std::chrono::steady_clock::now() - disconnectedStarted > 100ms) {
            return Fail("disconnected network cleanup was not finite");
        }
        return 0;
    }

    int TestSerialPriorityAndSequence()
    {
        KmBoxBManager manager;
        std::vector<std::string> writes;
        KmboxQueueSelfTestAccess::PrepareSerial(
            manager,
            [&](const std::string& command) {
                writes.push_back(command);
                return true;
            });
        if (KmboxQueueSelfTestAccess::QueueSerialNormal(manager, "km.left(1)\r\n") != success)
            return Fail("could not prepare serial backlog");
        auto completion = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueSerialSafety(manager, completion) != success ||
            KmboxQueueSelfTestAccess::SerialQueueSize(manager) != 1) {
            return Fail("serial safety barrier did not cancel old normal output");
        }

        KmboxQueueSelfTestAccess::StartSerialWorker(manager);
        const int status = WaitForCompletion(completion);
        KmboxQueueSelfTestAccess::StopSerialWorker(manager);
        if (status != success || writes != std::vector<std::string>{
                "km.left(0)\r\n", "km.right(0)\r\n", "km.middle(0)\r\n" }) {
            return Fail("serial safety release order or completion status is wrong");
        }

        KmboxQueueSelfTestAccess::PrepareSerial(manager, [](const std::string&) {
            return true;
        });
        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            if (KmboxQueueSelfTestAccess::QueueSerialNormal(
                    manager, "km.left(1)\r\n") != success) {
                return Fail("could not fill serial normal queue");
            }
        }
        auto accepted = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueSerialSafety(manager, accepted) != success ||
            KmboxQueueSelfTestAccess::SerialQueueSize(manager) != 1) {
            return Fail("serial safety command was rejected by a full normal queue");
        }
        KmboxQueueSelfTestAccess::FillSerialWithSafety(manager);
        auto rejected = std::make_shared<KmBoxCommandCompletion>();
        if (KmboxQueueSelfTestAccess::QueueSerialSafety(manager, rejected) != err_queue_full ||
            !CompletionEquals(rejected, err_queue_full)) {
            return Fail("all-safety serial queue did not fail closed");
        }
        KmboxQueueSelfTestAccess::StopSerialWorker(manager);
        return 0;
    }

    int TestMockReleaseAll()
    {
        using namespace kmbox;
        MockHardware mock;
        if (mock.Initialize() != success ||
            mock.RecordButton(0, true) != success ||
            mock.RecordKeyboardKey(KEY_E, true) != success ||
            mock.MaskMouse(0x7Fu) != success) {
            return Fail("could not prepare mock output state");
        }
        if (mock.ReleaseAllOutputAndWait(50ms) != success)
            return Fail("mock safety cleanup failed");
        const MockHardwareSnapshot snapshot = mock.Snapshot();
        if (snapshot.outputMouseButtons != 0 || snapshot.maskedButtons != 0 ||
            snapshot.outputKeyboardKeys != 0) {
            return Fail("mock safety cleanup left output state held");
        }

        mock.SetFaultMode(MockFaultMode::OutputTimeout);
        const auto started = std::chrono::steady_clock::now();
        if (mock.ReleaseAllOutputAndWait(20ms) != err_net_rx_timeout ||
            std::chrono::steady_clock::now() - started > 100ms) {
            return Fail("mock timeout was not returned promptly");
        }
        return 0;
    }
}

int main()
{
    OW::Config::Menu = false;
    OW::Config::kmboxSuppressOutputWhileMenuOpen = true;

    if (TestNormalQueueSemantics() != 0 ||
        TestNetworkPriorityAndSequence() != 0 ||
        TestNetworkFullQueueAndFailure() != 0 ||
        TestNetworkTimeoutIsBounded() != 0 ||
        TestSerialPriorityAndSequence() != 0 ||
        TestMockReleaseAll() != 0) {
        return 1;
    }

    std::puts("[KmboxQueueSelfTest] PASS");
    return 0;
}
