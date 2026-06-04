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
    const OW::HeroSkillDefinition* anaBioticGrenade = nullptr;
    const OW::HeroSkillDefinition* roadhogChainHook = nullptr;
    const OW::HeroSkillDefinition* tracerPulseBomb = nullptr;
    const OW::HeroSkillDefinition* tracerRecall = nullptr;
    const OW::HeroSkillDefinition* reaperWraithForm = nullptr;
    const OW::HeroSkillDefinition* zenyattaTranscendence = nullptr;
    const OW::HeroSkillDefinition* soldierHelixRockets = nullptr;
    const OW::HeroSkillDefinition* echoStickyBombs = nullptr;
    const OW::HeroSkillDefinition* brigitteWhipShot = nullptr;
    const OW::HeroSkillDefinition* sigmaAccretion = nullptr;
    const OW::HeroSkillDefinition* zaryaPropelJump = nullptr;
    const OW::HeroSkillDefinition* zaryaLowAmmoRightClick = nullptr;
    const OW::HeroSkillDefinition* genjiDashCombo = nullptr;
    const OW::HeroSkillDefinition* tracerAutoMelee = nullptr;
    size_t autoMeleeDefinitions = 0;
    for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
        const std::string skillId(definition.skillId ? definition.skillId : "");
        if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ASHE) &&
            skillId == "fire-pattern") {
            asheFirePattern = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ANA) &&
                   skillId == "sleep-dart") {
            anaSleepDart = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ANA) &&
                   skillId == "biotic-grenade") {
            anaBioticGrenade = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ROADHOG) &&
                   skillId == "chain-hook") {
            roadhogChainHook = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_TRACER) &&
                   skillId == "pulse-bomb") {
            tracerPulseBomb = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_TRACER) &&
                   skillId == "recall") {
            tracerRecall = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_REAPER) &&
                   skillId == "wraith-form") {
            reaperWraithForm = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ZENYATTA) &&
                   skillId == "transcendence") {
            zenyattaTranscendence = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_SOLDIER76) &&
                   skillId == "helix-rockets") {
            soldierHelixRockets = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ECHO) &&
                   skillId == "sticky-bombs") {
            echoStickyBombs = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_BRIGITTE) &&
                   skillId == "whip-shot") {
            brigitteWhipShot = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_SIGMA) &&
                   skillId == "accretion") {
            sigmaAccretion = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ZARYA) &&
                   skillId == "propel-jump") {
            zaryaPropelJump = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_ZARYA) &&
                   skillId == "reload-ammo-probe") {
            zaryaLowAmmoRightClick = &definition;
        } else if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_GENJI) &&
                   skillId == "dash-combo") {
            genjiDashCombo = &definition;
        }

        if (skillId == "auto-melee") {
            ++autoMeleeDefinitions;
            if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_TRACER))
                tracerAutoMelee = &definition;
        }
    }

    if (!asheFirePattern)
        return Fail();
    if (!anaSleepDart || !anaBioticGrenade || !roadhogChainHook || !tracerPulseBomb ||
        !soldierHelixRockets || !echoStickyBombs ||
        !brigitteWhipShot || !sigmaAccretion)
        return Fail();
    if (!tracerRecall || !reaperWraithForm || !zenyattaTranscendence ||
        !zaryaPropelJump || !zaryaLowAmmoRightClick || !genjiDashCombo)
        return Fail();
    if (!tracerAutoMelee)
        return Fail();
    if (autoMeleeDefinitions != OW::AutoMeleeHeroDefinitionCount())
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
           float preFireDelayMs,
           int skillKey,
           bool projectileGravity = false) {
            if (!OW::HasHeroSkillControl(definition, OW::HeroSkillControls::TrackingOverlay))
                return false;
            if (!OW::HasHeroSkillControl(definition, OW::HeroSkillControls::Prediction))
                return false;

            const OW::Config::HeroSkillSettings skillSettings = definition.defaultSettings;
            if (skillSettings.key != OW::HeroSkillHotkey::Mouse4)
                return false;
            if (skillSettings.skillKey != skillKey)
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
            if (skillSettings.projectileGravity != projectileGravity)
                return false;
            if (!NearlyEqual(skillSettings.preFireDelayMs, preFireDelayMs))
                return false;
            return true;
        };

    if (anaSleepDart->inputAction != OW::HeroSkillInputAction::Ability1)
        return Fail();
    if (!verifyProjectileAimSkill(*anaSleepDart, 60.0f, 0.2f, 320.0f, OW::HeroSkillHotkey::LeftShift))
        return Fail();
    if (!NearlyEqual(anaSleepDart->defaultSettings.enemyHealthThreshold, 100.0f))
        return Fail();
    if (anaBioticGrenade->inputAction != OW::HeroSkillInputAction::Ability2)
        return Fail();
    if (anaBioticGrenade->defaultSettings.key != OW::HeroSkillHotkey::EKey)
        return Fail();
    if (!verifyProjectileAimSkill(*roadhogChainHook, 62.0f, 0.5f, 100.0f, OW::HeroSkillHotkey::LeftShift))
        return Fail();
    if (!NearlyEqual(roadhogChainHook->defaultSettings.enemyHealthThreshold, 100.0f))
        return Fail();
    if (!verifyProjectileAimSkill(*tracerPulseBomb, 15.0f, 0.0f, 0.0f, OW::HeroSkillHotkey::QKey, true))
        return Fail();
    if (!OW::HasHeroSkillControl(*tracerPulseBomb, OW::HeroSkillControls::Radius))
        return Fail();
    if (!NearlyEqual(tracerPulseBomb->defaultSettings.radius, 3.0f))
        return Fail();
    if (!verifyProjectileAimSkill(*soldierHelixRockets, 50.0f, 0.32f, 0.0f, OW::HeroSkillHotkey::RightMouse))
        return Fail();
    if (!verifyProjectileAimSkill(*echoStickyBombs, 50.0f, 0.2f, 0.0f, OW::HeroSkillHotkey::EKey))
        return Fail();
    if (!verifyProjectileAimSkill(*brigitteWhipShot, 80.0f, 0.3f, 0.0f, OW::HeroSkillHotkey::LeftShift))
        return Fail();
    if (!verifyProjectileAimSkill(*sigmaAccretion, 37.5f, 0.5f, 0.0f, OW::HeroSkillHotkey::EKey, true))
        return Fail();

    if (!OW::HasHeroSkillControl(*tracerRecall, OW::HeroSkillControls::HealthAbsolute))
        return Fail();
    if (tracerRecall->defaultSettings.key != OW::HeroSkillHotkey::EKey ||
        tracerRecall->defaultSettings.skillKey != OW::HeroSkillHotkey::EKey) {
        return Fail();
    }
    if (!NearlyEqual(tracerRecall->defaultSettings.healthThreshold, 30.0f))
        return Fail();
    if (!NearlyEqual(tracerRecall->defaultSettings.cooldown, 12.0f))
        return Fail();

    if (!OW::HasHeroSkillControl(*reaperWraithForm, OW::HeroSkillControls::HealthAbsolute))
        return Fail();
    if (!OW::HasHeroSkillControl(*reaperWraithForm, OW::HeroSkillControls::Distance))
        return Fail();
    if (reaperWraithForm->defaultSettings.skillKey != OW::HeroSkillHotkey::LeftShift)
        return Fail();
    if (!NearlyEqual(reaperWraithForm->defaultSettings.healthThreshold, 30.0f))
        return Fail();
    if (!NearlyEqual(reaperWraithForm->defaultSettings.distance, 30.0f))
        return Fail();

    if (!OW::HasHeroSkillControl(*zenyattaTranscendence, OW::HeroSkillControls::Distance))
        return Fail();
    if (zenyattaTranscendence->defaultSettings.skillKey != OW::HeroSkillHotkey::QKey)
        return Fail();
    if (!NearlyEqual(zenyattaTranscendence->defaultSettings.distance, 15.0f))
        return Fail();

    if (zaryaPropelJump->defaultSettings.key != OW::HeroSkillHotkey::Mouse4 ||
        zaryaPropelJump->defaultSettings.skillKey != OW::HeroSkillHotkey::RightMouse) {
        return Fail();
    }
    if (zaryaLowAmmoRightClick->defaultSettings.key != OW::HeroSkillHotkey::RightMouse)
        return Fail();

    if (!OW::HasHeroSkillControl(*genjiDashCombo, OW::HeroSkillControls::ComboAction))
        return Fail();
    if (!OW::HasHeroSkillControl(*genjiDashCombo, OW::HeroSkillControls::TrackingOverlay))
        return Fail();
    if (!OW::HasHeroSkillControl(*genjiDashCombo, OW::HeroSkillControls::Prediction))
        return Fail();
    if (genjiDashCombo->defaultSettings.key != OW::HeroSkillHotkey::Mouse4)
        return Fail();
    if (!NearlyEqual(genjiDashCombo->defaultSettings.distance, 20.0f))
        return Fail();
    if (!NearlyEqual(genjiDashCombo->defaultSettings.enemyHealthThreshold, 100.0f))
        return Fail();
    if (genjiDashCombo->defaultSettings.tracking.aimBehavior != OW::Config::kAimBehaviorFlick)
        return Fail();
    if (genjiDashCombo->defaultSettings.tracking.bone != OW::Config::kAimBoneHead)
        return Fail();
    if (!NearlyEqual(genjiDashCombo->defaultSettings.projectileSpeed, 75.0f))
        return Fail();
    if (!NearlyEqual(genjiDashCombo->defaultSettings.projectileRadius, 0.125f))
        return Fail();

    if (!OW::HasHeroSkillControl(*tracerAutoMelee, OW::HeroSkillControls::EnemyHealthAbsolute))
        return Fail();
    if (!OW::HasHeroSkillControl(*tracerAutoMelee, OW::HeroSkillControls::Distance))
        return Fail();
    if (!OW::HasHeroSkillControl(*tracerAutoMelee, OW::HeroSkillControls::Bone))
        return Fail();
    if (!OW::HasHeroSkillControl(*tracerAutoMelee, OW::HeroSkillControls::Hitbox))
        return Fail();
    const OW::Config::HeroSkillSettings autoMelee = tracerAutoMelee->defaultSettings;
    if (autoMelee.key != OW::HeroSkillHotkey::VKey ||
        autoMelee.skillKey != OW::HeroSkillHotkey::VKey) {
        return Fail();
    }
    if (!NearlyEqual(autoMelee.enemyHealthThreshold, 40.0f))
        return Fail();
    if (!NearlyEqual(autoMelee.distance, 3.0f))
        return Fail();
    if (!NearlyEqual(autoMelee.cooldown, 0.55f))
        return Fail();
    if (autoMelee.tracking.bone != OW::Config::kAimBoneClosest)
        return Fail();
    if (!NearlyEqual(autoMelee.tracking.hitbox, OW::Config::kMaxHitboxScalePercent))
        return Fail();

    return EXIT_SUCCESS;
}
