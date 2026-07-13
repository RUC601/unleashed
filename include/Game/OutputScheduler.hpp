#pragma once

#include "Game/OutputOwnership.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace OW
{
    struct OutputRuntimeState
    {
        std::uint64_t backendGeneration = 0;
        bool outputGateOpen = false;

        bool operator==(const OutputRuntimeState&) const = default;
    };

    struct OutputAggregateUpdate
    {
        // Aggregate edge changes caused by one scheduler step. Keyboard
        // consumers must send the complete held report below whenever any
        // keyboard control appears in changes.
        std::vector<OutputChange> changes;
        std::uint64_t backendGeneration = 0;
        std::uint8_t keyboardModifierMask = 0;
        std::vector<std::uint8_t> keyboardUsages;

        bool IsReleaseOnly() const noexcept
        {
            if (changes.empty())
                return false;
            for (const OutputChange& change : changes) {
                if (change.transition != OutputTransition::Release)
                    return false;
            }
            return true;
        }
    };

    using OutputAggregateSink =
        std::function<int(const OutputAggregateUpdate& update)>;
    using OutputRuntimeStateSource =
        std::function<OutputRuntimeState()>;
    using OutputScheduledCallback =
        std::function<void(std::uint64_t backendGeneration)>;
    enum class OutputActionCancelReason : std::uint8_t
    {
        Explicit,
        RuntimeChanged,
        OutputFailure
    };
    using OutputScheduledCancelCallback =
        std::function<void(
            OutputActionCancelReason reason,
            std::uint64_t backendGeneration)>;

    // Deterministic concurrency seam used only by the offline self-test. The
    // hook runs outside the scheduler mutex, but while the serialized Schedule
    // operation still owns the worker-start/action-submit slot.
    struct OutputSchedulerTestHooks
    {
        std::function<void()> afterWorkerStartBeforeSubmit;
    };

    struct OutputActionStep
    {
        std::chrono::milliseconds afterStart{};
        std::vector<OutputControl> acquire;
        std::vector<OutputControl> release;
        OutputScheduledCallback callback;
        bool complete = false;
    };

    struct OutputActionPlan
    {
        std::string key;
        OutputOwnerSource ownerSource = OutputOwnerSource::HeroTimedAction;
        std::vector<OutputControl> acquire;
        std::vector<OutputActionStep> steps;
        OutputScheduledCancelCallback cancelCleanup;
    };

    // One lazy, joinable worker owns every timed deadline. OutputOwnership
    // keeps overlapping actions aggregate-safe: the first owner emits a down
    // edge and only the final owner emits the matching up edge.
    class OutputScheduler
    {
    public:
        OutputScheduler(
            OutputAggregateSink sink,
            OutputRuntimeStateSource runtimeStateSource,
            OutputSchedulerTestHooks testHooks = {});
        ~OutputScheduler();

        OutputScheduler(const OutputScheduler&) = delete;
        OutputScheduler& operator=(const OutputScheduler&) = delete;

        bool Schedule(OutputActionPlan plan);
        bool ScheduleTimedHold(
            std::string key,
            OutputOwnerSource ownerSource,
            std::vector<OutputControl> controls,
            std::chrono::milliseconds duration);

        bool Cancel(const std::string& key);
        void CancelAllAndJoin(
            OutputActionCancelReason reason = OutputActionCancelReason::Explicit);

        // Runtime switch hook: close the scheduler gate before the controller
        // releases/shuts down the old backend. The pause survives a failed or
        // disabled reconcile and is lifted only after SynchronizeRuntime sees
        // an open, different backend generation.
        void PrepareForRuntimeTransition();

        // Call at the runtime boundary (and once per producer tick). A closed
        // gate or generation change cancels every action and joins the worker
        // before this function returns. A later valid state can lazily start a
        // fresh worker.
        bool SynchronizeRuntime();
        bool SynchronizeRuntime(OutputRuntimeState state);

        bool IsActive(const std::string& key) const;
        std::size_t ActiveActionCount() const;
        std::size_t PendingDeadlineCount() const;
        std::size_t WorkerCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
