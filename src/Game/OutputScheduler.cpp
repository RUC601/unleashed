#include "Game/OutputScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

namespace OW
{
    namespace
    {
        using Clock = std::chrono::steady_clock;
        constexpr auto kReleaseRetryDelay = std::chrono::milliseconds(10);

        OutputAggregateUpdate BuildUpdate(
            const OutputOwnership& ownership,
            std::vector<OutputChange> changes)
        {
            OutputAggregateUpdate update{};
            update.changes = std::move(changes);
            update.backendGeneration = ownership.BackendGeneration();
            for (const OutputControl& control : ownership.HeldControls()) {
                if (control.kind == OutputControlKind::KeyboardModifier) {
                    const unsigned int bit =
                        static_cast<unsigned int>(control.code - 0xE0u);
                    if (bit < 8) {
                        update.keyboardModifierMask |=
                            static_cast<std::uint8_t>(1u << bit);
                    }
                } else if (control.kind == OutputControlKind::KeyboardUsage) {
                    update.keyboardUsages.push_back(
                        static_cast<std::uint8_t>(control.code));
                }
            }
            return update;
        }

        bool ApplyControls(
            OutputOwnership& ownership,
            const OwnerToken& owner,
            const std::vector<OutputControl>& releases,
            const std::vector<OutputControl>& acquires,
            std::vector<OutputChange>& changes)
        {
            changes.clear();
            changes.reserve(releases.size() + acquires.size());
            for (const OutputControl& control : releases) {
                const auto transition = ownership.Release(control, owner);
                if (!transition.has_value())
                    return false;
                if (*transition != OutputTransition::None)
                    changes.push_back({ control, *transition });
            }
            for (const OutputControl& control : acquires) {
                const auto transition = ownership.Acquire(control, owner);
                if (!transition.has_value())
                    return false;
                if (*transition != OutputTransition::None)
                    changes.push_back({ control, *transition });
            }
            return true;
        }

        void AppendChanges(
            std::vector<OutputChange>& destination,
            const std::vector<OutputChange>& source)
        {
            destination.insert(destination.end(), source.begin(), source.end());
        }
    }

    struct OutputScheduler::Impl
    {
        struct ActionState
        {
            std::uint64_t id = 0;
            OwnerToken owner{};
            OutputScheduledCancelCallback cancelCleanup;
            bool cleanupInvoked = false;
            bool releaseOnly = false;
        };

        struct DeadlineKey
        {
            Clock::time_point deadline{};
            std::uint64_t sequence = 0;

            bool operator<(const DeadlineKey& other) const noexcept
            {
                if (deadline != other.deadline)
                    return deadline < other.deadline;
                return sequence < other.sequence;
            }
        };

        struct Deadline
        {
            std::uint64_t actionId = 0;
            std::string actionKey;
            OutputActionStep step;
            bool releaseRetry = false;
        };

        struct CleanupCall
        {
            OutputScheduledCancelCallback callback;
            OutputActionCancelReason reason = OutputActionCancelReason::Explicit;
            std::uint64_t generation = 0;
        };

        explicit Impl(
            OutputAggregateSink aggregateSink,
            OutputRuntimeStateSource stateSource,
            OutputSchedulerTestHooks hooks)
            : sink(std::move(aggregateSink)),
              runtimeStateSource(std::move(stateSource)),
              testHooks(std::move(hooks))
        {
        }

        ~Impl()
        {
            CancelAllAndJoin();
        }

        bool Emit(const OutputAggregateUpdate& update) const
        {
            if (update.changes.empty())
                return true;
            if (!sink)
                return false;
            try {
                return sink(update) == 0;
            } catch (...) {
                return false;
            }
        }

        OutputRuntimeState ReadRuntimeState() const
        {
            if (!runtimeStateSource)
                return {};
            try {
                return runtimeStateSource();
            } catch (...) {
                return {};
            }
        }

        bool EmitReleaseForCurrentGeneration(
            const OutputAggregateUpdate& update) const
        {
            if (update.changes.empty())
                return true;
            const OutputRuntimeState observed = ReadRuntimeState();
            return observed.backendGeneration == update.backendGeneration &&
                Emit(update);
        }

        static void InvokeCancelCleanup(
            const OutputScheduledCancelCallback& cleanup,
            OutputActionCancelReason reason,
            std::uint64_t generation) noexcept
        {
            if (!cleanup)
                return;
            try {
                cleanup(reason, generation);
            } catch (...) {
            }
        }

        static void InvokeCleanupCalls(
            const std::vector<CleanupCall>& cleanups) noexcept
        {
            for (const CleanupCall& cleanup : cleanups) {
                InvokeCancelCleanup(
                    cleanup.callback,
                    cleanup.reason,
                    cleanup.generation);
            }
        }

        void EraseDeadlinesLocked(std::uint64_t actionId)
        {
            for (auto it = deadlines.begin(); it != deadlines.end();) {
                if (it->second.actionId == actionId)
                    it = deadlines.erase(it);
                else
                    ++it;
            }
        }

        bool WaitForOperationSlotLocked(std::unique_lock<std::mutex>& lock)
        {
            if (operationInFlight &&
                externalCallThread == std::this_thread::get_id()) {
                return false;
            }
            cv.wait(lock, [this]() {
                return !operationInFlight || shuttingDown;
            });
            if (shuttingDown)
                return false;
            operationInFlight = true;
            externalCallThread = std::this_thread::get_id();
            return true;
        }

        void FinishOperationLocked()
        {
            externalCallThread = {};
            operationInFlight = false;
            cv.notify_all();
        }

        static constexpr unsigned int kNormalDispatchActive = 1u << 0;
        static constexpr unsigned int kTransitionPauseRequested = 1u << 1;

        bool TryBeginNormalDispatch()
        {
            unsigned int expected = 0;
            return normalDispatchState.compare_exchange_strong(
                expected,
                kNormalDispatchActive,
                std::memory_order_acq_rel,
                std::memory_order_acquire);
        }

        void EndNormalDispatch()
        {
            normalDispatchState.fetch_and(
                ~kNormalDispatchActive,
                std::memory_order_release);
            normalDispatchCv.notify_all();
        }

        void RequestTransitionPauseAndWait()
        {
            normalDispatchState.fetch_or(
                kTransitionPauseRequested,
                std::memory_order_acq_rel);
            std::unique_lock<std::mutex> lock(normalDispatchWaitMutex);
            normalDispatchCv.wait(lock, [this]() {
                return (normalDispatchState.load(std::memory_order_acquire) &
                        kNormalDispatchActive) == 0;
            });
        }

        void ResumeNormalDispatchLocked()
        {
            normalDispatchState.fetch_and(
                ~kTransitionPauseRequested,
                std::memory_order_release);
        }

        void QueueReleaseRetryLocked(
            const std::string& actionKey,
            ActionState& action)
        {
            EraseDeadlinesLocked(action.id);
            action.releaseOnly = true;
            if (stopping || !workerRunning)
                return;

            Deadline retry{};
            retry.actionId = action.id;
            retry.actionKey = actionKey;
            retry.releaseRetry = true;
            deadlines.emplace(
                DeadlineKey{
                    Clock::now() + kReleaseRetryDelay,
                    nextDeadlineSequence++ },
                std::move(retry));
        }

        void MarkCleanupLocked(
            ActionState& action,
            OutputActionCancelReason reason,
            std::vector<CleanupCall>& cleanups)
        {
            if (action.cleanupInvoked)
                return;
            action.cleanupInvoked = true;
            if (action.cancelCleanup) {
                cleanups.push_back({
                    action.cancelCleanup,
                    reason,
                    action.owner.backendGeneration });
            }
        }

        bool EnsureWorkerStartedWhileOperationLocked(
            std::unique_lock<std::mutex>& lock)
        {
            std::thread stoppedWorker;
            if (worker.joinable() && !workerRunning)
                stoppedWorker = std::move(worker);
            if (stoppedWorker.joinable()) {
                lock.unlock();
                stoppedWorker.join();
                lock.lock();
            }

            if (shuttingDown || transitionPaused)
                return false;
            if (workerRunning)
                return true;
            try {
                stopping = false;
                workerRunning = true;
                worker = std::thread([this]() { WorkerMain(); });
                return true;
            } catch (...) {
                workerRunning = false;
                return false;
            }
        }

        bool RetryResidualReleasesWhileOperationLocked(
            std::unique_lock<std::mutex>& lock)
        {
            std::vector<std::string> residualKeys;
            OutputOwnership proposed = ownership;
            std::vector<OutputChange> changes;

            if (residualAll) {
                changes = proposed.CancelAll();
            } else {
                for (const auto& [key, action] : actions) {
                    if (!action.releaseOnly)
                        continue;
                    const auto released = proposed.CancelOwner(action.owner);
                    if (!released.has_value())
                        return false;
                    AppendChanges(changes, *released);
                    residualKeys.push_back(key);
                }
            }

            if (!residualAll && residualKeys.empty())
                return true;

            const OutputAggregateUpdate update =
                BuildUpdate(proposed, std::move(changes));
            lock.unlock();
            const bool accepted = EmitReleaseForCurrentGeneration(update);
            lock.lock();

            if (accepted) {
                ownership = std::move(proposed);
                residualAll = false;
                for (const std::string& key : residualKeys) {
                    const auto actionIt = actions.find(key);
                    if (actionIt == actions.end() || !actionIt->second.releaseOnly)
                        continue;
                    EraseDeadlinesLocked(actionIt->second.id);
                    actions.erase(actionIt);
                }
                return true;
            }

            for (const std::string& key : residualKeys) {
                const auto actionIt = actions.find(key);
                if (actionIt != actions.end() && actionIt->second.releaseOnly)
                    QueueReleaseRetryLocked(key, actionIt->second);
            }
            return false;
        }

        bool InvokeScheduleBarrierWhileOperationLocked(
            std::unique_lock<std::mutex>& lock)
        {
            const auto hook = testHooks.afterWorkerStartBeforeSubmit;
            if (!hook)
                return true;
            bool succeeded = true;
            lock.unlock();
            try {
                hook();
            } catch (...) {
                succeeded = false;
            }
            lock.lock();
            return succeeded && !shuttingDown && !transitionPaused;
        }

        bool Schedule(OutputActionPlan plan)
        {
            if (plan.key.empty() || plan.acquire.empty() || plan.steps.empty())
                return false;
            if (!SynchronizeRuntime())
                return false;

            std::unique_lock<std::mutex> lock(mutex);
            if (!WaitForOperationSlotLocked(lock))
                return false;
            if (!runtimeValid || transitionPaused || actions.contains(plan.key)) {
                FinishOperationLocked();
                return false;
            }
            if (!RetryResidualReleasesWhileOperationLocked(lock)) {
                FinishOperationLocked();
                return false;
            }
            if (!EnsureWorkerStartedWhileOperationLocked(lock) ||
                !InvokeScheduleBarrierWhileOperationLocked(lock)) {
                FinishOperationLocked();
                return false;
            }

            const std::uint64_t actionId = nextActionId++;
            if (nextActionId == 0)
                ++nextActionId;
            const OwnerToken owner{
                plan.ownerSource,
                actionId,
                ownership.BackendGeneration()
            };
            OutputOwnership proposed = ownership;
            std::vector<OutputChange> changes;
            if (!ApplyControls(proposed, owner, {}, plan.acquire, changes)) {
                FinishOperationLocked();
                return false;
            }
            const OutputAggregateUpdate update =
                BuildUpdate(proposed, std::move(changes));
            const OutputRuntimeState expected{
                ownership.BackendGeneration(), true };
            lock.unlock();

            const OutputRuntimeState observed = ReadRuntimeState();
            const bool dispatchClaimed =
                observed == expected && TryBeginNormalDispatch();
            const bool accepted = dispatchClaimed && Emit(update);
            if (dispatchClaimed)
                EndNormalDispatch();
            if (!accepted) {
                InvokeCancelCleanup(
                    plan.cancelCleanup,
                    observed == expected && dispatchClaimed
                        ? OutputActionCancelReason::OutputFailure
                        : OutputActionCancelReason::RuntimeChanged,
                    owner.backendGeneration);
            }

            lock.lock();
            if (accepted) {
                ownership = std::move(proposed);
                ActionState action{};
                action.id = actionId;
                action.owner = owner;
                action.cancelCleanup = std::move(plan.cancelCleanup);
                actions.emplace(plan.key, std::move(action));

                const Clock::time_point started = Clock::now();
                for (OutputActionStep& step : plan.steps) {
                    step.afterStart = (std::max)(
                        step.afterStart,
                        std::chrono::milliseconds::zero());
                    Deadline event{};
                    event.actionId = actionId;
                    event.actionKey = plan.key;
                    event.step = std::move(step);
                    deadlines.emplace(
                        DeadlineKey{
                            started + event.step.afterStart,
                            nextDeadlineSequence++ },
                        std::move(event));
                }
            }
            FinishOperationLocked();
            return accepted;
        }

        bool Cancel(const std::string& key)
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!WaitForOperationSlotLocked(lock))
                return false;
            const auto actionIt = actions.find(key);
            if (actionIt == actions.end()) {
                FinishOperationLocked();
                return false;
            }

            if (actionIt->second.releaseOnly) {
                (void)RetryResidualReleasesWhileOperationLocked(lock);
                FinishOperationLocked();
                return true;
            }

            ActionState& action = actionIt->second;
            EraseDeadlinesLocked(action.id);
            action.releaseOnly = true;
            std::vector<CleanupCall> cleanups;
            MarkCleanupLocked(
                action,
                OutputActionCancelReason::Explicit,
                cleanups);

            OutputOwnership proposed = ownership;
            const auto changes = proposed.CancelOwner(action.owner);
            const bool valid = changes.has_value();
            const OutputAggregateUpdate update = valid
                ? BuildUpdate(proposed, *changes)
                : OutputAggregateUpdate{};
            lock.unlock();

            const bool accepted = valid &&
                EmitReleaseForCurrentGeneration(update);
            InvokeCleanupCalls(cleanups);

            lock.lock();
            const auto currentAction = actions.find(key);
            if (accepted) {
                ownership = std::move(proposed);
                if (currentAction != actions.end() &&
                    currentAction->second.id == action.id) {
                    EraseDeadlinesLocked(currentAction->second.id);
                    actions.erase(currentAction);
                }
            } else if (currentAction != actions.end() &&
                       currentAction->second.id == action.id) {
                QueueReleaseRetryLocked(key, currentAction->second);
            }
            FinishOperationLocked();
            return true;
        }

        void CancelAllAndJoin(
            OutputActionCancelReason reason = OutputActionCancelReason::Explicit)
        {
            std::thread activeWorker;
            std::vector<CleanupCall> cleanups;

            std::unique_lock<std::mutex> lock(mutex);
            if (operationInFlight &&
                externalCallThread == std::this_thread::get_id()) {
                return;
            }
            cv.wait(lock, [this]() { return !shuttingDown; });
            shuttingDown = true;
            cv.wait(lock, [this]() { return !operationInFlight; });
            operationInFlight = true;
            externalCallThread = std::this_thread::get_id();
            stopping = true;
            deadlines.clear();
            cv.notify_all();

            cleanups.reserve(actions.size());
            for (auto& [key, action] : actions) {
                (void)key;
                MarkCleanupLocked(action, reason, cleanups);
            }

            OutputOwnership proposed = ownership;
            const OutputAggregateUpdate update =
                BuildUpdate(proposed, proposed.CancelAll());
            lock.unlock();

            const bool accepted = EmitReleaseForCurrentGeneration(update);
            InvokeCleanupCalls(cleanups);

            lock.lock();
            if (accepted)
                ownership = std::move(proposed);
            actions.clear();
            residualAll = !ownership.Empty();
            runtimeValid = false;
            FinishOperationLocked();
            if (worker.joinable())
                activeWorker = std::move(worker);
            lock.unlock();

            if (activeWorker.joinable())
                activeWorker.join();

            lock.lock();
            workerRunning = false;
            stopping = false;
            shuttingDown = false;
            cv.notify_all();
        }

        void PrepareForRuntimeTransition()
        {
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (operationInFlight &&
                    externalCallThread == std::this_thread::get_id()) {
                    return;
                }
            }

            transitionSetupInProgress.fetch_add(1, std::memory_order_acq_rel);
            // Linearize the pause before waiting for the scheduler operation.
            // A Schedule still reading runtime state can no longer claim the
            // normal-dispatch slot after this point.
            RequestTransitionPauseAndWait();
            const OutputRuntimeState observed = ReadRuntimeState();
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [this]() { return !shuttingDown; });
                if (!transitionPaused) {
                    transitionPaused = true;
                    pausedGeneration = observed.outputGateOpen
                        ? observed.backendGeneration
                        : ownership.BackendGeneration();
                } else if (observed.outputGateOpen &&
                           observed.backendGeneration != pausedGeneration) {
                    // Back-to-back transactions can begin before the producer
                    // tick has resumed the previous pause. Rebase the pause on
                    // the backend that transaction B is about to replace.
                    pausedGeneration = observed.backendGeneration;
                }
                // SynchronizeRuntime may have completed an older transition
                // between the atomic request and this lock acquisition.
                normalDispatchState.fetch_or(
                    kTransitionPauseRequested,
                    std::memory_order_release);
            }
            // The older transition may have cleared PAUSE while this setup was
            // waiting on its runtime-state read. Reassert and wait again so
            // transaction B cannot proceed while a down claimed that gap.
            RequestTransitionPauseAndWait();
            CancelAllAndJoin(OutputActionCancelReason::RuntimeChanged);
            transitionSetupInProgress.fetch_sub(1, std::memory_order_acq_rel);
        }

        bool SynchronizeRuntime()
        {
            if (!runtimeStateSource)
                return false;
            return SynchronizeRuntime(ReadRuntimeState());
        }

        bool SynchronizeRuntime(OutputRuntimeState state)
        {
            bool cancelAttempted = false;
            for (;;) {
                bool needsCancel = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (operationInFlight &&
                        externalCallThread == std::this_thread::get_id()) {
                        return false;
                    }
                    const bool generationChanged =
                        ownership.BackendGeneration() != state.backendGeneration;
                    const bool hasLiveWork = worker.joinable() ||
                        !actions.empty() || !deadlines.empty();
                    needsCancel = !transitionPaused && !cancelAttempted &&
                        hasLiveWork &&
                        (!state.outputGateOpen || generationChanged);
                }
                if (needsCancel) {
                    CancelAllAndJoin(OutputActionCancelReason::RuntimeChanged);
                    cancelAttempted = true;
                    continue;
                }

                std::unique_lock<std::mutex> lock(mutex);
                if (!WaitForOperationSlotLocked(lock))
                    return false;

                const bool generationChanged =
                    ownership.BackendGeneration() != state.backendGeneration;
                const bool hasLiveWork = worker.joinable() ||
                    !actions.empty() || !deadlines.empty();
                if (!transitionPaused && !cancelAttempted && hasLiveWork &&
                    (!state.outputGateOpen || generationChanged)) {
                    FinishOperationLocked();
                    lock.unlock();
                    CancelAllAndJoin(OutputActionCancelReason::RuntimeChanged);
                    cancelAttempted = true;
                    continue;
                }

                if (transitionPaused) {
                    if (transitionSetupInProgress.load(
                            std::memory_order_acquire) != 0 ||
                        !state.outputGateOpen ||
                        state.backendGeneration == pausedGeneration) {
                        // A transition pause blocks new downs, not safety-up
                        // retries against the still-current generation.
                        (void)RetryResidualReleasesWhileOperationLocked(lock);
                        FinishOperationLocked();
                        return false;
                    }
                    transitionPaused = false;
                    ResumeNormalDispatchLocked();
                }

                if (ownership.BackendGeneration() != state.backendGeneration) {
                    // A backend transaction already safety-cleaned the old
                    // device before publishing the new generation. Stale
                    // residual leases are model-only here: never send their up
                    // report through the new backend.
                    (void)ownership.CancelAll();
                    residualAll = false;
                    for (auto it = actions.begin(); it != actions.end();) {
                        EraseDeadlinesLocked(it->second.id);
                        it = actions.erase(it);
                    }
                    if (!ownership.TrySetBackendGeneration(
                            state.backendGeneration)) {
                        runtimeValid = false;
                        FinishOperationLocked();
                        return false;
                    }
                }

                if (!state.outputGateOpen) {
                    (void)RetryResidualReleasesWhileOperationLocked(lock);
                    runtimeValid = false;
                    FinishOperationLocked();
                    return false;
                }

                runtimeValid = true;
                const bool residualReleased =
                    RetryResidualReleasesWhileOperationLocked(lock);
                FinishOperationLocked();
                return residualReleased;
            }
        }

        void AbortWorkerForRuntimeChangeLocked(
            std::unique_lock<std::mutex>& lock,
            OutputRuntimeState observed)
        {
            std::vector<CleanupCall> cleanups;
            cleanups.reserve(actions.size());
            for (auto& [key, action] : actions) {
                (void)key;
                MarkCleanupLocked(
                    action,
                    OutputActionCancelReason::RuntimeChanged,
                    cleanups);
            }
            deadlines.clear();
            stopping = true;
            runtimeValid = false;

            OutputOwnership proposed = ownership;
            const OutputAggregateUpdate update =
                BuildUpdate(proposed, proposed.CancelAll());
            const bool sameGeneration =
                observed.backendGeneration == ownership.BackendGeneration();
            lock.unlock();
            const bool accepted = sameGeneration &&
                EmitReleaseForCurrentGeneration(update);
            InvokeCleanupCalls(cleanups);
            lock.lock();

            if (accepted)
                ownership = std::move(proposed);
            actions.clear();
            residualAll = !ownership.Empty();
        }

        void WorkerMain()
        {
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                cv.wait(lock, [this]() {
                    return stopping || (!operationInFlight && !deadlines.empty());
                });
                if (stopping)
                    break;

                const Clock::time_point deadline = deadlines.begin()->first.deadline;
                if (Clock::now() < deadline) {
                    cv.wait_until(lock, deadline, [this, deadline]() {
                        return stopping || operationInFlight || deadlines.empty() ||
                            deadlines.begin()->first.deadline < deadline;
                    });
                    continue;
                }

                operationInFlight = true;
                externalCallThread = std::this_thread::get_id();
                const OutputRuntimeState expected{
                    ownership.BackendGeneration(), true };
                lock.unlock();
                const OutputRuntimeState observed = ReadRuntimeState();
                lock.lock();

                if (observed != expected || !runtimeValid || transitionPaused) {
                    AbortWorkerForRuntimeChangeLocked(lock, observed);
                    FinishOperationLocked();
                    break;
                }

                auto eventIt = deadlines.begin();
                Deadline event = std::move(eventIt->second);
                deadlines.erase(eventIt);
                const auto actionIt = actions.find(event.actionKey);
                if (actionIt == actions.end() ||
                    actionIt->second.id != event.actionId) {
                    FinishOperationLocked();
                    continue;
                }

                if (event.releaseRetry) {
                    (void)RetryResidualReleasesWhileOperationLocked(lock);
                    FinishOperationLocked();
                    continue;
                }

                if (!TryBeginNormalDispatch()) {
                    AbortWorkerForRuntimeChangeLocked(lock, observed);
                    FinishOperationLocked();
                    break;
                }

                const OwnerToken owner = actionIt->second.owner;
                OutputOwnership proposed = ownership;
                std::vector<OutputChange> changes;
                const bool validStep = ApplyControls(
                    proposed,
                    owner,
                    event.step.release,
                    event.step.acquire,
                    changes);
                const OutputAggregateUpdate update = validStep
                    ? BuildUpdate(proposed, std::move(changes))
                    : OutputAggregateUpdate{};
                lock.unlock();

                const bool outputAccepted = validStep && Emit(update);
                bool callbackAccepted = true;
                if (outputAccepted && event.step.callback) {
                    try {
                        event.step.callback(owner.backendGeneration);
                    } catch (...) {
                        callbackAccepted = false;
                    }
                }
                EndNormalDispatch();
                const bool accepted = outputAccepted && callbackAccepted;

                lock.lock();
                std::vector<CleanupCall> cleanups;
                const auto currentAction = actions.find(event.actionKey);
                if (currentAction == actions.end() ||
                    currentAction->second.id != event.actionId) {
                    FinishOperationLocked();
                    continue;
                }

                if (outputAccepted)
                    ownership = std::move(proposed);

                if (!accepted) {
                    ActionState& action = currentAction->second;
                    EraseDeadlinesLocked(action.id);
                    action.releaseOnly = true;
                    MarkCleanupLocked(
                        action,
                        OutputActionCancelReason::OutputFailure,
                        cleanups);
                    QueueReleaseRetryLocked(event.actionKey, action);
                    lock.unlock();
                    InvokeCleanupCalls(cleanups);
                    lock.lock();
                    FinishOperationLocked();
                    continue;
                }

                if (!event.step.complete) {
                    FinishOperationLocked();
                    continue;
                }

                ActionState& action = currentAction->second;
                EraseDeadlinesLocked(action.id);
                OutputOwnership completed = ownership;
                const auto residual = completed.CancelOwner(action.owner);
                const bool validResidual = residual.has_value();
                const OutputAggregateUpdate cleanupUpdate = validResidual
                    ? BuildUpdate(completed, *residual)
                    : OutputAggregateUpdate{};
                lock.unlock();
                const bool cleanupAccepted = validResidual &&
                    EmitReleaseForCurrentGeneration(cleanupUpdate);
                lock.lock();

                const auto completedAction = actions.find(event.actionKey);
                if (cleanupAccepted) {
                    ownership = std::move(completed);
                    if (completedAction != actions.end() &&
                        completedAction->second.id == event.actionId) {
                        actions.erase(completedAction);
                    }
                } else if (completedAction != actions.end() &&
                           completedAction->second.id == event.actionId) {
                    ActionState& residualAction = completedAction->second;
                    residualAction.releaseOnly = true;
                    MarkCleanupLocked(
                        residualAction,
                        OutputActionCancelReason::OutputFailure,
                        cleanups);
                    QueueReleaseRetryLocked(
                        event.actionKey,
                        residualAction);
                }
                lock.unlock();
                InvokeCleanupCalls(cleanups);
                lock.lock();
                FinishOperationLocked();
            }

            workerRunning = false;
            cv.notify_all();
        }

        bool IsActive(const std::string& key) const
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto it = actions.find(key);
            return it != actions.end() && !it->second.releaseOnly;
        }

        std::size_t ActiveActionCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            std::size_t active = 0;
            for (const auto& [key, action] : actions) {
                (void)key;
                active += action.releaseOnly ? 0u : 1u;
            }
            return active;
        }

        std::size_t PendingDeadlineCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return deadlines.size();
        }

        std::size_t WorkerCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return workerRunning ? 1u : 0u;
        }

        OutputAggregateSink sink;
        OutputRuntimeStateSource runtimeStateSource;
        OutputSchedulerTestHooks testHooks;
        mutable std::mutex mutex;
        std::condition_variable cv;
        std::atomic<unsigned int> normalDispatchState{ 0 };
        std::atomic<unsigned int> transitionSetupInProgress{ 0 };
        std::mutex normalDispatchWaitMutex;
        std::condition_variable normalDispatchCv;
        OutputOwnership ownership{};
        std::unordered_map<std::string, ActionState> actions;
        std::map<DeadlineKey, Deadline> deadlines;
        std::thread worker;
        std::uint64_t nextActionId = 1;
        std::uint64_t nextDeadlineSequence = 1;
        std::uint64_t pausedGeneration = 0;
        bool runtimeValid = false;
        bool residualAll = false;
        bool operationInFlight = false;
        std::thread::id externalCallThread{};
        bool stopping = false;
        bool shuttingDown = false;
        bool workerRunning = false;
        bool transitionPaused = false;
    };

    OutputScheduler::OutputScheduler(
        OutputAggregateSink sink,
        OutputRuntimeStateSource runtimeStateSource,
        OutputSchedulerTestHooks testHooks)
        : impl_(std::make_unique<Impl>(
            std::move(sink),
            std::move(runtimeStateSource),
            std::move(testHooks)))
    {
    }

    OutputScheduler::~OutputScheduler() = default;

    bool OutputScheduler::Schedule(OutputActionPlan plan)
    {
        return impl_->Schedule(std::move(plan));
    }

    bool OutputScheduler::ScheduleTimedHold(
        std::string key,
        OutputOwnerSource ownerSource,
        std::vector<OutputControl> controls,
        std::chrono::milliseconds duration)
    {
        duration = (std::max)(duration, std::chrono::milliseconds(1));
        OutputActionPlan plan{};
        plan.key = std::move(key);
        plan.ownerSource = ownerSource;
        plan.acquire = controls;
        OutputActionStep release{};
        release.afterStart = duration;
        release.release = std::move(controls);
        release.complete = true;
        plan.steps.push_back(std::move(release));
        return Schedule(std::move(plan));
    }

    bool OutputScheduler::Cancel(const std::string& key)
    {
        return impl_->Cancel(key);
    }

    void OutputScheduler::CancelAllAndJoin(OutputActionCancelReason reason)
    {
        impl_->CancelAllAndJoin(reason);
    }

    void OutputScheduler::PrepareForRuntimeTransition()
    {
        impl_->PrepareForRuntimeTransition();
    }

    bool OutputScheduler::SynchronizeRuntime()
    {
        return impl_->SynchronizeRuntime();
    }

    bool OutputScheduler::SynchronizeRuntime(OutputRuntimeState state)
    {
        return impl_->SynchronizeRuntime(state);
    }

    bool OutputScheduler::IsActive(const std::string& key) const
    {
        return impl_->IsActive(key);
    }

    std::size_t OutputScheduler::ActiveActionCount() const
    {
        return impl_->ActiveActionCount();
    }

    std::size_t OutputScheduler::PendingDeadlineCount() const
    {
        return impl_->PendingDeadlineCount();
    }

    std::size_t OutputScheduler::WorkerCount() const
    {
        return impl_->WorkerCount();
    }
}
