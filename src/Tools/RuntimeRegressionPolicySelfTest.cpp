#include <cmath>
#include <cstdio>

#include "Game/AimSmoothingTiming.hpp"
#include "Game/EntityRosterPolicy.hpp"
#include "Game/HeroProfileSwitch.hpp"

namespace {

bool Expect(bool condition, const char* message)
{
    if (condition)
        return true;
    std::fprintf(stderr, "RuntimeRegressionPolicySelfTest failed: %s\n", message);
    return false;
}

} // namespace

int main()
{
    bool ok = true;

    const uint64_t alpha =
        OW::EntityRosterPolicy::ParticipantRosterKey("PlayerAlpha#1234");
    const uint64_t alphaAgain =
        OW::EntityRosterPolicy::ParticipantRosterKey("PlayerAlpha#1234");
    const uint64_t bravo =
        OW::EntityRosterPolicy::ParticipantRosterKey("PlayerBravo#1234");
    ok &= Expect(alpha != 0 && alpha == alphaAgain,
                 "participant identity must be stable");
    ok &= Expect(alpha != bravo,
                 "different participant identities must not share a roster key");
    ok &= Expect(OW::EntityRosterPolicy::ParticipantRosterKey("x") == 0,
                 "implausibly short participant identities must be rejected");

    using Action = OW::EntityRosterPolicy::SameParticipantObservationAction;
    ok &= Expect(
        OW::EntityRosterPolicy::ResolveSameParticipantObservation(
            true, false, false, true) == Action::ReplaceExisting,
        "fresh respawn actor must replace the dead participant row");
    ok &= Expect(
        OW::EntityRosterPolicy::ResolveSameParticipantObservation(
            true, false, true, false) == Action::KeepExisting,
        "late dead actor must not replace a fresh respawn actor");
    ok &= Expect(
        OW::EntityRosterPolicy::ResolveSameParticipantObservation(
            true, false, true, true) == Action::UseActorKey,
        "simultaneous fresh actors must remain separate");
    ok &= Expect(OW::EntityRosterPolicy::ShouldRenderHeroAvatar(true) &&
                 !OW::EntityRosterPolicy::ShouldRenderHeroAvatar(false),
                 "hero avatars must be enemy-only");

    OW::HeroProfileSwitchDebouncer debouncer(300);
    ok &= Expect(!debouncer.Observe(10, 100, 0, true),
                 "first valid hero sample must start debounce");
    ok &= Expect(!debouncer.Observe(0xBAD, 999, 100, false),
                 "unknown hero samples must never switch profiles");
    ok &= Expect(!debouncer.Observe(10, 100, 150, true),
                 "valid hero must restart debounce after invalid data");
    ok &= Expect(!debouncer.Observe(10, 100, 449, true),
                 "hero must remain stable for the full debounce window");
    const auto firstSwitch = debouncer.Observe(10, 100, 450, true);
    ok &= Expect(firstSwitch && firstSwitch->nextHeroId == 10 &&
                     firstSwitch->previousHeroId == 0,
                 "stable initial hero must load exactly once");
    ok &= Expect(!debouncer.Observe(0xCAFE, 555, 600, false) &&
                     debouncer.ConfirmedHeroId() == 10,
                 "unknown hero data must not replace the confirmed profile");
    ok &= Expect(!debouncer.Observe(20, 200, 1000, true) &&
                     !debouncer.Observe(20, 200, 1299, true),
                 "real hero changes must also debounce");
    const auto secondSwitch = debouncer.Observe(20, 200, 1300, true);
    ok &= Expect(secondSwitch && secondSwitch->previousHeroId == 10 &&
                     secondSwitch->previousLinkBase == 100 &&
                     secondSwitch->nextHeroId == 20,
                 "profile switch must save the last confirmed hero snapshot");

    const float referenceFactor =
        OW::AimSmoothingTiming::NormalizePerTickFactor(0.05f, 1.0f / 144.0f);
    const float oneMillisecondFactor =
        OW::AimSmoothingTiming::NormalizePerTickFactor(0.05f, 0.001f);
    const double referenceResidual = std::pow(1.0 - referenceFactor, 144.0);
    const double fastResidual = std::pow(1.0 - oneMillisecondFactor, 1000.0);
    ok &= Expect(std::fabs(referenceFactor - 0.05f) < 0.000001f,
                 "reference-rate smoothing must preserve configured gain");
    ok &= Expect(oneMillisecondFactor < referenceFactor,
                 "high-rate loops must use a smaller per-tick gain");
    ok &= Expect(std::fabs(referenceResidual - fastResidual) < 0.0001,
                 "smoothing convergence must be tick-rate independent");

    const float compositeReferenceFactor = 0.05f * 0.20f;
    const float normalizedComposite =
        OW::AimSmoothingTiming::NormalizePerTickFactor(
            compositeReferenceFactor,
            0.001f);
    const float normalizedBeforeComposition = oneMillisecondFactor * 0.20f;
    ok &= Expect(
        std::fabs(normalizedComposite - normalizedBeforeComposition) > 0.00001f,
        "method-specific gain must be composed before time normalization");

    return ok ? 0 : 1;
}
