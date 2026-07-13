#pragma once

#include "Game/AimArchitecture.hpp"
#include "Game/OutputScheduler.hpp"
#include "Kmbox/KmboxRuntime.hpp"
#include "Utils/Config.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace OW {

struct ExecutionToken {
    ExecutionSource source = ExecutionSource::GlobalAim;
    int priority = 0;
    bool active = false;
};

inline int ExecutionPriority(ExecutionSource source)
{
    switch (source) {
    case ExecutionSource::SequenceInternal:
        return 100;
    case ExecutionSource::HeroSkill:
        return 60;
    case ExecutionSource::Trigger:
        return 40;
    case ExecutionSource::GlobalAim:
    default:
        return 20;
    }
}

inline ExecutionToken MakeExecutionToken(ExecutionSource source, bool active = true)
{
    return ExecutionToken{ source, ExecutionPriority(source), active };
}

inline bool ShouldYieldToExecution(const ExecutionToken& current, const ExecutionToken& owner)
{
    return owner.active && owner.priority > current.priority;
}

namespace InputOrchestratorDetail {
inline std::atomic<bool> runtimeOutputSessionEnabled{ true };
inline std::atomic<std::uint64_t> runtimeOutputTransitionEpoch{ 1 };
inline std::mutex runtimeOutputProducerTransitionMutex;
}

inline void SetRuntimeOutputSessionEnabled(bool enabled) noexcept
{
    InputOrchestratorDetail::runtimeOutputSessionEnabled.store(
        enabled,
        std::memory_order_release);
}

inline bool RuntimeOutputSessionEnabled() noexcept
{
    return InputOrchestratorDetail::runtimeOutputSessionEnabled.load(
        std::memory_order_acquire);
}

inline std::uint64_t RuntimeOutputTransitionEpoch() noexcept
{
    return InputOrchestratorDetail::runtimeOutputTransitionEpoch.load(
        std::memory_order_acquire);
}

inline bool RuntimeOutputTransitionMatches(
    std::uint64_t expectedEpoch) noexcept
{
    return expectedEpoch != 0 &&
        RuntimeOutputTransitionEpoch() == expectedEpoch;
}

inline std::mutex& RuntimeOutputProducerTransitionMutex() noexcept
{
    return InputOrchestratorDetail::runtimeOutputProducerTransitionMutex;
}

inline OutputRuntimeState CurrentKmboxOutputRuntimeState()
{
    const kmbox::KmboxRuntimeAppliedState applied =
        kmbox::ActiveRuntimeSnapshot();
    return {
        applied.generation,
        RuntimeOutputSessionEnabled() &&
            applied.descriptor.backend != kmbox::KmboxRuntimeBackend::None &&
            kmbox::IsRuntimeOutputGateOpen() &&
            !Config::KmboxOutputSuppressedByMenu()
    };
}

inline int DispatchKmboxAggregateOutput(const OutputAggregateUpdate& update)
{
    int status = success;
    bool keyboardChanged = false;
    for (const OutputChange& change : update.changes) {
        if (change.control.kind == OutputControlKind::MouseButton) {
            const int result = kmbox::DispatchMouseButtonForGeneration(
                update.backendGeneration,
                static_cast<int>(change.control.code),
                change.transition == OutputTransition::Press);
            if (status == success && result != success)
                status = result;
        } else {
            keyboardChanged = true;
        }
    }

    if (keyboardChanged) {
        const int result = kmbox::DispatchKeyboardReportForGeneration(
            update.backendGeneration,
            update.keyboardModifierMask,
            update.keyboardUsages,
            update.IsReleaseOnly()
                ? KmBoxOutputIntent::SafetyRelease
                : KmBoxOutputIntent::Normal);
        if (status == success && result != success)
            status = result;
    }
    return status;
}

inline OutputAggregateSink MakeKmboxOutputAggregateSink()
{
    return [](const OutputAggregateUpdate& update) {
        return DispatchKmboxAggregateOutput(update);
    };
}

inline OutputRuntimeStateSource MakeKmboxOutputRuntimeStateSource()
{
    return []() { return CurrentKmboxOutputRuntimeState(); };
}

inline OutputScheduler& RuntimeOutputScheduler()
{
    struct RuntimeSchedulerHolder
    {
        RuntimeSchedulerHolder()
            : scheduler(
                MakeKmboxOutputAggregateSink(),
                MakeKmboxOutputRuntimeStateSource())
        {
            kmbox::SetRuntimePreReconcileHook([this]() {
                std::lock_guard<std::mutex> producerLock(
                    RuntimeOutputProducerTransitionMutex());
                InputOrchestratorDetail::runtimeOutputTransitionEpoch.fetch_add(
                    1,
                    std::memory_order_acq_rel);
                scheduler.PrepareForRuntimeTransition();
            });
        }

        ~RuntimeSchedulerHolder()
        {
            kmbox::SetRuntimePreReconcileHook({});
            scheduler.CancelAllAndJoin(
                OutputActionCancelReason::RuntimeChanged);
        }

        OutputScheduler scheduler;
    };

    static RuntimeSchedulerHolder holder;
    return holder.scheduler;
}

} // namespace OW
