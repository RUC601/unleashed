#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"

#include <algorithm>
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

    static int QueueInvalidNetworkSafetyMove(KmBoxNetManager& manager)
    {
        KmBoxQueuedNetCommand command{};
        command.type = KmBoxCommandType::MouseMove;
        command.outputIntent = KmBoxOutputIntent::SafetyRelease;
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

    static std::vector<KmBoxQueuedNetCommand> NetworkCommands(
        KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return { manager.commandQueue.begin(), manager.commandQueue.end() };
    }

    static std::pair<unsigned char, std::array<unsigned char, 10>>
    NetworkDesiredKeyboard(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.keyboardStateMutex);
        return {
            manager.desiredKeyboardModifierMask,
            manager.desiredKeyboardUsages
        };
    }

    static std::pair<unsigned char, std::array<unsigned char, 10>>
    NetworkLastSentKeyboard(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.keyboardStateMutex);
        return {
            manager.lastSentKeyboardModifierMask,
            manager.lastSentKeyboardUsages
        };
    }

    static unsigned int NetworkDesiredMouseButtons(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.mouseStateMutex);
        return static_cast<unsigned int>(manager.Mouse.MouseData.button) & 0x07u;
    }

    static unsigned int NetworkLastSentMouseButtons(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.mouseStateMutex);
        return manager.lastSentMouseButtonStateMask & 0x07u;
    }

    static void SeedNetworkTrackedOutput(
        KmBoxNetManager& manager,
        unsigned int mouseButtons,
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages)
    {
        std::scoped_lock lock(
            manager.mouseStateMutex,
            manager.keyboardStateMutex);
        manager.Mouse.MouseData.button = static_cast<int>(mouseButtons & 0x07u);
        manager.lastSentMouseButtonStateMask = mouseButtons & 0x07u;
        manager.desiredKeyboardModifierMask = modifierMask;
        manager.desiredKeyboardUsages.fill(0);
        manager.lastSentKeyboardModifierMask = modifierMask;
        manager.lastSentKeyboardUsages.fill(0);
        std::copy(
            usages.begin(), usages.end(), manager.desiredKeyboardUsages.begin());
        std::copy(
            usages.begin(), usages.end(), manager.lastSentKeyboardUsages.begin());
    }

    static void ClearNetworkQueue(KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
    }

    static void FillNetworkWithNormal(
        KmBoxNetManager& manager,
        KmBoxCommandType type)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            KmBoxQueuedNetCommand command{};
            command.type = type;
            command.outputIntent = KmBoxOutputIntent::Normal;
            command.priority = KmBoxCommandPriority::Normal;
            manager.commandQueue.push_back(std::move(command));
        }
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

    static void FillNetworkWithProtectedKeyboardSafety(
        KmBoxNetManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();

        KmBoxQueuedNetCommand predecessor{};
        predecessor.type = KmBoxCommandType::Keyboard;
        predecessor.outputIntent = KmBoxOutputIntent::Normal;
        predecessor.priority = KmBoxCommandPriority::Normal;
        manager.commandQueue.push_back(std::move(predecessor));

        for (int i = 1; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            KmBoxQueuedNetCommand release{};
            release.type = KmBoxCommandType::Keyboard;
            release.outputIntent = KmBoxOutputIntent::SafetyRelease;
            release.priority = KmBoxCommandPriority::Safety;
            manager.commandQueue.push_back(std::move(release));
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

    static int QueueInvalidSerialSafetyDown(KmBoxBManager& manager)
    {
        return manager.EnqueueCommand(
            "km.left(1)\r\n",
            KmBoxCommandType::MouseButton,
            KmBoxOutputIntent::SafetyRelease);
    }

    static std::vector<KmBoxCommandType> SerialQueueSnapshot(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        std::vector<KmBoxCommandType> snapshot;
        for (const auto& command : manager.commandQueue)
            snapshot.push_back(command.type);
        return snapshot;
    }

    static std::vector<KmBoxQueuedSerialCommand> SerialCommands(
        KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        return { manager.commandQueue.begin(), manager.commandQueue.end() };
    }

    static void FillSerialWithNormalButtons(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
        for (int i = 0; i < KmBoxRuntimeConfig::CommandQueueMaxSize; ++i) {
            KmBoxQueuedSerialCommand command{};
            command.command = "km.left(1)\r\n";
            command.type = KmBoxCommandType::MouseButton;
            command.outputIntent = KmBoxOutputIntent::Normal;
            command.priority = KmBoxCommandPriority::Normal;
            manager.commandQueue.push_back(std::move(command));
        }
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

    static void ClearSerialQueue(KmBoxBManager& manager)
    {
        std::lock_guard<std::mutex> lock(manager.queueMutex);
        manager.commandQueue.clear();
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

    bool WaitForNetworkQueueToDrain(
        KmBoxNetManager& manager,
        std::chrono::milliseconds timeout = 500ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (KmboxQueueSelfTestAccess::NetworkQueueSize(manager) == 0)
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return KmboxQueueSelfTestAccess::NetworkQueueSize(manager) == 0;
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

    int TestOrderedKeyboardSafety()
    {
        KmBoxNetManager manager;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [](unsigned int, const client_data&, int) { return success; });

        if (manager.SendKeyboardReport(
                BIT1, { KEY_E }, KmBoxOutputIntent::Normal) != success ||
            manager.SendKeyboardReport(
                0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != success) {
            return Fail("could not queue ordered Shift+E release sequence");
        }

        const auto ordered = KmboxQueueSelfTestAccess::NetworkCommands(manager);
        if (ordered.size() != 2 ||
            ordered[0].type != KmBoxCommandType::Keyboard ||
            ordered[0].priority != KmBoxCommandPriority::Normal ||
            static_cast<unsigned char>(ordered[0].data.cmd_keyboard.ctrl) != BIT1 ||
            static_cast<unsigned char>(ordered[0].data.cmd_keyboard.button[0]) != KEY_E ||
            ordered[1].type != KmBoxCommandType::Keyboard ||
            ordered[1].priority != KmBoxCommandPriority::Safety ||
            ordered[1].outputIntent != KmBoxOutputIntent::SafetyRelease ||
            ordered[1].data.cmd_keyboard.ctrl != 0 ||
            static_cast<unsigned char>(ordered[1].data.cmd_keyboard.button[0]) != KEY_E) {
            return Fail("partial keyboard safety release was reordered ahead of its press");
        }

        const std::size_t beforeInvalid = ordered.size();
        if (manager.SendKeyboardReport(
                0,
                { KEY_E, KEY_W },
                KmBoxOutputIntent::SafetyRelease) != err_net_cmd ||
            KmboxQueueSelfTestAccess::NetworkQueueSize(manager) != beforeInvalid) {
            return Fail("keyboard safety report added a new desired key");
        }

        const auto desired =
            KmboxQueueSelfTestAccess::NetworkDesiredKeyboard(manager);
        if (desired.first != 0 || desired.second[0] != KEY_E)
            return Fail("invalid keyboard safety report changed desired state");

        KmboxQueueSelfTestAccess::FillNetworkWithSafety(manager);
        if (manager.SendKeyboardReport(
                0, { KEY_W }, KmBoxOutputIntent::Normal) != err_queue_full) {
            return Fail("all-safety queue unexpectedly accepted normal keyboard state");
        }
        const auto rolledBack =
            KmboxQueueSelfTestAccess::NetworkDesiredKeyboard(manager);
        if (rolledBack.first != 0 || rolledBack.second[0] != KEY_E)
            return Fail("failed keyboard enqueue did not roll desired state back");

        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        return 0;
    }

    int TestOrderedSafetyWorkerGates()
    {
        {
            KmBoxNetManager manager;
            std::mutex sendsMutex;
            std::vector<client_data> sends;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int, const client_data& packet, int) {
                    std::lock_guard<std::mutex> lock(sendsMutex);
                    sends.push_back(packet);
                    return success;
                });

            OW::Config::Menu = false;
            if (manager.SendKeyboardReport(
                    BIT1, { KEY_E }, KmBoxOutputIntent::Normal) != success ||
                manager.SendKeyboardReport(
                    0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != success) {
                return Fail("could not queue offline keyboard send sequence");
            }
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);
            if (!WaitForNetworkQueueToDrain(manager))
                return Fail("offline keyboard send queue did not drain");
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);

            std::lock_guard<std::mutex> lock(sendsMutex);
            if (sends.size() != 2 ||
                static_cast<unsigned char>(sends[0].cmd_keyboard.ctrl) != BIT1 ||
                static_cast<unsigned char>(sends[0].cmd_keyboard.button[0]) != KEY_E ||
                sends[1].cmd_keyboard.ctrl != 0 ||
                static_cast<unsigned char>(sends[1].cmd_keyboard.button[0]) != KEY_E) {
                return Fail("offline worker did not preserve keyboard press-release order");
            }
        }

        {
            KmBoxNetManager manager;
            std::atomic<int> sends{ 0 };
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int, const client_data&, int) {
                    sends.fetch_add(1, std::memory_order_relaxed);
                    return success;
                });

            OW::Config::Menu = true;
            if (manager.SendKeyboardReport(
                    BIT1, { KEY_E }, KmBoxOutputIntent::Normal) != success ||
                manager.SendKeyboardReport(
                    0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != success) {
                return Fail("could not queue menu-suppressed keyboard sequence");
            }
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);
            if (!WaitForNetworkQueueToDrain(manager))
                return Fail("menu-suppressed keyboard queue did not drain");
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            if (sends.load(std::memory_order_relaxed) != 0)
                return Fail("partial safety report injected key after its press was suppressed");
        }

        {
            KmBoxNetManager manager;
            std::atomic<int> sends{ 0 };
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int, const client_data&, int) {
                    sends.fetch_add(1, std::memory_order_relaxed);
                    return success;
                });

            OW::Config::Menu = true;
            if (manager.SetMouseButtonStateMask(0x02u) != success ||
                manager.ForceReleaseMouseButton(0) != success) {
                return Fail("could not queue menu-suppressed mouse full-state sequence");
            }
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);
            if (!WaitForNetworkQueueToDrain(manager))
                return Fail("menu-suppressed mouse queue did not drain");
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            if (sends.load(std::memory_order_relaxed) != 0)
                return Fail("mouse safety release injected an unsent preserved button");
        }

        OW::Config::Menu = false;
        return 0;
    }

    int TestOrderedSafetyQueuePressure()
    {
        KmBoxNetManager network;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            network,
            [](unsigned int, const client_data&, int) { return success; });
        if (network.SendKeyboardReport(
                BIT1, { KEY_E }, KmBoxOutputIntent::Normal) != success) {
            return Fail("could not seed desired keyboard state for queue pressure");
        }
        KmboxQueueSelfTestAccess::FillNetworkWithNormal(
            network, KmBoxCommandType::Keyboard);
        if (network.SendKeyboardReport(
                0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != success) {
            return Fail("full network state queue rejected ordered safety release");
        }
        const auto networkCommands =
            KmboxQueueSelfTestAccess::NetworkCommands(network);
        if (networkCommands.size() !=
                static_cast<std::size_t>(KmBoxRuntimeConfig::CommandQueueMaxSize) ||
            networkCommands.size() < 2 ||
            networkCommands[networkCommands.size() - 2].type !=
                KmBoxCommandType::Keyboard ||
            networkCommands[networkCommands.size() - 2].priority !=
                KmBoxCommandPriority::Normal ||
            networkCommands.back().priority != KmBoxCommandPriority::Safety ||
            networkCommands.back().outputIntent != KmBoxOutputIntent::SafetyRelease) {
            return Fail("network queue did not protect ordered safety behind state predecessors");
        }

        for (int i = 0; i < 8; ++i) {
            const int result = network.SendKeyboardReport(
                0, { KEY_E }, KmBoxOutputIntent::SafetyRelease);
            if (result != success)
                return Fail("repeated network safety enqueue returned an unexpected status");
            if (KmboxQueueSelfTestAccess::NetworkQueueSize(network) >
                static_cast<std::size_t>(KmBoxRuntimeConfig::CommandQueueMaxSize)) {
                return Fail("repeated network safety enqueue exceeded the queue capacity");
            }
        }

        KmboxQueueSelfTestAccess::FillNetworkWithProtectedKeyboardSafety(network);
        if (network.SendKeyboardReport(
                0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != err_queue_full ||
            KmboxQueueSelfTestAccess::NetworkQueueSize(network) !=
                static_cast<std::size_t>(KmBoxRuntimeConfig::CommandQueueMaxSize)) {
            return Fail("saturated network safety queue did not fail closed at capacity");
        }
        const auto boundedCommands =
            KmboxQueueSelfTestAccess::NetworkCommands(network);
        const auto firstSafety = std::find_if(
            boundedCommands.begin(), boundedCommands.end(),
            [](const KmBoxQueuedNetCommand& command) {
                return command.outputIntent == KmBoxOutputIntent::SafetyRelease;
            });
        if (firstSafety == boundedCommands.begin() ||
            firstSafety == boundedCommands.end() ||
            std::prev(firstSafety)->type != KmBoxCommandType::Keyboard ||
            std::prev(firstSafety)->priority != KmBoxCommandPriority::Normal) {
            return Fail("bounded network safety queue lost its required predecessor");
        }
        if (KmboxQueueSelfTestAccess::QueueNetworkNormal(
                network, KmBoxCommandType::MouseButton) != err_queue_full ||
            KmboxQueueSelfTestAccess::NetworkQueueSize(network) !=
                boundedCommands.size()) {
            return Fail("later network normal command displaced protected safety state");
        }
        KmboxQueueSelfTestAccess::StopNetworkWorker(network);

        KmBoxBManager serial;
        KmboxQueueSelfTestAccess::PrepareSerial(
            serial, [](const std::string&) { return true; });
        KmboxQueueSelfTestAccess::FillSerialWithNormalButtons(serial);
        serial.km_left(false);
        const auto serialCommands =
            KmboxQueueSelfTestAccess::SerialCommands(serial);
        if (serialCommands.size() !=
                static_cast<std::size_t>(KmBoxRuntimeConfig::CommandQueueMaxSize) ||
            serialCommands.back().command != "km.left(0)\r\n" ||
            serialCommands.back().priority != KmBoxCommandPriority::Safety ||
            serialCommands.back().outputIntent != KmBoxOutputIntent::SafetyRelease) {
            return Fail("serial full queue did not retain ordered button release");
        }
        const int laterSerialStatus = KmboxQueueSelfTestAccess::QueueSerialNormal(
            serial, "km.right(1)\r\n");
        const auto serialAfterNormal =
            KmboxQueueSelfTestAccess::SerialCommands(serial);
        if (laterSerialStatus != success ||
            serialAfterNormal.size() != serialCommands.size() ||
            std::none_of(
                serialAfterNormal.begin(),
                serialAfterNormal.end(),
                [](const KmBoxQueuedSerialCommand& command) {
                    return command.outputIntent == KmBoxOutputIntent::SafetyRelease;
                })) {
            return Fail("later serial normal command displaced protected safety release");
        }

        KmboxQueueSelfTestAccess::FillSerialWithSafety(serial);
        for (int i = 0; i < 8; ++i) {
            if (serial.km_left(false) != err_queue_full ||
                KmboxQueueSelfTestAccess::SerialQueueSize(serial) !=
                    static_cast<std::size_t>(
                        KmBoxRuntimeConfig::CommandQueueMaxSize)) {
                return Fail("saturated serial safety queue did not stay bounded");
            }
        }
        KmboxQueueSelfTestAccess::ClearSerialQueue(serial);
        if (serial.km_left(false) != success) {
            return Fail("serial release did not surface successful retry enqueue");
        }
        const auto serialRetry =
            KmboxQueueSelfTestAccess::SerialCommands(serial);
        if (serialRetry.size() != 1 ||
            serialRetry.front().command != "km.left(0)\r\n" ||
            serialRetry.front().outputIntent !=
                KmBoxOutputIntent::SafetyRelease) {
            return Fail("serial release retry did not queue the safety command");
        }
        KmboxQueueSelfTestAccess::StopSerialWorker(serial);
        return 0;
    }

    int TestMouseDesiredStateRollsBackOnQueueFailure()
    {
        KmBoxNetManager manager;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [](unsigned int, const client_data&, int) { return success; });

        if (manager.SetMouseButtonStateMask(0x01u) != success ||
            KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0x01u) {
            return Fail("could not seed desired mouse state for rollback test");
        }

        KmboxQueueSelfTestAccess::FillNetworkWithSafety(manager);
        if (manager.SetMouseButtonStateMask(0x00u) != err_queue_full ||
            KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0x01u) {
            return Fail("failed mouse release prematurely changed desired state");
        }

        KmboxQueueSelfTestAccess::ClearNetworkQueue(manager);
        if (manager.SetMouseButtonStateMask(0x00u) != success ||
            KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0x00u) {
            return Fail("mouse release retry was coalesced instead of enqueued");
        }

        const auto commands =
            KmboxQueueSelfTestAccess::NetworkCommands(manager);
        if (commands.size() != 1 ||
            commands.front().type != KmBoxCommandType::MouseButton ||
            commands.front().outputIntent != KmBoxOutputIntent::SafetyRelease ||
            commands.front().mouseButtonStateMask != 0 ||
            commands.front().data.cmd_mouse.button != 0) {
            return Fail("mouse release retry did not enqueue the zero-button report");
        }

        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        return 0;
    }

    int TestReleaseAllQueueFailureRollsBackTrackedState()
    {
        KmBoxNetManager manager;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [](unsigned int, const client_data&, int) { return success; });
        KmboxQueueSelfTestAccess::SeedNetworkTrackedOutput(
            manager, 0x03u, BIT1, { KEY_E });
        KmboxQueueSelfTestAccess::FillNetworkWithSafety(manager);

        if (manager.ReleaseAllOutputAndWait(20ms) != err_queue_full)
            return Fail("full safety queue unexpectedly accepted release-all");

        const auto desiredKeyboard =
            KmboxQueueSelfTestAccess::NetworkDesiredKeyboard(manager);
        const auto lastKeyboard =
            KmboxQueueSelfTestAccess::NetworkLastSentKeyboard(manager);
        if (KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0x03u ||
            KmboxQueueSelfTestAccess::NetworkLastSentMouseButtons(manager) != 0x03u ||
            desiredKeyboard.first != BIT1 ||
            desiredKeyboard.second[0] != KEY_E ||
            lastKeyboard.first != BIT1 ||
            lastKeyboard.second[0] != KEY_E) {
            return Fail("failed release-all enqueue cleared tracked output state");
        }

        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        return 0;
    }

    int TestReleaseAllCommitsSuccessfulDomainsOnly()
    {
        KmBoxNetManager manager;
        std::atomic<int> failurePhase{ 0 };
        std::atomic<int> sendCount{ 0 };
        KmboxQueueSelfTestAccess::PrepareNetwork(
            manager,
            [&](unsigned int command, const client_data&, int) {
                sendCount.fetch_add(1, std::memory_order_relaxed);
                if (failurePhase.load(std::memory_order_acquire) == 1 &&
                    (command == cmd_mouse_right || command == cmd_keyboard_all)) {
                    return err_net_tx;
                }
                return success;
            });

        OW::Config::Menu = false;
        if (manager.SetMouseButtonStateMask(0x07u) != success ||
            manager.SendKeyboardReport(
                BIT1, { KEY_E }, KmBoxOutputIntent::Normal) != success) {
            return Fail("could not queue tracked output seed state");
        }
        KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

        const auto waitForTrackedState =
            [&](unsigned int mouseButtons,
                unsigned char modifierMask,
                unsigned char firstUsage) {
                const auto deadline = std::chrono::steady_clock::now() + 500ms;
                while (std::chrono::steady_clock::now() < deadline) {
                    const auto keyboard =
                        KmboxQueueSelfTestAccess::NetworkLastSentKeyboard(manager);
                    if (KmboxQueueSelfTestAccess::NetworkLastSentMouseButtons(manager) ==
                            mouseButtons &&
                        keyboard.first == modifierMask &&
                        keyboard.second[0] == firstUsage) {
                        return true;
                    }
                    std::this_thread::sleep_for(1ms);
                }
                return false;
            };

        if (!waitForTrackedState(0x07u, BIT1, KEY_E)) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("offline seed sends did not establish last-sent state");
        }

        failurePhase.store(1, std::memory_order_release);
        if (manager.ReleaseAllOutputAndWait(500ms) != err_net_tx) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("partial release-all failure did not surface transport error");
        }

        const auto failedDesiredKeyboard =
            KmboxQueueSelfTestAccess::NetworkDesiredKeyboard(manager);
        const auto failedLastKeyboard =
            KmboxQueueSelfTestAccess::NetworkLastSentKeyboard(manager);
        if (KmboxQueueSelfTestAccess::NetworkLastSentMouseButtons(manager) != 0x02u ||
            KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0x02u ||
            failedLastKeyboard.first != BIT1 ||
            failedLastKeyboard.second[0] != KEY_E ||
            failedDesiredKeyboard.first != BIT1 ||
            failedDesiredKeyboard.second[0] != KEY_E) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("partial release-all falsely cleared a failed output domain");
        }

        failurePhase.store(0, std::memory_order_release);
        if (manager.ForceReleaseMouseButton(1) != success ||
            manager.SendKeyboardReport(
                0, { KEY_E }, KmBoxOutputIntent::SafetyRelease) != success) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("failed output domains rejected safe partial retry");
        }
        if (!waitForTrackedState(0x00u, 0, KEY_E)) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("partial safety retry did not commit successful transport state");
        }

        const int beforeRetry = sendCount.load(std::memory_order_relaxed);
        if (manager.ReleaseAllOutputAndWait(500ms) != success) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("full release retry did not succeed");
        }
        const int retrySends =
            sendCount.load(std::memory_order_relaxed) - beforeRetry;
        const auto finalDesiredKeyboard =
            KmboxQueueSelfTestAccess::NetworkDesiredKeyboard(manager);
        const auto finalLastKeyboard =
            KmboxQueueSelfTestAccess::NetworkLastSentKeyboard(manager);
        if (retrySends != 5 ||
            KmboxQueueSelfTestAccess::NetworkDesiredMouseButtons(manager) != 0 ||
            KmboxQueueSelfTestAccess::NetworkLastSentMouseButtons(manager) != 0 ||
            finalDesiredKeyboard.first != 0 ||
            finalDesiredKeyboard.second[0] != 0 ||
            finalLastKeyboard.first != 0 ||
            finalLastKeyboard.second[0] != 0) {
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            return Fail("successful full retry did not clear every tracked domain");
        }

        KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
        return 0;
    }

    int TestSafetyIntentRejectsPresses()
    {
        KmBoxNetManager network;
        KmboxQueueSelfTestAccess::PrepareNetwork(
            network,
            [](unsigned int, const client_data&, int) { return success; });
        if (KmboxQueueSelfTestAccess::QueueInvalidNetworkSafetyMove(network) !=
                err_net_cmd ||
            KmboxQueueSelfTestAccess::NetworkQueueSize(network) != 0) {
            return Fail("network safety intent accepted a move command");
        }
        KmboxQueueSelfTestAccess::StopNetworkWorker(network);

        KmBoxBManager serial;
        KmboxQueueSelfTestAccess::PrepareSerial(
            serial, [](const std::string&) { return true; });
        if (KmboxQueueSelfTestAccess::QueueInvalidSerialSafetyDown(serial) !=
                err_net_cmd ||
            KmboxQueueSelfTestAccess::SerialQueueSize(serial) != 0) {
            return Fail("serial safety intent accepted a button press");
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

    int TestTrackedMouseMaskCompletionAndForceGate()
    {
        {
            KmBoxNetManager manager;
            std::vector<unsigned int> sends;
            int forceReleaseAttempts = 0;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int command, const client_data&, int) {
                    sends.push_back(command);
                    return success;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            auto maskCompletion = std::make_shared<KmBoxCommandCompletion>();
            const int maskEnqueue = manager.MaskMouseTracked(
                0x01u,
                maskCompletion);
            const int maskStatus = maskEnqueue == success
                ? WaitForCompletion(maskCompletion, 100ms)
                : maskEnqueue;
            if (maskStatus == success)
                ++forceReleaseAttempts;

            auto unmaskCompletion = std::make_shared<KmBoxCommandCompletion>();
            const int unmaskEnqueue = manager.UnmaskAllTracked(
                unmaskCompletion);
            const int unmaskStatus = unmaskEnqueue == success
                ? WaitForCompletion(unmaskCompletion, 100ms)
                : unmaskEnqueue;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            if (maskStatus != success || unmaskStatus != success ||
                sends != std::vector<unsigned int>{
                    cmd_mask_mouse,
                    cmd_unmask_all
                } ||
                forceReleaseAttempts != 1) {
                return Fail("tracked mask/unmask success was not transport-confirmed");
            }
        }

        {
            KmBoxNetManager manager;
            int sendAttempts = 0;
            int forceReleaseAttempts = 0;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int, const client_data&, int) {
                    ++sendAttempts;
                    return err_net_tx;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            auto completion = std::make_shared<KmBoxCommandCompletion>();
            const int enqueueStatus = manager.MaskMouseTracked(0x01u, completion);
            const int status = enqueueStatus == success
                ? WaitForCompletion(completion, 100ms)
                : enqueueStatus;
            if (status == success)
                ++forceReleaseAttempts;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            if (status != err_net_tx || sendAttempts != 1 ||
                forceReleaseAttempts != 0) {
                return Fail("failed network mask opened the force-release gate");
            }
        }

        {
            KmBoxNetManager manager;
            int sendAttempts = 0;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int command, const client_data&, int) {
                    if (command == cmd_unmask_all)
                        ++sendAttempts;
                    return err_net_tx;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            auto completion = std::make_shared<KmBoxCommandCompletion>();
            const int enqueueStatus = manager.UnmaskAllTracked(completion);
            const int status = enqueueStatus == success
                ? WaitForCompletion(completion, 100ms)
                : enqueueStatus;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);
            if (status != err_net_tx || sendAttempts != 1)
                return Fail("failed tracked unmask was reported as successful");
        }

        {
            KmBoxNetManager manager;
            std::vector<unsigned int> sends;
            int forceReleaseAttempts = 0;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int command, const client_data&, int) {
                    sends.push_back(command);
                    return success;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            OW::Config::Menu = true;
            auto maskCompletion = std::make_shared<KmBoxCommandCompletion>();
            const int maskEnqueue = manager.MaskMouseTracked(
                0x01u,
                maskCompletion);
            const int maskStatus = maskEnqueue == success
                ? WaitForCompletion(maskCompletion, 100ms)
                : maskEnqueue;
            if (maskStatus == success)
                ++forceReleaseAttempts;

            auto unmaskCompletion = std::make_shared<KmBoxCommandCompletion>();
            const int unmaskEnqueue = manager.UnmaskAllTracked(
                unmaskCompletion);
            const int unmaskStatus = unmaskEnqueue == success
                ? WaitForCompletion(unmaskCompletion, 100ms)
                : unmaskEnqueue;
            OW::Config::Menu = false;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);

            if (maskStatus != err_output_suppressed ||
                unmaskStatus != success ||
                sends != std::vector<unsigned int>{ cmd_unmask_all } ||
                forceReleaseAttempts != 0) {
                return Fail("menu-suppressed mask was confirmed or safety unmask was suppressed");
            }
        }

        {
            KmBoxNetManager manager;
            std::mutex gateMutex;
            std::condition_variable gateCv;
            bool releaseSender = false;
            std::atomic<bool> maskAttempted{ false };
            std::vector<unsigned int> sends;
            int forceReleaseAttempts = 0;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int command, const client_data&, int) {
                    sends.push_back(command);
                    if (command == cmd_mask_mouse) {
                        maskAttempted.store(true, std::memory_order_release);
                        std::unique_lock<std::mutex> lock(gateMutex);
                        gateCv.wait(lock, [&releaseSender]() {
                            return releaseSender;
                        });
                    }
                    return success;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            auto maskCompletion = std::make_shared<KmBoxCommandCompletion>();
            const int maskEnqueue = manager.MaskMouseTracked(
                0x01u,
                maskCompletion);
            const auto started = std::chrono::steady_clock::now();
            const int maskStatus = maskEnqueue == success
                ? WaitForCompletion(maskCompletion, 20ms)
                : maskEnqueue;
            const auto elapsed = std::chrono::steady_clock::now() - started;
            if (maskStatus == success)
                ++forceReleaseAttempts;

            const bool maskCancelled = maskEnqueue == success
                ? manager.CancelQueuedMouseMask(maskCompletion)
                : false;

            auto cleanupCompletion =
                std::make_shared<KmBoxCommandCompletion>();
            const int cleanupEnqueue =
                maskEnqueue == success && !maskCancelled
                    ? manager.QueueMouseUnmaskCleanup(cleanupCompletion)
                    : err_queue_stopped;
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                releaseSender = true;
            }
            gateCv.notify_all();
            const int cleanupStatus = cleanupEnqueue == success
                ? WaitForCompletion(cleanupCompletion, 100ms)
                : cleanupEnqueue;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);

            if (maskEnqueue != success)
                return Fail("could not enqueue tracked mask timeout fixture");
            if (maskStatus != err_completion_timeout || elapsed > 250ms ||
                !maskAttempted.load(std::memory_order_acquire) ||
                forceReleaseAttempts != 0) {
                return Fail("timed-out mask was not bounded and fail-closed");
            }
            if (maskCancelled)
                return Fail("in-flight mask timeout fixture was incorrectly cancellable");
            if (cleanupEnqueue != success)
                return Fail("could not queue paired late-mask cleanup");
            if (cleanupStatus != success ||
                sends != std::vector<unsigned int>{
                    cmd_mask_mouse,
                    cmd_unmask_all
                } ||
                forceReleaseAttempts != 0) {
                return Fail("late mask was not followed by confirmed safety unmask");
            }
        }

        {
            KmBoxNetManager manager;
            std::mutex gateMutex;
            std::condition_variable gateCv;
            bool releaseSender = false;
            std::atomic<bool> unmaskAttempted{ false };
            std::vector<unsigned int> sends;
            KmboxQueueSelfTestAccess::PrepareNetwork(
                manager,
                [&](unsigned int command, const client_data&, int) {
                    sends.push_back(command);
                    if (command == cmd_unmask_all) {
                        unmaskAttempted.store(true, std::memory_order_release);
                        std::unique_lock<std::mutex> lock(gateMutex);
                        gateCv.wait(lock, [&releaseSender]() {
                            return releaseSender;
                        });
                    }
                    return success;
                });
            KmboxQueueSelfTestAccess::StartNetworkWorker(manager);

            auto completion = std::make_shared<KmBoxCommandCompletion>();
            const int enqueueStatus = manager.UnmaskAllTracked(completion);
            const auto started = std::chrono::steady_clock::now();
            const int initialStatus = enqueueStatus == success
                ? WaitForCompletion(completion, 20ms)
                : enqueueStatus;
            const auto elapsed = std::chrono::steady_clock::now() - started;
            {
                std::lock_guard<std::mutex> lock(gateMutex);
                releaseSender = true;
            }
            gateCv.notify_all();
            const int lateStatus = enqueueStatus == success
                ? WaitForCompletion(completion, 100ms)
                : enqueueStatus;
            KmboxQueueSelfTestAccess::StopNetworkWorker(manager);

            if (enqueueStatus != success ||
                initialStatus != err_completion_timeout || elapsed > 250ms ||
                !unmaskAttempted.load(std::memory_order_acquire) ||
                lateStatus != success ||
                sends != std::vector<unsigned int>{ cmd_unmask_all }) {
                return Fail("tracked unmask timeout was not bounded or observable");
            }
        }

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
        TestOrderedKeyboardSafety() != 0 ||
        TestOrderedSafetyWorkerGates() != 0 ||
        TestOrderedSafetyQueuePressure() != 0 ||
        TestMouseDesiredStateRollsBackOnQueueFailure() != 0 ||
        TestReleaseAllQueueFailureRollsBackTrackedState() != 0 ||
        TestReleaseAllCommitsSuccessfulDomainsOnly() != 0 ||
        TestSafetyIntentRejectsPresses() != 0 ||
        TestNetworkPriorityAndSequence() != 0 ||
        TestNetworkFullQueueAndFailure() != 0 ||
        TestTrackedMouseMaskCompletionAndForceGate() != 0 ||
        TestNetworkTimeoutIsBounded() != 0 ||
        TestSerialPriorityAndSequence() != 0 ||
        TestMockReleaseAll() != 0) {
        return 1;
    }

    std::puts("[KmboxQueueSelfTest] PASS");
    return 0;
}
