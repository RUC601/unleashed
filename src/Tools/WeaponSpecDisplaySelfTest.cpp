#include "Game/GameData.hpp"
#include "Game/HeroPerkRuntime.hpp"
#include "Game/WeaponSpec.hpp"
#include "Memory/Memory.h"

#include <cstdlib>
#include <cmath>
#include <cstring>

namespace {

int Fail()
{
    return EXIT_FAILURE;
}

bool SameText(const char* lhs, const char* rhs)
{
    return std::strcmp(lhs, rhs) == 0;
}

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

} // namespace

Memory::~Memory() {}

int main()
{
    constexpr auto widow = OW::GameData::MakeHeroId(0x00A);
    constexpr auto ashe = OW::GameData::MakeHeroId(0x200);
    constexpr auto mei = OW::GameData::MakeHeroId(0x0DD);
    constexpr auto cassidy = OW::GameData::MakeHeroId(0x042);
    constexpr auto doomfist = OW::GameData::MakeHeroId(0x12F);
    constexpr auto orisa = OW::GameData::MakeHeroId(0x13E);

    if (!OW::HeroUsesScopedStanceActions(widow))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(widow, 0), "Unscoped"))
        return Fail();
    if (!SameText(OW::AttackActionCompactNameForHero(ashe, 0), "Unscoped"))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(widow, 2), "Scoped"))
        return Fail();
    if (OW::FireKeyMaskForAttackAction(2) != 0x1u)
        return Fail();
    const OW::WeaponSpec* widowScoped = OW::ResolveWeaponSpec(widow, 2);
    if (!widowScoped || OW::ResolveGeneratedFireKeyMask(widowScoped, 2) != 0x1u)
        return Fail();
    if (OW::ResolveTrackingHoldMouseButton(widowScoped, 2) != 0)
        return Fail();

    if (OW::HeroUsesScopedStanceActions(mei))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(mei, 0), "Primary Fire"))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(mei, 1), "Secondary Fire"))
        return Fail();

    OW::HeroPerkRuntime::SetManualPerkOverride(cassidy, OW::HeroPerkRuntime::ManualOverrideMode::None);
    const OW::WeaponSpec* cassidyDefaultSecondary = OW::ResolveWeaponSpec(cassidy, 1);
    if (!cassidyDefaultSecondary || cassidyDefaultSecondary->weaponId != "cassidy_fan_the_hammer")
        return Fail();
    if (OW::ResolveGeneratedFireKeyMask(cassidyDefaultSecondary, 1) != 0x2u)
        return Fail();

    OW::HeroPerkRuntime::SetManualPerkOverride(cassidy, OW::HeroPerkRuntime::ManualOverrideMode::ForceOn);
    const OW::WeaponSpec* cassidyPerkSecondary = OW::ResolveWeaponSpec(cassidy, 1);
    if (!cassidyPerkSecondary || cassidyPerkSecondary->weaponId != "cassidy_ads_perk")
        return Fail();
    if (cassidyPerkSecondary->variantRequirement != OW::RuntimeVariantRequirement::PerkOn)
        return Fail();
    if (cassidyPerkSecondary->replacesWeaponId != "cassidy_fan_the_hammer")
        return Fail();
    if (OW::ResolveGeneratedFireKeyMask(cassidyPerkSecondary, 1) != 0x1u)
        return Fail();
    if (OW::ResolveTrackingHoldMouseButton(cassidyPerkSecondary, 1) != 0)
        return Fail();
    if (cassidyPerkSecondary->control.stanceHoldButton != 1)
        return Fail();
    OW::HeroPerkRuntime::SetManualPerkOverride(cassidy, OW::HeroPerkRuntime::ManualOverrideMode::None);

    const OW::WeaponSpec* rocketPunch = OW::ResolveWeaponSpec(doomfist, 1);
    if (!rocketPunch || rocketPunch->weaponId != "doomfist_rocket_punch" ||
        rocketPunch->aimClass != OW::AimClass::Movement ||
        rocketPunch->defaultBehavior != OW::AimBehaviorType::Flick ||
        rocketPunch->firePolicy.type != OW::FirePolicyType::ManualOnly ||
        !rocketPunch->projectile.enabledByDefault ||
        !NearlyEqual(rocketPunch->projectile.chargeMinSpeed, 15.0f) ||
        !NearlyEqual(rocketPunch->projectile.chargeMaxSpeed, 35.0f) ||
        OW::MouseButtonForAttackAction(rocketPunch->action) != 1 ||
        OW::ResolveGeneratedFireKeyMask(rocketPunch, 1) != 0u ||
        OW::ResolveTrackingHoldMouseButton(rocketPunch, 1) != OW::kWeaponControlNoButton) {
        return Fail();
    }

    const OW::WeaponSpec* energyJavelin = OW::ResolveWeaponSpec(orisa, 1);
    if (!energyJavelin || energyJavelin->weaponId != "orisa_energy_javelin" ||
        energyJavelin->aimClass != OW::AimClass::ProjectileSingle ||
        energyJavelin->defaultBehavior != OW::AimBehaviorType::Flick ||
        energyJavelin->firePolicy.type != OW::FirePolicyType::ManualOnly ||
        !energyJavelin->projectile.enabledByDefault ||
        !NearlyEqual(energyJavelin->projectile.chargeMinSpeed, 70.0f) ||
        !NearlyEqual(energyJavelin->projectile.chargeMaxSpeed, 140.0f) ||
        !NearlyEqual(energyJavelin->projectile.projectileRadius, 0.5f) ||
        OW::MouseButtonForAttackAction(energyJavelin->action) != 1 ||
        OW::ResolveGeneratedFireKeyMask(energyJavelin, 1) != 0u ||
        OW::ResolveTrackingHoldMouseButton(energyJavelin, 1) != OW::kWeaponControlNoButton) {
        return Fail();
    }

    if (!OW::HeroHasAttackAction(doomfist, 1) || !OW::HeroHasAttackAction(orisa, 1))
        return Fail();

    if (OW::DefaultFirePolicyForBehavior(OW::AimBehaviorType::Tracking) !=
            OW::FirePolicyType::ManualOnly ||
        OW::DefaultFirePolicyForBehavior(OW::AimBehaviorType::Flick) !=
            OW::FirePolicyType::HoldWhileTracking ||
        OW::DefaultFirePolicyForBehavior(OW::AimBehaviorType::Flick2nd) !=
            OW::FirePolicyType::HoldWhileTracking ||
        OW::DefaultFirePolicyForBehavior(OW::AimBehaviorType::Reacquire) !=
            OW::FirePolicyType::HoldWhileTracking ||
        OW::DefaultFirePolicyForBehavior(OW::AimBehaviorType::MagneticTrigger) !=
            OW::FirePolicyType::HoldWhileTracking ||
        OW::AllowsGeneratedAimFire(OW::FirePolicyType::ManualOnly) ||
        !OW::AllowsGeneratedAimFire(OW::FirePolicyType::HoldWhileTracking) ||
        !OW::AimActivationAllowed(true, false, false) ||
        OW::AimActivationAllowed(false, true, true) ||
        OW::AimActivationAllowed(true, true, false) ||
        !OW::AimActivationAllowed(true, true, true)) {
        return Fail();
    }

    return EXIT_SUCCESS;
}
