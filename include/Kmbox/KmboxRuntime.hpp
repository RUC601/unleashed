#pragma once

#include "Kmbox/KmBoxConfig.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

namespace kmbox
{
    enum class KmboxRuntimeBackend : int {
        None = 0,
        Network,
        Serial,
        Mock
    };

    inline constexpr bool SupportsKeyboardOutput(KmboxRuntimeBackend backend) noexcept
    {
        return backend == KmboxRuntimeBackend::Network ||
            backend == KmboxRuntimeBackend::Mock;
    }

    struct KmboxNetworkEndpoint {
        std::string ip;
        std::uint16_t commandPort = 0;
        std::string mac;
        std::uint16_t monitorPort = 0;

        bool operator==(const KmboxNetworkEndpoint&) const = default;
    };

    struct KmboxSerialEndpoint {
        std::string port;

        bool operator==(const KmboxSerialEndpoint&) const = default;
    };

    using KmboxRuntimeEndpoint = std::variant<
        std::monostate,
        KmboxNetworkEndpoint,
        KmboxSerialEndpoint>;

    struct KmboxRuntimeDescriptor {
        KmboxRuntimeBackend backend = KmboxRuntimeBackend::None;
        KmboxRuntimeEndpoint endpoint{};

        static KmboxRuntimeDescriptor Disabled();
        static KmboxRuntimeDescriptor Network(
            std::string ip,
            std::uint16_t commandPort,
            std::string mac,
            std::uint16_t monitorPort);
        static KmboxRuntimeDescriptor Serial(std::string port);
        static KmboxRuntimeDescriptor Mock();

        bool operator==(const KmboxRuntimeDescriptor&) const = default;
    };

    struct KmboxRuntimeAppliedState {
        KmboxRuntimeDescriptor descriptor{};
        std::uint64_t generation = 0;
    };

    inline bool IsRuntimeReady(
        int status,
        const KmboxRuntimeAppliedState& applied,
        bool outputGateOpen) noexcept
    {
        return status == success && outputGateOpen &&
            applied.descriptor.backend != KmboxRuntimeBackend::None;
    }

    class IKmboxRuntimeBackendOps
    {
    public:
        virtual ~IKmboxRuntimeBackendOps() = default;

        virtual void OnOutputGateChanged(bool open) noexcept;
        virtual int ReleaseAll(
            KmboxRuntimeBackend backend,
            std::chrono::milliseconds timeout) = 0;
        virtual int Shutdown(
            KmboxRuntimeBackend backend,
            std::chrono::milliseconds timeout) = 0;
        virtual int Initialize(const KmboxRuntimeDescriptor& descriptor) = 0;
    };

    class KmboxProductionBackendOps final : public IKmboxRuntimeBackendOps
    {
    public:
        int ReleaseAll(
            KmboxRuntimeBackend backend,
            std::chrono::milliseconds timeout) override;
        int Shutdown(
            KmboxRuntimeBackend backend,
            std::chrono::milliseconds timeout) override;
        int Initialize(const KmboxRuntimeDescriptor& descriptor) override;
    };

    class KmboxRuntimeController
    {
    public:
        explicit KmboxRuntimeController(IKmboxRuntimeBackendOps& backendOps);

        int Reconcile(
            const KmboxRuntimeDescriptor& desired,
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        int ReleaseActive(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        int ShutdownActive(
            std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
        int DispatchActive(
            const std::function<int(const KmboxRuntimeAppliedState&)>& dispatch);

        KmboxRuntimeDescriptor Desired() const;
        KmboxRuntimeAppliedState Applied() const;
        bool IsOutputGateOpen() const noexcept;
        bool CanDispatch(std::uint64_t generation) const noexcept;
        int LastError() const;

    private:
        void SetOutputGate(bool open) noexcept;

        IKmboxRuntimeBackendOps& backendOps_;
        mutable std::mutex reconcileMutex_;
        KmboxRuntimeDescriptor desired_{};
        KmboxRuntimeDescriptor active_{};
        std::atomic<std::uint64_t> generation_{ 0 };
        std::atomic<bool> outputGateOpen_{ false };
        int lastError_ = success;
    };

    KmboxRuntimeController& RuntimeController();
    using RuntimePreReconcileHook = std::function<void()>;
    void SetRuntimePreReconcileHook(RuntimePreReconcileHook hook);
    KmboxRuntimeDescriptor RuntimeDescriptorFromConfig();
    int ReconcileRuntimeFromConfig(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    KmboxRuntimeAppliedState ActiveRuntimeSnapshot();
    KmBoxConnectionState ActiveRuntimeConnectionState();
    bool IsRuntimeOutputGateOpen() noexcept;
    bool CanDispatchRuntimeGeneration(std::uint64_t generation) noexcept;
    int ReleaseActiveRuntime(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    int ShutdownActiveRuntime(
        std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

    int DispatchMouseMove(int x, int y, int runtimeMs = 0);
    int DispatchMouseButton(int button, bool down);
    int DispatchMouseButtonForGeneration(
        std::uint64_t expectedGeneration,
        int button,
        bool down);
    int DispatchMouseButtonStateMask(std::uint32_t mask, bool force = false);
    int DispatchForceReleaseMouseButtons();
    int DispatchForceReleaseMouseButton(int button);
    int DispatchMouseMask(std::uint32_t mask);
    int DispatchMouseUnmask();
    int DispatchKeyboardReport(
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages);
    int DispatchKeyboardReportForGeneration(
        std::uint64_t expectedGeneration,
        unsigned char modifierMask,
        const std::vector<unsigned char>& usages,
        KmBoxOutputIntent intent = KmBoxOutputIntent::Normal);
    int DispatchKeyboardKey(unsigned char hidCode, bool down);
}
