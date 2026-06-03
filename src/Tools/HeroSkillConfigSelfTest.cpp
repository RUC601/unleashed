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
    const OW::HeroSkillDefinition* anaSleepDart = nullptr;
    const OW::HeroSkillDefinition* roadhogChainHook = nullptr;
    for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
        if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ASHE) &&
            std::string(definition.skillId ? definition.skillId : "") == "fire-pattern") {
            asheFirePattern = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ANA) &&
                   std::string(definition.skillId ? definition.skillId : "") == "sleep-dart") {
            anaSleepDart = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ROADHOG) &&
                   std::string(definition.skillId ? definition.skillId : "") == "chain-hook") {
            roadhogChainHook = &definition;
        }
    }

    if (!asheFirePattern)
        return Fail();
    if (!anaSleepDart || !roadhogChainHook)
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

    const auto verifyProjectileAimSkill =
        [](const OW::HeroSkillDefinition& definition,
           float projectileSpeed,
           float projectileRadius,
           float preFireDelayMs) {
            if (!OW::HasHeroSkillControl(definition, OW::HeroSkillControls::TrackingOverlay))
                return false;
            if (!OW::HasHeroSkillControl(definition, OW::HeroSkillControls::Prediction))
                return false;

            const OW::Config::HeroSkillSettings skillSettings = definition.defaultSettings;
            if (skillSettings.key != OW::HeroSkillHotkey::Mouse4)
                return false;
            if (skillSettings.skillKey != OW::HeroSkillHotkey::LeftShift)
                return false;
            if (skillSettings.tracking.aimBehavior != OW::Config::kAimBehaviorFlick)
                return false;
            if (skillSettings.tracking.bone != OW::Config::kAimBoneChest)
                return false;
            if (!NearlyEqual(skillSettings.tracking.fov, OW::Config::kDefaultFovDeg))
                return false;
            if (!NearlyEqual(skillSettings.tracking.hitbox, OW::Config::kDefaultHitboxScalePercent))
                return false;
            if (!NearlyEqual(skillSettings.projectileSpeed, projectileSpeed))
                return false;
            if (!NearlyEqual(skillSettings.projectileRadius, projectileRadius))
                return false;
            if (!NearlyEqual(skillSettings.preFireDelayMs, preFireDelayMs))
                return false;
            return true;
        };

    if (!verifyProjectileAimSkill(*anaSleepDart, 60.0f, 0.2f, 320.0f))
        return Fail();
    if (!verifyProjectileAimSkill(*roadhogChainHook, 62.0f, 0.5f, 100.0f))
        return Fail();
    if (!NearlyEqual(roadhogChainHook->defaultSettings.enemyHealthThreshold, 100.0f))
        return Fail();

    return EXIT_SUCCESS;
}
