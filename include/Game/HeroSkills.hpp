#pragma once

#include "Utils/Config.hpp"
#include "Game/InputOrchestrator.hpp"
#include "Game/Structs.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace OW {

enum class HeroSkillCategory {
    Skill,
    Ultimate
};

enum class HeroSkillInputAction {
    PrimaryFire,
    SecondaryFire,
    Ability1,
    Ability2,
    Ability3,
    Ultimate,
    Jump,
    Crouch
};

using HeroSkillControlFlags = uint32_t;

namespace HeroSkillControls {
    inline constexpr HeroSkillControlFlags Enabled = 1u << 0;
    inline constexpr HeroSkillControlFlags Key = 1u << 1;
    inline constexpr HeroSkillControlFlags HealthThreshold = 1u << 2;
    inline constexpr HeroSkillControlFlags EnemyHealthThreshold = 1u << 3;
    inline constexpr HeroSkillControlFlags AllyHealthThreshold = 1u << 4;
    inline constexpr HeroSkillControlFlags Distance = 1u << 5;
    inline constexpr HeroSkillControlFlags Mode = 1u << 6;
    inline constexpr HeroSkillControlFlags CooldownGuard = 1u << 7;
    inline constexpr HeroSkillControlFlags Prediction = 1u << 8;
    inline constexpr HeroSkillControlFlags MinTargets = 1u << 9;
    inline constexpr HeroSkillControlFlags Radius = 1u << 10;
    inline constexpr HeroSkillControlFlags Cooldown = 1u << 11;
    inline constexpr HeroSkillControlFlags SequenceSteps = 1u << 12;
    inline constexpr HeroSkillControlFlags TrackingOverlay = 1u << 13;
    inline constexpr HeroSkillControlFlags PitchControl = 1u << 14;
    inline constexpr HeroSkillControlFlags PhaseTiming = 1u << 15;
    inline constexpr HeroSkillControlFlags AmmoGuard = 1u << 16;
}

namespace HeroSkillHotkey {
    inline constexpr int RightMouse = 0;
    inline constexpr int LeftMouse = 1;
    inline constexpr int Mouse4 = 2;
    inline constexpr int Mouse5 = 3;
    inline constexpr int LeftShift = 4;
    inline constexpr int LeftAlt = 5;
    inline constexpr int VKey = 6;
    inline constexpr int LeftCtrl = 7;
    inline constexpr int Tab = 8;
    inline constexpr int EKey = 9;
    inline constexpr int QKey = 10;
    inline constexpr int FKey = 11;
    inline constexpr int CapsLock = 12;
    inline constexpr int Space = 13;
}

struct HeroSkillDefinition {
    uint64_t heroId;
    const char* heroSlug;
    const char* skillId;
    const char* displayName;
    const char* iconSlug;
    HeroSkillInputAction inputAction;
    HeroSkillCategory category;
    HeroSkillControlFlags controls;
    Config::HeroSkillSettings defaultSettings;
};

inline constexpr bool HasHeroSkillControl(const HeroSkillDefinition& definition,
                                          HeroSkillControlFlags control)
{
    return (definition.controls & control) != 0;
}

inline constexpr const char* HeroSkillCategoryName(HeroSkillCategory category)
{
    return category == HeroSkillCategory::Ultimate ? "Ultimate" : "Skill";
}

inline constexpr const char* HeroSkillInputActionName(HeroSkillInputAction action)
{
    switch (action) {
    case HeroSkillInputAction::PrimaryFire: return "Primary Fire";
    case HeroSkillInputAction::SecondaryFire: return "Secondary Fire";
    case HeroSkillInputAction::Ability1: return "Ability 1";
    case HeroSkillInputAction::Ability2: return "Ability 2";
    case HeroSkillInputAction::Ability3: return "Ability 3";
    case HeroSkillInputAction::Ultimate: return "Ultimate";
    case HeroSkillInputAction::Jump: return "Jump";
    case HeroSkillInputAction::Crouch: return "Crouch";
    default: return "Unknown";
    }
}

enum class HeroSkillRunState {
    InProgress,
    Completed,
    Cancelled
};

void RunInputSequence(const std::string& skillId,
                      const std::vector<Config::HeroSkillSequenceStep>& steps,
                      int key,
                      const Config::HeroSkillTrackingParams& trackingParams,
                      bool ammoGuardEnabled,
                      int ammoGuardReserve);
HeroSkillRunState RunViewpointController(const std::string& skillId,
                                         const Config::HeroSkillSettings& params);
bool AnyInputSequenceActive();
ExecutionToken ActiveInputSequenceToken();
bool ShouldBlockForActiveSequence(ExecutionSource requester);
void CancelActiveSkill();
void ProcessHeroSkills();

inline Config::HeroSkillSettings MakeAsheComboSequenceDefaults()
{
    Config::HeroSkillSettings settings{};
    settings.enabled = false;
    settings.key = HeroSkillHotkey::Mouse4;
    settings.healthThreshold = 50.0f;
    settings.enemyHealthThreshold = 50.0f;
    settings.allyHealthThreshold = 50.0f;
    settings.distance = 30.0f;
    settings.mode = 0;
    settings.cooldown = 0.0f;
    settings.cooldownGuard = true;
    settings.prediction = true;
    settings.minTargets = 1;
    settings.radius = 0.0f;
    settings.tracking = { 0, 5.0f, 0.0f, Config::kAimBoneHead, Config::kDefaultHitboxScalePercent };
    settings.ammoGuard = true;
    settings.ammoGuardReserve = 1;

    // Locked backend Ashe fire pattern. This timing was tuned live and is no
    // longer exposed as editable UI steps. mask bit0=left, bit1=right.
    settings.sequenceSteps = {
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
    };
    return settings;
}

inline std::vector<Config::HeroSkillSequenceStep> MakeAsheBruteLeftSequenceSteps()
{
    // Experimental recovery pattern: keep right held through a scoped window and
    // over-sample left-click edges so the rate limiter can accept the first valid tap.
    return {
        { 0x01, 73, 1.0f, 0 },  // opening left press
        { 0x03, 11, 1.0f, 0 },  // right joins
        { 0x02, 95, 1.0f, 0 },  // left release, hold scoped state
        { 0x03, 18, 1.0f, 0 },
        { 0x02, 28, 1.0f, 0 },
        { 0x03, 18, 1.0f, 0 },
        { 0x02, 28, 1.0f, 0 },
        { 0x03, 18, 1.0f, 0 },
        { 0x02, 28, 1.0f, 0 },
        { 0x03, 18, 1.0f, 0 },
        { 0x02, 28, 1.0f, 0 },
        { 0x03, 18, 1.0f, 0 },
        { 0x02, 28, 1.0f, 0 },
        { 0x00, 16, 1.0f, 0 },  // release scope
        { 0x01, 34, 1.0f, 0 },  // hip-fire retry at the cycle boundary
        { 0x00, 87, 1.0f, 0 },
    };
}

inline std::vector<Config::HeroSkillSequenceStep> MakeAsheSpacedLeftSequenceSteps()
{
    // Experimental stable pattern: keep the 518 ms cycle, but space the scoped
    // left taps farther apart than the hand-measured baseline.
    return {
        { 0x01,  73, 1.0f, 0 },
        { 0x03,  11, 1.0f, 0 },
        { 0x02, 125, 1.0f, 0 },
        { 0x03,  24, 1.0f, 0 },
        { 0x02, 105, 1.0f, 0 },
        { 0x03,  24, 1.0f, 0 },
        { 0x02,  15, 1.0f, 0 },
        { 0x00,  16, 1.0f, 0 },
        { 0x01,  42, 1.0f, 0 },
        { 0x00,  83, 1.0f, 0 },
    };
}

inline Config::HeroSkillSettings MakeFrejaSequenceSetDefaults()
{
    Config::HeroSkillSettings settings{};
    settings.enabled = false;
    settings.key = HeroSkillHotkey::Mouse4;
    settings.healthThreshold = 50.0f;
    settings.enemyHealthThreshold = 50.0f;
    settings.allyHealthThreshold = 50.0f;
    settings.distance = 30.0f;
    settings.mode = 0;
    settings.cooldown = 0.0f;
    settings.cooldownGuard = true;
    settings.prediction = true;
    settings.minTargets = 1;
    settings.radius = 0.0f;
    settings.tracking = { 0, 5.0f, 0.0f, Config::kAimBoneChest, Config::kDefaultHitboxScalePercent };
    settings.ammoGuard = false;
    settings.ammoGuardReserve = 1;

    // Editable Freja placeholder: two primary taps, then a secondary hold/release.
    // Keep this visible in the UI so we can tune the timing after range tests.
    settings.sequenceSteps = {
        { 0x01,  45 },
        { 0x00, 110 },
        { 0x01,  45 },
        { 0x00, 110 },
        { 0x02, 180 },
        { 0x00, 180 },
    };
    return settings;
}

inline Config::HeroSkillSettings MakeZaryaPropelJumpDefaults()
{
    Config::HeroSkillSettings settings{};
    settings.enabled = false;
    settings.key = HeroSkillHotkey::Mouse4;
    settings.healthThreshold = 50.0f;
    settings.enemyHealthThreshold = 50.0f;
    settings.allyHealthThreshold = 50.0f;
    settings.distance = 30.0f;
    settings.mode = 0;
    settings.cooldown = 0.0f;
    settings.cooldownGuard = false;
    settings.prediction = false;
    settings.minTargets = 1;
    settings.radius = 0.0f;

    // Pitch down: duration-driven. Target angle / randomized duration -> speed.
    // fireDelayMs is the gap between the secondary-fire pulse and jump key.
    settings.pitchDownDurationMs = 45;
    settings.pitchDownDurationJitter = 10.0f;
    settings.pitchDownTargetAngle = 90.0f;
    settings.pitchUpOffsetJitter = 1.5f;
    settings.fireDelayMs = 50;
    settings.jumpKeyCode = VK_SPACE;
    return settings;
}

inline Config::HeroSkillSettings MakeZaryaReloadAmmoProbeDefaults()
{
    Config::HeroSkillSettings settings{};
    settings.enabled = false;
    settings.key = HeroSkillHotkey::RightMouse;
    settings.cooldownGuard = false;
    settings.cooldown = 0.0f;
    settings.prediction = false;
    settings.ammoGuard = false;
    settings.ammoGuardReserve = 10;
    return settings;
}

inline const HeroSkillDefinition kHeroSkillDefinitions[] = {
    {
        OW::eHero::HERO_ASHE,
        "ashe",
        "coach-gun",
        "Coach Gun",
        "coach-gun",
        HeroSkillInputAction::Ability1,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::LeftShift, 50.0f, 50.0f, 50.0f, 15.0f, 0, 10.0f, true, false, 1, 0.0f }
    },
    {
        OW::eHero::HERO_ASHE,
        "ashe",
        "dynamite",
        "Dynamite",
        "dynamite",
        HeroSkillInputAction::Ability2,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::AllyHealthThreshold |
            HeroSkillControls::Distance | HeroSkillControls::CooldownGuard |
            HeroSkillControls::Prediction | HeroSkillControls::MinTargets |
            HeroSkillControls::Radius | HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::EKey, 50.0f, 50.0f, 50.0f, 35.0f, 0, 12.0f, true, true, 1, 1.5f }
    },
    {
        OW::eHero::HERO_HANJO,
        "hanzo",
        "sonic-arrow",
        "Sonic Arrow",
        "sonic-arrow",
        HeroSkillInputAction::Ability1,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Prediction |
            HeroSkillControls::MinTargets | HeroSkillControls::Radius |
            HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::LeftShift, 50.0f, 50.0f, 50.0f, 40.0f, 0, 12.0f, true, true, 1, 9.0f }
    },
    {
        OW::eHero::HERO_HANJO,
        "hanzo",
        "storm-arrows",
        "Storm Arrows",
        "storm-arrows",
        HeroSkillInputAction::Ability2,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::AllyHealthThreshold |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Prediction |
            HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::EKey, 50.0f, 50.0f, 50.0f, 30.0f, 0, 8.0f, true, true, 1, 0.0f }
    },
    {
        OW::eHero::HERO_ASHE,
        "ashe",
        "fire-pattern",
        "Fire Pattern",
        "fire-pattern",
        HeroSkillInputAction::PrimaryFire,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown |
            HeroSkillControls::AmmoGuard |
            HeroSkillControls::SequenceSteps,
        MakeAsheComboSequenceDefaults()
    },
    {
        OW::eHero::HERO_FREJA,
        "freja",
        "sequence-set",
        "Sequence Set",
        "quick-dash",
        HeroSkillInputAction::PrimaryFire,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown |
            HeroSkillControls::AmmoGuard |
            HeroSkillControls::SequenceSteps,
        MakeFrejaSequenceSetDefaults()
    },
    {
        OW::eHero::HERO_HANJO,
        "hanzo",
        "lunge",
        "Lunge",
        "lunge",
        HeroSkillInputAction::Jump,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::Distance | HeroSkillControls::CooldownGuard |
            HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::Space, 50.0f, 50.0f, 50.0f, 7.85f, 0, 4.0f, true, false, 1, 0.0f }
    },
    {
        OW::eHero::HERO_GENJI,
        "genji",
        "swift-strike",
        "Swift Strike",
        "swift-strike",
        HeroSkillInputAction::Ability1,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::LeftShift, 50.0f, 45.0f, 50.0f, 15.0f, 0, 8.0f, true, false, 1, 0.0f }
    },
    {
        OW::eHero::HERO_GENJI,
        "genji",
        "deflect",
        "Deflect",
        "deflect",
        HeroSkillInputAction::Ability2,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::HealthThreshold | HeroSkillControls::CooldownGuard |
            HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::EKey, 60.0f, 50.0f, 50.0f, 30.0f, 0, 8.0f, true, false, 1, 0.0f }
    },
    {
        OW::eHero::HERO_ANA,
        "ana",
        "sleep-dart",
        "Sleep Dart",
        "sleep-dart",
        HeroSkillInputAction::Ability1,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Prediction |
            HeroSkillControls::MinTargets | HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::LeftShift, 50.0f, 50.0f, 50.0f, 45.0f, 0, 14.0f, true, true, 1, 0.0f }
    },
    {
        OW::eHero::HERO_ANA,
        "ana",
        "biotic-grenade",
        "Biotic Grenade",
        "biotic-grenade",
        HeroSkillInputAction::Ability2,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::AllyHealthThreshold |
            HeroSkillControls::Distance | HeroSkillControls::CooldownGuard |
            HeroSkillControls::Prediction | HeroSkillControls::MinTargets |
            HeroSkillControls::Radius | HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::EKey, 50.0f, 50.0f, 60.0f, 35.0f, 0, 12.0f, true, true, 1, 4.0f }
    },
    {
        OW::eHero::HERO_ZARYA,
        "zarya",
        "propel-jump",
        "Propel Jump",
        "propel-jump",
        HeroSkillInputAction::SecondaryFire,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown |
            HeroSkillControls::PitchControl | HeroSkillControls::PhaseTiming,
        MakeZaryaPropelJumpDefaults()
    },
    {
        OW::eHero::HERO_ZARYA,
        "zarya",
        "reload-ammo-probe",
        "Reload Ammo Probe",
        "particle-cannon",
        HeroSkillInputAction::SecondaryFire,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled,
        MakeZaryaReloadAmmoProbeDefaults()
    },
    {
        OW::eHero::HERO_ROADHOG,
        "roadhog",
        "chain-hook",
        "Chain Hook",
        "chain-hook",
        HeroSkillInputAction::Ability1,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Prediction |
            HeroSkillControls::Cooldown,
        { false, HeroSkillHotkey::LeftShift, 50.0f, 50.0f, 50.0f, 20.0f, 0, 6.0f, true, true, 1, 0.0f }
    },
    {
        OW::eHero::HERO_DOOMFIST,
        "doomfist",
        "rocket-punch",
        "Rocket Punch",
        "rocket-punch",
        HeroSkillInputAction::SecondaryFire,
        HeroSkillCategory::Skill,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Cooldown |
            HeroSkillControls::Prediction,
        { false, HeroSkillHotkey::RightMouse, 50.0f, 65.0f, 50.0f, 20.0f, 0, 4.0f, true, true, 1, 0.0f }
    },
    {
        OW::eHero::HERO_TRACER,
        "tracer",
        "pulse-bomb",
        "Pulse Bomb",
        "pulse-bomb",
        HeroSkillInputAction::Ultimate,
        HeroSkillCategory::Ultimate,
        HeroSkillControls::Enabled | HeroSkillControls::Key |
            HeroSkillControls::EnemyHealthThreshold | HeroSkillControls::Distance |
            HeroSkillControls::CooldownGuard | HeroSkillControls::Prediction |
            HeroSkillControls::Radius,
        { false, HeroSkillHotkey::QKey, 50.0f, 100.0f, 50.0f, 5.0f, 0, 0.0f, false, true, 1, 3.0f }
    }
};

inline const auto& AllHeroSkillDefinitions()
{
    return kHeroSkillDefinitions;
}

inline size_t HeroSkillDefinitionCount()
{
    return sizeof(kHeroSkillDefinitions) / sizeof(kHeroSkillDefinitions[0]);
}

inline int CountHeroSkillDefinitions(uint64_t heroId)
{
    int count = 0;
    for (const HeroSkillDefinition& definition : kHeroSkillDefinitions) {
        if (definition.heroId == heroId)
            ++count;
    }
    return count;
}

} // namespace OW
