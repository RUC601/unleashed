#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <ctime>
#include <utility>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <windows.h>
#include <process.h>
#include <DirectXMath.h>

#include "Game/Target.hpp"
#include "Renderer/IconManager.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"

using namespace OW;

// =========================================================================
// Global state variables
// =========================================================================

namespace OW {

    // ---- View matrices (updated by viewmatrix_thread) ----
    inline uint64_t viewMatrixPtr = 0;
    inline uint64_t viewMatrix_xor_ptr = 0;
    inline Matrix viewMatrix{};
    inline Matrix viewMatrix_xor{};

    // ---- Entity containers ----
    inline std::vector<c_entity> entities{};
    inline std::vector<hpanddy> hp_dy_entities{};
    inline c_entity local_entity{};

    // ---- Raw entity scan exchange buffer ----
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities{};
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities_scan{};

    // ---- Screen / window ----
    inline float WX = 0.f, WY = 0.f;

    // ---- Scan coordination ----
    inline int abletotread = 0;
    inline int howbigentitysize = 0;

    inline Vector2 ResolveScreenSize()
    {
        if (Config::manualScreenWidth > 0 && Config::manualScreenHeight > 0) {
            return Vector2(
                static_cast<float>(Config::manualScreenWidth),
                static_cast<float>(Config::manualScreenHeight));
        }

        return Vector2(
            static_cast<float>(GetSystemMetrics(SM_CXSCREEN)),
            static_cast<float>(GetSystemMetrics(SM_CYSCREEN)));
    }

    inline float ResolveScreenWidth()
    {
        return ResolveScreenSize().X;
    }

    inline float ResolveScreenHeight()
    {
        return ResolveScreenSize().Y;
    }

    inline void RefreshScreenSizeFromConfig()
    {
        const Vector2 screenSize = ResolveScreenSize();
        WX = screenSize.X;
        WY = screenSize.Y;
    }

    inline bool PipelineDebugEnabled()
    {
        return Config::kmboxDebugLog;
    }

    inline bool IsMatrixNonIdentity(const Matrix& matrix)
    {
        const float values[] = {
            matrix.m11, matrix.m12, matrix.m13, matrix.m14,
            matrix.m21, matrix.m22, matrix.m23, matrix.m24,
            matrix.m31, matrix.m32, matrix.m33, matrix.m34,
            matrix.m41, matrix.m42, matrix.m43, matrix.m44
        };
        const float identity[] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        bool differsFromIdentity = false;
        bool hasNonZeroValue = false;
        for (size_t index = 0; index < 16; ++index) {
            if (!std::isfinite(values[index]))
                return false;
            if (std::fabs(values[index]) > 0.0001f)
                hasNonZeroValue = true;
            if (std::fabs(values[index] - identity[index]) > 0.001f)
                differsFromIdentity = true;
        }
        return hasNonZeroValue && differsFromIdentity;
    }

    inline void RecordViewMatrixUnresolved(const char* reason, uint64_t value, DWORD& lastLogTick)
    {
        Diagnostics::SetViewMatrixStatus(false, false);
        if (!PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        if (lastLogTick == 0 || now - lastLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 2 view matrix unresolved: %s value=0x%llX.",
                reason ? reason : "unknown",
                static_cast<unsigned long long>(value));
            lastLogTick = now;
        }
    }

    inline void RecordViewMatrixResolved(
        uint64_t renderViewProjectionPtr,
        uint64_t cameraViewPtr,
        bool valid,
        bool& hasLastStatus,
        bool& lastValid,
        DWORD& lastLogTick)
    {
        Diagnostics::SetViewMatrixStatus(true, valid);
        if (!PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        const bool changed = !hasLastStatus || lastValid != valid;
        if (changed || lastLogTick == 0 || now - lastLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 2 view matrix %s renderVP=0x%llX cameraView=0x%llX.",
                valid ? "valid" : "zero/invalid",
                static_cast<unsigned long long>(renderViewProjectionPtr),
                static_cast<unsigned long long>(cameraViewPtr));
            hasLastStatus = true;
            lastValid = valid;
            lastLogTick = now;
        }
    }
} // namespace OW

inline std::mutex g_mutex;

// =========================================================================
// Entity scan thread (lightweight, just calls get_ow_entities)
// =========================================================================

inline void entity_scan_thread() {
    Diagnostics::Info("Entity scan thread started.");
    size_t lastLoggedScanCount = static_cast<size_t>(-1);
    DWORD lastScanLogTick = 0;
    while (OW::Config::doingentity == 1) {
        bool should_scan = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            should_scan = OW::abletotread == 0;
        }

        if (should_scan) {
            std::vector<std::pair<uint64_t, uint64_t>> scanned = OW::get_ow_entities();
            Diagnostics::RecordEntityScanCycle(scanned.size());
            Diagnostics::Trace("Entity scan cycle found %zu raw entities.", scanned.size());
            if (OW::PipelineDebugEnabled()) {
                const DWORD now = GetTickCount();
                if (lastLoggedScanCount != scanned.size() || lastScanLogTick == 0 ||
                    now - lastScanLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 3 entity scan raw=%zu", scanned.size());
                    lastLoggedScanCount = scanned.size();
                    lastScanLogTick = now;
                }
            }
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (OW::abletotread == 0) {
                    OW::ow_entities_scan = std::move(scanned);
                    OW::abletotread = 1;
                }
            }
        }
        Sleep(10);
    }
    Diagnostics::Info("Entity scan thread stopping.");
}

// =========================================================================
// Entity processing thread (decrypts components, builds c_entity list)
// =========================================================================

inline void entity_thread() {
    constexpr DWORD kEntityExchangeIntervalMs = 16;
    DWORD entitytime = 0;
    Vector3 lastpos{};
    size_t lastLoggedRawCount = static_cast<size_t>(-1);
    size_t lastLoggedValidatedCount = static_cast<size_t>(-1);
    DWORD lastProcessLogTick = 0;
    uint64_t entityCycleCount = 0;
    DWORD entityCycleRateTick = GetTickCount();
    double entityCycleHz = 0.0;

    struct ComponentBaseCache {
        uint64_t linkParent = 0;
        uint64_t health = 0;
        uint64_t link = 0;
        uint64_t team = 0;
        uint64_t velocity = 0;
        uint64_t hero = 0;
        uint64_t bone = 0;
        uint64_t rotation = 0;
        uint64_t skill = 0;
        uint64_t visibility = 0;
        uint64_t angle = 0;
        uint64_t enemyAngle = 0;
        DWORD healthUpdateTick = 0;
        bool healthValid = false;
        float playerHealth = 0.0f;
        float playerHealthMax = 0.0f;
        float minHealth = 0.0f;
        float maxHealth = 0.0f;
        float minArmorHealth = 0.0f;
        float maxArmorHealth = 0.0f;
        float minBarrierHealth = 0.0f;
        float maxBarrierHealth = 0.0f;
        bool alive = false;
        bool imort = false;
        bool barrprot = false;
        bool heroValid = false;
        uint64_t heroId = 0;
        DWORD slowUpdateTick = 0;
        bool slowValid = false;
        bool isEnemy = false;
        bool vis = false;
        bool skill1act = false;
        bool skill2act = false;
        float ultimate = 0.0f;
        float skillcd1 = 0.0f;
        float skillcd2 = 0.0f;
        bool reloading = false;
        std::string heroName = "Unknown";
        OW::c_entity::SkeletonBoneCache skeletonCache{};
    };
    std::unordered_map<uint64_t, ComponentBaseCache> componentBaseCache{};
    componentBaseCache.reserve(128);

    struct DynamicEntityCache {
        uint64_t linkParent = 0;
        bool valid = false;
        uint64_t entityId = 0;
        uintptr_t meshBase = 0;
        XMFLOAT3 pos{};
        DWORD updateTick = 0;
    };
    std::unordered_map<uint64_t, DynamicEntityCache> dynamicEntityCache{};
    dynamicEntityCache.reserve(64);

    auto recordEntityCycle = [&]() {
        ++entityCycleCount;
        const DWORD now = GetTickCount();
        if ((entityCycleCount % 60ull) == 0ull) {
            const DWORD elapsed = now - entityCycleRateTick;
            entityCycleHz = elapsed > 0 ? (60000.0 / static_cast<double>(elapsed)) : 0.0;
            entityCycleRateTick = now;
            if (OW::PipelineDebugEnabled()) {
                Diagnostics::Info("[PIPELINE] entity_thread cycle %llu at ~%.1f Hz.",
                    static_cast<unsigned long long>(entityCycleCount),
                    entityCycleHz);
            }
        }
        Diagnostics::RecordEntityProcessCycle(entityCycleHz);
    };

    while (OW::Config::doingentity == 1) {
        SDK->BeginFrame();

        if (entitytime == 0) entitytime = GetTickCount();
        if (GetTickCount() - entitytime >= kEntityExchangeIntervalMs) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OW::abletotread) {
                OW::ow_entities = OW::ow_entities_scan;
                OW::abletotread = 0;
                entitytime = GetTickCount();
            }
        }

        std::vector<std::pair<uint64_t, uint64_t>> raw_entities;
        std::vector<OW::c_entity> previous_entities;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            raw_entities = OW::ow_entities;
            previous_entities = OW::entities;
        }

        // No entities available
        if (raw_entities.empty()) {
            Diagnostics::EntityProcessStats stats{};
            Diagnostics::LocalEntityStats localStats{};
            componentBaseCache.clear();
            dynamicEntityCache.clear();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                OW::entities = {};
                OW::hp_dy_entities = {};
                Diagnostics::SetEntityCount(0);
            }
            Diagnostics::SetEntityProcessStats(stats);
            Diagnostics::SetLocalEntityStats(localStats);
            if (OW::PipelineDebugEnabled()) {
                const DWORD now = GetTickCount();
                const bool changed = lastLoggedRawCount != 0 || lastLoggedValidatedCount != 0;
                if (changed || now - lastProcessLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 4 entity processing raw=0 validated=0.");
                    lastLoggedRawCount = 0;
                    lastLoggedValidatedCount = 0;
                    lastProcessLogTick = now;
                }
            }
            recordEntityCycle();
            Sleep(16);
            continue;
        }
        if (componentBaseCache.size() > 512)
            componentBaseCache.clear();
        if (dynamicEntityCache.size() > 512)
            dynamicEntityCache.clear();

        std::vector<OW::c_entity> tmp_entities{};
        std::vector<OW::hpanddy> hpdy_entities{};
        OW::c_entity lastentity{};
        Diagnostics::EntityProcessStats processStats{};
        processStats.raw = raw_entities.size();
        Diagnostics::LocalEntityStats localStats{};
        bool sampledBoneCandidateHasAngle = false;
        const DWORD processLoopTick = GetTickCount();
        const bool detailedProcessLog = OW::PipelineDebugEnabled() &&
            (lastProcessLogTick == 0 || processLoopTick - lastProcessLogTick >= 1000);
        const auto cameraLocation = OW::viewMatrix_xor.get_location();
        const auto toCentimeters = [](float value) -> int {
            return std::isfinite(value) ? static_cast<int>(value * 100.0f) : 0;
        };
        localStats.cameraXCm = std::isfinite(cameraLocation.x) ? static_cast<int>(cameraLocation.x * 100.0f) : 0;
        localStats.cameraYCm = std::isfinite(cameraLocation.y) ? static_cast<int>(cameraLocation.y * 100.0f) : 0;
        localStats.cameraZCm = std::isfinite(cameraLocation.z) ? static_cast<int>(cameraLocation.z * 100.0f) : 0;

        std::unordered_map<uint64_t, const OW::c_entity*> previousEntityByAddress;
        previousEntityByAddress.reserve(previous_entities.size());
        for (const OW::c_entity& previous : previous_entities) {
            if (previous.address)
                previousEntityByAddress.emplace(previous.address, &previous);
        }

        auto attachPreviousRenderSample = [&](OW::c_entity& entity) {
            entity.render_sample_tick_ms = processLoopTick;

            const auto previousIt = previousEntityByAddress.find(entity.address);
            if (previousIt == previousEntityByAddress.end() || !previousIt->second) {
                entity.previous_render_sample_tick_ms = processLoopTick;
                entity.has_previous_render_sample = false;
                entity.previous_head_pos = entity.head_pos;
                entity.previous_velocity = entity.velocity;
                entity.previous_pos = entity.pos;
                entity.previous_neck_pos = entity.neck_pos;
                entity.previous_chest_pos = entity.chest_pos;
                entity.previous_skeleton_bones = entity.skeleton_bones;
                entity.previous_skeleton_bone_valid = entity.skeleton_bone_valid;
                entity.previous_cached_bot_chest_bone = entity.cached_bot_chest_bone;
                entity.previous_cached_bot_chest_bone_valid = entity.cached_bot_chest_bone_valid;
                return;
            }

            const OW::c_entity& previous = *previousIt->second;
            entity.previous_render_sample_tick_ms =
                previous.render_sample_tick_ms ? previous.render_sample_tick_ms : processLoopTick;
            entity.has_previous_render_sample =
                previous.render_sample_tick_ms != 0 &&
                previous.render_sample_tick_ms != processLoopTick;
            entity.previous_head_pos = previous.head_pos;
            entity.previous_velocity = previous.velocity;
            entity.previous_pos = previous.pos;
            entity.previous_neck_pos = previous.neck_pos;
            entity.previous_chest_pos = previous.chest_pos;
            entity.previous_skeleton_bones = previous.skeleton_bones;
            entity.previous_skeleton_bone_valid = previous.skeleton_bone_valid;
            entity.previous_cached_bot_chest_bone = previous.cached_bot_chest_bone;
            entity.previous_cached_bot_chest_bone_valid = previous.cached_bot_chest_bone_valid;
        };

        for (size_t i = 0; i < raw_entities.size(); i++) {
            OW::c_entity entity{};
            const bool progressLog = detailedProcessLog && (i < 3 || (i % 16) == 0);
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu/%zu start component=0x%llX link=0x%llX.",
                    i,
                    raw_entities.size(),
                    static_cast<unsigned long long>(raw_entities[i].first),
                    static_cast<unsigned long long>(raw_entities[i].second));
            }
            if (!raw_entities[i].first || !raw_entities[i].second) {
                processStats.nullPair++;
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            if (i >= raw_entities.size()) continue;

            const auto& [ComponentParent, LinkParent] = raw_entities[i];
            entity.address = ComponentParent;
            if (!entity.address || !LinkParent) {
                processStats.nullPair++;
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            auto dynamicCacheIt = dynamicEntityCache.find(ComponentParent);
            if (dynamicCacheIt != dynamicEntityCache.end() &&
                dynamicCacheIt->second.linkParent != LinkParent) {
                dynamicEntityCache.erase(dynamicCacheIt);
                dynamicCacheIt = dynamicEntityCache.end();
            }
            if (dynamicCacheIt != dynamicEntityCache.end() && dynamicCacheIt->second.valid) {
                DynamicEntityCache& cachedDynamic = dynamicCacheIt->second;
                if (cachedDynamic.meshBase &&
                    (cachedDynamic.updateTick == 0 ||
                     processLoopTick - cachedDynamic.updateTick >= 250)) {
                    OW::velocity_compo_t hpdyVelocity{};
                    if (SDK->read_range(cachedDynamic.meshBase, &hpdyVelocity, sizeof(hpdyVelocity))) {
                        cachedDynamic.pos = hpdyVelocity.location;
                        cachedDynamic.updateTick = processLoopTick;
                    }
                }

                OW::hpanddy hpdyentity{};
                hpdyentity.entityid = cachedDynamic.entityId;
                hpdyentity.MeshBase = cachedDynamic.meshBase;
                hpdyentity.POS = cachedDynamic.pos;
                hpdy_entities.push_back(hpdyentity);
                processStats.dynamic++;
                continue;
            }

            auto cacheIt = componentBaseCache.find(ComponentParent);
            const bool componentCacheHit =
                cacheIt != componentBaseCache.end() &&
                cacheIt->second.linkParent == LinkParent;

            OW::EntityHeaderSnapshot componentHeader{};
            OW::EntityHeaderSnapshot linkHeader{};
            const OW::EntityHeaderSnapshot* componentSnapshot = nullptr;
            const OW::EntityHeaderSnapshot* linkSnapshot = nullptr;

            if (!componentCacheHit) {
                componentHeader.Read(ComponentParent);
                componentSnapshot = componentHeader.valid ? &componentHeader : nullptr;
                linkSnapshot = componentSnapshot;
                if (LinkParent != ComponentParent) {
                    linkHeader.Read(LinkParent);
                    linkSnapshot = linkHeader.valid ? &linkHeader : nullptr;
                }
                if (progressLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu headers component_valid=%d link_valid=%d cache_hit=0.",
                        i,
                        componentHeader.valid ? 1 : 0,
                        linkSnapshot ? 1 : 0);
                }
            } else if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu headers cache_hit=1.", i);
            }

            // Check for special entity IDs (HP packs, Bob, etc.)
            if (!componentCacheHit) {
                uint64_t ptrValue = 0;
                if (!componentHeader.ReadParentOffset(0x30, ptrValue))
                    ptrValue = SDK->RPM<uint64_t>(ComponentParent + 0x30);
                uint64_t Ptr = ptrValue & 0xFFFFFFFFFFFFFFC0;
                if (Ptr && Ptr < 0xFFFFFFFFFFFFFFEF) {
                    uint64_t EntityID = SDK->RPM<uint64_t>(Ptr + 0x10);
                    if (EntityID == 0x400000000000060 || EntityID == 0x40000000000480A ||
                        EntityID == 0x40000000000005F || EntityID == 0x400000000002533) {
                        OW::hpanddy hpdyentity{};
                        hpdyentity.entityid = EntityID;
                        hpdyentity.MeshBase = OW::DecryptComponent(
                            ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                        OW::velocity_compo_t hpdyVelocity{};
                        if (hpdyentity.MeshBase &&
                            SDK->read_range(hpdyentity.MeshBase, &hpdyVelocity, sizeof(hpdyVelocity))) {
                            hpdyentity.POS = hpdyVelocity.location;
                        } else if (hpdyentity.MeshBase) {
                            hpdyentity.POS = SDK->RPM<XMFLOAT3>(hpdyentity.MeshBase + 0x380 + 0x50);
                        }
                        DynamicEntityCache dynamicCache{};
                        dynamicCache.linkParent = LinkParent;
                        dynamicCache.valid = true;
                        dynamicCache.entityId = EntityID;
                        dynamicCache.meshBase = hpdyentity.MeshBase;
                        dynamicCache.pos = hpdyentity.POS;
                        dynamicCache.updateTick = processLoopTick;
                        dynamicEntityCache.insert_or_assign(ComponentParent, dynamicCache);
                        hpdy_entities.push_back(hpdyentity);
                        processStats.dynamic++;
                        continue;
                    }
                }
            }

            // Component bases are stable for a live entity; cache them to avoid
            // repeating the expensive component-table decrypt path every cycle.
            if (!componentCacheHit) {
                ComponentBaseCache cache{};
                cache.linkParent = LinkParent;
                cache.health     = OW::DecryptComponent(ComponentParent, OW::TYPE_HEALTH, componentSnapshot);
                cache.link       = OW::DecryptComponent(LinkParent, OW::TYPE_LINK, linkSnapshot);
                cache.team       = OW::DecryptComponent(ComponentParent, OW::TYPE_TEAM, componentSnapshot);
                cache.velocity   = OW::DecryptComponent(ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                cache.hero       = OW::DecryptComponent(LinkParent, OW::TYPE_P_HEROID, linkSnapshot);
                cache.bone       = OW::DecryptComponent(ComponentParent, OW::TYPE_BONE, componentSnapshot);
                cache.rotation   = OW::DecryptComponent(ComponentParent, OW::TYPE_ROTATION, componentSnapshot);
                cache.skill      = OW::DecryptComponent(ComponentParent, OW::TYPE_SKILL, componentSnapshot);
                cache.visibility = OW::DecryptComponent(LinkParent, OW::TYPE_P_VISIBILITY, linkSnapshot);
                cache.angle      = OW::DecryptComponent(LinkParent, OW::TYPE_PLAYERCONTROLLER, linkSnapshot);
                cache.enemyAngle = OW::DecryptComponent(ComponentParent, OW::TYPE_ANGLE, componentSnapshot);
                cacheIt = componentBaseCache.insert_or_assign(ComponentParent, cache).first;
            }

            entity.HealthBase     = cacheIt->second.health;
            entity.LinkBase       = cacheIt->second.link;
            entity.TeamBase       = cacheIt->second.team;
            entity.VelocityBase   = cacheIt->second.velocity;
            entity.HeroBase       = cacheIt->second.hero;
            entity.BoneBase       = cacheIt->second.bone;
            entity.RotationBase   = cacheIt->second.rotation;
            entity.SkillBase      = cacheIt->second.skill;
            entity.VisBase        = cacheIt->second.visibility;
            entity.AngleBase      = cacheIt->second.angle;
            entity.EnemyAngleBase = cacheIt->second.enemyAngle;
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu decrypt health=0x%llX link=0x%llX velocity=0x%llX hero=0x%llX bone=0x%llX angle=0x%llX.",
                    i,
                    static_cast<unsigned long long>(entity.HealthBase),
                    static_cast<unsigned long long>(entity.LinkBase),
                    static_cast<unsigned long long>(entity.VelocityBase),
                    static_cast<unsigned long long>(entity.HeroBase),
                    static_cast<unsigned long long>(entity.BoneBase),
                    static_cast<unsigned long long>(entity.AngleBase));
            }
            if (!entity.LinkBase) {
                componentBaseCache.erase(ComponentParent);
                processStats.linkBaseFail++;
            }

            // Skip duplicates
            if (entity == lastentity) {
                processStats.duplicate++;
                continue;
            }
            lastentity = entity;

            OW::velocity_compo_t velo_compo{};
            const bool velocityRead = entity.VelocityBase &&
                SDK->read_range(entity.VelocityBase, &velo_compo, sizeof(velo_compo));

            // ---- Health ----
            if (entity.HealthBase) {
                ComponentBaseCache& componentCache = cacheIt->second;
                const bool refreshHealth =
                    !componentCache.healthValid ||
                    processLoopTick - componentCache.healthUpdateTick >= 100;
                if (refreshHealth) {
                    OW::health_compo_t health_compo{};
                    if (!SDK->read_range(entity.HealthBase, &health_compo, sizeof(health_compo))) {
                        componentBaseCache.erase(ComponentParent);
                        processStats.healthBaseFail++;
                        Diagnostics::RecordInvalidEntity();
                        continue;
                    }
                    const Vector2 healthext = health_compo.health_ext;
                    componentCache.playerHealth =
                        health_compo.health + health_compo.armor + health_compo.barrier + healthext.Y;
                    componentCache.playerHealthMax =
                        health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + healthext.X;
                    componentCache.minHealth = health_compo.health;
                    componentCache.maxHealth = health_compo.health_max;
                    componentCache.minArmorHealth = health_compo.armor;
                    componentCache.maxArmorHealth = health_compo.armor_max;
                    componentCache.minBarrierHealth = health_compo.barrier;
                    componentCache.maxBarrierHealth = health_compo.barrier_max;
                    componentCache.alive = componentCache.playerHealth > 0.f;
                    componentCache.imort = health_compo.isImmortal;
                    componentCache.barrprot = health_compo.isBarrierProjected;
                    componentCache.healthUpdateTick = processLoopTick;
                    componentCache.healthValid = true;
                }
                entity.PlayerHealth = componentCache.playerHealth;
                entity.PlayerHealthMax = componentCache.playerHealthMax;
                entity.MinHealth = componentCache.minHealth;
                entity.MaxHealth = componentCache.maxHealth;
                entity.MinArmorHealth = componentCache.minArmorHealth;
                entity.MaxArmorHealth = componentCache.maxArmorHealth;
                entity.MinBarrierHealth = componentCache.minBarrierHealth;
                entity.MaxBarrierHealth = componentCache.maxBarrierHealth;
                entity.Alive = componentCache.alive;
                entity.imort = componentCache.imort;
                entity.barrprot = componentCache.barrprot;
            } else {
                processStats.healthBaseFail++;
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            // ---- Rotation ----
            if (entity.RotationBase) {
                uint64_t rotPtr = SDK->RPM<uint64_t>(entity.RotationBase + 0x7C0 + 0x10);
                entity.Rot = SDK->RPM<Vector3>(rotPtr + 0x8FC);
            }

            // ---- Velocity / position / bones ----
            if (velocityRead) {
                entity.pos      = Vector3(velo_compo.location.x, velo_compo.location.y - 1.f, velo_compo.location.z);
                entity.velocity = Vector3(velo_compo.velocity.x, velo_compo.velocity.y, velo_compo.velocity.z);
            }

            // ---- Hero ID ----
            if (entity.HeroBase) {
                ComponentBaseCache& componentCache = cacheIt->second;
                if (componentCache.heroValid) {
                    entity.HeroID = componentCache.heroId;
                } else {
                    OW::hero_compo_t hero_compo{};
                    if (SDK->read_range(entity.HeroBase, &hero_compo, sizeof(hero_compo))) {
                        entity.HeroID = hero_compo.heroid;
                        componentCache.heroId = entity.HeroID;
                        componentCache.heroValid = entity.HeroID != 0;
                    }
                }
            } else {
                processStats.heroBaseMissing++;
                // Fallback: identify by MaxHealth
                if (entity.MaxHealth == 225) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y + 1.f, temppos.z);
                    entity.HeroID = 0x16dd; // TOBTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 30) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y, temppos.z);
                    entity.HeroID = 0x16ee; // SYMTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 1000) {
                    entity.HeroID = 0x16bb; // Bob
                } else {
                    processStats.heroFallbackFail++;
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
            }
            if (progressLog) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu hero=0x%llX health=%.1f pos=(%.2f,%.2f,%.2f).",
                    i,
                    static_cast<unsigned long long>(entity.HeroID),
                    entity.PlayerHealth,
                    entity.pos.X,
                    entity.pos.Y,
                    entity.pos.Z);
            }

            if (entity.VelocityBase && entity.HeroID != 0x16dd && entity.HeroID != 0x16ee) {
                processStats.boneCandidates++;
                if (entity.BoneBase)
                    processStats.boneBaseNonZero++;

                const uint64_t velocityBoneData = velocityRead
                    ? velo_compo.bonedata
                    : SDK->RPM<uint64_t>(entity.VelocityBase + 0x8B0);

                if (detailedProcessLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu bone_start hero=0x%llX velocity=0x%llX bone=0x%llX vbd=0x%llX bones_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.HeroID),
                        static_cast<unsigned long long>(entity.VelocityBase),
                        static_cast<unsigned long long>(entity.BoneBase),
                        static_cast<unsigned long long>(velocityBoneData),
                        0ull);
                }
                entity.CacheSkeletonBones(cacheIt->second.skeletonCache, velocityBoneData);

                if (entity.debugHeadBoneData)
                    processStats.velocityBoneDataNonZero++;
                if (entity.debugHeadBonePtr)
                    processStats.boneDataPtrNonZero++;
                if (entity.debugHeadBonesBase)
                    processStats.bonesBaseNonZero++;
                if (entity.debugHeadBoneIdTable)
                    processStats.velocityBoneIdTableNonZero++;
                const bool boneCountValid =
                    entity.debugHeadBoneCount > 0 &&
                    entity.debugHeadBoneCount <= OW::c_entity::kMaxBoneIdCount;
                if (boneCountValid)
                    processStats.velocityBoneCountValid++;
                if (entity.debugHeadLookupResolved)
                    processStats.velocityBoneIdTableReadable++;
                if (entity.debugHeadIdFound)
                    processStats.velocityBoneHeadIdFound++;

                if (processStats.sampleBoneAddress == 0 ||
                    (entity.AngleBase && !sampledBoneCandidateHasAngle)) {
                    processStats.sampleBoneAddress = entity.address;
                    processStats.sampleVelocityBase = entity.VelocityBase;
                    processStats.sampleBoneBase = entity.BoneBase;
                    processStats.sampleVelocityBoneData = entity.debugHeadBoneData;
                    processStats.sampleBoneDataPtr = entity.debugHeadBonePtr;
                    processStats.sampleBonesBase = entity.debugHeadBonesBase;
                    processStats.sampleBoneIdTable = entity.debugHeadBoneIdTable;
                    processStats.sampleBoneCount = static_cast<int>(entity.debugHeadBoneCount);
                    processStats.sampleBoneIdTableReadable = entity.debugHeadLookupResolved ? 1 : 0;
                    processStats.sampleBoneHeadIndex = entity.debugHeadMappedIndex;
                    sampledBoneCandidateHasAngle = entity.AngleBase != 0;
                }
                if (detailedProcessLog) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu bone_done head_res=%d id=%d local=%d world=%d idx=%d head=(%.2f,%.2f,%.2f).",
                        i,
                        entity.debugHeadLookupResolved ? 1 : 0,
                        entity.debugHeadIdFound ? 1 : 0,
                        entity.debugHeadLocalNonZero ? 1 : 0,
                        entity.debugHeadWorldNonZero ? 1 : 0,
                        entity.debugHeadMappedIndex,
                        entity.debugHeadWorld.X,
                        entity.debugHeadWorld.Y,
                        entity.debugHeadWorld.Z);
                }
                if (detailedProcessLog) {
                    processStats.headProbeCandidates++;
                    if (entity.debugHeadLookupResolved)
                        processStats.headProbeResolved++;
                    if (entity.debugHeadIdFound)
                        processStats.headProbeIdFound++;
                    if (entity.debugHeadLocalFinite)
                        processStats.headProbeLocalFinite++;
                    if (entity.debugHeadLocalNonZero)
                        processStats.headProbeLocalNonZero++;
                    if (entity.debugHeadWorldNonZero)
                        processStats.headProbeWorldNonZero++;
                    if (entity.debugHeadLookupException)
                        processStats.headProbeExceptions++;

                    const float posDist = Vector3(cameraLocation.x, cameraLocation.y, cameraLocation.z)
                        .DistTo(entity.pos);
                    const bool nearCameraByPosition = std::isfinite(posDist) && posDist <= 3.0f;
                    if (nearCameraByPosition) {
                        processStats.headProbeNearCandidates++;
                        if (entity.debugHeadWorldNonZero)
                            processStats.headProbeNearWorldNonZero++;
                    } else {
                        processStats.headProbeFarCandidates++;
                        if (entity.debugHeadWorldNonZero)
                            processStats.headProbeFarWorldNonZero++;
                    }

                    if (entity.debugHeadWorldNonZero && processStats.sampleHeadGoodAddress == 0) {
                        const float headDist = Vector3(cameraLocation.x, cameraLocation.y, cameraLocation.z)
                            .DistTo(entity.debugHeadWorld);
                        processStats.sampleHeadGoodAddress = entity.address;
                        processStats.sampleHeadGoodHeroId = entity.HeroID;
                        processStats.sampleHeadGoodMappedIndex = entity.debugHeadMappedIndex;
                        processStats.sampleHeadGoodLocalXCm = toCentimeters(entity.debugHeadLocal.x);
                        processStats.sampleHeadGoodLocalYCm = toCentimeters(entity.debugHeadLocal.y);
                        processStats.sampleHeadGoodLocalZCm = toCentimeters(entity.debugHeadLocal.z);
                        processStats.sampleHeadGoodWorldXCm = toCentimeters(entity.debugHeadWorld.X);
                        processStats.sampleHeadGoodWorldYCm = toCentimeters(entity.debugHeadWorld.Y);
                        processStats.sampleHeadGoodWorldZCm = toCentimeters(entity.debugHeadWorld.Z);
                        processStats.sampleHeadGoodDistanceCm = std::isfinite(headDist)
                            ? static_cast<int>(headDist * 100.0f)
                            : -1;
                    }

                    if ((!entity.debugHeadWorldNonZero || !entity.debugHeadIdFound ||
                         !entity.debugHeadLocalFinite || !entity.debugHeadLocalNonZero) &&
                        processStats.sampleHeadBadAddress == 0) {
                        processStats.sampleHeadBadAddress = entity.address;
                        processStats.sampleHeadBadHeroId = entity.HeroID;
                        processStats.sampleHeadBadBoneData = entity.debugHeadBoneData;
                        processStats.sampleHeadBadBonesBase = entity.debugHeadBonesBase;
                        processStats.sampleHeadBadBonePtr = entity.debugHeadBonePtr;
                        processStats.sampleHeadBadBoneIdTable = entity.debugHeadBoneIdTable;
                        processStats.sampleHeadBadMappedIndex = entity.debugHeadMappedIndex;
                        processStats.sampleHeadBadBoneCount = static_cast<int>(entity.debugHeadBoneCount);
                        processStats.sampleHeadBadLocalXCm = toCentimeters(entity.debugHeadLocal.x);
                        processStats.sampleHeadBadLocalYCm = toCentimeters(entity.debugHeadLocal.y);
                        processStats.sampleHeadBadLocalZCm = toCentimeters(entity.debugHeadLocal.z);
                    }
                }
                const bool anyBoneValid = std::any_of(
                    entity.skeleton_bone_valid.begin(),
                    entity.skeleton_bone_valid.end(),
                    [](bool valid) { return valid; });
                if (anyBoneValid)
                    processStats.skeletonAnyValid++;
                if (entity.skeleton_bone_valid[0]) entity.head_pos = entity.skeleton_bones[0];
                if (entity.skeleton_bone_valid[1]) entity.neck_pos = entity.skeleton_bones[1];
                if (entity.skeleton_bone_valid[2]) entity.chest_pos = entity.skeleton_bones[2];
                if (entity.skeleton_bone_valid[0])
                    processStats.skeletonHeadValid++;
            }

            if (entity.HeroID == OW::eHero::HERO_WRECKINGBALL) {
                entity.head_pos.Y += 0.02f;
            }

            ComponentBaseCache& slowCache = cacheIt->second;
            const DWORD slowNow = GetTickCount();
            const bool refreshSlowFields =
                !slowCache.slowValid || slowNow - slowCache.slowUpdateTick >= 500;
            std::string name = refreshSlowFields
                ? OW::GetHeroEngNames(entity.HeroID, entity.LinkBase)
                : slowCache.heroName;
            if (name.empty())
                name = "Unknown";

            if (entity.HeroID == OW::eHero::HERO_DVA && name != "Hana") {
                entity.imort = false;
                entity.head_pos.Y -= 0.1f;
                entity.chest_pos = entity.neck_pos;
                entity.chest_pos.Y -= 0.3f;
            }

            bool isStandardBot = (entity.HeroID == OW::eHero::HERO_TRAININGBOT1 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT2 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT3 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT4 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT8 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT5 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT6 ||
                                  entity.HeroID == OW::eHero::HERO_TRAININGBOT7);
            if (isStandardBot) {
                if (entity.cached_bot_chest_bone_valid)
                    entity.chest_pos = entity.cached_bot_chest_bone;
                else
                    entity.chest_pos = entity.GetBonePos(83);
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu post_bone_adjust.", i);
            }

            // ---- BattleTag (optional) ----
            if (OW::Config::draw_info && OW::Config::drawbattletag) {
                entity.statcombase = OW::DecryptComponent(LinkParent, OW::TYPE_STAT, linkSnapshot);
                if (entity.statcombase && entity != OW::local_entity) {
                    uintptr_t off = SDK->RPM<uintptr_t>(entity.statcombase + 0xE0);
                    char buffer[64] = "";
                    SDK->read_buf(off, buffer, sizeof(buffer));
                    entity.battletag = buffer;
                }
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu post_battletag.", i);
            }

            // ---- Team ----
            if (!refreshSlowFields) {
                entity.Team = slowCache.isEnemy;
            } else if (entity.TeamBase) {
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu team_start team_base=0x%llX local_team_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.TeamBase),
                        static_cast<unsigned long long>(OW::local_entity.TeamBase));
                }
                auto team = entity.GetTeam();
                entity.Team = (team == OW::eTeam::TEAM_DEATHMATCH || team != OW::local_entity.GetTeam());
                slowCache.isEnemy = entity.Team;
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu team_done team=%d enemy=%d.",
                        i,
                        static_cast<int>(team),
                        entity.Team ? 1 : 0);
                }
            }

            // ---- Visibility (May 2026 UC p330: new direct decrypt from VisBase) ----
            if (!refreshSlowFields) {
                entity.Vis = slowCache.vis;
            } else if (entity.VisBase) {
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu vis_start vis_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.VisBase));
                }
                entity.Vis = OW::DecryptVis(entity.VisBase) != 0;
                slowCache.vis = entity.Vis;
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu vis_done vis=%d.", i, entity.Vis ? 1 : 0);
                }
            }

            // ---- Skills ----
            if (!refreshSlowFields) {
                entity.skill1act = slowCache.skill1act;
                entity.skill2act = slowCache.skill2act;
                entity.ultimate = slowCache.ultimate;
                entity.skillcd1 = slowCache.skillcd1;
                entity.skillcd2 = slowCache.skillcd2;
            } else if (entity.SkillBase) {
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu skill_start skill_base=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.SkillBase));
                }
                entity.skill1act = OW::IsSkillActive(entity.SkillBase + 0x40, 0, 0x28E3);
                entity.skill2act = OW::IsSkillActive(entity.SkillBase + 0x40, 0, 0x28E9);
                entity.ultimate  = OW::readult(entity.SkillBase + 0x40, 0, 0x1e32);
                slowCache.skill1act = entity.skill1act;
                slowCache.skill2act = entity.skill2act;
                slowCache.ultimate = entity.ultimate;

                // Sombra stealth: treat as invisible when translocated
                if (entity.HeroID == OW::eHero::HERO_SOMBRA && entity.Team &&
                    !OW::Config::Rage && !OW::Config::fov360 &&
                    !OW::Config::silent && !OW::Config::fakesilent) {
                    entity.Vis = (entity.Vis && !OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x7C5));
                    slowCache.vis = entity.Vis;
                }
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu skill_done.", i);
                }
            }

            // ---- Player controller / local entity detection ----
            if (entity.AngleBase) {
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_start angle=0x%llX.",
                        i,
                        static_cast<unsigned long long>(entity.AngleBase));
                }
                localStats.angleCandidates++;
                const bool headIsZero = entity.head_pos == Vector3(0, 0, 0);
                const bool positionIsNonZero = entity.pos != Vector3(0, 0, 0);
                if (headIsZero)
                    localStats.zeroHeadCandidates++;
                if (positionIsNonZero)
                    localStats.nonZeroPositionCandidates++;

                float dist = Vector3(cameraLocation.x, cameraLocation.y, cameraLocation.z)
                    .DistTo(entity.head_pos);
                const int distanceCm = std::isfinite(dist)
                    ? static_cast<int>(dist * 100.0f + 0.5f)
                    : -1;
                if (dist <= 1.f)
                    localStats.nearCameraCandidates++;

                const std::string& localHeroName = name;
                if (localHeroName != "Unknown")
                    localStats.namedCandidates++;
                if (distanceCm >= 0 &&
                    (localStats.bestDistanceCm < 0 || distanceCm < localStats.bestDistanceCm)) {
                    localStats.bestDistanceCm = distanceCm;
                    localStats.bestAddress = entity.address;
                    localStats.bestHeroId = entity.HeroID;
                    localStats.bestAngleBase = entity.AngleBase;
                    localStats.bestHealth = static_cast<int>(entity.PlayerHealth + 0.5f);
                    localStats.bestHeadXCm = std::isfinite(entity.head_pos.X) ? static_cast<int>(entity.head_pos.X * 100.0f) : 0;
                    localStats.bestHeadYCm = std::isfinite(entity.head_pos.Y) ? static_cast<int>(entity.head_pos.Y * 100.0f) : 0;
                    localStats.bestHeadZCm = std::isfinite(entity.head_pos.Z) ? static_cast<int>(entity.head_pos.Z * 100.0f) : 0;
                    localStats.bestPosXCm = std::isfinite(entity.pos.X) ? static_cast<int>(entity.pos.X * 100.0f) : 0;
                    localStats.bestPosYCm = std::isfinite(entity.pos.Y) ? static_cast<int>(entity.pos.Y * 100.0f) : 0;
                    localStats.bestPosZCm = std::isfinite(entity.pos.Z) ? static_cast<int>(entity.pos.Z * 100.0f) : 0;
                }

                if (dist <= 1.f && localHeroName != "Unknown") {
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd1_start skill_base=0x%llX hero=%s.",
                            i,
                            static_cast<unsigned long long>(entity.SkillBase),
                            localHeroName.c_str());
                    }
                    if (refreshSlowFields)
                        slowCache.skillcd1 = OW::readskillcd(entity.SkillBase + 0x40, 0, 0x189c);
                    entity.skillcd1 = slowCache.skillcd1;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd1_done value=%.3f.",
                            i,
                            entity.skillcd1);
                    }
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd2_start.", i);
                    }
                    if (refreshSlowFields)
                        slowCache.skillcd2 = OW::readskillcd(entity.SkillBase + 0x40, 0, 0x1f89);
                    entity.skillcd2 = slowCache.skillcd2;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select skillcd2_done value=%.3f.",
                            i,
                            entity.skillcd2);
                    }
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        OW::local_entity = entity;
                        SDK->g_player_controller = entity.AngleBase;
                    }
                    localStats.selected++;
                    localStats.selectedAddress = entity.address;
                    localStats.selectedHeroId = entity.HeroID;
                    localStats.selectedAngleBase = entity.AngleBase;
                    localStats.selectedHealth = static_cast<int>(entity.PlayerHealth + 0.5f);
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select reload_start.", i);
                    }
                    if (refreshSlowFields)
                        slowCache.reloading = OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x4BF);
                    OW::Config::reloading = slowCache.reloading;
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_select reload_done.", i);
                    }
                    if (entity.GetTeam() == OW::eTeam::TEAM_DEATHMATCH)
                        entity.Team = false;
                }
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_done selected=%zu.", i, localStats.selected);
                }
            }

            // Add to list if valid
            if (refreshSlowFields) {
                slowCache.heroName = name;
                slowCache.slowUpdateTick = slowNow;
                slowCache.slowValid = true;
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu name_start.", i);
            }
            if (detailedProcessLog && processStats.boneCandidates > 0) {
                Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu name_done name=%s.", i, name.c_str());
            }
            if (ComponentParent && LinkParent && name != "Unknown") {
                attachPreviousRenderSample(entity);
                tmp_entities.push_back(entity);
            } else {
                if (name == "Unknown")
                    processStats.nameUnknown++;
                Diagnostics::RecordInvalidEntity();
            }
        }

        // Swap processed entities
        const size_t valid_count = tmp_entities.size();
        const size_t dynamic_count = hpdy_entities.size();
        processStats.validated = valid_count;
        processStats.dynamic = dynamic_count;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            OW::entities = std::move(tmp_entities);
            OW::hp_dy_entities = std::move(hpdy_entities);
        }
        Diagnostics::SetEntityCount(valid_count);
        Diagnostics::SetEntityProcessStats(processStats);
        Diagnostics::SetLocalEntityStats(localStats);
        Diagnostics::Trace("Entity process cycle: valid=%zu hp_dynamic=%zu raw=%zu.",
            valid_count, dynamic_count, raw_entities.size());
        if (OW::PipelineDebugEnabled()) {
            const DWORD now = GetTickCount();
            const bool changed =
                lastLoggedRawCount != raw_entities.size() ||
                lastLoggedValidatedCount != valid_count;
            if (changed || now - lastProcessLogTick >= 1000) {
                Diagnostics::Info("[PIPELINE] Stage 4 entity processing raw=%zu validated=%zu hp_dynamic=%zu.",
                    raw_entities.size(), valid_count, dynamic_count);
                Diagnostics::Info("[PIPELINE] Stage 4 detail null=%zu duplicate=%zu health_base_fail=%zu link_base_fail=%zu hero_missing=%zu hero_fallback_fail=%zu name_unknown=%zu.",
                    processStats.nullPair,
                    processStats.duplicate,
                    processStats.healthBaseFail,
                    processStats.linkBaseFail,
                    processStats.heroBaseMissing,
                    processStats.heroFallbackFail,
                    processStats.nameUnknown);
                Diagnostics::Info("[PIPELINE] Stage 4 bone chain candidates=%zu bone_base=%zu vel_bonedata=%zu bone_ptr=%zu bones_base=%zu id_table=%zu count_ok=%zu table_read=%zu head_id=%zu skel_any=%zu skel_head=%zu sample addr=0x%llX velocity=0x%llX bone=0x%llX vel_bonedata=0x%llX bone_ptr=0x%llX bones_base=0x%llX id_table=0x%llX count=%d read=%d head_idx=%d.",
                    processStats.boneCandidates,
                    processStats.boneBaseNonZero,
                    processStats.velocityBoneDataNonZero,
                    processStats.boneDataPtrNonZero,
                    processStats.bonesBaseNonZero,
                    processStats.velocityBoneIdTableNonZero,
                    processStats.velocityBoneCountValid,
                    processStats.velocityBoneIdTableReadable,
                    processStats.velocityBoneHeadIdFound,
                    processStats.skeletonAnyValid,
                    processStats.skeletonHeadValid,
                    static_cast<unsigned long long>(processStats.sampleBoneAddress),
                    static_cast<unsigned long long>(processStats.sampleVelocityBase),
                    static_cast<unsigned long long>(processStats.sampleBoneBase),
                    static_cast<unsigned long long>(processStats.sampleVelocityBoneData),
                    static_cast<unsigned long long>(processStats.sampleBoneDataPtr),
                    static_cast<unsigned long long>(processStats.sampleBonesBase),
                    static_cast<unsigned long long>(processStats.sampleBoneIdTable),
                    processStats.sampleBoneCount,
                    processStats.sampleBoneIdTableReadable,
                    processStats.sampleBoneHeadIndex);
                Diagnostics::Info("[PIPELINE] Stage 4 head probe candidates=%zu resolved=%zu id_found=%zu local_finite=%zu local_nz=%zu world_nz=%zu exceptions=%zu near_pos=%zu/%zu far_pos=%zu/%zu.",
                    processStats.headProbeCandidates,
                    processStats.headProbeResolved,
                    processStats.headProbeIdFound,
                    processStats.headProbeLocalFinite,
                    processStats.headProbeLocalNonZero,
                    processStats.headProbeWorldNonZero,
                    processStats.headProbeExceptions,
                    processStats.headProbeNearWorldNonZero,
                    processStats.headProbeNearCandidates,
                    processStats.headProbeFarWorldNonZero,
                    processStats.headProbeFarCandidates);
                Diagnostics::Info("[PIPELINE] Stage 4 head sample good addr=0x%llX hero=0x%llX idx=%d local_cm=(%d,%d,%d) world_cm=(%d,%d,%d) dist_cm=%d.",
                    static_cast<unsigned long long>(processStats.sampleHeadGoodAddress),
                    static_cast<unsigned long long>(processStats.sampleHeadGoodHeroId),
                    processStats.sampleHeadGoodMappedIndex,
                    processStats.sampleHeadGoodLocalXCm,
                    processStats.sampleHeadGoodLocalYCm,
                    processStats.sampleHeadGoodLocalZCm,
                    processStats.sampleHeadGoodWorldXCm,
                    processStats.sampleHeadGoodWorldYCm,
                    processStats.sampleHeadGoodWorldZCm,
                    processStats.sampleHeadGoodDistanceCm);
                Diagnostics::Info("[PIPELINE] Stage 4 head sample bad addr=0x%llX hero=0x%llX idx=%d count=%d bonedata=0x%llX bones_base=0x%llX bone_ptr=0x%llX id_table=0x%llX local_cm=(%d,%d,%d).",
                    static_cast<unsigned long long>(processStats.sampleHeadBadAddress),
                    static_cast<unsigned long long>(processStats.sampleHeadBadHeroId),
                    processStats.sampleHeadBadMappedIndex,
                    processStats.sampleHeadBadBoneCount,
                    static_cast<unsigned long long>(processStats.sampleHeadBadBoneData),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBonesBase),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBonePtr),
                    static_cast<unsigned long long>(processStats.sampleHeadBadBoneIdTable),
                    processStats.sampleHeadBadLocalXCm,
                    processStats.sampleHeadBadLocalYCm,
                    processStats.sampleHeadBadLocalZCm);
                Diagnostics::Info("[PIPELINE] Stage 4 local angle_candidates=%zu near_camera=%zu named=%zu selected=%zu best_dist_cm=%d health=%d hero=0x%llX angle=0x%llX.",
                    localStats.angleCandidates,
                    localStats.nearCameraCandidates,
                    localStats.namedCandidates,
                    localStats.selected,
                    localStats.bestDistanceCm,
                    localStats.selectedHealth,
                    static_cast<unsigned long long>(localStats.selectedHeroId),
                    static_cast<unsigned long long>(localStats.selectedAngleBase));
                Diagnostics::Info("[PIPELINE] Stage 4 local coords zero_head=%zu nonzero_pos=%zu best addr=0x%llX hero=0x%llX angle=0x%llX health=%d head_cm=(%d,%d,%d) pos_cm=(%d,%d,%d) camera_cm=(%d,%d,%d).",
                    localStats.zeroHeadCandidates,
                    localStats.nonZeroPositionCandidates,
                    static_cast<unsigned long long>(localStats.bestAddress),
                    static_cast<unsigned long long>(localStats.bestHeroId),
                    static_cast<unsigned long long>(localStats.bestAngleBase),
                    localStats.bestHealth,
                    localStats.bestHeadXCm,
                    localStats.bestHeadYCm,
                    localStats.bestHeadZCm,
                    localStats.bestPosXCm,
                    localStats.bestPosYCm,
                    localStats.bestPosZCm,
                    localStats.cameraXCm,
                    localStats.cameraYCm,
                    localStats.cameraZCm);
                lastLoggedRawCount = raw_entities.size();
                lastLoggedValidatedCount = valid_count;
                lastProcessLogTick = now;
            }
        }
        recordEntityCycle();
        Sleep(1);
    }
}

// =========================================================================
// View matrix reader thread
// =========================================================================

inline void viewmatrix_thread() {
    DWORD lastViewMatrixLogTick = 0;
    bool hasLastViewMatrixStatus = false;
    bool lastViewMatrixValid = false;

    __try {
        while (true) {
            // VM11 (May 2026): three-key subtract-XOR-subtract chain.
            // UC p321-p323 working snippets use the p2 -> +0x6C8 -> +0x8 -> +0xC0
            // pre-composed view-projection matrix as the primary WorldToScreen source.
            uint64_t enc = SDK->RPM<uint64_t>(SDK->dwGameBase + OW::offset::Address_viewmatrix_base);
            if (!enc) {
                OW::RecordViewMatrixUnresolved("encrypted base pointer", enc, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }
            uint64_t dec = ((enc - OW::offset::offset_viewmatrix_xor_key)
                         ^ OW::offset::offset_viewmatrix_xor_key2)
                         - OW::offset::offset_viewmatrix_xor_key3;
            if (!dec) {
                OW::RecordViewMatrixUnresolved("decoded base pointer", dec, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            uint64_t p1 = SDK->RPM<uint64_t>(dec + OW::offset::VM_P1);
            if (!p1) {
                OW::RecordViewMatrixUnresolved("p1", p1, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }
            uint64_t p2 = SDK->RPM<uint64_t>(p1 + OW::offset::VM_P2);
            if (!p2) {
                OW::RecordViewMatrixUnresolved("p2", p2, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            OW::RefreshScreenSizeFromConfig();

            const uint64_t p3 = SDK->RPM<uint64_t>(p2 + OW::offset::VM_ViewProjectionParent);
            if (!p3) {
                OW::RecordViewMatrixUnresolved("view projection p3", p3, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            const uint64_t p4 = SDK->RPM<uint64_t>(p3 + OW::offset::VM_ViewProjectionPtr);
            if (!p4) {
                OW::RecordViewMatrixUnresolved("view projection p4", p4, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            viewMatrixPtr = p4 + OW::offset::VM_ViewProjectionMatrix;
            viewMatrix_xor_ptr = p2 + OW::offset::VM_ViewMatrix;

            OW::Matrix renderViewProjection = SDK->RPM<OW::Matrix>(viewMatrixPtr);
            OW::Matrix cameraViewMatrix = SDK->RPM<OW::Matrix>(viewMatrix_xor_ptr);

            const bool renderViewProjectionValid = OW::IsMatrixNonIdentity(renderViewProjection);
            const bool cameraViewValid = OW::IsMatrixNonIdentity(cameraViewMatrix);
            OW::viewMatrix = renderViewProjection;
            if (cameraViewValid) {
                OW::viewMatrix_xor = cameraViewMatrix;
            } else if (renderViewProjectionValid) {
                // UC p322-p323 samples set viewMatrix_xor_ptr to the same +0xC0 matrix.
                // Keep that as a last-resort camera source if +0x140 is not populated.
                viewMatrix_xor_ptr = viewMatrixPtr;
                OW::viewMatrix_xor = renderViewProjection;
            } else {
                OW::viewMatrix_xor = cameraViewMatrix;
            }

            const bool viewMatrixValid =
                OW::IsMatrixNonIdentity(OW::viewMatrix) &&
                OW::IsMatrixNonIdentity(OW::viewMatrix_xor);
            OW::RecordViewMatrixResolved(
                viewMatrixPtr,
                viewMatrix_xor_ptr,
                viewMatrixValid,
                hasLastViewMatrixStatus,
                lastViewMatrixValid,
                lastViewMatrixLogTick);

            Sleep(5);
        }
    } __except (1) {}
}

// =========================================================================
// ESP rendering helpers (require ImGui and Render:: namespace)
// =========================================================================

namespace OverlayRenderDetail {

    inline ImU32 ToImU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    inline float Clamp01(float value) {
        return std::clamp(value, 0.0f, 1.0f);
    }

    inline float Lerp(float from, float to, float t) {
        return from + (to - from) * Clamp01(t);
    }

    inline bool IsFiniteVector(const Vector3& value) {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    inline bool IsNonZeroVector(const Vector3& value) {
        return value != Vector3(0.0f, 0.0f, 0.0f);
    }

    inline Vector3 LerpVector(const Vector3& from, const Vector3& to, float t) {
        const float alpha = Clamp01(t);
        return Vector3(
            Lerp(from.X, to.X, alpha),
            Lerp(from.Y, to.Y, alpha),
            Lerp(from.Z, to.Z, alpha));
    }

    inline Vector3 LerpWorldPosition(const Vector3& from, const Vector3& to, float t) {
        if (!IsFiniteVector(from) || !IsFiniteVector(to) ||
            !IsNonZeroVector(from) || !IsNonZeroVector(to)) {
            return to;
        }
        return LerpVector(from, to, t);
    }

    inline Vector3 LerpFiniteVector(const Vector3& from, const Vector3& to, float t) {
        if (!IsFiniteVector(from) || !IsFiniteVector(to))
            return to;
        return LerpVector(from, to, t);
    }

    inline Vector3 SelectInterpolationAnchor(const OW::c_entity& entity, bool previous) {
        const Vector3 position = previous ? entity.previous_pos : entity.pos;
        if (IsFiniteVector(position) && IsNonZeroVector(position))
            return position;

        const Vector3 head = previous ? entity.previous_head_pos : entity.head_pos;
        return head;
    }

    inline bool CanInterpolateEntity(const OW::c_entity& entity) {
        if (!entity.has_previous_render_sample)
            return false;
        if (!IsFiniteVector(entity.head_pos) || !IsFiniteVector(entity.previous_head_pos))
            return false;

        const Vector3 currentAnchor = SelectInterpolationAnchor(entity, false);
        const Vector3 previousAnchor = SelectInterpolationAnchor(entity, true);
        if (!IsFiniteVector(currentAnchor) || !IsFiniteVector(previousAnchor) ||
            !IsNonZeroVector(currentAnchor) || !IsNonZeroVector(previousAnchor)) {
            return false;
        }

        const float movement = previousAnchor.DistTo(currentAnchor);
        return std::isfinite(movement) && movement <= 8.0f;
    }

    inline float EntityInterpolationAlpha(const OW::c_entity& entity, DWORD now) {
        if (!CanInterpolateEntity(entity))
            return 1.0f;

        const DWORD previousTick = entity.previous_render_sample_tick_ms;
        const DWORD currentTick = entity.render_sample_tick_ms;
        if (previousTick == 0 || currentTick == 0 || currentTick <= previousTick)
            return 1.0f;

        const DWORD interval = currentTick - previousTick;
        if (interval < 4 || interval > 250)
            return 1.0f;

        if (now - currentTick > 250)
            return 1.0f;

        constexpr DWORD kInterpolationDelayMs = 16;
        const DWORD renderTick = (now > kInterpolationDelayMs) ? (now - kInterpolationDelayMs) : now;
        if (renderTick <= previousTick)
            return 0.0f;
        if (renderTick >= currentTick)
            return 1.0f;

        return static_cast<float>(renderTick - previousTick) / static_cast<float>(interval);
    }

    inline OW::c_entity InterpolateEntityForRender(const OW::c_entity& source, DWORD now) {
        const float alpha = EntityInterpolationAlpha(source, now);
        if (alpha >= 0.999f)
            return source;

        OW::c_entity entity = source;
        entity.head_pos = LerpWorldPosition(source.previous_head_pos, source.head_pos, alpha);
        entity.velocity = LerpFiniteVector(source.previous_velocity, source.velocity, alpha);
        entity.pos = LerpWorldPosition(source.previous_pos, source.pos, alpha);
        entity.neck_pos = LerpWorldPosition(source.previous_neck_pos, source.neck_pos, alpha);
        entity.chest_pos = LerpWorldPosition(source.previous_chest_pos, source.chest_pos, alpha);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (source.previous_skeleton_bone_valid[i] && source.skeleton_bone_valid[i]) {
                entity.skeleton_bones[i] =
                    LerpWorldPosition(source.previous_skeleton_bones[i], source.skeleton_bones[i], alpha);
                entity.skeleton_bone_valid[i] = true;
            }
        }

        if (source.previous_cached_bot_chest_bone_valid && source.cached_bot_chest_bone_valid) {
            entity.cached_bot_chest_bone =
                LerpWorldPosition(source.previous_cached_bot_chest_bone, source.cached_bot_chest_bone, alpha);
            entity.cached_bot_chest_bone_valid = true;
        }

        return entity;
    }

    inline int ToByte(float value) {
        return static_cast<int>(Clamp01(value) * 255.0f + 0.5f);
    }

    inline Render::Color ToRenderColor(const ImVec4& color) {
        return Render::Color(ToByte(color.x), ToByte(color.y), ToByte(color.z), ToByte(color.w));
    }

    inline float VisibilityAlpha(const OW::c_entity& entity, float opacity) {
        return Clamp01(opacity * (entity.Vis ? 1.0f : 0.30f));
    }

    inline ImVec4 ApplyVisualState(ImVec4 color, const OW::c_entity& entity, float opacity) {
        const float brightness = entity.Vis ? 1.18f : 0.55f;
        color.x = Clamp01(color.x * brightness);
        color.y = Clamp01(color.y * brightness);
        color.z = Clamp01(color.z * brightness);
        color.w = Clamp01(color.w * VisibilityAlpha(entity, opacity));
        return color;
    }

    inline bool IsValidScreenPoint(const Vector2& point) {
        return point.X > 0.0f && point.Y > 0.0f &&
               point.X < OW::WX && point.Y < OW::WY &&
               std::isfinite(point.X) && std::isfinite(point.Y);
    }

    inline bool IsReasonableProjectedPoint(const Vector2& point) {
        return std::isfinite(point.X) && std::isfinite(point.Y) &&
               point.X > -OW::WX && point.X < OW::WX * 2.0f &&
               point.Y > -OW::WY && point.Y < OW::WY * 2.0f;
    }

    struct ProjectedEntityBounds {
        float left = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float centerX = 0.0f;
    };

    struct ProjectedBoundsState {
        bool valid = false;
        float height = 0.0f;
        float width = 0.0f;
        DWORD tick = 0;
    };

    inline bool TryProjectPoint(const Vector3& world, Vector2& screen) {
        if (!IsFiniteVector(world) || !IsNonZeroVector(world))
            return false;
        if (!OW::viewMatrix.WorldToScreen(world, &screen, Vector2(OW::WX, OW::WY)))
            return false;
        return IsReasonableProjectedPoint(screen);
    }

    inline bool TryBuildHeadOffsetBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds) {
        Vector2 low{}, high{};
        const Vector3 head = entity.head_pos;
        if (!TryProjectPoint(Vector3(head.X, head.Y - 1.5f, head.Z), low))
            return false;
        if (!TryProjectPoint(Vector3(head.X, head.Y + 1.0f, head.Z), high))
            return false;

        const float height = std::fabs(low.Y - high.Y);
        const float width = height * 0.85f;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width))
            return false;

        bounds.top = (low.Y < high.Y) ? low.Y : high.Y;
        bounds.bottom = (low.Y > high.Y) ? low.Y : high.Y;
        bounds.centerX = (low.X + high.X) * 0.5f;
        bounds.height = height;
        bounds.width = width;
        bounds.left = bounds.centerX - bounds.width * 0.5f;
        return true;
    }

    inline bool TryBuildProjectedBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds) {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
        int projectedCount = 0;

        auto includeScreenPoint = [&](const Vector2& screen) {
            if (projectedCount == 0) {
                minX = maxX = screen.X;
                minY = maxY = screen.Y;
            } else {
                minX = (screen.X < minX) ? screen.X : minX;
                maxX = (screen.X > maxX) ? screen.X : maxX;
                minY = (screen.Y < minY) ? screen.Y : minY;
                maxY = (screen.Y > maxY) ? screen.Y : maxY;
            }
            ++projectedCount;
        };

        auto includeWorldPoint = [&](const Vector3& world) {
            Vector2 screen{};
            if (TryProjectPoint(world, screen))
                includeScreenPoint(screen);
        };

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                includeWorldPoint(entity.skeleton_bones[i]);
        }
        includeWorldPoint(entity.head_pos);
        includeWorldPoint(entity.neck_pos);
        includeWorldPoint(entity.chest_pos);
        includeWorldPoint(entity.pos);
        if (entity.cached_bot_chest_bone_valid)
            includeWorldPoint(entity.cached_bot_chest_bone);

        if (projectedCount < 3)
            return TryBuildHeadOffsetBounds(entity, bounds);

        float height = maxY - minY;
        if (height <= 8.0f || !std::isfinite(height))
            return TryBuildHeadOffsetBounds(entity, bounds);

        Vector2 headScreen{};
        Vector2 posScreen{};
        const bool hasHead = TryProjectPoint(entity.head_pos, headScreen);
        const bool hasPos = TryProjectPoint(entity.pos, posScreen);
        float centerX = (minX + maxX) * 0.5f;
        if (hasHead && hasPos)
            centerX = (headScreen.X + posScreen.X) * 0.5f;
        else if (hasHead)
            centerX = headScreen.X;

        const float verticalMarginTop = height * 0.08f;
        const float verticalMarginBottom = height * 0.05f;
        const float top = minY - verticalMarginTop;
        const float bottom = maxY + verticalMarginBottom;
        height = bottom - top;

        const float bodyWidth = (maxX - minX) * 1.20f;
        const float minimumBodyWidth = height * 0.55f;
        float width = (bodyWidth > minimumBodyWidth) ? bodyWidth : minimumBodyWidth;
        width = std::clamp(width, height * 0.45f, height * 0.95f);

        if (height <= 2.0f || width <= 2.0f ||
            !std::isfinite(top) || !std::isfinite(bottom) ||
            !std::isfinite(centerX) || !std::isfinite(width)) {
            return false;
        }

        bounds.top = top;
        bounds.bottom = bottom;
        bounds.height = height;
        bounds.width = width;
        bounds.centerX = centerX;
        bounds.left = centerX - width * 0.5f;
        return true;
    }

    inline void StabilizeProjectedBounds(
        uint64_t address,
        ProjectedEntityBounds& bounds,
        DWORD now,
        std::unordered_map<uint64_t, ProjectedBoundsState>& states) {
        auto stateIt = states.find(address);
        if (stateIt != states.end() && stateIt->second.valid &&
            stateIt->second.height > 2.0f && now - stateIt->second.tick <= 250) {
            const DWORD elapsed = now - stateIt->second.tick;
            const float frameScale = std::clamp(static_cast<float>(elapsed) / 16.0f, 1.0f, 4.0f);
            const float maxScaleStep = 1.0f + 0.10f * frameScale;
            const float minHeight = stateIt->second.height / maxScaleStep;
            const float maxHeight = stateIt->second.height * maxScaleStep;
            const float targetHeight = std::clamp(bounds.height, minHeight, maxHeight);
            const float centerY = (bounds.top + bounds.bottom) * 0.5f;

            bounds.height = Lerp(stateIt->second.height, targetHeight, 0.65f);
            bounds.width = std::clamp(bounds.width, bounds.height * 0.45f, bounds.height * 0.95f);
            bounds.top = centerY - bounds.height * 0.5f;
            bounds.bottom = centerY + bounds.height * 0.5f;
            bounds.left = bounds.centerX - bounds.width * 0.5f;
        }

        ProjectedBoundsState next{};
        next.valid = true;
        next.height = bounds.height;
        next.width = bounds.width;
        next.tick = now;
        states.insert_or_assign(address, next);
    }

    inline void PruneProjectedBoundsStates(
        std::unordered_map<uint64_t, ProjectedBoundsState>& states,
        DWORD now) {
        for (auto it = states.begin(); it != states.end();) {
            if (now - it->second.tick > 1000)
                it = states.erase(it);
            else
                ++it;
        }
    }

    inline bool IsSpecialEntity(const OW::c_entity& entity) {
        return entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb;
    }

    inline ImVec4 EntityBaseColor(const OW::c_entity& entity, size_t index) {
        if (entity.Team && OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi)) {
            return OW::Config::targetargb;
        }
        if (entity.Team && !entity.Vis)
            return OW::Config::invisnenargb;
        return entity.Team ? OW::Config::enargb : OW::Config::allyargb;
    }

    inline ImVec4 HealthGradientColor(const OW::c_entity& entity) {
        float ratio = 0.0f;
        if (entity.PlayerHealthMax > 0.0f)
            ratio = Clamp01(entity.PlayerHealth / entity.PlayerHealthMax);

        if (ratio >= 0.5f) {
            const float t = (ratio - 0.5f) * 2.0f;
            return ImVec4(Lerp(1.0f, 0.0f, t), 1.0f, 0.0f, 1.0f);
        }

        const float t = ratio * 2.0f;
        return ImVec4(1.0f, Lerp(0.0f, 1.0f, t), 0.0f, 1.0f);
    }

    inline bool ShouldRenderAtDistance(float distance) {
        if (!OW::Config::dist || OW::Config::visualMaxDist <= 0.0f)
            return true;
        return distance <= OW::Config::visualMaxDist;
    }

    inline float DistanceOpacity(float distance) {
        if (!OW::Config::dist || OW::Config::visualMaxDist <= 0.0f)
            return 1.0f;
        return Clamp01(1.0f - Clamp01(distance / OW::Config::visualMaxDist));
    }

    inline ImU32 EntityColor(const OW::c_entity& entity, size_t index, float opacity = 1.0f) {
        return ToImU32(ApplyVisualState(EntityBaseColor(entity, index), entity, opacity));
    }

    inline ImU32 EntityBoxColor(const OW::c_entity& entity, size_t index, float opacity) {
        if (OW::Config::drawhealth)
            return ToImU32(ApplyVisualState(HealthGradientColor(entity), entity, opacity));
        return EntityColor(entity, index, opacity);
    }

    inline Render::Color EntityRenderColor(const OW::c_entity& entity, size_t index, float opacity = 1.0f) {
        return ToRenderColor(ApplyVisualState(EntityBaseColor(entity, index), entity, opacity));
    }

    inline void DrawCenteredText(const ImVec2& center, ImU32 color, const std::string& text, float fontSize) {
        if (text.empty()) return;
        ImVec2 size = ImGui::CalcTextSize(text.c_str());
        Render::DrawStrokeText(ImVec2(center.x - size.x * 0.5f, center.y), color, text.c_str(), fontSize);
    }

    inline ImColor ImColorWithAlpha(int r, int g, int b, float opacity) {
        return ImColor(r, g, b, ToByte(opacity));
    }

    inline ImU32 ImU32WithAlpha(int r, int g, int b, float opacity) {
        return IM_COL32(r, g, b, ToByte(opacity));
    }

    inline float PositiveFinite(float value) {
        return (std::isfinite(value) && value > 0.0f) ? value : 0.0f;
    }

    inline void DrawStackedResourceBar(const OW::c_entity& entity, float x, float top, float height, float opacity) {
        const float baseHealth = PositiveFinite(entity.MinHealth);
        const float maxHealth = PositiveFinite(entity.MaxHealth);
        const float armor = PositiveFinite(entity.MinArmorHealth);
        const float maxArmor = PositiveFinite(entity.MaxArmorHealth);
        const float barrier = PositiveFinite(entity.MinBarrierHealth);
        const float maxBarrier = PositiveFinite(entity.MaxBarrierHealth);
        const float totalMax = maxHealth + maxArmor + maxBarrier;

        if (totalMax <= 0.0f || height <= 2.0f || opacity <= 0.0f)
            return;

        constexpr float barWidth = 4.0f;
        Render::DrawFilledRect(Vector2(x, top), barWidth, height, ImColorWithAlpha(0, 0, 0, opacity * 0.55f));

        float y = top + height;
        auto drawSegment = [&](float current, float maximum, const ImColor& color) {
            if (current <= 0.0f || maximum <= 0.0f || y <= top)
                return;

            const float clampedValue = (current < maximum) ? current : maximum;
            float segmentHeight = height * Clamp01(clampedValue / totalMax);
            if (segmentHeight <= 0.0f)
                return;
            const float remainingHeight = y - top;
            segmentHeight = (segmentHeight < remainingHeight) ? segmentHeight : remainingHeight;

            y -= segmentHeight;
            Render::DrawFilledRect(Vector2(x, y), barWidth, segmentHeight, color);
            if (y > top + 1.0f)
                Render::DrawFilledRect(Vector2(x, y), barWidth, 1.0f, ImColorWithAlpha(0, 0, 0, opacity * 0.7f));
        };

        drawSegment(baseHealth, maxHealth, ImColorWithAlpha(245, 245, 245, opacity));
        drawSegment(armor, maxArmor, ImColorWithAlpha(255, 150, 35, opacity));
        drawSegment(barrier, maxBarrier, ImColorWithAlpha(70, 170, 255, opacity));

        if (height >= 34.0f) {
            const int rawTicks = static_cast<int>(totalMax / 25.0f);
            const int maxTicks = (rawTicks < 48) ? rawTicks : 48;
            for (int tick = 1; tick < maxTicks; ++tick) {
                const float tickY = top + height - height * ((tick * 25.0f) / totalMax);
                if (tickY > top + 1.0f && tickY < top + height - 1.0f)
                    Render::DrawFilledRect(Vector2(x, tickY), barWidth, 1.0f, ImColorWithAlpha(0, 0, 0, opacity * 0.45f));
            }
        }
    }

    inline std::string CompactHeroIconKey(const std::string& heroName) {
        if (heroName == "D.Va") return "Dva";
        if (heroName == "Soldier 76") return "Soldier76";

        std::string key;
        key.reserve(heroName.size());
        for (char ch : heroName) {
            if (ch != ' ' && ch != '.' && ch != '\'')
                key.push_back(ch);
        }
        return key;
    }

    inline ID3D11ShaderResourceView* FindHeroIcon(const std::string& heroName) {
        IconManager* icons = Render::GetIconManager();
        if (!icons || heroName.empty() || heroName == "Unknown" || heroName == "Bot")
            return nullptr;

        if (ID3D11ShaderResourceView* texture = icons->GetIcon(heroName))
            return texture;

        const std::string compactKey = CompactHeroIconKey(heroName);
        if (compactKey != heroName)
            return icons->GetIcon(compactKey);

        return nullptr;
    }

    inline void DrawUltimateReadyIndicator(const Vector2& center, float opacity) {
        if (opacity <= 0.0f)
            return;

        Render::DrawFilledCircle(center, 13.0f, Render::Color(255, 190, 25, ToByte(opacity * 0.22f)), 40);
        Render::DrawFilledCircle(center, 8.0f, Render::Color(255, 225, 70, ToByte(opacity * 0.48f)), 32);
        Render::DrawFilledCircle(center, 3.4f, Render::Color(255, 255, 235, ToByte(opacity)), 20);

        const Render::Color starColor(255, 245, 120, ToByte(opacity));
        Render::DrawLine(Vector2(center.X - 10.0f, center.Y), Vector2(center.X + 10.0f, center.Y), starColor, 1.3f);
        Render::DrawLine(Vector2(center.X, center.Y - 10.0f), Vector2(center.X, center.Y + 10.0f), starColor, 1.3f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y - 6.0f), Vector2(center.X + 6.0f, center.Y + 6.0f), starColor, 1.0f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y + 6.0f), Vector2(center.X + 6.0f, center.Y - 6.0f), starColor, 1.0f);
    }

    inline void DrawUltimateStatus(const OW::c_entity& entity, const Vector2& indicatorCenter,
                                   float left, float bottom, float opacity) {
        if (!std::isfinite(entity.ultimate) || entity.ultimate < 90.0f)
            return;

        const float ultimate = (entity.ultimate < 100.0f) ? entity.ultimate : 100.0f;
        if (ultimate >= 100.0f)
            DrawUltimateReadyIndicator(indicatorCenter, opacity);

        const std::string text = "ULT: " + std::to_string(static_cast<int>(ultimate + 0.5f)) + "%";
        Render::DrawStrokeText(ImVec2(left - 36.0f, bottom + 5.0f),
                               ImU32WithAlpha(255, 225, 60, opacity), text.c_str(), 12.0f);
    }

    inline bool IsSkillOnCooldown(bool active, float cooldown) {
        return !active && std::isfinite(cooldown) && cooldown > 0.05f;
    }

    inline std::string FormatCooldownLabel(const char* label, float cooldown) {
        char buffer[24] = {};
        std::snprintf(buffer, sizeof(buffer), "%s: %.1fs", label, cooldown);
        return buffer;
    }

    inline std::string CooldownSummary(const OW::c_entity& entity) {
        std::string text;
        if (IsSkillOnCooldown(entity.skill1act, entity.skillcd1))
            text = FormatCooldownLabel("S1", entity.skillcd1);
        if (IsSkillOnCooldown(entity.skill2act, entity.skillcd2)) {
            if (!text.empty()) text += " ";
            text += FormatCooldownLabel("S2", entity.skillcd2);
        }
        return text;
    }

    inline void DrawSkillCooldowns(const OW::c_entity& entity, float x, float y, float opacity) {
        int line = 0;
        const ImU32 cooldownColor = ImU32WithAlpha(255, 230, 120, opacity);
        if (IsSkillOnCooldown(entity.skill1act, entity.skillcd1)) {
            const std::string text = FormatCooldownLabel("S1", entity.skillcd1);
            Render::DrawStrokeText(ImVec2(x, y + line * 13.0f), cooldownColor, text.c_str(), 12.0f);
            ++line;
        }
        if (IsSkillOnCooldown(entity.skill2act, entity.skillcd2)) {
            const std::string text = FormatCooldownLabel("S2", entity.skillcd2);
            Render::DrawStrokeText(ImVec2(x, y + line * 13.0f), cooldownColor, text.c_str(), 12.0f);
        }
    }

    inline void DrawBoneSegment(const Vector2& from, const Vector2& to, const Render::Color& color, float thickness) {
        if (IsValidScreenPoint(from) && IsValidScreenPoint(to))
            Render::DrawLine(from, to, color, thickness);
    }

    inline void DrawSkeleton(const OW::c_entity& entity, const Render::Color& color, float thickness) {
        Vector2 points[18]{};
        bool projected[18]{};
        const Vector2 windowSize(OW::WX, OW::WY);

        for (int i = 0; i < 18; ++i) {
            if (!entity.skeleton_bone_valid[i])
                continue;
            projected[i] = OW::viewMatrix.WorldToScreen(entity.skeleton_bones[i], &points[i], windowSize);
        }

        auto draw = [&](int from, int to) {
            if (from < 0 || from >= 18 || to < 0 || to >= 18) return;
            if (projected[from] && projected[to])
                DrawBoneSegment(points[from], points[to], color, thickness);
        };

        static constexpr std::pair<int, int> kBoneConnections[] = {
            {0, 1}, {1, 2}, {2, 3},
            {1, 4}, {4, 6}, {6, 12},
            {1, 5}, {5, 7}, {7, 13},
            {3, 8}, {8, 10},
            {3, 9}, {9, 11},
        };

        for (const auto& link : kBoneConnections)
            draw(link.first, link.second);
    }

    inline void DrawVisibleEyeIndicator(const Vector2& center, const Render::Color& color) {
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y), Vector2(center.X - 3.0f, center.Y - 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X - 6.0f, center.Y), Vector2(center.X - 3.0f, center.Y + 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X + 6.0f, center.Y), Vector2(center.X + 3.0f, center.Y - 2.0f), color, 1.0f);
        Render::DrawLine(Vector2(center.X + 6.0f, center.Y), Vector2(center.X + 3.0f, center.Y + 2.0f), color, 1.0f);
        Render::DrawCircle(center, 3.2f, color, 16, 1.0f);
        Render::DrawFilledCircle(center, 1.2f, color, 12);
    }

} // namespace OverlayRenderDetail

inline void PlayerInfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    auto local_snapshot = OW::TargetingDetail::SnapshotLocalEntity();

    Diagnostics::PlayerInfoStats renderStats{};
    renderStats.input = entity_snapshot.size();
    static DWORD lastPlayerInfoLogTick = 0;
    static std::unordered_map<uint64_t, OverlayRenderDetail::ProjectedBoundsState> projectedBoundsStates;
    const DWORD renderTick = GetTickCount();

    auto publishStats = [&]() {
        Diagnostics::SetPlayerInfoStats(renderStats);
        if (!OW::PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        if (lastPlayerInfoLogTick == 0 || now - lastPlayerInfoLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 PlayerInfo input=%zu projected=%zu drawn=%zu skip[dead/localhp/self/dist/opacity/w2s/box/window]=%zu/%zu/%zu/%zu/%zu/%zu/%zu/%zu w2s[low/high]=%zu/%zu.",
                renderStats.input,
                renderStats.projected,
                renderStats.drawn,
                renderStats.skippedDead,
                renderStats.skippedLocalHealth,
                renderStats.skippedLocalEntity,
                renderStats.skippedDistance,
                renderStats.skippedOpacity,
                renderStats.skippedWorldToScreen,
                renderStats.skippedBox,
                renderStats.skippedWindow,
                renderStats.skippedWorldToScreenLow,
                renderStats.skippedWorldToScreenHigh);
            lastPlayerInfoLogTick = now;
        }
    };

    if (entity_snapshot.empty()) {
        projectedBoundsStates.clear();
        publishStats();
        return;
    }
    if (OW::WX <= 0.0f || OW::WY <= 0.0f) {
        renderStats.skippedWindow = entity_snapshot.size();
        publishStats();
        return;
    }

    for (size_t index = 0; index < entity_snapshot.size(); ++index) {
        OW::c_entity entity = OverlayRenderDetail::InterpolateEntityForRender(entity_snapshot[index], renderTick);
        if (!entity.Alive) {
            renderStats.skippedDead++;
            continue;
        }
        if (local_snapshot.PlayerHealth <= 0.f) {
            renderStats.skippedLocalHealth++;
            continue;
        }
        if (entity.address == local_snapshot.address) {
            renderStats.skippedLocalEntity++;
            continue;
        }

        Vector3 Vec3 = entity.head_pos;
        float dist = Vector3(OW::viewMatrix_xor.get_location().x,
                             OW::viewMatrix_xor.get_location().y,
                             OW::viewMatrix_xor.get_location().z).DistTo(Vec3);
        if (!OverlayRenderDetail::ShouldRenderAtDistance(dist)) {
            renderStats.skippedDistance++;
            continue;
        }

        const float distanceOpacity = OverlayRenderDetail::DistanceOpacity(dist);
        if (distanceOpacity <= 0.0f) {
            renderStats.skippedOpacity++;
            continue;
        }

        OverlayRenderDetail::ProjectedEntityBounds bounds{};
        if (!OverlayRenderDetail::TryBuildProjectedBounds(entity, bounds)) {
            renderStats.skippedWorldToScreen++;
            continue;
        }

        OverlayRenderDetail::StabilizeProjectedBounds(entity.address, bounds, renderTick, projectedBoundsStates);
        float height = bounds.height;
        float width  = bounds.width;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width)) {
            renderStats.skippedBox++;
            continue;
        }
        renderStats.projected++;

        float top = bounds.top;
        float bottom = bounds.bottom;
        float centerX = bounds.centerX;
        float left = bounds.left;

        ImU32 color = OverlayRenderDetail::EntityColor(entity, index, distanceOpacity);
        ImU32 boxColor = OverlayRenderDetail::EntityBoxColor(entity, index, distanceOpacity);
        Render::Color lineColor = OverlayRenderDetail::EntityRenderColor(entity, index, distanceOpacity);
        const float visualOpacity = OverlayRenderDetail::VisibilityAlpha(entity, distanceOpacity);
        const float outlineThickness = entity.Vis ? 1.8f : 1.2f;
        const float skeletonThickness = entity.Vis ? 1.5f : 1.0f;
        const bool specialEntity = OverlayRenderDetail::IsSpecialEntity(entity);
        bool drewAny = false;

        std::string heroName;
        if (OW::Config::draw_info || OW::Config::skillinfo)
            heroName = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);

        ID3D11ShaderResourceView* heroIcon = nullptr;
        if (OW::Config::draw_info && !specialEntity && heroName != "Unknown")
            heroIcon = OverlayRenderDetail::FindHeroIcon(heroName);

        constexpr float iconSize = 24.0f;
        float labelY = top - 10.0f;
        Vector2 ultimateIndicatorCenter(centerX, top - 18.0f);
        if (heroIcon) {
            const float iconY = top - iconSize - 10.0f;
            ultimateIndicatorCenter = Vector2(centerX, iconY + iconSize * 0.5f);
            labelY = iconY - 13.0f;
        } else if (OW::Config::draw_info && OW::Config::ult && entity.ultimate >= 100.0f) {
            labelY = top - 38.0f;
        }

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            Render::DrawCorneredBox(left, top, width, bottom - top, boxColor, outlineThickness);
            drewAny = true;
        }

        if (OW::Config::drawhealth && OW::Config::healthbar) {
            Render::DrawHealthBar(Vector2(left - 7.0f, top), bottom - top,
                                  entity.PlayerHealth, entity.PlayerHealthMax, visualOpacity);
            OverlayRenderDetail::DrawStackedResourceBar(entity, left - 13.0f, top, bottom - top, visualOpacity);
            drewAny = true;
        }

        if (OW::Config::drawhealth && OW::Config::healthbar2) {
            int shield = static_cast<int>(entity.MinArmorHealth + entity.MinBarrierHealth);
            int maxShield = static_cast<int>(entity.MaxArmorHealth + entity.MaxBarrierHealth);
            Render::DrawSeerLikeHealth(centerX, bottom + 26.0f, shield, maxShield,
                                        static_cast<int>(entity.MinHealth),
                                        static_cast<int>(entity.MaxHealth));
            drewAny = true;
        }

        if (OW::Config::drawline) {
            Render::DrawLine(Vector2(OW::WX * 0.5f, OW::WY), Vector2(centerX, bottom), lineColor, 1.0f);
            drewAny = true;
        }

        if (OW::Config::draw_skel && !specialEntity) {
            OverlayRenderDetail::DrawSkeleton(entity, lineColor, skeletonThickness);
            drewAny = true;
        }

        if (entity.Vis) {
            OverlayRenderDetail::DrawVisibleEyeIndicator(
                Vector2(left + width + 8.0f, top + 7.0f), lineColor);
            drewAny = true;
        }

        if (OW::Config::eyeray) {
            Vector2 eyeStart{}, eyeEnd{};
            Vector3 rayEnd(Vec3.X + sinf(entity.Rot.X) * 5.0f, Vec3.Y, Vec3.Z + cosf(entity.Rot.X) * 5.0f);
            if (OW::viewMatrix.WorldToScreen(Vec3, &eyeStart, Vector2(OW::WX, OW::WY)) &&
                OW::viewMatrix.WorldToScreen(rayEnd, &eyeEnd, Vector2(OW::WX, OW::WY))) {
                Render::DrawLine(eyeStart, eyeEnd, lineColor, 1.0f);
                drewAny = true;
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, bottom + 4.0f), color, distanceText, 14.0f);
            drewAny = true;
        }

        if (OW::Config::draw_info && OW::Config::ult && !specialEntity) {
            OverlayRenderDetail::DrawUltimateStatus(entity, ultimateIndicatorCenter, left, bottom, visualOpacity);
            drewAny = true;
        }

        if (heroIcon) {
            Render::DrawIcon(heroIcon,
                             ImVec2(centerX - iconSize * 0.5f, ultimateIndicatorCenter.Y - iconSize * 0.5f),
                             ImVec2(iconSize, iconSize),
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, visualOpacity));
            drewAny = true;
        }

        if (OW::Config::skillinfo) {
            OverlayRenderDetail::DrawSkillCooldowns(entity, left + width + 8.0f, top + 20.0f, visualOpacity);
            drewAny = true;
        }

        if (OW::Config::draw_info && (OW::Config::name || OW::Config::ult)) {
            std::string label;
            if (OW::Config::name && heroName != "Unknown") {
                label = heroName;
            }
            if (OW::Config::ult && !specialEntity) {
                if (!label.empty()) label += " ";
                label += "Ult " + std::to_string(static_cast<int>(entity.ultimate)) + "%";
            }
            if (!label.empty()) {
                Render::DrawInfo(ImVec2(centerX, labelY), color, 14.0f, label.c_str(),
                                 dist, entity.PlayerHealth, entity.PlayerHealthMax);
                drewAny = true;
            }
        }

        if (drewAny)
            renderStats.drawn++;
    }
    OverlayRenderDetail::PruneProjectedBoundsStates(projectedBoundsStates, renderTick);
    publishStats();
}

inline void skillinfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    if (!OW::Config::skillinfo || entity_snapshot.empty()) return;
    int i = 10;
    for (OW::c_entity entity : entity_snapshot) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Enemy: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            const std::string cooldowns = OverlayRenderDetail::CooldownSummary(entity);
            if (!cooldowns.empty()) info += " " + cooldowns;
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        } else if (entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Enemy Entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        }
    }
    i += 60;
    for (OW::c_entity entity : entity_snapshot) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (!entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Ally: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            const std::string cooldowns = OverlayRenderDetail::CooldownSummary(entity);
            if (!cooldowns.empty()) info += " " + cooldowns;
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        } else if (!entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Ally entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        }
    }
}

// =========================================================================
// Main aimbot thread
// =========================================================================

namespace AimbotDetail {

    struct RuntimeState {
        int hitbotdelaytime = 0;
        int afterdelaytime = 0;
        bool dodelay = false;
    };

    inline OW::c_entity LocalEntity();

    struct ScopedHeroPresetOverride {
        bool active = false;
        OW::Config::HeroPreset original{};

        ScopedHeroPresetOverride() {
            const OW::c_entity local = LocalEntity();
            if (local.HeroID == 0)
                return;

            OW::Config::HeroPreset preset{};
            if (!OW::Config::TryGetHeroPreset(local.HeroID, preset))
                return;

            original = OW::Config::MakeHeroPresetFromCurrent();
            OW::Config::ApplyHeroPresetToGlobals(preset);
            active = true;
        }

        ~ScopedHeroPresetOverride() {
            if (active)
                OW::Config::ApplyHeroPresetToGlobals(original);
        }
    };

    struct AimData {
        Vector3 local_angle{};
        Vector3 target_angle{};
        Vector3 smoothed_angle{};
        Vector3 local_pos{};
    };

    inline bool IsZeroVector(const Vector3& value) {
        return value == Vector3(0, 0, 0);
    }

    inline Vector3 CameraPosition() {
        const auto camera = OW::viewMatrix_xor.get_location();
        return Vector3(camera.x, camera.y, camera.z);
    }

    inline OW::c_entity LocalEntity() {
        return OW::TargetingDetail::SnapshotLocalEntity();
    }

    inline bool HasEntitySnapshot() {
        return !OW::TargetingDetail::SnapshotEntities().empty();
    }

    inline void MaintainSensitivity(float& origin_sens) {
        const uintptr_t sensitive_ptr = OW::GetSenstivePTR();
        if (!sensitive_ptr) return;
        const float current_sens = SDK->RPM<float>(sensitive_ptr);
        if (current_sens) origin_sens = current_sens;
    }

    inline void SetSensitivityLocked(bool locked, float origin_sens) {
        (void)locked;
        (void)origin_sens;
    }

    inline void MoveAimDelta(const Vector3& current_angle, const Vector3& target_angle, int move_time_ms = 5) {
        OW::SendMouseMove(target_angle - current_angle, move_time_ms);
    }

    inline void ClickMouseButton(int button, DWORD sleep_ms = 10) {
        OW::SendMouseButton(button, true);
        Sleep(sleep_ms);
        OW::SendMouseButton(button, false);
    }

    inline void ClickDmaMouseKey(uint32_t key, DWORD sleep_ms = 10) {
        int button = -1;
        if (OW::DmaKeyToMouseButton(key, button)) {
            ClickMouseButton(button, sleep_ms);
        } else {
            OW::SetKey(key);
            Sleep(sleep_ms);
        }
    }

    inline void PressWithSensitivity(uint32_t key, float origin_sens, DWORD sleep_ms = 1) {
        SetSensitivityLocked(true, origin_sens);
        ClickDmaMouseKey(key, sleep_ms);
        SetSensitivityLocked(false, origin_sens);
    }

    inline bool CurrentTarget(c_entity& target, bool requireVisible = true) {
        return OW::TryGetTargetEntity(OW::Config::Targetenemyi, target, requireVisible);
    }

    inline bool IsPrimaryTargetActionable(c_entity& target) {
        if (!CurrentTarget(target, true)) return false;
        if (target.skill2act && target.HeroID == OW::eHero::HERO_GENJI) return false;
        if (target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE) return false;
        if ((target.imort || target.barrprot) && !OW::Config::switch_team) return false;
        return true;
    }

    inline bool IsTriggerTargetActionable() {
        c_entity target{};
        if (!CurrentTarget(target, true)) return false;

        c_entity local = LocalEntity();
        if (target.skill2act &&
            target.HeroID == OW::eHero::HERO_GENJI &&
            target.GetTeam() != local.GetTeam()) {
            return false;
        }
        return true;
    }

    inline AimData BuildAimData(const Vector3& world_target, bool accelerated, float smooth, float acceleration) {
        AimData data{};
        data.local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
        const auto calc_target = OW::CalcAngle(XMFLOAT3(world_target.X, world_target.Y, world_target.Z),
                                               OW::viewMatrix_xor.get_location());
        data.target_angle = Vector3(calc_target.x, calc_target.y, calc_target.z);
        data.smoothed_angle = accelerated
            ? OW::SmoothAccelerate(data.local_angle, data.target_angle, smooth, acceleration)
            : OW::SmoothLinear(data.local_angle, data.target_angle, smooth);
        data.local_pos = CameraPosition();
        return data;
    }

    inline float AimNoise(float divisor) {
        const float direction = (rand() % 10 > 5) ? 1.f : -1.f;
        return direction * static_cast<float>(rand()) / RAND_MAX / divisor;
    }

    inline void ApplyAiAimNoise(Vector3& target, float divisor, bool clampSecondaryFov) {
        if (!OW::Config::aiaim) return;
        target.X += AimNoise(divisor);
        target.Y += AimNoise(divisor);
        target.Z += AimNoise(divisor);

        if (OW::Config::minFov1 > 500.f) OW::Config::minFov1 = 500.f;
        if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
        if (clampSecondaryFov) {
            if (OW::Config::minFov2 > 500.f) OW::Config::minFov2 = 500.f;
            if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
        }
        if (OW::Config::fov360) OW::Config::fov360 = false;
    }

    inline bool TargetDelayReady(RuntimeState* state, bool stampHitDelay, bool resetWhenDisabled) {
        if (!OW::Config::targetdelay) {
            if (resetWhenDisabled && OW::Config::doingdelay) OW::Config::doingdelay = false;
            return true;
        }

        if (OW::Config::lastenemy != OW::Config::Targetenemyi) OW::Config::doingdelay = true;
        if (!OW::Config::doingdelay) return true;

        OW::Config::lastenemy = OW::Config::Targetenemyi;
        if (OW::Config::timebeforedelay == 0) {
            OW::Config::timebeforedelay = GetTickCount();
            return false;
        }

        if (GetTickCount() - OW::Config::timebeforedelay < static_cast<DWORD>(OW::Config::targetdelaytime))
            return false;

        OW::Config::timebeforedelay = 0;
        OW::Config::doingdelay = false;
        if (stampHitDelay && state) state->hitbotdelaytime = GetTickCount();
        return true;
    }

    inline void ArmDelayedShot(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot) return;
        if (OW::Config::shooted || !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) {
            state.dodelay = true;
            state.hitbotdelaytime = 0;
        }
    }

    inline void PrimeDelayedShot(RuntimeState& state) {
        if (state.dodelay && !OW::Config::doingdelay) {
            state.hitbotdelaytime = GetTickCount();
            state.dodelay = false;
        }
    }

    inline bool DelayedShotTimedOut(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot || state.hitbotdelaytime == 0) return false;
        state.afterdelaytime = GetTickCount();
        return state.afterdelaytime - state.hitbotdelaytime > OW::Config::hiboxdelaytime &&
               !OW::Config::doingdelay;
    }

    inline void FirePrimaryNormal() {
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_GENJI || local.HeroID == OW::eHero::HERO_KIRIKO) {
            ClickMouseButton(1);
            if (OW::Config::dontshot) OW::Config::shotcount++;
            return;
        }

        if ((local.HeroID == OW::eHero::HERO_ANA ||
             local.HeroID == OW::eHero::HERO_WIDOWMAKER ||
             local.HeroID == OW::eHero::HERO_ASHE) && GetAsyncKeyState(0x2)) {
            OW::SetKeyscopeHold(0x1, 30);
        } else {
            ClickMouseButton(0);
        }
    }

    inline void FireHanzo() {
        const c_entity local = LocalEntity();
        if (local.skill2act) ClickMouseButton(0);
        else OW::SetKeyHold(0x1000, 100);
    }

    inline void RunAutoScaleFov() {
        if (!OW::Config::autoscalefov) return;

        auto fvec = OW::GetVector3forfov();
        c_entity fov_target{};
        if (IsZeroVector(fvec) || !OW::TryGetTargetEntity(OW::Config::Targetenemyifov, fov_target, true)) {
            OW::Config::Fov = OW::Config::minFov1;
            OW::Config::Fov2 = OW::Config::minFov2;
            return;
        }

        Vector2 high{}, low{};
        if (OW::viewMatrix.WorldToScreen(fov_target.head_pos, &high, Vector2(OW::WX, OW::WY)) &&
            OW::viewMatrix.WorldToScreen(fov_target.chest_pos, &low, Vector2(OW::WX, OW::WY))) {
            OW::Config::Fov = -(high.Y - low.Y) * 4.f;
            if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
            else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;

            OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
            if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
            else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
        } else {
            OW::Config::Fov = OW::Config::minFov1;
            OW::Config::Fov2 = OW::Config::minFov2;
        }
    }

    inline void RunCloseRangeActions(const Vector3& target_pos) {
        const float dist = CameraPosition().DistTo(target_pos);
        if (OW::Config::health <= OW::Config::meleehealth &&
            dist <= OW::Config::meleedistance &&
            OW::Config::AutoMelee) {
            OW::SetKey(0x800);
        }
        if (OW::Config::health <= OW::Config::AutoRMBhealth &&
            dist <= OW::Config::AutoRMBdistance &&
            OW::Config::AutoRMB) {
            ClickMouseButton(1);
        }
    }

    inline void RunTriggerbot(bool secondary, float origin_sens) {
        const Vector3 vec = secondary ? OW::GetVector3aim2(OW::Config::Prediction2)
                                      : OW::GetVector3(OW::Config::Prediction);
        if (IsZeroVector(vec) || !IsTriggerTargetActionable()) return;

        AimData aim = BuildAimData(vec, false, 1.0f, 0.0f);
        const float hitbox = secondary ? OW::Config::hitbox2 : OW::Config::hitbox;
        if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitbox))
            PressWithSensitivity(0x1, origin_sens, 2);
    }

    inline bool ShouldYieldToSecondaryAim() {
        return OW::Config::highPriority && GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2));
    }

    inline void RunTracking(float origin_sens) {
        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::reloading) {
            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            c_entity target{};
            if (!IsZeroVector(vec) && IsPrimaryTargetActionable(target)) {
                AimData aim = BuildAimData(vec, false, OW::Config::Tracking_smooth / 10.f, 0.0f);
                ApplyAiAimNoise(aim.smoothed_angle, 500.f, true);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (!TargetDelayReady(nullptr, false, false)) continue;
                    MoveAimDelta(aim.local_angle,
                                 OW::Config::Rage ? aim.target_angle : aim.smoothed_angle);
                    RunCloseRangeActions(vec);
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
    }

    inline bool RunPrimaryRageShot(const AimData& aim, float origin_sens) {
        if (!OW::Config::Rage) return false;

        if (OW::Config::fakesilent) {
            Vector3 original_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
            MoveAimDelta(original_angle, aim.target_angle);
            PressWithSensitivity(0x1, origin_sens, 25);
            OW::Config::shooted = true;
            MoveAimDelta(aim.target_angle, original_angle);
        } else {
            MoveAimDelta(aim.local_angle, aim.target_angle);
            PressWithSensitivity(0x1, origin_sens, 1);
            OW::Config::shooted = true;
        }
        return true;
    }

    inline bool RunHanzoRageShot(const AimData& aim, float origin_sens) {
        if (!OW::Config::Rage) return false;

        if (OW::Config::fakesilent) {
            Vector3 original_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
            MoveAimDelta(original_angle, aim.target_angle);
            SetSensitivityLocked(true, origin_sens);
            FireHanzo();
            Sleep(25);
            SetSensitivityLocked(false, origin_sens);
            OW::Config::shooted = true;
            MoveAimDelta(aim.target_angle, original_angle);
        } else {
            MoveAimDelta(aim.local_angle, aim.target_angle);
            FireHanzo();
            OW::Config::shooted = true;
        }
        return true;
    }

    inline void RunFlick(RuntimeState& state, float origin_sens) {
        ArmDelayedShot(state);

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) &&
               !OW::Config::shooted &&
               !OW::Config::reloading) {
            if (LocalEntity().HeroID == OW::eHero::HERO_WIDOWMAKER && !GetAsyncKeyState(0x2)) {
                Sleep(1);
                continue;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            if (IsZeroVector(vec)) break;

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                if (!TargetDelayReady(&state, true, true)) continue;
                PrimeDelayedShot(state);

                AimData aim = BuildAimData(vec, true, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (DelayedShotTimedOut(state)) {
                        const c_entity local = LocalEntity();
                        ClickMouseButton((local.HeroID == OW::eHero::HERO_GENJI ||
                                          local.HeroID == OW::eHero::HERO_KIRIKO) ? 1 : 0);
                        OW::Config::shooted = true;
                        continue;
                    }

                    if (RunPrimaryRageShot(aim, origin_sens)) continue;

                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox)) {
                        SetSensitivityLocked(true, origin_sens);
                        FirePrimaryNormal();
                        SetSensitivityLocked(false, origin_sens);
                        OW::Config::shooted = true;
                        if (OW::Config::dontshot) OW::Config::shotcount++;
                        break;
                    }

                    if (OW::Config::dontshot &&
                        OW::Config::shotcount >= OW::Config::shotmanydont &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::missbox)) {
                        OW::Config::shotcount = 0;
                        const c_entity local = LocalEntity();
                        ClickMouseButton((local.HeroID == OW::eHero::HERO_GENJI ||
                                          local.HeroID == OW::eHero::HERO_KIRIKO) ? 1 : 0);
                        OW::Config::shooted = true;
                        continue;
                    }
                }
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
    }

    inline void RunHanzoFlick(RuntimeState& state, float origin_sens) {
        ArmDelayedShot(state);

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::shooted) {
            const Vector3 vec = OW::GetVector3(true);
            if (IsZeroVector(vec)) break;

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                AimData aim = BuildAimData(vec, true, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                PrimeDelayedShot(state);
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (DelayedShotTimedOut(state)) {
                        FireHanzo();
                        OW::Config::shooted = true;
                        continue;
                    }

                    if (RunHanzoRageShot(aim, origin_sens)) continue;
                    if (!TargetDelayReady(&state, true, true)) continue;

                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox)) {
                        SetSensitivityLocked(true, origin_sens);
                        FireHanzo();
                        Sleep(1);
                        if (OW::Config::dontshot) OW::Config::shotcount++;
                        SetSensitivityLocked(false, origin_sens);
                        OW::Config::shooted = true;
                    } else if (OW::Config::dontshot &&
                               OW::Config::shotcount >= OW::Config::shotmanydont &&
                               OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::missbox)) {
                        OW::Config::shotcount = 0;
                        FireHanzo();
                        OW::Config::shooted = true;
                        continue;
                    }
                }
            }

            Sleep(1);
            RunAutoScaleFov();
        }
    }

    inline void RunGenjiBlade() {
        c_entity local = LocalEntity();
        if (!OW::Config::GenjiBlade || !GetAsyncKeyState(0x51) ||
            local.HeroID != OW::eHero::HERO_GENJI ||
            local.ultimate != 100.f) {
            return;
        }

        OW::Config::Qstarttime = GetTickCount();
        OW::Config::Qtime = OW::Config::Qstarttime;
        OW::Config::lastenemy = -1;
        Sleep(1000);

        int detecttoggle = 0;
        int first = 1;
        float speed = 0.f;
        while (OW::Config::GenjiBlade && (OW::Config::Qtime - OW::Config::Qstarttime) <= 7000) {
            local = LocalEntity();
            speed = !local.skillcd1 ? OW::Config::Tracking_smooth : OW::Config::bladespeed;
            OW::Config::Qtime = GetTickCount();

            const Vector3 vec = OW::GetVector3forgenji();
            if (!IsZeroVector(vec)) {
                const float dist = CameraPosition().DistTo(vec);
                if (dist > 20.f) continue;

                AimData aim = BuildAimData(vec, false, speed / 10.f, 0.0f);
                if (!IsZeroVector(aim.smoothed_angle)) {
                    const float dist2 = CameraPosition().DistTo(vec);
                    if ((!local.skillcd1 && dist2 < 20.f) || dist2 < 7.f) {
                        MoveAimDelta(aim.local_angle,
                                     OW::Config::Rage ? aim.target_angle : aim.smoothed_angle);
                    }
                    if (!local.skillcd1 && OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 0.8f)) {
                        if (detecttoggle && !first) {
                            detecttoggle = 0;
                            Sleep(50);
                            continue;
                        }
                        OW::SetKeyHold(0x8, 70);
                        first = 0;
                    }
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f) && dist2 < 5.f)
                        ClickMouseButton(0);
                    if (local.skillcd1 != 0 && !detecttoggle) detecttoggle = 1;
                }
            }
            Sleep(1);
            OW::Config::lastenemy = OW::Config::Targetenemyi;
        }
    }

    inline void RunAutoMelee() {
        if (!OW::Config::AutoMelee) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target, true) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::meleehealth &&
                dist <= OW::Config::meleedistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                OW::SetKey(0x800);
                Sleep(1);
            }
        }
    }

    inline void RunAutoRmb() {
        if (!OW::Config::AutoRMB) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target, true) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::AutoRMBhealth &&
                dist <= OW::Config::AutoRMBdistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                ClickMouseButton(1);
                Sleep(1);
            }
        }
    }

    inline void RunAutoShiftGenji() {
        if (!OW::Config::AutoShiftGenji) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (IsZeroVector(vec) || !CurrentTarget(target, true)) return;
        if (target.imort || target.barrprot) return;
        if (target.HeroID == 0x16dd || target.HeroID == 0x16ee) return;

        const c_entity local = LocalEntity();
        if (local.skillcd1) return;

        const float dist = CameraPosition().DistTo(vec);
        AimData aim = BuildAimData(vec, false, OW::Config::Tracking_smooth / 10.f, 0.0f);
        if (OW::Config::health <= 50.f && dist <= 15.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f))
                OW::SetKeyHold(0x8, 40);
        } else if (OW::Config::health <= 80.f && dist >= 15.f && dist <= 17.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f)) {
                OW::SetKey(0x8);
                Sleep(500);
                OW::SetKey(0x800);
            }
        }
    }

    inline void RunAutoSkill() {
        if (!OW::Config::AutoSkill) return;

        const c_entity local = LocalEntity();
        if (local.PlayerHealth > OW::Config::SkillHealth && OW::Config::skilled)
            OW::Config::skilled = false;
        else if (local.PlayerHealth < OW::Config::SkillHealth && OW::Config::skilled &&
                 local.PlayerHealth < OW::Config::lasthealth &&
                 local.HeroID != OW::eHero::HERO_DOOMFIST)
            OW::Config::skilled = false;

        if (local.PlayerHealth >= OW::Config::SkillHealth || OW::Config::skilled) return;

        const auto hID = local.HeroID;
        if (hID == OW::eHero::HERO_TRACER || hID == OW::eHero::HERO_SOMBRA ||
            hID == OW::eHero::HERO_ROADHOG || hID == OW::eHero::HERO_TORBJORN ||
            hID == OW::eHero::HERO_SOLDIER76 || hID == OW::eHero::HERO_VENTURE) {
            OW::SetKey(0x10);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        } else if (hID == OW::eHero::HERO_REAPER || hID == OW::eHero::HERO_MEI ||
                   hID == OW::eHero::HERO_JUNKERQUEEN || hID == OW::eHero::HERO_MOIRA ||
                   hID == OW::eHero::HERO_ZARYA) {
            OW::SetKey(0x8);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        } else if (hID == OW::eHero::HERO_WINSTON || hID == OW::eHero::HERO_ZENYATTA) {
            OW::SetKey(0x20);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        }
    }

    inline void RunAutoShootCooldown() {
        const c_entity local = LocalEntity();
        if (!OW::Config::AutoShoot || !OW::Config::shooted ||
            (local.HeroID == OW::eHero::HERO_HANJO && !local.skill2act)) {
            return;
        }

        const int rectime = GetTickCount();
        if (OW::Config::lasttime == 0) OW::Config::lasttime = rectime;
        else if (rectime - OW::Config::lasttime >= OW::Config::Shoottime) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }

        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
    }

    inline void ResetShootStateOnRelease() {
        if (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) return;

        OW::Config::shooted = false;
        OW::Config::lasttime = 0;
        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
        OW::Config::Targetenemyi = -1;
    }

    inline void RunReaperReloadCancel() {
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_REAPER && OW::Config::reloading) {
            Sleep(300);
            OW::SetKey(0x800);
        }
    }

    inline void RunSecondAim() {
        if (!OW::Config::secondaim) return;

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)) && !OW::Config::shooted2) {
            const Vector3 vec = OW::GetVector3aim2(OW::Config::Prediction2);
            c_entity target{};
            if (!IsZeroVector(vec) && CurrentTarget(target, true) &&
                !(target.skill2act && target.HeroID == OW::eHero::HERO_GENJI)) {
                AimData aim{};
                if (OW::Config::Tracking2)
                    aim = BuildAimData(vec, false, OW::Config::Tracking_smooth2 / 10.f, 0.0f);
                else if (OW::Config::Flick2)
                    aim = BuildAimData(vec, true, OW::Config::Flick_smooth2 / 10.f, OW::Config::accvalue2);
                else
                    aim = BuildAimData(vec, false, 1.0f, 0.0f);

                if (OW::Config::Rage) aim.smoothed_angle = aim.target_angle;
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    RunCloseRangeActions(vec);
                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::Config::Flick2 &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox2)) {
                        const int tk = OW::Config::togglekey;
                        if (tk == 0)       ClickMouseButton(0);
                        else if (tk == 1)  ClickMouseButton(1);
                        else if (tk == 2)  OW::SetKey(0x8);
                        else if (tk == 3)  OW::SetKey(0x10);
                        else if (tk == 4)  OW::SetKey(0x20);
                        Sleep(1);
                        OW::Config::shooted2 = true;
                    }
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }
            Sleep(1);
        }

        if (OW::Config::shooted2 && !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)))
            OW::Config::shooted2 = false;
    }

    inline void RunAimbotTick(RuntimeState& state, float& origin_sens) {
        if (OW::Config::AntiAFK) {
            OW::SetKey(0x57);
            Sleep(1000);
        }

        if (!HasEntitySnapshot()) return;

        MaintainSensitivity(origin_sens);

        if (OW::Config::triggerbot) RunTriggerbot(false, origin_sens);
        if (OW::Config::triggerbot2) RunTriggerbot(true, origin_sens);

        if (OW::Config::Tracking) RunTracking(origin_sens);
        else if (OW::Config::Flick) RunFlick(state, origin_sens);
        else if (OW::Config::hanzo_flick) RunHanzoFlick(state, origin_sens);

        RunGenjiBlade();
        RunAutoScaleFov();
        RunAutoMelee();
        RunAutoRmb();
        RunAutoShiftGenji();
        RunAutoSkill();
        RunAutoShootCooldown();
        ResetShootStateOnRelease();
        RunReaperReloadCancel();
        RunSecondAim();
    }

    inline void RunAimbotTickWithHeroPreset(RuntimeState& state, float& origin_sens) {
        ScopedHeroPresetOverride heroPresetOverride;
        RunAimbotTick(state, origin_sens);
    }
}

inline void aimbot_thread() {
    __try {
        AimbotDetail::RuntimeState state{};
        static float origin_sens = 0.f;

        while (true) {
            AimbotDetail::RunAimbotTickWithHeroPreset(state, origin_sens);
            Sleep(2);
        }
    } __except (1) {}
}

// =========================================================================
// Config save/load thread
// =========================================================================

namespace OW { namespace Config {
    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
}}

inline void configsavenloadthread() {
    uint64_t lastHeroId = 0;
    while (1) {
        const uint64_t currentHeroId = OW::local_entity.HeroID;
        if (!OW::Config::Menu && currentHeroId != 0 && lastHeroId != currentHeroId) {
            if (lastHeroId != 0) {
                OW::Config::SaveConfigForHero(".\\config.ini", lastHeroId, OW::local_entity.LinkBase);
            }

            OW::Config::LoadConfigForHero(".\\config.ini", currentHeroId, OW::local_entity.LinkBase);
            lastHeroId = currentHeroId;
            OW::Config::nowhero = "Now using: " + OW::GetHeroEngNames(currentHeroId, OW::local_entity.LinkBase);
        } else if (OW::Config::manualsave && lastHeroId != 0) {
            OW::Config::SaveConfigForHero(".\\config.ini", lastHeroId, OW::local_entity.LinkBase);
            OW::Config::manualsave = false;
        }
        Sleep(2);
    }

#if 0
    TCHAR bufsave[100];
    if (OW::Config::lastheroid == -2) {
        OW::Config::lastheroid = 0;
    }
    while (1) {
        if (!OW::Config::Menu && OW::Config::lastheroid != OW::local_entity.HeroID) {
            // Auto-save previous hero config
            if (OW::Config::lastheroid != 0) {
                auto saveHero = [&](const char* section, const char* key, int value) {
                    sprintf(bufsave, "%d",value);
                    WritePrivateProfileStringA(section, key, bufsave, ".\\config.ini");
                };
                auto saveHeroFloat = [&](const char* section, const char* key, float value) {
                    sprintf(bufsave, "%d",(int)(value * 10000));
                    WritePrivateProfileStringA(section, key, bufsave, ".\\config.ini");
                };

                std::string heroName = OW::GetHeroEngNames(OW::Config::lastheroid, OW::local_entity.LinkBase);
                const char* sec = heroName.c_str();

                // Save per-hero settings
                saveHero(sec, "highPriority",  OW::Config::highPriority);
                saveHero(sec, "aiaim",          OW::Config::aiaim);
                saveHero(sec, "hanzoautospeed", OW::Config::hanzoautospeed);
                saveHero(sec, "autoscalefov",   OW::Config::autoscalefov);
                saveHero(sec, "lockontarget",   OW::Config::lockontarget);
                saveHero(sec, "trackc",         OW::Config::trackcompensate);
                saveHeroFloat(sec, "comarea",   OW::Config::comarea);
                saveHeroFloat(sec, "comspeed",  OW::Config::comspeed);
                saveHero(sec, "FOV",            (int)OW::Config::Fov);
                saveHeroFloat(sec, "hitbox",    OW::Config::hitbox);
                saveHeroFloat(sec, "missbox",   OW::Config::missbox);
                saveHeroFloat(sec, "Tracking_smooth", OW::Config::Tracking_smooth);
                saveHeroFloat(sec, "Flick_smooth",    OW::Config::Flick_smooth);
                saveHero(sec, "AutoShootTime",        OW::Config::Shoottime);
                saveHero(sec, "predit_level",         (int)OW::Config::predit_level);
                saveHero(sec, "aim_key",              OW::Config::aim_key);
                saveHero(sec, "Gravitypredit",        OW::Config::Gravitypredit);
                saveHero(sec, "SkillHealth",          (int)OW::Config::SkillHealth);
                saveHero(sec, "AutoSkill",            OW::Config::AutoSkill);
                saveHero(sec, "AntiAFK",              OW::Config::AntiAFK);

                int dec = OW::Config::Tracking ? 0 : OW::Config::Flick ? 1 : OW::Config::hanzo_flick ? 2 : OW::Config::silent ? 3 : 4;
                saveHero(sec, "Aim Mode",    dec);
                saveHero(sec, "autoshootonoff", OW::Config::AutoShoot ? 1 : 0);
                saveHero(sec, "predictdec",     OW::Config::Prediction ? 1 : 0);
                saveHero(sec, "dontshot",       OW::Config::dontshot ? 1 : 0);
                saveHero(sec, "targetdelay",    OW::Config::targetdelay ? 1 : 0);
                saveHero(sec, "targetdelaytime", OW::Config::targetdelaytime);
                saveHero(sec, "dontmanyshot",   OW::Config::shotmanydont);
                saveHero(sec, "hitboxdelayshoot", OW::Config::hitboxdelayshoot);
                saveHero(sec, "hitboxdelaytime",  OW::Config::hiboxdelaytime);

                saveHeroFloat(sec, "recoilnum", OW::Config::recoilnum);
                saveHeroFloat(sec, "accvalue",  OW::Config::accvalue);
                saveHero(sec, "norecoil",    OW::Config::norecoil);
                saveHero(sec, "horizonreco", OW::Config::horizonreco);
                saveHero(sec, "switch_team", OW::Config::switch_team);
                saveHero(sec, "switch_team2", OW::Config::switch_team2);

                saveHero(sec, "Bone",      OW::Config::Bone);
                saveHero(sec, "autobone",  OW::Config::autobone);
                saveHero(sec, "Bone2",     OW::Config::Bone2);
                saveHero(sec, "autobone2", OW::Config::autobone2);
                saveHero(sec, "AutoMelee", OW::Config::AutoMelee);
                saveHeroFloat(sec, "meleedistance", OW::Config::meleedistance);
                saveHeroFloat(sec, "meleehealth",   OW::Config::meleehealth);
                saveHero(sec, "AutoRMB",   OW::Config::AutoRMB);
                saveHeroFloat(sec, "AutoRMBdistance", OW::Config::AutoRMBdistance);
                saveHeroFloat(sec, "AutoRMBhealth",   OW::Config::AutoRMBhealth);

                saveHero(sec, "secondaim",    OW::Config::secondaim);
                saveHero(sec, "triggerbot2",  OW::Config::triggerbot2);
                saveHero(sec, "Tracking2",    OW::Config::Tracking2);
                saveHero(sec, "Flick2",       OW::Config::Flick2);
                saveHero(sec, "Prediction2",  OW::Config::Prediction2);
                saveHero(sec, "Gravitypredit2", OW::Config::Gravitypredit2);
                saveHero(sec, "aim_key2",     OW::Config::aim_key2);
                saveHero(sec, "togglekey",    OW::Config::togglekey);
                saveHeroFloat(sec, "predit_level2",    OW::Config::predit_level2);
                saveHeroFloat(sec, "Tracking_smooth2", OW::Config::Tracking_smooth2);
                saveHeroFloat(sec, "Flick_smooth2",    OW::Config::Flick_smooth2);
                saveHeroFloat(sec, "accvalue2",        OW::Config::accvalue2);
                saveHeroFloat(sec, "hitbox2",          OW::Config::hitbox2);
                saveHeroFloat(sec, "Fov2",             OW::Config::Fov2);

                saveHero(sec, "enablechangefov", OW::Config::enablechangefov);
                saveHeroFloat(sec, "CHANGEFOV",  OW::Config::CHANGEFOV);

                // Genji-specific
                if (OW::Config::lastheroid == OW::eHero::HERO_GENJI) {
                    saveHero(sec, "GenjiBlade",     OW::Config::GenjiBlade);
                    saveHero(sec, "AutoShiftGenji", OW::Config::AutoShiftGenji);
                    saveHeroFloat(sec, "bladespeed", OW::Config::bladespeed);
                }
                // Widow-specific
                if (OW::Config::lastheroid == OW::eHero::HERO_WIDOWMAKER)
                    saveHero(sec, "widowautounscope", OW::Config::widowautounscope);

                // Global settings
                saveHero("Global", "draw_hp_pack",   OW::Config::draw_hp_pack);
                saveHero("Global", "crosscircle",    OW::Config::crosscircle);
                saveHero("Global", "eyeray",         OW::Config::eyeray);
                saveHero("Global", "trackback",      OW::Config::trackback);
                saveHero("Global", "draw_info",      OW::Config::draw_info);
                saveHero("Global", "drawbattletag",  OW::Config::drawbattletag);
                saveHero("Global", "drawhealth",     OW::Config::drawhealth);
                saveHero("Global", "healthbar",      OW::Config::healthbar);
                saveHero("Global", "healthbar2",     OW::Config::healthbar2);
                saveHeroFloat("Global", "healthbartextsize", OW::Config::healthbartextsize);
                saveHero("Global", "dist",           OW::Config::dist);
                saveHero("Global", "name",           OW::Config::name);
                saveHero("Global", "ult",            OW::Config::ult);
                saveHero("Global", "draw_skel",      OW::Config::draw_skel);
                saveHero("Global", "skillinfo",      OW::Config::skillinfo);
                saveHero("Global", "outline",        OW::Config::outline);
                saveHero("Global", "externaloutline", OW::Config::externaloutline);
                saveHero("Global", "teamoutline",    OW::Config::teamoutline);
                saveHero("Global", "healthoutline",  OW::Config::healthoutline);
                saveHero("Global", "rainbowoutline", OW::Config::rainbowoutline);
                saveHero("Global", "draw_edge",      OW::Config::draw_edge);
                saveHero("Global", "drawbox3d",      OW::Config::drawbox3d);
                saveHero("Global", "radar",          OW::Config::radar);
                saveHero("Global", "radarline",      OW::Config::radarline);
                saveHero("Global", "drawline",       OW::Config::drawline);
                saveHero("Global", "draw_fov",       OW::Config::draw_fov);
                saveHero("Global", "MenuToggleKey",  OW::Config::MenuToggleKey);

                // Save colors
                auto saveColor = [&](const char* section, const char* prefix, const ImVec4& c) {
                    sprintf(bufsave, "%d",(int)(c.x * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "x").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.y * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "y").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.z * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "z").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.w * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "w").c_str(), bufsave, ".\\config.ini");
                };
                saveColor("Global", "EnemyCol",    OW::Config::EnemyCol);
                saveColor("Global", "fovcol",      OW::Config::fovcol);
                saveColor("Global", "fovcol2",     OW::Config::fovcol2);
                saveColor("Global", "invisenargb", OW::Config::invisnenargb);
                saveColor("Global", "enargb",      OW::Config::enargb);
                saveColor("Global", "targetargb",  OW::Config::targetargb);
                saveColor("Global", "targetargb2", OW::Config::targetargb2);
                saveColor("Global", "allyargb",    OW::Config::allyargb);

                std::string saveMsg = "Saved: " + heroName;
                // Notification is handled by overlay layer
            }

            // Load config for new hero
            auto loadHero = [&](const char* section, const char* key, int def) -> int {
                return GetPrivateProfileIntA(section, key, def, ".\\config.ini");
            };
            auto loadHeroFloat = [&](const char* section, const char* key, int def) -> float {
                return (float)GetPrivateProfileIntA(section, key, def, ".\\config.ini") / 10000.f;
            };

            std::string heroName = OW::GetHeroEngNames(OW::local_entity.HeroID, OW::local_entity.LinkBase);
            const char* sec = heroName.c_str();

            OW::Config::Fov             = (float)loadHero(sec, "FOV", 200);
            OW::Config::minFov1         = (float)loadHero(sec, "FOV", 200);
            OW::Config::comarea         = loadHeroFloat(sec, "comarea", 100);
            OW::Config::comspeed        = loadHeroFloat(sec, "comspeed", 5000);
            OW::Config::hitbox          = loadHeroFloat(sec, "hitbox", 1300);
            OW::Config::missbox         = loadHeroFloat(sec, "missbox", 6000);
            OW::Config::Tracking_smooth = loadHeroFloat(sec, "Tracking_smooth", 1000);
            OW::Config::Flick_smooth    = loadHeroFloat(sec, "Flick_smooth", 1000);
            OW::Config::Shoottime       = loadHero(sec, "AutoShootTime", 500);
            OW::Config::predit_level    = (float)loadHero(sec, "predit_level", 110);
            OW::Config::aim_key         = loadHero(sec, "aim_key", 6);
            OW::Config::dontshot        = loadHero(sec, "dontshot", 0);
            OW::Config::targetdelay     = loadHero(sec, "targetdelay", 0);
            OW::Config::targetdelaytime = loadHero(sec, "targetdelaytime", 200);
            OW::Config::shotmanydont    = loadHero(sec, "dontmanyshot", 3);
            OW::Config::hitboxdelayshoot = loadHero(sec, "hitboxdelayshoot", 0);
            OW::Config::hiboxdelaytime  = loadHero(sec, "hitboxdelaytime", 200);
            OW::Config::predit_level    = (float)loadHero(sec, "predit_level", 110);
            OW::Config::Gravitypredit   = loadHero(sec, "Gravitypredit", 0);
            OW::Config::SkillHealth     = (float)loadHero(sec, "SkillHealth", 50);
            OW::Config::AutoSkill       = loadHero(sec, "AutoSkill", 0);
            OW::Config::AntiAFK         = loadHero(sec, "AntiAFK", 0);
            OW::Config::recoilnum       = loadHeroFloat(sec, "recoilnum", 5000);
            OW::Config::accvalue        = loadHeroFloat(sec, "accvalue", 1000);
            OW::Config::norecoil        = loadHero(sec, "norecoil", 0);
            OW::Config::horizonreco     = loadHero(sec, "horizonreco", 0);
            OW::Config::switch_team     = loadHero(sec, "switch_team", 0);
            OW::Config::switch_team2    = loadHero(sec, "switch_team2", 0);
            OW::Config::Bone            = loadHero(sec, "Bone", 1);
            OW::Config::autobone        = loadHero(sec, "autobone", 0);
            OW::Config::Bone2           = loadHero(sec, "Bone2", 1);
            OW::Config::autobone2       = loadHero(sec, "autobone2", 0);
            OW::Config::AutoMelee       = loadHero(sec, "AutoMelee", 0);
            OW::Config::meleedistance   = loadHeroFloat(sec, "meleedistance", 5000);
            OW::Config::meleehealth     = loadHeroFloat(sec, "meleehealth", 3000);
            OW::Config::AutoRMB         = loadHero(sec, "AutoRMB", 0);
            OW::Config::AutoRMBdistance = loadHeroFloat(sec, "AutoRMBdistance", 3000);
            OW::Config::AutoRMBhealth   = loadHeroFloat(sec, "AutoRMBhealth", 10000);

            OW::Config::secondaim       = loadHero(sec, "secondaim", 0);
            OW::Config::triggerbot2     = loadHero(sec, "triggerbot2", 0);
            OW::Config::Tracking2       = loadHero(sec, "Tracking2", 0);
            OW::Config::Flick2          = loadHero(sec, "Flick2", 0);
            OW::Config::Prediction2     = loadHero(sec, "Prediction2", 0);
            OW::Config::Gravitypredit2  = loadHero(sec, "Gravitypredit2", 0);
            OW::Config::aim_key2        = loadHero(sec, "aim_key2", 5);
            OW::Config::togglekey       = loadHero(sec, "togglekey", 0);
            OW::Config::predit_level2   = loadHeroFloat(sec, "predit_level2", 11000);
            OW::Config::Tracking_smooth2 = loadHeroFloat(sec, "Tracking_smooth2", 1000);
            OW::Config::Flick_smooth2   = loadHeroFloat(sec, "Flick_smooth2", 1000);
            OW::Config::accvalue2       = loadHeroFloat(sec, "accvalue2", 1000);
            OW::Config::hitbox2         = loadHeroFloat(sec, "hitbox2", 1300);
            OW::Config::Fov2            = (float)loadHero(sec, "Fov2", 200);
            OW::Config::minFov2         = (float)loadHero(sec, "Fov2", 200);

            OW::Config::enablechangefov = loadHero(sec, "enablechangefov", 0);
            OW::Config::CHANGEFOV       = loadHeroFloat(sec, "CHANGEFOV", 1030000);

            OW::Config::lockontarget    = loadHero(sec, "lockontarget", 1);
            OW::Config::trackcompensate = loadHero(sec, "trackc", 0);
            OW::Config::autoscalefov    = loadHero(sec, "autoscalefov", 0);
            OW::Config::highPriority    = loadHero(sec, "highPriority", 0);
            OW::Config::aiaim           = loadHero(sec, "aiaim", 0);
            OW::Config::hanzoautospeed  = loadHero(sec, "hanzoautospeed", 0);

            OW::Config::trackback       = loadHero("Global", "trackback", 0);
            OW::Config::draw_info       = loadHero("Global", "draw_info", 1);
            OW::Config::drawbattletag   = loadHero("Global", "drawbattletag", 0);
            OW::Config::drawhealth      = loadHero("Global", "drawhealth", 1);
            OW::Config::healthbar       = loadHero("Global", "healthbar", 1);
            OW::Config::healthbar2      = loadHero("Global", "healthbar2", 0);
            OW::Config::healthbartextsize = loadHeroFloat("Global", "healthbartextsize", 160000);
            OW::Config::dist            = loadHero("Global", "dist", 1);
            OW::Config::name            = loadHero("Global", "name", 1);
            OW::Config::ult             = loadHero("Global", "ult", 1);
            OW::Config::draw_skel       = loadHero("Global", "draw_skel", 1);
            OW::Config::skillinfo       = loadHero("Global", "skillinfo", 0);
            OW::Config::externaloutline = loadHero("Global", "externaloutline", 0);
            OW::Config::teamoutline     = loadHero("Global", "teamoutline", 0);
            OW::Config::healthoutline   = loadHero("Global", "healthoutline", 0);
            OW::Config::rainbowoutline  = loadHero("Global", "rainbowoutline", 0);
            OW::Config::draw_edge       = loadHero("Global", "draw_edge", 0);
            OW::Config::drawbox3d       = loadHero("Global", "drawbox3d", 0);
            OW::Config::radar           = loadHero("Global", "radar", 0);
            OW::Config::radarline       = loadHero("Global", "radarline", 0);
            OW::Config::drawline        = loadHero("Global", "drawline", 0);
            OW::Config::draw_fov        = loadHero("Global", "draw_fov", 0);
            OW::Config::MenuToggleKey   = loadHero("Global", "MenuToggleKey", VK_HOME);
            OW::Config::eyeray          = loadHero("Global", "eyeray", 0);
            OW::Config::crosscircle     = loadHero("Global", "crosscircle", 0);
            OW::Config::draw_hp_pack    = loadHero("Global", "draw_hp_pack", 0);

            // Load colors
            auto loadColor = [&](const char* section, const char* prefix, ImVec4& c, float dx, float dy, float dz, float dw) {
                c.x = loadHeroFloat(section, (std::string(prefix) + "x").c_str(), (int)(dx * 10000));
                c.y = loadHeroFloat(section, (std::string(prefix) + "y").c_str(), (int)(dy * 10000));
                c.z = loadHeroFloat(section, (std::string(prefix) + "z").c_str(), (int)(dz * 10000));
                c.w = loadHeroFloat(section, (std::string(prefix) + "w").c_str(), (int)(dw * 10000));
            };

            loadColor("Global", "EnemyCol",    OW::Config::EnemyCol,    1.f, 1.f, 1.f, 1.f);
            loadColor("Global", "fovcol",      OW::Config::fovcol,      1.f, 0.9f, 0.f, 1.f);
            loadColor("Global", "fovcol2",     OW::Config::fovcol2,     0.855f, 0.439f, 0.839f, 0.5f);
            loadColor("Global", "invisenargb", OW::Config::invisnenargb, 0.4f, 0.37f, 0.91f, 1.f);
            loadColor("Global", "enargb",      OW::Config::enargb,      1.f, 0.3f, 0.f, 1.f);
            loadColor("Global", "targetargb",  OW::Config::targetargb,  1.f, 1.f, 0.f, 0.8f);
            loadColor("Global", "targetargb2", OW::Config::targetargb2, 1.f, 1.f, 0.4f, 0.8f);
            loadColor("Global", "allyargb",    OW::Config::allyargb,    0.4f, 1.f, 1.f, 0.4f);

            // Restore aim mode
            int dec = loadHero(sec, "Aim Mode", 0);
            OW::Config::Tracking = (dec == 0);
            OW::Config::Flick = (dec == 1);
            OW::Config::hanzo_flick = (dec == 2);
            OW::Config::silent = (dec == 3);
            OW::Config::triggerbot = (dec == 4);

            OW::Config::AutoShoot   = (loadHero(sec, "autoshootonoff", 0) == 1);
            OW::Config::Prediction  = (loadHero(sec, "predictdec", 0) == 1);

            // Genji-specific
            if (OW::local_entity.HeroID == OW::eHero::HERO_GENJI) {
                OW::Config::GenjiBlade     = loadHero(sec, "GenjiBlade", 0);
                OW::Config::AutoShiftGenji = loadHero(sec, "AutoShiftGenji", 0);
                OW::Config::bladespeed     = loadHeroFloat(sec, "bladespeed", 5000);
            } else {
                OW::Config::GenjiBlade = false;
                OW::Config::AutoShiftGenji = false;
            }
            if (OW::local_entity.HeroID == OW::eHero::HERO_WIDOWMAKER)
                OW::Config::widowautounscope = loadHero(sec, "widowautounscope", 0);
            else
                OW::Config::widowautounscope = false;

            OW::Config::lastheroid = OW::local_entity.HeroID;
            Sleep(2);
            OW::Config::nowhero = "Now using: " + heroName;
        } else if (OW::Config::manualsave && OW::Config::lastheroid != 0) {
            // Manual save is handled the same as auto-save above
            OW::Config::manualsave = false;
            std::string heroName = OW::GetHeroEngNames(OW::Config::lastheroid, OW::local_entity.LinkBase);
            std::string saveMsg = "Saved: " + heroName;
            // Notification handled by overlay layer
        }
        Sleep(2);
    }
#endif
}

// =========================================================================
// Loop RPM thread (continuous recoil control / FOV change)
// =========================================================================

inline void looprpmthread() {
    while (1) {
        Sleep(10);
    }
}
