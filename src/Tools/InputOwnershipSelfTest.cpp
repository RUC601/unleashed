#include "Game/OutputOwnership.hpp"
#include "Game/OutputScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
    constexpr std::uint64_t kGeneration = 7;
    constexpr OW::OutputControl kLeftMouse{
        OW::OutputControlKind::MouseButton, 0 };
    constexpr OW::OutputControl kRightMouse{
        OW::OutputControlKind::MouseButton, 1 };
    constexpr OW::OutputControl kE{
        OW::OutputControlKind::KeyboardUsage, 0x08 };
    constexpr OW::OutputControl kQ{
        OW::OutputControlKind::KeyboardUsage, 0x14 };
    constexpr OW::OutputControl kLeftShift{
        OW::OutputControlKind::KeyboardModifier, 0xE1 };

    using namespace std::chrono_literals;

    struct SinkRecorder
    {
        int Apply(const OW::OutputAggregateUpdate& update)
        {
            std::lock_guard<std::mutex> lock(mutex);
            updates.push_back(update);
            return 0;
        }

        std::vector<OW::OutputAggregateUpdate> Snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return updates;
        }

        mutable std::mutex mutex;
        std::vector<OW::OutputAggregateUpdate> updates;
    };

    struct AtomicRuntimeState
    {
        OW::OutputRuntimeState Snapshot() const noexcept
        {
            return {
                generation.load(std::memory_order_acquire),
                outputGateOpen.load(std::memory_order_acquire)
            };
        }

        std::atomic<std::uint64_t> generation{ kGeneration };
        std::atomic<bool> outputGateOpen{ true };
    };

    OW::OwnerToken Owner(
        std::uint64_t id,
        OW::OutputOwnerSource source = OW::OutputOwnerSource::Test,
        std::uint64_t generation = kGeneration)
    {
        return { source, id, generation };
    }

    int Fail(const char* message)
    {
        std::fprintf(stderr, "[InputOwnershipSelfTest] FAIL: %s\n", message);
        return 1;
    }

    bool IsTransition(
        const std::optional<OW::OutputTransition>& actual,
        OW::OutputTransition expected)
    {
        return actual.has_value() && *actual == expected;
    }

    bool HasRelease(
        const std::vector<OW::OutputChange>& changes,
        const OW::OutputControl& control)
    {
        for (const auto& change : changes) {
            if (change.control == control &&
                change.transition == OW::OutputTransition::Release) {
                return true;
            }
        }
        return false;
    }

    bool HasTransition(
        const OW::OutputAggregateUpdate& update,
        const OW::OutputControl& control,
        OW::OutputTransition transition)
    {
        for (const auto& change : update.changes) {
            if (change.control == control && change.transition == transition)
                return true;
        }
        return false;
    }

    bool WaitForSchedulerIdle(
        const OW::OutputScheduler& scheduler,
        std::chrono::milliseconds timeout = 500ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (scheduler.ActiveActionCount() == 0 &&
                scheduler.PendingDeadlineCount() == 0) {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        }
        return false;
    }

    template <typename Predicate>
    bool WaitForCondition(
        Predicate predicate,
        std::chrono::milliseconds timeout = 500ms)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return predicate();
    }

    int TestSharedControlLastOwnerReleases()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto ownerA = Owner(1);
        const auto ownerB = Owner(2);

        if (!IsTransition(
                ownership.Acquire(kLeftMouse, ownerA),
                OW::OutputTransition::Press) ||
            !IsTransition(
                ownership.Acquire(kLeftMouse, ownerB),
                OW::OutputTransition::None) ||
            ownership.OwnerCount(kLeftMouse) != 2) {
            return Fail("first/second owner acquire transition is wrong");
        }
        if (!IsTransition(
                ownership.Release(kLeftMouse, ownerA),
                OW::OutputTransition::None) ||
            !ownership.IsHeld(kLeftMouse) ||
            ownership.OwnerCount(kLeftMouse) != 1) {
            return Fail("owner A released owner B's shared control");
        }
        if (!IsTransition(
                ownership.Release(kLeftMouse, ownerB),
                OW::OutputTransition::Release) ||
            !ownership.Empty()) {
            return Fail("last owner did not release shared control");
        }
        return 0;
    }

    int TestIndependentControlsAndModifier()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto ownerA = Owner(1);
        const auto ownerB = Owner(2);

        if (!IsTransition(ownership.Acquire(kE, ownerA), OW::OutputTransition::Press) ||
            !IsTransition(ownership.Acquire(kQ, ownerB), OW::OutputTransition::Press) ||
            !IsTransition(
                ownership.Acquire(kLeftShift, ownerA),
                OW::OutputTransition::Press) ||
            ownership.HeldControlCount() != 3 ||
            !ownership.IsHeld(kE) || !ownership.IsHeld(kQ) ||
            !ownership.IsHeld(kLeftShift)) {
            return Fail("different keys or modifier were not held independently");
        }
        const auto held = ownership.HeldControls();
        if (held.size() != 3 || held[0] != kE || held[1] != kQ ||
            held[2] != kLeftShift) {
            return Fail("held-control snapshot is incomplete or unstable");
        }
        return 0;
    }

    int TestIdempotentAcquireRelease()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto owner = Owner(1);

        if (!IsTransition(ownership.Acquire(kE, owner), OW::OutputTransition::Press) ||
            !IsTransition(ownership.Acquire(kE, owner), OW::OutputTransition::None) ||
            ownership.OwnerCount(kE) != 1 ||
            !IsTransition(ownership.Release(kE, owner), OW::OutputTransition::Release) ||
            !IsTransition(ownership.Release(kE, owner), OW::OutputTransition::None) ||
            !ownership.Empty()) {
            return Fail("same-owner acquire/release was not idempotent");
        }
        return 0;
    }

    int TestCancelOwnerDoesNotAffectOthers()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto ownerA = Owner(1);
        const auto ownerB = Owner(2);

        ownership.Acquire(kLeftMouse, ownerA);
        ownership.Acquire(kLeftMouse, ownerB);
        ownership.Acquire(kE, ownerA);
        ownership.Acquire(kQ, ownerB);

        const auto changes = ownership.CancelOwner(ownerA);
        if (!changes.has_value() || changes->size() != 1 ||
            !HasRelease(*changes, kE) || !ownership.IsHeld(kLeftMouse) ||
            ownership.OwnerCount(kLeftMouse) != 1 || !ownership.IsHeld(kQ) ||
            ownership.IsHeld(kE)) {
            return Fail("CancelOwner affected another owner or missed a sole lease");
        }
        return 0;
    }

    int TestStaleGenerationRejected()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto current = Owner(1);
        const auto stale = Owner(1, OW::OutputOwnerSource::Test, kGeneration - 1);

        if (!IsTransition(
                ownership.Acquire(kRightMouse, current),
                OW::OutputTransition::Press) ||
            ownership.Release(kRightMouse, stale).has_value() ||
            ownership.CancelOwner(stale).has_value() ||
            !ownership.IsHeld(kRightMouse) ||
            ownership.OwnerCount(kRightMouse) != 1) {
            return Fail("stale token changed current generation state");
        }

        if (ownership.TrySetBackendGeneration(kGeneration + 1))
            return Fail("generation changed while controls were held");
        const auto released = ownership.CancelAll();
        if (released.size() != 1 || !HasRelease(released, kRightMouse) ||
            !ownership.TrySetBackendGeneration(kGeneration + 1) ||
            ownership.BackendGeneration() != kGeneration + 1) {
            return Fail("empty model could not advance backend generation");
        }
        return 0;
    }

    int TestCancelAllClearsModel()
    {
        OW::OutputOwnership ownership(kGeneration);
        ownership.Acquire(kLeftMouse, Owner(1));
        ownership.Acquire(kLeftMouse, Owner(2));
        ownership.Acquire(kE, Owner(1));
        ownership.Acquire(kLeftShift, Owner(3));

        const auto changes = ownership.CancelAll();
        if (changes.size() != 3 || !HasRelease(changes, kLeftMouse) ||
            !HasRelease(changes, kE) || !HasRelease(changes, kLeftShift) ||
            !ownership.Empty() || ownership.HeldControlCount() != 0 ||
            ownership.OwnerCount(kLeftMouse) != 0 ||
            !ownership.HeldControls().empty()) {
            return Fail("CancelAll did not release every aggregate control");
        }
        if (!ownership.CancelAll().empty())
            return Fail("CancelAll was not idempotent");
        return 0;
    }

    int TestInvalidControlAndTokenRejected()
    {
        OW::OutputOwnership ownership(kGeneration);
        const auto validOwner = Owner(1);
        const OW::OutputControl invalidMouse{
            OW::OutputControlKind::MouseButton, 3 };
        const OW::OutputControl modifierAsKey{
            OW::OutputControlKind::KeyboardUsage, 0xE1 };
        const OW::OutputControl keyAsModifier{
            OW::OutputControlKind::KeyboardModifier, 0x08 };
        const OW::OutputControl invalidKind{
            static_cast<OW::OutputControlKind>(0xFF), 0 };
        const auto zeroId = Owner(0);
        const auto invalidSource = Owner(
            2, static_cast<OW::OutputOwnerSource>(0xFF));

        if (ownership.Acquire(invalidMouse, validOwner).has_value() ||
            ownership.Acquire(modifierAsKey, validOwner).has_value() ||
            ownership.Acquire(keyAsModifier, validOwner).has_value() ||
            ownership.Acquire(invalidKind, validOwner).has_value() ||
            ownership.Acquire(kE, zeroId).has_value() ||
            ownership.Release(kE, invalidSource).has_value() ||
            ownership.CancelOwner(zeroId).has_value() ||
            !ownership.Empty()) {
            return Fail("invalid control or token was accepted");
        }
        return 0;
    }

    int TestSchedulerOverlappingSameKey()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "same-a", OW::OutputOwnerSource::Test, { kE }, 30ms)) {
            return Fail("scheduler rejected first same-key hold");
        }
        std::this_thread::sleep_for(5ms);
        if (!scheduler.ScheduleTimedHold(
                "same-b", OW::OutputOwnerSource::Test, { kE }, 60ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            return Fail("scheduler rejected or failed to finish overlapping same-key hold");
        }

        int presses = 0;
        int releases = 0;
        for (const auto& update : recorder.Snapshot()) {
            presses += HasTransition(update, kE, OW::OutputTransition::Press) ? 1 : 0;
            releases += HasTransition(update, kE, OW::OutputTransition::Release) ? 1 : 0;
        }
        scheduler.CancelAllAndJoin();
        if (presses != 1 || releases != 1 || scheduler.WorkerCount() != 0)
            return Fail("same-key overlap emitted an early/duplicate edge or leaked worker");
        return 0;
    }

    int TestSchedulerOverlappingDifferentKeys()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "different-e", OW::OutputOwnerSource::Test, { kE }, 35ms) ||
            !scheduler.ScheduleTimedHold(
                "different-q", OW::OutputOwnerSource::Test, { kQ }, 55ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            return Fail("different-key overlap failed to schedule or complete");
        }

        bool sawBoth = false;
        bool sawOnlyQAfterE = false;
        for (const auto& update : recorder.Snapshot()) {
            const bool hasE = std::find(
                update.keyboardUsages.begin(), update.keyboardUsages.end(), 0x08) !=
                update.keyboardUsages.end();
            const bool hasQ = std::find(
                update.keyboardUsages.begin(), update.keyboardUsages.end(), 0x14) !=
                update.keyboardUsages.end();
            sawBoth = sawBoth || (hasE && hasQ);
            sawOnlyQAfterE = sawOnlyQAfterE || (!hasE && hasQ);
        }
        scheduler.CancelAllAndJoin();
        if (!sawBoth || !sawOnlyQAfterE)
            return Fail("different-key aggregate report lost an overlapping key");
        return 0;
    }

    int TestSchedulerModifierAndUsageReport()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "shift-e",
                OW::OutputOwnerSource::Test,
                { kLeftShift, kE },
                25ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            return Fail("Shift+E aggregate hold did not complete");
        }

        const auto updates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (updates.size() != 2 || updates.front().keyboardModifierMask != 0x02 ||
            updates.front().keyboardUsages != std::vector<std::uint8_t>{ 0x08 } ||
            updates.back().keyboardModifierMask != 0 ||
            !updates.back().keyboardUsages.empty()) {
            return Fail("Shift+E did not use complete aggregate keyboard reports");
        }
        return 0;
    }

    int TestSchedulerReleaseOnlyReportPreservesHeldUsage()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "held-e", OW::OutputOwnerSource::Test, { kE }, 70ms) ||
            !scheduler.ScheduleTimedHold(
                "short-shift", OW::OutputOwnerSource::Test, { kLeftShift }, 25ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            return Fail("release-only aggregate scenario did not complete");
        }

        bool sawShiftReleasePreservingE = false;
        for (const auto& update : recorder.Snapshot()) {
            if (HasTransition(
                    update, kLeftShift, OW::OutputTransition::Release) &&
                update.IsReleaseOnly() &&
                update.keyboardModifierMask == 0 &&
                update.keyboardUsages == std::vector<std::uint8_t>{ 0x08 }) {
                sawShiftReleasePreservingE = true;
            }
        }
        scheduler.CancelAllAndJoin();
        if (!sawShiftReleasePreservingE)
            return Fail("Shift release did not preserve held E as release-only report");
        return 0;
    }

    int TestSchedulerRejectedDownDoesNotEmitUp()
    {
        std::mutex updatesMutex;
        std::vector<OW::OutputAggregateUpdate> updates;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> cleanupReason{ -1 };
        OW::OutputScheduler scheduler(
            [&updatesMutex, &updates](const OW::OutputAggregateUpdate& update) {
                std::lock_guard<std::mutex> lock(updatesMutex);
                updates.push_back(update);
                return -1;
            },
            [&runtime]() { return runtime; });

        OW::OutputActionPlan plan{};
        plan.key = "reject-down";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&cleanupReason](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            cleanupReason.store(static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep release{};
        release.afterStart = 20ms;
        release.release = { kE };
        release.complete = true;
        plan.steps.push_back(std::move(release));

        if (scheduler.Schedule(std::move(plan)))
            return Fail("scheduler accepted a rejected down sink");
        scheduler.CancelAllAndJoin();

        std::lock_guard<std::mutex> lock(updatesMutex);
        if (updates.size() != 1 ||
            !HasTransition(updates.front(), kE, OW::OutputTransition::Press) ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::OutputFailure)) {
            return Fail("rejected down mutated ownership or emitted compensating up");
        }
        return 0;
    }

    int TestSchedulerScheduleGenerationToctouRejected()
    {
        std::atomic<int> sourceReads{ 0 };
        std::atomic<int> sinkCalls{ 0 };
        std::atomic<int> cleanupReason{ -1 };
        OW::OutputScheduler scheduler(
            [&sinkCalls](const OW::OutputAggregateUpdate&) {
                ++sinkCalls;
                return 0;
            },
            [&sourceReads]() {
                const int read = ++sourceReads;
                return OW::OutputRuntimeState{
                    read == 1 ? kGeneration : kGeneration + 1,
                    true
                };
            });

        OW::OutputActionPlan plan{};
        plan.key = "toctou";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&cleanupReason](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            cleanupReason.store(static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep release{};
        release.afterStart = 20ms;
        release.release = { kE };
        release.complete = true;
        plan.steps.push_back(std::move(release));

        if (scheduler.Schedule(std::move(plan)) || sinkCalls.load() != 0 ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::RuntimeChanged) ||
            scheduler.ActiveActionCount() != 0) {
            return Fail("schedule TOCTOU dispatched a stale-generation down");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerCancelNoLateCallbackAndRestart()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> lateCallbacks{ 0 };
        std::atomic<int> cancelCleanups{ 0 };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        OW::OutputActionPlan plan{};
        plan.key = "cancelled";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&cancelCleanups](
            OW::OutputActionCancelReason,
            std::uint64_t) {
            ++cancelCleanups;
        };
        OW::OutputActionStep late{};
        late.afterStart = 50ms;
        late.release = { kE };
        late.callback = [&lateCallbacks](std::uint64_t) { ++lateCallbacks; };
        late.complete = true;
        plan.steps.push_back(std::move(late));

        if (!scheduler.Schedule(std::move(plan)))
            return Fail("cancel test action did not schedule");
        std::this_thread::sleep_for(5ms);
        scheduler.CancelAllAndJoin();
        std::this_thread::sleep_for(65ms);
        if (lateCallbacks.load() != 0 || cancelCleanups.load() != 1 ||
            scheduler.ActiveActionCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0 ||
            scheduler.WorkerCount() != 0) {
            return Fail("cancel returned before cleanup or allowed a late callback");
        }

        if (!scheduler.ScheduleTimedHold(
                "restart", OW::OutputOwnerSource::Test, { kQ }, 20ms) ||
            scheduler.WorkerCount() != 1 ||
            !WaitForSchedulerIdle(scheduler)) {
            return Fail("scheduler worker did not restart after cancel+join");
        }
        scheduler.CancelAllAndJoin();
        if (scheduler.WorkerCount() != 0)
            return Fail("restarted scheduler worker did not join");
        return 0;
    }

    int TestSchedulerGenerationAndGateCancellation()
    {
        SinkRecorder recorder;
        AtomicRuntimeState runtime;
        std::atomic<int> callbacks{ 0 };
        std::atomic<int> staleGenerationSinkCalls{ 0 };
        std::atomic<int> gatedSinkRejects{ 0 };
        OW::OutputScheduler scheduler(
            [&recorder, &runtime, &staleGenerationSinkCalls, &gatedSinkRejects](
                const OW::OutputAggregateUpdate& update) {
                const OW::OutputRuntimeState current = runtime.Snapshot();
                if (update.backendGeneration != current.backendGeneration) {
                    ++staleGenerationSinkCalls;
                    return -1;
                }
                if (!current.outputGateOpen) {
                    ++gatedSinkRejects;
                    return -1;
                }
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime.Snapshot(); });

        OW::OutputActionPlan plan{};
        plan.key = "generation";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        OW::OutputActionStep step{};
        step.afterStart = 80ms;
        step.release = { kE };
        step.callback = [&callbacks](std::uint64_t) { ++callbacks; };
        step.complete = true;
        plan.steps.push_back(std::move(step));
        if (!scheduler.Schedule(std::move(plan)))
            return Fail("generation test action did not schedule");

        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            scheduler.WorkerCount() != 0 || scheduler.ActiveActionCount() != 0) {
            return Fail("generation change did not cancel+join and re-arm");
        }
        std::this_thread::sleep_for(90ms);
        if (callbacks.load() != 0)
            return Fail("stale-generation callback ran after cancellation");

        if (!scheduler.ScheduleTimedHold(
                "new-generation", OW::OutputOwnerSource::Test, { kQ }, 40ms)) {
            return Fail("new generation could not start a replacement worker");
        }
        runtime.outputGateOpen.store(false, std::memory_order_release);
        if (scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            scheduler.WorkerCount() != 0 ||
            scheduler.ActiveActionCount() != 0 || scheduler.PendingDeadlineCount() != 0) {
            return Fail("closed output gate did not cancel+join scheduler");
        }
        if (staleGenerationSinkCalls.load() != 0 ||
            gatedSinkRejects.load() < 1) {
            return Fail("generation change sent an old release through the new backend");
        }
        return 0;
    }

    int TestSchedulerRetriesFailedReleaseWithoutForgettingLease()
    {
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> remainingFailures{ 3 };
        std::atomic<int> releaseAttempts{ 0 };
        std::atomic<int> acceptedReleases{ 0 };
        std::atomic<int> cleanupCalls{ 0 };
        std::atomic<int> cleanupReason{ -1 };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                if (!HasTransition(
                        update, kE, OW::OutputTransition::Release)) {
                    return 0;
                }
                ++releaseAttempts;
                int remaining = remainingFailures.load(std::memory_order_acquire);
                while (remaining > 0 &&
                       !remainingFailures.compare_exchange_weak(
                           remaining,
                           remaining - 1,
                           std::memory_order_acq_rel)) {
                }
                if (remaining > 0)
                    return -1;
                ++acceptedReleases;
                return 0;
            },
            [&runtime]() { return runtime; });

        OW::OutputActionPlan plan{};
        plan.key = "release-retry";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            ++cleanupCalls;
            cleanupReason.store(
                static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep release{};
        release.afterStart = 15ms;
        release.release = { kE };
        release.complete = true;
        plan.steps.push_back(std::move(release));

        if (!scheduler.Schedule(std::move(plan)) ||
            !WaitForCondition([&]() {
                return acceptedReleases.load(std::memory_order_acquire) == 1 &&
                    scheduler.ActiveActionCount() == 0 &&
                    scheduler.PendingDeadlineCount() == 0;
            })) {
            scheduler.CancelAllAndJoin();
            return Fail("failed up was forgotten instead of retried to recovery");
        }
        scheduler.CancelAllAndJoin();
        if (releaseAttempts.load() < 4 || cleanupCalls.load() != 1 ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::OutputFailure) ||
            scheduler.WorkerCount() != 0) {
            return Fail("release retry duplicated cleanup or leaked scheduler state");
        }
        return 0;
    }

    int TestSchedulerCancelFailureSuppressesNormalCallbacks()
    {
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<bool> allowRelease{ false };
        std::atomic<int> acceptedReleases{ 0 };
        std::atomic<int> normalCallbacks{ 0 };
        std::atomic<int> cleanupCalls{ 0 };
        std::atomic<int> cleanupReason{ -1 };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                if (!HasTransition(
                        update, kE, OW::OutputTransition::Release)) {
                    return 0;
                }
                if (!allowRelease.load(std::memory_order_acquire))
                    return -1;
                ++acceptedReleases;
                return 0;
            },
            [&runtime]() { return runtime; });

        OW::OutputActionPlan plan{};
        plan.key = "cancel-release-retry";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            ++cleanupCalls;
            cleanupReason.store(
                static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep late{};
        late.afterStart = 80ms;
        late.release = { kE };
        late.callback = [&](std::uint64_t) { ++normalCallbacks; };
        late.complete = true;
        plan.steps.push_back(std::move(late));

        if (!scheduler.Schedule(std::move(plan)))
            return Fail("cancel release retry action did not schedule");
        std::this_thread::sleep_for(5ms);
        if (!scheduler.Cancel("cancel-release-retry")) {
            scheduler.CancelAllAndJoin();
            return Fail("cancel rejected an active action");
        }
        std::this_thread::sleep_for(100ms);
        if (normalCallbacks.load() != 0 || cleanupCalls.load() != 1 ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::Explicit)) {
            scheduler.CancelAllAndJoin();
            return Fail("failed cancel up allowed a late callback or duplicate cleanup");
        }

        allowRelease.store(true, std::memory_order_release);
        if (!WaitForCondition([&]() {
                return acceptedReleases.load(std::memory_order_acquire) == 1 &&
                    scheduler.PendingDeadlineCount() == 0;
            })) {
            scheduler.CancelAllAndJoin();
            return Fail("cancelled residual lease did not release after recovery");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerCancelAllPersistentFailureIsFiniteAndBlocksNewDown()
    {
        struct Attempt
        {
            OW::OutputAggregateUpdate update;
            bool accepted = false;
        };
        std::mutex attemptsMutex;
        std::vector<Attempt> attempts;
        std::atomic<bool> rejectRelease{ false };
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                const bool isRelease = update.IsReleaseOnly();
                const bool accepted =
                    !isRelease || !rejectRelease.load(std::memory_order_acquire);
                std::lock_guard<std::mutex> lock(attemptsMutex);
                attempts.push_back({ update, accepted });
                return accepted ? 0 : -1;
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "persistent", OW::OutputOwnerSource::Test, { kE }, 2s)) {
            return Fail("persistent release test did not schedule");
        }
        rejectRelease.store(true, std::memory_order_release);
        const auto started = std::chrono::steady_clock::now();
        scheduler.CancelAllAndJoin();
        const auto elapsed = std::chrono::steady_clock::now() - started;
        if (elapsed > 250ms || scheduler.WorkerCount() != 0 ||
            scheduler.ActiveActionCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0) {
            return Fail("persistent release failure prevented finite cancel+join");
        }

        if (scheduler.ScheduleTimedHold(
                "blocked-down", OW::OutputOwnerSource::Test, { kQ }, 20ms)) {
            scheduler.CancelAllAndJoin();
            return Fail("new down bypassed an unreleased same-generation residual");
        }

        rejectRelease.store(false, std::memory_order_release);
        std::size_t recoveryStart = 0;
        {
            std::lock_guard<std::mutex> lock(attemptsMutex);
            recoveryStart = attempts.size();
        }
        if (!scheduler.ScheduleTimedHold(
                "recovered-down", OW::OutputOwnerSource::Test, { kQ }, 25ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("scheduler did not recover after residual release succeeded");
        }

        std::vector<Attempt> recovered;
        {
            std::lock_guard<std::mutex> lock(attemptsMutex);
            recovered.assign(attempts.begin() + recoveryStart, attempts.end());
        }
        scheduler.CancelAllAndJoin();
        if (recovered.size() < 2 || !recovered[0].accepted ||
            !HasTransition(
                recovered[0].update, kE, OW::OutputTransition::Release) ||
            !recovered[1].accepted ||
            !HasTransition(
                recovered[1].update, kQ, OW::OutputTransition::Press)) {
            return Fail("recovery did not release residual before accepting new down");
        }
        return 0;
    }

    int TestSchedulerGenerationChangeDropsResidualWithoutNewBackendUp()
    {
        SinkRecorder recorder;
        std::atomic<bool> rejectRelease{ false };
        AtomicRuntimeState runtime;
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                recorder.Apply(update);
                if (update.IsReleaseOnly() &&
                    rejectRelease.load(std::memory_order_acquire)) {
                    return -1;
                }
                return 0;
            },
            [&runtime]() { return runtime.Snapshot(); });

        if (!scheduler.ScheduleTimedHold(
                "stale-residual", OW::OutputOwnerSource::Test, { kE }, 2s)) {
            return Fail("generation residual test did not schedule");
        }
        rejectRelease.store(true, std::memory_order_release);
        scheduler.CancelAllAndJoin();
        const std::size_t beforeGenerationChange = recorder.Snapshot().size();

        rejectRelease.store(false, std::memory_order_release);
        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()))
            return Fail("new generation did not discard stale residual model");
        if (recorder.Snapshot().size() != beforeGenerationChange) {
            return Fail("stale residual emitted an up through the new backend");
        }

        if (!scheduler.ScheduleTimedHold(
                "new-generation-down",
                OW::OutputOwnerSource::Test,
                { kQ },
                20ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("new generation could not schedule after stale residual drop");
        }
        const auto updates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (updates.size() <= beforeGenerationChange ||
            updates[beforeGenerationChange].backendGeneration != kGeneration + 1 ||
            !HasTransition(
                updates[beforeGenerationChange],
                kQ,
                OW::OutputTransition::Press)) {
            return Fail("first new-generation output was not the requested down");
        }
        return 0;
    }

    int TestSchedulerWorkerStartAndSubmissionAreOneOperation()
    {
        std::mutex barrierMutex;
        std::condition_variable barrierCv;
        bool hookEntered = false;
        bool releaseHook = false;
        std::atomic<bool> hookEnabled{ true };
        std::atomic<bool> scheduleResult{ true };
        std::atomic<bool> cancelReturned{ false };
        OW::OutputRuntimeState runtime{ kGeneration, true };

        OW::OutputSchedulerTestHooks hooks{};
        hooks.afterWorkerStartBeforeSubmit = [&]() {
            if (!hookEnabled.load(std::memory_order_acquire))
                return;
            std::unique_lock<std::mutex> lock(barrierMutex);
            hookEntered = true;
            barrierCv.notify_all();
            barrierCv.wait(lock, [&]() { return releaseHook; });
        };
        OW::OutputScheduler scheduler(
            [](const OW::OutputAggregateUpdate&) { return 0; },
            [&runtime]() { return runtime; },
            std::move(hooks));

        std::thread scheduleThread([&]() {
            scheduleResult.store(
                scheduler.ScheduleTimedHold(
                    "barrier", OW::OutputOwnerSource::Test, { kE }, 40ms),
                std::memory_order_release);
        });
        {
            std::unique_lock<std::mutex> lock(barrierMutex);
            if (!barrierCv.wait_for(lock, 500ms, [&]() { return hookEntered; })) {
                releaseHook = true;
                barrierCv.notify_all();
                scheduleThread.join();
                scheduler.CancelAllAndJoin();
                return Fail("schedule operation barrier was not reached");
            }
        }

        std::thread cancelThread([&]() {
            scheduler.CancelAllAndJoin();
            cancelReturned.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(20ms);
        const bool cancelledBeforeSubmitFinished =
            cancelReturned.load(std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock(barrierMutex);
            releaseHook = true;
        }
        barrierCv.notify_all();
        scheduleThread.join();
        cancelThread.join();

        if (cancelledBeforeSubmitFinished || scheduleResult.load() ||
            scheduler.WorkerCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0) {
            return Fail("CancelAll split worker startup from action submission");
        }

        hookEnabled.store(false, std::memory_order_release);
        if (!scheduler.ScheduleTimedHold(
                "post-barrier", OW::OutputOwnerSource::Test, { kQ }, 20ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("scheduler left a deadline without a live worker");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerRuntimeTransitionPauseRequiresNewGeneration()
    {
        SinkRecorder recorder;
        AtomicRuntimeState runtime;
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime.Snapshot(); });

        if (!scheduler.ScheduleTimedHold(
                "before-transition",
                OW::OutputOwnerSource::Test,
                { kE },
                2s)) {
            return Fail("transition pause setup did not schedule");
        }
        scheduler.PrepareForRuntimeTransition();
        if (scheduler.WorkerCount() != 0 ||
            scheduler.ScheduleTimedHold(
                "paused-same-generation",
                OW::OutputOwnerSource::Test,
                { kQ },
                20ms) ||
            scheduler.SynchronizeRuntime(runtime.Snapshot())) {
            return Fail("runtime transition pause reopened on the old generation");
        }

        runtime.outputGateOpen.store(false, std::memory_order_release);
        if (scheduler.SynchronizeRuntime(runtime.Snapshot()))
            return Fail("disabled reconcile reopened the transition pause");

        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        scheduler.PrepareForRuntimeTransition();
        if (scheduler.SynchronizeRuntime(runtime.Snapshot()))
            return Fail("failed reconcile attempt reopened the transition pause");
        runtime.outputGateOpen.store(true, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !scheduler.ScheduleTimedHold(
                "after-transition",
                OW::OutputOwnerSource::Test,
                { kQ },
                20ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("new open generation did not resume the paused scheduler");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerTransitionPauseWinsBlockedScheduleStateRead()
    {
        std::atomic<int> stateReads{ 0 };
        std::atomic<std::uint64_t> generation{ kGeneration };
        std::atomic<int> sinkCalls{ 0 };
        std::atomic<int> cleanupCalls{ 0 };
        std::atomic<int> cleanupReason{ -1 };
        std::mutex stateMutex;
        std::condition_variable stateCv;
        bool secondReadEntered = false;
        bool pauseReadObserved = false;
        bool releaseSecondRead = false;

        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate&) {
                ++sinkCalls;
                return 0;
            },
            [&]() {
                const int read = ++stateReads;
                if (read == 2) {
                    std::unique_lock<std::mutex> lock(stateMutex);
                    secondReadEntered = true;
                    stateCv.notify_all();
                    stateCv.wait(lock, [&]() { return releaseSecondRead; });
                } else if (read >= 3) {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    pauseReadObserved = true;
                    stateCv.notify_all();
                }
                return OW::OutputRuntimeState{
                    generation.load(std::memory_order_acquire), true };
            });

        OW::OutputActionPlan plan{};
        plan.key = "blocked-state-read";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            ++cleanupCalls;
            cleanupReason.store(
                static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep release{};
        release.afterStart = 50ms;
        release.release = { kE };
        release.complete = true;
        plan.steps.push_back(std::move(release));

        std::atomic<bool> scheduleAccepted{ true };
        std::thread scheduleThread([&]() {
            scheduleAccepted.store(
                scheduler.Schedule(std::move(plan)),
                std::memory_order_release);
        });
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            if (!stateCv.wait_for(
                    lock, 500ms, [&]() { return secondReadEntered; })) {
                releaseSecondRead = true;
                stateCv.notify_all();
                scheduleThread.join();
                scheduler.CancelAllAndJoin();
                return Fail("Schedule did not reach its blocked second state read");
            }
        }

        std::thread pauseThread([&]() {
            scheduler.PrepareForRuntimeTransition();
        });
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            if (!stateCv.wait_for(
                    lock, 500ms, [&]() { return pauseReadObserved; })) {
                releaseSecondRead = true;
                stateCv.notify_all();
                scheduleThread.join();
                pauseThread.join();
                scheduler.CancelAllAndJoin();
                return Fail("transition pause did not linearize during state read");
            }
            releaseSecondRead = true;
        }
        stateCv.notify_all();
        scheduleThread.join();
        pauseThread.join();

        if (scheduleAccepted.load(std::memory_order_acquire) ||
            sinkCalls.load(std::memory_order_acquire) != 0 ||
            cleanupCalls.load(std::memory_order_acquire) != 1 ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::RuntimeChanged) ||
            scheduler.WorkerCount() != 0) {
            return Fail("transition pause allowed a Press after its linearization point");
        }

        generation.store(kGeneration + 1, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime({ kGeneration + 1, true }))
            return Fail("blocked-read transition pause could not resume on new generation");
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerBackToBackTransitionSetupCannotLosePause()
    {
        AtomicRuntimeState runtime;
        std::atomic<bool> blockNextStateRead{ false };
        std::atomic<int> sinkCalls{ 0 };
        std::mutex stateMutex;
        std::condition_variable stateCv;
        bool blockedReadEntered = false;
        bool releaseBlockedRead = false;

        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate&) {
                ++sinkCalls;
                return 0;
            },
            [&]() {
                if (blockNextStateRead.exchange(
                        false, std::memory_order_acq_rel)) {
                    std::unique_lock<std::mutex> lock(stateMutex);
                    blockedReadEntered = true;
                    stateCv.notify_all();
                    stateCv.wait(lock, [&]() { return releaseBlockedRead; });
                }
                return runtime.Snapshot();
            });

        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()))
            return Fail("stacked transition test could not initialize generation A");
        scheduler.PrepareForRuntimeTransition();

        // Transaction A has published generation B, but no producer tick has
        // resumed A's pause yet. Begin transaction B and hold its state read at
        // the exact window where the old implementation could clear PAUSE.
        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        blockNextStateRead.store(true, std::memory_order_release);
        std::thread prepareB([&]() {
            scheduler.PrepareForRuntimeTransition();
        });
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            if (!stateCv.wait_for(
                    lock, 500ms, [&]() { return blockedReadEntered; })) {
                releaseBlockedRead = true;
                stateCv.notify_all();
                prepareB.join();
                scheduler.CancelAllAndJoin();
                return Fail("stacked transition B did not block in state read");
            }
        }

        const bool resumedA = scheduler.SynchronizeRuntime(
            { kGeneration + 1, true });
        std::atomic<bool> scheduleAccepted{ true };
        std::thread scheduleDuringB([&]() {
            scheduleAccepted.store(
                scheduler.ScheduleTimedHold(
                    "stacked-transition-down",
                    OW::OutputOwnerSource::Test,
                    { kE },
                    30ms),
                std::memory_order_release);
        });
        scheduleDuringB.join();

        const bool leakedBeforeBWasEstablished =
            resumedA || scheduleAccepted.load(std::memory_order_acquire) ||
            sinkCalls.load(std::memory_order_acquire) != 0;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            releaseBlockedRead = true;
        }
        stateCv.notify_all();
        prepareB.join();
        if (leakedBeforeBWasEstablished) {
            scheduler.CancelAllAndJoin();
            return Fail("stacked transition cleared B's in-progress pause");
        }

        if (scheduler.SynchronizeRuntime({ kGeneration + 1, true }) ||
            scheduler.ScheduleTimedHold(
                "stacked-old-generation",
                OW::OutputOwnerSource::Test,
                { kE },
                20ms)) {
            scheduler.CancelAllAndJoin();
            return Fail("transition B was not rebased on generation B");
        }

        runtime.generation.store(kGeneration + 2, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime({ kGeneration + 2, true }) ||
            !scheduler.ScheduleTimedHold(
                "stacked-new-generation",
                OW::OutputOwnerSource::Test,
                { kQ },
                20ms) ||
            !WaitForSchedulerIdle(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("stacked transition did not resume on transaction B's result");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerCallbackReentryFailsClosed()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<bool> callbackReturned{ false };
        std::atomic<bool> nestedCancelResult{ true };
        OW::OutputScheduler* schedulerPtr = nullptr;
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });
        schedulerPtr = &scheduler;

        OW::OutputActionPlan plan{};
        plan.key = "reentry";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        OW::OutputActionStep release{};
        release.afterStart = 10ms;
        release.release = { kE };
        release.callback = [&](std::uint64_t) {
            nestedCancelResult.store(
                schedulerPtr->Cancel("reentry"),
                std::memory_order_release);
            schedulerPtr->CancelAllAndJoin();
            callbackReturned.store(true, std::memory_order_release);
        };
        release.complete = true;
        plan.steps.push_back(std::move(release));

        if (!scheduler.Schedule(std::move(plan)) ||
            !WaitForSchedulerIdle(scheduler) ||
            !callbackReturned.load(std::memory_order_acquire) ||
            nestedCancelResult.load(std::memory_order_acquire)) {
            return Fail("callback scheduler reentry blocked or mutated active operation");
        }
        scheduler.CancelAllAndJoin();
        if (scheduler.WorkerCount() != 0)
            return Fail("reentry test worker did not join");
        return 0;
    }
}

int main()
{
    if (const int status = TestSharedControlLastOwnerReleases(); status != 0)
        return status;
    if (const int status = TestIndependentControlsAndModifier(); status != 0)
        return status;
    if (const int status = TestIdempotentAcquireRelease(); status != 0)
        return status;
    if (const int status = TestCancelOwnerDoesNotAffectOthers(); status != 0)
        return status;
    if (const int status = TestStaleGenerationRejected(); status != 0)
        return status;
    if (const int status = TestCancelAllClearsModel(); status != 0)
        return status;
    if (const int status = TestInvalidControlAndTokenRejected(); status != 0)
        return status;
    if (const int status = TestSchedulerOverlappingSameKey(); status != 0)
        return status;
    if (const int status = TestSchedulerOverlappingDifferentKeys(); status != 0)
        return status;
    if (const int status = TestSchedulerModifierAndUsageReport(); status != 0)
        return status;
    if (const int status = TestSchedulerReleaseOnlyReportPreservesHeldUsage(); status != 0)
        return status;
    if (const int status = TestSchedulerRejectedDownDoesNotEmitUp(); status != 0)
        return status;
    if (const int status = TestSchedulerScheduleGenerationToctouRejected(); status != 0)
        return status;
    if (const int status = TestSchedulerCancelNoLateCallbackAndRestart(); status != 0)
        return status;
    if (const int status = TestSchedulerGenerationAndGateCancellation(); status != 0)
        return status;
    if (const int status = TestSchedulerRetriesFailedReleaseWithoutForgettingLease(); status != 0)
        return status;
    if (const int status = TestSchedulerCancelFailureSuppressesNormalCallbacks(); status != 0)
        return status;
    if (const int status = TestSchedulerCancelAllPersistentFailureIsFiniteAndBlocksNewDown(); status != 0)
        return status;
    if (const int status = TestSchedulerGenerationChangeDropsResidualWithoutNewBackendUp(); status != 0)
        return status;
    if (const int status = TestSchedulerWorkerStartAndSubmissionAreOneOperation(); status != 0)
        return status;
    if (const int status = TestSchedulerRuntimeTransitionPauseRequiresNewGeneration(); status != 0)
        return status;
    if (const int status = TestSchedulerTransitionPauseWinsBlockedScheduleStateRead(); status != 0)
        return status;
    if (const int status = TestSchedulerBackToBackTransitionSetupCannotLosePause(); status != 0)
        return status;
    if (const int status = TestSchedulerCallbackReentryFailsClosed(); status != 0)
        return status;

    std::puts("[InputOwnershipSelfTest] PASS");
    return 0;
}
