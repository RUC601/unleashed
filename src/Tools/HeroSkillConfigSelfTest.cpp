#include "Game/HeroSkills.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

Memory::~Memory() = default;

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int Fail()
{
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    const OW::HeroSkillDefinition* asheFirePattern = nullptr;
    for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
        if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ASHE) &&
            std::string(definition.skillId ? definition.skillId : "") == "fire-pattern") {
            asheFirePattern = &definition;
            break;
        }
    }

    if (!asheFirePattern)
        return Fail();
    if (!OW::HasHeroSkillControl(*asheFirePattern, OW::HeroSkillControls::SequenceSteps))
        return Fail();
    if (!OW::HasHeroSkillControl(*asheFirePattern, OW::HeroSkillControls::TrackingOverlay))
        return Fail();

    const OW::Config::HeroSkillSettings settings = asheFirePattern->defaultSettings;
    if (settings.tracking.aimBehavior != OW::Config::kAimBehaviorTracking)
        return Fail();
    if (!NearlyEqual(settings.tracking.speedScale, 100.0f))
        return Fail();
    if (!NearlyEqual(settings.tracking.fov, OW::Config::kDefaultFovDeg))
        return Fail();
    if (settings.tracking.bone != OW::Config::kAimBoneHead)
        return Fail();
    if (!NearlyEqual(settings.tracking.hitbox, OW::Config::kDefaultHitboxScalePercent))
        return Fail();

    return EXIT_SUCCESS;
}
