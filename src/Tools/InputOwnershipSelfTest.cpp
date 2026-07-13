#include "Game/OutputOwnership.hpp"
#include "Game/OutputScheduler.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <set>
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

    // Models a transport-backed aggregate sink that can apply one edge and
    // then report failure before the remaining edges reach hardware.
    struct PartialAcceptanceSink
    {
        int Apply(const OW::OutputAggregateUpdate& update)
        {
            std::lock_guard<std::mutex> lock(mutex);
            updates.push_back(update);
            ++calls;

            if (update.IsReleaseOnly()) {
                ++releaseAttempts;
                if (rejectReleases)
                    return -1;
            }

            for (const OW::OutputChange& change : update.changes) {
                ApplyChange(change);
                if (failNextAcquire &&
                    change.transition == OW::OutputTransition::Press) {
                    failNextAcquire = false;
                    ++partialFailures;
                    return -1;
                }
            }
            return 0;
        }

        void ArmPartialAcquireFailure()
        {
            std::lock_guard<std::mutex> lock(mutex);
            failNextAcquire = true;
        }

        void SetRejectReleases(bool reject)
        {
            std::lock_guard<std::mutex> lock(mutex);
            rejectReleases = reject;
        }

        bool Empty() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return physicallyHeld.empty();
        }

        std::size_t HeldCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return physicallyHeld.size();
        }

        int CallCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return calls;
        }

        int PartialFailureCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return partialFailures;
        }

        int ReleaseAttemptCount() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return releaseAttempts;
        }

        std::vector<OW::OutputAggregateUpdate> Snapshot() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return updates;
        }

    private:
        void ApplyChange(const OW::OutputChange& change)
        {
            if (change.transition == OW::OutputTransition::Press)
                physicallyHeld.insert(change.control);
            else if (change.transition == OW::OutputTransition::Release)
                physicallyHeld.erase(change.control);
        }

        mutable std::mutex mutex;
        std::set<OW::OutputControl> physicallyHeld;
        std::vector<OW::OutputAggregateUpdate> updates;
        bool failNextAcquire = false;
        bool rejectReleases = false;
        int calls = 0;
        int partialFailures = 0;
        int releaseAttempts = 0;
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

    int CountTransitions(
        const std::vector<OW::OutputAggregateUpdate>& updates,
        const OW::OutputControl& control,
        OW::OutputTransition transition)
    {
        return static_cast<int>(std::count_if(
            updates.begin(),
            updates.end(),
            [&](const OW::OutputAggregateUpdate& update) {
                return HasTransition(update, control, transition);
            }));
    }

    bool IsSchedulerOutputZero(const OW::OutputScheduler& scheduler)
    {
        return !scheduler.IsControlHeld(kLeftMouse) &&
            !scheduler.IsControlHeld(kRightMouse) &&
            !scheduler.IsControlHeld(kE) &&
            !scheduler.IsControlHeld(kQ) &&
            !scheduler.IsControlHeld(kLeftShift) &&
            scheduler.ActiveActionCount() == 0 &&
            scheduler.PendingDeadlineCount() == 0 &&
            scheduler.WorkerCount() == 0;
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

    int TestSchedulerRejectedDownEmitsConservativeUp()
    {
        std::mutex updatesMutex;
        std::vector<OW::OutputAggregateUpdate> updates;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> cleanupReason{ -1 };
        OW::OutputScheduler scheduler(
            [&updatesMutex, &updates](const OW::OutputAggregateUpdate& update) {
                std::lock_guard<std::mutex> lock(updatesMutex);
                updates.push_back(update);
                return update.IsReleaseOnly() ? 0 : -1;
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
        std::lock_guard<std::mutex> lock(updatesMutex);
        if (updates.size() != 2 ||
            !HasTransition(updates.front(), kE, OW::OutputTransition::Press) ||
            !HasTransition(updates.back(), kE, OW::OutputTransition::Release) ||
            cleanupReason.load(std::memory_order_acquire) !=
                static_cast<int>(OW::OutputActionCancelReason::OutputFailure) ||
            !IsSchedulerOutputZero(scheduler)) {
            return Fail("rejected down did not emit a conservative safety up");
        }
        scheduler.CancelAllAndJoin();
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

    int TestSchedulerManualExpectedGenerationCannotRebindAfterTransition()
    {
        SinkRecorder recorder;
        AtomicRuntimeState runtime;
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime.Snapshot(); });

        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !scheduler.SetManualControlsForGeneration(
                "expected-generation-sequence",
                OW::OutputOwnerSource::Sequence,
                { kRightMouse },
                kGeneration)) {
            return Fail("expected-generation manual setup failed");
        }

        // Disconnect closes the process-scoped session first. Cleanup may use
        // the old generation for safety ups, but no new down may self-sync.
        runtime.outputGateOpen.store(false, std::memory_order_release);
        if (scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !IsSchedulerOutputZero(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("closed output session did not cancel manual ownership");
        }

        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        if (scheduler.SynchronizeRuntime(runtime.Snapshot())) {
            scheduler.CancelAllAndJoin();
            return Fail("closed output session reopened during generation advance");
        }
        const std::size_t beforeStaleAttempt = recorder.Snapshot().size();
        if (scheduler.SetManualControlsForGeneration(
                "expected-generation-sequence",
                OW::OutputOwnerSource::Sequence,
                { kRightMouse },
                kGeneration) ||
            recorder.Snapshot().size() != beforeStaleAttempt ||
            scheduler.IsControlHeld(kRightMouse)) {
            return Fail("stale manual owner rebound itself to the new generation");
        }

        runtime.outputGateOpen.store(true, std::memory_order_release);
        if (scheduler.SetManualControlsForGeneration(
                "expected-generation-sequence",
                OW::OutputOwnerSource::Sequence,
                { kRightMouse },
                kGeneration) ||
            recorder.Snapshot().size() != beforeStaleAttempt) {
            scheduler.CancelAllAndJoin();
            return Fail("reopened session accepted an old-generation producer");
        }

        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !scheduler.SetManualControlsForGeneration(
                "expected-generation-sequence",
                OW::OutputOwnerSource::Sequence,
                { kRightMouse },
                kGeneration + 1)) {
            return Fail("new-generation manual owner did not rearm explicitly");
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

    int TestSchedulerManualOwnersShareAggregateEdges()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControl(
                "manual-aim",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "manual-aim",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "manual-trigger",
                OW::OutputOwnerSource::Trigger,
                kLeftMouse,
                true) ||
            !scheduler.IsControlHeld(kLeftMouse) ||
            scheduler.WorkerCount() != 0) {
            return Fail("manual shared owners did not acquire idempotently without a worker");
        }

        scheduler.CancelSource(OW::OutputOwnerSource::GlobalAim);
        if (scheduler.IsActive("manual-aim") ||
            !scheduler.IsActive("manual-trigger") ||
            !scheduler.IsControlHeld(kLeftMouse) ||
            scheduler.WorkerCount() != 0) {
            return Fail("source cancel removed the other shared manual owner");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::Trigger);
        if (scheduler.IsControlHeld(kLeftMouse))
            return Fail("held-control query remained true after the last owner release");

        int presses = 0;
        int releases = 0;
        for (const auto& update : recorder.Snapshot()) {
            presses += HasTransition(
                update, kLeftMouse, OW::OutputTransition::Press) ? 1 : 0;
            releases += HasTransition(
                update, kLeftMouse, OW::OutputTransition::Release) ? 1 : 0;
        }
        if (presses != 1 || releases != 1 ||
            scheduler.ActiveActionCount() != 0 || scheduler.WorkerCount() != 0) {
            return Fail("manual shared owners emitted an early or duplicate edge");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerManualMultiControlReports()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControls(
                "manual-mouse",
                OW::OutputOwnerSource::GameAction,
                { kRightMouse, kLeftMouse, kLeftMouse })) {
            return Fail("manual multi-button owner was rejected");
        }
        const auto mouseDown = recorder.Snapshot();
        if (mouseDown.size() != 1 ||
            !HasTransition(
                mouseDown.front(), kLeftMouse, OW::OutputTransition::Press) ||
            !HasTransition(
                mouseDown.front(), kRightMouse, OW::OutputTransition::Press) ||
            scheduler.WorkerCount() != 0) {
            return Fail("manual LMB+RMB did not produce one aggregate update");
        }
        if (!scheduler.Cancel("manual-mouse"))
            return Fail("manual multi-button owner could not be cancelled");

        if (!scheduler.SetManualControls(
                "manual-keyboard",
                OW::OutputOwnerSource::GameAction,
                { kE, kLeftShift }) ||
            !scheduler.SetManualControl(
                "manual-keyboard",
                OW::OutputOwnerSource::GameAction,
                kLeftShift,
                false)) {
            return Fail("manual Shift+E update failed");
        }
        const auto updates = recorder.Snapshot();
        if (updates.size() < 4 ||
            updates[2].keyboardModifierMask != 0x02 ||
            updates[2].keyboardUsages != std::vector<std::uint8_t>{ 0x08 } ||
            updates[3].keyboardModifierMask != 0 ||
            updates[3].keyboardUsages != std::vector<std::uint8_t>{ 0x08 } ||
            !HasTransition(
                updates[3], kLeftShift, OW::OutputTransition::Release)) {
            return Fail("manual modifier release did not preserve held usage report");
        }

        scheduler.CancelAllAndJoin();
        const auto finalUpdates = recorder.Snapshot();
        if (finalUpdates.empty() ||
            finalUpdates.back().keyboardModifierMask != 0 ||
            !finalUpdates.back().keyboardUsages.empty() ||
            !HasTransition(
                finalUpdates.back(), kE, OW::OutputTransition::Release) ||
            scheduler.ActiveActionCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0 ||
            scheduler.WorkerCount() != 0) {
            return Fail("manual CancelAll did not emit a full zero report");
        }
        return 0;
    }

    int TestSchedulerManualFailedReleaseRetainsLease()
    {
        SinkRecorder recorder;
        std::atomic<bool> rejectRelease{ false };
        std::atomic<bool> rejectNormal{ false };
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                if (update.IsReleaseOnly() &&
                    rejectRelease.load(std::memory_order_acquire)) {
                    return -1;
                }
                if (!update.IsReleaseOnly() &&
                    rejectNormal.load(std::memory_order_acquire)) {
                    return -1;
                }
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControls(
                "manual-release-failure",
                OW::OutputOwnerSource::Sequence,
                { kLeftMouse, kRightMouse })) {
            return Fail("manual failed-release setup was rejected");
        }
        rejectRelease.store(true, std::memory_order_release);
        if (scheduler.SetManualControl(
                "manual-release-failure",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                false) ||
            scheduler.SetManualControl(
                "manual-release-failure",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true) ||
            scheduler.WorkerCount() != 0) {
            return Fail("failed manual release was committed or started a worker");
        }

        rejectRelease.store(false, std::memory_order_release);
        if (!scheduler.SetManualControl(
                "manual-release-failure",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                false) ||
            !scheduler.Cancel("manual-release-failure")) {
            return Fail("manual residual release could not be retried and cancelled");
        }

        int leftReleases = 0;
        int rightReleases = 0;
        for (const auto& update : recorder.Snapshot()) {
            leftReleases += HasTransition(
                update, kLeftMouse, OW::OutputTransition::Release) ? 1 : 0;
            rightReleases += HasTransition(
                update, kRightMouse, OW::OutputTransition::Release) ? 1 : 0;
        }
        if (leftReleases != 1 || rightReleases != 1 ||
            scheduler.ActiveActionCount() != 0 || scheduler.WorkerCount() != 0) {
            return Fail("manual failed release forgot or duplicated a lease");
        }

        const std::size_t beforeNormalFailure = recorder.Snapshot().size();
        if (!scheduler.SetManualControl(
                "manual-normal-failure",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                true)) {
            return Fail("manual normal-failure setup did not press");
        }
        rejectNormal.store(true, std::memory_order_release);
        if (scheduler.SetManualControl(
                "manual-normal-failure",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true)) {
            return Fail("failed manual down was reported as accepted");
        }
        rejectNormal.store(false, std::memory_order_release);
        const auto normalFailureUpdates = recorder.Snapshot();
        if (normalFailureUpdates.size() != beforeNormalFailure + 2 ||
            !HasTransition(
                normalFailureUpdates[beforeNormalFailure],
                kLeftMouse,
                OW::OutputTransition::Press) ||
            !HasTransition(
                normalFailureUpdates.back(),
                kLeftMouse,
                OW::OutputTransition::Release) ||
            !HasTransition(
                normalFailureUpdates.back(),
                kRightMouse,
                OW::OutputTransition::Release) ||
            scheduler.Cancel("manual-normal-failure") ||
            scheduler.IsControlHeld(kLeftMouse) ||
            scheduler.IsControlHeld(kRightMouse)) {
            return Fail("rejected manual down did not conservatively release its owner");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerManualSafetyReleaseAndGenerationBoundary()
    {
        SinkRecorder recorder;
        AtomicRuntimeState runtime;
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime.Snapshot(); });

        if (!scheduler.SetManualControl(
                "manual-gate",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "manual-gate-key",
                OW::OutputOwnerSource::GameAction,
                kQ,
                true)) {
            return Fail("manual gate setup did not press");
        }
        runtime.outputGateOpen.store(false, std::memory_order_release);
        if (!scheduler.SetManualControl(
                "manual-gate",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                false) ||
            !scheduler.Cancel("manual-gate-key") ||
            scheduler.SetManualControl(
                "manual-closed-down",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true)) {
            return Fail("closed gate blocked safety up or allowed a new down");
        }

        runtime.outputGateOpen.store(true, std::memory_order_release);
        if (!scheduler.SetManualControl(
                "manual-generation",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true)) {
            return Fail("manual generation setup did not press");
        }
        const std::size_t beforeGenerationChange = recorder.Snapshot().size();
        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            recorder.Snapshot().size() != beforeGenerationChange ||
            scheduler.IsActive("manual-generation")) {
            return Fail("generation change emitted stale release or retained old key");
        }
        if (!scheduler.SetManualControl(
                "manual-generation",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true) ||
            !scheduler.Cancel("manual-generation")) {
            return Fail("manual key could not be safely reused in new generation");
        }
        const auto updates = recorder.Snapshot();
        if (updates.size() < beforeGenerationChange + 2 ||
            updates[beforeGenerationChange].backendGeneration != kGeneration + 1 ||
            updates.back().backendGeneration != kGeneration + 1 ||
            scheduler.WorkerCount() != 0) {
            return Fail("reused manual key dispatched outside the new generation");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestSchedulerCancelSourcePreservesOtherSourcesAndJoinsWorker()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControl(
                "source-global",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "source-trigger",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true) ||
            !scheduler.ScheduleTimedHold(
                "source-hero",
                OW::OutputOwnerSource::HeroTimedAction,
                { kE },
                2s)) {
            return Fail("source-cancel setup failed");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::GlobalAim);
        if (scheduler.IsActive("source-global") ||
            !scheduler.IsActive("source-trigger") ||
            !scheduler.IsActive("source-hero") ||
            scheduler.WorkerCount() != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("source cancel affected unrelated timed/manual owners");
        }

        scheduler.CancelSource(OW::OutputOwnerSource::HeroTimedAction);
        if (scheduler.IsActive("source-hero") ||
            !scheduler.IsActive("source-trigger") ||
            scheduler.WorkerCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0) {
            scheduler.CancelAllAndJoin();
            return Fail("source cancel did not synchronously join the idle worker");
        }

        if (!scheduler.ScheduleTimedHold(
                "source-completed",
                OW::OutputOwnerSource::HeroTimedAction,
                { kQ },
                15ms) ||
            !WaitForCondition([&]() {
                return !scheduler.IsActive("source-completed") &&
                    scheduler.PendingDeadlineCount() == 0;
            }) ||
            scheduler.WorkerCount() != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("completed source did not leave the expected idle worker seam");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::HeroTimedAction);
        if (!scheduler.IsActive("source-trigger") ||
            scheduler.WorkerCount() != 0) {
            scheduler.CancelAllAndJoin();
            return Fail("empty source cancel did not join a completed worker");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::Trigger);
        if (scheduler.ActiveActionCount() != 0 || scheduler.WorkerCount() != 0) {
            return Fail("source cancel did not clear final manual owner");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestBatch8SequenceCancelPreservesGlobalAimMouse()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControl(
                "batch8-global-lmb",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "batch8-sequence-rmb",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true) ||
            !scheduler.Cancel("batch8-sequence-rmb")) {
            scheduler.CancelAllAndJoin();
            return Fail("batch8 aim/sequence setup or sequence cancel failed");
        }

        const auto afterSequence = recorder.Snapshot();
        if (!scheduler.IsActive("batch8-global-lmb") ||
            scheduler.IsActive("batch8-sequence-rmb") ||
            !scheduler.IsControlHeld(kLeftMouse) ||
            scheduler.IsControlHeld(kRightMouse) ||
            CountTransitions(
                afterSequence,
                kLeftMouse,
                OW::OutputTransition::Release) != 0 ||
            CountTransitions(
                afterSequence,
                kRightMouse,
                OW::OutputTransition::Release) != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("sequence cancel released or lost GlobalAim LMB ownership");
        }

        scheduler.CancelSource(OW::OutputOwnerSource::GlobalAim);
        const auto finalUpdates = recorder.Snapshot();
        if (!IsSchedulerOutputZero(scheduler) ||
            CountTransitions(
                finalUpdates,
                kLeftMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                finalUpdates,
                kLeftMouse,
                OW::OutputTransition::Release) != 1 ||
            CountTransitions(
                finalUpdates,
                kRightMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                finalUpdates,
                kRightMouse,
                OW::OutputTransition::Release) != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("aim/sequence owners did not finish with one isolated edge pair");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestBatch8SequenceAndTimedHeroShareRightMouse()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        // Sequence ends first: the timed Hero owner must keep RMB down.
        if (!scheduler.SetManualControl(
                "batch8-sequence-first",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true) ||
            !scheduler.ScheduleTimedHold(
                "batch8-hero-last",
                OW::OutputOwnerSource::HeroTimedAction,
                { kRightMouse },
                45ms) ||
            !scheduler.Cancel("batch8-sequence-first") ||
            !scheduler.IsControlHeld(kRightMouse) ||
            CountTransitions(
                recorder.Snapshot(),
                kRightMouse,
                OW::OutputTransition::Release) != 0 ||
            !WaitForCondition([&]() {
                return !scheduler.IsActive("batch8-hero-last") &&
                    scheduler.PendingDeadlineCount() == 0;
            }) ||
            scheduler.IsControlHeld(kRightMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("sequence-first overlap released RMB before the timed Hero owner");
        }
        scheduler.CancelAllAndJoin();

        // Timed Hero ends first: the manual Sequence owner must keep RMB down.
        if (!scheduler.SetManualControl(
                "batch8-sequence-last",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true) ||
            !scheduler.ScheduleTimedHold(
                "batch8-hero-first",
                OW::OutputOwnerSource::HeroTimedAction,
                { kRightMouse },
                20ms) ||
            !WaitForCondition([&]() {
                return !scheduler.IsActive("batch8-hero-first") &&
                    scheduler.PendingDeadlineCount() == 0;
            }) ||
            !scheduler.IsControlHeld(kRightMouse) ||
            CountTransitions(
                recorder.Snapshot(),
                kRightMouse,
                OW::OutputTransition::Release) != 1 ||
            !scheduler.Cancel("batch8-sequence-last") ||
            scheduler.IsControlHeld(kRightMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("timed-Hero-first overlap released RMB before Sequence ended");
        }

        const auto updates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (CountTransitions(
                updates,
                kRightMouse,
                OW::OutputTransition::Press) != 2 ||
            CountTransitions(
                updates,
                kRightMouse,
                OW::OutputTransition::Release) != 2 ||
            !IsSchedulerOutputZero(scheduler)) {
            return Fail("shared Sequence/Hero RMB emitted duplicate or missing edges");
        }
        return 0;
    }

    int TestBatch8ThreeSourceAggregateReport()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControl(
                "batch8-three-global",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.SetManualControl(
                "batch8-three-trigger",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true) ||
            !scheduler.ScheduleTimedHold(
                "batch8-three-hero",
                OW::OutputOwnerSource::HeroTimedAction,
                { kLeftMouse, kLeftShift, kE },
                25ms) ||
            !WaitForCondition([&]() {
                return !scheduler.IsActive("batch8-three-hero") &&
                    scheduler.PendingDeadlineCount() == 0;
            })) {
            scheduler.CancelAllAndJoin();
            return Fail("three-source aggregate scenario did not complete Hero hold");
        }

        const auto afterHero = recorder.Snapshot();
        bool sawFullKeyboardDown = false;
        bool sawKeyboardZeroRelease = false;
        for (const auto& update : afterHero) {
            if (update.keyboardModifierMask == 0x02 &&
                update.keyboardUsages == std::vector<std::uint8_t>{ 0x08 } &&
                HasTransition(update, kLeftShift, OW::OutputTransition::Press) &&
                HasTransition(update, kE, OW::OutputTransition::Press)) {
                sawFullKeyboardDown = true;
            }
            if (update.keyboardModifierMask == 0 &&
                update.keyboardUsages.empty() &&
                HasTransition(update, kLeftShift, OW::OutputTransition::Release) &&
                HasTransition(update, kE, OW::OutputTransition::Release)) {
                sawKeyboardZeroRelease = true;
            }
        }
        if (!sawFullKeyboardDown || !sawKeyboardZeroRelease ||
            !scheduler.IsControlHeld(kLeftMouse) ||
            !scheduler.IsControlHeld(kRightMouse) ||
            scheduler.IsControlHeld(kLeftShift) || scheduler.IsControlHeld(kE) ||
            CountTransitions(
                afterHero,
                kLeftMouse,
                OW::OutputTransition::Release) != 0) {
            scheduler.CancelAllAndJoin();
            return Fail("Hero completion corrupted aggregate mouse or keyboard report");
        }

        scheduler.CancelSource(OW::OutputOwnerSource::Trigger);
        if (!scheduler.IsControlHeld(kLeftMouse) ||
            scheduler.IsControlHeld(kRightMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("Trigger release changed GlobalAim ownership");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::GlobalAim);
        const auto finalUpdates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (!IsSchedulerOutputZero(scheduler) || finalUpdates.empty() ||
            finalUpdates.back().keyboardModifierMask != 0 ||
            !finalUpdates.back().keyboardUsages.empty() ||
            CountTransitions(
                finalUpdates,
                kLeftMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                finalUpdates,
                kLeftMouse,
                OW::OutputTransition::Release) != 1 ||
            CountTransitions(
                finalUpdates,
                kRightMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                finalUpdates,
                kRightMouse,
                OW::OutputTransition::Release) != 1) {
            return Fail("three-source scenario did not end in an aggregate zero report");
        }
        return 0;
    }

    int TestBatch8SourceCancelPermanentReleaseFailureIsFinite()
    {
        SinkRecorder recorder;
        std::atomic<bool> rejectRelease{ true };
        std::atomic<int> releaseAttempts{ 0 };
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                if (update.IsReleaseOnly()) {
                    ++releaseAttempts;
                    if (rejectRelease.load(std::memory_order_acquire))
                        return -1;
                }
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.ScheduleTimedHold(
                "batch8-source-release-failure",
                OW::OutputOwnerSource::HeroTimedAction,
                { kE },
                2s)) {
            return Fail("permanent source-release failure setup did not schedule");
        }

        const auto cancelStarted = std::chrono::steady_clock::now();
        scheduler.CancelSource(OW::OutputOwnerSource::HeroTimedAction);
        const auto cancelElapsed = std::chrono::steady_clock::now() - cancelStarted;
        if (cancelElapsed > 250ms || scheduler.ActiveActionCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0 || scheduler.WorkerCount() != 0 ||
            !scheduler.IsControlHeld(kE) ||
            releaseAttempts.load(std::memory_order_acquire) != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("failed source release did not return finite with an idle residual lease");
        }

        std::this_thread::sleep_for(50ms);
        if (releaseAttempts.load(std::memory_order_acquire) != 1 ||
            scheduler.PendingDeadlineCount() != 0 || scheduler.WorkerCount() != 0) {
            scheduler.CancelAllAndJoin();
            return Fail("failed source release left a 10ms retry loop running");
        }

        rejectRelease.store(false, std::memory_order_release);
        scheduler.CancelSource(OW::OutputOwnerSource::HeroTimedAction);
        const auto updates = recorder.Snapshot();
        if (!IsSchedulerOutputZero(scheduler) ||
            releaseAttempts.load(std::memory_order_acquire) != 2 ||
            CountTransitions(
                updates,
                kE,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                updates,
                kE,
                OW::OutputTransition::Release) != 1) {
            scheduler.CancelAllAndJoin();
            return Fail("explicit source retry did not clear the retained release-only lease");
        }
        scheduler.CancelAllAndJoin();
        return 0;
    }

    int TestBatch8CombinedManualReleaseBranches()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> fallbackCalls{ 0 };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });
        const auto fallback = [&fallbackCalls](std::uint64_t generation) {
            if (generation != kGeneration)
                return false;
            ++fallbackCalls;
            return true;
        };

        // (a) This owner holds the control: emit only its modeled release.
        if (!scheduler.SetManualControl(
                "batch8-combined-self",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                true) ||
            !scheduler.ReleaseManualControlOrExecuteIfUnowned(
                "batch8-combined-self",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                fallback) ||
            fallbackCalls.load(std::memory_order_acquire) != 0 ||
            scheduler.IsControlHeld(kLeftMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("combined release called fallback instead of releasing its own lease");
        }

        // (b) Another owner holds the control: do nothing and preserve it.
        if (!scheduler.SetManualControl(
                "batch8-combined-other",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduler.ReleaseManualControlOrExecuteIfUnowned(
                "batch8-combined-missing",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                fallback) ||
            fallbackCalls.load(std::memory_order_acquire) != 0 ||
            !scheduler.IsActive("batch8-combined-other") ||
            !scheduler.IsControlHeld(kLeftMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("combined release invoked fallback or released another owner");
        }
        scheduler.CancelSource(OW::OutputOwnerSource::GlobalAim);

        // (c) No owner holds the control: execute the fallback exactly once.
        const std::size_t beforeFallback = recorder.Snapshot().size();
        if (!scheduler.ReleaseManualControlOrExecuteIfUnowned(
                "batch8-combined-unowned",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                fallback) ||
            fallbackCalls.load(std::memory_order_acquire) != 1 ||
            recorder.Snapshot().size() != beforeFallback ||
            scheduler.IsControlHeld(kLeftMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("combined unowned branch did not execute exactly one fallback");
        }

        const auto updates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (CountTransitions(
                updates,
                kLeftMouse,
                OW::OutputTransition::Press) != 2 ||
            CountTransitions(
                updates,
                kLeftMouse,
                OW::OutputTransition::Release) != 2 ||
            !IsSchedulerOutputZero(scheduler)) {
            return Fail("combined release branches produced an unexpected modeled edge");
        }
        return 0;
    }

    int TestBatch8CombinedFallbackSerializesConcurrentAcquire()
    {
        SinkRecorder recorder;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SynchronizeRuntime(runtime))
            return Fail("combined fallback scheduler did not synchronize runtime");

        std::mutex barrierMutex;
        std::condition_variable barrierCv;
        bool fallbackEntered = false;
        bool releaseFallback = false;
        std::atomic<int> fallbackCalls{ 0 };
        std::atomic<bool> combinedResult{ false };
        std::atomic<bool> acquireReturned{ false };
        std::atomic<bool> acquireResult{ false };

        std::thread fallbackThread([&]() {
            combinedResult.store(
                scheduler.ReleaseManualControlOrExecuteIfUnowned(
                    "batch8-combined-barrier",
                    OW::OutputOwnerSource::Sequence,
                    kLeftMouse,
                    [&](std::uint64_t generation) {
                        ++fallbackCalls;
                        std::unique_lock<std::mutex> lock(barrierMutex);
                        fallbackEntered = generation == kGeneration;
                        barrierCv.notify_all();
                        barrierCv.wait(lock, [&]() { return releaseFallback; });
                        return generation == kGeneration;
                    }),
                std::memory_order_release);
        });

        {
            std::unique_lock<std::mutex> lock(barrierMutex);
            if (!barrierCv.wait_for(lock, 500ms, [&]() { return fallbackEntered; })) {
                releaseFallback = true;
                barrierCv.notify_all();
                lock.unlock();
                fallbackThread.join();
                scheduler.CancelAllAndJoin();
                return Fail("combined fallback did not enter the concurrency barrier");
            }
        }

        std::thread acquireThread([&]() {
            acquireResult.store(
                scheduler.SetManualControl(
                    "batch8-combined-acquire",
                    OW::OutputOwnerSource::GlobalAim,
                    kLeftMouse,
                    true),
                std::memory_order_release);
            acquireReturned.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(25ms);
        const bool acquireWaited =
            !acquireReturned.load(std::memory_order_acquire) &&
            recorder.Snapshot().empty();

        {
            std::lock_guard<std::mutex> lock(barrierMutex);
            releaseFallback = true;
        }
        barrierCv.notify_all();
        fallbackThread.join();
        acquireThread.join();

        const auto updates = recorder.Snapshot();
        const bool valid = acquireWaited &&
            combinedResult.load(std::memory_order_acquire) &&
            acquireReturned.load(std::memory_order_acquire) &&
            acquireResult.load(std::memory_order_acquire) &&
            fallbackCalls.load(std::memory_order_acquire) == 1 &&
            scheduler.IsControlHeld(kLeftMouse) &&
            CountTransitions(
                updates,
                kLeftMouse,
                OW::OutputTransition::Press) == 1;
        scheduler.CancelAllAndJoin();
        if (!valid || !IsSchedulerOutputZero(scheduler)) {
            return Fail("combined fallback did not serialize a concurrent acquire");
        }
        return 0;
    }

    int TestBatch8CombinedReleaseRetriesResidualWithoutFallback()
    {
        SinkRecorder recorder;
        std::atomic<bool> rejectRelease{ true };
        std::atomic<int> releaseAttempts{ 0 };
        std::atomic<int> fallbackCalls{ 0 };
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&](const OW::OutputAggregateUpdate& update) {
                if (update.IsReleaseOnly()) {
                    ++releaseAttempts;
                    if (rejectRelease.load(std::memory_order_acquire))
                        return -1;
                }
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime; });
        const auto fallback = [&fallbackCalls](std::uint64_t) {
            ++fallbackCalls;
            return true;
        };

        if (!scheduler.SetManualControl(
                "batch8-combined-residual",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                true) ||
            scheduler.ReleaseManualControlOrExecuteIfUnowned(
                "batch8-combined-residual",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                fallback) ||
            fallbackCalls.load(std::memory_order_acquire) != 0 ||
            releaseAttempts.load(std::memory_order_acquire) != 1 ||
            !scheduler.IsControlHeld(kRightMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("combined release failure did not retain a callback-free residual lease");
        }

        rejectRelease.store(false, std::memory_order_release);
        if (!scheduler.ReleaseManualControlOrExecuteIfUnowned(
                "batch8-combined-residual",
                OW::OutputOwnerSource::Sequence,
                kRightMouse,
                fallback) ||
            fallbackCalls.load(std::memory_order_acquire) != 0 ||
            releaseAttempts.load(std::memory_order_acquire) != 2 ||
            scheduler.IsControlHeld(kRightMouse)) {
            scheduler.CancelAllAndJoin();
            return Fail("combined residual retry forced an unowned callback or kept the lease");
        }

        const auto updates = recorder.Snapshot();
        scheduler.CancelAllAndJoin();
        if (CountTransitions(
                updates,
                kRightMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                updates,
                kRightMouse,
                OW::OutputTransition::Release) != 1 ||
            !IsSchedulerOutputZero(scheduler)) {
            return Fail("combined residual retry did not emit exactly one accepted up");
        }
        return 0;
    }

    int TestBatch8RuntimeBoundariesClearOwnershipWithoutLateEvents()
    {
        SinkRecorder recorder;
        AtomicRuntimeState runtime;
        std::atomic<int> lateCallbacks{ 0 };
        OW::OutputScheduler scheduler(
            [&recorder](const OW::OutputAggregateUpdate& update) {
                return recorder.Apply(update);
            },
            [&runtime]() { return runtime.Snapshot(); });

        const auto scheduleTracked = [&](
            const char* key,
            OW::OutputControl control) {
            OW::OutputActionPlan plan{};
            plan.key = key;
            plan.ownerSource = OW::OutputOwnerSource::HeroTimedAction;
            plan.acquire = { control };
            OW::OutputActionStep release{};
            release.afterStart = 70ms;
            release.release = { control };
            release.callback = [&lateCallbacks](std::uint64_t) {
                ++lateCallbacks;
            };
            release.complete = true;
            plan.steps.push_back(std::move(release));
            return scheduler.Schedule(std::move(plan));
        };

        // Menu/gate close must synchronously cancel both manual and timed work.
        if (!scheduler.SetManualControl(
                "batch8-gate-manual",
                OW::OutputOwnerSource::GlobalAim,
                kLeftMouse,
                true) ||
            !scheduleTracked("batch8-gate-timed", kE)) {
            scheduler.CancelAllAndJoin();
            return Fail("gate-close cleanup setup failed");
        }
        runtime.outputGateOpen.store(false, std::memory_order_release);
        if (scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !IsSchedulerOutputZero(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("gate close left output ownership or worker state behind");
        }
        const std::size_t afterGateClose = recorder.Snapshot().size();
        std::this_thread::sleep_for(85ms);
        if (lateCallbacks.load(std::memory_order_acquire) != 0 ||
            recorder.Snapshot().size() != afterGateClose) {
            return Fail("gate-close cleanup allowed a late event");
        }

        runtime.outputGateOpen.store(true, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !scheduler.SetManualControl(
                "batch8-generation-manual",
                OW::OutputOwnerSource::Trigger,
                kRightMouse,
                true) ||
            !scheduleTracked("batch8-generation-timed", kQ)) {
            scheduler.CancelAllAndJoin();
            return Fail("generation-switch cleanup setup failed");
        }
        runtime.generation.store(kGeneration + 1, std::memory_order_release);
        if (!scheduler.SynchronizeRuntime(runtime.Snapshot()) ||
            !IsSchedulerOutputZero(scheduler)) {
            scheduler.CancelAllAndJoin();
            return Fail("generation switch retained stale output ownership");
        }
        const std::size_t afterGenerationSwitch = recorder.Snapshot().size();
        std::this_thread::sleep_for(85ms);
        if (lateCallbacks.load(std::memory_order_acquire) != 0 ||
            recorder.Snapshot().size() != afterGenerationSwitch) {
            return Fail("generation switch allowed a stale deadline event");
        }

        if (!scheduler.SetManualControl(
                "batch8-cancel-manual",
                OW::OutputOwnerSource::Sequence,
                kLeftMouse,
                true) ||
            !scheduleTracked("batch8-cancel-timed", kLeftShift)) {
            scheduler.CancelAllAndJoin();
            return Fail("CancelAll cleanup setup failed");
        }
        scheduler.CancelAllAndJoin();
        if (!IsSchedulerOutputZero(scheduler))
            return Fail("CancelAll did not synchronously clear ownership and worker state");
        const std::size_t afterCancelAll = recorder.Snapshot().size();
        std::this_thread::sleep_for(85ms);
        if (lateCallbacks.load(std::memory_order_acquire) != 0 ||
            recorder.Snapshot().size() != afterCancelAll) {
            return Fail("CancelAll allowed a late deadline event");
        }

        // Destruction is the scheduler's final shutdown seam and must also join.
        SinkRecorder shutdownRecorder;
        std::atomic<int> shutdownLateCallbacks{ 0 };
        {
            OW::OutputScheduler shutdownScheduler(
                [&shutdownRecorder](const OW::OutputAggregateUpdate& update) {
                    return shutdownRecorder.Apply(update);
                },
                [&runtime]() { return runtime.Snapshot(); });
            OW::OutputActionPlan plan{};
            plan.key = "batch8-destructor-timed";
            plan.ownerSource = OW::OutputOwnerSource::HeroTimedAction;
            plan.acquire = { kE };
            OW::OutputActionStep release{};
            release.afterStart = 70ms;
            release.release = { kE };
            release.callback = [&shutdownLateCallbacks](std::uint64_t) {
                ++shutdownLateCallbacks;
            };
            release.complete = true;
            plan.steps.push_back(std::move(release));
            if (!shutdownScheduler.SetManualControl(
                    "batch8-destructor-manual",
                    OW::OutputOwnerSource::GlobalAim,
                    kRightMouse,
                    true) ||
                !shutdownScheduler.Schedule(std::move(plan))) {
                shutdownScheduler.CancelAllAndJoin();
                return Fail("scheduler shutdown cleanup setup failed");
            }
        }
        const std::size_t afterShutdown = shutdownRecorder.Snapshot().size();
        std::this_thread::sleep_for(85ms);
        const auto shutdownUpdates = shutdownRecorder.Snapshot();
        if (shutdownLateCallbacks.load(std::memory_order_acquire) != 0 ||
            shutdownUpdates.size() != afterShutdown ||
            CountTransitions(
                shutdownUpdates,
                kRightMouse,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                shutdownUpdates,
                kRightMouse,
                OW::OutputTransition::Release) != 1 ||
            CountTransitions(
                shutdownUpdates,
                kE,
                OW::OutputTransition::Press) != 1 ||
            CountTransitions(
                shutdownUpdates,
                kE,
                OW::OutputTransition::Release) != 1) {
            return Fail("scheduler shutdown leaked ownership or a late event");
        }
        return 0;
    }

    int TestSchedulerInitialPartialAcquireIsConservativelyReleased()
    {
        OW::OutputRuntimeState runtime{ kGeneration, true };

        {
            PartialAcceptanceSink sink;
            std::atomic<int> cleanupCalls{ 0 };
            std::atomic<int> cleanupReason{ -1 };
            OW::OutputScheduler scheduler(
                [&sink](const OW::OutputAggregateUpdate& update) {
                    return sink.Apply(update);
                },
                [&runtime]() { return runtime; });

            OW::OutputActionPlan plan{};
            plan.key = "partial-initial";
            plan.ownerSource = OW::OutputOwnerSource::Test;
            plan.acquire = { kLeftMouse, kRightMouse };
            plan.cancelCleanup = [&cleanupCalls, &cleanupReason](
                OW::OutputActionCancelReason reason,
                std::uint64_t) {
                ++cleanupCalls;
                cleanupReason.store(
                    static_cast<int>(reason), std::memory_order_release);
            };
            OW::OutputActionStep release{};
            release.afterStart = 1s;
            release.release = { kLeftMouse, kRightMouse };
            release.complete = true;
            plan.steps.push_back(std::move(release));

            sink.ArmPartialAcquireFailure();
            const bool scheduled = scheduler.Schedule(std::move(plan));
            const auto updates = sink.Snapshot();
            const bool valid = !scheduled && sink.Empty() &&
                sink.PartialFailureCount() == 1 &&
                sink.ReleaseAttemptCount() == 1 &&
                cleanupCalls.load(std::memory_order_acquire) == 1 &&
                cleanupReason.load(std::memory_order_acquire) ==
                    static_cast<int>(OW::OutputActionCancelReason::OutputFailure) &&
                CountTransitions(
                    updates,
                    kLeftMouse,
                    OW::OutputTransition::Release) == 1 &&
                CountTransitions(
                    updates,
                    kRightMouse,
                    OW::OutputTransition::Release) == 1 &&
                IsSchedulerOutputZero(scheduler);
            scheduler.CancelAllAndJoin();
            if (!valid || !IsSchedulerOutputZero(scheduler))
                return Fail("partial initial acquire was not conservatively released");
        }

        // A failed safety-up is retained as one finite global residual. It
        // must not create a 10 ms retry loop; an explicit later operation can
        // retry it after the transport recovers.
        {
            PartialAcceptanceSink sink;
            sink.SetRejectReleases(true);
            OW::OutputScheduler scheduler(
                [&sink](const OW::OutputAggregateUpdate& update) {
                    return sink.Apply(update);
                },
                [&runtime]() { return runtime; });

            OW::OutputActionPlan plan{};
            plan.key = "partial-initial-residual";
            plan.ownerSource = OW::OutputOwnerSource::Test;
            plan.acquire = { kLeftMouse, kRightMouse };
            OW::OutputActionStep release{};
            release.afterStart = 1s;
            release.release = { kLeftMouse, kRightMouse };
            release.complete = true;
            plan.steps.push_back(std::move(release));

            sink.ArmPartialAcquireFailure();
            if (scheduler.Schedule(std::move(plan)) ||
                sink.PartialFailureCount() != 1 ||
                sink.ReleaseAttemptCount() != 1 ||
                sink.HeldCount() != 1 ||
                !scheduler.IsControlHeld(kLeftMouse) ||
                !scheduler.IsControlHeld(kRightMouse) ||
                scheduler.ActiveActionCount() != 0 ||
                scheduler.PendingDeadlineCount() != 0 ||
                scheduler.WorkerCount() != 0) {
                scheduler.CancelAllAndJoin();
                return Fail("partial initial cleanup failure did not retain a finite residual");
            }

            const int callsAfterFailure = sink.CallCount();
            std::this_thread::sleep_for(40ms);
            if (sink.CallCount() != callsAfterFailure) {
                scheduler.CancelAllAndJoin();
                return Fail("partial initial cleanup failure entered a hot retry loop");
            }

            sink.SetRejectReleases(false);
            if (!scheduler.SynchronizeRuntime(runtime) || !sink.Empty() ||
                !IsSchedulerOutputZero(scheduler)) {
                scheduler.CancelAllAndJoin();
                return Fail("partial initial residual did not clear after explicit recovery");
            }
            scheduler.CancelAllAndJoin();
        }
        return 0;
    }

    int TestSchedulerWorkerPartialAcquireIsConservativelyReleased()
    {
        PartialAcceptanceSink sink;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        std::atomic<int> cleanupCalls{ 0 };
        std::atomic<int> cleanupReason{ -1 };
        std::atomic<int> stepCallbacks{ 0 };
        OW::OutputScheduler scheduler(
            [&sink](const OW::OutputAggregateUpdate& update) {
                return sink.Apply(update);
            },
            [&runtime]() { return runtime; });

        OW::OutputActionPlan plan{};
        plan.key = "partial-worker";
        plan.ownerSource = OW::OutputOwnerSource::Test;
        plan.acquire = { kE };
        plan.cancelCleanup = [&cleanupCalls, &cleanupReason](
            OW::OutputActionCancelReason reason,
            std::uint64_t) {
            ++cleanupCalls;
            cleanupReason.store(
                static_cast<int>(reason), std::memory_order_release);
        };
        OW::OutputActionStep mixedStep{};
        mixedStep.afterStart = 40ms;
        mixedStep.release = { kE };
        mixedStep.acquire = { kLeftMouse, kRightMouse };
        mixedStep.callback = [&stepCallbacks](std::uint64_t) {
            ++stepCallbacks;
        };
        mixedStep.complete = true;
        plan.steps.push_back(std::move(mixedStep));

        if (!scheduler.Schedule(std::move(plan)))
            return Fail("partial worker acquire setup did not schedule");
        sink.ArmPartialAcquireFailure();
        if (!WaitForCondition([&]() {
                return cleanupCalls.load(std::memory_order_acquire) == 1 &&
                    scheduler.ActiveActionCount() == 0 &&
                    scheduler.PendingDeadlineCount() == 0;
            })) {
            scheduler.CancelAllAndJoin();
            return Fail("partial worker acquire did not finish conservative cleanup");
        }

        const auto updates = sink.Snapshot();
        const bool cleanupReportValid = !updates.empty() &&
            HasTransition(
                updates.back(), kE, OW::OutputTransition::Release) &&
            HasTransition(
                updates.back(), kLeftMouse, OW::OutputTransition::Release) &&
            HasTransition(
                updates.back(), kRightMouse, OW::OutputTransition::Release);
        const bool valid = sink.Empty() &&
            sink.PartialFailureCount() == 1 &&
            sink.ReleaseAttemptCount() == 1 &&
            cleanupReportValid &&
            stepCallbacks.load(std::memory_order_acquire) == 0 &&
            cleanupReason.load(std::memory_order_acquire) ==
                static_cast<int>(OW::OutputActionCancelReason::OutputFailure);
        scheduler.CancelAllAndJoin();
        if (!valid || !IsSchedulerOutputZero(scheduler))
            return Fail("partial worker acquire leaked an accepted edge or callback");
        return 0;
    }

    int TestSchedulerManualPartialAcquireIsConservativelyReleased()
    {
        PartialAcceptanceSink sink;
        OW::OutputRuntimeState runtime{ kGeneration, true };
        OW::OutputScheduler scheduler(
            [&sink](const OW::OutputAggregateUpdate& update) {
                return sink.Apply(update);
            },
            [&runtime]() { return runtime; });

        if (!scheduler.SetManualControls(
                "partial-manual",
                OW::OutputOwnerSource::Test,
                { kE })) {
            return Fail("partial manual acquire setup failed");
        }

        sink.ArmPartialAcquireFailure();
        const bool updated = scheduler.SetManualControls(
            "partial-manual",
            OW::OutputOwnerSource::Test,
            { kLeftMouse, kRightMouse });
        const auto updates = sink.Snapshot();
        const bool cleanupReportValid = !updates.empty() &&
            HasTransition(
                updates.back(), kE, OW::OutputTransition::Release) &&
            HasTransition(
                updates.back(), kLeftMouse, OW::OutputTransition::Release) &&
            HasTransition(
                updates.back(), kRightMouse, OW::OutputTransition::Release);
        const bool valid = !updated && sink.Empty() &&
            sink.PartialFailureCount() == 1 &&
            sink.ReleaseAttemptCount() == 1 &&
            cleanupReportValid &&
            !scheduler.IsActive("partial-manual") &&
            IsSchedulerOutputZero(scheduler);
        scheduler.CancelAllAndJoin();
        if (!valid || !IsSchedulerOutputZero(scheduler))
            return Fail("partial manual acquire did not release old and possible controls");
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
    if (const int status = TestSchedulerRejectedDownEmitsConservativeUp(); status != 0)
        return status;
    if (const int status = TestSchedulerScheduleGenerationToctouRejected(); status != 0)
        return status;
    if (const int status = TestSchedulerManualExpectedGenerationCannotRebindAfterTransition(); status != 0)
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
    if (const int status = TestSchedulerManualOwnersShareAggregateEdges(); status != 0)
        return status;
    if (const int status = TestSchedulerManualMultiControlReports(); status != 0)
        return status;
    if (const int status = TestSchedulerManualFailedReleaseRetainsLease(); status != 0)
        return status;
    if (const int status = TestSchedulerManualSafetyReleaseAndGenerationBoundary(); status != 0)
        return status;
    if (const int status = TestSchedulerCancelSourcePreservesOtherSourcesAndJoinsWorker(); status != 0)
        return status;
    if (const int status = TestBatch8SequenceCancelPreservesGlobalAimMouse(); status != 0)
        return status;
    if (const int status = TestBatch8SequenceAndTimedHeroShareRightMouse(); status != 0)
        return status;
    if (const int status = TestBatch8ThreeSourceAggregateReport(); status != 0)
        return status;
    if (const int status = TestBatch8SourceCancelPermanentReleaseFailureIsFinite(); status != 0)
        return status;
    if (const int status = TestBatch8CombinedManualReleaseBranches(); status != 0)
        return status;
    if (const int status = TestBatch8CombinedFallbackSerializesConcurrentAcquire(); status != 0)
        return status;
    if (const int status = TestBatch8CombinedReleaseRetriesResidualWithoutFallback(); status != 0)
        return status;
    if (const int status = TestBatch8RuntimeBoundariesClearOwnershipWithoutLateEvents(); status != 0)
        return status;
    if (const int status = TestSchedulerInitialPartialAcquireIsConservativelyReleased(); status != 0)
        return status;
    if (const int status = TestSchedulerWorkerPartialAcquireIsConservativelyReleased(); status != 0)
        return status;
    if (const int status = TestSchedulerManualPartialAcquireIsConservativelyReleased(); status != 0)
        return status;
    if (const int status = TestSchedulerCallbackReentryFailsClosed(); status != 0)
        return status;

    std::puts("[InputOwnershipSelfTest] PASS");
    return 0;
}
