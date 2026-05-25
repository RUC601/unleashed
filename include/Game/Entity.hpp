#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <DirectXMath.h>
using namespace DirectX;

#include "Game/Structs.hpp"
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"

namespace OW {

    class c_entity {
    public:
        // --- Base addresses ---
        uint64_t address       = 0;
        uint64_t LinkBase      = 0;
        uint64_t HealthBase    = 0;
        uint64_t TeamBase      = 0;
        uint64_t VelocityBase  = 0;
        uint64_t HeroBase      = 0;
        uint64_t BoneBase      = 0;
        uint64_t OutlineBase   = 0;
        uint64_t SkillBase     = 0;
        uint64_t RotationBase  = 0;
        uint64_t VisBase       = 0;
        uint64_t AngleBase     = 0;
        uint64_t EnemyAngleBase = 0;
        uint64_t ObjectBase    = 0;
        uint64_t HeroID        = 0;
        uint64_t statcombase   = 0;

        int      head_index    = 0;
        uint32_t PlayerID      = 0;
        uint16_t Dva           = 0;

        std::string battletag;

        // --- Health ---
        float PlayerHealth      = 0.f;
        float PlayerHealthMax   = 0.f;
        float MinHealth         = 0.f;
        float MaxHealth         = 0.f;
        float MinArmorHealth    = 0.f;
        float MaxArmorHealth    = 0.f;
        float MinBarrierHealth  = 0.f;
        float MaxBarrierHealth  = 0.f;

        bool barrprot = false;
        bool imort    = false;

        // --- State ---
        float ULT    = 0.f;
        bool  Alive  = true;
        bool  Vis    = false;
        bool  Team   = false;
        bool  Trg    = false;

        bool   skill1act = false;
        bool   skill2act = false;
        float  ultimate  = 0.f;
        float  skillcd1  = 0.f;
        float  skillcd2  = 0.f;
        float  skillready = 0.f;

        // --- Positions ---
        Vector3 head_pos{};
        Vector3 velocity{};
        Vector3 Rot{};
        Vector3 pos{};
        Vector3 neck_pos{};
        Vector3 chest_pos{};

        // =====================================================================
        // Constructors / operators
        // =====================================================================
        c_entity() : address(0) {}
        explicit c_entity(uint64_t _UniqueID) : address(_UniqueID) {}

        bool operator==(const c_entity& entity) const {
            return (this->address == entity.address);
        }
        bool operator!=(const c_entity& entity) const {
            return (this->address != entity.address);
        }

        // =====================================================================
        // Team detection
        // =====================================================================
        eTeam GetTeam() {
            uint32_t teamBits = SDK->RPM<uint32_t>(this->TeamBase + 0x58) & 0x0F800000;
            std::bitset<sizeof(int) * CHAR_BIT> bitTeam(teamBits);
            if (bitTeam[0x17]) return eTeam::TEAM_RED;
            if (bitTeam[0x18]) return eTeam::TEAM_BLUE;
            if (bitTeam[0x19]) return eTeam::TEAM_UNKNOWN1;
            if (bitTeam[0x1A]) return eTeam::TEAM_UNKNOWN2;
            if (bitTeam[0x1B]) return eTeam::TEAM_DEATHMATCH;
            return eTeam::TEAM_RED;
        }

        // =====================================================================
        // Bone helpers
        // =====================================================================
        int get_bone_id(uint64_t bonedata, int bone_id) {
            __try {
                uint64_t bonePtr = SDK->RPM<uint64_t>(bonedata);
                uint32_t* v1 = (uint32_t*)SDK->RPM<uint64_t>(bonePtr + 0x38);
                uint16_t count = SDK->RPM<uint16_t>(bonePtr + 0x64);
                for (int i = 0; i < count; i++) {
                    if (SDK->RPM<uint16_t>((uint64_t)(v1 + i)) == bone_id) {
                        return i;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
            return 0;
        }

        Vector3 GetBonePos(int index) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t pBoneData = SDK->RPM<uint64_t>(this->VelocityBase + 0x8A0);
                    if (pBoneData) {
                        uint64_t bonesBase = SDK->RPM<uint64_t>(pBoneData + 0x20);
                        if (bonesBase) {
                            XMFLOAT3 currentBone = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * get_bone_id(pBoneData, index)) + 0x20
                            );
                            XMFLOAT3 Result{};
                            XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                            XMStoreFloat3(&Result, XMVector3Transform(XMLoadFloat3(&currentBone), rotMatrix));
                            if (this->HeroID == eHero::HERO_WRECKINGBALL) {
                                return Vector3(Result.x, Result.y - 0.7f, Result.z) + this->pos;
                            }
                            return Vector3(Result.x, Result.y, Result.z) + this->pos;
                        }
                    }
                }
                return Vector3{};
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return Vector3{};
            }
        }

        // =====================================================================
        // Skeleton definition per hero
        // =====================================================================
        std::array<int, 18> GetSkel() {
            switch (this->HeroID) {
                case eHero::HERO_ANA:
                case eHero::HERO_ASHE:
                case eHero::HERO_BAPTISTE:
                case eHero::HERO_BRIGITTE:
                case eHero::HERO_DOOMFIST:
                case eHero::HERO_ECHO:
                case eHero::HERO_GENJI:
                case eHero::HERO_HANJO:
                case eHero::HERO_JUNKRAT:
                case eHero::HERO_LUCIO:
                case eHero::HERO_MCCREE:
                case eHero::HERO_MERCY:
                case eHero::HERO_MOIRA:
                case eHero::HERO_PHARAH:
                case eHero::HERO_REAPER:
                case eHero::HERO_REINHARDT:
                case eHero::HERO_ROADHOG:
                case eHero::HERO_SIGMA:
                case eHero::HERO_SOLDIER76:
                case eHero::HERO_SOMBRA:
                case eHero::HERO_SYMMETRA:
                case eHero::HERO_TRACER:
                case eHero::HERO_VENTURE:
                case eHero::HERO_WIDOWMAKER:
                case eHero::HERO_WINSTON:
                case eHero::HERO_WRECKINGBALL:
                case eHero::HERO_ZARYA:
                case eHero::HERO_ZENYATTA:
                case eHero::HERO_LIFEWEAVER:
                case eHero::HERO_TRAININGBOT1:
                case eHero::HERO_TRAININGBOT2:
                case eHero::HERO_TRAININGBOT3:
                case eHero::HERO_TRAININGBOT4:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, BONE_R_ELBOW,
                            BONE_L_ANKLE, BONE_R_ANKLE,
                            BONE_L_SHANK, BONE_R_SHANK,
                            BONE_L_HAND, BONE_R_HAND,
                            99, 89, 100, 90};

                case eHero::HERO_BASTION:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, BONE_R_ELBOW,
                            85, 95, 89, 99,
                            BONE_L_HAND, 156,
                            99, 89, 100, 90};

                case eHero::HERO_DVA:
                    if (SDK->RPM<uint16_t>(this->LinkBase + 0xD4) !=
                        SDK->RPM<uint16_t>(this->LinkBase + 0xD8))
                        return {BONE_HEAD, BONE_NECK, 4, BONE_BODY_BOT,
                                80, 53, 27, 57,
                                85, 95, 89, 99,
                                153, 154, 101, 91, 101, 91};
                    else
                        return {BONE_HEAD, 16, 81, 82,
                                BONE_L_SHOULDER, BONE_R_SHOULDER,
                                BONE_L_ELBOW, BONE_R_ELBOW,
                                BONE_L_ANKLE, BONE_R_ANKLE,
                                BONE_L_SHANK, BONE_R_SHANK,
                                BONE_L_HAND, BONE_R_HAND,
                                99, 89, 100, 90};

                case eHero::HERO_MEI:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, 56,
                            BONE_L_ANKLE, BONE_R_ANKLE,
                            BONE_L_SHANK, BONE_R_SHANK,
                            BONE_L_HAND, 70,
                            99, 89, 100, 90};

                case eHero::HERO_ORISA:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, 56,
                            85, 95, 92, 102,
                            BONE_L_HAND, 58,
                            99, 89, 100, 90};

                case eHero::HERO_TORBJORN:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, BONE_R_ELBOW,
                            BONE_L_ANKLE, BONE_R_ANKLE,
                            BONE_L_SHANK, BONE_R_SHANK,
                            28, BONE_R_HAND,
                            99, 89, 100, 90};

                default:
                    return {BONE_HEAD, BONE_NECK, BONE_BODY, BONE_BODY_BOT,
                            BONE_L_SHOULDER, BONE_R_SHOULDER,
                            BONE_L_ELBOW, BONE_R_ELBOW,
                            BONE_L_ANKLE, BONE_R_ANKLE,
                            BONE_L_SHANK, BONE_R_SHANK,
                            BONE_L_HAND, BONE_R_HAND,
                            99, 89, 100, 90};
            }
        }

        // =====================================================================
        // 3D box corners
        // =====================================================================
        void Get3DBoxPos(Vector3& veca, Vector3& vecb, Vector3& vecc, Vector3& vecd,
                         Vector3& vece, Vector3& vecf, Vector3& vecg, Vector3& vech) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t pBoneData = SDK->RPM<uint64_t>(this->VelocityBase + 0x8A0);
                    if (pBoneData) {
                        uint64_t bonesBase = SDK->RPM<uint64_t>(pBoneData + 0x20);
                        if (bonesBase) {
                            XMFLOAT3 currentBone = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * get_bone_id(pBoneData, 17)) + 0x20
                            );
                            currentBone.y += 0.3f;
                            XMFLOAT3 a = {currentBone.x - 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 b = {currentBone.x - 0.5f, currentBone.y, currentBone.z + 0.5f};
                            XMFLOAT3 c = {currentBone.x + 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 d = {currentBone.x + 0.5f, currentBone.y, currentBone.z + 0.5f};
                            currentBone.y -= 1.5f;
                            XMFLOAT3 e = {currentBone.x - 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 f = {currentBone.x - 0.5f, currentBone.y, currentBone.z + 0.5f};
                            XMFLOAT3 g = {currentBone.x + 0.5f, currentBone.y, currentBone.z - 0.5f};
                            XMFLOAT3 h = {currentBone.x + 0.5f, currentBone.y, currentBone.z + 0.5f};

                            XMFLOAT3 Ra{}, Rb{}, Rc{}, Rd{}, Re{}, Rf{}, Rg{}, Rh{};
                            XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                            XMStoreFloat3(&Ra, XMVector3Transform(XMLoadFloat3(&a), rotMatrix));
                            XMStoreFloat3(&Rb, XMVector3Transform(XMLoadFloat3(&b), rotMatrix));
                            XMStoreFloat3(&Rc, XMVector3Transform(XMLoadFloat3(&c), rotMatrix));
                            XMStoreFloat3(&Rd, XMVector3Transform(XMLoadFloat3(&d), rotMatrix));
                            XMStoreFloat3(&Re, XMVector3Transform(XMLoadFloat3(&e), rotMatrix));
                            XMStoreFloat3(&Rf, XMVector3Transform(XMLoadFloat3(&f), rotMatrix));
                            XMStoreFloat3(&Rg, XMVector3Transform(XMLoadFloat3(&g), rotMatrix));
                            XMStoreFloat3(&Rh, XMVector3Transform(XMLoadFloat3(&h), rotMatrix));

                            float offY = (this->HeroID == eHero::HERO_WRECKINGBALL) ? -0.7f : 0.f;
                            veca = Vector3(Ra.x, Ra.y + offY, Ra.z) + this->pos;
                            vecb = Vector3(Rb.x, Rb.y + offY, Rb.z) + this->pos;
                            vecc = Vector3(Rc.x, Rc.y + offY, Rc.z) + this->pos;
                            vecd = Vector3(Rd.x, Rd.y + offY, Rd.z) + this->pos;
                            vece = Vector3(Re.x, Re.y + offY, Re.z) + this->pos;
                            vecf = Vector3(Rf.x, Rf.y + offY, Rf.z) + this->pos;
                            vecg = Vector3(Rg.x, Rg.y + offY, Rg.z) + this->pos;
                            vech = Vector3(Rh.x, Rh.y + offY, Rh.z) + this->pos;
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // =====================================================================
        // Eye ray (head-sight direction)
        // =====================================================================
        void GetEyeRayPoint(Vector3& veceye, Vector3& arrow1, Vector3& arrow2,
                            Vector3& arrow3, Vector3& arrow4) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t pBoneData = SDK->RPM<uint64_t>(this->VelocityBase + 0x8A0);
                    if (pBoneData) {
                        uint64_t bonesBase = SDK->RPM<uint64_t>(pBoneData + 0x20);
                        if (bonesBase) {
                            XMFLOAT3 b = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * get_bone_id(pBoneData, 17)) + 0x20
                            );
                            b = XMFLOAT3(b.x, b.y, b.z + 0.8f);
                            XMFLOAT3 b1 = {b.x + 0.2f, b.y,     b.z - 0.3f};
                            XMFLOAT3 b2 = {b.x - 0.2f, b.y,     b.z - 0.3f};
                            XMFLOAT3 b3 = {b.x,       b.y + 0.2f, b.z - 0.3f};
                            XMFLOAT3 b4 = {b.x,       b.y - 0.2f, b.z - 0.3f};

                            XMFLOAT3 R0{}, R1{}, R2{}, R3{}, R4{};
                            XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                            XMStoreFloat3(&R0, XMVector3Transform(XMLoadFloat3(&b), rotMatrix));
                            XMStoreFloat3(&R1, XMVector3Transform(XMLoadFloat3(&b1), rotMatrix));
                            XMStoreFloat3(&R2, XMVector3Transform(XMLoadFloat3(&b2), rotMatrix));
                            XMStoreFloat3(&R3, XMVector3Transform(XMLoadFloat3(&b3), rotMatrix));
                            XMStoreFloat3(&R4, XMVector3Transform(XMLoadFloat3(&b4), rotMatrix));

                            veceye  = Vector3(R0.x, R0.y, R0.z) + this->pos;
                            arrow1  = Vector3(R1.x, R1.y, R1.z) + this->pos;
                            arrow2  = Vector3(R2.x, R2.y, R2.z) + this->pos;
                            arrow3  = Vector3(R3.x, R3.y, R3.z) + this->pos;
                            arrow4  = Vector3(R4.x, R4.y, R4.z) + this->pos;
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }

        // =====================================================================
        // ESP bone list (projects to screen)
        // =====================================================================
        espBone getBoneList(std::array<int, 18> index) {
            __try {
                espBone a{};
                Vector2 pmin{}, pmax{}, w2s{};
                Vector3 root = this->pos;

                bool isBot = (HeroID == eHero::HERO_TRAININGBOT1 ||
                              HeroID == eHero::HERO_TRAININGBOT2 ||
                              HeroID == eHero::HERO_TRAININGBOT3 ||
                              HeroID == eHero::HERO_TRAININGBOT4 ||
                              HeroID == eHero::HERO_TRAININGBOT5 ||
                              HeroID == eHero::HERO_TRAININGBOT6 ||
                              HeroID == eHero::HERO_TRAININGBOT7);

                // For W2S we need the global viewMatrix, WX, WY.
                // These are accessed via the global variables declared in Overwatch.hpp.
                // We rely on the caller to have set up the extern viewMatrix and WX/WY.
                extern Matrix viewMatrix;
                extern float WX, WY;

                if (isBot) {
                    if (!viewMatrix.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    int botIndices[] = { 17, 16, 3, 13, 54 };
                    for (int i = 0; i < 5; i++) {
                        Vector3 bone = GetBonePos(botIndices[i]);
                        Vector2 ws{};
                        if (viewMatrix.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
                            if (i == 0) { a.head = ws; a.head.Y += 4.f; }
                            else if (i == 1) a.neck = ws;
                            else if (i == 2) a.body_1 = ws;
                            else if (i == 3) a.l_1 = ws;
                            else if (i == 4) a.r_1 = ws;
                            if (ws.X < pmin.X) pmin.X = ws.X;
                            if (ws.Y < pmin.Y) pmin.Y = ws.Y;
                            if (ws.X > pmax.X) pmax.X = ws.X;
                            if (ws.Y > pmax.Y) pmax.Y = ws.Y;
                        }
                    }
                } else {
                    if (!viewMatrix.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    for (int i = 0; i < 18; i++) {
                        Vector3 bone = GetBonePos(index[i]);
                        Vector2 ws{};
                        if (viewMatrix.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
                            if      (i == 0)  a.head = ws;
                            else if (i == 1)  a.neck = ws;
                            else if (i == 2)  a.body_1 = ws;
                            else if (i == 3)  a.body_2 = ws;
                            else if (i == 4)  a.l_1 = ws;
                            else if (i == 5)  a.r_1 = ws;
                            else if (i == 6)  a.l_d_1 = ws;
                            else if (i == 7)  a.r_d_1 = ws;
                            else if (i == 8)  a.l_a_1 = ws;
                            else if (i == 9)  a.r_a_1 = ws;
                            else if (i == 10) a.l_a_2 = ws;
                            else if (i == 11) a.r_a_2 = ws;
                            else if (i == 12) a.l_d_2 = ws;
                            else if (i == 13) a.r_d_2 = ws;
                            else if (i == 14) a.sex = ws;
                            else if (i == 15) a.sex1 = ws;
                            else if (i == 16) a.sex2 = ws;
                            else if (i == 17) a.sex3 = ws;
                            if (ws.X < pmin.X) pmin.X = ws.X;
                            if (ws.Y < pmin.Y) pmin.Y = ws.Y;
                            if (ws.X > pmax.X) pmax.X = ws.X;
                            if (ws.Y > pmax.Y) pmax.Y = ws.Y;
                        }
                    }
                }

                a.upL   = pmin;
                a.upR   = Vector2(pmax.X, pmin.Y);
                a.downL = Vector2(pmin.X, pmax.Y);
                a.downR = pmax;
                return a;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return espBone{};
            }
        }
    };

} // namespace OW
