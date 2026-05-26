#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <vector>
#include <bitset>
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
        HERO_REAPER        = 0x2E0000000000002,
        HERO_TRACER        = 0x2E0000000000003,
        HERO_MERCY         = 0x2E0000000000004,
        HERO_HANJO         = 0x2E0000000000005,
        HERO_TORBJORN      = 0x2E0000000000006,
        HERO_REINHARDT     = 0x2E0000000000007,
        HERO_PHARAH        = 0x2E0000000000008,
        HERO_WINSTON       = 0x2E0000000000009,
        HERO_WIDOWMAKER    = 0x2E000000000000A,
        HERO_BASTION       = 0x2E0000000000015,
        HERO_SYMMETRA      = 0x2E0000000000016,
        HERO_ZENYATTA      = 0x2E0000000000020,
        HERO_GENJI         = 0x2E0000000000029,
        HERO_ROADHOG       = 0x2E0000000000040,
        HERO_MCCREE        = 0x2E0000000000042,
        HERO_JUNKRAT       = 0x2E0000000000065,
        HERO_ZARYA         = 0x2E0000000000068,
        HERO_SOLDIER76     = 0x2E000000000006E,
        HERO_LUCIO         = 0x2E0000000000079,
        HERO_DVA           = 0x2E000000000007A,
        HERO_MEI           = 0x2E00000000000DD,
        HERO_ANA           = 0x2E000000000013B,
        HERO_SOMBRA        = 0x2E000000000012E,
        HERO_ORISA         = 0x2E000000000013E,
        HERO_DOOMFIST      = 0x2E000000000012F,
        HERO_MOIRA         = 0x2E00000000001A2,
        HERO_BRIGITTE      = 0x2E0000000000195,
        HERO_WRECKINGBALL  = 0x2E00000000001CA,
        HERO_SOJOURN       = 0x2E00000000001EC,
        HERO_ASHE          = 0x2E0000000000200,
        HERO_BAPTISTE      = 0x2E0000000000221,
        HERO_KIRIKO        = 0x2E0000000000231,
        HERO_JUNKERQUEEN   = 0x2E0000000000236,
        HERO_SIGMA         = 0x2E000000000023B,
        HERO_ECHO          = 0x2E0000000000206,
        HERO_RAMATTRA      = 0x2E000000000028D,
        HERO_TRAININGBOT1  = 0x2E000000000033C,
        HERO_TRAININGBOT2  = 0x2E0000000000337,
        HERO_TRAININGBOT3  = 0x2E000000000035A,
        HERO_TRAININGBOT4  = 0x2E000000000016C,
        HERO_TRAININGBOT5  = 0x2E0000000000363,
        HERO_TRAININGBOT6  = 0x2E0000000000349,
        HERO_TRAININGBOT7  = 0x2E0000000000339,
        HERO_LIFEWEAVER    = 0x02E0000000000291,
        HERO_ILLARI        = 0x02E000000000031C,
        HERO_MAUGA         = 0x02E000000000030A,
        HERO_VENTURE       = 0x2E000000000032B,
        TOBTERT            = 0x16DD,
        SYMTERT            = 0x16EE,
        Bob                = 0x16BB,
    };

    enum eComponentType {
        TYPE_ERROR            = -1,
        TYPE_VELOCITY         = 0x4,
        TYPE_TEAM             = 0x21,
        TYPE_BONE             = 0x24,  // updated per UC p330 (was 0x27)
        TYPE_ROTATION         = 0x2F,
        TYPE_LINK             = 0x34,
        TYPE_P_VISIBILITY     = 0x35,
        TYPE_SKILL            = 0x37,
        TYPE_ANGLE            = 0x39,
        TYPE_HEALTH           = 0x3B,
        TYPE_PLAYERCONTROLLER = 0x44,  // updated per UC p330 (was 0x43)
        TYPE_P_HEROID         = 0x54,
        // TYPE_OUTLINE = 0x5B  — REMOVED: DMA external cannot render outlines
        TYPE_STAT             = 0x25,
    };

    // =========================================================================
    // Component structs
    // =========================================================================

    struct health_compo_t {
        union {
            OFF_MEMB(float,  health,          0xE0);
            OFF_MEMB(float,  health_max,      0xDC);
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
