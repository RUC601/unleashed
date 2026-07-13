#include "Kmbox/KmboxRuntime.hpp"

#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Kmbox/KmboxTimerResolution.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <utility>

namespace kmbox
{
    namespace
    {
        std::mutex g_runtimeFacadeMutex;
        std::mutex g_preReconcileHookMutex;
        RuntimePreReconcileHook g_preReconcileHook;
        constexpr auto kMaxTransportCompletionWait =
            std::chrono::milliseconds(500);

        std::chrono::milliseconds BoundedTransportCompletionWait(
            std::chrono::milliseconds timeout)
        {
            return (std::min)(timeout, kMaxTransportCompletionWait);
        }

        int WaitForTransportCompletion(
            const std::shared_ptr<KmBoxCommandCompletion>& completion,
            std::chrono::milliseconds timeout,
            const char* operation)
        {
            std::unique_lock<std::mutex> completionLock(completion->mutex);
            if (!completion->cv.wait_for(
                    completionLock,
                    timeout,
                    [&completion]() { return completion->completed; })) {
                Diagnostics::Warn(
                    "[KMBOX-NET] Timed out waiting for %s transport completion. timeout_ms=%lld",
                    operation ? operation : "command",
                    static_cast<long long>(timeout.count()));
                return err_completion_timeout;
            }
            return completion->status;
        }

        void RecordFirstFailure(int& aggregate, int status)
        {
            if (aggregate == success && status != success)
                aggregate = status;
        }

        bool IsReconcileNoOp(
            KmboxRuntimeController& controller,
            const KmboxRuntimeDescriptor& desired)
        {
            const KmboxRuntimeAppliedState applied = controller.Applied();
            return desired == applied.descriptor &&
                (applied.descriptor.backend == KmboxRuntimeBackend::None ||
                 controller.IsOutputGateOpen());
        }

        int ReconcileProductionRuntime(
            const KmboxRuntimeDescriptor& desired,
            std::chrono::milliseconds timeout)
        {
            std::lock_guard<std::mutex> transitionLock(g_runtimeFacadeMutex);
            KmboxRuntimeController& controller = RuntimeController();
            if (!IsReconcileNoOp(controller, desired)) {
                // Linearize the transaction's "pause new output" step before
                // producer cancellation. Direct move/mask paths do not pass
                // through the scheduler hook and must see the closed gate too.
                controller.PauseOutputForTransition();
                RuntimePreReconcileHook hook;
                {
                    std::lock_guard<std::mutex> hookLock(
                        g_preReconcileHookMutex);
                    hook = g_preReconcileHook;
                }
                if (hook)
                    hook();
            }
            return controller.Reconcile(desired, timeout);
        }
    }

    KmboxRuntimeDescriptor KmboxRuntimeDescriptor::Disabled()
    {
        return {};
    }

    KmboxRuntimeDescriptor KmboxRuntimeDescriptor::Network(
        std::string ip,
        std::uint16_t commandPort,
        std::string mac,
        std::uint16_t monitorPort)
    {
        KmboxRuntimeDescriptor descriptor{};
        descriptor.backend = KmboxRuntimeBackend::Network;
        descriptor.endpoint = KmboxNetworkEndpoint{
            std::move(ip), commandPort, std::move(mac), monitorPort
        };
        return descriptor;
    }

    KmboxRuntimeDescriptor KmboxRuntimeDescriptor::Serial(std::string port)
    {
        KmboxRuntimeDescriptor descriptor{};
        descriptor.backend = KmboxRuntimeBackend::Serial;
        descriptor.endpoint = KmboxSerialEndpoint{ std::move(port) };
        return descriptor;
    }

    KmboxRuntimeDescriptor KmboxRuntimeDescriptor::Mock()
    {
        KmboxRuntimeDescriptor descriptor{};
        descriptor.backend = KmboxRuntimeBackend::Mock;
        return descriptor;
    }

    void IKmboxRuntimeBackendOps::OnOutputGateChanged(bool) noexcept
    {
    }

    int KmboxProductionBackendOps::ReleaseAll(
        KmboxRuntimeBackend backend,
        std::chrono::milliseconds timeout)
    {
        switch (backend) {
        case KmboxRuntimeBackend::None:
            return success;
        case KmboxRuntimeBackend::Network:
            return KmBoxMgr.ReleaseAllOutputAndWait(timeout);
        case KmboxRuntimeBackend::Serial:
            return kmBoxBMgr.ReleaseAllOutputAndWait(timeout);
        case KmboxRuntimeBackend::Mock:
            return MockHardwareMgr.ReleaseAllOutputAndWait(timeout);
        default:
            return err_net_cmd;
        }
    }

    int KmboxProductionBackendOps::Shutdown(
        KmboxRuntimeBackend backend,
        std::chrono::milliseconds timeout)
    {
        int status = success;
        switch (backend) {
        case KmboxRuntimeBackend::None:
            break;
        case KmboxRuntimeBackend::Network:
            status = KmBoxMgr.Shutdown(timeout);
            break;
        case KmboxRuntimeBackend::Serial:
            status = kmBoxBMgr.Shutdown(timeout);
            break;
        case KmboxRuntimeBackend::Mock:
            status = MockHardwareMgr.Shutdown(timeout);
            break;
        default:
            status = err_net_cmd;
            break;
        }
        ReleaseTimerResolution();
        return status;
    }

    int KmboxProductionBackendOps::Initialize(const KmboxRuntimeDescriptor& descriptor)
    {
        switch (descriptor.backend) {
        case KmboxRuntimeBackend::None:
            return success;
        case KmboxRuntimeBackend::Network: {
            const auto* endpoint = std::get_if<KmboxNetworkEndpoint>(&descriptor.endpoint);
            if (!endpoint || endpoint->ip.empty() || endpoint->commandPort == 0 ||
                endpoint->mac.empty() || endpoint->monitorPort == 0) {
                return err_net_cmd;
            }
            (void)EnsureTimerResolution();

            int status = KmBoxMgr.InitDevice(
                endpoint->ip,
                static_cast<WORD>(endpoint->commandPort),
                endpoint->mac);
            if (status != success)
                return status;

            const int recoveryStatus = KmBoxMgr.RecoverMousePassthrough();
            if (recoveryStatus != success) {
                Diagnostics::Warn(
                    "[KMBOX-RUNTIME] Network passthrough recovery warning. status=%d",
                    recoveryStatus);
            }

            const int monitorStatus = KmBoxMgr.KeyBoard.StartMonitor(
                static_cast<WORD>(endpoint->monitorPort));
            if (monitorStatus != success) {
                Diagnostics::Warn(
                    "[KMBOX-RUNTIME] Network monitor start warning. port=%u status=%d",
                    static_cast<unsigned int>(endpoint->monitorPort),
                    monitorStatus);
            }
            return success;
        }
        case KmboxRuntimeBackend::Serial: {
            const auto* endpoint = std::get_if<KmboxSerialEndpoint>(&descriptor.endpoint);
            if (!endpoint)
                return err_net_cmd;
            (void)EnsureTimerResolution();
            return kmBoxBMgr.init(endpoint->port);
        }
        case KmboxRuntimeBackend::Mock:
            if (!std::holds_alternative<std::monostate>(descriptor.endpoint))
                return err_net_cmd;
            (void)EnsureTimerResolution();
            return MockHardwareMgr.Initialize();
        default:
            return err_net_cmd;
        }
    }

    KmboxRuntimeController::KmboxRuntimeController(IKmboxRuntimeBackendOps& backendOps)
        : backendOps_(backendOps)
    {
    }

    int KmboxRuntimeController::Reconcile(
        const KmboxRuntimeDescriptor& desired,
        std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        desired_ = desired;

        if (desired == active_ &&
            (active_.backend == KmboxRuntimeBackend::None ||
             outputGateOpen_.load(std::memory_order_acquire))) {
            lastError_ = success;
            return success;
        }

        SetOutputGate(false);
        const KmboxRuntimeDescriptor previous = active_;
        int transitionStatus = success;

        if (previous.backend != KmboxRuntimeBackend::None) {
            RecordFirstFailure(
                transitionStatus,
                backendOps_.ReleaseAll(previous.backend, timeout));
            RecordFirstFailure(
                transitionStatus,
                backendOps_.Shutdown(previous.backend, timeout));
            active_ = KmboxRuntimeDescriptor::Disabled();
        }

        if (transitionStatus != success) {
            lastError_ = transitionStatus;
            return transitionStatus;
        }

        if (desired.backend == KmboxRuntimeBackend::None) {
            active_ = KmboxRuntimeDescriptor::Disabled();
            generation_.fetch_add(1, std::memory_order_acq_rel);
            lastError_ = success;
            return success;
        }

        const int initializeStatus = backendOps_.Initialize(desired);
        if (initializeStatus != success) {
            // Initialization may have partially opened a transport. Roll it back
            // before publishing the safe, gated-off None state.
            (void)backendOps_.Shutdown(desired.backend, timeout);
            active_ = KmboxRuntimeDescriptor::Disabled();
            lastError_ = initializeStatus;
            return initializeStatus;
        }

        active_ = desired;
        generation_.fetch_add(1, std::memory_order_acq_rel);
        lastError_ = success;
        SetOutputGate(true);
        return success;
    }

    int KmboxRuntimeController::ReleaseActive(std::chrono::milliseconds timeout)
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        if (active_.backend == KmboxRuntimeBackend::None)
            return success;

        SetOutputGate(false);
        const int status = backendOps_.ReleaseAll(active_.backend, timeout);
        lastError_ = status;
        if (status == success) {
            // A successful all-up is an output-session boundary even though
            // the transport remains initialized. Invalidate every producer
            // token captured before the cleanup before reopening dispatch.
            generation_.fetch_add(1, std::memory_order_acq_rel);
            SetOutputGate(true);
        }
        return status;
    }

    int KmboxRuntimeController::ShutdownActive(std::chrono::milliseconds timeout)
    {
        return Reconcile(KmboxRuntimeDescriptor::Disabled(), timeout);
    }

    void KmboxRuntimeController::PauseOutputForTransition()
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        SetOutputGate(false);
    }

    int KmboxRuntimeController::DispatchActive(
        const std::function<int(const KmboxRuntimeAppliedState&)>& dispatch)
    {
        if (!dispatch)
            return err_net_cmd;

        std::lock_guard<std::mutex> lock(reconcileMutex_);
        const std::uint64_t generation = generation_.load(std::memory_order_acquire);
        if (active_.backend == KmboxRuntimeBackend::None ||
            !outputGateOpen_.load(std::memory_order_acquire) ||
            !CanDispatch(generation)) {
            return err_queue_stopped;
        }

        return dispatch({ active_, generation });
    }

    KmboxRuntimeDescriptor KmboxRuntimeController::Desired() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return desired_;
    }

    KmboxRuntimeAppliedState KmboxRuntimeController::Applied() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return { active_, generation_.load(std::memory_order_acquire) };
    }

    bool KmboxRuntimeController::IsOutputGateOpen() const noexcept
    {
        return outputGateOpen_.load(std::memory_order_acquire);
    }

    bool KmboxRuntimeController::CanDispatch(std::uint64_t generation) const noexcept
    {
        return outputGateOpen_.load(std::memory_order_acquire) &&
            generation_.load(std::memory_order_acquire) == generation;
    }

    int KmboxRuntimeController::LastError() const
    {
        std::lock_guard<std::mutex> lock(reconcileMutex_);
        return lastError_;
    }

    void KmboxRuntimeController::SetOutputGate(bool open) noexcept
    {
        outputGateOpen_.store(open, std::memory_order_release);
        backendOps_.OnOutputGateChanged(open);
    }

    KmboxRuntimeController& RuntimeController()
    {
        static KmboxProductionBackendOps backendOps;
        static KmboxRuntimeController controller(backendOps);
        return controller;
    }

    void SetRuntimePreReconcileHook(RuntimePreReconcileHook hook)
    {
        std::lock_guard<std::mutex> lock(g_preReconcileHookMutex);
        g_preReconcileHook = std::move(hook);
    }

    KmboxRuntimeDescriptor RuntimeDescriptorFromConfig()
    {
        std::lock_guard<std::mutex> lock(OW::Config::mutex);
        if (!OW::Config::kmboxEnabled)
            return KmboxRuntimeDescriptor::Disabled();

        switch (OW::Config::kmboxDeviceType) {
        case 0: {
            const int commandPort = OW::Config::kmboxPort;
            const int monitorPort = OW::Config::EffectiveKmboxMonitorPort();
            return KmboxRuntimeDescriptor::Network(
                OW::Config::kmboxIp,
                OW::Config::IsValidKmboxUdpPort(commandPort)
                    ? static_cast<std::uint16_t>(commandPort)
                    : 0,
                OW::Config::kmboxMac,
                OW::Config::IsValidKmboxUdpPort(monitorPort)
                    ? static_cast<std::uint16_t>(monitorPort)
                    : 0);
        }
        case 1:
            return KmboxRuntimeDescriptor::Serial(OW::Config::kmboxComPort);
        case 2:
            return KmboxRuntimeDescriptor::Mock();
        default:
            return KmboxRuntimeDescriptor::Disabled();
        }
    }

    int ReconcileRuntimeFromConfig(std::chrono::milliseconds timeout)
    {
        const KmboxRuntimeDescriptor desired = RuntimeDescriptorFromConfig();
        return ReconcileProductionRuntime(desired, timeout);
    }

    KmboxRuntimeAppliedState ActiveRuntimeSnapshot()
    {
        return RuntimeController().Applied();
    }

    KmBoxConnectionState ActiveRuntimeConnectionState()
    {
        const KmboxRuntimeAppliedState active = RuntimeController().Applied();
        if (!RuntimeController().CanDispatch(active.generation))
            return KmBoxConnectionState::Disconnected;

        switch (active.descriptor.backend) {
        case KmboxRuntimeBackend::Network:
            return KmBoxMgr.GetConnectionState();
        case KmboxRuntimeBackend::Serial:
            return kmBoxBMgr.GetConnectionState();
        case KmboxRuntimeBackend::Mock:
            return MockHardwareMgr.IsInitialized()
                ? KmBoxConnectionState::Connected
                : KmBoxConnectionState::Disconnected;
        default:
            return KmBoxConnectionState::Disconnected;
        }
    }

    bool IsRuntimeOutputGateOpen() noexcept
    {
        return RuntimeController().IsOutputGateOpen();
    }

    bool CanDispatchRuntimeGeneration(std::uint64_t generation) noexcept
    {
        return RuntimeController().CanDispatch(generation);
    }

    int ReleaseActiveRuntime(std::chrono::milliseconds timeout)
    {
        return RuntimeController().ReleaseActive(timeout);
    }

    int ShutdownActiveRuntime(std::chrono::milliseconds timeout)
    {
        return ReconcileProductionRuntime(
            KmboxRuntimeDescriptor::Disabled(), timeout);
    }

    int DispatchMouseMove(int x, int y, int runtimeMs)
    {
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return runtimeMs > 0
                        ? KmBoxMgr.Mouse.Move_Auto(x, y, runtimeMs)
                        : KmBoxMgr.Mouse.Move(x, y);
                case KmboxRuntimeBackend::Serial:
                    if (runtimeMs > 0)
                        kmBoxBMgr.km_move_auto(x, y, runtimeMs);
                    else
                        kmBoxBMgr.km_move(x, y);
                    return success;
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordMove(x, y, runtimeMs);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMoveForGeneration(
        std::uint64_t expectedGeneration,
        int x,
        int y,
        int runtimeMs)
    {
        if (expectedGeneration == 0)
            return err_queue_stopped;

        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return runtimeMs > 0
                        ? KmBoxMgr.Mouse.Move_Auto(x, y, runtimeMs)
                        : KmBoxMgr.Mouse.Move(x, y);
                case KmboxRuntimeBackend::Serial:
                    if (runtimeMs > 0)
                        kmBoxBMgr.km_move_auto(x, y, runtimeMs);
                    else
                        kmBoxBMgr.km_move(x, y);
                    return success;
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordMove(x, y, runtimeMs);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButton(int button, bool down)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    if (button == 0)
                        return KmBoxMgr.Mouse.Left(down);
                    if (button == 1)
                        return KmBoxMgr.Mouse.Right(down);
                    return KmBoxMgr.Mouse.Middle(down);
                case KmboxRuntimeBackend::Serial:
                    if (button == 0)
                        return kmBoxBMgr.km_left(down);
                    if (button == 1)
                        return kmBoxBMgr.km_right(down);
                    return kmBoxBMgr.km_middle(down);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordButton(button, down);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button,
        bool down)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        const KmBoxOutputIntent intent = OutputIntentForState(down);
        if (ShouldSuppressOutputForMenu(
                OW::Config::KmboxOutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    if (button == 0)
                        return KmBoxMgr.Mouse.Left(down);
                    if (button == 1)
                        return KmBoxMgr.Mouse.Right(down);
                    return KmBoxMgr.Mouse.Middle(down);
                case KmboxRuntimeBackend::Serial:
                    if (button == 0)
                        return kmBoxBMgr.km_left(down);
                    if (button == 1)
                        return kmBoxBMgr.km_right(down);
                    return kmBoxBMgr.km_middle(down);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordButton(button, down);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.ForceReleaseMouseButton(button);
                case KmboxRuntimeBackend::Serial:
                    if (button == 0)
                        return kmBoxBMgr.km_left(false);
                    if (button == 1)
                        return kmBoxBMgr.km_right(false);
                    return kmBoxBMgr.km_middle(false);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButton(button);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseButtonStateMask(std::uint32_t mask, bool force)
    {
        const std::uint32_t effectiveMask = mask & 0x7u;
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.SetMouseButtonStateMask(effectiveMask, force);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.SetMouseButtonStateMask(effectiveMask, force);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButtons()
    {
        return RuntimeController().DispatchActive(
            [](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.ForceReleaseMouseButtons();
                case KmboxRuntimeBackend::Serial:
                {
                    int status = success;
                    RecordFirstFailure(status, kmBoxBMgr.km_left(false));
                    RecordFirstFailure(status, kmBoxBMgr.km_right(false));
                    RecordFirstFailure(status, kmBoxBMgr.km_middle(false));
                    return status;
                }
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButtons();
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchForceReleaseMouseButton(int button)
    {
        if (button < 0 || button > 2)
            return err_net_cmd;

        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.ForceReleaseMouseButton(button);
                case KmboxRuntimeBackend::Serial:
                    if (button == 0)
                        return kmBoxBMgr.km_left(false);
                    if (button == 1)
                        return kmBoxBMgr.km_right(false);
                    return kmBoxBMgr.km_middle(false);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.ForceReleaseMouseButton(button);
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMask(std::uint32_t mask)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.MaskMouse(effectiveMask);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseUnmask()
    {
        return RuntimeController().DispatchActive(
            [](const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.UnmaskAll();
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMaskForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        if (effectiveMask == 0)
            return err_net_cmd;
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.MaskMouse(effectiveMask);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseMaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::uint32_t mask,
        std::chrono::milliseconds timeout)
    {
        const std::uint32_t effectiveMask = mask & 0x7Fu;
        if (effectiveMask == 0 || expectedGeneration == 0 ||
            timeout < std::chrono::milliseconds::zero()) {
            return err_net_cmd;
        }
        timeout = BoundedTransportCompletionWait(timeout);

        auto completion = std::make_shared<KmBoxCommandCompletion>();
        bool waitForNetworkWorker = false;
        const int enqueueStatus = RuntimeController().DispatchActive(
            [=, &waitForNetworkWorker](
                const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    waitForNetworkWorker = true;
                    return KmBoxMgr.MaskMouseTracked(effectiveMask, completion);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.MaskMouse(effectiveMask);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
        if (enqueueStatus != success || !waitForNetworkWorker)
            return enqueueStatus;

        // DispatchActive has returned here, so neither the runtime reconcile
        // mutex nor the network queue mutex is held while transport completes.
        const int completionStatus = WaitForTransportCompletion(
            completion,
            timeout,
            "mouse mask");
        if (completionStatus != err_completion_timeout)
            return completionStatus;

        // If the worker has not popped the mask, remove it by token. Otherwise
        // it may still arrive after this call returns, so place a safety
        // unmask directly behind the in-flight command and wait for it. The
        // cleanup stays queued even if its own bounded wait expires.
        if (KmBoxMgr.CancelQueuedMouseMask(completion))
            return err_completion_timeout;

        auto cleanupCompletion = std::make_shared<KmBoxCommandCompletion>();
        const int cleanupEnqueueStatus = RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                if (active.descriptor.backend != KmboxRuntimeBackend::Network)
                    return err_net_cmd;
                return KmBoxMgr.QueueMouseUnmaskCleanup(cleanupCompletion);
            });
        if (cleanupEnqueueStatus == success) {
            const int cleanupStatus = WaitForTransportCompletion(
                cleanupCompletion,
                timeout,
                "timed-out mouse mask cleanup");
            if (cleanupStatus != success) {
                Diagnostics::Warn(
                    "[KMBOX-NET] Timed-out mouse mask cleanup remains pending or failed. status=%d",
                    cleanupStatus);
            }
        } else if (cleanupEnqueueStatus != err_queue_stopped) {
            Diagnostics::Warn(
                "[KMBOX-NET] Could not queue timed-out mouse mask cleanup. status=%d",
                cleanupEnqueueStatus);
        }
        return err_completion_timeout;
    }

    int DispatchMouseUnmaskForGeneration(
        std::uint64_t expectedGeneration)
    {
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.UnmaskAll();
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchMouseUnmaskAndWaitForGeneration(
        std::uint64_t expectedGeneration,
        std::chrono::milliseconds timeout)
    {
        if (expectedGeneration == 0 ||
            timeout < std::chrono::milliseconds::zero()) {
            return err_net_cmd;
        }
        timeout = BoundedTransportCompletionWait(timeout);

        auto completion = std::make_shared<KmBoxCommandCompletion>();
        bool waitForNetworkWorker = false;
        const int enqueueStatus = RuntimeController().DispatchActive(
            [=, &waitForNetworkWorker](
                const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    waitForNetworkWorker = true;
                    return KmBoxMgr.UnmaskAllTracked(completion);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.UnmaskAll();
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
        if (enqueueStatus != success || !waitForNetworkWorker)
            return enqueueStatus;

        return WaitForTransportCompletion(
            completion,
            timeout,
            "mouse unmask");
    }

    int DispatchKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages)
    {
        soft_keyboard_t validated{};
        if (!KmBoxNetManager::BuildKeyboardReport(
                modifierMask, usages, validated)) {
            return err_net_cmd;
        }

        const KmBoxOutputIntent intent = modifierMask == 0 && usages.empty()
            ? KmBoxOutputIntent::SafetyRelease
            : KmBoxOutputIntent::Normal;
        if (ShouldSuppressOutputForMenu(
                OW::Config::KmboxOutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [modifierMask, usages, intent](
                const KmboxRuntimeAppliedState& active) -> int {
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.SendKeyboardReport(modifierMask, usages);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordKeyboardReport(
                        modifierMask, usages, intent);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchKeyboardReportForGeneration(
        std::uint64_t expectedGeneration,
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        KmBoxOutputIntent intent)
    {
        soft_keyboard_t validated{};
        if (!KmBoxNetManager::BuildKeyboardReport(
                modifierMask, usages, validated)) {
            return err_net_cmd;
        }

        if (ShouldSuppressOutputForMenu(
                OW::Config::KmboxOutputSuppressedByMenu(), intent)) {
            return err_output_suppressed;
        }

        return RuntimeController().DispatchActive(
            [expectedGeneration, modifierMask, usages, intent](
                const KmboxRuntimeAppliedState& active) -> int {
                if (active.generation != expectedGeneration)
                    return err_queue_stopped;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.SendKeyboardReport(
                        modifierMask, usages, intent);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordKeyboardReport(
                        modifierMask, usages, intent);
                case KmboxRuntimeBackend::Serial:
                    return err_net_cmd;
                default:
                    return err_queue_stopped;
                }
            });
    }

    int DispatchKeyboardKey(unsigned char hidCode, bool down)
    {
        const bool isModifier =
            hidCode >= KEY_LEFTCONTROL && hidCode <= KEY_RIGHT_GUI;
        const bool isUsage = hidCode >= KEY_A && hidCode < KEY_LEFTCONTROL;
        if (!isModifier && !isUsage)
            return err_net_cmd;

        if (!down)
            return DispatchKeyboardReport(0, {});
        if (isModifier) {
            const auto modifierBit = static_cast<unsigned char>(
                1u << (hidCode - KEY_LEFTCONTROL));
            return DispatchKeyboardReport(modifierBit, {});
        }
        return DispatchKeyboardReport(0, { hidCode });
    }
}
