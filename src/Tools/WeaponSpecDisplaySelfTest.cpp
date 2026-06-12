#include "Game/GameData.hpp"
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

    if (!OW::HeroUsesScopedStanceActions(widow))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(widow, 0), "Unscoped"))
        return Fail();
    if (!SameText(OW::AttackActionCompactNameForHero(ashe, 0), "Unscoped"))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(widow, 2), "Scoped"))
        return Fail();

    if (OW::HeroUsesScopedStanceActions(mei))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(mei, 0), "Primary Fire"))
        return Fail();
    if (!SameText(OW::AttackActionNameForHero(mei, 1), "Secondary Fire"))
        return Fail();

    return EXIT_SUCCESS;
}
