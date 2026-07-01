#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace OW::GameData {

struct NamedU64 {
    const char* name;
    uint64_t value;
};

struct NamedInt {
    const char* name;
    int value;
};

inline constexpr uint64_t kHeroIdPrefix = 0x02E0000000000000ull;
inline constexpr uint64_t kHeroIdPrefixMask = 0xFFFF000000000000ull;

constexpr uint64_t MakeHeroId(uint16_t lowId) noexcept {
    return kHeroIdPrefix | static_cast<uint64_t>(lowId);
}

constexpr bool HasHeroIdPrefix(uint64_t heroId) noexcept {
    return (heroId & kHeroIdPrefixMask) == kHeroIdPrefix;
}

// Source: D:\Desktop\SenseZen\ECS_O\02_RESEARCH\forum-capture\grap\516727\332.txt, plexies, 2026-05-27 04:18 AM.
// Live CN probe additions, 2026-06-02: Vendetta/Anran.
// Local research additions, 2026-06-07: Sierra/Emre.
// Live entity probe additions, 2026-06-16: Mizuki/Shion.
inline constexpr NamedU64 kHeroIds[] = {
    { "Reaper", MakeHeroId(0x002) },
    { "Tracer", MakeHeroId(0x003) },
    { "Mercy", MakeHeroId(0x004) },
    { "Hanzo", MakeHeroId(0x005) },
    { "Torbjorn", MakeHeroId(0x006) },
    { "Reinhardt", MakeHeroId(0x007) },
    { "Pharah", MakeHeroId(0x008) },
    { "Winston", MakeHeroId(0x009) },
    { "Widowmaker", MakeHeroId(0x00A) },
    { "Bastion", MakeHeroId(0x015) },
    { "Symmetra", MakeHeroId(0x016) },
    { "Zenyatta", MakeHeroId(0x020) },
    { "Genji", MakeHeroId(0x029) },
    { "Roadhog", MakeHeroId(0x040) },
    { "Cassidy", MakeHeroId(0x042) },
    { "Junkrat", MakeHeroId(0x065) },
    { "Zarya", MakeHeroId(0x068) },
    { "Soldier 76", MakeHeroId(0x06E) },
    { "Lucio", MakeHeroId(0x079) },
    { "D.Va", MakeHeroId(0x07A) },
    { "Mei", MakeHeroId(0x0DD) },
    { "Sombra", MakeHeroId(0x12E) },
    { "Doomfist", MakeHeroId(0x12F) },
    { "Ana", MakeHeroId(0x13B) },
    { "Orisa", MakeHeroId(0x13E) },
    { "Brigitte", MakeHeroId(0x195) },
    { "Moira", MakeHeroId(0x1A2) },
    { "Wrecking Ball", MakeHeroId(0x1CA) },
    { "Sojourn", MakeHeroId(0x1EC) },
    { "Ashe", MakeHeroId(0x200) },
    { "Echo", MakeHeroId(0x206) },
    { "Baptiste", MakeHeroId(0x221) },
    { "Kiriko", MakeHeroId(0x231) },
    { "Junker Queen", MakeHeroId(0x236) },
    { "Sigma", MakeHeroId(0x23B) },
    { "Ramattra", MakeHeroId(0x28D) },
    { "Lifeweaver", MakeHeroId(0x291) },
    { "Mauga", MakeHeroId(0x30A) },
    { "Illari", MakeHeroId(0x31C) },
    { "Freja", MakeHeroId(0x32A) },
    { "Venture", MakeHeroId(0x32B) },
    { "Hazard", MakeHeroId(0x362) },
    { "Juno", MakeHeroId(0x365) },
    { "Wuyang", MakeHeroId(0x3C3) },
    { "Vendetta", MakeHeroId(0x472) },
    { "Sierra", MakeHeroId(0x4D2) },
    { "Emre", MakeHeroId(0x4D8) },
    { "Anran", MakeHeroId(0x4DD) },
    { "Mizuki", MakeHeroId(0x4E3) },
    { "Jetpack Cat", MakeHeroId(0x516) },
    { "Shion", MakeHeroId(0x52C) },
    { "Standard Bot", MakeHeroId(0x33C) },
    { "Tank Bot", MakeHeroId(0x337) },
    { "Sniper Bot", MakeHeroId(0x35A) },
    { "Training Bot 4", MakeHeroId(0x16C) },
    { "Friendly Bot", MakeHeroId(0x4E7) },
    { "Friendly Heavy Bot", MakeHeroId(0x363) },
    { "Training Bot 6", MakeHeroId(0x349) },
    { "Training Bot 7", MakeHeroId(0x339) },
};

inline constexpr NamedInt kBoneIds[] = {
    { "HEAD", 17 },
    { "NECK", 16 },
    { "CHEST", 2 },
    { "PELVIS", 3 },
    { "BODY", 81 },
    { "BODY_BOT", 82 },
    { "R_SHOULDER", 54 },
    { "R_ELBOW", 51 },
    { "R_HAND", 71 },
    { "L_SHOULDER", 49 },
    { "L_ELBOW", 14 },
    { "L_HAND", 41 },
    { "R_KNEE", 99 },
    { "R_SHANK", 97 },
    { "R_ANKLE", 96 },
    { "L_KNEE", 89 },
    { "L_SHANK", 87 },
    { "L_ANKLE", 86 },
};

inline constexpr NamedU64 kPoolIds[] = {
    { "HealthPack_Small", 0x40000000000005Full },
    { "HealthPack_Large", 0x400000000000060ull },
    { "HealthPack_Mega", 0x40000000000480Aull },
    { "Torb_Turret", 0x4000000000028B7ull },
    { "Illari_Pylon", 0x400000000004BCAull },
    { "Baptiste_Immortality", 0x4000000000029A9ull },
    { "Lifeweaver_Platform", 0x400000000004504ull },
    { "Symmetra_Sentry", 0x400000000002658ull },
    { "Symmetra_TpStart", 0x4000000000026F2ull },
    { "Symmetra_TpEnd", 0x40000000000272Dull },
    { "Ashe_BOB", 0x4000000000025C3ull },
    { "Junkrat_Trap", 0x40000000000103Bull },
    { "Mei_Wall", 0x400000000000932ull },
    { "Mei_Blizzard", 0x400000000000946ull },
    { "WreckingBall_Mine", 0x400000000002578ull },
    { "Widow_VenomMine", 0x4000000000000AEull },
    { "Soldier_BioticField", 0x40000000000068Aull },
    { "Junkrat_HeroId", 0x400000000000380ull },
    { "Junkrat_ConcMine", 0x400000000000F9Bull },
    { "Hazard_Wall", 0x400000000005920ull },
    { "Winston_Barrier", 0x4000000000001C3ull },
    { "Orisa_Barrier", 0x4000000000014A1ull },
    { "Domina_Barrier", 0x40000000000679Full },
    { "Ramattra_Barrier", 0x40000000000343Full },
    { "Ramattra_Vortex", 0x400000000003523ull },
    { "Reinhardt_Barrier", 0x40000000000011Aull },
    { "Brigitte_Barrier", 0x400000000002284ull },
    { "Sigma_Barrier", 0x400000000002D25ull },
    { "Zarya_BarrierSelf", 0x4000000000013F4ull },
    { "Zarya_BarrierOther", 0x4000000000003DAull },
    { "Sierra_AnchorDrone", 0x400000000007196ull },
    { "Juno_HyperRing", 0x400000000005530ull },
    { "Sierra_Trailblazer", 0x400000000007255ull },
    { "Mizuki_Sanctuary", 0x400000000006D24ull },
    { "Ashe_Dynamite", 0x400000000002533ull },
    { "Ana_AntiHeal", 0x400000000001426ull },
    { "Ana_SleepDart", 0x4000000000015E8ull },
    { "Sigma_Accretion", 0x4000000000018B7ull },
    { "Bastion_Grenade", 0x400000000003F69ull },
    { "Bastion_ArtShell", 0x400000000003EFDull },
    { "Tracer_PulseBomb", 0x400000000000045ull },
    { "Hanzo_Arrow", 0x40000000000020Bull },
    { "Hanzo_ReconDart", 0x40000000000020Dull },
    { "Hanzo_StormArrow", 0x40000000000270Full },
    { "Hanzo_FrostArrow", 0x400000000006EDBull },
    { "Hanzo_ScatterArrow", 0x400000000003749ull },
    { "Hanzo_Dragonstrike", 0x40000000000270Full },
    { "JQ_JaggedBlade", 0x400000000003555ull },
    { "DVa_MicroMissile", 0x400000000001E8Aull },
    { "Soldier_HelixRocket", 0x40000000000072Cull },
    { "Freja_Bolt", 0x400000000004AD3ull },
    { "Freja_ExploBolt", 0x4000000000056B6ull },
    { "Freja_BolaShot", 0x400000000005E3Dull },
    { "Pharah_Rocket", 0x400000000000033ull },
    { "Pharah_ConcBlast", 0x40000000000010Full },
    { "Baptiste_HealProj", 0x4000000000029ADull },
    { "Kiriko_Suzu", 0x400000000003FD3ull },
    { "Kiriko_Kunai", 0x400000000004103ull },
    { "Illari_CaptiveSun", 0x400000000004AC0ull },
    { "Junkrat_Frag", 0x400000000000385ull },
    { "Roadhog_Hook", 0x4000000000003F7ull },
    { "Orisa_Javelin", 0x400000000004147ull },
    { "Genji_Shuriken", 0x4000000000007CAull },
    { "Cassidy_Flashbang", 0x40000000000037Dull },
    { "Venture_Seismic", 0x400000000004D90ull },
    { "Sombra_Translocator", 0x400000000001575ull },
    { "Mercy_Resurrect", 0x40000000000014Full },
    { "Reaper_ShadowStep", 0x40000000000019Dull },
    { "Ping_Marker", 0x4000000000043A0ull },
};

inline constexpr float kSanityMaxWorldCoord = 100000.0f;
inline constexpr float kSanityViewMatrixDetTol = 0.5f;
inline constexpr uint64_t kSanityMinHeapPtr = 0x10000ull;
inline constexpr uint64_t kSanityMaxHeapPtr = 0x00007FFFFFFFFFFFull;

template <std::size_t N>
constexpr const char* FindName(const NamedU64 (&items)[N], uint64_t value) noexcept {
    for (const NamedU64& item : items) {
        if (item.value == value)
            return item.name;
    }
    return "";
}

template <std::size_t N>
constexpr bool Contains(const NamedU64 (&items)[N], uint64_t value) noexcept {
    for (const NamedU64& item : items) {
        if (item.value == value)
            return true;
    }
    return false;
}

constexpr const char* HeroName(uint64_t heroId) noexcept {
    return FindName(kHeroIds, heroId);
}

inline std::string UnknownHeroFallbackName(uint64_t heroId) {
    char fallback[24] = {};
    std::snprintf(fallback, sizeof(fallback), "BzHero_%04llX",
        static_cast<unsigned long long>(heroId & 0xFFFFull));
    return fallback;
}

constexpr bool IsKnownHeroId(uint64_t heroId) noexcept {
    return Contains(kHeroIds, heroId);
}

constexpr bool IsKnownPoolId(uint64_t poolId) noexcept {
    return Contains(kPoolIds, poolId);
}

constexpr bool IsTrainingBotHeroId(uint64_t heroId) noexcept {
    return heroId == MakeHeroId(0x33C) ||
           heroId == MakeHeroId(0x337) ||
           heroId == MakeHeroId(0x35A) ||
           heroId == MakeHeroId(0x16C) ||
           heroId == MakeHeroId(0x4E7) ||
           heroId == MakeHeroId(0x363) ||
           heroId == MakeHeroId(0x349) ||
           heroId == MakeHeroId(0x339);
}

constexpr bool IsFriendlyTrainingBotHeroId(uint64_t heroId) noexcept {
    return heroId == MakeHeroId(0x4E7) ||
           heroId == MakeHeroId(0x363);
}

} // namespace OW::GameData
