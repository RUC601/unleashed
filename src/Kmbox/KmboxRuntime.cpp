#include "Kmbox/KmboxRuntime.hpp"

#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Kmbox/KmboxTimerResolution.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <utility>

namespace kmbox
{
    namespace
    {
        void RecordFirstFailure(int& aggregate, int status)
        {
            if (aggregate == success && status != success)
                aggregate = status;
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
        if (status == success)
            SetOutputGate(true);
        return status;
    }

    int KmboxRuntimeController::ShutdownActive(std::chrono::milliseconds timeout)
    {
        return Reconcile(KmboxRuntimeDescriptor::Disabled(), timeout);
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
        return RuntimeController().Reconcile(desired, timeout);
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
        return RuntimeController().ShutdownActive(timeout);
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
                        kmBoxBMgr.km_left(down);
                    else if (button == 1)
                        kmBoxBMgr.km_right(down);
                    else
                        kmBoxBMgr.km_middle(down);
                    return success;
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordButton(button, down);
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
                    kmBoxBMgr.km_left(false);
                    kmBoxBMgr.km_right(false);
                    kmBoxBMgr.km_middle(false);
                    return success;
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
                        kmBoxBMgr.km_left(false);
                    else if (button == 1)
                        kmBoxBMgr.km_right(false);
                    else
                        kmBoxBMgr.km_middle(false);
                    return success;
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

    int DispatchKeyboardKey(unsigned char hidCode, bool down)
    {
        return RuntimeController().DispatchActive(
            [=](const KmboxRuntimeAppliedState& active) -> int {
                if (!SupportsKeyboardOutput(active.descriptor.backend))
                    return err_net_cmd;
                switch (active.descriptor.backend) {
                case KmboxRuntimeBackend::Network:
                    return KmBoxMgr.SendKeyboardKey(hidCode, down);
                case KmboxRuntimeBackend::Mock:
                    return MockHardwareMgr.RecordKeyboardKey(hidCode, down);
                default:
                    return err_queue_stopped;
                }
            });
    }
}
