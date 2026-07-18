#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <bitset>
#include "Game/GameData.hpp"
#include "Utils/Types.hpp"

using DirectX::XMFLOAT3;

#define OFF_MEMB(type, name, offset)\
struct {\
    char zpad##name[offset];\
    type name;\
}

namespace OW {

    // =========================================================================
    // Types (Vector2, Vector3, Matrix, Color, Rect) are in Utils/Types.hpp
    // =========================================================================

    // =========================================================================
    // Enums
    // =========================================================================

    enum eTeam {
        TEAM_RED,
        TEAM_BLUE,
        TEAM_UNKNOWN1,
        TEAM_UNKNOWN2,
        TEAM_DEATHMATCH,
    };

    enum BONE : int {
        BONE_HEAD        = 17,
        BONE_NECK        = 16,
        BONE_CHEST       = 2,
        BONE_R_KNEE      = 99,
        BONE_R_SHANK     = 97,
        BONE_R_ANKLE     = 96,
        BONE_R_SHOULDER  = 54,
        BONE_R_ELBOW     = 51,
        BONE_R_HAND      = 71,
        BONE_L_KNEE      = 89,
        BONE_L_SHANK     = 87,
        BONE_L_ANKLE     = 86,
        BONE_L_SHOULDER  = 49,
        BONE_L_ELBOW     = 14,
        BONE_L_HAND      = 41,
        BONE_PELVIS      = 3,
        BONE_BODY        = 81,
        BONE_BODY_BOT    = 82,
    };

    enum eHero : uint64_t {
        HERO_REAPER        = GameData::MakeHeroId(0x002),
        HERO_TRACER        = GameData::MakeHeroId(0x003),
        HERO_MERCY         = GameData::MakeHeroId(0x004),
        HERO_HANJO         = GameData::MakeHeroId(0x005),
        HERO_HANZO         = HERO_HANJO,
        HERO_TORBJORN      = GameData::MakeHeroId(0x006),
        HERO_REINHARDT     = GameData::MakeHeroId(0x007),
        HERO_PHARAH        = GameData::MakeHeroId(0x008),
        HERO_WINSTON       = GameData::MakeHeroId(0x009),
        HERO_WIDOWMAKER    = GameData::MakeHeroId(0x00A),
        HERO_BASTION       = GameData::MakeHeroId(0x015),
        HERO_SYMMETRA      = GameData::MakeHeroId(0x016),
        HERO_ZENYATTA      = GameData::MakeHeroId(0x020),
        HERO_GENJI         = GameData::MakeHeroId(0x029),
        HERO_ROADHOG       = GameData::MakeHeroId(0x040),
        HERO_CASSIDY       = GameData::MakeHeroId(0x042),
        HERO_MCCREE        = HERO_CASSIDY,
        HERO_JUNKRAT       = GameData::MakeHeroId(0x065),
        HERO_ZARYA         = GameData::MakeHeroId(0x068),
        HERO_SOLDIER76     = GameData::MakeHeroId(0x06E),
        HERO_LUCIO         = GameData::MakeHeroId(0x079),
        HERO_DVA           = GameData::MakeHeroId(0x07A),
        HERO_MEI           = GameData::MakeHeroId(0x0DD),
        HERO_SOMBRA        = GameData::MakeHeroId(0x12E),
        HERO_DOOMFIST      = GameData::MakeHeroId(0x12F),
        HERO_ANA           = GameData::MakeHeroId(0x13B),
        HERO_ORISA         = GameData::MakeHeroId(0x13E),
        HERO_BRIGITTE      = GameData::MakeHeroId(0x195),
        HERO_MOIRA         = GameData::MakeHeroId(0x1A2),
        HERO_WRECKINGBALL  = GameData::MakeHeroId(0x1CA),
        HERO_SOJOURN       = GameData::MakeHeroId(0x1EC),
        HERO_ASHE          = GameData::MakeHeroId(0x200),
        HERO_ECHO          = GameData::MakeHeroId(0x206),
        HERO_BAPTISTE      = GameData::MakeHeroId(0x221),
        HERO_KIRIKO        = GameData::MakeHeroId(0x231),
        HERO_JUNKERQUEEN   = GameData::MakeHeroId(0x236),
        HERO_SIGMA         = GameData::MakeHeroId(0x23B),
        HERO_RAMATTRA      = GameData::MakeHeroId(0x28D),
        HERO_LIFEWEAVER    = GameData::MakeHeroId(0x291),
        HERO_MAUGA         = GameData::MakeHeroId(0x30A),
        HERO_ILLARI        = GameData::MakeHeroId(0x31C),
        HERO_FREJA         = GameData::MakeHeroId(0x32A),
        HERO_VENTURE       = GameData::MakeHeroId(0x32B),
        HERO_HAZARD        = GameData::MakeHeroId(0x362),
        HERO_JUNO          = GameData::MakeHeroId(0x365),
        HERO_WUYANG        = GameData::MakeHeroId(0x3C3),
        HERO_VENDETTA      = GameData::MakeHeroId(0x472),
        HERO_SIERRA        = GameData::MakeHeroId(0x4D2),
        HERO_EMRE          = GameData::MakeHeroId(0x4D8),
        HERO_ANRAN         = GameData::MakeHeroId(0x4DD),
        HERO_MIZUKI        = GameData::MakeHeroId(0x4E3),
        HERO_JETPACKCAT    = GameData::MakeHeroId(0x516),
        HERO_SHION         = GameData::MakeHeroId(0x52C),
        HERO_TRAININGBOT1  = GameData::MakeHeroId(0x33C),
        HERO_TRAININGBOT2  = GameData::MakeHeroId(0x337),
        HERO_TRAININGBOT3  = GameData::MakeHeroId(0x35A),
        HERO_TRAININGBOT4  = GameData::MakeHeroId(0x4E7),
        HERO_TRAININGBOT5  = GameData::MakeHeroId(0x363),
        HERO_TRAININGBOT6  = GameData::MakeHeroId(0x349),
        HERO_TRAININGBOT7  = GameData::MakeHeroId(0x339),
        TOBTERT            = 0x16DD,
        SYMTERT            = 0x16EE,
        Bob                = 0x16BB,
    };

    enum eComponentType {
        TYPE_ERROR            = -1,
        TYPE_TRANSFORM        = 0x01,
        TYPE_VELOCITY         = 0x4,
        // Legacy 151177 values kept for source compatibility. Runtime code
        // must use offset::Active().type* so BZ and CN/NE can diverge.
        TYPE_TEAM             = 0x21,
        TYPE_BONE             = 0x24,  // updated per UC p330 (was 0x27)
        TYPE_TRANSFORM_ALT    = 0x25,
        TYPE_ROTATION         = 0x2F,
        TYPE_LINK             = 0x34,  // legacy 151177 id; active BZ/NE 151752 profiles use 0x33
        TYPE_P_VISIBILITY     = 0x34,  // active BZ/NE 151752 id; +0x98 semantics are profile-specific
        TYPE_SKILL            = 0x37,
        TYPE_ANGLE            = 0x39,
        TYPE_HEALTH           = 0x3B,
        TYPE_PLAYERCONTROLLER = 0x43,  // corrected per plexies p332 (was 0x44)
        TYPE_P_HEROID         = 0x54,
        TYPE_OUTLINE          = 0x5A,
        TYPE_REPLICATED_POS   = 0x60,
        TYPE_STAT             = TYPE_TRANSFORM_ALT,
    };

    // =========================================================================
    // Component structs
    // =========================================================================

    struct health_compo_t {
        union {
            OFF_MEMB(float,  health,          0xE0);
            OFF_MEMB(float,  health_max,      0xDC);
            OFF_MEMB(Vector2, health_ext,     0xF0);
            OFF_MEMB(float,  armor,           0x220);
            OFF_MEMB(float,  armor_max,       0x21C);
            OFF_MEMB(float,  barrier,         0x360);
            OFF_MEMB(float,  barrier_max,     0x35C);
            OFF_MEMB(bool,   isImmortal,      0x4A9);
            OFF_MEMB(bool,   isBarrierProjected, 0x4A8);
        };
    };

    struct obj_compo_t {
        union {
            OFF_MEMB(XMFLOAT3, obj_pos, 0xE0);
        };
    };

    struct velocity_compo_t {
        union {
            OFF_MEMB(XMFLOAT3,  velocity, 0x50);
            OFF_MEMB(XMFLOAT3,  location, 0x1B0 + 0x50);
            OFF_MEMB(uint64_t,  bonedata, 0x8B0);
        };
    };

    struct hero_compo_t {
        union {
            OFF_MEMB(uint64_t, heroid, 0xD0);
        };
    };

    struct vis_compo_t {
        union {
            OFF_MEMB(uint64_t, key1, 0xA0);
            OFF_MEMB(uint64_t, key2, 0x98);
        };
    };

    // =========================================================================
    // ESP helper types
    // =========================================================================

    struct espBone {
        bool boneerror = false;
        Vector2 upL, upR, downL, downR;
        Vector2 head, neck, body_1, body_2,
                l_1, l_2, r_1, r_2,
                l_d_1, l_d_2, r_d_1, r_d_2,
                l_a_1, l_a_2, r_a_1, r_a_2,
                sex, sex1, sex2, sex3;
    };

    class hpanddy {
    public:
        uint64_t  entityid = 0;
        uintptr_t MeshBase = 0;
        XMFLOAT3  POS = {};
    };

} // namespace OW
