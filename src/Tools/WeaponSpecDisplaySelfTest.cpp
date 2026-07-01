#include "Game/GameData.hpp"
#include "Game/HeroPerkRuntime.hpp"
#include "Game/WeaponSpec.hpp"
#include "Memory/Memory.h"

#include <cstdlib>
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

} // namespace

Memory::~Memory() {}

int main()
{
    constexpr auto widow = OW::GameData::MakeHeroId(0x00A);
    constexpr auto ashe = OW::GameData::MakeHeroId(0x200);
    constexpr auto mei = OW::GameData::MakeHeroId(0x0DD);
    constexpr auto cassidy = OW::GameData::MakeHeroId(0x042);

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

    return EXIT_SUCCESS;
}
