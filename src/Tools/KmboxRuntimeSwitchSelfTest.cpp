#include "Kmbox/KmboxRuntime.hpp"
#include "Utils/Config.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace
{
    constexpr int kInitFailure = -7001;

    const char* BackendName(kmbox::KmboxRuntimeBackend backend)
    {
        using kmbox::KmboxRuntimeBackend;
        switch (backend) {
        case KmboxRuntimeBackend::None:    return "none";
        case KmboxRuntimeBackend::Network: return "network";
        case KmboxRuntimeBackend::Serial:  return "serial";
        case KmboxRuntimeBackend::Mock:    return "mock";
        default:                           return "unknown";
        }
    }

    class FakeBackendOps final : public kmbox::IKmboxRuntimeBackendOps
    {
    public:
        kmbox::KmboxRuntimeController* controller = nullptr;
        std::vector<std::string> events;
        int releaseStatus = success;
        int shutdownStatus = success;
        int initializeStatus = success;
        bool operationObservedOpenGate = false;

        void OnOutputGateChanged(bool open) noexcept override
        {
            events.emplace_back(open ? "gate:open" : "gate:closed");
        }

        int ReleaseAll(
            kmbox::KmboxRuntimeBackend backend,
            std::chrono::milliseconds) override
        {
            ObserveGate();
            events.emplace_back(std::string("release:") + BackendName(backend));
            return releaseStatus;
        }

        int Shutdown(
            kmbox::KmboxRuntimeBackend backend,
            std::chrono::milliseconds) override
        {
            ObserveGate();
            events.emplace_back(std::string("shutdown:") + BackendName(backend));
            return shutdownStatus;
        }

        int Initialize(const kmbox::KmboxRuntimeDescriptor& descriptor) override
        {
            ObserveGate();
            events.emplace_back(std::string("init:") + BackendName(descriptor.backend));
            return initializeStatus;
        }

        void ClearEvents()
        {
            events.clear();
            operationObservedOpenGate = false;
        }

    private:
        void ObserveGate()
        {
            if (controller && controller->IsOutputGateOpen())
                operationObservedOpenGate = true;
        }
    };

    int Fail(const char* message)
    {
        std::fprintf(stderr, "[KmboxRuntimeSwitchSelfTest] FAIL: %s\n", message);
        return 1;
    }

    bool EventsEqual(
        const FakeBackendOps& ops,
        std::initializer_list<const char*> expected)
    {
        if (ops.events.size() != expected.size())
            return false;
        std::size_t index = 0;
        for (const char* event : expected) {
            if (ops.events[index++] != event)
                return false;
        }
        return true;
    }

    int TestNoneMockNoneAndDisableOrder()
    {
        using namespace kmbox;
        if (!SupportsKeyboardOutput(KmboxRuntimeBackend::Network) ||
            !SupportsKeyboardOutput(KmboxRuntimeBackend::Mock) ||
            SupportsKeyboardOutput(KmboxRuntimeBackend::Serial) ||
            SupportsKeyboardOutput(KmboxRuntimeBackend::None)) {
            return Fail("keyboard transport capability mapping is wrong");
        }
        if (IsRuntimeReady(success, {}, false) ||
            IsRuntimeReady(kInitFailure, { KmboxRuntimeDescriptor::Mock(), 1 }, true) ||
            !IsRuntimeReady(success, { KmboxRuntimeDescriptor::Mock(), 1 }, true)) {
            return Fail("runtime readiness accepted an inactive or failed backend");
        }

        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;

        if (controller.Reconcile(KmboxRuntimeDescriptor::Disabled(), 20ms) != success ||
            !ops.events.empty()) {
            return Fail("initial None -> None was not a strict no-op");
        }

        if (controller.Reconcile(KmboxRuntimeDescriptor::Mock(), 20ms) != success ||
            !EventsEqual(ops, { "gate:closed", "init:mock", "gate:open" }) ||
            ops.operationObservedOpenGate) {
            return Fail("None -> Mock ordering or gate state is wrong");
        }
        const auto mockApplied = controller.Applied();
        if (mockApplied.descriptor != KmboxRuntimeDescriptor::Mock() ||
            mockApplied.generation != 1 ||
            !controller.IsOutputGateOpen() ||
            !controller.CanDispatch(mockApplied.generation)) {
            return Fail("Mock was not committed with generation 1 and an open gate");
        }

        ops.ClearEvents();
        if (controller.Reconcile(KmboxRuntimeDescriptor::Disabled(), 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:mock", "shutdown:mock" }) ||
            ops.operationObservedOpenGate) {
            return Fail("disable did not pause, release, then shutdown");
        }
        const auto disabled = controller.Applied();
        if (disabled.descriptor != KmboxRuntimeDescriptor::Disabled() ||
            disabled.generation != 2 || controller.IsOutputGateOpen() ||
            controller.CanDispatch(mockApplied.generation) ||
            controller.CanDispatch(disabled.generation)) {
            return Fail("disable did not commit gated-off None generation");
        }
        return 0;
    }

    int TestNetworkSerialSwitches()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto network = KmboxRuntimeDescriptor::Network(
            "192.168.2.188", 8808, "12525C53", 8809);
        const auto serial = KmboxRuntimeDescriptor::Serial("COM3");

        if (controller.Reconcile(network, 20ms) != success)
            return Fail("could not initialize fake network backend");
        ops.ClearEvents();
        if (controller.Reconcile(serial, 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:network", "shutdown:network",
                "init:serial", "gate:open" }) ||
            ops.operationObservedOpenGate) {
            return Fail("Network -> Serial transaction ordering is wrong");
        }
        const auto serialApplied = controller.Applied();
        if (serialApplied.descriptor != serial || serialApplied.generation != 2)
            return Fail("Serial descriptor was not committed");

        ops.ClearEvents();
        if (controller.Reconcile(network, 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:serial", "shutdown:serial",
                "init:network", "gate:open" })) {
            return Fail("Serial -> Network transaction ordering is wrong");
        }
        return 0;
    }

    int TestInitFailureAndRetry()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto serial = KmboxRuntimeDescriptor::Serial("COM7");

        if (controller.Reconcile(KmboxRuntimeDescriptor::Mock(), 20ms) != success)
            return Fail("could not prepare old backend for failure test");
        const std::uint64_t oldGeneration = controller.Applied().generation;

        ops.ClearEvents();
        ops.initializeStatus = kInitFailure;
        if (controller.Reconcile(serial, 20ms) != kInitFailure ||
            !EventsEqual(ops, {
                "gate:closed", "release:mock", "shutdown:mock",
                "init:serial", "shutdown:serial" }) ||
            controller.Applied().descriptor != KmboxRuntimeDescriptor::Disabled() ||
            controller.Desired() != serial || controller.IsOutputGateOpen() ||
            controller.LastError() != kInitFailure ||
            controller.CanDispatch(oldGeneration)) {
            return Fail("init failure did not roll back to gated-off None");
        }

        ops.ClearEvents();
        ops.initializeStatus = success;
        if (controller.Reconcile(serial, 20ms) != success ||
            !EventsEqual(ops, { "gate:closed", "init:serial", "gate:open" }) ||
            controller.Applied().generation == oldGeneration) {
            return Fail("same desired descriptor did not retry after failed apply");
        }
        return 0;
    }

    int TestEndpointChangeAndStrictNoOp()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto first = KmboxRuntimeDescriptor::Network(
            "192.168.2.188", 8808, "12525C53", 8809);
        const auto monitorChanged = KmboxRuntimeDescriptor::Network(
            "192.168.2.188", 8808, "12525C53", 8810);

        if (controller.Reconcile(first, 20ms) != success)
            return Fail("could not prepare endpoint change test");
        const std::uint64_t oldGeneration = controller.Applied().generation;
        ops.ClearEvents();
        if (controller.Reconcile(monitorChanged, 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:network", "shutdown:network",
                "init:network", "gate:open" })) {
            return Fail("monitor endpoint change did not restart network backend");
        }

        const auto changed = controller.Applied();
        if (changed.descriptor != monitorChanged ||
            changed.generation == oldGeneration ||
            controller.CanDispatch(oldGeneration) ||
            !controller.CanDispatch(changed.generation)) {
            return Fail("old queued generation remained dispatchable after endpoint switch");
        }

        ops.ClearEvents();
        if (controller.Reconcile(monitorChanged, 20ms) != success ||
            !ops.events.empty() || controller.Applied().generation != changed.generation) {
            return Fail("repeated applied descriptor was not a strict no-op");
        }
        return 0;
    }

    int TestFailedReleaseCanRepairSameDescriptor()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto mock = KmboxRuntimeDescriptor::Mock();

        if (controller.Reconcile(mock, 20ms) != success)
            return Fail("could not prepare failed-release repair test");
        const std::uint64_t oldGeneration = controller.Applied().generation;

        ops.ClearEvents();
        ops.releaseStatus = kInitFailure;
        if (controller.ReleaseActive(20ms) != kInitFailure ||
            !EventsEqual(ops, { "gate:closed", "release:mock" }) ||
            controller.IsOutputGateOpen()) {
            return Fail("failed safety release did not close the output gate");
        }

        ops.ClearEvents();
        ops.releaseStatus = success;
        if (controller.Reconcile(mock, 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:mock", "shutdown:mock",
                "init:mock", "gate:open" }) ||
            controller.Applied().generation == oldGeneration ||
            !controller.IsOutputGateOpen()) {
            return Fail("same-descriptor reconcile did not repair a closed gate");
        }
        return 0;
    }

    int TestSuccessfulReleaseStartsNewOutputGeneration()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto mock = KmboxRuntimeDescriptor::Mock();

        if (controller.Reconcile(mock, 20ms) != success)
            return Fail("could not prepare successful release generation test");
        const auto before = controller.Applied();
        ops.ClearEvents();

        if (controller.ReleaseActive(20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:mock", "gate:open" }) ||
            ops.operationObservedOpenGate) {
            return Fail("successful ReleaseActive ordering is wrong");
        }

        const auto after = controller.Applied();
        if (after.descriptor != before.descriptor ||
            after.generation == before.generation ||
            controller.CanDispatch(before.generation) ||
            !controller.CanDispatch(after.generation) ||
            !controller.IsOutputGateOpen()) {
            return Fail("successful ReleaseActive did not invalidate the old generation");
        }

        ops.ClearEvents();
        if (controller.Reconcile(mock, 20ms) != success ||
            !ops.events.empty() ||
            controller.Applied().generation != after.generation) {
            return Fail("post-release same descriptor was not a strict no-op");
        }
        return 0;
    }

    int TestExplicitTransitionPauseRejectsDispatch()
    {
        using namespace kmbox;
        FakeBackendOps ops;
        KmboxRuntimeController controller(ops);
        ops.controller = &controller;
        const auto mock = KmboxRuntimeDescriptor::Mock();

        if (controller.Reconcile(mock, 20ms) != success)
            return Fail("could not prepare explicit transition pause test");
        const auto before = controller.Applied();
        ops.ClearEvents();

        controller.PauseOutputForTransition();
        int dispatchAttempts = 0;
        const int dispatchStatus = controller.DispatchActive(
            [&dispatchAttempts](const KmboxRuntimeAppliedState&) {
                ++dispatchAttempts;
                return success;
            });
        if (!EventsEqual(ops, { "gate:closed" }) ||
            controller.IsOutputGateOpen() ||
            controller.CanDispatch(before.generation) ||
            dispatchStatus != err_queue_stopped ||
            dispatchAttempts != 0) {
            return Fail("explicit transition pause accepted normal output");
        }

        ops.ClearEvents();
        if (controller.Reconcile(mock, 20ms) != success ||
            !EventsEqual(ops, {
                "gate:closed", "release:mock", "shutdown:mock",
                "init:mock", "gate:open" }) ||
            controller.Applied().generation == before.generation) {
            return Fail("paused same-descriptor runtime did not complete reconciliation");
        }
        return 0;
    }

    int TestConfigDescriptorSnapshot()
    {
        using namespace kmbox;
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 0;
        std::snprintf(OW::Config::kmboxIp, sizeof(OW::Config::kmboxIp), "%s", "10.0.0.25");
        std::snprintf(OW::Config::kmboxMac, sizeof(OW::Config::kmboxMac), "%s", "ABCDEF12");
        OW::Config::kmboxPort = 5512;
        OW::Config::kmboxMonitorPort = 5513;
        OW::Config::kmboxMonitorPortManualOverride = false;

        const auto expected = KmboxRuntimeDescriptor::Network(
            "10.0.0.25", 5512, "ABCDEF12", 5513);
        if (RuntimeDescriptorFromConfig() != expected)
            return Fail("Config endpoint snapshot did not produce the expected descriptor");

        OW::Config::kmboxEnabled = false;
        if (RuntimeDescriptorFromConfig() != KmboxRuntimeDescriptor::Disabled())
            return Fail("disabled Config did not produce a None descriptor");
        return 0;
    }
}

int main()
{
    OW::Config::Menu = false;
    OW::Config::kmboxSuppressOutputWhileMenuOpen = true;

    if (TestNoneMockNoneAndDisableOrder() != 0 ||
        TestNetworkSerialSwitches() != 0 ||
        TestInitFailureAndRetry() != 0 ||
        TestEndpointChangeAndStrictNoOp() != 0 ||
        TestFailedReleaseCanRepairSameDescriptor() != 0 ||
        TestSuccessfulReleaseStartsNewOutputGeneration() != 0 ||
        TestExplicitTransitionPauseRejectsDispatch() != 0 ||
        TestConfigDescriptorSnapshot() != 0) {
        return 1;
    }

    std::puts("[KmboxRuntimeSwitchSelfTest] PASS");
    return 0;
}
