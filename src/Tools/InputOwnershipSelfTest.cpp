#include "Game/OutputOwnership.hpp"

#include <cstdio>
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

    std::puts("[InputOwnershipSelfTest] PASS");
    return 0;
}
