#include "Game/GameData.hpp"
#include "Game/WeaponCadence.hpp"
#include "Game/WeaponSpec.hpp"
#include "Memory/Memory.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

Memory::~Memory() {}

namespace {

int Fail(const char* message)
{
    std::fprintf(stderr, "WeaponCadenceSelfTest failed: %s\n", message);
    return EXIT_FAILURE;
}

bool NearlyEqual(float lhs, float rhs)
{
    return std::fabs(lhs - rhs) <= 0.001f;
}

bool ExpectInterval(const OW::WeaponCadenceTable& table,
                    std::string_view weaponId,
                    float expected)
{
    const OW::WeaponCadenceEntry* entry = table.Find(weaponId);
    return entry && entry->triggerCycleIntervalMs &&
        NearlyEqual(*entry->triggerCycleIntervalMs, expected);
}

} // namespace

int main()
{
#ifndef WEAPON_CADENCE_SOURCE_PATH
    return Fail("WEAPON_CADENCE_SOURCE_PATH is not defined");
#else
    OW::WeaponCadenceTable table;
    std::string error;
    if (!table.LoadTsv(WEAPON_CADENCE_SOURCE_PATH, &error)) {
        std::fprintf(stderr, "WeaponCadenceSelfTest load error: %s\n", error.c_str());
        return EXIT_FAILURE;
    }
#endif

    if (!table.Loaded() || table.SourceRowCount() != 81 ||
        table.EligibleRowCount() != 75 || table.Size() != 75 ||
        table.DiscreteIntervalCount() != 67) {
        return Fail("unexpected table coverage counts");
    }

    for (const OW::WeaponSpec* spec = OW::WeaponSpecsBegin();
         spec != OW::WeaponSpecsEnd();
         ++spec) {
        const bool aimOnlyAction =
            OW::ResolveGeneratedFireKeyMask(spec, spec->action) == 0u &&
            OW::ResolveTrackingHoldMouseButton(spec, spec->action) ==
                OW::kWeaponControlNoButton;
        if (!aimOnlyAction && !table.Find(spec->weaponId)) {
            std::fprintf(stderr,
                         "WeaponCadenceSelfTest missing WeaponSpec join: %.*s\n",
                         static_cast<int>(spec->weaponId.size()),
                         spec->weaponId.data());
            return EXIT_FAILURE;
        }
    }

    if (!ExpectInterval(table, "ashe_the_viper_hip", 256.0f) ||
        !ExpectInterval(table, "ashe_take_aim_ads", 656.0f) ||
        !ExpectInterval(table, "baptiste_biotic_launcher", 588.0f) ||
        !ExpectInterval(table, "hanzo_storm_bow", 1220.0f) ||
        !ExpectInterval(table, "tracer_pulse_pistols", 50.0f) ||
        !ExpectInterval(table, "zenyatta_orb_of_destruction_alt", 3220.0f)) {
        return Fail("representative trigger-cycle interval mismatch");
    }

    const OW::WeaponCadenceEntry* zarya = table.Find("zarya_particle_cannon");
    if (!zarya || zarya->triggerCycleIntervalMs)
        return Fail("continuous beam was given a fabricated discrete interval");

    if (table.Find("anran_zhuque_fan") || table.Find("domina_particle_blaster"))
        return Fail("proposed or blocked rows entered the runtime join");

    const OW::WeaponCadenceTable& runtime = OW::RuntimeWeaponCadenceTable();
    if (!runtime.Loaded() || runtime.Size() != table.Size() ||
        !ExpectInterval(runtime, "cassidy_ads_perk", 500.0f)) {
        return Fail("runtime cadence discovery did not load the checked table");
    }

    std::puts("WeaponCadenceSelfTest PASS: 81 source rows, 75 runtime joins, 67 discrete intervals");
    return EXIT_SUCCESS;
}
