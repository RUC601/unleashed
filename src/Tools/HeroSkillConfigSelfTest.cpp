#include "Game/HeroSkills.hpp"
#include "Game/AbilityIcons.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_set>

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
    std::unordered_set<std::string> heroSlugs;
    size_t catalogSkillSlots = 0;
    for (const OW::HeroAbilityIcons& icons : OW::AllHeroAbilityIcons()) {
        if (!icons.heroSlug || !*icons.heroSlug ||
            !icons.ability1Icon || !*icons.ability1Icon ||
            !icons.ability2Icon || !*icons.ability2Icon ||
            !icons.ultimateIcon || !*icons.ultimateIcon ||
            !heroSlugs.emplace(icons.heroSlug).second) {
            return Fail();
        }
        catalogSkillSlots += 3;
    }
    if (OW::HeroAbilityIconCount() != 52 || catalogSkillSlots != 156)
        return Fail();

    int matchedHotkey = -1;
    int observedVk = 0;
    if (OW::Labels::AimActivationKeyVk(OW::HeroSkillHotkey::Mouse4) != VK_XBUTTON1 ||
        OW::Labels::AimActivationKeyVk(OW::HeroSkillHotkey::Mouse5) != VK_XBUTTON2 ||
        !OW::Labels::TryMatchAimActivationKey(
            OW::HeroSkillHotkey::Mouse4,
            [&](int vk) {
                observedVk = vk;
                return vk == VK_XBUTTON1;
            },
            &matchedHotkey) ||
        observedVk != VK_XBUTTON1 ||
        matchedHotkey != OW::HeroSkillHotkey::Mouse4) {
        return Fail();
    }

    if (OW::Labels::HeroSkillOutputKeyCount() != 12 ||
        !OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::RightMouse) ||
        !OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::LeftMouse) ||
        !OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::EKey) ||
        OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::Mouse4) ||
        OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::Mouse5) ||
        OW::Labels::IsHeroSkillOutputHotkey(OW::HeroSkillHotkey::None) ||
        OW::ResolveHeroSkillOutputHotkey(
            OW::HeroSkillHotkey::Mouse4,
            OW::HeroSkillInputAction::Ability1) != OW::HeroSkillHotkey::LeftShift ||
        OW::ResolveHeroSkillOutputHotkey(
            OW::HeroSkillHotkey::Mouse5,
            OW::HeroSkillInputAction::SecondaryFire) != OW::HeroSkillHotkey::RightMouse) {
        return Fail();
    }
    for (int index = 0; index < OW::Labels::HeroSkillOutputKeyCount(); ++index) {
        const int storedHotkey = OW::Labels::kHeroSkillOutputHotkeys[index];
        if (std::string(OW::Labels::kHeroSkillOutputKeys[index]) !=
            OW::Labels::AimActivationKeyName(storedHotkey)) {
            return Fail();
        }
    }

    using OW::HeroSkillDetail::SequenceWorkerExitReason;
    if (OW::HeroSkillDetail::SequenceWorkerExitRequiresRelease(
            SequenceWorkerExitReason::Running) ||
        OW::HeroSkillDetail::SequenceWorkerExitRequiresRelease(
            SequenceWorkerExitReason::StopRequested) ||
        !OW::HeroSkillDetail::SequenceWorkerExitRequiresRelease(
            SequenceWorkerExitReason::RuntimeChanged) ||
        !OW::HeroSkillDetail::SequenceWorkerExitRequiresRelease(
            SequenceWorkerExitReason::AmmoBudget) ||
        !OW::HeroSkillDetail::SequenceWorkerExitRequiresRelease(
            SequenceWorkerExitReason::OutputFailure)) {
        return Fail();
    }
    bool sequenceRearmBlocked = true;
    if (OW::HeroSkillDetail::AdvanceSequenceRearmGate(
            sequenceRearmBlocked, true) ||
        !sequenceRearmBlocked) {
        return Fail();
    }
    if (OW::HeroSkillDetail::AdvanceSequenceRearmGate(
            sequenceRearmBlocked, false) ||
        sequenceRearmBlocked) {
        return Fail();
    }
    if (!OW::HeroSkillDetail::AdvanceSequenceRearmGate(
            sequenceRearmBlocked, true)) {
        return Fail();
    }
    if (!OW::HeroSkillDetail::SequenceWorkerRuntimeMatches(7, 7, true) ||
        OW::HeroSkillDetail::SequenceWorkerRuntimeMatches(7, 8, true) ||
        OW::HeroSkillDetail::SequenceWorkerRuntimeMatches(7, 7, false) ||
        OW::HeroSkillDetail::SequenceWorkerRuntimeMatches(0, 0, true)) {
        return Fail();
    }
    if (!OW::HeroSkillDetail::SequenceClaimsExecution(
            true, false, OW::ExecutionSource::GlobalAim) ||
        !OW::HeroSkillDetail::SequenceClaimsExecution(
            false, true, OW::ExecutionSource::Trigger) ||
        OW::HeroSkillDetail::SequenceClaimsExecution(
            false, true, OW::ExecutionSource::GlobalAim) ||
        OW::HeroSkillDetail::SequenceClaimsExecution(
            false, false, OW::ExecutionSource::Trigger)) {
        return Fail();
    }
    if (OW::HeroSkillDetail::TrackingBoneMaskForSelection(OW::Config::kAimBoneHead) !=
            OW::kSkeletonBoneHead ||
        OW::HeroSkillDetail::TrackingBoneMaskForSelection(OW::Config::kAimBoneNeck) !=
            OW::kSkeletonBoneNeck ||
        OW::HeroSkillDetail::TrackingBoneMaskForSelection(OW::Config::kAimBoneChest) !=
            OW::kSkeletonBoneChest ||
        OW::HeroSkillDetail::TrackingBoneMaskForSelection(OW::Config::kAimBoneClosest) !=
            (OW::kSkeletonBoneHead | OW::kSkeletonBoneNeck | OW::kSkeletonBoneChest)) {
        return Fail();
    }

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
    const OW::HeroSkillDefinition* emreAutoMelee = nullptr;
    const OW::HeroSkillDefinition* sierraAutoMelee = nullptr;
    const OW::HeroSkillDefinition* mizukiAutoMelee = nullptr;
    const OW::HeroSkillDefinition* shionAutoMelee = nullptr;
    size_t autoMeleeDefinitions = 0;
    size_t aimableSkillDefinitions = 0;
    for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
        const std::string skillId(definition.skillId ? definition.skillId : "");
        const OW::Config::HeroSkillSettings defaults = definition.defaultSettings;
        const bool ownsCompositeOutput =
            OW::HasHeroSkillControl(definition, OW::HeroSkillControls::SequenceSteps) ||
            OW::HasHeroSkillControl(definition, OW::HeroSkillControls::ComboAction);
        if (OW::HasHeroSkillControl(definition, OW::HeroSkillControls::Key) &&
            OW::Labels::AimActivationKeyVk(defaults.key) == 0) {
            std::fprintf(
                stderr,
                "HeroSkillConfigSelfTest invalid activation key: %s key=%d\n",
                skillId.c_str(),
                defaults.key);
            return Fail();
        }
        if (!ownsCompositeOutput) {
            const int expectedOutput = OW::DefaultHeroSkillOutputHotkey(definition.inputAction);
            const int effectiveOutput = defaults.skillKey >= 0 ? defaults.skillKey : defaults.key;
            if (expectedOutput < 0 || effectiveOutput != expectedOutput ||
                !OW::Labels::IsHeroSkillOutputHotkey(effectiveOutput)) {
                std::fprintf(
                    stderr,
                    "HeroSkillConfigSelfTest key mismatch: %s expected=%d actual=%d\n",
                    skillId.c_str(),
                    expectedOutput,
                    effectiveOutput);
                return Fail();
            }
        }

        const bool aimable =
            OW::HasHeroSkillControl(definition, OW::HeroSkillControls::TrackingOverlay) &&
            OW::HasHeroSkillControl(definition, OW::HeroSkillControls::Prediction) &&
            !ownsCompositeOutput;
        if (aimable) {
            ++aimableSkillDefinitions;
            if (!std::isfinite(defaults.projectileSpeed) || defaults.projectileSpeed <= 0.0f ||
                !std::isfinite(defaults.projectileRadius) || defaults.projectileRadius < 0.0f ||
                !std::isfinite(defaults.preFireDelayMs) || defaults.preFireDelayMs < 0.0f ||
                !std::isfinite(defaults.maxAimTimeMs) || defaults.maxAimTimeMs <= 0.0f ||
                defaults.tracking.hitbox <= 0.0f || defaults.distance <= 0.0f) {
                std::fprintf(
                    stderr,
                    "HeroSkillConfigSelfTest incomplete aim contract: %s\n",
                    skillId.c_str());
                return Fail();
            }
        }
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
            if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_EMRE))
                emreAutoMelee = &definition;
            if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_SIERRA))
                sierraAutoMelee = &definition;
            if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_MIZUKI))
                mizukiAutoMelee = &definition;
            if (definition.heroId == static_cast<uint64_t>(OW::eHero::HERO_SHION))
                shionAutoMelee = &definition;
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
    if (!tracerAutoMelee || !emreAutoMelee || !sierraAutoMelee || !mizukiAutoMelee || !shionAutoMelee)
        return Fail();
    if (std::string(OW::GameData::HeroName(static_cast<uint64_t>(OW::eHero::HERO_EMRE))) != "Emre")
        return Fail();
    if (std::string(OW::GameData::HeroName(static_cast<uint64_t>(OW::eHero::HERO_SIERRA))) != "Sierra")
        return Fail();
    if (std::string(OW::GameData::HeroName(static_cast<uint64_t>(OW::eHero::HERO_MIZUKI))) != "Mizuki")
        return Fail();
    if (std::string(OW::GameData::HeroName(static_cast<uint64_t>(OW::eHero::HERO_SHION))) != "Shion")
        return Fail();
    if (autoMeleeDefinitions != OW::AutoMeleeHeroDefinitionCount())
        return Fail();
    if (aimableSkillDefinitions != 7)
        return Fail();
    if (!OW::HasHeroSkillControl(*asheFirePattern, OW::HeroSkillControls::SequenceSteps))
        return Fail();
    if (!OW::HasHeroSkillControl(*asheFirePattern, OW::HeroSkillControls::TrackingOverlay))
        return Fail();

    const OW::Config::HeroSkillSettings settings = asheFirePattern->defaultSettings;
    if (settings.tracking.aimBehavior != OW::Config::kAimBehaviorTracking)
        return Fail();
    if (!NearlyEqual(
            settings.tracking.speedScale,
            OW::HeroSkillDetail::kAsheFirePatternTrackingSpeedScale))
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
