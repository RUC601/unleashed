#pragma once
#include <cstdint>
#include <array>
#include <string>
#include <cmath>
#include <cstring>
#include <mutex>
#include <DirectXMath.h>
using namespace DirectX;

#include "Game/BoneSlots.hpp"
#include "Game/Structs.hpp"
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"
#include "Utils/Diagnostics.hpp"

namespace OW {

    enum class EntityRosterState : uint8_t {
        Fresh = 0,
        Missing,
        Dead
    };

    class c_entity {
    public:
        // --- Base addresses ---
        uint64_t address       = 0;
        uint64_t LinkParent    = 0;
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
        EntityRosterState roster_state = EntityRosterState::Fresh;
        uint64_t roster_key = 0;
        uint32_t match_id = 0;
        uint32_t last_seen_tick_ms = 0;
        uint32_t missing_since_tick_ms = 0;

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
        std::array<Vector3, 18> skeleton_bones{};
        std::array<bool, 18> skeleton_bone_valid{};
        Vector3 cached_bot_chest_bone{};
        bool cached_bot_chest_bone_valid = false;

        // Render interpolation samples. The processing thread writes the current
        // positions and carries the previous published positions for the renderer.
        uint32_t render_sample_tick_ms = 0;
        uint32_t previous_render_sample_tick_ms = 0;
        uint32_t position_sample_tick_ms = 0;
        bool has_previous_render_sample = false;
        Vector3 previous_head_pos{};
        Vector3 previous_velocity{};
        Vector3 previous_pos{};
        Vector3 previous_neck_pos{};
        Vector3 previous_chest_pos{};
        std::array<Vector3, 18> previous_skeleton_bones{};
        std::array<bool, 18> previous_skeleton_bone_valid{};
        Vector3 previous_cached_bot_chest_bone{};
        bool previous_cached_bot_chest_bone_valid = false;

        uint64_t bone_fallback_logged_offset = UINT64_MAX;
        bool debugHeadLookupAttempted = false;
        bool debugHeadLookupResolved = false;
        bool debugHeadIdFound = false;
        bool debugHeadLocalFinite = false;
        bool debugHeadLocalNonZero = false;
        bool debugHeadWorldNonZero = false;
        bool debugHeadLookupException = false;
        uint64_t debugHeadBoneData = 0;
        uint64_t debugHeadBonesBase = 0;
        uint64_t debugHeadBonePtr = 0;
        uint64_t debugHeadBoneIdTable = 0;
        uint64_t debugHeadLocalAddress = 0;
        uint16_t debugHeadBoneCount = 0;
        int debugHeadMappedIndex = -1;
        XMFLOAT3 debugHeadLocal{};
        Vector3 debugHeadWorld{};

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
        uint32_t GetTeamFlags() const {
            if (!this->TeamBase)
                return 0;
            return SDK->RPM<uint32_t>(this->TeamBase + OW::offset::Team_FlagsOffset);
        }

        uint32_t GetTeamComparisonKey() const {
            return OW::offset::TeamComparisonKeyFromFlags(GetTeamFlags());
        }

        bool SameTeamAs(const c_entity& other) const {
            if (OW::offset::IsCnNeProfile()) {
                const uint32_t selfKey = GetTeamComparisonKey();
                const uint32_t otherKey = other.GetTeamComparisonKey();
                if (selfKey != 0 && otherKey != 0)
                    return selfKey == otherKey;
            }
            return GetTeam() == other.GetTeam();
        }

        eTeam GetTeam() const {
            uint32_t teamBits = GetTeamFlags() & OW::offset::Team_LegacyMask;
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
        static constexpr uint16_t kMaxBoneIdCount = 1024;

        struct BoneDebugProbe {
            int requestedBoneId = 0;
            bool preconditions = false;
            bool resolved = false;
            bool idFound = false;
            bool localFinite = false;
            bool localNonZero = false;
            bool worldNonZero = false;
            bool exception = false;
            uint64_t boneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint64_t localAddress = 0;
            uint16_t boneCount = 0;
            int mappedIndex = -1;
            XMFLOAT3 local{};
            Vector3 world{};
        };

        struct BoneResolveInfo {
            uint64_t boneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint16_t boneCount = 0;
        };

        struct SkeletonBoneCache {
            bool valid = false;
            uint64_t heroId = 0;
            uint64_t boneData = 0;
            uint64_t bonesBase = 0;
            uint64_t bonePtr = 0;
            uint64_t boneIdTable = 0;
            uint16_t boneCount = 0;
            std::array<int, 18> requestedBoneIds{};
            std::array<int, 18> mappedIndices{};
            int maxMappedIndex = -1;
            int botChestMappedIndex = -1;
            bool hasBotChest = false;
        };

        static bool IsFiniteBoneValue(const XMFLOAT3& value) {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        static bool IsNonZeroBoneValue(const XMFLOAT3& value) {
            return value.x != 0.0f || value.y != 0.0f || value.z != 0.0f;
        }

        static bool IsFiniteVectorValue(const Vector3& value) {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        int get_bone_id(uint64_t bonedata, int bone_id) {
            const bool debugHead = bone_id == OW::BONE_HEAD;
            if (debugHead) {
                debugHeadLookupAttempted = true;
                debugHeadLookupResolved = false;
                debugHeadIdFound = false;
                debugHeadLookupException = false;
                debugHeadBoneData = bonedata;
                debugHeadBonesBase = 0;
                debugHeadBonePtr = 0;
                debugHeadBoneIdTable = 0;
                debugHeadBoneCount = 0;
                debugHeadMappedIndex = -1;
                debugHeadLocalAddress = 0;
                debugHeadLocal = {};
                debugHeadWorld = {};
                debugHeadLocalFinite = false;
                debugHeadLocalNonZero = false;
                debugHeadWorldNonZero = false;
            }
            __try {
                uint64_t bonePtr = SDK->RPM<uint64_t>(bonedata);
                if (debugHead)
                    debugHeadBonePtr = bonePtr;
                if (!bonePtr)
                    return -1;

                uint64_t boneIdTable = SDK->RPM<uint64_t>(bonePtr + 0x38);
                if (debugHead)
                    debugHeadBoneIdTable = boneIdTable;
                if (!boneIdTable)
                    return -1;

                uint16_t count = SDK->RPM<uint16_t>(bonePtr + 0x64);
                if (debugHead)
                    debugHeadBoneCount = count;
                if (count == 0 || count > kMaxBoneIdCount)
                    return -1;

                if (debugHead)
                    debugHeadLookupResolved = true;

                std::array<uint32_t, kMaxBoneIdCount> boneIds{};
                if (!SDK->read_range(boneIdTable, boneIds.data(), sizeof(uint32_t) * count))
                    return -1;

                for (uint16_t i = 0; i < count; i++) {
                    if (static_cast<uint16_t>(boneIds[i]) == bone_id) {
                        if (debugHead) {
                            debugHeadIdFound = true;
                            debugHeadMappedIndex = static_cast<int>(i);
                        }
                        return i;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                if (debugHead)
                    debugHeadLookupException = true;
            }
            return -1;
        }

        bool ResolveBoneDataCandidate(uint64_t pBoneData, BoneResolveInfo& info) {
            info = {};
            if (!pBoneData)
                return false;

            info.boneData = pBoneData;
            info.bonesBase = SDK->RPM<uint64_t>(pBoneData + 0x20);
            if (!info.bonesBase)
                return false;

            info.bonePtr = SDK->RPM<uint64_t>(pBoneData);
            if (!info.bonePtr)
                return false;

            info.boneIdTable = SDK->RPM<uint64_t>(info.bonePtr + 0x38);
            if (!info.boneIdTable)
                return false;

            info.boneCount = SDK->RPM<uint16_t>(info.bonePtr + 0x64);
            return info.boneCount > 0 && info.boneCount <= kMaxBoneIdCount;
        }

        bool ResolveBoneDataCandidate(uint64_t pBoneData, uint64_t& bonesBase) {
            BoneResolveInfo info{};
            const bool resolved = ResolveBoneDataCandidate(pBoneData, info);
            bonesBase = info.bonesBase;
            return resolved;
        }

        BoneResolveInfo ResolveBoneDataInfo(uint64_t knownVelocityBoneData = 0) {
            BoneResolveInfo info{};
            if (this->VelocityBase) {
                const uint64_t pBoneData = knownVelocityBoneData
                    ? knownVelocityBoneData
                    : SDK->RPM<uint64_t>(this->VelocityBase + 0x8B0);
                if (ResolveBoneDataCandidate(pBoneData, info))
                    return info;
            }

            if (!this->BoneBase)
                return {};

            constexpr std::array<uint64_t, 3> entryOffsets = { 0x20, 0x0, 0x10 };
            for (const uint64_t entryOffset : entryOffsets) {
                const uint64_t pBoneData = SDK->RPM<uint64_t>(this->BoneBase + entryOffset);
                if (!ResolveBoneDataCandidate(pBoneData, info))
                    continue;

                if (this->bone_fallback_logged_offset != entryOffset) {
                    Diagnostics::Info(
                        "[BONE] BoneBase fallback resolved entry=0x%llX bone_base=0x%llX bonedata=0x%llX bones_base=0x%llX.",
                        static_cast<unsigned long long>(entryOffset),
                        static_cast<unsigned long long>(this->BoneBase),
                        static_cast<unsigned long long>(info.boneData),
                        static_cast<unsigned long long>(info.bonesBase));
                    this->bone_fallback_logged_offset = entryOffset;
                }
                return info;
            }

            return {};
        }

        uint64_t ResolveBoneData(uint64_t& bonesBase) {
            const BoneResolveInfo info = ResolveBoneDataInfo();
            bonesBase = info.bonesBase;
            return info.boneData;
        }

        BoneDebugProbe ProbeBone(int boneId) {
            BoneDebugProbe probe{};
            probe.requestedBoneId = boneId;
            __try {
                probe.preconditions = this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0;
                if (!probe.preconditions)
                    return probe;

                probe.boneData = ResolveBoneData(probe.bonesBase);
                if (!probe.boneData || !probe.bonesBase)
                    return probe;

                probe.bonePtr = SDK->RPM<uint64_t>(probe.boneData);
                if (!probe.bonePtr)
                    return probe;

                probe.boneIdTable = SDK->RPM<uint64_t>(probe.bonePtr + 0x38);
                if (!probe.boneIdTable)
                    return probe;

                probe.boneCount = SDK->RPM<uint16_t>(probe.bonePtr + 0x64);
                if (probe.boneCount == 0 || probe.boneCount > kMaxBoneIdCount)
                    return probe;

                probe.resolved = true;
                std::array<uint32_t, kMaxBoneIdCount> boneIds{};
                if (!SDK->read_range(probe.boneIdTable, boneIds.data(), sizeof(uint32_t) * probe.boneCount))
                    return probe;

                for (uint16_t i = 0; i < probe.boneCount; ++i) {
                    if (static_cast<uint16_t>(boneIds[i]) == boneId) {
                        probe.idFound = true;
                        probe.mappedIndex = static_cast<int>(i);
                        break;
                    }
                }

                if (!probe.idFound)
                    return probe;

                probe.localAddress = probe.bonesBase + (0x30 * static_cast<uint64_t>(probe.mappedIndex)) + 0x20;
                probe.local = SDK->RPM<XMFLOAT3>(probe.localAddress);
                probe.localFinite = IsFiniteBoneValue(probe.local);
                probe.localNonZero = probe.localFinite && IsNonZeroBoneValue(probe.local);
                if (!probe.localFinite)
                    return probe;

                XMFLOAT3 result{};
                XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                XMStoreFloat3(&result, XMVector3Transform(XMLoadFloat3(&probe.local), rotMatrix));
                if (this->HeroID == eHero::HERO_WRECKINGBALL)
                    probe.world = Vector3(result.x, result.y - 0.7f, result.z) + this->pos;
                else
                    probe.world = Vector3(result.x, result.y, result.z) + this->pos;
                probe.worldNonZero = IsFiniteVectorValue(probe.world) && probe.world != Vector3(0, 0, 0);
                return probe;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                probe.exception = true;
                return probe;
            }
        }

        Vector3 GetBonePos(int index) {
            __try {
                if (index < 0)
                    return Vector3{};

                if (index == 83 && cached_bot_chest_bone_valid)
                    return cached_bot_chest_bone;

                const std::array<int, 18> cachedIndices = GetRenderSkel();
                for (size_t i = 0; i < cachedIndices.size(); ++i) {
                    if (cachedIndices[i] == index && skeleton_bone_valid[i])
                        return skeleton_bones[i];
                }

                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t bonesBase = 0;
                    uint64_t pBoneData = ResolveBoneData(bonesBase);
                    if (index == OW::BONE_HEAD) {
                        debugHeadLookupAttempted = true;
                        debugHeadBoneData = pBoneData;
                        debugHeadBonesBase = bonesBase;
                    }
                    if (pBoneData) {
                        if (bonesBase) {
                            const int mappedBoneIndex = get_bone_id(pBoneData, index);
                            if (mappedBoneIndex < 0)
                                return Vector3{};
                            const uint64_t boneAddress = bonesBase + (0x30 * static_cast<uint64_t>(mappedBoneIndex)) + 0x20;
                            XMFLOAT3 currentBone = SDK->RPM<XMFLOAT3>(
                                boneAddress
                            );
                            XMFLOAT3 Result{};
                            XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                            XMStoreFloat3(&Result, XMVector3Transform(XMLoadFloat3(&currentBone), rotMatrix));
                            Vector3 worldBone{};
                            if (this->HeroID == eHero::HERO_WRECKINGBALL) {
                                worldBone = Vector3(Result.x, Result.y - 0.7f, Result.z) + this->pos;
                            } else {
                                worldBone = Vector3(Result.x, Result.y, Result.z) + this->pos;
                            }
                            if (index == OW::BONE_HEAD) {
                                debugHeadLocalAddress = boneAddress;
                                debugHeadLocal = currentBone;
                                debugHeadLocalFinite = IsFiniteBoneValue(currentBone);
                                debugHeadLocalNonZero = debugHeadLocalFinite && IsNonZeroBoneValue(currentBone);
                                debugHeadWorld = worldBone;
                                debugHeadWorldNonZero = IsFiniteVectorValue(worldBone) && worldBone != Vector3(0, 0, 0);
                            }
                            return worldBone;
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
                case eHero::HERO_TRAININGBOT5:
                case eHero::HERO_TRAININGBOT6:
                case eHero::HERO_JETPACKCAT:
                case eHero::HERO_TRAININGBOT7:
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

        // Overlay rendering uses the current 16-slot skeleton map; GetSkel remains for legacy aim paths.
        std::array<int, 18> GetRenderSkel() const {
            return OW::Plexies20260609::ResolveRenderSkeletonMap(this->HeroID);
        }

        void CacheSkeletonBones(uint64_t knownVelocityBoneData = 0) {
            CacheSkeletonBonesCached(nullptr, knownVelocityBoneData);
        }

        void CacheSkeletonBones(SkeletonBoneCache& cache, uint64_t knownVelocityBoneData = 0) {
            CacheSkeletonBonesCached(&cache, knownVelocityBoneData);
        }

        void CacheSkeletonBonesCached(SkeletonBoneCache* cache, uint64_t knownVelocityBoneData) {
            skeleton_bone_valid.fill(false);
            skeleton_bones.fill(Vector3{});
            cached_bot_chest_bone = {};
            cached_bot_chest_bone_valid = false;
            if (this->pos == Vector3(0, 0, 0) || this->PlayerHealth <= 0.f)
                return;

            const std::array<int, 18> indices = GetRenderSkel();
            const bool needsBotChestBone = GameData::IsTrainingBotHeroId(this->HeroID);
            auto fallbackSlowPath = [&]() {
                for (size_t i = 0; i < indices.size(); ++i) {
                    if (indices[i] < 0)
                        continue;
                    Vector3 bone = GetBonePos(indices[i]);
                    skeleton_bones[i] = bone;
                    skeleton_bone_valid[i] = (bone != Vector3(0, 0, 0));
                }
                if (needsBotChestBone) {
                    cached_bot_chest_bone = GetBonePos(83);
                    cached_bot_chest_bone_valid = cached_bot_chest_bone != Vector3(0, 0, 0);
                }
            };

            __try {
                debugHeadLookupAttempted = true;
                debugHeadLookupResolved = false;
                debugHeadIdFound = false;
                debugHeadLookupException = false;
                debugHeadBoneData = 0;
                debugHeadBonesBase = 0;
                debugHeadBonePtr = 0;
                debugHeadBoneIdTable = 0;
                debugHeadBoneCount = 0;
                debugHeadMappedIndex = -1;
                debugHeadLocalAddress = 0;
                debugHeadLocal = {};
                debugHeadWorld = {};
                debugHeadLocalFinite = false;
                debugHeadLocalNonZero = false;
                debugHeadWorldNonZero = false;

                std::array<int, 18> mappedIndices{};
                mappedIndices.fill(-1);
                int maxMappedIndex = -1;
                int botChestMappedIndex = -1;

                uint64_t pBoneData = 0;
                uint64_t bonesBase = 0;
                uint64_t bonePtr = 0;
                uint64_t boneIdTable = 0;
                uint16_t boneCount = 0;

                bool mapReady = false;
                if (cache && cache->valid &&
                    cache->heroId == this->HeroID &&
                    cache->requestedBoneIds == indices &&
                    cache->hasBotChest == needsBotChestBone &&
                    (!knownVelocityBoneData || cache->boneData == knownVelocityBoneData)) {
                    pBoneData = cache->boneData;
                    bonesBase = cache->bonesBase;
                    bonePtr = cache->bonePtr;
                    boneIdTable = cache->boneIdTable;
                    boneCount = cache->boneCount;
                    mappedIndices = cache->mappedIndices;
                    maxMappedIndex = cache->maxMappedIndex;
                    botChestMappedIndex = cache->botChestMappedIndex;
                    mapReady = pBoneData && bonesBase && bonePtr && boneIdTable &&
                        boneCount > 0 && boneCount <= kMaxBoneIdCount &&
                        maxMappedIndex >= 0;
                }

                if (!mapReady) {
                    const BoneResolveInfo boneInfo = ResolveBoneDataInfo(knownVelocityBoneData);
                    pBoneData = boneInfo.boneData;
                    bonesBase = boneInfo.bonesBase;
                    bonePtr = boneInfo.bonePtr;
                    boneIdTable = boneInfo.boneIdTable;
                    boneCount = boneInfo.boneCount;
                }

                debugHeadBoneData = pBoneData;
                debugHeadBonesBase = bonesBase;
                if (!pBoneData || !bonesBase) {
                    fallbackSlowPath();
                    return;
                }

                debugHeadBonePtr = bonePtr;
                if (!bonePtr) {
                    fallbackSlowPath();
                    return;
                }

                debugHeadBoneIdTable = boneIdTable;
                if (!boneIdTable) {
                    fallbackSlowPath();
                    return;
                }

                debugHeadBoneCount = boneCount;
                if (boneCount == 0 || boneCount > kMaxBoneIdCount) {
                    fallbackSlowPath();
                    return;
                }

                debugHeadLookupResolved = true;

                if (!mapReady) {
                    std::array<uint32_t, kMaxBoneIdCount> boneIds{};
                    if (!SDK->read_range(boneIdTable, boneIds.data(), sizeof(uint32_t) * boneCount)) {
                        if (cache)
                            cache->valid = false;
                        fallbackSlowPath();
                        return;
                    }

                    for (size_t requested = 0; requested < indices.size(); ++requested) {
                        if (indices[requested] < 0)
                            continue;
                        for (uint16_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                            if (static_cast<uint16_t>(boneIds[boneIndex]) != static_cast<uint16_t>(indices[requested]))
                                continue;

                            mappedIndices[requested] = static_cast<int>(boneIndex);
                            if (mappedIndices[requested] > maxMappedIndex)
                                maxMappedIndex = mappedIndices[requested];
                            break;
                        }
                    }
                    if (needsBotChestBone) {
                        for (uint16_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
                            if (static_cast<uint16_t>(boneIds[boneIndex]) != 83)
                                continue;

                            botChestMappedIndex = static_cast<int>(boneIndex);
                            if (botChestMappedIndex > maxMappedIndex)
                                maxMappedIndex = botChestMappedIndex;
                            break;
                        }
                    }

                    if (cache) {
                        cache->valid = maxMappedIndex >= 0;
                        cache->heroId = this->HeroID;
                        cache->boneData = pBoneData;
                        cache->bonesBase = bonesBase;
                        cache->bonePtr = bonePtr;
                        cache->boneIdTable = boneIdTable;
                        cache->boneCount = boneCount;
                        cache->requestedBoneIds = indices;
                        cache->mappedIndices = mappedIndices;
                        cache->maxMappedIndex = maxMappedIndex;
                        cache->botChestMappedIndex = botChestMappedIndex;
                        cache->hasBotChest = needsBotChestBone;
                    }
                }

                for (size_t requested = 0; requested < indices.size(); ++requested) {
                    if (indices[requested] != OW::BONE_HEAD || mappedIndices[requested] < 0)
                        continue;

                    debugHeadIdFound = true;
                    debugHeadMappedIndex = mappedIndices[requested];
                    break;
                }

                if (maxMappedIndex < 0)
                    return;

                constexpr size_t kBoneStride = 0x30;
                constexpr size_t kBoneValueOffset = 0x20;
                constexpr size_t kMaxBoneBlockBytes = kBoneStride * kMaxBoneIdCount + sizeof(XMFLOAT3);
                const size_t boneBlockBytes =
                    kBoneStride * static_cast<size_t>(maxMappedIndex) + sizeof(XMFLOAT3);
                if (boneBlockBytes == 0 || boneBlockBytes > kMaxBoneBlockBytes) {
                    fallbackSlowPath();
                    return;
                }

                std::array<uint8_t, kMaxBoneBlockBytes> boneBlock;
                if (!SDK->read_range(bonesBase + kBoneValueOffset, boneBlock.data(), boneBlockBytes)) {
                    if (cache)
                        cache->valid = false;
                    fallbackSlowPath();
                    return;
                }

                const XMMATRIX rotMatrix = XMMatrixRotationY(this->Rot.X);
                for (size_t i = 0; i < indices.size(); ++i) {
                    const int mappedIndex = mappedIndices[i];
                    if (mappedIndex < 0)
                        continue;

                    const size_t localOffset = kBoneStride * static_cast<size_t>(mappedIndex);
                    if (localOffset + sizeof(XMFLOAT3) > boneBlockBytes)
                        continue;

                    XMFLOAT3 localBone{};
                    std::memcpy(&localBone, boneBlock.data() + localOffset, sizeof(localBone));
                    const bool localFinite = IsFiniteBoneValue(localBone);
                    const bool localNonZero = localFinite && IsNonZeroBoneValue(localBone);

                    XMFLOAT3 transformed{};
                    if (localFinite)
                        XMStoreFloat3(&transformed, XMVector3Transform(XMLoadFloat3(&localBone), rotMatrix));

                    Vector3 worldBone{};
                    if (localFinite) {
                        if (this->HeroID == eHero::HERO_WRECKINGBALL)
                            worldBone = Vector3(transformed.x, transformed.y - 0.7f, transformed.z) + this->pos;
                        else
                            worldBone = Vector3(transformed.x, transformed.y, transformed.z) + this->pos;
                    }

                    skeleton_bones[i] = worldBone;
                    skeleton_bone_valid[i] = IsFiniteVectorValue(worldBone) && worldBone != Vector3(0, 0, 0);

                    if (indices[i] == OW::BONE_HEAD) {
                        debugHeadLocalAddress = bonesBase + kBoneValueOffset +
                            kBoneStride * static_cast<uint64_t>(mappedIndex);
                        debugHeadLocal = localBone;
                        debugHeadLocalFinite = localFinite;
                        debugHeadLocalNonZero = localNonZero;
                        debugHeadWorld = worldBone;
                        debugHeadWorldNonZero = skeleton_bone_valid[i];
                    }
                }

                if (needsBotChestBone && botChestMappedIndex >= 0) {
                    const size_t localOffset = kBoneStride * static_cast<size_t>(botChestMappedIndex);
                    if (localOffset + sizeof(XMFLOAT3) <= boneBlockBytes) {
                        XMFLOAT3 localBone{};
                        std::memcpy(&localBone, boneBlock.data() + localOffset, sizeof(localBone));
                        if (IsFiniteBoneValue(localBone)) {
                            XMFLOAT3 transformed{};
                            XMStoreFloat3(&transformed, XMVector3Transform(XMLoadFloat3(&localBone), rotMatrix));
                            cached_bot_chest_bone = Vector3(transformed.x, transformed.y, transformed.z) + this->pos;
                            cached_bot_chest_bone_valid =
                                IsFiniteVectorValue(cached_bot_chest_bone) &&
                                cached_bot_chest_bone != Vector3(0, 0, 0);
                        }
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                debugHeadLookupException = true;
                fallbackSlowPath();
            }
        }

        // =====================================================================
        // 3D box corners
        // =====================================================================
        void Get3DBoxPos(Vector3& veca, Vector3& vecb, Vector3& vecc, Vector3& vecd,
                         Vector3& vece, Vector3& vecf, Vector3& vecg, Vector3& vech) {
            __try {
                if (this->pos != Vector3(0, 0, 0) && this->PlayerHealth > 0) {
                    uint64_t bonesBase = 0;
                    uint64_t pBoneData = ResolveBoneData(bonesBase);
                    if (pBoneData) {
                        if (bonesBase) {
                            const int boneIdx17 = get_bone_id(pBoneData, 17);
                            if (boneIdx17 < 0)
                                return;
                            XMFLOAT3 currentBone = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * boneIdx17) + 0x20
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
                    uint64_t bonesBase = 0;
                    uint64_t pBoneData = ResolveBoneData(bonesBase);
                    if (pBoneData) {
                        if (bonesBase) {
                            const int boneIdx17 = get_bone_id(pBoneData, 17);
                            if (boneIdx17 < 0)
                                return;
                            XMFLOAT3 b = SDK->RPM<XMFLOAT3>(
                                bonesBase + (0x30 * boneIdx17) + 0x20
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

                bool isBot = GameData::IsTrainingBotHeroId(HeroID);

                // For W2S we need the global viewMatrix, WX, WY.
                // These are accessed via the global variables declared in Overwatch.hpp.
                // We rely on the caller to have set up the extern viewMatrix and WX/WY.
                extern Matrix viewMatrix;
                extern float WX, WY;
                extern std::mutex g_viewMatrixMutex;
                Matrix view{};
                g_viewMatrixMutex.lock();
                view = viewMatrix;
                g_viewMatrixMutex.unlock();

                if (isBot) {
                    if (!view.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    int botIndices[] = { 17, 16, 3, 13, 54 };
                    for (int i = 0; i < 5; i++) {
                        Vector3 bone = GetBonePos(botIndices[i]);
                        Vector2 ws{};
                        if (view.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
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
                    if (!view.WorldToScreen(root, &w2s, Vector2(WX, WY))) {
                        a.boneerror = true;
                        return a;
                    }
                    pmin = pmax = w2s;
                    for (int i = 0; i < 18; i++) {
                        Vector3 bone = GetBonePos(index[i]);
                        Vector2 ws{};
                        if (view.WorldToScreen(bone, &ws, Vector2(WX, WY))) {
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
