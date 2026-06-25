#include "Game/WeaponSpec.hpp"

#include <algorithm>
#include <array>
#include <string_view>

#include "Game/HeroPerkRuntime.hpp"
#include "Game/Structs.hpp"
#include "Utils/InputLabels.hpp"

namespace OW {
namespace {

using AC = AimClass;
using AB = AimBehaviorType;
using FP = FirePolicyType;

constexpr std::string_view kReviewSource =
    "D:/Desktop/SenseZen/ECS_O/02_RESEARCH/un-dma-research/武器分类研究/weapon_backend_mapping_review_0530_zh.md";
constexpr std::string_view kWikiBase = "https://overwatch.fandom.com/wiki/";
constexpr std::string_view kTemporaryProjectileFallbackNote =
    "Temporary radius fallback from data/aim_public_weapon_projectiles_0530.tsv; verify against live/source data later.";

constexpr ProjectileSpec Hitscan(float radius)
{
    return ProjectileSpec{ false, 0.0f, false, radius, 0.0f, 0.0f, true };
}

constexpr ProjectileSpec Projectile(float speed, float radius, bool gravity = false)
{
    return ProjectileSpec{ true, speed, gravity, radius, 0.0f, 0.0f, true };
}

constexpr ProjectileSpec ChargedProjectile(float minSpeed, float maxSpeed, float radius, bool gravity = false)
{
    return ProjectileSpec{ true, maxSpeed, gravity, radius, minSpeed, maxSpeed, true };
}

constexpr FirePolicy Policy(FP type)
{
    return FirePolicy{ type, 0.0f, 0.0f, 0.0f, 100.0f };
}

constexpr WeaponControlSpec Control(int generatedFireButton = kWeaponControlUseActionButton,
                                    int trackingHoldButton = kWeaponControlUseActionButton,
                                    int stanceHoldButton = kWeaponControlNoButton)
{
    return WeaponControlSpec{ generatedFireButton, trackingHoldButton, stanceHoldButton };
}

constexpr WeaponSpec W(uint64_t heroId,
                       std::string_view heroName,
                       std::string_view weaponId,
                       std::string_view weaponName,
                       int action,
                       int order,
                       AC aimClass,
                       ProjectileSpec projectile,
                       AB behavior,
                       FP firePolicy,
                       float confidence = 0.75f,
                       std::string_view note = kReviewSource,
                       RuntimeVariantRequirement variantRequirement = RuntimeVariantRequirement::None,
                       std::string_view variantId = {},
                       std::string_view replacesWeaponId = {},
                       WeaponControlSpec control = {})
{
    return WeaponSpec{
        heroId,
        heroName,
        weaponId,
        weaponName,
        action,
        order,
        aimClass,
        projectile,
        Policy(firePolicy),
        behavior,
        variantRequirement,
        variantId,
        replacesWeaponId,
        control,
        kWikiBase,
        note,
        confidence
    };
}

constexpr std::array<WeaponSpec, 75> kWeaponSpecs = {
    W(eHero::HERO_REAPER, "Reaper", "reaper_hellfire_shotguns", "Hellfire Shotguns", 0, 1, AC::Shotgun, Hitscan(0.04f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_TRACER, "Tracer", "tracer_pulse_pistols", "Pulse Pistols", 0, 1, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MERCY, "Mercy", "mercy_caduceus_staff_heal", "Caduceus Staff", 0, 1, AC::Targeted, Hitscan(0.0f), AB::Tracking, FP::ManualOnly),
    W(eHero::HERO_MERCY, "Mercy", "mercy_caduceus_staff_damage", "Caduceus Staff Damage Boost", 1, 2, AC::Targeted, Hitscan(0.0f), AB::Tracking, FP::ManualOnly),
    W(eHero::HERO_MERCY, "Mercy", "mercy_caduceus_blaster", "Caduceus Blaster", 0, 3, AC::ProjectileAuto, Projectile(50.0f, 0.275f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_HANJO, "Hanzo", "hanzo_storm_bow", "Storm Bow", 0, 1, AC::ProjectileSingle, ChargedProjectile(25.0f, 110.0f, 0.125f, true), AB::FlickDelay, FP::ChargeRelease),
    W(eHero::HERO_TORBJORN, "Torbjorn", "torbjorn_rivet_gun", "Rivet Gun", 0, 1, AC::ProjectileSingle, Projectile(70.0f, 0.18f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_TORBJORN, "Torbjorn", "torbjorn_rivet_gun_alt", "Rivet Gun Alt Fire", 1, 2, AC::Shotgun, Projectile(120.0f, 0.05f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_TORBJORN, "Torbjorn", "torbjorn_forge_hammer", "Forge Hammer", 0, 3, AC::Melee, Hitscan(0.0f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_REINHARDT, "Reinhardt", "reinhardt_rocket_hammer", "Rocket Hammer", 0, 1, AC::Melee, Hitscan(0.0f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_PHARAH, "Pharah", "pharah_rocket_launcher", "Rocket Launcher", 0, 1, AC::ProjectileExplosive, Projectile(40.0f, 0.325f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_WINSTON, "Winston", "winston_tesla_cannon", "Tesla Cannon", 0, 1, AC::Beam, Hitscan(0.0f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_WINSTON, "Winston", "winston_tesla_cannon_alt", "Tesla Cannon Alt Fire", 1, 2, AC::Beam, Projectile(0.0f, 0.33f, true), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_WIDOWMAKER, "Widowmaker", "widowmaker_widows_kiss_hip", "Widow's Kiss", 0, 1, AC::HitscanAuto, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_WIDOWMAKER, "Widowmaker", "widowmaker_widows_kiss_ads", "Widow's Kiss ADS", 2, 2, AC::HitscanSingle, Hitscan(0.0f), AB::Flick, FP::TapOnHitWindow),
    W(eHero::HERO_BASTION, "Bastion", "bastion_recon", "Configuration: Recon", 0, 1, AC::HitscanAuto, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_BASTION, "Bastion", "bastion_assault", "Configuration: Assault", 0, 2, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_SYMMETRA, "Symmetra", "symmetra_photon_projector", "Photon Projector", 0, 1, AC::Beam, Hitscan(0.25f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_SYMMETRA, "Symmetra", "symmetra_photon_projector_alt", "Photon Projector Alt Fire", 1, 2, AC::ProjectileExplosive, Projectile(50.0f, 0.475f), AB::FlickDelay, FP::ChargeRelease),
    W(eHero::HERO_ZENYATTA, "Zenyatta", "zenyatta_orb_of_destruction", "Orb of Destruction", 0, 1, AC::ProjectileSingle, Projectile(90.0f, 0.225f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_ZENYATTA, "Zenyatta", "zenyatta_orb_of_destruction_alt", "Orb of Destruction Alt Fire", 1, 2, AC::ProjectileSingle, Projectile(90.0f, 0.225f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_GENJI, "Genji", "genji_shuriken", "Shuriken", 0, 1, AC::ProjectileSingle, Projectile(75.0f, 0.125f), AB::FlickDelay, FP::ReleaseAfterDelay, 0.85f, "Overwatch Wiki Projectile table, checked 2026-05-30."),
    W(eHero::HERO_GENJI, "Genji", "genji_shuriken_alt", "Shuriken Alt Fire", 1, 2, AC::ProjectileSingle, Projectile(75.0f, 0.125f, true), AB::FlickDelay, FP::ReleaseAfterDelay, 0.85f, "Overwatch Wiki Projectile table, checked 2026-05-30."),
    W(eHero::HERO_ROADHOG, "Roadhog", "roadhog_scrap_gun", "Scrap Gun", 0, 1, AC::Shotgun, Projectile(80.0f, 0.05f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_ROADHOG, "Roadhog", "roadhog_scrap_gun_alt", "Scrap Gun Alt Fire", 1, 2, AC::Shotgun, Projectile(80.0f, 0.07f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_MCCREE, "Cassidy", "cassidy_peacekeeper", "Peacekeeper", 0, 1, AC::HitscanSingle, Hitscan(0.07f), AB::Flick, FP::TapOnHitWindow),
    W(eHero::HERO_MCCREE, "Cassidy", "cassidy_fan_the_hammer", "Fan the Hammer", 1, 2, AC::HitscanAuto, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MCCREE, "Cassidy", "cassidy_ads_perk", "Peacekeeper ADS Perk", 1, 3, AC::HitscanSingle, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking,
      0.50f,
      "Manual perk variant: physical right mouse holds ADS stance; generated fire uses left mouse.",
      RuntimeVariantRequirement::PerkOn,
      "cassidy_ads_perk",
      "cassidy_fan_the_hammer",
      Control(0, 0, 1)),
    W(eHero::HERO_JUNKRAT, "Junkrat", "junkrat_frag_launcher", "Frag Launcher", 0, 1, AC::ProjectileExplosive, Projectile(30.0f, 0.325f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_ZARYA, "Zarya", "zarya_particle_cannon", "Particle Cannon", 0, 1, AC::Beam, Hitscan(0.2f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_ZARYA, "Zarya", "zarya_particle_cannon_alt", "Particle Cannon Alt Fire", 1, 2, AC::ProjectileExplosive, Projectile(25.0f, 0.32f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_SOLDIER76, "Soldier 76", "soldier76_heavy_pulse_rifle", "Heavy Pulse Rifle", 0, 1, AC::HitscanAuto, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_LUCIO, "Lucio", "lucio_sonic_amplifier", "Sonic Amplifier", 0, 1, AC::ProjectileSingle, Projectile(50.0f, 0.22f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_DVA, "DVa", "dva_fusion_cannons", "Fusion Cannons", 0, 1, AC::Shotgun, Hitscan(0.04f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_DVA, "DVa", "dva_light_gun", "Light Gun", 0, 2, AC::ProjectileAuto, Projectile(50.0f, 0.275f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MEI, "Mei", "mei_endothermic_blaster", "Endothermic Blaster", 0, 1, AC::Beam, Projectile(40.0f, 0.55f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MEI, "Mei", "mei_icicle", "Icicle", 1, 2, AC::ProjectileSingle, Projectile(115.0f, 0.2f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_SOMBRA, "Sombra", "sombra_machine_pistol", "Machine Pistol", 0, 1, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_DOOMFIST, "Doomfist", "doomfist_hand_cannon", "Hand Cannon", 0, 1, AC::Shotgun, Projectile(80.0f, 0.05f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_ANA, "Ana", "ana_biotic_rifle_hip", "Biotic Rifle", 0, 1, AC::ProjectileSingle, Projectile(125.0f, 0.15f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_ANA, "Ana", "ana_biotic_rifle_ads", "Zoom ADS", 2, 2, AC::HitscanSingle, Hitscan(0.07f), AB::Flick, FP::TapOnHitWindow),
    W(eHero::HERO_ORISA, "Orisa", "orisa_augmented_fusion_driver", "Augmented Fusion Driver", 0, 1, AC::ProjectileAuto, Projectile(100.0f, 0.225f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_BRIGITTE, "Brigitte", "brigitte_rocket_flail", "Rocket Flail", 0, 1, AC::Melee, Hitscan(0.0f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_MOIRA, "Moira", "moira_biotic_grasp_heal", "Biotic Grasp", 0, 1, AC::Beam, Projectile(40.0f, 0.0f, true), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MOIRA, "Moira", "moira_biotic_grasp_damage", "Biotic Grasp Alt Fire", 1, 2, AC::Beam, Hitscan(0.0f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_WRECKINGBALL, "Wrecking Ball", "wreckingball_quad_cannons", "Quad Cannons", 0, 1, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_SOJOURN, "Sojourn", "sojourn_railgun", "Railgun", 0, 1, AC::ProjectileAuto, Projectile(150.0f, 0.175f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_SOJOURN, "Sojourn", "sojourn_charged_shot", "Charged Shot", 1, 2, AC::HitscanSingle, Hitscan(0.14f), AB::Flick, FP::ChargeRelease),
    W(eHero::HERO_ASHE, "Ashe", "ashe_the_viper_hip", "The Viper", 0, 1, AC::HitscanAuto, Hitscan(0.07f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_ASHE, "Ashe", "ashe_take_aim_ads", "Take Aim ADS", 2, 2, AC::HitscanSingle, Hitscan(0.07f), AB::Flick, FP::TapOnHitWindow),
    W(eHero::HERO_ECHO, "Echo", "echo_tri_shot", "Tri-Shot", 0, 1, AC::ProjectileSingle, Projectile(75.0f, 0.175f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_BAPTISTE, "Baptiste", "baptiste_biotic_launcher", "Biotic Launcher", 0, 1, AC::HitscanBurst, Hitscan(0.07f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_BAPTISTE, "Baptiste", "baptiste_biotic_launcher_alt", "Biotic Launcher Alt Fire", 1, 2, AC::ProjectileExplosive, Projectile(60.0f, 0.20f, true), AB::FlickDelay, FP::ReleaseAfterDelay, 0.60f, kTemporaryProjectileFallbackNote),
    W(eHero::HERO_KIRIKO, "Kiriko", "kiriko_healing_ofuda", "Healing Ofuda", 0, 1, AC::Targeted, Projectile(30.0f, 0.0f, true), AB::Tracking, FP::ManualOnly),
    W(eHero::HERO_KIRIKO, "Kiriko", "kiriko_kunai", "Kunai", 1, 2, AC::ProjectileSingle, Projectile(90.0f, 0.195f), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_JUNKERQUEEN, "Junker Queen", "junkerqueen_scattergun", "Scattergun", 0, 1, AC::Shotgun, Hitscan(0.04f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_SIGMA, "Sigma", "sigma_hyperspheres", "Hyperspheres", 0, 1, AC::ProjectileExplosive, Projectile(48.0f, 0.32f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_RAMATTRA, "Ramattra", "ramattra_void_accelerator", "Void Accelerator", 0, 1, AC::ProjectileAuto, Projectile(80.0f, 0.15f, true), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_RAMATTRA, "Ramattra", "ramattra_pummel", "Pummel", 0, 2, AC::Melee, Projectile(105.0f, 0.575f, true), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_LIFEWEAVER, "Lifeweaver", "lifeweaver_healing_blossom", "Healing Blossom", 0, 1, AC::Targeted, Projectile(60.0f, 0.25f, true), AB::Tracking, FP::ManualOnly),
    W(eHero::HERO_LIFEWEAVER, "Lifeweaver", "lifeweaver_thorn_volley", "Thorn Volley", 1, 2, AC::ProjectileAuto, Projectile(100.0f, 0.2f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MAUGA, "Mauga", "mauga_incendiary_chaingun", "Incendiary Chaingun", 0, 1, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_MAUGA, "Mauga", "mauga_volatile_chaingun", "Volatile Chaingun", 1, 2, AC::HitscanAuto, Hitscan(0.04f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_ILLARI, "Illari", "illari_solar_rifle", "Solar Rifle", 0, 1, AC::HitscanSingle, Hitscan(0.07f), AB::Flick, FP::TapOnHitWindow),
    W(eHero::HERO_ILLARI, "Illari", "illari_solar_rifle_alt", "Solar Rifle Alt Fire", 1, 2, AC::Beam, Hitscan(0.25f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_EMRE, "Emre", "emre_synthetic_burst_rifle", "Synthetic Burst Rifle", 0, 1, AC::HitscanBurst, Hitscan(0.07f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_EMRE, "Emre", "emre_synthetic_burst_rifle_ads", "Synthetic Burst Rifle ADS", 2, 2, AC::HitscanBurst, Hitscan(0.07f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_FREJA, "Freja", "freja_revdraw_crossbow", "Revdraw Crossbow", 0, 1, AC::ProjectileAuto, Projectile(125.0f, 0.175f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_FREJA, "Freja", "freja_take_aim", "Take Aim", 2, 2, AC::ProjectileExplosive, Projectile(125.0f, 0.175f, true), AB::FlickDelay, FP::ChargeRelease),
    W(eHero::HERO_VENTURE, "Venture", "venture_smart_excavator", "Smart Excavator", 0, 1, AC::ProjectileExplosive, Projectile(50.0f, 0.32f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_HAZARD, "Hazard", "hazard_bonespur", "Bonespur", 0, 1, AC::Shotgun, Projectile(140.0f, 0.05f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_JUNO, "Juno", "juno_mediblaster", "Mediblaster", 0, 1, AC::HitscanBurst, Hitscan(0.04f), AB::FlickClamp, FP::TapOnHitWindow),
    W(eHero::HERO_SIERRA, "Sierra", "sierra_helix_rifle", "Helix Rifle", 0, 1, AC::ProjectileAuto, Projectile(85.0f, 0.26f), AB::Tracking, FP::HoldWhileTracking),
    W(eHero::HERO_WUYANG, "Wuyang", "wuyang_xuanwu_staff", "Xuanwu Staff", 0, 1, AC::ProjectileExplosive, Projectile(25.0f, 0.42f, true), AB::FlickDelay, FP::ReleaseAfterDelay),
    W(eHero::HERO_JETPACKCAT, "Jetpack Cat", "jetpackcat_biotic_pawjectiles", "Biotic Pawjectiles", 0, 1, AC::Shotgun, Projectile(60.0f, 0.37f, true), AB::FlickClamp, FP::TapOnHitWindow),
};

} // namespace

const WeaponSpec* ResolveWeaponSpec(uint64_t heroId, int action)
{
    const WeaponSpec* firstForHero = nullptr;
    const WeaponSpec* firstBaseForHero = nullptr;
    const WeaponSpec* baseForAction = nullptr;
    const WeaponSpec* variantForAction = nullptr;
    const bool perkOn = HeroPerkRuntime::IsEffectivePerkOn(heroId);

    for (const WeaponSpec& spec : kWeaponSpecs) {
        if (spec.heroId != heroId)
            continue;

        if (!firstForHero)
            firstForHero = &spec;

        const bool isVariant = spec.variantRequirement != RuntimeVariantRequirement::None;
        if (!isVariant && !firstBaseForHero)
            firstBaseForHero = &spec;

        if (spec.action != action)
            continue;

        if (isVariant) {
            if (spec.variantRequirement == RuntimeVariantRequirement::PerkOn && perkOn)
                variantForAction = &spec;
            continue;
        }

        if (!baseForAction)
            baseForAction = &spec;
    }

    if (variantForAction)
        return variantForAction;
    if (baseForAction)
        return baseForAction;
    return firstBaseForHero ? firstBaseForHero : firstForHero;
}

const WeaponSpec* ResolveDefaultWeaponSpec(uint64_t heroId)
{
    return ResolveWeaponSpec(heroId, 0);
}

const WeaponSpec* WeaponSpecsBegin()
{
    return kWeaponSpecs.data();
}

const WeaponSpec* WeaponSpecsEnd()
{
    return kWeaponSpecs.data() + kWeaponSpecs.size();
}

std::size_t WeaponSpecCount()
{
    return kWeaponSpecs.size();
}

bool HeroHasAttackAction(uint64_t heroId, int action)
{
    for (const WeaponSpec& spec : kWeaponSpecs) {
        if (spec.heroId == heroId &&
            spec.action == action &&
            spec.variantRequirement == RuntimeVariantRequirement::None) {
            return true;
        }
    }
    return false;
}

bool HeroUsesScopedStanceActions(uint64_t heroId)
{
    return HeroHasAttackAction(heroId, 0) && HeroHasAttackAction(heroId, 2);
}

const char* AttackActionNameForHero(uint64_t heroId, int action)
{
    if (HeroUsesScopedStanceActions(heroId) && action == 0)
        return "Unscoped";
    return Labels::AttackActionName(action);
}

const char* AttackActionCompactNameForHero(uint64_t heroId, int action)
{
    if (HeroUsesScopedStanceActions(heroId) && action == 0)
        return "Unscoped";
    return Labels::AttackActionCompactName(action);
}

const char* AimClassName(AimClass value)
{
    switch (value) {
    case AimClass::HitscanSingle: return "HitscanSingle";
    case AimClass::HitscanAuto: return "HitscanAuto";
    case AimClass::HitscanBurst: return "HitscanBurst";
    case AimClass::ProjectileSingle: return "ProjectileSingle";
    case AimClass::ProjectileAuto: return "ProjectileAuto";
    case AimClass::ProjectileExplosive: return "ProjectileExplosive";
    case AimClass::Shotgun: return "Shotgun";
    case AimClass::Beam: return "Beam";
    case AimClass::Targeted: return "Targeted";
    case AimClass::Melee: return "Melee";
    case AimClass::Movement: return "Movement";
    default: return "Unknown";
    }
}

const char* RuntimeVariantRequirementName(RuntimeVariantRequirement value)
{
    switch (value) {
    case RuntimeVariantRequirement::PerkOn: return "PerkOn";
    case RuntimeVariantRequirement::None:
    default: return "None";
    }
}

int MouseButtonForAttackAction(int action)
{
    switch (action) {
    case 1: // Secondary Fire
    case 2: // Scoped
        return 1;
    case 0: // Primary Fire
    case 3: // Unscoped
        return 0;
    default:
        return -1;
    }
}

uint32_t FireKeyMaskForMouseButton(int button)
{
    switch (button) {
    case 0: return 0x1u;
    case 1: return 0x2u;
    case 2: return 0x4u;
    default: return 0u;
    }
}

uint32_t FireKeyMaskForAttackAction(int action)
{
    return FireKeyMaskForMouseButton(MouseButtonForAttackAction(action));
}

int ResolveGeneratedFireMouseButton(const WeaponSpec* weapon, int fallbackAction)
{
    if (weapon && weapon->control.generatedFireButton != kWeaponControlUseActionButton)
        return weapon->control.generatedFireButton;
    return MouseButtonForAttackAction(fallbackAction);
}

int ResolveTrackingHoldMouseButton(const WeaponSpec* weapon, int fallbackAction)
{
    if (weapon && weapon->control.trackingHoldButton != kWeaponControlUseActionButton)
        return weapon->control.trackingHoldButton;
    return ResolveGeneratedFireMouseButton(weapon, fallbackAction);
}

uint32_t ResolveGeneratedFireKeyMask(const WeaponSpec* weapon, int fallbackAction)
{
    return FireKeyMaskForMouseButton(ResolveGeneratedFireMouseButton(weapon, fallbackAction));
}

const char* AimBehaviorName(AimBehaviorType value)
{
    switch (value) {
    case AimBehaviorType::Tracking: return "Tracking";
    case AimBehaviorType::Flick: return "Flick";
    case AimBehaviorType::Flick2nd: return "Flick2nd";
    case AimBehaviorType::Reacquire: return "Reacquire";
    case AimBehaviorType::AssistFlick: return "AssistFlick";
    default: return "Tracking";
    }
}

const char* FirePolicyName(FirePolicyType value)
{
    switch (value) {
    case FirePolicyType::ManualOnly: return "ManualOnly";
    case FirePolicyType::HoldWhileTracking: return "HoldWhileTracking";
    case FirePolicyType::TapOnHitWindow: return "TapOnHitWindow";
    case FirePolicyType::ReleaseAfterDelay: return "ReleaseAfterDelay";
    case FirePolicyType::TimedBurst: return "TimedBurst";
    case FirePolicyType::ChargeRelease: return "ChargeRelease";
    default: return "ManualOnly";
    }
}

const char* PredictionOverrideName(PredictionOverrideMode value)
{
    switch (value) {
    case PredictionOverrideMode::Auto: return "Auto";
    case PredictionOverrideMode::ForceOn: return "Force On";
    case PredictionOverrideMode::ForceOff: return "Force Off";
    default: return "Auto";
    }
}

} // namespace OW
