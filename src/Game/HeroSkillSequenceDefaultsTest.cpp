#include "Game/HeroSkills.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

struct ExpectedStep {
    int buttonMask;
    int durationMs;
};

bool NearlyEqual(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) <= 0.0001f;
}

} // namespace

int main()
{
    const OW::Config::HeroSkillSettings settings = OW::MakeAsheComboSequenceDefaults();
    constexpr std::array<ExpectedStep, 28> expected{ {
        { 0x01,  49 },
        { 0x02, 140 },
        { 0x03,  70 },
        { 0x02,  70 },
        { 0x00, 183 },
        { 0x01,  49 },
        { 0x00, 183 },

        { 0x01,  49 },
        { 0x00, 183 },
        { 0x01,  49 },
        { 0x00,  45 },
        { 0x02, 140 },
        { 0x03,  70 },
        { 0x02,  70 },
        { 0x00, 183 },

        { 0x01,  49 },
        { 0x00, 183 },
        { 0x01,  49 },
        { 0x00, 183 },

        { 0x01,  49 },
        { 0x00,  47 },
        { 0x02, 140 },
        { 0x03,  70 },
        { 0x02,  70 },
        { 0x00, 183 },
        { 0x01,  49 },
        { 0x00, 183 },
        { 0x01,  49 },
    } };

    if (settings.sequenceSteps.size() != expected.size()) {
        std::fprintf(stderr,
            "Expected %zu Ashe fire-pattern steps, got %zu\n",
            expected.size(),
            settings.sequenceSteps.size());
        return 1;
    }

    int totalDurationMs = 0;
    for (size_t index = 0; index < expected.size(); ++index) {
        const OW::Config::HeroSkillSequenceStep& step = settings.sequenceSteps[index];
        totalDurationMs += step.durationMs;

        if (step.buttonMask != expected[index].buttonMask ||
            step.durationMs != expected[index].durationMs) {
            std::fprintf(stderr,
                "Step %zu mismatch: mask=0x%02X duration=%d\n",
                index + 1,
                step.buttonMask,
                step.durationMs);
            return 1;
        }

        if (!NearlyEqual(step.speedScale, 1.0f) || step.jitterMs != 0) {
            std::fprintf(stderr,
                "Step %zu defaults mismatch: speedScale=%.3f jitterMs=%d\n",
                index + 1,
                step.speedScale,
                step.jitterMs);
            return 1;
        }
    }

    if (!settings.ammoGuard || settings.ammoGuardReserve != 1) {
        std::fprintf(stderr,
            "Expected Ashe ammo guard reserve 1, got enabled=%d reserve=%d\n",
            settings.ammoGuard ? 1 : 0,
            settings.ammoGuardReserve);
        return 1;
    }

    if (!settings.cooldownGuard) {
        std::fprintf(stderr, "Expected Ashe cooldown guard to default on\n");
        return 1;
    }

    if (totalDurationMs != 2837) {
        std::fprintf(stderr,
            "Expected Ashe locked sequence duration 2837 ms, got %d ms\n",
            totalDurationMs);
        return 1;
    }

    const std::vector<OW::Config::HeroSkillSequenceStep> bruteSteps =
        OW::MakeAsheBruteLeftSequenceSteps();
    if (bruteSteps.size() > OW::Config::kMaxHeroSkillSequenceSteps) {
        std::fprintf(stderr,
            "Expected Ashe brute-left test to fit within %d steps, got %zu\n",
            OW::Config::kMaxHeroSkillSequenceSteps,
            bruteSteps.size());
        return 1;
    }

    int bruteLeftEdges = 0;
    int previousMask = 0;
    for (const OW::Config::HeroSkillSequenceStep& step : bruteSteps) {
        if ((previousMask & 0x01) == 0 && (step.buttonMask & 0x01) != 0)
            ++bruteLeftEdges;
        previousMask = step.buttonMask;
    }

    if (bruteLeftEdges < 6) {
        std::fprintf(stderr,
            "Expected Ashe brute-left test to provide at least 6 left down edges, got %d\n",
            bruteLeftEdges);
        return 1;
    }

    const std::vector<OW::Config::HeroSkillSequenceStep> spacedSteps =
        OW::MakeAsheSpacedLeftSequenceSteps();
    int spacedTotalDurationMs = 0;
    for (const OW::Config::HeroSkillSequenceStep& step : spacedSteps)
        spacedTotalDurationMs += step.durationMs;

    if (spacedSteps.size() != 10 || spacedTotalDurationMs != 518) {
        std::fprintf(stderr,
            "Expected Ashe spaced-left test to use 10 steps / 518 ms, got %zu / %d ms\n",
            spacedSteps.size(),
            spacedTotalDurationMs);
        return 1;
    }

    const OW::Config::HeroSkillSettings frejaSettings = OW::MakeFrejaSequenceSetDefaults();
    constexpr std::array<ExpectedStep, 6> expectedFreja{ {
        { 0x01,  45 },
        { 0x00, 110 },
        { 0x01,  45 },
        { 0x00, 110 },
        { 0x02, 180 },
        { 0x00, 180 },
    } };

    if (OW::CountHeroSkillDefinitions(static_cast<uint64_t>(OW::eHero::HERO_FREJA)) <= 0) {
        std::fprintf(stderr, "Expected Freja to expose a hero skill sequence definition\n");
        return 1;
    }

    if (frejaSettings.sequenceSteps.size() != expectedFreja.size()) {
        std::fprintf(stderr,
            "Expected %zu Freja sequence-set steps, got %zu\n",
            expectedFreja.size(),
            frejaSettings.sequenceSteps.size());
        return 1;
    }

    for (size_t index = 0; index < expectedFreja.size(); ++index) {
        const OW::Config::HeroSkillSequenceStep& step = frejaSettings.sequenceSteps[index];
        if (step.buttonMask != expectedFreja[index].buttonMask ||
            step.durationMs != expectedFreja[index].durationMs ||
            !NearlyEqual(step.speedScale, 1.0f) ||
            step.jitterMs != 0) {
            std::fprintf(stderr,
                "Freja step %zu mismatch: mask=0x%02X duration=%d scale=%.3f jitter=%d\n",
                index + 1,
                step.buttonMask,
                step.durationMs,
                step.speedScale,
                step.jitterMs);
            return 1;
        }
    }

    const OW::Config::HeroSkillSettings zaryaProbeSettings =
        OW::MakeZaryaReloadAmmoProbeDefaults();
    if (OW::CountHeroSkillDefinitions(static_cast<uint64_t>(OW::eHero::HERO_ZARYA)) < 2) {
        std::fprintf(stderr, "Expected Zarya to expose propel and reload-ammo probe definitions\n");
        return 1;
    }

    if (zaryaProbeSettings.enabled || zaryaProbeSettings.cooldownGuard ||
        zaryaProbeSettings.ammoGuard || zaryaProbeSettings.ammoGuardReserve != 10) {
        std::fprintf(stderr,
            "Unexpected Zarya reload-ammo probe defaults: enabled=%d cooldownGuard=%d ammoGuard=%d reserve=%d\n",
            zaryaProbeSettings.enabled ? 1 : 0,
            zaryaProbeSettings.cooldownGuard ? 1 : 0,
            zaryaProbeSettings.ammoGuard ? 1 : 0,
            zaryaProbeSettings.ammoGuardReserve);
        return 1;
    }

    return 0;
}
