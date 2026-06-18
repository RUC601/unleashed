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
#include <cctype>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <windows.h>
#include "Utils/Diagnostics.hpp"
#include <process.h>
#include <DirectXMath.h>

#include "Game/AbilityIcons.hpp"
#include "Game/BoneSlots.hpp"
#include "Game/HeroPerkRuntime.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/Target.hpp"
#include "Kmbox/KmBoxMock.h"
#include "Renderer/IconManager.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "Memory/KeyState.hpp"

using namespace OW;

// =========================================================================
// Global state variables
// =========================================================================

namespace OW {

    inline std::string HeroDisplayNameToSlug(const std::string& displayName) {
        static const std::unordered_map<std::string, std::string> specialCases = {
            { "Soldier 76", "soldier-76" },
            { "Soldier76", "soldier-76" },
            { "D.Va", "dva" },
            { "DVa", "dva" },
            { "Dva", "dva" },
            { "Hana", "dva" },
            { "Wrecking Ball", "wrecking-ball" },
            { "WreckingBall", "wrecking-ball" },
            { "Junker Queen", "junker-queen" },
            { "JunkerQueen", "junker-queen" },
            { "LifeWeaver", "lifeweaver" },
            { "Lifeweaver", "lifeweaver" },
            { "McCree", "cassidy" },
        };

        const auto special = specialCases.find(displayName);
        if (special != specialCases.end())
            return special->second;

        std::string slug;
        slug.reserve(displayName.size());
        bool previousWasHyphen = false;
        for (const unsigned char value : displayName) {
            if (value == '.' || value == '\'')
                continue;

            if (std::isspace(value) || value == '-' || value == '_') {
                if (!slug.empty() && !previousWasHyphen) {
                    slug.push_back('-');
                    previousWasHyphen = true;
                }
                continue;
            }

            slug.push_back(static_cast<char>(std::tolower(value)));
            previousWasHyphen = false;
        }

        if (!slug.empty() && slug.back() == '-')
            slug.pop_back();

        return slug;
    }

    // ---- View matrices (updated by viewmatrix_thread) ----
    inline uint64_t viewMatrixPtr = 0;
    inline uint64_t viewMatrix_xor_ptr = 0;
    inline Matrix viewMatrix{};
    inline Matrix viewMatrix_xor{};
    inline std::mutex g_viewMatrixMutex;

    inline void SetViewMatrices(const Matrix& vm, const Matrix& vmx) {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        viewMatrix = vm;
        viewMatrix_xor = vmx;
    }

    inline void GetViewMatricesSnapshot(Matrix& vm, Matrix& vmx) {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        vm = viewMatrix;
        vmx = viewMatrix_xor;
    }

    // ---- Entity containers ----
    inline std::vector<c_entity> entities{};
    inline std::vector<hpanddy> hp_dy_entities{};
    inline c_entity local_entity{};

    // ---- Raw entity scan exchange buffer ----
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities{};
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities_scan{};

    // ---- Target/source screen used for projection ----
    inline float WX = 0.f, WY = 0.f;
    inline int detectedScreenWidth = 0;
    inline int detectedScreenHeight = 0;

    // ---- Scan coordination ----
    inline int abletotread = 0;
    inline int howbigentitysize = 0;
    inline constexpr DWORD kEntityScanIntervalMs = 250;
    inline constexpr DWORD kEntityEmptyScanIntervalMs = 50;
    inline constexpr DWORD kEntityProcessIntervalMs = 16;
    inline constexpr DWORD kEntitySlowFieldIntervalMs = 500;
    inline constexpr DWORD kEntityHealthIntervalMs = kEntityProcessIntervalMs;
    inline constexpr DWORD kEntityHeroIntervalMs = 50;
    inline constexpr DWORD kEntityRosterTtlMs = 13000;
    inline constexpr DWORD kCnNeEntityScanStableHoldMs = kEntityRosterTtlMs;
    inline constexpr DWORD kCnNeEntityRosterFreshGraceMs = kEntityRosterTtlMs;
    inline constexpr size_t kCnNeEntityScanStableMaxPairs = 128;
    inline constexpr DWORD kEntityDeadComponentRefreshMs = 250;
    inline constexpr DWORD kEntityLiveComponentRefreshMs = 2000;
    inline constexpr DWORD kEntityFastRescanWindowMs = 2000;
    inline constexpr DWORD kEntityScannerIdleSleepMs = 5;
    inline DWORD entity_fast_scan_until_tick = 0;

    inline Vector2 ResolveScreenSize()
    {
        if (detectedScreenWidth > 0 && detectedScreenHeight > 0) {
            return Vector2(
                static_cast<float>(detectedScreenWidth),
                static_cast<float>(detectedScreenHeight));
        }

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

    inline void SetDetectedScreenSize(int width, int height)
    {
        detectedScreenWidth = width > 0 ? width : 0;
        detectedScreenHeight = height > 0 ? height : 0;
        RefreshScreenSizeFromConfig();
    }

    inline void ClearDetectedScreenSize()
    {
        detectedScreenWidth = 0;
        detectedScreenHeight = 0;
        RefreshScreenSizeFromConfig();
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

    inline bool IsCameraViewMatrixPlausible(const Matrix& matrix)
    {
        if (!IsMatrixNonIdentity(matrix))
            return false;

        const DirectX::XMFLOAT3 camera = matrix.get_location();
        const DirectX::XMFLOAT3 forward = matrix.get_rotation();
        const float cameraLengthSq = camera.x * camera.x + camera.y * camera.y + camera.z * camera.z;
        const float forwardLengthSq = forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;

        return std::isfinite(camera.x) && std::isfinite(camera.y) && std::isfinite(camera.z) &&
            std::isfinite(forward.x) && std::isfinite(forward.y) && std::isfinite(forward.z) &&
            cameraLengthSq > 0.000001f && cameraLengthSq < 1000000000000.0f &&
            forwardLengthSq > 0.0625f && forwardLengthSq < 4.0f;
    }

    inline Matrix MultiplyMatrices(const Matrix& a, const Matrix& b)
    {
        Matrix result{};
        result.m11 = a.m11 * b.m11 + a.m12 * b.m21 + a.m13 * b.m31 + a.m14 * b.m41;
        result.m12 = a.m11 * b.m12 + a.m12 * b.m22 + a.m13 * b.m32 + a.m14 * b.m42;
        result.m13 = a.m11 * b.m13 + a.m12 * b.m23 + a.m13 * b.m33 + a.m14 * b.m43;
        result.m14 = a.m11 * b.m14 + a.m12 * b.m24 + a.m13 * b.m34 + a.m14 * b.m44;
        result.m21 = a.m21 * b.m11 + a.m22 * b.m21 + a.m23 * b.m31 + a.m24 * b.m41;
        result.m22 = a.m21 * b.m12 + a.m22 * b.m22 + a.m23 * b.m32 + a.m24 * b.m42;
        result.m23 = a.m21 * b.m13 + a.m22 * b.m23 + a.m23 * b.m33 + a.m24 * b.m43;
        result.m24 = a.m21 * b.m14 + a.m22 * b.m24 + a.m23 * b.m34 + a.m24 * b.m44;
        result.m31 = a.m31 * b.m11 + a.m32 * b.m21 + a.m33 * b.m31 + a.m34 * b.m41;
        result.m32 = a.m31 * b.m12 + a.m32 * b.m22 + a.m33 * b.m32 + a.m34 * b.m42;
        result.m33 = a.m31 * b.m13 + a.m32 * b.m23 + a.m33 * b.m33 + a.m34 * b.m43;
        result.m34 = a.m31 * b.m14 + a.m32 * b.m24 + a.m33 * b.m34 + a.m34 * b.m44;
        result.m41 = a.m41 * b.m11 + a.m42 * b.m21 + a.m43 * b.m31 + a.m44 * b.m41;
        result.m42 = a.m41 * b.m12 + a.m42 * b.m22 + a.m43 * b.m32 + a.m44 * b.m42;
        result.m43 = a.m41 * b.m13 + a.m42 * b.m23 + a.m43 * b.m33 + a.m44 * b.m43;
        result.m44 = a.m41 * b.m14 + a.m42 * b.m24 + a.m43 * b.m34 + a.m44 * b.m44;
        return result;
    }

    inline bool ProjectionMatrixNeedsCompositionM14Zero(const Matrix& projection)
    {
        return std::isfinite(projection.m11) && std::isfinite(projection.m22) &&
            std::isfinite(projection.m14) && std::isfinite(projection.m24) &&
            std::isfinite(projection.m34) && std::isfinite(projection.m43) &&
            std::isfinite(projection.m44) &&
            std::fabs(projection.m11) > 0.01f &&
            std::fabs(projection.m22) > 0.01f &&
            std::fabs(projection.m14) > 0.5f &&
            std::fabs(projection.m24) < 0.01f &&
            std::fabs(projection.m34 - 1.0f) < 0.05f &&
            std::fabs(projection.m43) > 0.001f &&
            std::fabs(projection.m44) < 0.01f;
    }

    inline Matrix NormalizeProjectionMatrixForComposition(const Matrix& projection)
    {
        Matrix normalized = projection;
        if (ProjectionMatrixNeedsCompositionM14Zero(normalized))
            normalized.m14 = 0.0f;
        return normalized;
    }

    inline Matrix ComposeCameraProjection(const Matrix& cameraView, const Matrix& projection)
    {
        return MultiplyMatrices(cameraView, NormalizeProjectionMatrixForComposition(projection));
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
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityScan);
    Diagnostics::Info("Entity scan thread started. scan_interval_ms=%lu empty_interval_ms=%lu.",
        static_cast<unsigned long>(OW::kEntityScanIntervalMs),
        static_cast<unsigned long>(OW::kEntityEmptyScanIntervalMs));
    size_t lastLoggedScanCount = static_cast<size_t>(-1);
    DWORD lastScanLogTick = 0;
    DWORD lastScanTick = 0;
    DWORD lastScanCycleTick = 0;
    double entityScanHz = 0.0;
    std::unordered_map<uint64_t, std::pair<std::pair<uint64_t, uint64_t>, DWORD>> cnNeStableScanPairs{};

    auto scanPairKey = [](const std::pair<uint64_t, uint64_t>& pair) {
        const uint64_t first = pair.first >> 4;
        const uint64_t second = pair.second >> 4;
        return first ^ (second << 17) ^ (second >> 13);
    };

    auto appendScanPairUnique = [&](std::vector<std::pair<uint64_t, uint64_t>>& pairs,
                                    const std::pair<uint64_t, uint64_t>& pair) {
        if (!pair.first || !pair.second)
            return false;
        if (std::find(pairs.begin(), pairs.end(), pair) != pairs.end())
            return false;
        pairs.push_back(pair);
        return true;
    };

    while (OW::Config::doingentity == 1) {
        if (!OW::ProcessConnection::IsConnected()) {
            Diagnostics::RecordEntityScanCycle(0, 0.0);
            Sleep(100);
            continue;
        }

        const DWORD now = GetTickCount();
        bool pending_scan = false;
        bool known_entities_empty = true;
        bool fast_rescan = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            pending_scan = OW::abletotread != 0;
            known_entities_empty = OW::ow_entities.empty() && OW::ow_entities_scan.empty();
            fast_rescan = OW::entity_fast_scan_until_tick != 0 &&
                now < OW::entity_fast_scan_until_tick;
        }

        const DWORD scanInterval = (known_entities_empty || fast_rescan)
            ? OW::kEntityEmptyScanIntervalMs
            : OW::kEntityScanIntervalMs;
        const bool scanDue = lastScanTick == 0 || now - lastScanTick >= scanInterval;
        if (!pending_scan && scanDue) {
            if (lastScanCycleTick != 0) {
                const DWORD elapsed = now - lastScanCycleTick;
                entityScanHz = elapsed > 0 ? (1000.0 / static_cast<double>(elapsed)) : 0.0;
            }
            lastScanCycleTick = now;
            lastScanTick = now;

            std::vector<std::pair<uint64_t, uint64_t>> scanned = OW::get_ow_entities();
            if (OW::offset::IsCnNeProfile()) {
                const DWORD stableNow = GetTickCount();
                for (const auto& pair : scanned) {
                    if (pair.first && pair.second)
                        cnNeStableScanPairs[scanPairKey(pair)] = std::make_pair(pair, stableNow);
                }

                for (auto it = cnNeStableScanPairs.begin(); it != cnNeStableScanPairs.end();) {
                    const DWORD observedTick = it->second.second;
                    if (observedTick == 0 ||
                        stableNow - observedTick > OW::kCnNeEntityScanStableHoldMs) {
                        it = cnNeStableScanPairs.erase(it);
                    } else {
                        ++it;
                    }
                }

                while (cnNeStableScanPairs.size() > OW::kCnNeEntityScanStableMaxPairs) {
                    auto oldest = cnNeStableScanPairs.begin();
                    for (auto it = cnNeStableScanPairs.begin(); it != cnNeStableScanPairs.end(); ++it) {
                        if (it->second.second < oldest->second.second)
                            oldest = it;
                    }
                    cnNeStableScanPairs.erase(oldest);
                }

                const size_t liveScanCount = scanned.size();
                for (const auto& stablePair : cnNeStableScanPairs)
                    appendScanPairUnique(scanned, stablePair.second.first);
                if (scanned.size() != liveScanCount) {
                    Diagnostics::Trace("CN/NE entity scan stabilized raw=%zu merged=%zu stable=%zu.",
                        liveScanCount,
                        scanned.size(),
                        cnNeStableScanPairs.size());
                }
            }
            Diagnostics::RecordEntityScanCycle(scanned.size(), entityScanHz);
            Diagnostics::Trace("Entity scan cycle found %zu raw entities.", scanned.size());
            if (OW::PipelineDebugEnabled()) {
                const DWORD logNow = GetTickCount();
                if (lastLoggedScanCount != scanned.size() || lastScanLogTick == 0 ||
                    logNow - lastScanLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 3 entity scan raw=%zu scan_hz=%.1f interval_ms=%lu",
                        scanned.size(),
                        entityScanHz,
                        static_cast<unsigned long>(scanInterval));
                    lastLoggedScanCount = scanned.size();
                    lastScanLogTick = logNow;
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
        Sleep(OW::kEntityScannerIdleSleepMs);
    }
    Diagnostics::Info("Entity scan thread stopping.");
}

// =========================================================================
// Entity processing thread (decrypts components, builds c_entity list)
// =========================================================================

inline void entity_thread() {
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::EntityDecrypt);
    Diagnostics::Info("Entity processing thread started. process_interval_ms=%lu health_interval_ms=%lu slow_field_interval_ms=%lu visibility=per_process.",
        static_cast<unsigned long>(OW::kEntityProcessIntervalMs),
        static_cast<unsigned long>(OW::kEntityHealthIntervalMs),
        static_cast<unsigned long>(OW::kEntitySlowFieldIntervalMs));
    DWORD lastProcessTick = 0;
    Vector3 lastpos{};
    size_t lastLoggedRawCount = static_cast<size_t>(-1);
    size_t lastLoggedValidatedCount = static_cast<size_t>(-1);
    size_t previousProcessedValidCount = 0;
    DWORD lastProcessLogTick = 0;
    uint64_t entityCycleCount = 0;
    DWORD entityCycleRateTick = GetTickCount();
    double entityCycleHz = 0.0;

    struct ComponentBaseCache {
        uint64_t linkParent = 0;
        uint64_t health = 0;
        uint64_t link = 0;
        uint64_t team = 0;
        uint64_t transform = 0;
        uint64_t velocity = 0;
        uint64_t hero = 0;
        uint64_t bone = 0;
        uint64_t rotation = 0;
        uint64_t skill = 0;
        uint64_t visibility = 0;
        uint64_t angle = 0;
        uint64_t enemyAngle = 0;
        DWORD baseUpdateTick = 0;
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
        DWORD heroUpdateTick = 0;
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

    struct RosterEntry {
        OW::c_entity entity{};
        bool seenThisCycle = false;
        int skippedJumpObservations = 0;
    };
    std::unordered_map<uint64_t, RosterEntry> entityRoster{};
    entityRoster.reserve(128);

    constexpr uint64_t kRosterMatchPrefix = 0x1000000000000000ull;
    constexpr uint64_t kRosterLinkPrefix = 0x2000000000000000ull;
    constexpr uint64_t kRosterComponentPrefix = 0x3000000000000000ull;

    auto makeRosterKey = [&](uint32_t matchId, uint64_t linkParent, uint64_t componentParent) -> uint64_t {
        if (matchId != 0)
            return kRosterMatchPrefix | static_cast<uint64_t>(matchId);
        if (linkParent != 0)
            return kRosterLinkPrefix ^ linkParent;
        return kRosterComponentPrefix ^ componentParent;
    };

    auto resetRosterCycleFlags = [&]() {
        for (auto& rosterPair : entityRoster)
            rosterPair.second.seenThisCycle = false;
    };

    auto sanitizeStaleRosterEntity = [](OW::c_entity& entity) {
        entity.Alive = false;
        entity.Vis = false;
        entity.Trg = false;
        entity.velocity = Vector3(0, 0, 0);
    };

    constexpr float kObservationTeleportBaseMeters = 8.0f;
    constexpr float kObservationMaxSpeedMetersPerSecond = 120.0f;
    constexpr DWORD kObservationPreviousMaxAgeMs = 250;
    constexpr int kObservationMaxSkippedJumpFrames = 2;
    constexpr float kObservationPointJumpBaseMeters = 3.0f;
    constexpr float kObservationPointJumpAnchorSlackMeters = 2.0f;

    auto isFiniteVector = [](const Vector3& value) {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    };

    auto isUsableVector = [&](const Vector3& value) {
        return isFiniteVector(value) && value != Vector3(0, 0, 0);
    };

    auto observationAnchor = [&](const OW::c_entity& entity) {
        if (isUsableVector(entity.pos))
            return entity.pos;
        if (isUsableVector(entity.chest_pos))
            return entity.chest_pos;
        if (isUsableVector(entity.neck_pos))
            return entity.neck_pos;
        return entity.head_pos;
    };

    auto hasRenderableAnchor = [&](const OW::c_entity& entity) {
        if (isUsableVector(entity.pos) ||
            isUsableVector(entity.head_pos) ||
            isUsableVector(entity.neck_pos) ||
            isUsableVector(entity.chest_pos) ||
            (entity.cached_bot_chest_bone_valid && isUsableVector(entity.cached_bot_chest_bone))) {
            return true;
        }

        for (size_t boneIndex = 0; boneIndex < entity.skeleton_bones.size(); ++boneIndex) {
            if (entity.skeleton_bone_valid[boneIndex] &&
                isUsableVector(entity.skeleton_bones[boneIndex])) {
                return true;
            }
        }
        return false;
    };

    auto observationSampleSeconds = [](DWORD now, DWORD previousTick) {
        if (previousTick == 0 || now <= previousTick)
            return OW::kEntityProcessIntervalMs / 1000.0f;
        return std::clamp((now - previousTick) / 1000.0f, 0.004f, 0.250f);
    };

    auto maxObservationMovement = [&](DWORD now, const OW::c_entity& previous) {
        const float sampleSeconds = observationSampleSeconds(now, previous.last_seen_tick_ms);
        return (std::max)(
            kObservationTeleportBaseMeters,
            kObservationMaxSpeedMetersPerSecond * sampleSeconds);
    };

    auto resetRenderHistory = [](OW::c_entity& entity, DWORD now) {
        entity.render_sample_tick_ms = now;
        entity.previous_render_sample_tick_ms = now;
        entity.position_sample_tick_ms = now;
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
    };

    auto shouldSkipTeleportObservation = [&](const OW::c_entity& current,
                                             const RosterEntry& rosterEntry,
                                             DWORD now) {
        const OW::c_entity& previous = rosterEntry.entity;
        if (!previous.address || !previous.Alive ||
            previous.roster_state != OW::EntityRosterState::Fresh) {
            return false;
        }
        if (previous.HeroID != 0 && current.HeroID != 0 && previous.HeroID != current.HeroID)
            return false;
        if (previous.last_seen_tick_ms == 0 || now - previous.last_seen_tick_ms > kObservationPreviousMaxAgeMs)
            return false;

        const Vector3 previousAnchor = observationAnchor(previous);
        const Vector3 currentAnchor = observationAnchor(current);
        if (!isUsableVector(previousAnchor) || !isUsableVector(currentAnchor))
            return false;

        const float movement = previousAnchor.DistTo(currentAnchor);
        return std::isfinite(movement) && movement > maxObservationMovement(now, previous);
    };

    auto stabilizeObservationPointOutliers = [&](OW::c_entity& current,
                                                 const OW::c_entity& previous,
                                                 DWORD now) {
        if (!previous.address || previous.last_seen_tick_ms == 0 ||
            now - previous.last_seen_tick_ms > kObservationPreviousMaxAgeMs) {
            return static_cast<size_t>(0);
        }

        const Vector3 previousAnchor = observationAnchor(previous);
        const Vector3 currentAnchor = observationAnchor(current);
        if (!isUsableVector(previousAnchor) || !isUsableVector(currentAnchor))
            return static_cast<size_t>(0);

        const float anchorMovement = previousAnchor.DistTo(currentAnchor);
        if (!std::isfinite(anchorMovement) || anchorMovement > maxObservationMovement(now, previous))
            return static_cast<size_t>(0);

        const float maxPointMovement = (std::max)(
            kObservationPointJumpBaseMeters,
            anchorMovement + kObservationPointJumpAnchorSlackMeters);
        size_t suppressed = 0;

        auto suppressPoint = [&](Vector3& point, const Vector3& previousPoint) {
            if (!isUsableVector(point) || !isUsableVector(previousPoint))
                return;
            const float movement = previousPoint.DistTo(point);
            if (std::isfinite(movement) && movement > maxPointMovement) {
                point = previousPoint;
                ++suppressed;
            }
        };

        for (size_t boneIndex = 0; boneIndex < current.skeleton_bones.size(); ++boneIndex) {
            if (!current.skeleton_bone_valid[boneIndex] ||
                !previous.skeleton_bone_valid[boneIndex]) {
                continue;
            }
            suppressPoint(current.skeleton_bones[boneIndex], previous.skeleton_bones[boneIndex]);
        }

        if (current.skeleton_bone_valid[0]) current.head_pos = current.skeleton_bones[0];
        if (current.skeleton_bone_valid[1]) current.neck_pos = current.skeleton_bones[1];
        if (current.skeleton_bone_valid[2]) current.chest_pos = current.skeleton_bones[2];

        suppressPoint(current.pos, previous.pos);
        suppressPoint(current.head_pos, previous.head_pos);
        suppressPoint(current.neck_pos, previous.neck_pos);
        suppressPoint(current.chest_pos, previous.chest_pos);
        if (current.cached_bot_chest_bone_valid && previous.cached_bot_chest_bone_valid)
            suppressPoint(current.cached_bot_chest_bone, previous.cached_bot_chest_bone);

        return suppressed;
    };

    auto publishRosterSnapshot = [&](DWORD now, size_t heroChanged, std::vector<OW::c_entity>& published) {
        Diagnostics::RosterStats rosterStats{};
        rosterStats.heroChanged = heroChanged;
        published.clear();
        published.reserve(entityRoster.size());

        for (auto it = entityRoster.begin(); it != entityRoster.end();) {
            OW::c_entity& rosterEntity = it->second.entity;
            const bool seen = it->second.seenThisCycle;
            const DWORD lastSeen = rosterEntity.last_seen_tick_ms;
            const DWORD unseenAge = lastSeen != 0 ? now - lastSeen : 0;

            if (!seen && lastSeen != 0 && unseenAge > OW::kEntityRosterTtlMs) {
                it = entityRoster.erase(it);
                rosterStats.expired++;
                continue;
            }

            const bool keepFreshDuringCnNeGrace =
                OW::offset::IsCnNeProfile() &&
                !seen &&
                rosterEntity.roster_state == OW::EntityRosterState::Fresh &&
                rosterEntity.Alive &&
                rosterEntity.PlayerHealth > 0.0f &&
                hasRenderableAnchor(rosterEntity) &&
                lastSeen != 0 &&
                unseenAge <= OW::kCnNeEntityRosterFreshGraceMs;

            if (!seen &&
                !keepFreshDuringCnNeGrace &&
                rosterEntity.roster_state != OW::EntityRosterState::Dead) {
                if (rosterEntity.missing_since_tick_ms == 0)
                    rosterEntity.missing_since_tick_ms = now;
                rosterEntity.roster_state = OW::EntityRosterState::Missing;
            }

            if (rosterEntity.roster_state == OW::EntityRosterState::Missing ||
                rosterEntity.roster_state == OW::EntityRosterState::Dead) {
                sanitizeStaleRosterEntity(rosterEntity);
            }

            if (OW::offset::IsCnNeProfile() &&
                rosterEntity.roster_state == OW::EntityRosterState::Fresh &&
                !hasRenderableAnchor(rosterEntity)) {
                it = entityRoster.erase(it);
                rosterStats.expired++;
                continue;
            }

            switch (rosterEntity.roster_state) {
            case OW::EntityRosterState::Fresh:
                rosterStats.fresh++;
                break;
            case OW::EntityRosterState::Dead:
                rosterStats.dead++;
                break;
            case OW::EntityRosterState::Missing:
                rosterStats.missing++;
                break;
            }

            published.push_back(rosterEntity);
            ++it;
        }

        std::sort(published.begin(), published.end(),
            [](const OW::c_entity& lhs, const OW::c_entity& rhs) {
                return lhs.roster_key < rhs.roster_key;
            });

        return rosterStats;
    };

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
        if (!OW::ProcessConnection::IsConnected()) {
            Sleep(100);
            continue;
        }

        const DWORD cycleNow = GetTickCount();
        if (lastProcessTick != 0 && cycleNow - lastProcessTick < OW::kEntityProcessIntervalMs) {
            Sleep(1);
            continue;
        }
        lastProcessTick = cycleNow;

        SDK->BeginFrame();

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OW::abletotread) {
                OW::ow_entities = OW::ow_entities_scan;
                OW::abletotread = 0;
            }
        }

        std::vector<std::pair<uint64_t, uint64_t>> raw_entities;
        std::vector<OW::c_entity> previous_entities;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            raw_entities = OW::ow_entities;
            previous_entities = OW::entities;
        }
        const DWORD processLoopTick = GetTickCount();
        resetRosterCycleFlags();

        // No entities available
        if (raw_entities.empty()) {
            Diagnostics::EntityProcessStats stats{};
            Diagnostics::LocalEntityStats localStats{};
            std::vector<OW::c_entity> published_entities{};
            Diagnostics::RosterStats rosterStats = publishRosterSnapshot(processLoopTick, 0, published_entities);
            previousProcessedValidCount = 0;
            if (componentBaseCache.size() > 512)
                componentBaseCache.clear();
            if (dynamicEntityCache.size() > 512)
                dynamicEntityCache.clear();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                OW::entities = std::move(published_entities);
                OW::hp_dy_entities = {};
                if (rosterStats.fresh > 0 || rosterStats.missing > 0 || rosterStats.dead > 0)
                    OW::entity_fast_scan_until_tick = GetTickCount() + OW::kEntityFastRescanWindowMs;
            }
            Diagnostics::SetEntityCount(rosterStats.fresh + rosterStats.dead + rosterStats.missing);
            Diagnostics::SetEntityProcessStats(stats);
            Diagnostics::SetLocalEntityStats(localStats);
            Diagnostics::SetRosterStats(rosterStats);
            if (OW::PipelineDebugEnabled()) {
                const DWORD now = GetTickCount();
                const bool changed = lastLoggedRawCount != 0 || lastLoggedValidatedCount != 0;
                if (changed || now - lastProcessLogTick >= 1000) {
                    Diagnostics::Info("[PIPELINE] Stage 4 entity processing raw=0 validated=0.");
                    Diagnostics::Info("[PIPELINE] Stage 4 roster fresh=%zu dead=%zu missing=%zu expired=%zu hero_change=%zu.",
                        rosterStats.fresh,
                        rosterStats.dead,
                        rosterStats.missing,
                        rosterStats.expired,
                        rosterStats.heroChanged);
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
        size_t heroChangedThisCycle = 0;
        bool sampledBoneCandidateHasAngle = false;
        const bool detailedProcessLog = OW::PipelineDebugEnabled() &&
            (lastProcessLogTick == 0 || processLoopTick - lastProcessLogTick >= 1000);
        const OW::Vector3 cameraLocation = OW::SnapshotCameraPosition();
        OW::c_entity localCycleSnapshot = OW::SnapshotLocalEntity();
        const auto toCentimeters = [](float value) -> int {
            return std::isfinite(value) ? static_cast<int>(value * 100.0f) : 0;
        };
        localStats.cameraXCm = std::isfinite(cameraLocation.X) ? static_cast<int>(cameraLocation.X * 100.0f) : 0;
        localStats.cameraYCm = std::isfinite(cameraLocation.Y) ? static_cast<int>(cameraLocation.Y * 100.0f) : 0;
        localStats.cameraZCm = std::isfinite(cameraLocation.Z) ? static_cast<int>(cameraLocation.Z * 100.0f) : 0;

        auto recordHealthFailureSample = [&](const OW::c_entity& failedEntity,
                                             uint64_t componentParent,
                                             uint64_t linkParent,
                                             bool healthReadOk) {
            if (processStats.sampleHealthFailComponentParent)
                return;
            processStats.sampleHealthFailComponentParent = componentParent;
            processStats.sampleHealthFailLinkParent = linkParent;
            processStats.sampleHealthFailHealthBase = failedEntity.HealthBase;
            processStats.sampleHealthFailLinkBase = failedEntity.LinkBase;
            processStats.sampleHealthFailVelocityBase = failedEntity.VelocityBase;
            processStats.sampleHealthFailHeroBase = failedEntity.HeroBase;
            processStats.sampleHealthFailTeamBase = failedEntity.TeamBase;
            processStats.sampleHealthFailBoneBase = failedEntity.BoneBase;
            processStats.sampleHealthFailReadOk = healthReadOk ? 1 : 0;
        };
        auto recordNameUnknownSample = [&](const OW::c_entity& unknownEntity,
                                           uint64_t componentParent,
                                           uint64_t linkParent,
                                           const std::string& resolvedName) {
            if (processStats.sampleNameUnknownComponentParent)
                return;
            const auto scanHeroIdCandidate = [](uint64_t heroBase, uint64_t& candidate, int& offset) {
                candidate = 0;
                offset = -1;
                if (!heroBase)
                    return;
                std::array<uint64_t, 0x100> qwords{};
                if (!SDK->read_range(heroBase, qwords.data(), qwords.size() * sizeof(uint64_t)))
                    return;
                for (size_t qwordIndex = 0; qwordIndex < qwords.size(); ++qwordIndex) {
                    const uint64_t value = qwords[qwordIndex];
                    if (OW::GameData::HasHeroIdPrefix(value)) {
                        candidate = value;
                        offset = static_cast<int>(qwordIndex * sizeof(uint64_t));
                        return;
                    }
                }
            };
            auto readHeroId = [](uint64_t heroBase) -> uint64_t {
                if (!heroBase)
                    return 0;
                OW::hero_compo_t heroCompo{};
                if (!SDK->read_range(heroBase, &heroCompo, sizeof(heroCompo)))
                    return 0;
                return heroCompo.heroid;
            };
            uint64_t linkHeroCandidate = 0;
            int linkHeroCandidateOffset = -1;
            scanHeroIdCandidate(unknownEntity.HeroBase, linkHeroCandidate, linkHeroCandidateOffset);
            const uint64_t componentHeroBase =
                OW::DecryptComponent(componentParent, OW::TYPE_P_HEROID);
            const uint64_t componentHeroId = readHeroId(componentHeroBase);
            uint64_t componentHeroCandidate = 0;
            int componentHeroCandidateOffset = -1;
            scanHeroIdCandidate(componentHeroBase, componentHeroCandidate, componentHeroCandidateOffset);

            processStats.sampleNameUnknownComponentParent = componentParent;
            processStats.sampleNameUnknownLinkParent = linkParent;
            processStats.sampleNameUnknownComponentMatchId = unknownEntity.match_id;
            processStats.sampleNameUnknownLinkMatchId = linkParent
                ? SDK->RPM<uint32_t>(linkParent + offset::Entity_MatchId)
                : 0;
            processStats.sampleNameUnknownHeroBase = unknownEntity.HeroBase;
            processStats.sampleNameUnknownHeroId = unknownEntity.HeroID;
            processStats.sampleNameUnknownHeroIdCandidate = linkHeroCandidate;
            processStats.sampleNameUnknownHeroIdCandidateOffset = linkHeroCandidateOffset;
            processStats.sampleNameUnknownComponentHeroBase = componentHeroBase;
            processStats.sampleNameUnknownComponentHeroId = componentHeroId;
            processStats.sampleNameUnknownComponentHeroIdCandidate = componentHeroCandidate;
            processStats.sampleNameUnknownComponentHeroIdCandidateOffset = componentHeroCandidateOffset;
            processStats.sampleNameUnknownLinkBase = unknownEntity.LinkBase;
            processStats.sampleNameUnknownSkillBase = unknownEntity.SkillBase;
            processStats.sampleNameUnknownTeamBase = unknownEntity.TeamBase;
            processStats.sampleNameUnknownBoneBase = unknownEntity.BoneBase;
            processStats.sampleNameUnknownKind = resolvedName.rfind("Hero_", 0) == 0 ? 2 : 1;
        };

        // Also log entity MatchIds for cross-reference on first detailed cycle
        static bool matchIdLogEnabled = true;

        std::unordered_map<uint64_t, const OW::c_entity*> previousEntityByAddress;
        previousEntityByAddress.reserve(previous_entities.size());
        std::unordered_map<uint64_t, const OW::c_entity*> previousEntityByRosterKey;
        previousEntityByRosterKey.reserve(previous_entities.size());
        for (const OW::c_entity& previous : previous_entities) {
            if (previous.address)
                previousEntityByAddress.emplace(previous.address, &previous);
            if (previous.roster_key)
                previousEntityByRosterKey.emplace(previous.roster_key, &previous);
        }

        auto attachPreviousRenderSample = [&](OW::c_entity& entity) {
            entity.render_sample_tick_ms = processLoopTick;

            const OW::c_entity* previousEntity = nullptr;
            const auto previousIt = previousEntityByAddress.find(entity.address);
            if (previousIt != previousEntityByAddress.end())
                previousEntity = previousIt->second;
            if (!previousEntity && entity.roster_key != 0) {
                const auto rosterPreviousIt = previousEntityByRosterKey.find(entity.roster_key);
                if (rosterPreviousIt != previousEntityByRosterKey.end())
                    previousEntity = rosterPreviousIt->second;
            }

            if (!previousEntity) {
                entity.previous_render_sample_tick_ms = processLoopTick;
                entity.position_sample_tick_ms = processLoopTick;
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

            const OW::c_entity& previous = *previousEntity;
            entity.previous_render_sample_tick_ms =
                previous.render_sample_tick_ms ? previous.render_sample_tick_ms : processLoopTick;
            const bool positionChanged =
                isUsableVector(entity.pos) &&
                isUsableVector(previous.pos) &&
                entity.pos.DistTo(previous.pos) > 0.03f;
            entity.position_sample_tick_ms = positionChanged
                ? processLoopTick
                : (previous.position_sample_tick_ms ? previous.position_sample_tick_ms : processLoopTick);
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
            entity.LinkParent = LinkParent;
            if (!entity.address || !LinkParent) {
                processStats.nullPair++;
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            entity.match_id = SDK->RPM<uint32_t>(entity.address + offset::Entity_MatchId);
            entity.roster_key = makeRosterKey(entity.match_id, LinkParent, ComponentParent);

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
                     processLoopTick - cachedDynamic.updateTick >= OW::kEntityProcessIntervalMs)) {
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
            bool componentCacheHit =
                cacheIt != componentBaseCache.end() &&
                cacheIt->second.linkParent == LinkParent;

            if (componentCacheHit && !cacheIt->second.alive && cacheIt->second.health) {
                OW::health_compo_t cachedHealth{};
                if (SDK->read_range(cacheIt->second.health, &cachedHealth, sizeof(cachedHealth))) {
                    const Vector2 cachedHealthExt = cachedHealth.health_ext;
                    const float cachedTotalHealth =
                        cachedHealth.health + cachedHealth.armor + cachedHealth.barrier + cachedHealthExt.Y;
                    if (cachedTotalHealth > 0.0f) {
                        componentBaseCache.erase(cacheIt);
                        cacheIt = componentBaseCache.end();
                        componentCacheHit = false;
                    }
                }
            }
            if (componentCacheHit) {
                const ComponentBaseCache& cachedBases = cacheIt->second;
                const DWORD refreshInterval = cachedBases.alive
                    ? OW::kEntityLiveComponentRefreshMs
                    : OW::kEntityDeadComponentRefreshMs;
                if (cachedBases.baseUpdateTick == 0 ||
                    processLoopTick - cachedBases.baseUpdateTick >= refreshInterval) {
                    componentBaseCache.erase(cacheIt);
                    cacheIt = componentBaseCache.end();
                    componentCacheHit = false;
                }
            }

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
                cache.baseUpdateTick = processLoopTick;
                cache.health     = OW::DecryptComponent(ComponentParent, OW::TYPE_HEALTH, componentSnapshot);
                cache.link       = OW::DecryptComponent(LinkParent, OW::TYPE_LINK, linkSnapshot);
                cache.team       = OW::DecryptComponent(ComponentParent, OW::TYPE_TEAM, componentSnapshot);
                cache.transform  = OW::DecryptComponent(ComponentParent, OW::TYPE_TRANSFORM, componentSnapshot);
                cache.velocity   = OW::DecryptComponent(ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                cache.hero       = OW::DecryptComponent(LinkParent, OW::TYPE_P_HEROID, linkSnapshot);
                cache.bone       = OW::DecryptComponent(ComponentParent, OW::TYPE_BONE, componentSnapshot);
                cache.rotation   = OW::DecryptComponent(ComponentParent, OW::TYPE_ROTATION, componentSnapshot);
                cache.skill      = OW::DecryptComponent(ComponentParent, OW::TYPE_SKILL, componentSnapshot);
                cache.visibility = OW::DecryptComponent(LinkParent, OW::TYPE_P_VISIBILITY, linkSnapshot);
                cache.angle      = OW::DecryptComponent(LinkParent, OW::TYPE_PLAYERCONTROLLER, linkSnapshot);
                cache.enemyAngle = OW::DecryptComponent(ComponentParent, OW::TYPE_ANGLE, componentSnapshot);
                if (LinkParent != ComponentParent) {
                    if (!cache.health)
                        cache.health = OW::DecryptComponent(LinkParent, OW::TYPE_HEALTH, linkSnapshot);
                    if (!cache.team)
                        cache.team = OW::DecryptComponent(LinkParent, OW::TYPE_TEAM, linkSnapshot);
                    if (!cache.transform)
                        cache.transform = OW::DecryptComponent(LinkParent, OW::TYPE_TRANSFORM, linkSnapshot);
                    if (!cache.velocity)
                        cache.velocity = OW::DecryptComponent(LinkParent, OW::TYPE_VELOCITY, linkSnapshot);
                    if (!cache.bone)
                        cache.bone = OW::DecryptComponent(LinkParent, OW::TYPE_BONE, linkSnapshot);
                    if (!cache.rotation)
                        cache.rotation = OW::DecryptComponent(LinkParent, OW::TYPE_ROTATION, linkSnapshot);
                    if (!cache.skill)
                        cache.skill = OW::DecryptComponent(LinkParent, OW::TYPE_SKILL, linkSnapshot);
                    if (!cache.enemyAngle)
                        cache.enemyAngle = OW::DecryptComponent(LinkParent, OW::TYPE_ANGLE, linkSnapshot);
                }
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
                cacheIt->second.baseUpdateTick = 0;
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
                    processLoopTick - componentCache.healthUpdateTick >= OW::kEntityHealthIntervalMs;
                if (refreshHealth) {
                    OW::health_compo_t health_compo{};
                    if (!SDK->read_range(entity.HealthBase, &health_compo, sizeof(health_compo))) {
                        componentBaseCache.erase(ComponentParent);
                        processStats.healthBaseFail++;
                        processStats.healthReadFail++;
                        recordHealthFailureSample(entity, ComponentParent, LinkParent, false);
                        Diagnostics::RecordInvalidEntity();
                        continue;
                    }
                    const Vector2 healthext = health_compo.health_ext;
                    const float totalHealth =
                        health_compo.health + health_compo.armor + health_compo.barrier + healthext.Y;
                    const float totalHealthMax =
                        health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + healthext.X;
                    const bool plausibleHealthLayout =
                        std::isfinite(totalHealth) &&
                        std::isfinite(totalHealthMax) &&
                        totalHealthMax > 0.0f &&
                        totalHealthMax <= 10000.0f &&
                        totalHealth >= 0.0f &&
                        totalHealth <= totalHealthMax + 2000.0f;
                    if (offset::IsCnNeProfile() && !plausibleHealthLayout) {
                        componentBaseCache.erase(ComponentParent);
                        processStats.healthBaseFail++;
                        processStats.healthReadFail++;
                        recordHealthFailureSample(entity, ComponentParent, LinkParent, true);
                        Diagnostics::RecordInvalidEntity();
                        continue;
                    }
                    if (!offset::IsCnNeProfile() && !plausibleHealthLayout) {
                        componentCache.playerHealth = 1.0f;
                        componentCache.playerHealthMax = 1.0f;
                        componentCache.minHealth = 1.0f;
                        componentCache.maxHealth = 1.0f;
                        componentCache.minArmorHealth = 0.0f;
                        componentCache.maxArmorHealth = 0.0f;
                        componentCache.minBarrierHealth = 0.0f;
                        componentCache.maxBarrierHealth = 0.0f;
                        componentCache.alive = true;
                    } else {
                        componentCache.playerHealth = totalHealth;
                        componentCache.playerHealthMax = totalHealthMax;
                        componentCache.minHealth = health_compo.health;
                        componentCache.maxHealth = health_compo.health_max;
                        componentCache.minArmorHealth = health_compo.armor;
                        componentCache.maxArmorHealth = health_compo.armor_max;
                        componentCache.minBarrierHealth = health_compo.barrier;
                        componentCache.maxBarrierHealth = health_compo.barrier_max;
                        componentCache.alive = componentCache.playerHealth > 0.f;
                    }
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
                processStats.healthBaseMissing++;
                recordHealthFailureSample(entity, ComponentParent, LinkParent, false);
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            entity.last_seen_tick_ms = processLoopTick;
            entity.missing_since_tick_ms = 0;
            entity.roster_state = entity.Alive
                ? OW::EntityRosterState::Fresh
                : OW::EntityRosterState::Dead;
            if (entity.roster_state == OW::EntityRosterState::Dead)
                sanitizeStaleRosterEntity(entity);

            // ---- Rotation ----
            if (entity.RotationBase) {
                uint64_t rotPtr = SDK->RPM<uint64_t>(entity.RotationBase + offset::RotationBase_Sub1);
                entity.Rot = SDK->RPM<Vector3>(rotPtr + offset::RotationBase_Sub2);
            }

            // ---- Velocity / position / bones ----
            auto isPlausibleBzPosition = [](const OW::Vector3& position) {
                return std::isfinite(position.X) &&
                       std::isfinite(position.Y) &&
                       std::isfinite(position.Z) &&
                       std::fabs(position.X) < 5000.0f &&
                       std::fabs(position.Y) < 5000.0f &&
                       std::fabs(position.Z) < 5000.0f &&
                       position.DistTo(OW::Vector3(0, 0, 0)) > 1.0f;
            };
            if (velocityRead) {
                entity.pos      = Vector3(velo_compo.location.x, velo_compo.location.y - 1.f, velo_compo.location.z);
                entity.velocity = Vector3(velo_compo.velocity.x, velo_compo.velocity.y, velo_compo.velocity.z);
            }
            if (!offset::IsCnNeProfile()) {
                XMFLOAT3 transformPosition{};
                if (cacheIt->second.transform &&
                    SDK->read_range(cacheIt->second.transform + 0x3D0, &transformPosition, sizeof(transformPosition))) {
                    const OW::Vector3 candidate(
                        transformPosition.x,
                        transformPosition.y,
                        transformPosition.z);
                    if (isPlausibleBzPosition(candidate))
                        entity.pos = candidate;
                }
                if (!isPlausibleBzPosition(entity.pos)) {
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
            }

            // ---- Hero ID ----
            if (entity.HeroBase) {
                ComponentBaseCache& componentCache = cacheIt->second;
                const bool refreshHero =
                    !componentCache.heroValid ||
                    componentCache.heroUpdateTick == 0 ||
                    processLoopTick - componentCache.heroUpdateTick >= OW::kEntityHeroIntervalMs;
                if (refreshHero) {
                    OW::hero_compo_t hero_compo{};
                    if (SDK->read_range(entity.HeroBase, &hero_compo, sizeof(hero_compo))) {
                        const uint64_t newHeroId = hero_compo.heroid;
                        if (newHeroId != 0) {
                            uint64_t previousHeroId = componentCache.heroValid
                                ? componentCache.heroId
                                : 0;
                            if (previousHeroId == 0 && entity.roster_key != 0) {
                                const auto rosterIt = entityRoster.find(entity.roster_key);
                                if (rosterIt != entityRoster.end())
                                    previousHeroId = rosterIt->second.entity.HeroID;
                            }
                            const bool heroChanged =
                                previousHeroId != 0 &&
                                previousHeroId != newHeroId;
                            if (heroChanged) {
                                componentCache.slowValid = false;
                                componentCache.slowUpdateTick = 0;
                                componentCache.heroName = "Unknown";
                                componentCache.skill1act = false;
                                componentCache.skill2act = false;
                                componentCache.ultimate = 0.0f;
                                componentCache.skillcd1 = 0.0f;
                                componentCache.skillcd2 = 0.0f;
                                componentCache.skeletonCache = OW::c_entity::SkeletonBoneCache{};
                                ++heroChangedThisCycle;
                                Diagnostics::Info("[PIPELINE] Stage 4 hero_change roster=0x%llX component=0x%llX old=0x%llX new=0x%llX.",
                                    static_cast<unsigned long long>(entity.roster_key),
                                    static_cast<unsigned long long>(ComponentParent),
                                    static_cast<unsigned long long>(previousHeroId),
                                    static_cast<unsigned long long>(newHeroId));
                            }
                            componentCache.heroId = newHeroId;
                            componentCache.heroValid = true;
                            entity.HeroID = newHeroId;
                        } else if (componentCache.heroValid) {
                            entity.HeroID = componentCache.heroId;
                        }
                        componentCache.heroUpdateTick = processLoopTick;
                    } else if (componentCache.heroValid) {
                        entity.HeroID = componentCache.heroId;
                    }
                } else {
                    entity.HeroID = componentCache.heroId;
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
                } else if (offset::IsCnNeProfile() &&
                           std::fabs(entity.PlayerHealthMax - 200.0f) <= 5.0f) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.HeroID = OW::eHero::HERO_TRAININGBOT4;
                    const float baseY = temppos.y - OW::offset::Velocity_LocationYBias;
                    entity.pos = Vector3(temppos.x, baseY, temppos.z);
                    entity.head_pos = Vector3(temppos.x, baseY + 1.45f, temppos.z);
                    entity.neck_pos = Vector3(temppos.x, baseY + 1.10f, temppos.z);
                    entity.chest_pos = Vector3(temppos.x, baseY + 0.75f, temppos.z);
                    entity.cached_bot_chest_bone = entity.chest_pos;
                    entity.cached_bot_chest_bone_valid = true;
                } else if (offset::IsCnNeProfile() &&
                           std::fabs(entity.PlayerHealthMax - 500.0f) <= 10.0f) {
                    XMFLOAT3 temppos = velocityRead
                        ? velo_compo.location
                        : SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.HeroID = OW::eHero::HERO_TRAININGBOT5;
                    const float baseY = temppos.y - OW::offset::Velocity_LocationYBias;
                    entity.pos = Vector3(temppos.x, baseY, temppos.z);
                    entity.head_pos = Vector3(temppos.x, baseY + 2.00f, temppos.z);
                    entity.neck_pos = Vector3(temppos.x, baseY + 1.55f, temppos.z);
                    entity.chest_pos = Vector3(temppos.x, baseY + 1.05f, temppos.z);
                    entity.cached_bot_chest_bone = entity.chest_pos;
                    entity.cached_bot_chest_bone_valid = true;
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

            const bool syntheticCnNeTrainingBot =
                offset::IsCnNeProfile() &&
                !entity.HeroBase &&
                OW::GameData::IsTrainingBotHeroId(entity.HeroID);

            if (entity.VelocityBase &&
                entity.HeroID != 0x16dd &&
                entity.HeroID != 0x16ee &&
                !syntheticCnNeTrainingBot) {
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

                    const float posDist = cameraLocation.DistTo(entity.pos);
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
                        const float headDist = cameraLocation.DistTo(entity.debugHeadWorld);
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
            if (!offset::IsCnNeProfile() &&
                entity.head_pos == OW::Vector3(0, 0, 0) &&
                isPlausibleBzPosition(entity.pos)) {
                entity.head_pos = entity.pos + OW::Vector3(0.0f, 1.65f, 0.0f);
                entity.neck_pos = entity.pos + OW::Vector3(0.0f, 1.35f, 0.0f);
                entity.chest_pos = entity.pos + OW::Vector3(0.0f, 1.05f, 0.0f);
            }

            if (entity.HeroID == OW::eHero::HERO_WRECKINGBALL) {
                entity.head_pos.Y += 0.02f;
            }

            ComponentBaseCache& slowCache = cacheIt->second;
            const DWORD slowNow = GetTickCount();
            const bool refreshSlowFields =
                !slowCache.slowValid ||
                slowNow - slowCache.slowUpdateTick >= OW::kEntitySlowFieldIntervalMs;
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

            bool isStandardBot = OW::GameData::IsTrainingBotHeroId(entity.HeroID);
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
            if (OW::Config::drawbattletag) {
                entity.statcombase = OW::DecryptComponent(LinkParent, OW::TYPE_STAT, linkSnapshot);
                if (entity.statcombase && entity != localCycleSnapshot) {
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
                        static_cast<unsigned long long>(localCycleSnapshot.TeamBase));
                }
                auto team = entity.GetTeam();
                entity.Team = (team == OW::eTeam::TEAM_DEATHMATCH || !entity.SameTeamAs(localCycleSnapshot));
                if (OW::offset::IsCnNeProfile() && OW::GameData::IsFriendlyTrainingBotHeroId(entity.HeroID))
                    entity.Team = false;
                slowCache.isEnemy = entity.Team;
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu team_done team=%d enemy=%d.",
                        i,
                        static_cast<int>(team),
                        entity.Team ? 1 : 0);
                }
            }

            // ---- Visibility (refreshed every frame) ----
            // DecryptVis owns profile-specific polarity. CN/NE uses visComp+0x98
            // as a live-verified raw bool state: raw == 1 means visible.
            if (entity.VisBase) {
                const uint64_t rawVis = OW::DecryptVis(entity.VisBase);
                entity.Vis = (rawVis == 1);
                slowCache.vis = entity.Vis;
                if (rawVis != 0 && rawVis != 1) {
                    Diagnostics::Aim("visibility.anomaly raw=%llu addr=0x%llX hero=0x%llX visbase=0x%llX",
                        static_cast<unsigned long long>(rawVis),
                        static_cast<unsigned long long>(entity.address),
                        static_cast<unsigned long long>(entity.HeroID),
                        static_cast<unsigned long long>(entity.VisBase));
                }
            } else if (syntheticCnNeTrainingBot) {
                entity.Vis = true;
                slowCache.vis = true;
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
                if (detailedProcessLog && processStats.boneCandidates > 0) {
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu skill_done.", i);
                }
            }

            // Sombra stealth affects the effective visibility state, so keep it
            // on the per-process path with DecryptVis instead of the slow cache.
            if (entity.HeroID == OW::eHero::HERO_SOMBRA && entity.Team && entity.SkillBase) {
                entity.Vis = (entity.Vis && !OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x7C5));
                slowCache.vis = entity.Vis;
            }

            // ---- Player controller / local entity detection ----
            // AngleBase is produced from the PlayerController component and is
            // the primary local-player anchor. Camera proximity remains a
            // secondary diagnostic fallback.
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

                float dist = cameraLocation.DistTo(entity.head_pos);
                const int distanceCm = std::isfinite(dist)
                    ? static_cast<int>(dist * 100.0f + 0.5f)
                    : -1;

                const std::string& localHeroName = name;
                if (localHeroName != "Unknown")
                    localStats.namedCandidates++;

                // Log MatchId cross-reference on first cycle (diagnostic only)
                if (matchIdLogEnabled) {
                    const uint32_t eMatchId = entity.match_id;
                    if (eMatchId != 0) {
                        Diagnostics::Info("[MATCHID] entity=0x%llX MatchId=0x%08X(%u) hero=%s angle=0x%llX dist=%.1fm.",
                            static_cast<unsigned long long>(entity.address),
                            static_cast<unsigned int>(eMatchId),
                            static_cast<unsigned int>(eMatchId),
                            localHeroName.c_str(),
                            static_cast<unsigned long long>(entity.AngleBase),
                            static_cast<double>(dist));
                    }
                }
                // Track proximity stats
                if (dist <= 1.f)
                    localStats.nearCameraCandidates++;
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

                // Selection priority:
                // 1. First AngleBase candidate (only local has PlayerController)
                // 2. Proximity fallback (within 1m + known name)
                const bool isNearCamera = dist <= 1.f;
                const bool hasKnownName = localHeroName != "Unknown";
                const bool shouldSelectLocal =
                    localStats.selected == 0 ||
                    (isNearCamera && hasKnownName);

                if (shouldSelectLocal) {
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
                    localCycleSnapshot = entity;
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
                    Diagnostics::Info("[PIPELINE] Stage 4 progress idx=%zu local_done selected=%zu.",
                        i, localStats.selected);
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
            if (name.rfind("Hero_", 0) == 0) {
                processStats.nameUnknown++;
                recordNameUnknownSample(entity, ComponentParent, LinkParent, name);
            }
            const bool hasRenderablePosition =
                !OW::offset::IsCnNeProfile() || hasRenderableAnchor(entity);
            if (ComponentParent && LinkParent && name != "Unknown" && hasRenderablePosition) {
                entity.roster_state = entity.Alive
                    ? OW::EntityRosterState::Fresh
                    : OW::EntityRosterState::Dead;
                entity.last_seen_tick_ms = processLoopTick;
                entity.missing_since_tick_ms = 0;
                if (entity.roster_state == OW::EntityRosterState::Dead)
                    sanitizeStaleRosterEntity(entity);
                RosterEntry& rosterEntry = entityRoster[entity.roster_key];

                if (entity.roster_state == OW::EntityRosterState::Fresh &&
                    shouldSkipTeleportObservation(entity, rosterEntry, processLoopTick)) {
                    if (rosterEntry.skippedJumpObservations < kObservationMaxSkippedJumpFrames) {
                        OW::c_entity held = rosterEntry.entity;
                        held.last_seen_tick_ms = processLoopTick;
                        held.missing_since_tick_ms = 0;
                        held.roster_state = OW::EntityRosterState::Fresh;
                        held.velocity = Vector3(0, 0, 0);
                        resetRenderHistory(held, processLoopTick);
                        rosterEntry.entity = held;
                        rosterEntry.seenThisCycle = true;
                        rosterEntry.skippedJumpObservations++;
                        tmp_entities.push_back(held);
                        if (detailedProcessLog) {
                            Diagnostics::Info("[PIPELINE] Stage 4 observation_gate skip_jump idx=%zu roster=0x%llX skipped=%d.",
                                i,
                                static_cast<unsigned long long>(entity.roster_key),
                                rosterEntry.skippedJumpObservations);
                        }
                        continue;
                    }

                    resetRenderHistory(entity, processLoopTick);
                    if (detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 observation_gate accept_jump_reset idx=%zu roster=0x%llX skipped=%d.",
                            i,
                            static_cast<unsigned long long>(entity.roster_key),
                            rosterEntry.skippedJumpObservations);
                    }
                } else {
                    const size_t suppressedPoints = stabilizeObservationPointOutliers(
                        entity,
                        rosterEntry.entity,
                        processLoopTick);
                    if (suppressedPoints > 0 && detailedProcessLog) {
                        Diagnostics::Info("[PIPELINE] Stage 4 observation_gate suppress_points idx=%zu roster=0x%llX count=%zu.",
                            i,
                            static_cast<unsigned long long>(entity.roster_key),
                            suppressedPoints);
                    }
                    attachPreviousRenderSample(entity);
                }

                rosterEntry.entity = entity;
                rosterEntry.seenThisCycle = true;
                rosterEntry.skippedJumpObservations = 0;
                tmp_entities.push_back(entity);
            } else {
                if (name == "Unknown") {
                    processStats.nameUnknown++;
                    recordNameUnknownSample(entity, ComponentParent, LinkParent, name);
                }
                Diagnostics::RecordInvalidEntity();
            }
        }

        // Swap processed entities
        const size_t valid_count = tmp_entities.size();
        const size_t dynamic_count = hpdy_entities.size();
        processStats.validated = valid_count;
        processStats.dynamic = dynamic_count;
        std::vector<OW::c_entity> published_entities{};
        Diagnostics::RosterStats rosterStats =
            publishRosterSnapshot(processLoopTick, heroChangedThisCycle, published_entities);
        const size_t published_count = published_entities.size();
        const bool suspectStaleScan =
            (valid_count == 0 && !raw_entities.empty()) ||
            (previousProcessedValidCount > 0 && valid_count + 1 < previousProcessedValidCount);
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            OW::entities = std::move(published_entities);
            OW::hp_dy_entities = std::move(hpdy_entities);
            if (suspectStaleScan)
                OW::entity_fast_scan_until_tick = GetTickCount() + OW::kEntityFastRescanWindowMs;
        }
        previousProcessedValidCount = valid_count;
        Diagnostics::SetEntityCount(published_count);
        Diagnostics::SetEntityProcessStats(processStats);
        Diagnostics::SetLocalEntityStats(localStats);
        Diagnostics::SetRosterStats(rosterStats);
        if (matchIdLogEnabled)
            matchIdLogEnabled = false;
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
                Diagnostics::Info("[PIPELINE] Stage 4 timing scan_ms=%lu process_ms=%lu health_ms=%lu slow_ms=%lu visibility=per_process.",
                    static_cast<unsigned long>(OW::kEntityScanIntervalMs),
                    static_cast<unsigned long>(OW::kEntityProcessIntervalMs),
                    static_cast<unsigned long>(OW::kEntityHealthIntervalMs),
                    static_cast<unsigned long>(OW::kEntitySlowFieldIntervalMs));
                Diagnostics::Info("[PIPELINE] Stage 4 roster fresh=%zu dead=%zu missing=%zu expired=%zu hero_change=%zu published=%zu.",
                    rosterStats.fresh,
                    rosterStats.dead,
                    rosterStats.missing,
                    rosterStats.expired,
                    rosterStats.heroChanged,
                    published_count);
                if (suspectStaleScan) {
                    Diagnostics::Info("[PIPELINE] Stage 4 suspected stale scan/cache; fast rescan for %lu ms.",
                        static_cast<unsigned long>(OW::kEntityFastRescanWindowMs));
                }
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
    Diagnostics::ScopedDmaCallsite::Push(Diagnostics::DmaCallsite::ViewMatrix);
    DWORD lastViewMatrixLogTick = 0;
    bool hasLastViewMatrixStatus = false;
    bool lastViewMatrixValid = false;

    __try {
        while (true) {
            if (!OW::ProcessConnection::IsConnected()) {
                Diagnostics::SetViewMatrixStatus(false, false);
                Sleep(100);
                continue;
            }

            // World/BZ uses a two-key add-XOR root with a direct render
            // view-projection candidate. CN/NE reads the root pointer directly
            // and still follows the camera/projection chain.
            const auto& activeOffsets = OW::offset::Active();
            if (!activeOffsets.Address_viewmatrix_base) {
                OW::RecordViewMatrixUnresolved("profile viewmatrix unresolved", 0, lastViewMatrixLogTick);
                Sleep(100);
                continue;
            }

            const auto viewMatrixMode = activeOffsets.viewMatrixMode;
            const bool directViewMatrixRoot =
                viewMatrixMode == OW::offset::ViewMatrixMode::DirectChain;
            const bool subXorSubEncryptedRoot =
                viewMatrixMode == OW::offset::ViewMatrixMode::EncryptedChainSubXorSub;
            uint64_t root = SDK->RPM<uint64_t>(SDK->dwGameBase + activeOffsets.Address_viewmatrix_base);
            if (!root) {
                OW::RecordViewMatrixUnresolved(
                    directViewMatrixRoot ? "direct root pointer" : "encrypted base pointer",
                    root,
                    lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }
            uint64_t dec = directViewMatrixRoot
                ? root
                : (subXorSubEncryptedRoot
                    ? ((root - activeOffsets.offset_viewmatrix_xor_key)
                        ^ activeOffsets.offset_viewmatrix_xor_key2) -
                        activeOffsets.offset_viewmatrix_xor_key3
                    : ((root + activeOffsets.offset_viewmatrix_xor_key)
                        ^ activeOffsets.offset_viewmatrix_xor_key2));
            if (!dec) {
                OW::RecordViewMatrixUnresolved("decoded base pointer", dec, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            OW::RefreshScreenSizeFromConfig();

            if (viewMatrixMode == OW::offset::ViewMatrixMode::EncryptedDirectMatrix) {
                viewMatrixPtr = dec + activeOffsets.VM_DirectMatrix;
                const uint64_t cameraViewCandidatePtr = activeOffsets.VM_ViewMatrix
                    ? dec + activeOffsets.VM_ViewMatrix
                    : 0;

                OW::Matrix renderViewProjection = SDK->RPM<OW::Matrix>(viewMatrixPtr);
                const bool renderViewProjectionValid = OW::IsMatrixNonIdentity(renderViewProjection);
                if (!renderViewProjectionValid) {
                    OW::RecordViewMatrixUnresolved("direct render view-projection", viewMatrixPtr, lastViewMatrixLogTick);
                    Sleep(5);
                    continue;
                }

                OW::Matrix cameraViewMatrix{};
                bool cameraViewValid = false;
                if (cameraViewCandidatePtr) {
                    cameraViewMatrix = SDK->RPM<OW::Matrix>(cameraViewCandidatePtr);
                    cameraViewValid = OW::IsCameraViewMatrixPlausible(cameraViewMatrix);
                }
                viewMatrix_xor_ptr = cameraViewValid ? cameraViewCandidatePtr : 0;

                OW::Matrix publishedCameraView{};
                OW::g_viewMatrixMutex.lock();
                OW::viewMatrix = renderViewProjection;
                OW::viewMatrix_xor = cameraViewValid ? cameraViewMatrix : OW::Matrix{};
                publishedCameraView = OW::viewMatrix_xor;
                OW::g_viewMatrixMutex.unlock();

                const bool cameraAvailable = OW::IsCameraViewMatrixPlausible(publishedCameraView);
                OW::RecordViewMatrixResolved(
                    viewMatrixPtr,
                    cameraAvailable ? viewMatrix_xor_ptr : 0,
                    renderViewProjectionValid,
                    hasLastViewMatrixStatus,
                    lastViewMatrixValid,
                    lastViewMatrixLogTick);

                Sleep(5);
                continue;
            }

            uint64_t p1 = SDK->RPM<uint64_t>(dec + activeOffsets.VM_P1);
            if (!p1) {
                OW::RecordViewMatrixUnresolved("p1", p1, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }
            uint64_t p2 = SDK->RPM<uint64_t>(p1 + activeOffsets.VM_P2);
            if (!p2) {
                OW::RecordViewMatrixUnresolved("p2", p2, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            OW::RefreshScreenSizeFromConfig();

            const uint64_t p3 = SDK->RPM<uint64_t>(p2 + activeOffsets.VM_ViewProjectionParent);
            if (!p3) {
                OW::RecordViewMatrixUnresolved("view projection p3", p3, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            const uint64_t p4 = SDK->RPM<uint64_t>(p3 + activeOffsets.VM_ViewProjectionPtr);
            if (!p4) {
                OW::RecordViewMatrixUnresolved("view projection p4", p4, lastViewMatrixLogTick);
                Sleep(5);
                continue;
            }

            viewMatrixPtr = p4 + activeOffsets.VM_ViewProjectionMatrix;
            viewMatrix_xor_ptr = p2 + activeOffsets.VM_ViewMatrix;
            const uint64_t projectionMatrixPtr = p2 + activeOffsets.VM_ProjMatrix;

            OW::Matrix renderViewProjection = SDK->RPM<OW::Matrix>(viewMatrixPtr);
            OW::Matrix cameraViewMatrix = SDK->RPM<OW::Matrix>(viewMatrix_xor_ptr);
            OW::Matrix projectionMatrix = SDK->RPM<OW::Matrix>(projectionMatrixPtr);

            bool renderViewProjectionValid = OW::IsMatrixNonIdentity(renderViewProjection);
            const bool cameraViewValid = OW::IsCameraViewMatrixPlausible(cameraViewMatrix);
            const bool projectionValid = OW::IsMatrixNonIdentity(projectionMatrix);
            if (!renderViewProjectionValid &&
                (directViewMatrixRoot || OW::offset::ActiveProfile() == OW::offset::RuntimeProfile::WorldBz) &&
                cameraViewValid &&
                projectionValid) {
                renderViewProjection = OW::ComposeCameraProjection(cameraViewMatrix, projectionMatrix);
                renderViewProjectionValid = OW::IsMatrixNonIdentity(renderViewProjection);
                if (renderViewProjectionValid)
                    viewMatrixPtr = projectionMatrixPtr;
            }
            OW::Matrix publishedCameraView{};
            OW::g_viewMatrixMutex.lock();
            OW::viewMatrix = renderViewProjection;
            if (cameraViewValid) {
                OW::viewMatrix_xor = cameraViewMatrix;
            } else if (renderViewProjectionValid) {
                if (!OW::IsMatrixNonIdentity(OW::viewMatrix_xor)) {
                    OW::viewMatrix_xor = cameraViewMatrix;
                }
            } else {
                OW::viewMatrix_xor = cameraViewMatrix;
            }
            publishedCameraView = OW::viewMatrix_xor;
            OW::g_viewMatrixMutex.unlock();

            const bool viewMatrixValid =
                renderViewProjectionValid &&
                OW::IsCameraViewMatrixPlausible(publishedCameraView);
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
    Diagnostics::ScopedDmaCallsite::Pop();
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

    inline bool IsReasonableRenderVelocity(const Vector3& velocity, float* speedOut = nullptr) {
        if (!IsFiniteVector(velocity)) {
            if (speedOut) *speedOut = 0.0f;
            return false;
        }

        const float speed = velocity.Size();
        if (speedOut) *speedOut = speed;
        return std::isfinite(speed) && speed >= 0.75f && speed <= 80.0f;
    }

    inline void OffsetRenderPoint(Vector3& point, const Vector3& offset) {
        if (IsFiniteVector(point) && IsNonZeroVector(point))
            point += offset;
    }

    inline void ApplyTrainingBotRenderPrediction(OW::c_entity& entity, DWORD now) {
        if (!OW::GameData::IsTrainingBotHeroId(entity.HeroID))
            return;
        if (entity.position_sample_tick_ms == 0 || now <= entity.position_sample_tick_ms)
            return;

        float speed = 0.0f;
        if (!IsReasonableRenderVelocity(entity.velocity, &speed))
            return;

        const DWORD ageMs = now - entity.position_sample_tick_ms;
        if (ageMs < 20)
            return;

        constexpr float kMaxTrainingBotRenderLeadSeconds = 0.22f;
        const float leadSeconds = (std::min)(
            static_cast<float>(ageMs) / 1000.0f,
            kMaxTrainingBotRenderLeadSeconds);
        const Vector3 offset = entity.velocity * leadSeconds;

        OffsetRenderPoint(entity.pos, offset);
        OffsetRenderPoint(entity.head_pos, offset);
        OffsetRenderPoint(entity.neck_pos, offset);
        OffsetRenderPoint(entity.chest_pos, offset);
        if (entity.cached_bot_chest_bone_valid)
            OffsetRenderPoint(entity.cached_bot_chest_bone, offset);

        for (size_t i = 0; i < entity.skeleton_bones.size(); ++i) {
            if (entity.skeleton_bone_valid[i])
                OffsetRenderPoint(entity.skeleton_bones[i], offset);
        }
    }

    inline OW::c_entity InterpolateEntityForRender(const OW::c_entity& source, DWORD now) {
        const float alpha = EntityInterpolationAlpha(source, now);
        OW::c_entity entity = source;
        if (alpha >= 0.999f) {
            ApplyTrainingBotRenderPrediction(entity, now);
            return entity;
        }

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

        ApplyTrainingBotRenderPrediction(entity, now);
        return entity;
    }

    inline int ToByte(float value) {
        return static_cast<int>(Clamp01(value) * 255.0f + 0.5f);
    }

    inline Render::Color ToRenderColor(const ImVec4& color) {
        return Render::Color(ToByte(color.x), ToByte(color.y), ToByte(color.z), ToByte(color.w));
    }

    inline float VisibilityAlpha(const OW::c_entity& entity, float opacity) {
        (void)entity;
        return Clamp01(opacity);
    }

    inline ImVec4 ApplyVisualState(ImVec4 color, const OW::c_entity& entity, float opacity) {
        const float brightness = entity.Vis ? 1.18f : 0.90f;
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

    inline bool TryProjectPoint(const Vector3& world, Vector2& screen, const OW::Matrix* vm = nullptr) {
        if (!IsFiniteVector(world) || !IsNonZeroVector(world))
            return false;
        const OW::Matrix& proj = vm ? *vm : OW::viewMatrix;
        if (!proj.WorldToScreen(world, &screen, Vector2(OW::WX, OW::WY)))
            return false;
        return IsReasonableProjectedPoint(screen);
    }

    inline bool TryBuildHeadOffsetBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds, const OW::Matrix* vm = nullptr) {
        Vector2 low{}, high{};
        const Vector3 head = entity.head_pos;
        if (!TryProjectPoint(Vector3(head.X, head.Y - 1.5f, head.Z), low, vm))
            return false;
        if (!TryProjectPoint(Vector3(head.X, head.Y + 1.0f, head.Z), high, vm))
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

    inline bool TryBuildProjectedBounds(const OW::c_entity& entity, ProjectedEntityBounds& bounds, const OW::Matrix* vm = nullptr) {
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
            if (TryProjectPoint(world, screen, vm))
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
            return TryBuildHeadOffsetBounds(entity, bounds, vm);

        float height = maxY - minY;
        if (height <= 8.0f || !std::isfinite(height))
            return TryBuildHeadOffsetBounds(entity, bounds, vm);

        Vector2 headScreen{};
        Vector2 posScreen{};
        const bool hasHead = TryProjectPoint(entity.head_pos, headScreen, vm);
        const bool hasPos = TryProjectPoint(entity.pos, posScreen, vm);
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

    inline uint64_t EntitySelectionKey(const OW::c_entity& entity) {
        if (entity.address)
            return entity.address;
        if (entity.LinkBase)
            return entity.LinkBase;
        return entity.roster_key;
    }

    inline bool IsSelectedEnemyTarget(const OW::c_entity& entity,
                                      size_t index,
                                      uint64_t selectedTargetKey,
                                      int selectedTargetIndex) {
        if (!entity.Team)
            return false;

        const uint64_t entityKey = EntitySelectionKey(entity);
        if (selectedTargetKey != 0 && entityKey != 0)
            return entityKey == selectedTargetKey;

        return selectedTargetIndex >= 0 &&
               index == static_cast<size_t>(selectedTargetIndex);
    }

    inline ImVec4 EntityBaseColor(const OW::c_entity& entity,
                                  size_t index,
                                  uint64_t selectedTargetKey,
                                  int selectedTargetIndex) {
        if (IsSelectedEnemyTarget(entity, index, selectedTargetKey, selectedTargetIndex)) {
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
        if (OW::Config::visualMaxDist <= 0.0f)
            return true;
        return distance <= OW::Config::visualMaxDist;
    }

    inline float DistanceOpacity(float distance) {
        if (OW::Config::visualMaxDist <= 0.0f)
            return 1.0f;
        return Clamp01(1.0f - Clamp01(distance / OW::Config::visualMaxDist));
    }

    inline ImU32 EntityColor(const OW::c_entity& entity,
                             size_t index,
                             uint64_t selectedTargetKey,
                             int selectedTargetIndex,
                             float opacity = 1.0f) {
        return ToImU32(ApplyVisualState(
            EntityBaseColor(entity, index, selectedTargetKey, selectedTargetIndex),
            entity,
            opacity));
    }

    inline ImU32 EntityBoxColor(const OW::c_entity& entity,
                                size_t index,
                                uint64_t selectedTargetKey,
                                int selectedTargetIndex,
                                float opacity) {
        if (IsSelectedEnemyTarget(entity, index, selectedTargetKey, selectedTargetIndex))
            return EntityColor(entity, index, selectedTargetKey, selectedTargetIndex, opacity);
        if (OW::Config::drawhealth)
            return ToImU32(ApplyVisualState(HealthGradientColor(entity), entity, opacity));
        return EntityColor(entity, index, selectedTargetKey, selectedTargetIndex, opacity);
    }

    inline ImU32 BoxOutlineColor(float opacity) {
        ImVec4 color = OW::Config::EnemyCol;
        color.w = Clamp01(color.w * opacity);
        return ToImU32(color);
    }

    inline Render::Color EntityRenderColor(const OW::c_entity& entity,
                                           size_t index,
                                           uint64_t selectedTargetKey,
                                           int selectedTargetIndex,
                                           float opacity = 1.0f) {
        return ToRenderColor(ApplyVisualState(
            EntityBaseColor(entity, index, selectedTargetKey, selectedTargetIndex),
            entity,
            opacity));
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
        if (compactKey != heroName) {
            if (ID3D11ShaderResourceView* texture = icons->GetIcon(compactKey))
                return texture;
        }

        const std::string slugKey = OW::HeroDisplayNameToSlug(heroName);
        if (!slugKey.empty() && slugKey != heroName && slugKey != compactKey)
            return icons->GetIcon(slugKey);

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

    inline bool IsSkillReadyCooldownSentinel(float cooldown) {
        return std::isfinite(cooldown) && std::fabs(cooldown - 1.0f) <= 0.001f;
    }

    inline bool IsSkillOnCooldown(bool active, float cooldown) {
        return !active && std::isfinite(cooldown) && cooldown > 0.05f &&
               !IsSkillReadyCooldownSentinel(cooldown);
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

    inline void DrawSkillTextCooldowns(const OW::c_entity& entity, float x, float y, float opacity) {
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

    inline std::string FormatIconCooldown(float cooldown) {
        char buffer[16] = {};
        if (cooldown >= 10.0f)
            std::snprintf(buffer, sizeof(buffer), "%.0f", cooldown);
        else
            std::snprintf(buffer, sizeof(buffer), "%.1f", cooldown);
        return buffer;
    }

    inline std::string FormatUltimatePercent(float ultimate) {
        if (!std::isfinite(ultimate))
            return "--%";

        char buffer[16] = {};
        std::snprintf(buffer, sizeof(buffer), "%d%%",
                      static_cast<int>(std::clamp(ultimate, 0.0f, 100.0f) + 0.5f));
        return buffer;
    }

    inline void AdvanceIconSlot(float& cursorX, float iconSize, float gap) {
        cursorX += iconSize + gap;
    }

    inline void DrawFallbackIconText(float slotX, float y, float iconSize, float opacity,
                                     const std::string& text, ImU32 color, float fontSize) {
        const float alpha = std::clamp(opacity, 0.0f, 1.0f);
        Render::DrawFilledRect(Vector2(slotX, y), iconSize, iconSize,
                               ImColorWithAlpha(0, 0, 0, alpha * 0.28f));
        Render::DrawRect(Vector2(slotX, y), iconSize, iconSize,
                         Render::Color(255, 255, 255, ToByte(alpha * 0.24f)), 1.0f);
        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f) {
            const float textScale = fontSize / baseFontSize;
            textSize.x *= textScale;
            textSize.y *= textScale;
        }
        Render::DrawText(ImVec2(slotX + (iconSize - textSize.x) * 0.5f,
                                y + (iconSize - textSize.y) * 0.5f),
                         color, text.c_str(), fontSize);
    }

    inline void DrawSkillFallbackSlot(float slotX, float y, float iconSize, float opacity,
                                      const char* label, bool active, float cooldown) {
        const bool onCooldown = IsSkillOnCooldown(active, cooldown);
        const std::string text = onCooldown ? FormatIconCooldown(cooldown) : std::string(label);
        const ImU32 color = onCooldown
            ? ImU32WithAlpha(255, 230, 120, opacity)
            : ImU32WithAlpha(235, 235, 235, opacity);
        DrawFallbackIconText(slotX, y, iconSize, opacity, text, color, 9.0f);
    }

    inline void DrawUltimateFallbackSlot(float slotX, float y, float iconSize,
                                         float opacity, float ultimate) {
        DrawFallbackIconText(slotX, y, iconSize, opacity, FormatUltimatePercent(ultimate),
                             ImU32WithAlpha(255, 225, 60, opacity), 8.0f);
    }

    inline void DrawIconSlotText(float slotX, float y, float iconSize, const std::string& text,
                                 ImU32 color, float fontSize) {
        if (text.empty())
            return;

        ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f) {
            const float textScale = fontSize / baseFontSize;
            textSize.x *= textScale;
            textSize.y *= textScale;
        }

        Render::DrawText(ImVec2(slotX + (iconSize - textSize.x) * 0.5f,
                                y + (iconSize - textSize.y) * 0.5f),
                         color, text.c_str(), fontSize);
    }

    inline void EnsureAbilityIconsLoaded(IconManager* iconManager, const OW::HeroAbilityIcons& icons) {
        static std::unordered_map<std::string, bool> attemptedLoads;
        if (!iconManager || !attemptedLoads.emplace(icons.heroSlug, true).second)
            return;

        iconManager->LoadAbilityIcons(icons.heroSlug, {
            icons.ability1Icon,
            icons.ability2Icon,
            icons.ultimateIcon
        });
    }

    inline bool DrawAbilityIconSlot(IconManager* iconManager, const OW::HeroAbilityIcons& icons,
                                    const char* abilityIcon, float& cursorX, float y,
                                    float iconSize, float gap, float opacity,
                                    bool onCooldown, float cooldown) {
        if (!iconManager || !abilityIcon || abilityIcon[0] == '\0') {
            AdvanceIconSlot(cursorX, iconSize, gap);
            return false;
        }

        const std::string key = std::string(icons.heroSlug) + "/" + abilityIcon;
        ID3D11ShaderResourceView* texture = iconManager->GetIcon(key);
        if (!texture) {
            AdvanceIconSlot(cursorX, iconSize, gap);
            return false;
        }

        const float alpha = std::clamp(opacity, 0.0f, 1.0f);
        const float imagePadding = std::clamp(iconSize * 0.09f, 1.0f, 2.0f);
        const float imageSize = (std::max)(1.0f, iconSize - imagePadding * 2.0f);
        Render::DrawIcon(texture,
                         ImVec2(cursorX + imagePadding, y + imagePadding),
                         ImVec2(imageSize, imageSize),
                         ImU32WithAlpha(255, 255, 255, alpha));

        if (onCooldown) {
            Render::DrawFilledRect(Vector2(cursorX, y), iconSize, iconSize,
                                   ImColorWithAlpha(0, 0, 0, alpha * 0.58f));
            const std::string text = FormatIconCooldown(cooldown);
            DrawIconSlotText(cursorX, y, iconSize, text,
                             ImU32WithAlpha(255, 230, 120, alpha), 9.0f);
        }

        AdvanceIconSlot(cursorX, iconSize, gap);
        return true;
    }

    inline bool DrawUltimateIconSlot(IconManager* iconManager, const OW::HeroAbilityIcons& icons,
                                     float& cursorX, float y, float iconSize, float gap,
                                     float opacity, float ultimate) {
        bool drew = DrawAbilityIconSlot(iconManager, icons, icons.ultimateIcon, cursorX, y,
                                        iconSize, gap, opacity, false, 0.0f);
        if (!drew)
            return false;

        if (std::isfinite(ultimate) && ultimate < 100.0f) {
            const float iconX = cursorX - iconSize - gap;
            const float alpha = std::clamp(opacity, 0.0f, 1.0f);
            Render::DrawFilledRect(Vector2(iconX, y), iconSize, iconSize,
                                   ImColorWithAlpha(0, 0, 0, alpha * 0.42f));

            const std::string text = FormatUltimatePercent(ultimate);
            DrawIconSlotText(iconX, y, iconSize, text,
                             ImU32WithAlpha(255, 225, 60, alpha), 8.0f);
        }
        return true;
    }

    inline void DrawSkillCooldowns(const OW::c_entity& entity, const std::string& heroName,
                                   float x, float y, float opacity) {
        const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroName);
        IconManager* iconManager = Render::GetIconManager();
        if (!icons || !iconManager) {
            DrawSkillTextCooldowns(entity, x, y, opacity);
            return;
        }

        EnsureAbilityIconsLoaded(iconManager, *icons);

        constexpr float iconSize = 18.0f;
        constexpr float gap = 3.0f;
        float cursorX = x;
        const bool includeUltimateIcon = OW::Config::ult && OW::Config::ultimateDisplayMode == 0;
        const bool drewAny =
            DrawAbilityIconSlot(iconManager, *icons, icons->ability1Icon, cursorX, y,
                                iconSize, gap, opacity,
                                IsSkillOnCooldown(entity.skill1act, entity.skillcd1), entity.skillcd1) |
            DrawAbilityIconSlot(iconManager, *icons, icons->ability2Icon, cursorX, y,
                                iconSize, gap, opacity,
                                IsSkillOnCooldown(entity.skill2act, entity.skillcd2), entity.skillcd2) |
            (includeUltimateIcon && DrawUltimateIconSlot(iconManager, *icons, cursorX, y,
                                                         iconSize, gap, opacity, entity.ultimate));

        if (!drewAny)
            DrawSkillTextCooldowns(entity, x, y, opacity);
    }

    inline void DrawBoneSegment(const Vector2& from, const Vector2& to, const Render::Color& color, float thickness) {
        if (IsValidScreenPoint(from) && IsValidScreenPoint(to))
            Render::DrawLine(from, to, color, thickness);
    }

    inline void DrawSkeleton(const OW::c_entity& entity, const OW::Matrix& view, const Render::Color& color, float thickness) {
        Vector2 points[18]{};
        bool projected[18]{};
        const Vector2 windowSize(OW::WX, OW::WY);
        constexpr int kRenderSkeletonSlots =
            static_cast<int>(OW::Plexies20260609::kSkeletonSlotCount);

        for (int i = 0; i < kRenderSkeletonSlots; ++i) {
            if (!entity.skeleton_bone_valid[i])
                continue;
            projected[i] = view.WorldToScreen(entity.skeleton_bones[i], &points[i], windowSize);
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
            {3, 8}, {8, 10}, {10, 14},
            {3, 9}, {9, 11}, {11, 15},
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

    struct AimTriggerStatusRow {
        std::string text;
        ImU32 color = IM_COL32_WHITE;
        float fontSize = 11.5f;
    };

    inline const char* YesNo(bool value) {
        return value ? "Y" : "N";
    }

    inline uint64_t CurrentLocalHeroId(const OW::c_entity& local) {
        if (local.HeroID != 0)
            return local.HeroID;
        if (OW::Config::lastheroid > 0)
            return static_cast<uint64_t>(OW::Config::lastheroid);
        return 0;
    }

    inline std::string CurrentLocalHeroName(uint64_t heroId, const OW::c_entity& local) {
        if (heroId == 0)
            return "Unknown";

        std::string heroName = OW::GetHeroEngNames(heroId, local.LinkBase);
        if (heroName.empty() || heroName == "Unknown") {
            char fallback[64]{};
            std::snprintf(fallback, sizeof(fallback), "Hero 0x%llX",
                          static_cast<unsigned long long>(heroId));
            return fallback;
        }
        return heroName;
    }

    inline std::vector<AimTriggerStatusRow> BuildAimTriggerStatusRows() {
        std::vector<AimTriggerStatusRow> rows;
        const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
        const uint64_t heroId = CurrentLocalHeroId(local);
        if (heroId == 0 && !OW::Config::secondaim && !OW::Config::triggerbot2)
            return rows;

        rows.push_back({
            "Hero: " + CurrentLocalHeroName(heroId, local),
            ImU32WithAlpha(255, 255, 255, 0.96f),
            13.5f
        });

        const OW::HeroPerkRuntime::Snapshot perkSnapshot = OW::HeroPerkRuntime::CurrentSnapshot();
        if (perkSnapshot.supported && perkSnapshot.heroId == heroId) {
            std::string perkLine = "Perk: ";
            perkLine += perkSnapshot.effectivePerkOn ? "On" : "Off";
            perkLine += " | ";
            perkLine += OW::HeroPerkRuntime::EffectiveSourceName(perkSnapshot.source);
            if (perkSnapshot.variantId && perkSnapshot.variantId[0] != '\0') {
                perkLine += " | ";
                perkLine += perkSnapshot.variantId;
            }

            rows.push_back({
                perkLine,
                perkSnapshot.effectivePerkOn
                    ? ImU32WithAlpha(166, 255, 199, 0.96f)
                    : ImU32WithAlpha(226, 232, 240, 0.72f),
                11.5f
            });
        }

        std::vector<std::pair<int, OW::Config::HeroSlotPreset>> aimSlots;
        std::vector<std::pair<int, OW::Config::HeroSlotPreset>> triggerSlots;
        aimSlots.reserve(OW::Config::kMaxHeroPresetSlots);
        triggerSlots.reserve(OW::Config::kMaxHeroPresetSlots);

        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot))
                aimSlots.emplace_back(slotIndex, slot);
            if (OW::Config::TryGetHeroTriggerSlot(heroId, slotIndex, slot))
                triggerSlots.emplace_back(slotIndex, slot);
        }

        auto firstEnabledSlot = [](const std::vector<std::pair<int, OW::Config::HeroSlotPreset>>& slots) {
            for (const auto& item : slots) {
                if (item.second.enabled)
                    return item.first;
            }
            return -1;
        };

        const int activeAimSlot = firstEnabledSlot(aimSlots);
        const int runtimeTriggerSlot = firstEnabledSlot(triggerSlots);

        if (!aimSlots.empty()) {
            rows.push_back({ "Aim active slots", ImU32WithAlpha(126, 220, 255, 0.94f), 11.5f });
            for (const auto& item : aimSlots) {
                const int slotIndex = item.first;
                const OW::Config::HeroSlotPreset& slot = item.second;
                const bool active = slot.enabled && slotIndex == activeAimSlot;
                char line[240]{};
                std::snprintf(line, sizeof(line),
                              "#%d %s | %s | FOV %.0f deg | Hold | enabled:%s%s",
                              slotIndex + 1,
                              OW::Labels::AimModeName(slot.preset.aimMode),
                              OW::Labels::AimActivationKeyName(slot.preset.key),
                              slot.preset.fov,
                              YesNo(slot.enabled),
                              active ? " | active" : "");
                rows.push_back({
                    line,
                    active
                        ? ImU32WithAlpha(162, 255, 188, 0.96f)
                        : ImU32WithAlpha(226, 232, 240, slot.enabled ? 0.90f : 0.58f),
                    11.2f
                });
            }
        }

        if (!triggerSlots.empty()) {
            rows.push_back({ "Trigger active slots", ImU32WithAlpha(255, 204, 118, 0.95f), 11.5f });
            for (const auto& item : triggerSlots) {
                const int slotIndex = item.first;
                const OW::Config::HeroSlotPreset& slot = item.second;
                const OW::Config::TriggerPreset& trigger = slot.preset.trigger;
                const bool active = slot.enabled && trigger.enabled && slotIndex == runtimeTriggerSlot;
                char line[280]{};
                std::snprintf(line, sizeof(line),
                              "#%d %s | %s | %s | charge:%s | invis:%s | slot:%s trig:%s%s",
                              slotIndex + 1,
                              OW::AttackActionCompactNameForHero(heroId, trigger.action),
                              OW::Labels::TriggerbotModeName(trigger.mode),
                              OW::Labels::AimActivationKeyName(trigger.key),
                              YesNo(trigger.chargeAware),
                              YesNo(trigger.ignoreInvisible),
                              YesNo(slot.enabled),
                              YesNo(trigger.enabled),
                              active ? " | active" : "");
                rows.push_back({
                    line,
                    active
                        ? ImU32WithAlpha(255, 232, 161, 0.97f)
                        : ImU32WithAlpha(226, 232, 240, (slot.enabled && trigger.enabled) ? 0.90f : 0.58f),
                    10.8f
                });
            }
        }

        if (OW::Config::secondaim || OW::Config::triggerbot2) {
            rows.push_back({ "Secondary", ImU32WithAlpha(190, 168, 255, 0.95f), 11.5f });
            if (OW::Config::secondaim) {
                const int secondaryAimMode = OW::Config::Flick2 ? 1 : 0;
                char line[192]{};
                std::snprintf(line, sizeof(line),
                              "Secondary Aim | %s | %s | Hold | enabled:Y",
                              OW::Labels::AimModeName(secondaryAimMode),
                              OW::Labels::AimActivationKeyName(OW::Config::aim_key2));
                rows.push_back({ line, ImU32WithAlpha(204, 194, 255, 0.96f), 11.0f });
            }
            if (OW::Config::triggerbot2) {
                char line[224]{};
                std::snprintf(line, sizeof(line),
                              "Triggerbot2 | Primary | %s | %s | charge:%s | invis:%s | enabled:Y",
                              OW::Labels::TriggerbotModeName(OW::Config::triggerbotMode2),
                              OW::Labels::AimActivationKeyName(OW::Config::triggerbotKey2),
                              YesNo(OW::Config::triggerbotChargeAware2),
                              YesNo(OW::Config::triggerbotIgnoreInvisible2));
                rows.push_back({ line, ImU32WithAlpha(204, 194, 255, 0.96f), 10.8f });
            }
        }

        if (rows.size() == 1)
            rows.push_back({ "No hero Aim/Trigger slots", ImU32WithAlpha(176, 184, 196, 0.78f), 11.0f });

        return rows;
    }

    inline float ScaledTextWidth(const std::string& text, float fontSize) {
        ImVec2 size = ImGui::CalcTextSize(text.c_str());
        const float baseFontSize = ImGui::GetFontSize();
        if (baseFontSize > 0.0f)
            size.x *= fontSize / baseFontSize;
        return size.x;
    }

    inline float AimTriggerStatusPanelWidth(const std::vector<AimTriggerStatusRow>& rows) {
        float width = 0.0f;
        for (const AimTriggerStatusRow& row : rows)
            width = (std::max)(width, ScaledTextWidth(row.text, row.fontSize));
        return std::clamp(width + 18.0f, 260.0f, 360.0f);
    }

    inline float AimTriggerStatusPanelHeight(const std::vector<AimTriggerStatusRow>& rows) {
        if (rows.empty())
            return 0.0f;

        constexpr float paddingY = 7.0f;
        constexpr float lineGap = 2.0f;
        float height = paddingY * 2.0f;
        for (const AimTriggerStatusRow& row : rows)
            height += row.fontSize + lineGap;
        return height - lineGap;
    }

    inline float AimTriggerStatusPanelHeight() {
        return AimTriggerStatusPanelHeight(BuildAimTriggerStatusRows());
    }

    inline float AimTriggerStatusPanelReservedBottomY() {
        const float height = AimTriggerStatusPanelHeight();
        return height > 0.0f ? 10.0f + height + 8.0f : 10.0f;
    }

    inline float DrawAimTriggerStatusPanel() {
        const std::vector<AimTriggerStatusRow> rows = BuildAimTriggerStatusRows();
        if (rows.empty())
            return 0.0f;

        constexpr float panelX = 10.0f;
        constexpr float panelY = 10.0f;
        constexpr float paddingX = 9.0f;
        constexpr float paddingY = 7.0f;
        constexpr float lineGap = 2.0f;
        const float panelWidth = AimTriggerStatusPanelWidth(rows);
        const float panelHeight = AimTriggerStatusPanelHeight(rows);

        Render::DrawFilledRect(Vector2(panelX, panelY), panelWidth, panelHeight,
                               ImColorWithAlpha(6, 9, 13, 0.74f));
        Render::DrawFilledRect(Vector2(panelX, panelY), 3.0f, panelHeight,
                               ImColorWithAlpha(80, 190, 255, 0.88f));
        Render::DrawRect(Vector2(panelX, panelY), panelWidth, panelHeight,
                         Render::Color(255, 255, 255, ToByte(0.14f)), 1.0f);

        float textY = panelY + paddingY;
        for (const AimTriggerStatusRow& row : rows) {
            Render::DrawText(ImVec2(panelX + paddingX, textY), row.color,
                             row.text.c_str(), row.fontSize);
            textY += row.fontSize + lineGap;
        }

        return panelY + panelHeight;
    }

} // namespace OverlayRenderDetail

inline float DrawAimTriggerStatusPanel() {
    return OverlayRenderDetail::DrawAimTriggerStatusPanel();
}

inline void PlayerInfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    auto local_snapshot = OW::TargetingDetail::SnapshotLocalEntity();

    Diagnostics::PlayerInfoStats renderStats{};
    renderStats.input = entity_snapshot.size();
    static DWORD lastPlayerInfoLogTick = 0;
    static std::unordered_map<uint64_t, OverlayRenderDetail::ProjectedBoundsState> projectedBoundsStates;
    const DWORD renderTick = GetTickCount();
    const OW::TargetingDetail::TargetLockRuntime targetLock =
        OW::TargetingDetail::SnapshotTargetLockRuntime();
    const uint64_t selectedTargetKey = targetLock.active ? targetLock.entityKey : 0;
    const int selectedTargetIndex = OW::Config::Targetenemyi;

    OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
    OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);

    auto publishStats = [&]() {
        Diagnostics::SetPlayerInfoStats(renderStats);
        if (!OW::PipelineDebugEnabled())
            return;

        const DWORD now = GetTickCount();
        if (lastPlayerInfoLogTick == 0 || now - lastPlayerInfoLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 PlayerInfo hero=0x%llX target[index/key]=%d/0x%llX colorA[enemy/hidden/target]=%.2f/%.2f/%.2f input=%zu projected=%zu drawn=%zu skip[dead/localhp/self/dist/opacity/w2s/box/window]=%zu/%zu/%zu/%zu/%zu/%zu/%zu/%zu w2s[low/high]=%zu/%zu.",
                static_cast<unsigned long long>(local_snapshot.HeroID),
                selectedTargetIndex,
                static_cast<unsigned long long>(selectedTargetKey),
                OW::Config::enargb.w,
                OW::Config::invisnenargb.w,
                OW::Config::targetargb.w,
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

    auto captureProjectedSample = [&](const OW::c_entity& entity, float left, float top,
        float width, float height, float centerX, float bottom, float distance) {
        if (renderStats.sampleProjected)
            return;
        renderStats.sampleProjected = true;
        renderStats.sampleProjectedAddress = entity.address;
        renderStats.sampleProjectedHeroId = entity.HeroID;
        renderStats.sampleProjectedLeft = static_cast<int>(std::lround(left));
        renderStats.sampleProjectedTop = static_cast<int>(std::lround(top));
        renderStats.sampleProjectedWidth = static_cast<int>(std::lround(width));
        renderStats.sampleProjectedHeight = static_cast<int>(std::lround(height));
        renderStats.sampleProjectedCenterX = static_cast<int>(std::lround(centerX));
        renderStats.sampleProjectedBottom = static_cast<int>(std::lround(bottom));
        renderStats.sampleProjectedDistanceM = static_cast<int>(std::lround(distance));
    };

    auto captureDrawnSample = [&](const OW::c_entity& entity, float left, float top,
        float width, float height, float centerX, float bottom, float distance) {
        if (renderStats.sampleDrawn)
            return;
        renderStats.sampleDrawn = true;
        renderStats.sampleDrawnAddress = entity.address;
        renderStats.sampleDrawnHeroId = entity.HeroID;
        renderStats.sampleDrawnLeft = static_cast<int>(std::lround(left));
        renderStats.sampleDrawnTop = static_cast<int>(std::lround(top));
        renderStats.sampleDrawnWidth = static_cast<int>(std::lround(width));
        renderStats.sampleDrawnHeight = static_cast<int>(std::lround(height));
        renderStats.sampleDrawnCenterX = static_cast<int>(std::lround(centerX));
        renderStats.sampleDrawnBottom = static_cast<int>(std::lround(bottom));
        renderStats.sampleDrawnDistanceM = static_cast<int>(std::lround(distance));
    };

    for (size_t index = 0; index < entity_snapshot.size(); ++index) {
        OW::c_entity entity = OverlayRenderDetail::InterpolateEntityForRender(entity_snapshot[index], renderTick);
        if (entity.roster_state != OW::EntityRosterState::Fresh || !entity.Alive) {
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
        const auto camLoc = renderViewMatrixXor.get_location();
        float dist = Vector3(camLoc.x, camLoc.y, camLoc.z).DistTo(Vec3);
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
        if (!OverlayRenderDetail::TryBuildProjectedBounds(entity, bounds, &renderViewMatrix)) {
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
        captureProjectedSample(entity, left, top, width, height, centerX, bottom, dist);

        ImU32 color = OverlayRenderDetail::EntityColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
        ImU32 boxColor = OverlayRenderDetail::EntityBoxColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
        ImU32 boxOutlineColor = OverlayRenderDetail::BoxOutlineColor(distanceOpacity);
        Render::Color lineColor = OverlayRenderDetail::EntityRenderColor(
            entity, index, selectedTargetKey, selectedTargetIndex, distanceOpacity);
        const float visualOpacity = OverlayRenderDetail::VisibilityAlpha(entity, distanceOpacity);
        const float outlineThickness = entity.Vis ? 1.8f : 1.2f;
        const float skeletonThickness = entity.Vis ? 1.5f : 1.0f;
        const bool specialEntity = OverlayRenderDetail::IsSpecialEntity(entity);
        bool drewAny = false;

        std::string heroName;
        const bool showAboveHeadUltimate = OW::Config::ult && OW::Config::ultimateDisplayMode == 0;
        const bool showAboveHeadSkillCooldowns = OW::Config::skillinfo && OW::Config::skillDisplayMode == 0;
        if (OW::Config::skillinfo || OW::Config::healthbar2 || showAboveHeadUltimate || OW::Config::name)
            heroName = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);

        ID3D11ShaderResourceView* heroIcon = nullptr;
        if ((OW::Config::healthbar2 || showAboveHeadUltimate) && !specialEntity && heroName != "Unknown")
            heroIcon = OverlayRenderDetail::FindHeroIcon(heroName);

        constexpr float iconSize = 24.0f;
        Vector2 ultimateIndicatorCenter(centerX, top - 18.0f);
        if (heroIcon) {
            const float iconY = top - iconSize - 10.0f;
            ultimateIndicatorCenter = Vector2(centerX, iconY + iconSize * 0.5f);
        } else if (showAboveHeadUltimate && entity.ultimate >= 100.0f) {
            ultimateIndicatorCenter = Vector2(centerX, top - 28.0f);
        }

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            Render::DrawCorneredBox(left, top, width, bottom - top, boxColor, outlineThickness, boxOutlineColor);
            drewAny = true;
        }

        float labelY = heroIcon
            ? (top - iconSize - 24.0f)
            : (showAboveHeadUltimate ? top - 45.0f : top - 18.0f);
        if (OW::Config::name && !specialEntity && heroName != "Unknown" && heroName != "Bot" && !heroName.empty()) {
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, labelY), color, heroName, 13.0f);
            labelY += 13.0f;
            drewAny = true;
        }
        if (OW::Config::drawbattletag && !entity.battletag.empty()) {
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, labelY), color, entity.battletag, 12.0f);
            drewAny = true;
        }

        if (OW::Config::healthbar) {
            Render::DrawHealthBar(Vector2(left - 7.0f, top), bottom - top,
                                  entity.PlayerHealth, entity.PlayerHealthMax, visualOpacity);
            OverlayRenderDetail::DrawStackedResourceBar(entity, left - 13.0f, top, bottom - top, visualOpacity);
            drewAny = true;
        }

        if (OW::Config::healthbar2) {
            int shield = static_cast<int>(entity.MinArmorHealth + entity.MinBarrierHealth);
            int maxShield = static_cast<int>(entity.MaxArmorHealth + entity.MaxBarrierHealth);
            const float compactHealthTop = heroIcon
                ? (ultimateIndicatorCenter.Y + iconSize * 0.5f + 3.0f)
                : (top - 9.0f);
            Render::DrawSeerLikeHealth(centerX, compactHealthTop, shield, maxShield,
                                        static_cast<int>(entity.MinHealth),
                                        static_cast<int>(entity.MaxHealth));
            drewAny = true;
        }

        if (OW::Config::drawline) {
            Render::DrawLine(Vector2(OW::WX * 0.5f, OW::WY), Vector2(centerX, bottom), lineColor, 1.0f);
            drewAny = true;
        }

        if (OW::Config::draw_skel && !specialEntity) {
            OverlayRenderDetail::DrawSkeleton(entity, renderViewMatrix, lineColor, skeletonThickness);
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
            if (renderViewMatrix.WorldToScreen(Vec3, &eyeStart, Vector2(OW::WX, OW::WY)) &&
                renderViewMatrix.WorldToScreen(rayEnd, &eyeEnd, Vector2(OW::WX, OW::WY))) {
                Render::DrawLine(eyeStart, eyeEnd, lineColor, 1.0f);
                drewAny = true;
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, bottom + 4.0f), color, distanceText, 14.0f);
            drewAny = true;
        }

        if (showAboveHeadUltimate && !specialEntity) {
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

        if (showAboveHeadSkillCooldowns) {
            OverlayRenderDetail::DrawSkillCooldowns(entity, heroName, left + width + 8.0f, top + 20.0f, visualOpacity);
            drewAny = true;
        }

        if (drewAny) {
            captureDrawnSample(entity, left, top, width, height, centerX, bottom, dist);
            renderStats.drawn++;
        }
    }
    OverlayRenderDetail::PruneProjectedBoundsStates(projectedBoundsStates, renderTick);
    publishStats();
}

inline void skillinfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    const bool showUltimateLeft = OW::Config::ult && OW::Config::ultimateDisplayMode == 1;
    const bool showUltimateRight = OW::Config::ult && OW::Config::ultimateDisplayMode == 2;
    const bool showSkillLeft = OW::Config::skillinfo && OW::Config::skillDisplayMode == 1;
    const bool showSkillRight = OW::Config::skillinfo && OW::Config::skillDisplayMode == 2;
    if ((!showUltimateLeft && !showUltimateRight && !showSkillLeft && !showSkillRight) || entity_snapshot.empty())
        return;

    IconManager* iconManager = Render::GetIconManager();

    std::vector<OW::c_entity> enemies;
    std::vector<OW::c_entity> allies;
    for (const OW::c_entity& entity : entity_snapshot) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (heroname == "Bot" || heroname == "Unknown")
            continue;
        if (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)
            continue;

        if (entity.Team)
            enemies.push_back(entity);
        else
            allies.push_back(entity);
    }

    if (enemies.empty() && allies.empty())
        return;

    auto renderEntitySkillRow = [&](const OW::c_entity& entity, float x, float y, float opacity,
                                    bool includeUltimate, bool includeCooldowns) -> float {
        const std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        constexpr float rowHeight = 54.0f;
        constexpr float cardWidth = 232.0f;
        constexpr float avatarSize = 42.0f;
        constexpr float barWidth = 4.0f;
        constexpr float barHeight = 40.0f;
        constexpr float gap = 7.0f;
        constexpr float skillSlotSize = 28.0f;
        constexpr float skillSlotGap = 6.0f;
        constexpr float ultBarWidth = 62.0f;
        constexpr float ultBarHeight = 14.0f;
        constexpr float statusStackGap = 4.0f;

        const bool rosterFresh = entity.roster_state == OW::EntityRosterState::Fresh;
        const bool rosterDead = entity.roster_state == OW::EntityRosterState::Dead;
        const bool rosterMissing = entity.roster_state == OW::EntityRosterState::Missing;
        const float alpha = std::clamp(opacity * (rosterFresh ? 1.0f : 0.52f), 0.0f, 1.0f);
        const float maskAlpha = std::clamp(opacity, 0.0f, 1.0f);
        const bool isEnemy = entity.Team;
        Render::DrawFilledRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                               OverlayRenderDetail::ImColorWithAlpha(8, 11, 15, alpha * 0.70f));
        Render::DrawFilledRect(Vector2(x, y + 4.0f), 3.0f, rowHeight - 8.0f,
                               isEnemy
                                   ? OverlayRenderDetail::ImColorWithAlpha(255, 82, 92, alpha * 0.86f)
                                   : OverlayRenderDetail::ImColorWithAlpha(72, 190, 255, alpha * 0.86f));
        Render::DrawRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                         Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.14f)), 1.0f);

        auto scaledTextSize = [](const std::string& text, float fontSize) {
            ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
            const float baseFontSize = ImGui::GetFontSize();
            if (baseFontSize > 0.0f) {
                const float scale = fontSize / baseFontSize;
                textSize.x *= scale;
                textSize.y *= scale;
            }
            return textSize;
        };

        auto drawCenteredText = [&](float centerX, float centerY, const std::string& text,
                                    float fontSize, ImU32 color) {
            const ImVec2 textSize = scaledTextSize(text, fontSize);
            Render::DrawText(ImVec2(centerX - textSize.x * 0.5f,
                                    centerY - textSize.y * 0.5f),
                             color, text.c_str(), fontSize);
        };

        auto heroInitials = [&]() {
            std::string initials;
            for (unsigned char ch : heroname) {
                if (std::isalnum(ch)) {
                    initials.push_back(static_cast<char>(std::toupper(ch)));
                    if (initials.size() == 2)
                        break;
                }
            }
            return initials.empty() ? std::string("?") : initials;
        };

        auto findHeroAvatar = [&]() -> ID3D11ShaderResourceView* {
            if (!iconManager)
                return nullptr;
            const std::string slug = OW::HeroDisplayNameToSlug(heroname);
            if (slug.empty() || slug == "all")
                return nullptr;
            if (ID3D11ShaderResourceView* avatar = iconManager->GetIcon(slug))
                return avatar;
            return iconManager->LoadHeroAvatar(slug);
        };

        const float avatarX = x + 6.0f;
        const float avatarY = y + (rowHeight - avatarSize) * 0.5f;
        if (ID3D11ShaderResourceView* avatar = findHeroAvatar()) {
            Render::DrawIcon(avatar, ImVec2(avatarX, avatarY), ImVec2(avatarSize, avatarSize),
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        } else {
            const Render::Color fallbackColor = isEnemy
                ? Render::Color(136, 42, 48, OverlayRenderDetail::ToByte(alpha * 0.88f))
                : Render::Color(40, 85, 130, OverlayRenderDetail::ToByte(alpha * 0.88f));
            Render::DrawFilledCircle(Vector2(avatarX + avatarSize * 0.5f,
                                             avatarY + avatarSize * 0.5f),
                                     avatarSize * 0.5f, fallbackColor, 40);
            drawCenteredText(avatarX + avatarSize * 0.5f, avatarY + avatarSize * 0.5f,
                             heroInitials(), 13.0f,
                             OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        }

        const float baseHealth = OverlayRenderDetail::PositiveFinite(entity.MinHealth);
        const float armorHealth = OverlayRenderDetail::PositiveFinite(entity.MinArmorHealth);
        const float shieldHealth = OverlayRenderDetail::PositiveFinite(entity.MinBarrierHealth);
        const float liveCurrentHealth = OverlayRenderDetail::PositiveFinite(entity.PlayerHealth) > 0.0f
            ? OverlayRenderDetail::PositiveFinite(entity.PlayerHealth)
            : baseHealth + armorHealth + shieldHealth;
        const float currentHealth = rosterFresh ? liveCurrentHealth : 0.0f;
        float maxHealth = OverlayRenderDetail::PositiveFinite(entity.PlayerHealthMax);
        if (maxHealth <= 0.0f) {
            maxHealth = OverlayRenderDetail::PositiveFinite(entity.MaxHealth) +
                        OverlayRenderDetail::PositiveFinite(entity.MaxArmorHealth) +
                        OverlayRenderDetail::PositiveFinite(entity.MaxBarrierHealth);
        }
        const float healthRatio = maxHealth > 0.0f ? OverlayRenderDetail::Clamp01(currentHealth / maxHealth) : 0.0f;
        const ImColor healthColor = !rosterFresh
            ? OverlayRenderDetail::ImColorWithAlpha(132, 138, 148, alpha)
            : (healthRatio > 0.60f
            ? OverlayRenderDetail::ImColorWithAlpha(55, 230, 95, alpha)
            : (healthRatio >= 0.30f
                ? OverlayRenderDetail::ImColorWithAlpha(245, 210, 72, alpha)
                : OverlayRenderDetail::ImColorWithAlpha(245, 76, 72, alpha)));

        auto drawVerticalBar = [&](float barX, float barY, float ratio, const ImColor& fillColor) {
            Render::DrawFilledRect(Vector2(barX, barY), barWidth, barHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.48f));
            const float fillHeight = barHeight * OverlayRenderDetail::Clamp01(ratio);
            if (fillHeight > 0.0f) {
                Render::DrawFilledRect(Vector2(barX, barY + barHeight - fillHeight),
                                       barWidth, fillHeight, fillColor);
            }
            Render::DrawRect(Vector2(barX, barY), barWidth, barHeight,
                             Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.18f)), 1.0f);
        };

        float cursorX = avatarX + avatarSize + 4.0f;
        const float barY = y + (rowHeight - barHeight) * 0.5f;
        drawVerticalBar(cursorX, barY, healthRatio, healthColor);
        cursorX += barWidth + 3.0f;

        const float resourceX = cursorX;
        const float armorMax = OverlayRenderDetail::PositiveFinite(entity.MaxArmorHealth);
        const float shieldMax = OverlayRenderDetail::PositiveFinite(entity.MaxBarrierHealth);
        const float resourceCurrent = armorHealth + shieldHealth;
        float resourceMax = armorMax + shieldMax;
        if (resourceMax <= 0.0f)
            resourceMax = resourceCurrent;
        if (resourceMax > 0.0f || resourceCurrent > 0.0f) {
            Render::DrawFilledRect(Vector2(resourceX, barY), barWidth, barHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.48f));
            float fillBottom = barY + barHeight;
            const float clampedArmor = armorMax > 0.0f && armorHealth > armorMax ? armorMax : armorHealth;
            const float clampedShield = shieldMax > 0.0f && shieldHealth > shieldMax ? shieldMax : shieldHealth;
            const float armorHeight = barHeight * OverlayRenderDetail::Clamp01(clampedArmor / resourceMax);
            const float shieldHeight = barHeight * OverlayRenderDetail::Clamp01(clampedShield / resourceMax);
            if (armorHeight > 0.0f) {
                fillBottom -= armorHeight;
                Render::DrawFilledRect(Vector2(resourceX, fillBottom), barWidth, armorHeight,
                                       OverlayRenderDetail::ImColorWithAlpha(255, 194, 58, alpha));
            }
            if (shieldHeight > 0.0f && fillBottom > barY) {
                const float segmentHeight = (shieldHeight < fillBottom - barY) ? shieldHeight : (fillBottom - barY);
                fillBottom -= segmentHeight;
                Render::DrawFilledRect(Vector2(resourceX, fillBottom), barWidth, segmentHeight,
                                       OverlayRenderDetail::ImColorWithAlpha(68, 220, 255, alpha));
            }
            Render::DrawRect(Vector2(resourceX, barY), barWidth, barHeight,
                             Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.18f)), 1.0f);
        }
        cursorX += barWidth + gap;

        const std::string hpText = rosterFresh
            ? std::to_string(static_cast<int>(currentHealth + 0.5f))
            : (rosterDead ? std::string("0") : std::string("--"));
        Render::DrawText(ImVec2(cursorX, y + 17.0f),
                         OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha),
                         hpText.c_str(), 20.0f);
        cursorX += 48.0f + gap;

        const float statusBlockX = x + cardWidth - 7.0f - ultBarWidth;
        const float statusBlockTop = y + (rowHeight - (includeCooldowns && includeUltimate
            ? skillSlotSize + statusStackGap + ultBarHeight
            : (includeCooldowns ? skillSlotSize : ultBarHeight))) * 0.5f;
        const float skillRowY = statusBlockTop;
        const float ultRowY = statusBlockTop + (includeCooldowns ? skillSlotSize + statusStackGap : 0.0f);

        if (includeUltimate) {
            const float ultimate = std::isfinite(entity.ultimate)
                ? std::clamp(entity.ultimate, 0.0f, 100.0f)
                : 0.0f;
            const bool ultimateReady = ultimate >= 100.0f;
            const float ultY = ultRowY;
            Render::DrawFilledRect(Vector2(statusBlockX, ultY), ultBarWidth, ultBarHeight,
                                   ultimateReady
                                       ? OverlayRenderDetail::ImColorWithAlpha(116, 82, 16, alpha * 0.92f)
                                       : OverlayRenderDetail::ImColorWithAlpha(56, 60, 68, alpha * 0.86f));
            if (ultimate > 0.0f) {
                Render::DrawFilledRect(Vector2(statusBlockX, ultY), ultBarWidth * (ultimate / 100.0f), ultBarHeight,
                                       ultimateReady
                                           ? OverlayRenderDetail::ImColorWithAlpha(255, 202, 42, alpha)
                                           : OverlayRenderDetail::ImColorWithAlpha(66, 214, 255, alpha));
            }
            Render::DrawRect(Vector2(statusBlockX, ultY), ultBarWidth, ultBarHeight,
                             ultimateReady
                                 ? Render::Color(255, 238, 112, OverlayRenderDetail::ToByte(alpha))
                                 : Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.24f)),
                             1.0f);
            drawCenteredText(statusBlockX + ultBarWidth * 0.5f, ultY + ultBarHeight * 0.5f,
                             "Ult " + OverlayRenderDetail::FormatUltimatePercent(entity.ultimate),
                             12.5f,
                             ultimateReady
                                 ? OverlayRenderDetail::ImU32WithAlpha(34, 21, 0, alpha)
                                 : OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, alpha));
        }

        if (includeCooldowns) {
            const OW::HeroAbilityIcons* icons = OW::GetAbilityIcons(heroname);
            if (icons && iconManager)
                OverlayRenderDetail::EnsureAbilityIconsLoaded(iconManager, *icons);

            auto skillTexture = [&](const char* abilityIcon) -> ID3D11ShaderResourceView* {
                if (!icons || !iconManager || !abilityIcon || abilityIcon[0] == '\0')
                    return nullptr;
                return iconManager->GetIcon(std::string(icons->heroSlug) + "/" + abilityIcon);
            };

            auto drawSkillCooldownSlot = [&](const char* abilityIcon, const char* label,
                                             bool active, float cooldown) {
                const float slotX = cursorX;
                const float slotY = skillRowY;
                const bool onCooldown = OverlayRenderDetail::IsSkillOnCooldown(active, cooldown);
                const bool readyCooldownSentinel = OverlayRenderDetail::IsSkillReadyCooldownSentinel(cooldown);
                const bool ready = active || readyCooldownSentinel;
                const ImColor readyFillColor = isEnemy
                    ? OverlayRenderDetail::ImColorWithAlpha(74, 24, 18, alpha * 0.96f)
                    : OverlayRenderDetail::ImColorWithAlpha(18, 58, 48, alpha * 0.94f);
                const Render::Color readyBorderColor = isEnemy
                    ? Render::Color(255, 178, 92, OverlayRenderDetail::ToByte(alpha))
                    : Render::Color(172, 255, 226, OverlayRenderDetail::ToByte(alpha));

                Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                       ready
                                           ? readyFillColor
                                           : OverlayRenderDetail::ImColorWithAlpha(12, 16, 22, alpha * 0.82f));
                if (ID3D11ShaderResourceView* texture = skillTexture(abilityIcon)) {
                    Render::DrawIcon(texture, ImVec2(slotX + 1.0f, slotY + 1.0f),
                                     ImVec2(skillSlotSize - 2.0f, skillSlotSize - 2.0f),
                                     OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, ready ? alpha : alpha * 0.64f));
                } else {
                    drawCenteredText(slotX + skillSlotSize * 0.5f, slotY + skillSlotSize * 0.5f,
                                     label, 13.0f,
                                     OverlayRenderDetail::ImU32WithAlpha(255, 255, 255, ready ? alpha : alpha * 0.66f));
                }

                if (onCooldown) {
                    Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                           OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.66f));
                    drawCenteredText(slotX + skillSlotSize * 0.5f, slotY + skillSlotSize * 0.5f,
                                     OverlayRenderDetail::FormatIconCooldown(cooldown), 13.0f,
                                     OverlayRenderDetail::ImU32WithAlpha(255, 244, 128, alpha));
                } else if (!ready) {
                    Render::DrawFilledRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                           OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, alpha * 0.34f));
                }

                Render::DrawRect(Vector2(slotX, slotY), skillSlotSize, skillSlotSize,
                                 ready
                                     ? readyBorderColor
                                     : Render::Color(255, 255, 255, OverlayRenderDetail::ToByte(alpha * 0.28f)),
                                 ready ? 1.6f : 1.0f);
                cursorX += skillSlotSize + skillSlotGap;
            };

            cursorX = statusBlockX + (ultBarWidth - (skillSlotSize * 2.0f + skillSlotGap)) * 0.5f;
            drawSkillCooldownSlot(icons ? icons->ability1Icon : nullptr, "S1",
                                  entity.skill1act, entity.skillcd1);
            drawSkillCooldownSlot(icons ? icons->ability2Icon : nullptr, "S2",
                                  entity.skill2act, entity.skillcd2);
        }

        if (rosterDead || rosterMissing) {
            const char* statusText = rosterDead ? "DEAD" : "MISSING";
            Render::DrawFilledRect(Vector2(x, y + 1.0f), cardWidth, rowHeight - 2.0f,
                                   OverlayRenderDetail::ImColorWithAlpha(0, 0, 0, maskAlpha * 0.38f));
            constexpr float badgeWidth = 74.0f;
            constexpr float badgeHeight = 19.0f;
            const float badgeX = x + cardWidth - badgeWidth - 9.0f;
            const float badgeY = y + (rowHeight - badgeHeight) * 0.5f;
            Render::DrawFilledRect(Vector2(badgeX, badgeY), badgeWidth, badgeHeight,
                                   OverlayRenderDetail::ImColorWithAlpha(18, 21, 26, maskAlpha * 0.90f));
            Render::DrawRect(Vector2(badgeX, badgeY), badgeWidth, badgeHeight,
                             Render::Color(210, 218, 230, OverlayRenderDetail::ToByte(maskAlpha * 0.35f)), 1.0f);
            drawCenteredText(badgeX + badgeWidth * 0.5f, badgeY + badgeHeight * 0.5f,
                             statusText, 12.5f,
                             OverlayRenderDetail::ImU32WithAlpha(235, 240, 248, maskAlpha));
        }

        return y + rowHeight;
    };

    auto renderSidePanel = [&](bool isRight, bool includeUltimate, bool includeCooldowns) {
        constexpr float panelWidth = 236.0f;
        const float panelX = isRight ? OW::WX - panelWidth - 10.0f : 10.0f;
        constexpr float rowHeight = 54.0f;
        constexpr float sectionGap = 34.0f;
        constexpr float headerHeight = 26.0f;
        constexpr float opacity = 1.0f;

        const int enemyCount = static_cast<int>(enemies.size());
        const int allyCount = static_cast<int>(allies.size());

        float totalHeight = 0.0f;
        if (enemyCount > 0)
            totalHeight += headerHeight + enemyCount * rowHeight;
        if (allyCount > 0)
            totalHeight += headerHeight + allyCount * rowHeight;
        if (enemyCount > 0 && allyCount > 0)
            totalHeight += sectionGap;

        float y = (OW::WY - totalHeight) * 0.5f;
        if (y < 10.0f)
            y = 10.0f;
        if (!isRight)
            y = (std::max)(y, OverlayRenderDetail::AimTriggerStatusPanelReservedBottomY());

        auto renderRoster = [&](float startY, bool rowUltimate, bool rowCooldowns) {
            float rowY = startY;
            if (enemyCount > 0) {
                Render::DrawText(ImVec2(panelX, rowY),
                                 OverlayRenderDetail::ImU32WithAlpha(255, 106, 116, opacity),
                                 "ENEMIES", 16.5f);
                rowY += headerHeight;
                for (const OW::c_entity& entity : enemies)
                    rowY = renderEntitySkillRow(entity, panelX, rowY, opacity, rowUltimate, rowCooldowns);
            }

            if (allyCount > 0) {
                if (enemyCount > 0)
                    rowY += sectionGap;
                Render::DrawText(ImVec2(panelX, rowY),
                                 OverlayRenderDetail::ImU32WithAlpha(102, 204, 255, opacity),
                                 "ALLIES", 16.5f);
                rowY += headerHeight;
                for (const OW::c_entity& entity : allies)
                    rowY = renderEntitySkillRow(entity, panelX, rowY, opacity, rowUltimate, rowCooldowns);
            }
            return rowY;
        };

        renderRoster(y, includeUltimate, includeCooldowns);
    };

    if (showUltimateLeft || showSkillLeft)
        renderSidePanel(false, showUltimateLeft, showSkillLeft);
    if (showUltimateRight || showSkillRight)
        renderSidePanel(true, showUltimateRight, showSkillRight);
}

// =========================================================================
// Main aimbot thread
// =========================================================================

// ---- Aim diagnostic counters ----
inline uint64_t g_trackingAttempts = 0;
inline uint64_t g_trackingMoves = 0;
inline uint64_t g_flickAttempts = 0;
inline uint64_t g_flickFires = 0;

namespace OW {

inline bool RefreshAutoGameMouseSensitivity(bool force = false) {
    static DWORD lastRefreshTick = 0;

    if (!OW::Config::autoReadGameMouseSensitivity) {
        OW::Config::detectedGameMouseSensitivity = 0.0f;
        OW::Config::gameMouseSensitivityAutoDetected = false;
        return false;
    }

    const DWORD now = GetTickCount();
    if (!force && lastRefreshTick != 0 && now - lastRefreshTick < 1000)
        return OW::Config::gameMouseSensitivityAutoDetected;
    lastRefreshTick = now;

    float sensitivity = 0.0f;
    uint64_t sourceObject = 0;
    if (!OW::TryReadGameMouseSensitivity(sensitivity, &sourceObject)) {
        if (OW::Config::gameMouseSensitivityAutoDetected) {
            Diagnostics::Aim("game_sens.auto_read lost previous=%.6f manual=%.6f",
                OW::Config::detectedGameMouseSensitivity,
                OW::Config::gameMouseSensitivity);
        }
        OW::Config::detectedGameMouseSensitivity = 0.0f;
        OW::Config::gameMouseSensitivityAutoDetected = false;
        return false;
    }

    const bool changed =
        !OW::Config::gameMouseSensitivityAutoDetected ||
        std::fabs(OW::Config::detectedGameMouseSensitivity - sensitivity) > 0.001f;
    OW::Config::detectedGameMouseSensitivity = sensitivity;
    OW::Config::gameMouseSensitivityAutoDetected = true;
    if (changed) {
        Diagnostics::Aim("game_sens.auto_read value=%.6f object=0x%llX source=globaladmin_singleton_0x6",
            sensitivity,
            static_cast<unsigned long long>(sourceObject));
    }
    return true;
}

} // namespace OW

namespace AimbotDetail {

    struct RuntimeState {
        int hitbotdelaytime = 0;
        int afterdelaytime = 0;
        bool dodelay = false;
        DWORD lastFlickFireTick = 0;
        uint64_t trajectoryWaitEntityKey = 0;
        DWORD trajectoryWaitStartedTick = 0;
        bool hanzoCustomCharging = false;
        bool hanzoCustomLeftDown = false;
        DWORD hanzoCustomChargeStartedTick = 0;
        DWORD hanzoCustomLastLogTick = 0;
        bool trackingSessionActive = false;
        bool trackingSessionTimedOut = false;
        DWORD trackingSessionStartedTick = 0;
        uint64_t trackingSessionHeroId = 0;
        int trackingSessionSlotIndex = -1;
        int trackingSessionAimKey = 0;
    };

    inline OW::c_entity LocalEntity();
    inline bool IsConfiguredActivationKeyPressedForSelection(int keySetting);

    struct TrackingSessionIdentity {
        uint64_t heroId = 0;
        int slotIndex = -1;
        int aimKey = 0;
    };

    struct RuntimePresetSelection {
        OW::Config::HeroPreset preset{};
        int slotIndex = -1;
        bool matchedInput = false;
    };

    inline TrackingSessionIdentity CurrentTrackingSessionIdentity() {
        TrackingSessionIdentity identity{};
        identity.heroId = LocalEntity().HeroID;
        identity.aimKey = OW::Config::aim_key;

        const OW::Config::RuntimeDrawFovState drawFov = OW::Config::SnapshotRuntimeDrawFov();
        if (drawFov.active &&
            drawFov.slotKind == static_cast<int>(OW::Config::FovRingSlotKind::Aim)) {
            identity.slotIndex = drawFov.slotIndex;
        }

        return identity;
    }

    inline bool SameTrackingSessionIdentity(const RuntimeState& state,
                                            const TrackingSessionIdentity& identity) {
        return state.trackingSessionHeroId == identity.heroId &&
            state.trackingSessionSlotIndex == identity.slotIndex &&
            state.trackingSessionAimKey == identity.aimKey;
    }

    inline void ResetTrackingSession(RuntimeState& state) {
        state.trackingSessionActive = false;
        state.trackingSessionTimedOut = false;
        state.trackingSessionStartedTick = 0;
        state.trackingSessionHeroId = 0;
        state.trackingSessionSlotIndex = -1;
        state.trackingSessionAimKey = 0;
    }

    inline void EnsureTrackingSession(RuntimeState& state,
                                      const TrackingSessionIdentity& identity) {
        if (state.trackingSessionActive && SameTrackingSessionIdentity(state, identity))
            return;

        state.trackingSessionActive = true;
        state.trackingSessionTimedOut = false;
        state.trackingSessionStartedTick = GetTickCount();
        state.trackingSessionHeroId = identity.heroId;
        state.trackingSessionSlotIndex = identity.slotIndex;
        state.trackingSessionAimKey = identity.aimKey;
        Diagnostics::Aim("tracking.session start hero=0x%llX slot=%d key=%d",
            static_cast<unsigned long long>(identity.heroId),
            identity.slotIndex + 1,
            identity.aimKey);
    }

    inline bool DistancePassesAimPresetRange(float distance, const OW::Config::HeroPreset& preset) {
        if (!std::isfinite(distance))
            return false;

        const float minDistance = std::clamp(preset.minDistance, 0.0f, 500.0f);
        const float maxDistance = std::clamp(preset.maxDistance, 0.0f, 500.0f);
        if (minDistance > 0.0f && distance < minDistance)
            return false;
        if (maxDistance > 0.0f && distance > maxDistance)
            return false;
        return true;
    }

    inline int ResolvePresetAimBoneForDistance(const OW::Config::HeroPreset& preset, float distance) {
        const int normalized = OW::Config::NormalizeAimBone(preset.bone);
        const bool headRequested = normalized == OW::Config::kAimBoneHead;
        const float headGate = std::clamp(preset.maxHeadDistance, 0.0f, 500.0f);
        if (headRequested && headGate > 0.0f && distance > headGate)
            return OW::Config::kAimBoneNeck;
        return normalized;
    }

    inline bool AimPresetHasSelectableEntityInDistanceRange(const OW::Config::HeroPreset& preset) {
        const std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
        if (entities.empty())
            return false;

        const OW::c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
        const Vector3 camera = OW::TargetingDetail::CameraPosition();
        if (!OW::TargetingDetail::IsFiniteVector(camera) ||
            OW::TargetingDetail::IsZeroVector(camera))
            return false;

        const OW::TargetingDetail::FovRuntimeContext fovContext =
            OW::TargetingDetail::SnapshotFovRuntimeContext();
        if (!fovContext.valid)
            return false;

        for (OW::c_entity entity : entities) {
            if (!OW::TargetingDetail::IsRuntimeTargetValid(entity, false))
                continue;
            if (!OW::TargetingDetail::TargetTeamMatches(entity, preset.targetTeam, local))
                continue;
            if (preset.ignoreInvisible && !entity.Vis)
                continue;

            const float chestDistance = camera.DistTo(entity.chest_pos);
            if (!DistancePassesAimPresetRange(chestDistance, preset))
                continue;

            const int aimBone = ResolvePresetAimBoneForDistance(preset, chestDistance);
            const Vector3 aimPoint = OW::TargetingDetail::ConfiguredBonePosition(entity, aimBone);
            if (!OW::TargetingDetail::IsFiniteVector(aimPoint) ||
                OW::TargetingDetail::IsZeroVector(aimPoint))
                continue;

            const float aimDistance = camera.DistTo(aimPoint);
            if (!DistancePassesAimPresetRange(aimDistance, preset))
                continue;

            if (OW::TargetingDetail::IsWithinFovDeg(fovContext, aimPoint, preset.fov))
                return true;
        }

        return false;
    }

    inline bool AimPresetHasEntityInDistanceRange(const OW::Config::HeroPreset& preset) {
        const std::vector<OW::c_entity> entities = OW::TargetingDetail::SnapshotEntities();
        if (entities.empty())
            return false;

        const Vector3 camera = OW::TargetingDetail::CameraPosition();
        if (!OW::TargetingDetail::IsFiniteVector(camera) ||
            OW::TargetingDetail::IsZeroVector(camera))
            return false;

        for (const OW::c_entity& entity : entities) {
            if (!OW::TargetingDetail::IsRuntimeTargetValid(entity, false))
                continue;

            const float chestDistance = camera.DistTo(entity.chest_pos);
            if (DistancePassesAimPresetRange(chestDistance, preset))
                return true;
        }

        return false;
    }

    inline void LogRuntimePresetSelection(const char* kind,
                                          uint64_t heroId,
                                          const RuntimePresetSelection& selection) {
        struct LastState {
            bool initialized = false;
            uint64_t heroId = 0;
            int slotIndex = -2;
            bool matchedInput = false;
            int key = -1;
            float fov = -1.0f;
            int aimBehavior = -1;
            int triggerMode = -1;
            int triggerKey = -1;
            bool triggerEnabled = false;
        };

        static LastState aimState{};
        static LastState triggerState{};
        const bool isTriggerKind = kind && std::strcmp(kind, "trigger") == 0;
        LastState& state = isTriggerKind ? triggerState : aimState;

        const OW::Config::HeroPreset& preset = selection.preset;
        const bool changed = !state.initialized ||
            state.heroId != heroId ||
            state.slotIndex != selection.slotIndex ||
            state.matchedInput != selection.matchedInput ||
            state.key != preset.key ||
            (!isTriggerKind && std::fabs(state.fov - preset.fov) > 0.001f) ||
            state.aimBehavior != preset.aimBehavior ||
            state.triggerMode != preset.trigger.mode ||
            state.triggerKey != preset.trigger.key ||
            state.triggerEnabled != preset.trigger.enabled;

        if (!changed)
            return;

        const int behaviorMethod = OW::Config::AimBehaviorMethod(preset.aimBehavior);
        if (isTriggerKind) {
            Diagnostics::Aim("hero_preset.runtime kind=%s hero=0x%llX slot=%d matchedInput=%d key=%d aimMode=%d aimBehavior=%d behaviorMethod=%d triggerEnabled=%d triggerMode=%d triggerKey=%d",
                kind,
                static_cast<unsigned long long>(heroId),
                selection.slotIndex + 1,
                selection.matchedInput ? 1 : 0,
                preset.key,
                preset.aimMode,
                preset.aimBehavior,
                behaviorMethod,
                preset.trigger.enabled ? 1 : 0,
                preset.trigger.mode,
                preset.trigger.key);
        } else {
            Diagnostics::Aim("hero_preset.runtime kind=%s hero=0x%llX slot=%d matchedInput=%d key=%d fovDeg=%.2f aimMode=%d aimBehavior=%d behaviorMethod=%d triggerEnabled=%d triggerMode=%d triggerKey=%d",
                kind ? kind : "unknown",
                static_cast<unsigned long long>(heroId),
                selection.slotIndex + 1,
                selection.matchedInput ? 1 : 0,
                preset.key,
                preset.fov,
                preset.aimMode,
                preset.aimBehavior,
                behaviorMethod,
                preset.trigger.enabled ? 1 : 0,
                preset.trigger.mode,
                preset.trigger.key);
        }

        state.initialized = true;
        state.heroId = heroId;
        state.slotIndex = selection.slotIndex;
        state.matchedInput = selection.matchedInput;
        state.key = preset.key;
        state.fov = preset.fov;
        state.aimBehavior = preset.aimBehavior;
        state.triggerMode = preset.trigger.mode;
        state.triggerKey = preset.trigger.key;
        state.triggerEnabled = preset.trigger.enabled;
    }

    inline bool TrySelectRuntimeAimPreset(uint64_t heroId, RuntimePresetSelection& selection) {
        RuntimePresetSelection fallback{};
        RuntimePresetSelection inputFallback{};
        RuntimePresetSelection distanceFallback{};
        bool hasFallback = false;
        bool hasInputFallback = false;
        bool hasDistanceFallback = false;

        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (!OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot) || !slot.enabled)
                continue;

            if (!hasFallback) {
                fallback.preset = slot.preset;
                fallback.slotIndex = slotIndex;
                fallback.matchedInput = false;
                hasFallback = true;
            }

            if (IsConfiguredActivationKeyPressedForSelection(slot.preset.key)) {
                RuntimePresetSelection keyedSelection{};
                keyedSelection.preset = slot.preset;
                keyedSelection.slotIndex = slotIndex;
                keyedSelection.matchedInput = true;

                if (!hasInputFallback) {
                    inputFallback = keyedSelection;
                    hasInputFallback = true;
                }

                if (AimPresetHasSelectableEntityInDistanceRange(slot.preset)) {
                    selection = keyedSelection;
                    return true;
                }

                if (!hasDistanceFallback && AimPresetHasEntityInDistanceRange(slot.preset)) {
                    distanceFallback = keyedSelection;
                    hasDistanceFallback = true;
                }
            }
        }

        if (hasDistanceFallback) {
            selection = distanceFallback;
            return true;
        }

        if (hasInputFallback) {
            selection = inputFallback;
            return true;
        }

        if (!hasFallback)
            return false;

        selection = fallback;
        return true;
    }

    inline bool TrySelectRuntimeTriggerPreset(uint64_t heroId, RuntimePresetSelection& selection) {
        static uint64_t stickyHeroId = 0;
        static int stickyToggleSlotIndex = -1;
        if (stickyHeroId != heroId) {
            stickyHeroId = heroId;
            stickyToggleSlotIndex = -1;
        }

        RuntimePresetSelection fallback{};
        RuntimePresetSelection alwaysCandidate{};
        bool hasFallback = false;
        bool hasAlwaysCandidate = false;

        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            if (!OW::Config::TryGetHeroTriggerSlot(heroId, slotIndex, slot) || !slot.enabled)
                continue;

            if (!hasFallback) {
                fallback.preset = slot.preset;
                fallback.slotIndex = slotIndex;
                fallback.matchedInput = false;
                hasFallback = true;
            }

            const OW::Config::TriggerPreset& trigger = slot.preset.trigger;
            if (!trigger.enabled)
                continue;

            if ((trigger.mode == 0 || trigger.mode == 1) &&
                IsConfiguredActivationKeyPressedForSelection(trigger.key)) {
                stickyToggleSlotIndex = trigger.mode == 1 ? slotIndex : -1;
                selection.preset = slot.preset;
                selection.slotIndex = slotIndex;
                selection.matchedInput = true;
                return true;
            }

            if (trigger.mode == 2 && !hasAlwaysCandidate) {
                alwaysCandidate.preset = slot.preset;
                alwaysCandidate.slotIndex = slotIndex;
                alwaysCandidate.matchedInput = true;
                hasAlwaysCandidate = true;
            }
        }

        if (stickyToggleSlotIndex >= 0 && OW::Config::triggerbotToggleActive) {
            OW::Config::HeroSlotPreset stickySlot{};
            if (OW::Config::TryGetHeroTriggerSlot(heroId, stickyToggleSlotIndex, stickySlot) &&
                stickySlot.enabled &&
                stickySlot.preset.trigger.enabled &&
                stickySlot.preset.trigger.mode == 1) {
                selection.preset = stickySlot.preset;
                selection.slotIndex = stickyToggleSlotIndex;
                selection.matchedInput = true;
                return true;
            }
            stickyToggleSlotIndex = -1;
        }

        if (hasAlwaysCandidate) {
            stickyToggleSlotIndex = -1;
            selection = alwaysCandidate;
            return true;
        }

        if (!hasFallback)
            return false;

        selection = fallback;
        return true;
    }

    struct ScopedHeroPresetOverride {
        bool active = false;
        OW::Config::HeroPreset original{};
        float originalFov = OW::Config::kDefaultFovDeg;
        float originalFov2 = OW::Config::kDefaultFovDeg;
        float originalMinFov1 = OW::Config::kDefaultFovDeg;
        float originalMinFov2 = OW::Config::kDefaultFovDeg;
        OW::Config::RuntimeDrawFovState originalDrawFov{};

        ScopedHeroPresetOverride() {
            const OW::c_entity local = LocalEntity();
            if (local.HeroID == 0)
                return;

            RuntimePresetSelection selection{};
            if (!TrySelectRuntimeAimPreset(local.HeroID, selection))
                return;

            original = OW::Config::MakeHeroPresetFromCurrent();
            originalFov = OW::Config::Fov;
            originalFov2 = OW::Config::Fov2;
            originalMinFov1 = OW::Config::minFov1;
            originalMinFov2 = OW::Config::minFov2;
            originalDrawFov = OW::Config::SnapshotRuntimeDrawFov();
            OW::Config::ApplyHeroAimPresetToGlobals(selection.preset);
            OW::Config::SetRuntimeDrawFov(
                OW::Config::ResolveHeroPresetFovForDistance(selection.preset, 0.0f),
                static_cast<int>(OW::Config::FovRingSlotKind::Aim),
                selection.slotIndex);
            LogRuntimePresetSelection("aim", local.HeroID, selection);
            active = true;
        }

        ~ScopedHeroPresetOverride() {
            if (active) {
                OW::Config::ApplyHeroPresetToGlobals(original);
                OW::Config::Fov = originalFov;
                OW::Config::Fov2 = originalFov2;
                OW::Config::minFov1 = originalMinFov1;
                OW::Config::minFov2 = originalMinFov2;
                OW::Config::RestoreRuntimeDrawFov(originalDrawFov);
            }
        }
    };

    struct ScopedHeroTriggerPresetOverride {
        bool active = false;
        OW::Config::HeroPreset original{};
        float originalFov = OW::Config::kDefaultFovDeg;
        float originalFov2 = OW::Config::kDefaultFovDeg;
        float originalMinFov1 = OW::Config::kDefaultFovDeg;
        float originalMinFov2 = OW::Config::kDefaultFovDeg;
        OW::Config::RuntimeDrawFovState originalDrawFov{};

        ScopedHeroTriggerPresetOverride() {
            const OW::c_entity local = LocalEntity();
            if (local.HeroID == 0)
                return;

            RuntimePresetSelection selection{};
            if (!TrySelectRuntimeTriggerPreset(local.HeroID, selection))
                return;

            original = OW::Config::MakeHeroPresetFromCurrent();
            originalFov = OW::Config::Fov;
            originalFov2 = OW::Config::Fov2;
            originalMinFov1 = OW::Config::minFov1;
            originalMinFov2 = OW::Config::minFov2;
            originalDrawFov = OW::Config::SnapshotRuntimeDrawFov();
            OW::Config::ApplyHeroTriggerPresetToGlobals(selection.preset);
            LogRuntimePresetSelection("trigger", local.HeroID, selection);
            active = true;
        }

        ~ScopedHeroTriggerPresetOverride() {
            if (active) {
                OW::Config::ApplyHeroPresetToGlobals(original);
                OW::Config::Fov = originalFov;
                OW::Config::Fov2 = originalFov2;
                OW::Config::minFov1 = originalMinFov1;
                OW::Config::minFov2 = originalMinFov2;
                OW::Config::RestoreRuntimeDrawFov(originalDrawFov);
            }
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
        return OW::SnapshotCameraPosition();
    }

    inline OW::c_entity LocalEntity() {
        return OW::TargetingDetail::SnapshotLocalEntity();
    }

    inline bool IsMouseActivationKey(int vk) {
        switch (vk) {
        case VK_LBUTTON:
        case VK_RBUTTON:
        case VK_MBUTTON:
        case VK_XBUTTON1:
        case VK_XBUTTON2:
            return true;
        default:
            return false;
        }
    }

    inline void LogAimKeyState(int keySetting, int vk, int sourceType, bool pressed) {
        struct LastLogState {
            bool initialized = false;
            int keySetting = -1;
            int vk = -1;
            bool pressed = false;
            DWORD lastLogTick = 0;
        };

        static LastLogState lastStates[4]{};
        const int stateIndex = (sourceType >= 0 && sourceType < 4) ? sourceType : 0;
        LastLogState& state = lastStates[stateIndex];

        const DWORD now = GetTickCount();
        const bool changed = !state.initialized ||
            state.keySetting != keySetting ||
            state.vk != vk ||
            state.pressed != pressed;

        if (changed || (pressed && (state.lastLogTick == 0 || now - state.lastLogTick >= 1000))) {
            const char* sourceLabel = "GetAsyncKeyState";
            if (sourceType == 1)
                sourceLabel = "kmbox_monitor";
            else if (sourceType == 2)
                sourceLabel = "dma_keystate";
            else if (sourceType == 3)
                sourceLabel = "mock_hardware";

            const bool mockSource = sourceType == 3;
            const int monitorRunning = mockSource
                ? (kmbox::MockHardwareMgr.IsInitialized() ? 1 : 0)
                : (kmbox::KmBoxMgr.KeyBoard.ListenerRuned.load() ? 1 : 0);
            const unsigned long long monitorPackets = mockSource
                ? kmbox::MockHardwareMgr.InputPacketCount()
                : kmbox::KmBoxMgr.KeyBoard.InputPacketCount();

            Diagnostics::Aim("hotkey state keySetting=%d vk=0x%X source=%s monitorRunning=%d monitorPackets=%llu pressed=%d",
                keySetting,
                vk,
                sourceLabel,
                monitorRunning,
                monitorPackets,
                pressed ? 1 : 0);
            state.initialized = true;
            state.keySetting = keySetting;
            state.vk = vk;
            state.pressed = pressed;
            state.lastLogTick = now;
        }
    }

    inline bool IsKmBoxMonitorAvailable() {
        return OW::Config::kmboxEnabled &&
            OW::Config::kmboxDeviceType == 0 &&
            kmbox::KmBoxMgr.KeyBoard.ListenerRuned.load() &&
            kmbox::KmBoxMgr.KeyBoard.InputPacketCount() > 0;
    }

    inline bool IsMockHardwareAvailable() {
        return OW::Config::kmboxEnabled &&
            OW::Config::kmboxDeviceType == 2 &&
            kmbox::MockHardwareMgr.IsInitialized();
    }

    inline bool ReadKmBoxMonitorVk(int vk, int keySetting) {
        bool pressed = false;
        if (IsMouseActivationKey(vk))
            pressed = kmbox::KmBoxMgr.KeyBoard.IsMouseButtonPressed(vk);
        else
            pressed = kmbox::KmBoxMgr.KeyBoard.GetKeyState(static_cast<WORD>(vk));

        LogAimKeyState(keySetting, vk, 1, pressed);
        return pressed;
    }

    inline bool ReadMockHardwareVk(int vk, int keySetting) {
        const bool pressed = kmbox::MockHardwareMgr.IsVkDown(vk);
        LogAimKeyState(keySetting, vk, 3, pressed);
        return pressed;
    }

    inline bool ReadDmaKeyStateVk(int vk, int keySetting, bool strictDmaOnly) {
        const uint64_t keyStateAddress = KeyState::gafAsyncKeyStateAddr.load();
        if (!KeyState::initialized.load() || keyStateAddress == 0) {
            LogAimKeyState(keySetting, vk, 2, false);
            if (strictDmaOnly) {
                static DWORD lastDmaUnavailableWarnTick = 0;
                const DWORD now = GetTickCount();
                if (lastDmaUnavailableWarnTick == 0 || now - lastDmaUnavailableWarnTick >= 1000) {
                    Diagnostics::Aim("hotkey early_return reason=dma_keystate_unavailable keySetting=%d vk=0x%X gafAsyncKeyStateOffset=0x%llX resolvedAddress=0x%llX size=%zu build=%lu session=%lu readPid=%d",
                        keySetting,
                        vk,
                        static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset),
                        static_cast<unsigned long long>(keyStateAddress),
                        KeyState::keyStateByteCount.load(),
                        static_cast<unsigned long>(KeyState::detectedBuild.load()),
                        static_cast<unsigned long>(KeyState::resolvedSessionId.load()),
                        KeyState::keyStateReadPid.load());
                    lastDmaUnavailableWarnTick = now;
                }
            }
            return false;
        }

        const bool pressed = KeyState::IsKeyDown(static_cast<uint32_t>(vk));
        LogAimKeyState(keySetting, vk, 2, pressed);
        return pressed;
    }

    inline bool ReadLocalAsyncKeyStateVk(int vk, int keySetting) {
        const bool pressed = (GetAsyncKeyState(vk) & 0x8000) != 0;
        LogAimKeyState(keySetting, vk, 0, pressed);
        return pressed;
    }

    inline void LogKmBoxMonitorFallback(int keySetting, int vk) {
        static DWORD lastFallbackLogTick = 0;
        const DWORD now = GetTickCount();
        if (lastFallbackLogTick != 0 && now - lastFallbackLogTick < 1000)
            return;

        Diagnostics::Aim("hotkey fallback reason=kmbox_monitor_unavailable keySetting=%d vk=0x%X configuredSource=kmbox kmboxEnabled=%d deviceType=%d monitorRunning=%d monitorPackets=%llu dmaReady=%d dmaAddr=0x%llX",
            keySetting,
            vk,
            OW::Config::kmboxEnabled ? 1 : 0,
            OW::Config::kmboxDeviceType,
            kmbox::KmBoxMgr.KeyBoard.ListenerRuned.load() ? 1 : 0,
            kmbox::KmBoxMgr.KeyBoard.InputPacketCount(),
            (KeyState::initialized.load() && KeyState::gafAsyncKeyStateAddr.load() != 0) ? 1 : 0,
            static_cast<unsigned long long>(KeyState::gafAsyncKeyStateAddr.load()));
        lastFallbackLogTick = now;
    }

    inline bool ReadDmaThenLocalVk(int vk, int keySetting) {
        const bool dmaPressed = ReadDmaKeyStateVk(vk, keySetting, false);
        if (dmaPressed)
            return true;

        return ReadLocalAsyncKeyStateVk(vk, keySetting);
    }

    inline bool IsInputVkDown(int vk, int keySetting = -1) {
        if (vk <= 0) {
            LogAimKeyState(keySetting, vk, 0, false);
            return false;
        }

        if (OW::Config::inputSource == 4 ||
            (OW::Config::inputSource == 1 && OW::Config::kmboxDeviceType == 2)) {
            if (!IsMockHardwareAvailable()) {
                LogAimKeyState(keySetting, vk, 3, false);
                return false;
            }
            return ReadMockHardwareVk(vk, keySetting);
        }

        if (OW::Config::inputSource == 1) {
            if (!IsKmBoxMonitorAvailable()) {
                LogAimKeyState(keySetting, vk, 1, false);
                LogKmBoxMonitorFallback(keySetting, vk);
                return ReadDmaThenLocalVk(vk, keySetting);
            }
            return ReadKmBoxMonitorVk(vk, keySetting);
        }

        if (OW::Config::inputSource == 3)
            return ReadDmaKeyStateVk(vk, keySetting, true);

        if (OW::Config::inputSource == 2)
            return ReadLocalAsyncKeyStateVk(vk, keySetting);

        // Auto is only a fallback mode. If KMBox monitor is running, trust it
        // exclusively so a released KMBox key cannot be overridden by local/DMA.
        if (IsMockHardwareAvailable())
            return ReadMockHardwareVk(vk, keySetting);

        if (IsKmBoxMonitorAvailable())
            return ReadKmBoxMonitorVk(vk, keySetting);

        return ReadDmaThenLocalVk(vk, keySetting);
    }

    inline bool ReadKmBoxMonitorVkQuiet(int vk) {
        if (IsMouseActivationKey(vk))
            return kmbox::KmBoxMgr.KeyBoard.IsMouseButtonPressed(vk);
        return kmbox::KmBoxMgr.KeyBoard.GetKeyState(static_cast<WORD>(vk));
    }

    inline bool ReadMockHardwareVkQuiet(int vk) {
        return kmbox::MockHardwareMgr.IsVkDown(vk);
    }

    inline bool ReadDmaKeyStateVkQuiet(int vk) {
        const uint64_t keyStateAddress = KeyState::gafAsyncKeyStateAddr.load();
        if (!KeyState::initialized.load() || keyStateAddress == 0)
            return false;
        return KeyState::IsKeyDown(static_cast<uint32_t>(vk));
    }

    inline bool ReadLocalAsyncKeyStateVkQuiet(int vk) {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    inline bool ReadDmaThenLocalVkQuiet(int vk) {
        return ReadDmaKeyStateVkQuiet(vk) || ReadLocalAsyncKeyStateVkQuiet(vk);
    }

    inline bool IsInputVkDownQuiet(int vk) {
        if (vk <= 0)
            return false;

        if (OW::Config::inputSource == 4 ||
            (OW::Config::inputSource == 1 && OW::Config::kmboxDeviceType == 2)) {
            if (IsMockHardwareAvailable())
                return ReadMockHardwareVkQuiet(vk);
            return false;
        }

        if (OW::Config::inputSource == 1) {
            if (IsKmBoxMonitorAvailable())
                return ReadKmBoxMonitorVkQuiet(vk);
            return ReadDmaThenLocalVkQuiet(vk);
        }

        if (OW::Config::inputSource == 3)
            return ReadDmaKeyStateVkQuiet(vk);

        if (OW::Config::inputSource == 2)
            return ReadLocalAsyncKeyStateVkQuiet(vk);

        if (IsMockHardwareAvailable())
            return ReadMockHardwareVkQuiet(vk);

        if (IsKmBoxMonitorAvailable())
            return ReadKmBoxMonitorVkQuiet(vk);

        return ReadDmaThenLocalVkQuiet(vk);
    }

    inline bool IsConfiguredActivationKeyPressedForSelection(int keySetting) {
        return IsInputVkDownQuiet(OW::get_bind_id(keySetting));
    }

    inline bool IsConfiguredAimKeyPressed(int keySetting) {
        const int vk = OW::get_bind_id(keySetting);
        if (vk <= 0) {
            LogAimKeyState(keySetting, vk, 0, false);
            static DWORD lastInvalidBindingLogTick = 0;
            const DWORD now = GetTickCount();
            if (lastInvalidBindingLogTick == 0 || now - lastInvalidBindingLogTick >= 1000) {
                Diagnostics::Aim("hotkey early_return reason=invalid_binding keySetting=%d", keySetting);
                lastInvalidBindingLogTick = now;
            }
            return false;
        }

        return IsInputVkDown(vk, keySetting);
    }

    inline bool IsAimKeyPressed() {
        return IsConfiguredAimKeyPressed(OW::Config::aim_key);
    }

    inline bool IsSecondAimKeyPressed() {
        return IsConfiguredAimKeyPressed(OW::Config::aim_key2);
    }

    inline bool HasEntitySnapshot() {
        return !OW::TargetingDetail::SnapshotEntities().empty();
    }

    inline void MaintainSensitivity(float& origin_sens) {
        const uintptr_t sensitive_ptr = OW::GetSenstivePTR();
        if (!sensitive_ptr) return;
        const float current_sens = SDK->RPM<float>(sensitive_ptr);
        if (std::isfinite(current_sens) && current_sens > 0.0f) {
            origin_sens = current_sens;
        }
    }

    inline void SetSensitivityLocked(bool locked, float origin_sens) {
        (void)locked;
        (void)origin_sens;
    }

    inline void MoveAimDelta(const Vector3& current_angle, const Vector3& target_angle, int move_time_ms = -1) {
        const Vector3 delta = target_angle - current_angle;
        const int effective_move_time_ms = move_time_ms < 0
            ? OW::Config::kmboxInputDelayMs
            : move_time_ms;
        Diagnostics::Aim("move.delta current=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) delta=(%.9f,%.9f,%.9f) delta_len=%.9f moveTimeMs=%d",
            current_angle.X,
            current_angle.Y,
            current_angle.Z,
            target_angle.X,
            target_angle.Y,
            target_angle.Z,
            delta.X,
            delta.Y,
            delta.Z,
            delta.Size(),
            effective_move_time_ms);
        OW::SendMouseMove(delta, effective_move_time_ms);
    }

    inline void ClickMouseButton(int button, DWORD sleep_ms = 10) {
        OW::SendMouseButton(button, true);
        Sleep(sleep_ms);
        OW::SendMouseButton(button, false);
    }

    inline constexpr uint32_t PhysicalMouseMaskForButton(int button) {
        switch (button) {
        case 0: return 0x01u;
        case 1: return 0x02u;
        case 2: return 0x04u;
        default: return 0u;
        }
    }

    inline constexpr int VkForMouseButton(int button) {
        switch (button) {
        case 0: return VK_LBUTTON;
        case 1: return VK_RBUTTON;
        case 2: return VK_MBUTTON;
        default: return 0;
        }
    }

    inline bool ShouldHoldFireWhileTracking() {
        return ClampFirePolicy(OW::Config::aimbotFirePolicy) == FirePolicyType::HoldWhileTracking ||
            (OW::Config::aimbotKeepFiring && OW::Config::Tracking);
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

    inline void UnmaskPhysicalMouseButtonsBestEffort(const char* reason) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            const bool ok = OW::UnmaskPhysicalMouseButtons();
            Diagnostics::Aim("fire.unmask_all reason=%s attempt=%d ok=%d",
                reason ? reason : "unknown",
                attempt + 1,
                ok ? 1 : 0);
            if (attempt < 2)
                Sleep(1);
        }
    }

    inline void RecoverMouseAfterMaskedRelease(const char* reason, int button = 0) {
        if (!OW::Config::kmboxEnabled ||
            (OW::Config::kmboxDeviceType != 0 && OW::Config::kmboxDeviceType != 2))
            return;

        Diagnostics::Aim("fire.recover_mouse reason=%s button=%d stage=force_release",
            reason ? reason : "unknown",
            button);
        OW::ForceReleaseMouseButton(button);
        Sleep(1);
        UnmaskPhysicalMouseButtonsBestEffort(reason);
    }

    inline void RecoverHanzoPostFireMouseState() {
        if (!OW::Config::kmboxEnabled ||
            (OW::Config::kmboxDeviceType != 0 && OW::Config::kmboxDeviceType != 2))
            return;

        RecoverMouseAfterMaskedRelease("hanzo_post_fire", 0);
        Sleep(8);
        RecoverMouseAfterMaskedRelease("hanzo_post_fire_delayed", 0);
    }

    inline void ReleaseDmaMouseKey(uint32_t key, DWORD sleep_ms = 10, bool allowPhysicalMask = true) {
        int button = -1;
        if (OW::DmaKeyToMouseButton(key, button)) {
            const uint32_t physicalMask = PhysicalMouseMaskForButton(button);
            const int vk = VkForMouseButton(button);
            const bool physicalDown = vk != 0 && IsInputVkDownQuiet(vk);
            const bool masked = allowPhysicalMask &&
                physicalDown &&
                physicalMask != 0 &&
                OW::MaskPhysicalMouseButtons(physicalMask);
            Diagnostics::Aim("fire.release_mouse keyMask=0x%X button=%d physicalMask=0x%02X physicalDown=%d masked=%d allowMask=%d",
                key,
                button,
                physicalMask,
                physicalDown ? 1 : 0,
                masked ? 1 : 0,
                allowPhysicalMask ? 1 : 0);
            if (masked)
                Sleep(1);
            OW::ForceReleaseMouseButton(button);
            Sleep(sleep_ms);
            if (masked)
                UnmaskPhysicalMouseButtonsBestEffort("release_mouse");
        } else {
            OW::SetKey(key);
            Sleep(sleep_ms);
        }
    }

    inline bool ShouldReleaseChargedFire(const OW::c_entity& local, const OW::WeaponSpec* weapon) {
        if (!weapon || weapon->firePolicy.type != OW::FirePolicyType::ChargeRelease)
            return false;

        return !(local.HeroID == OW::eHero::HERO_HANJO && local.skill2act);
    }

    inline void FireConfiguredAction(const OW::c_entity& local, DWORD sleep_ms = 10) {
        const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::aimbotAttack);
        const uint32_t fireKey = OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::aimbotAttack);
        if (fireKey == 0)
            return;

        if (ShouldReleaseChargedFire(local, weapon)) {
            Diagnostics::Aim("fire.configured mode=charge_release hero=0x%llX action=%d weapon=%s keyMask=0x%X",
                static_cast<unsigned long long>(local.HeroID),
                OW::Config::aimbotAttack,
                weapon ? weapon->weaponId.data() : "none",
                fireKey);
            ReleaseDmaMouseKey(fireKey, sleep_ms);
            return;
        }

        ClickDmaMouseKey(fireKey, sleep_ms);
    }

    inline void PressWithSensitivity(uint32_t key, float origin_sens, DWORD sleep_ms = 1) {
        if (key == 0)
            return;
        SetSensitivityLocked(true, origin_sens);
        ClickDmaMouseKey(key, sleep_ms);
        SetSensitivityLocked(false, origin_sens);
    }

    inline void ClickConfiguredFire(DWORD sleep_ms = 10) {
        FireConfiguredAction(LocalEntity(), sleep_ms);
    }

    inline bool CurrentTarget(c_entity& target, bool requireVisible = false) {
        return OW::TryGetTargetEntity(OW::Config::Targetenemyi, target, requireVisible);
    }

    inline bool IsPrimaryTargetActionable(c_entity& target) {
        if (!CurrentTarget(target)) return false;
        if (target.skill2act && target.HeroID == OW::eHero::HERO_GENJI) return false;
        if (target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE) return false;
        if ((target.imort || target.barrprot) && !OW::Config::switch_team) return false;
        return true;
    }

    inline bool IsTriggerTargetActionable() {
        c_entity target{};
        if (!CurrentTarget(target)) return false;

        c_entity local = LocalEntity();
        if (target.skill2act &&
            target.HeroID == OW::eHero::HERO_GENJI &&
            !target.SameTeamAs(local)) {
            return false;
        }
        return true;
    }

    struct TwoStageScreenBox {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        int points = 0;
    };

    struct TwoStageRuntime {
        uint64_t entityKey = 0;
        bool torsoReached = false;
    };

    struct TwoStageAimPlan {
        Vector3 target{};
        bool active = false;
        bool triggerOpen = true;
        bool innerStage = false;
        int methodOverride = -1;
        float smoothScale = 1.0f;
        float bezierSpeed = -1.0f;
    };

    inline TwoStageRuntime& TwoStageState() {
        static TwoStageRuntime state{};
        return state;
    }

    inline uint64_t TwoStageEntityKey(const c_entity& target) {
        return target.address ? target.address : target.LinkBase;
    }

    inline void ResetTwoStageState() {
        TwoStageState() = TwoStageRuntime{};
    }

    inline bool ProjectAimPoint(const Vector3& world, Vector2& screen) {
        if (IsZeroVector(world))
            return false;
        const OW::Matrix view = OW::SnapshotViewMatrix();
        return view.WorldToScreen(world, &screen, Vector2(OW::WX, OW::WY), false);
    }

    inline void ExpandTwoStageBox(TwoStageScreenBox& box, const Vector2& point) {
        if (box.points == 0) {
            box.left = box.right = point.X;
            box.top = box.bottom = point.Y;
        } else {
            box.left = (std::min)(box.left, point.X);
            box.right = (std::max)(box.right, point.X);
            box.top = (std::min)(box.top, point.Y);
            box.bottom = (std::max)(box.bottom, point.Y);
        }
        ++box.points;
    }

    inline bool AddWorldPointToTwoStageBox(TwoStageScreenBox& box, const Vector3& world) {
        Vector2 screen{};
        if (!ProjectAimPoint(world, screen))
            return false;
        ExpandTwoStageBox(box, screen);
        return true;
    }

    inline bool TryBuildUpperBodyBox(c_entity target, TwoStageScreenBox& box) {
        box = TwoStageScreenBox{};
        AddWorldPointToTwoStageBox(box, target.head_pos);
        AddWorldPointToTwoStageBox(box, target.neck_pos);
        AddWorldPointToTwoStageBox(box, target.chest_pos);
        AddWorldPointToTwoStageBox(box, target.GetBonePos(OW::BONE_R_SHOULDER));
        AddWorldPointToTwoStageBox(box, target.GetBonePos(OW::BONE_L_SHOULDER));
        AddWorldPointToTwoStageBox(box, target.GetBonePos(OW::BONE_PELVIS));

        if (box.points < 2)
            return false;

        const float padding = std::clamp(OW::Config::aimbotFlick2ndBoxPadding, 0.0f, 80.0f);
        box.left -= padding;
        box.top -= padding;
        box.right += padding;
        box.bottom += padding;
        return box.left <= box.right && box.top <= box.bottom;
    }

    inline bool PointInsideTwoStageBox(const TwoStageScreenBox& box, const Vector2& point) {
        return box.points >= 2 &&
            point.X >= box.left &&
            point.X <= box.right &&
            point.Y >= box.top &&
            point.Y <= box.bottom;
    }

    inline float DistanceToTwoStageBox(const TwoStageScreenBox& box, const Vector2& point) {
        const float clampedX = std::clamp(point.X, box.left, box.right);
        const float clampedY = std::clamp(point.Y, box.top, box.bottom);
        const float dx = point.X - clampedX;
        const float dy = point.Y - clampedY;
        return sqrtf(dx * dx + dy * dy);
    }

    inline void ConsiderTwoStagePoint(const Vector3& world,
                                      const Vector2& crosshair,
                                      Vector3& bestWorld,
                                      float& bestDistance) {
        Vector2 screen{};
        if (!ProjectAimPoint(world, screen))
            return;
        const float distance = crosshair.Distance(screen);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestWorld = world;
        }
    }

    inline bool TryNearestUpperBodyPoint(c_entity target,
                                         bool prediction,
                                         Vector3& outTarget,
                                         float& outScreenDistance) {
        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        Vector3 bestWorld{};
        float bestDistance = (std::numeric_limits<float>::max)();

        ConsiderTwoStagePoint(target.head_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.neck_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.chest_pos, crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.GetBonePos(OW::BONE_R_SHOULDER), crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.GetBonePos(OW::BONE_L_SHOULDER), crosshair, bestWorld, bestDistance);
        ConsiderTwoStagePoint(target.GetBonePos(OW::BONE_PELVIS), crosshair, bestWorld, bestDistance);

        if (IsZeroVector(bestWorld))
            return false;

        outTarget = OW::TargetingDetail::ApplyPrediction(target, bestWorld, prediction, false);
        outScreenDistance = bestDistance;
        return true;
    }

    inline Vector3 ResolveHeadAimPoint(c_entity target, bool prediction, const Vector3& fallback) {
        Vector3 head = !IsZeroVector(target.head_pos) ? target.head_pos : target.neck_pos;
        if (IsZeroVector(head))
            head = fallback;
        return OW::TargetingDetail::ApplyPrediction(target, head, prediction, false);
    }

    inline bool TwoStageTriggerOpenForTarget(c_entity target) {
        if (!OW::Config::IsFlick2ndBehavior(OW::Config::aimBehavior) ||
            !OW::Config::aimbotFlick2ndTriggerGate)
            return true;

        const uint64_t entityKey = TwoStageEntityKey(target);
        if (entityKey == 0)
            return false;

        TwoStageRuntime& state = TwoStageState();
        if (state.entityKey != entityKey) {
            state.entityKey = entityKey;
            state.torsoReached = false;
        }

        TwoStageScreenBox box{};
        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        if (!TryBuildUpperBodyBox(target, box))
            return true;

        if (PointInsideTwoStageBox(box, crosshair))
            state.torsoReached = true;

        return state.torsoReached;
    }

    inline TwoStageAimPlan ResolveTwoStageAimPlan(const Vector3& defaultTarget,
                                                  c_entity target,
                                                  bool prediction) {
        TwoStageAimPlan plan{};
        plan.target = defaultTarget;
        if (!OW::Config::IsFlick2ndBehavior(OW::Config::aimBehavior))
            return plan;

        const uint64_t entityKey = TwoStageEntityKey(target);
        if (entityKey == 0)
            return plan;

        TwoStageRuntime& state = TwoStageState();
        if (state.entityKey != entityKey) {
            state.entityKey = entityKey;
            state.torsoReached = false;
        }

        const Vector2 crosshair = OW::TargetingDetail::CrosshairCenter();
        TwoStageScreenBox box{};
        const bool hasBox = TryBuildUpperBodyBox(target, box);
        if (!hasBox) {
            Diagnostics::Aim("two_stage fallback key=0x%llX reason=no_upper_body_box",
                static_cast<unsigned long long>(entityKey));
            return plan;
        }

        const bool insideBox = hasBox && PointInsideTwoStageBox(box, crosshair);
        if (insideBox)
            state.torsoReached = true;

        Vector2 headScreen{};
        const bool hasHeadScreen = ProjectAimPoint(target.head_pos, headScreen);
        const float headDistance = hasHeadScreen
            ? crosshair.Distance(headScreen)
            : (std::numeric_limits<float>::max)();
        const float innerRadius = std::clamp(OW::Config::aimbotFlick2ndInnerRadius, 0.0f, 250.0f);
        const bool innerStage = state.torsoReached || headDistance <= innerRadius;

        plan.active = true;
        plan.triggerOpen = !OW::Config::aimbotFlick2ndTriggerGate || state.torsoReached;
        plan.innerStage = innerStage;
        plan.methodOverride = innerStage
            ? OW::Config::ClampAimMethodIndex(OW::Config::aimbotFlick2ndInnerMethod)
            : -1;
        plan.smoothScale = innerStage
            ? std::clamp(OW::Config::aimbotFlick2ndInnerSmoothScale, 0.1f, 1.0f)
            : 1.0f;
        plan.bezierSpeed = innerStage
            ? (std::max)(1.0f, OW::Config::aimBezierSpeed * plan.smoothScale)
            : -1.0f;

        float bodyDistance = 0.0f;
        Vector3 bodyTarget{};
        const bool hasBodyTarget = TryNearestUpperBodyPoint(target, prediction, bodyTarget, bodyDistance);
        plan.target = innerStage
            ? ResolveHeadAimPoint(target, prediction, defaultTarget)
            : (hasBodyTarget ? bodyTarget : defaultTarget);

        const float boxDistance = hasBox
            ? DistanceToTwoStageBox(box, crosshair)
            : (std::numeric_limits<float>::max)();
        Diagnostics::Aim("two_stage plan key=0x%llX active=1 insideBox=%d torsoReached=%d triggerOpen=%d inner=%d boxDist=%.3f headDist=%.3f bodyDist=%.3f method=%d smoothScale=%.3f target=(%.9f,%.9f,%.9f)",
            static_cast<unsigned long long>(entityKey),
            insideBox ? 1 : 0,
            state.torsoReached ? 1 : 0,
            plan.triggerOpen ? 1 : 0,
            plan.innerStage ? 1 : 0,
            boxDistance,
            headDistance,
            hasBodyTarget ? bodyDistance : -1.0f,
            plan.methodOverride,
            plan.smoothScale,
            plan.target.X,
            plan.target.Y,
            plan.target.Z);
        return plan;
    }

    inline AimData BuildAimData(const Vector3& world_target,
                                bool accelerated,
                                float smooth,
                                float acceleration,
                                int methodOverride = -1,
                                float bezierSpeedOverride = -1.0f) {
        AimData data{};
        const uint64_t playerControllerBase = SDK->g_player_controller;
        if (!playerControllerBase) {
            Diagnostics::Aim("angle.missing playerControllerBase");
            return data;
        }

        OW::Matrix aimViewMatrix{}, aimViewMatrixXor{};
        OW::GetViewMatricesSnapshot(aimViewMatrix, aimViewMatrixXor);
        const XMFLOAT3 cameraPos = aimViewMatrixXor.get_location();
        const XMFLOAT3 cameraForward = aimViewMatrixXor.get_rotation();

        // PlayerController + 0x1260 stores a normalized camera forward direction vector (fx, fy, fz).
        const uint64_t viewDirAddress = playerControllerBase + OW::kPlayerControllerViewDirectionOffset;
        Vector3 viewDir = OW::ReadPlayerControllerViewDirection(playerControllerBase);
        float dirLen = sqrtf(viewDir.X * viewDir.X + viewDir.Y * viewDir.Y + viewDir.Z * viewDir.Z);
        const float matrixForwardLen = sqrtf(cameraForward.x * cameraForward.x +
                                             cameraForward.y * cameraForward.y +
                                             cameraForward.z * cameraForward.z);
        const bool memoryViewDirValid =
            std::isfinite(viewDir.X) &&
            std::isfinite(viewDir.Y) &&
            std::isfinite(viewDir.Z) &&
            dirLen > 0.5f &&
            dirLen < 1.5f;
        const bool matrixForwardValid =
            std::isfinite(cameraForward.x) &&
            std::isfinite(cameraForward.y) &&
            std::isfinite(cameraForward.z) &&
            matrixForwardLen > 0.5f &&
            matrixForwardLen < 1.5f;
        const char* viewDirSource = "player_controller+0x1260";
        if (!memoryViewDirValid && matrixForwardValid) {
            viewDir = Vector3(cameraForward.x, cameraForward.y, cameraForward.z);
            dirLen = matrixForwardLen;
            viewDirSource = "view_matrix_forward_fallback";
        }
        if (!memoryViewDirValid && !matrixForwardValid) {
            Diagnostics::Aim("angle.missing_valid_viewdir playerControllerBase=0x%llX viewDirAddress=0x%llX mem=(%.6f,%.6f,%.6f) mem_len=%.4f matrix=(%.6f,%.6f,%.6f) matrix_len=%.4f",
                static_cast<unsigned long long>(playerControllerBase),
                static_cast<unsigned long long>(viewDirAddress),
                viewDir.X,
                viewDir.Y,
                viewDir.Z,
                dirLen,
                cameraForward.x,
                cameraForward.y,
                cameraForward.z,
                matrixForwardLen);
            return data;
        }

        // Convert direction vector to pitch/yaw Euler angles (radians).
        // forward=(fx, fy, fz), same yaw convention as GetViewMatricesSnapshot().get_rotation().
        const Vector3 localEuler = OW::DirectionToAimEuler(viewDir);
        const float localPitch = localEuler.X;
        const float localYaw = localEuler.Y;
        data.local_angle = Vector3(localPitch, localYaw, 0.0f);

        uint8_t rawData[32]{};
        if (SDK->read_range(viewDirAddress, rawData, sizeof(rawData))) {
            char hexBuf[96]{};
            int off = 0;
            for (int i = 0; i < 32 && off < static_cast<int>(sizeof(hexBuf)) - 4; i++) {
                off += std::snprintf(hexBuf + off, sizeof(hexBuf) - off, "%02X ", rawData[i]);
            }
            Diagnostics::Aim("angle.raw playerControllerBase=0x%llX viewDirAddress=0x%llX hex_32=%s",
                static_cast<unsigned long long>(playerControllerBase),
                static_cast<unsigned long long>(viewDirAddress),
                hexBuf);
        }

        Diagnostics::Aim("angle.viewdir source=%s playerControllerBase=0x%llX viewDirAddress=0x%llX fx=%.6f fy=%.6f fz=%.6f len=%.4f mem_valid=%d matrix_forward=(%.6f,%.6f,%.6f) matrix_len=%.4f",
            viewDirSource,
            static_cast<unsigned long long>(playerControllerBase),
            static_cast<unsigned long long>(viewDirAddress),
            viewDir.X,
            viewDir.Y,
            viewDir.Z,
            dirLen,
            memoryViewDirValid ? 1 : 0,
            cameraForward.x,
            cameraForward.y,
            cameraForward.z,
            matrixForwardLen);
        Diagnostics::Aim("angle.local_converted pitch_rad=%.6f yaw_rad=%.6f pitch_deg=%.2f yaw_deg=%.2f dir_len=%.4f",
            localPitch,
            localYaw,
            RAD2DEG(localPitch),
            RAD2DEG(localYaw),
            dirLen);

        // Guard: camera not yet resolved (identity or zero translation).
        // Computing angles from a zero camera produces bogus aim direction.
        {
            const float camLen = sqrtf(cameraPos.x * cameraPos.x +
                                       cameraPos.y * cameraPos.y +
                                       cameraPos.z * cameraPos.z);
            if (camLen < 1.0f) {
                Diagnostics::Aim("angle.early_return reason=camera_position_zero_or_near_origin cam=(%.3f,%.3f,%.3f) len=%.3f",
                    cameraPos.x, cameraPos.y, cameraPos.z, camLen);
                return data;
            }
        }
        float dx = world_target.X - cameraPos.x;
        float dy = world_target.Y - cameraPos.y;
        float dz = world_target.Z - cameraPos.z;
        float horizontalDist = sqrtf(dx*dx + dz*dz);
        float distance = sqrtf(dx*dx + dy*dy + dz*dz);
        float pitch = -atan2f(dy, horizontalDist);
        float yaw = atan2f(dx, dz);
        data.target_angle = Vector3(pitch, yaw, 0.0f);

        // Yaw wrapping: crosshair delta must take the shortest arc across  PI.
        {
            float yawDelta = data.target_angle.Y - data.local_angle.Y;
            const float rawYawDelta = yawDelta;
            while (yawDelta >  M_PI_F) yawDelta -= 2.0f * M_PI_F;
            while (yawDelta < -M_PI_F) yawDelta += 2.0f * M_PI_F;
            data.target_angle.Y = data.local_angle.Y + yawDelta;
            Diagnostics::Aim("angle.wrap raw_yaw_delta=%.9f wrapped_yaw_delta=%.9f local_yaw=%.9f raw_target_yaw=%.9f wrapped_target_yaw=%.9f",
                rawYawDelta,
                yawDelta,
                data.local_angle.Y,
                yaw,
                data.target_angle.Y);
        }

        Diagnostics::Aim("angle.compute world_target=(%.9f,%.9f,%.9f) camera=(%.9f,%.9f,%.9f) direction=(%.9f,%.9f,%.9f) distance=%.9f horizontal=%.9f camera_forward=(%.9f,%.9f,%.9f) local_angle=(%.9f,%.9f,%.9f) target_angle_rad=(%.9f,%.9f,%.9f) local_angle_deg=(%.6f,%.6f,%.6f) target_angle_deg=(%.6f,%.6f,%.6f)",
            world_target.X,
            world_target.Y,
            world_target.Z,
            cameraPos.x,
            cameraPos.y,
            cameraPos.z,
            dx,
            dy,
            dz,
            distance,
            horizontalDist,
            cameraForward.x,
            cameraForward.y,
            cameraForward.z,
            data.local_angle.X,
            data.local_angle.Y,
            data.local_angle.Z,
            data.target_angle.X,
            data.target_angle.Y,
            data.target_angle.Z,
            RAD2DEG(data.local_angle.X),
            RAD2DEG(data.local_angle.Y),
            RAD2DEG(data.local_angle.Z),
            RAD2DEG(data.target_angle.X),
            RAD2DEG(data.target_angle.Y),
            RAD2DEG(data.target_angle.Z));
        Diagnostics::Aim("angle.coordinate_check localAngleLen=%.9f targetAngleLen=%.9f cameraForwardLen=%.9f expected=local_and_target_are_euler_radians camera_forward_is_direction_vector",
            data.local_angle.Size(),
            data.target_angle.Size(),
            sqrtf(cameraForward.x * cameraForward.x + cameraForward.y * cameraForward.y + cameraForward.z * cameraForward.z));
        if (!std::isfinite(distance) || distance <= 0.0001f ||
            !std::isfinite(data.local_angle.X) ||
            !std::isfinite(data.local_angle.Y) ||
            !std::isfinite(data.target_angle.X) ||
            !std::isfinite(data.target_angle.Y)) {
            Diagnostics::Aim("angle.compute warning possible_coordinate_or_read_issue distance=%.9f local_angle=(%.9f,%.9f,%.9f) target_angle=(%.9f,%.9f,%.9f)",
                distance,
                data.local_angle.X,
                data.local_angle.Y,
                data.local_angle.Z,
                data.target_angle.X,
                data.target_angle.Y,
                data.target_angle.Z);
        }
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::aimBehavior);
        const int dispatchMethod = methodOverride >= 0
            ? OW::Config::ClampAimMethodIndex(methodOverride)
            : OW::Config::AimBehaviorMethod(behavior);
        const float dispatchAcceleration = dispatchMethod == 4
            ? OW::Config::RuntimeAimMethodAcceleration(dispatchMethod)
            : (accelerated ? acceleration : 0.0f);
        data.smoothed_angle = OW::SmoothDispatchWithMethod(
            data.local_angle,
            data.target_angle,
            smooth,
            dispatchAcceleration,
            dispatchMethod,
            bezierSpeedOverride
        );
        const Vector3 rawDelta = data.target_angle - data.local_angle;
        const Vector3 smoothDelta = data.smoothed_angle - data.local_angle;
        Diagnostics::Aim("angle.smooth raw_delta=(%.9f,%.9f,%.9f) raw_delta_len=%.9f smoothed_angle=(%.9f,%.9f,%.9f) smooth_delta=(%.9f,%.9f,%.9f) smooth_delta_len=%.9f smooth_input=%.9f accelerated=%d acceleration=%.9f",
            rawDelta.X,
            rawDelta.Y,
            rawDelta.Z,
            rawDelta.Size(),
            data.smoothed_angle.X,
            data.smoothed_angle.Y,
            data.smoothed_angle.Z,
            smoothDelta.X,
            smoothDelta.Y,
            smoothDelta.Z,
            smoothDelta.Size(),
            smooth,
            accelerated ? 1 : 0,
            dispatchAcceleration);
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

        if (OW::Config::minFov1 > OW::Config::kMaxFovDeg) OW::Config::minFov1 = OW::Config::kMaxFovDeg;
        if (OW::Config::Fov > OW::Config::kMaxFovDeg) OW::Config::Fov = OW::Config::kMaxFovDeg;
        if (clampSecondaryFov) {
            if (OW::Config::minFov2 > OW::Config::kMaxFovDeg) OW::Config::minFov2 = OW::Config::kMaxFovDeg;
            if (OW::Config::Fov2 > OW::Config::kMaxFovDeg) OW::Config::Fov2 = OW::Config::kMaxFovDeg;
        }
    }

    inline bool TargetDelayReady(RuntimeState* state, bool stampHitDelay, bool resetWhenDisabled) {
        if (!OW::Config::targetdelay) {
            if (resetWhenDisabled && OW::Config::doingdelay) OW::Config::doingdelay = false;
            return true;
        }

        if (OW::Config::lastenemy != OW::Config::Targetenemyi) {
            Diagnostics::Aim("target_delay retarget last=%d current=%d targetdelaytime=%d",
                OW::Config::lastenemy,
                OW::Config::Targetenemyi,
                OW::Config::targetdelaytime);
            OW::Config::doingdelay = true;
        }
        if (!OW::Config::doingdelay) return true;

        OW::Config::lastenemy = OW::Config::Targetenemyi;
        if (OW::Config::timebeforedelay == 0) {
            OW::Config::timebeforedelay = GetTickCount();
            Diagnostics::Aim("target_delay early_return reason=started_wait target=%d waitMs=%d",
                OW::Config::Targetenemyi,
                OW::Config::targetdelaytime);
            return false;
        }

        const DWORD elapsed = GetTickCount() - OW::Config::timebeforedelay;
        if (elapsed < static_cast<DWORD>(OW::Config::targetdelaytime)) {
            Diagnostics::Aim("target_delay early_return reason=waiting target=%d elapsedMs=%lu waitMs=%d",
                OW::Config::Targetenemyi,
                static_cast<unsigned long>(elapsed),
                OW::Config::targetdelaytime);
            return false;
        }

        OW::Config::timebeforedelay = 0;
        OW::Config::doingdelay = false;
        if (stampHitDelay && state) state->hitbotdelaytime = GetTickCount();
        Diagnostics::Aim("target_delay ready target=%d elapsedMs=%lu",
            OW::Config::Targetenemyi,
            static_cast<unsigned long>(elapsed));
        return true;
    }

    inline void ArmDelayedShot(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot) return;
        if (OW::Config::shooted || !IsAimKeyPressed()) {
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

    inline DWORD ResolveFlickRestartDelayMs() {
        const DWORD shotClampMs = static_cast<DWORD>(OW::Config::ClampFlickShotClampMs(
            OW::Config::aimbotFlickShotClampMs));
        const DWORD postFireDelayMs = static_cast<DWORD>(OW::Config::ClampFlickPostFireDelayMs(
            OW::Config::aimbotFlickPostFireDelayMs));
        return (std::max)(shotClampMs, postFireDelayMs);
    }

    inline void StampFlickFire(RuntimeState& state) {
        state.lastFlickFireTick = GetTickCount();
        state.trajectoryWaitEntityKey = 0;
        state.trajectoryWaitStartedTick = 0;
    }

    inline void UpdateFlickShotCooldown(RuntimeState& state) {
        if (!OW::Config::shooted || !OW::Config::aimbotKeepFiring)
            return;
        if (state.lastFlickFireTick == 0) {
            OW::Config::shooted = false;
            return;
        }

        const DWORD elapsed = GetTickCount() - state.lastFlickFireTick;
        if (elapsed >= ResolveFlickRestartDelayMs()) {
            OW::Config::shooted = false;
            OW::Config::lasttime = 0;
        }
    }

    inline bool FlickPostFireLockoutActive(const RuntimeState& state) {
        const DWORD delayMs = static_cast<DWORD>(OW::Config::ClampFlickPostFireDelayMs(
            OW::Config::aimbotFlickPostFireDelayMs));
        if (delayMs == 0 || state.lastFlickFireTick == 0)
            return false;
        return GetTickCount() - state.lastFlickFireTick < delayMs;
    }

    inline float TrackingDeadzoneDampingScale(const Vector3& aimTarget, float* outDistancePixels = nullptr) {
        const float radius = OW::Config::ClampTrackingDeadzonePixels(OW::Config::aimbotTrackingDeadzone);
        if (radius <= 0.0f) {
            if (outDistancePixels)
                *outDistancePixels = 0.0f;
            return 1.0f;
        }

        Vector2 screen{};
        if (!ProjectAimPoint(aimTarget, screen)) {
            if (outDistancePixels)
                *outDistancePixels = 0.0f;
            return 1.0f;
        }

        const float distance = OW::TargetingDetail::CrosshairCenter().Distance(screen);
        if (outDistancePixels)
            *outDistancePixels = distance;
        return OW::Config::TrackingDeadzoneDampingScale(distance, radius);
    }

    inline bool FlickTrajectoryWaitReady(RuntimeState& state, const c_entity& target) {
        if (!OW::Config::aimbotFlickTrajectoryWait)
            return true;

        Vector2 screen{};
        ProjectAimPoint(!IsZeroVector(target.head_pos) ? target.head_pos : target.chest_pos, screen);
        const OW::EntityMotionState motion = OW::TargetingDetail::EstimateMotionState(target, screen);
        const uint64_t entityKey = TwoStageEntityKey(target);
        const DWORD now = GetTickCount();
        if (state.trajectoryWaitEntityKey != entityKey) {
            state.trajectoryWaitEntityKey = entityKey;
            state.trajectoryWaitStartedTick = now;
        }

        const DWORD maxWaitMs = static_cast<DWORD>(OW::Config::ClampTrajectoryWaitMs(
            OW::Config::aimbotFlickTrajectoryWaitMs));
        if (maxWaitMs == 0 || now - state.trajectoryWaitStartedTick >= maxWaitMs)
            return true;

        if (motion.kind != OW::EntityMotionState::Kind::AirborneRising)
            return true;

        constexpr float kGravityMetersPerSecond = 9.8f;
        const float verticalSpeed = (std::max)(0.0f, motion.verticalVelocity);
        const float timeToApexMs = (verticalSpeed / kGravityMetersPerSecond) * 1000.0f;
        const float apexWindowMs = OW::Config::ClampTrajectoryApexWindowMs(
            OW::Config::aimbotFlickTrajectoryApexWindowMs);
        if (!std::isfinite(timeToApexMs) || timeToApexMs <= apexWindowMs)
            return true;

        Diagnostics::Aim("flick trajectory_wait target=0x%llX vertical=%.3f timeToApexMs=%.3f windowMs=%.3f elapsedMs=%lu maxWaitMs=%lu",
            static_cast<unsigned long long>(entityKey),
            motion.verticalVelocity,
            timeToApexMs,
            apexWindowMs,
            static_cast<unsigned long>(now - state.trajectoryWaitStartedTick),
            static_cast<unsigned long>(maxWaitMs));
        return false;
    }

    inline void FireHanzo();

    inline void FirePrimaryNormal() {
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_HANJO) {
            FireHanzo();
            return;
        }

        const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::aimbotAttack);
        const uint32_t fireKey = OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::aimbotAttack);
        if (fireKey == 0)
            return;

        if (ShouldReleaseChargedFire(local, weapon)) {
            Diagnostics::Aim("fire.primary mode=charge_release hero=0x%llX action=%d weapon=%s keyMask=0x%X",
                static_cast<unsigned long long>(local.HeroID),
                OW::Config::aimbotAttack,
                weapon ? weapon->weaponId.data() : "none",
                fireKey);
            ReleaseDmaMouseKey(fireKey);
            return;
        }

        if (fireKey == 0x1u &&
            (local.HeroID == OW::eHero::HERO_ANA ||
             local.HeroID == OW::eHero::HERO_WIDOWMAKER ||
             local.HeroID == OW::eHero::HERO_ASHE) && IsInputVkDown(VK_RBUTTON)) {
            OW::SetKeyscopeHold(0x1, 30);
        } else {
            ClickDmaMouseKey(fireKey);
        }
    }

    inline void FireHanzo() {
        const c_entity local = LocalEntity();
        if (local.skill2act) {
            Diagnostics::Aim("hanzo.fire mode=storm_arrow_tap");
            ClickMouseButton(0);
            return;
        }

        Diagnostics::Aim("hanzo.fire mode=charge_release_no_physical_mask keyMask=0x1");
        ReleaseDmaMouseKey(0x1, 10, false);
        RecoverHanzoPostFireMouseState();
    }

    inline void RunAutoScaleFov() {
        if (!OW::Config::autoscalefov) return;

        auto fvec = OW::GetVector3forfov();
        c_entity fov_target{};
        if (IsZeroVector(fvec) || !OW::TryGetTargetEntity(OW::Config::Targetenemyifov, fov_target)) {
            OW::Config::Fov = OW::Config::minFov1;
            OW::Config::Fov2 = OW::Config::minFov2;
            return;
        }

        const Vector3 camera = CameraPosition();
        const float bodyAngleDeg = OW::TargetingDetail::AngularSeparationDegFromCamera(
            camera,
            fov_target.head_pos,
            fov_target.chest_pos);
        if (std::isfinite(bodyAngleDeg) && bodyAngleDeg > 0.0f) {
            const float scaledFovDeg = OW::Config::ClampFovDeg(bodyAngleDeg * 4.0f);
            OW::Config::Fov = (std::max)(scaledFovDeg, OW::Config::minFov1);
            OW::Config::Fov2 = (std::max)(scaledFovDeg, OW::Config::minFov2);
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

    inline bool IsTriggerKeyPressed(int keySetting) {
        return IsConfiguredAimKeyPressed(keySetting);
    }

    inline bool ResolveCurrentAimSlotPredictionEnabled() {
        const c_entity local = OW::TargetingDetail::SnapshotLocalEntity();
        const WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::aimbotAttack);
        return OW::ResolvePredictionEnabled(
            OW::ClampPredictionOverride(OW::Config::aimbotPredictionMode),
            weapon,
            OW::Config::Prediction);
    }

    inline bool InputSequenceBlocksAim(const char* caller,
                                       ExecutionSource requester = ExecutionSource::GlobalAim) {
        if (!OW::ShouldBlockForActiveSequence(requester))
            return false;

        static DWORD lastLogTick = 0;
        const DWORD now = GetTickCount();
        if (lastLogTick == 0 || now - lastLogTick >= 500) {
            Diagnostics::Aim("aim_trigger blocked reason=input_sequence_active caller=%s",
                caller ? caller : "unknown");
            lastLogTick = now;
        }
        return true;
    }

    inline void RunTriggerbot(bool secondary, float origin_sens) {
        if (InputSequenceBlocksAim(secondary ? "triggerbot2" : "triggerbot", ExecutionSource::Trigger))
            return;

        const int mode = secondary ? OW::Config::triggerbotMode2 : OW::Config::triggerbotMode;
        const int keySetting = secondary ? OW::Config::triggerbotKey2 : OW::Config::triggerbotKey;
        const float shotInterval = secondary ? OW::Config::triggerbotShotInterval2 : OW::Config::triggerbotShotInterval;
        const bool chargeAware = secondary ? OW::Config::triggerbotChargeAware2 : OW::Config::triggerbotChargeAware;
        const float minCharge = secondary ? OW::Config::triggerbotMinCharge2 : OW::Config::triggerbotMinCharge;
        const bool ignoreInvisible = secondary ? OW::Config::triggerbotIgnoreInvisible2 : OW::Config::triggerbotIgnoreInvisible;
        bool& toggleActive = secondary ? OW::Config::triggerbotToggleActive2 : OW::Config::triggerbotToggleActive;
        DWORD& lastFireTick = secondary ? OW::Config::triggerbotLastFireTick2 : OW::Config::triggerbotLastFireTick;

        // 1. Determine armed state
        bool armed = false;
        switch (mode) {
        case 0: // Hold
            armed = IsTriggerKeyPressed(keySetting);
            break;
        case 1: { // Toggle
            const bool down = IsTriggerKeyPressed(keySetting);
            static bool prevDownPrimary = false;
            static bool prevDownSecondary = false;
            bool& prevDown = secondary ? prevDownSecondary : prevDownPrimary;
            if (down && !prevDown)
                toggleActive = !toggleActive;
            prevDown = down;
            armed = toggleActive;
            break;
        }
        case 2: // Always
            armed = true;
            break;
        default:
            break;
        }

        if (!armed) return;

        // 2. Shot interval cooldown
        if (shotInterval > 0.0f) {
            const DWORD now = GetTickCount();
            const DWORD intervalMs = static_cast<DWORD>(shotInterval * 5.0f); // 0-100 → 0-500ms
            if (intervalMs > 0 && lastFireTick != 0 && (now - lastFireTick) < intervalMs)
                return;
        }

        // 3. Find target
        const bool predit = ResolveCurrentAimSlotPredictionEnabled();
        const Vector3 vec = secondary ? OW::GetVector3aim2(predit, ignoreInvisible) : OW::GetVector3(predit, ignoreInvisible);
        c_entity target{};
        if (IsZeroVector(vec) || !IsTriggerTargetActionable() || !CurrentTarget(target)) return;

        // 4. Check crosshair proximity
        AimData aim = BuildAimData(vec, false, 1.0f, 0.0f);
        const float hitbox = secondary
            ? OW::Config::aimbotEffectiveHitWindow2
            : OW::Config::aimbotEffectiveHitWindow;
        const bool triggerReady = (!secondary && OW::Config::IsFlick2ndBehavior(OW::Config::aimBehavior))
            ? TwoStageTriggerOpenForTarget(target)
            : OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitbox);
        if (!triggerReady)
            return;

        if (InputSequenceBlocksAim(secondary ? "triggerbot2_fire" : "triggerbot_fire", ExecutionSource::Trigger))
            return;

        // 5. Charge awareness
        if (chargeAware) {
            c_entity local = LocalEntity();
            float charge = 100.0f;

            if (local.HeroID == OW::eHero::HERO_HANJO) {
                charge = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 100.0f;
            } else if (local.HeroID == OW::eHero::HERO_WIDOWMAKER) {
                charge = IsInputVkDown(VK_RBUTTON)
                    ? readult(local.SkillBase + 0x40, 0xB, 0x2567) * 100.0f
                    : 0.0f;
            }

            if (charge < minCharge) return;
        }

        // 6. Fire
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_HANJO) {
            SetSensitivityLocked(true, origin_sens);
            FireHanzo();
            SetSensitivityLocked(false, origin_sens);
        } else {
            const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::aimbotAttack);
            PressWithSensitivity(OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::aimbotAttack), origin_sens, 2);
        }
        lastFireTick = GetTickCount();
    }

    inline bool ShouldYieldToSecondaryAim() {
        return OW::Config::highPriority && IsSecondAimKeyPressed();
    }

    inline bool AimSessionTimedOut(DWORD sessionStartedTick, const char* caller) {
        const DWORD timeoutMs = OW::TargetingDetail::ResolveAimSessionTimeoutMs();
        if (timeoutMs == 0)
            return false;
        const DWORD elapsed = GetTickCount() - sessionStartedTick;
        if (elapsed < timeoutMs)
            return false;

        Diagnostics::Aim("aim_session timeout caller=%s elapsedMs=%lu timeoutMs=%lu",
            caller ? caller : "unknown",
            static_cast<unsigned long>(elapsed),
            static_cast<unsigned long>(timeoutMs));
        OW::TargetingDetail::ResetTargetLockRuntime();
        return true;
    }

    inline void RunTracking(RuntimeState& state, float origin_sens) {
        if (InputSequenceBlocksAim("tracking"))
            return;
        if (!IsAimKeyPressed()) {
            ResetTrackingSession(state);
            return;
        }
        if (OW::Config::reloading)
            return;
        const TrackingSessionIdentity sessionIdentity = CurrentTrackingSessionIdentity();
        EnsureTrackingSession(state, sessionIdentity);
        if (state.trackingSessionTimedOut) {
            static DWORD lastHeldTimeoutLogTick = 0;
            const DWORD now = GetTickCount();
            if (lastHeldTimeoutLogTick == 0 || now - lastHeldTimeoutLogTick >= 250) {
                Diagnostics::Aim("tracking skipped reason=session_timeout_held hero=0x%llX slot=%d key=%d",
                    static_cast<unsigned long long>(sessionIdentity.heroId),
                    sessionIdentity.slotIndex + 1,
                    sessionIdentity.aimKey);
                lastHeldTimeoutLogTick = now;
            }
            OW::TargetingDetail::ResetTargetLockRuntime();
            return;
        }
        g_trackingAttempts++;
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::aimBehavior);

        Diagnostics::Aim("tracking.enter originSens=%.6f reloading=%d scale=%.6f baseSpeed=%.6f method=%d prediction=%d targetDelay=%d",
            origin_sens,
            OW::Config::reloading ? 1 : 0,
            OW::Config::Tracking_smooth,
            OW::Config::AimBehaviorBaseSpeed(behavior),
            OW::Config::AimBehaviorMethod(behavior),
            OW::Config::Prediction ? 1 : 0,
            OW::Config::targetdelay ? 1 : 0);

        // ---- Dry-run mode: log diagnostic info, don't move cursor ----
        if (OW::Config::aimDryRun) {
            static DWORD lastDryRunLog = 0;
            DWORD now = GetTickCount();
            if (now - lastDryRunLog >= (DWORD)OW::Config::aimDryRunLogIntervalMs) {
                lastDryRunLog = now;
                const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
                if (!IsZeroVector(vec)) {
                    AimData aim = BuildAimData(
                        vec,
                        false,
                        OW::Config::AimBehaviorSmoothInput(behavior, OW::Config::Tracking_smooth),
                        OW::Config::AimBehaviorAcceleration(behavior));
                    const float yawCountsPerRadian = OW::Config::KmboxYawCountsPerRadian();
                    const float pitchCountsPerRadian = OW::Config::KmboxPitchCountsPerRadian();
                    Diagnostics::Aim("dryrun.tracking local_angle_deg=(%.4f,%.4f) target_angle_deg=(%.4f,%.4f) delta_deg=(%.4f,%.4f) "
                        "delta_counts_est=(x_from_yaw=%.1f,y_from_pitch=%.1f) yawCountsPerRad=%.1f pitchCountsPerRad=%.1f would_move=%d target_pos=(%.1f,%.1f,%.1f)",
                        RAD2DEG(aim.local_angle.X), RAD2DEG(aim.local_angle.Y),
                        RAD2DEG(aim.target_angle.X), RAD2DEG(aim.target_angle.Y),
                        RAD2DEG(aim.target_angle.X - aim.local_angle.X), RAD2DEG(aim.target_angle.Y - aim.local_angle.Y),
                        -(aim.target_angle.Y - aim.local_angle.Y) * yawCountsPerRadian,
                        (aim.target_angle.X - aim.local_angle.X) * pitchCountsPerRadian,
                        yawCountsPerRadian,
                        pitchCountsPerRadian,
                        1, vec.X, vec.Y, vec.Z);
                } else {
                    Diagnostics::Aim("dryrun.tracking no_target_vector targetIndex=%d entities=%zu",
                        OW::Config::Targetenemyi,
                        OW::TargetingDetail::SnapshotEntities().size());
                }
            }
            Sleep(1);
            return; // Don't actually move cursor
        }

        bool fireHeld = false;
        const OW::c_entity localForWeapon = LocalEntity();
        const OW::WeaponSpec* trackingWeapon = OW::ResolveWeaponSpec(localForWeapon.HeroID, OW::Config::aimbotAttack);
        const int holdFireButton = OW::ResolveTrackingHoldMouseButton(trackingWeapon, OW::Config::aimbotAttack);
        const DWORD sessionStartedTick = state.trackingSessionStartedTick != 0
            ? state.trackingSessionStartedTick
            : GetTickCount();
        auto releaseHeldFire = [&]() {
            if (fireHeld && holdFireButton >= 0) {
                OW::SendMouseButton(holdFireButton, false);
                fireHeld = false;
            }
        };

        while (IsAimKeyPressed() &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            if (AimSessionTimedOut(sessionStartedTick, "tracking")) {
                state.trackingSessionTimedOut = true;
                break;
            }
            bool holdThisTick = false;
            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            c_entity target{};
            if (IsZeroVector(vec)) {
                Diagnostics::Aim("tracking no_move reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::SnapshotEntities().size());
                releaseHeldFire();
            } else if (!IsPrimaryTargetActionable(target)) {
                Diagnostics::Aim("tracking no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
                releaseHeldFire();
            } else {
                const Vector3 aimTarget = vec;
                float deadzoneDistance = 0.0f;
                const float deadzoneDampingScale = TrackingDeadzoneDampingScale(aimTarget, &deadzoneDistance);
                if (deadzoneDampingScale <= 0.0f) {
                    OW::ResetAimSmoothingState();
                    holdThisTick = ShouldHoldFireWhileTracking();
                    if (holdThisTick && !fireHeld && holdFireButton >= 0) {
                        OW::SendMouseButton(holdFireButton, true);
                        fireHeld = true;
                    } else if (!holdThisTick) {
                        releaseHeldFire();
                    }
                    Sleep(1);
                    RunAutoScaleFov();
                    if (ShouldYieldToSecondaryAim()) break;
                    continue;
                }
                const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                    behavior,
                    OW::Config::Tracking_smooth,
                    deadzoneDampingScale);
                if (OW::Config::aimVerboseLog && deadzoneDampingScale < 1.0f) {
                    Diagnostics::Aim("tracking.deadzone_damping distancePx=%.3f radiusPx=%.3f scale=%.6f smoothInput=%.6f",
                        deadzoneDistance,
                        OW::Config::ClampTrackingDeadzonePixels(OW::Config::aimbotTrackingDeadzone),
                        deadzoneDampingScale,
                        smoothInput);
                }
                AimData aim = BuildAimData(
                    aimTarget,
                    false,
                    smoothInput,
                    OW::Config::AimBehaviorAcceleration(behavior));
                ApplyAiAimNoise(aim.smoothed_angle, 500.f, true);
                holdThisTick = ShouldHoldFireWhileTracking();
                if (holdThisTick && !fireHeld && holdFireButton >= 0) {
                    OW::SendMouseButton(holdFireButton, true);
                    fireHeld = true;
                }

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (!TargetDelayReady(nullptr, false, false)) continue;
                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    g_trackingMoves++;
                    if (OW::Config::aimVerboseLog) {
                        Diagnostics::Aim("tracking.tick moved=1 delta_counts_est=(x_from_yaw=%.1f,y_from_pitch=%.1f) target_dist=%.1f",
                            -(aim.smoothed_angle.Y - aim.local_angle.Y) * OW::Config::KmboxYawCountsPerRadian(),
                            (aim.smoothed_angle.X - aim.local_angle.X) * OW::Config::KmboxPitchCountsPerRadian(),
                            CameraPosition().DistTo(aimTarget));
                    }
                    RunCloseRangeActions(aimTarget);
                } else {
                    Diagnostics::Aim("tracking no_move reason=smoothed_angle_zero local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f)",
                        aim.local_angle.X,
                        aim.local_angle.Y,
                        aim.local_angle.Z,
                        aim.target_angle.X,
                        aim.target_angle.Y,
                        aim.target_angle.Z);
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }

            if (!holdThisTick)
                releaseHeldFire();

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
        releaseHeldFire();
    }

    inline void ResetHanzoCustomFlickState(RuntimeState& state, const char* reason) {
        if (state.hanzoCustomLeftDown) {
            Diagnostics::Aim("hanzo.custom reset reason=%s release_left=1",
                reason ? reason : "unknown");
            OW::SendMouseButton(0, false);
        } else if (state.hanzoCustomCharging) {
            Diagnostics::Aim("hanzo.custom reset reason=%s release_left=0",
                reason ? reason : "unknown");
        }

        state.hanzoCustomCharging = false;
        state.hanzoCustomLeftDown = false;
        state.hanzoCustomChargeStartedTick = 0;
        state.hanzoCustomLastLogTick = 0;
    }

    inline void MaintainHanzoCustomFlickState(RuntimeState& state) {
        if (!state.hanzoCustomCharging && !state.hanzoCustomLeftDown)
            return;

        const c_entity local = LocalEntity();
        if (local.HeroID != OW::eHero::HERO_HANJO) {
            ResetHanzoCustomFlickState(state, "hero_changed");
            return;
        }

        if (local.skill2act) {
            ResetHanzoCustomFlickState(state, "storm_arrow_active");
            return;
        }

        if (!IsAimKeyPressed()) {
            ResetHanzoCustomFlickState(state, "aim_key_released");
            return;
        }

        if (OW::Config::shooted) {
            ResetHanzoCustomFlickState(state, "shot_state_active");
            return;
        }

        if (OW::Config::reloading)
            ResetHanzoCustomFlickState(state, "reloading");
    }

    inline float ReadHanzoStormBowChargePercent(const c_entity& local) {
        if (local.SkillBase == 0)
            return -1.0f;

        const float charge = OW::readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 100.0f;
        if (!std::isfinite(charge))
            return -1.0f;
        return std::clamp(charge, 0.0f, 100.0f);
    }

    inline float ResolveHanzoCustomMinChargePercent() {
        return std::clamp((std::max)(OW::Config::aimbotMinCharge, 65.0f), 0.0f, 100.0f);
    }

    inline DWORD ResolveHanzoCustomFallbackChargeMs(float minChargePercent) {
        const float normalized = std::clamp(minChargePercent / 100.0f, 0.0f, 1.0f);
        const float fallbackMs = 200.0f + normalized * 800.0f;
        return static_cast<DWORD>(std::clamp(fallbackMs, 250.0f, 1000.0f));
    }

    inline bool HanzoCustomChargeReady(RuntimeState& state,
                                       const c_entity& local,
                                       float& outChargePercent,
                                       DWORD& outElapsedMs,
                                       DWORD& outFallbackMs,
                                       float& outMinChargePercent) {
        const DWORD now = GetTickCount();
        outElapsedMs = state.hanzoCustomChargeStartedTick == 0
            ? 0
            : now - state.hanzoCustomChargeStartedTick;
        outMinChargePercent = ResolveHanzoCustomMinChargePercent();
        outFallbackMs = ResolveHanzoCustomFallbackChargeMs(outMinChargePercent);
        outChargePercent = ReadHanzoStormBowChargePercent(local);

        return (outChargePercent >= 0.0f && outChargePercent >= outMinChargePercent) ||
            outElapsedMs >= outFallbackMs;
    }

    inline bool BeginHanzoCustomCharge(RuntimeState& state) {
        const int aimVk = OW::get_bind_id(OW::Config::aim_key);
        if (aimVk == VK_LBUTTON) {
            static DWORD lastLeftKeyWarnTick = 0;
            const DWORD now = GetTickCount();
            if (lastLeftKeyWarnTick == 0 || now - lastLeftKeyWarnTick >= 1000) {
                Diagnostics::Aim("hanzo.custom early_return reason=aim_key_is_left_mouse keySetting=%d",
                    OW::Config::aim_key);
                lastLeftKeyWarnTick = now;
            }
            return false;
        }

        if (IsInputVkDownQuiet(VK_LBUTTON)) {
            static DWORD lastPhysicalWarnTick = 0;
            const DWORD now = GetTickCount();
            if (lastPhysicalWarnTick == 0 || now - lastPhysicalWarnTick >= 1000) {
                Diagnostics::Aim("hanzo.custom early_return reason=physical_left_held use_side_key_only=1");
                lastPhysicalWarnTick = now;
            }
            return false;
        }

        state.hanzoCustomCharging = true;
        state.hanzoCustomLeftDown = true;
        state.hanzoCustomChargeStartedTick = GetTickCount();
        state.hanzoCustomLastLogTick = 0;
        Diagnostics::Aim("hanzo.custom charge_begin keySetting=%d keyVk=0x%X minCharge=%.1f fallbackMs=%lu",
            OW::Config::aim_key,
            aimVk,
            ResolveHanzoCustomMinChargePercent(),
            static_cast<unsigned long>(ResolveHanzoCustomFallbackChargeMs(ResolveHanzoCustomMinChargePercent())));
        OW::SendMouseButton(0, true);
        return true;
    }

    inline void FireHanzoCustomChargedShot(RuntimeState& state) {
        Diagnostics::Aim("hanzo.custom release_left");
        OW::SendMouseButton(0, false);
        state.hanzoCustomLeftDown = false;
        state.hanzoCustomCharging = false;
        state.hanzoCustomChargeStartedTick = 0;
        state.hanzoCustomLastLogTick = 0;
    }

    inline void RunHanzoCustomFlick(RuntimeState& state, float origin_sens) {
        if (!state.hanzoCustomCharging && !BeginHanzoCustomCharge(state))
            return;

        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::aimBehavior);
        const DWORD sessionStartedTick = state.hanzoCustomChargeStartedTick != 0
            ? state.hanzoCustomChargeStartedTick
            : GetTickCount();

        while (IsAimKeyPressed() &&
               !OW::Config::shooted &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            if (AimSessionTimedOut(sessionStartedTick, "hanzo_custom_flick")) {
                ResetHanzoCustomFlickState(state, "session_timeout");
                break;
            }

            const c_entity local = LocalEntity();
            if (local.HeroID != OW::eHero::HERO_HANJO || local.skill2act) {
                ResetHanzoCustomFlickState(state, "hero_state_changed");
                break;
            }

            float chargePercent = -1.0f;
            float minChargePercent = 0.0f;
            DWORD elapsedMs = 0;
            DWORD fallbackMs = 0;
            const bool chargeReady = HanzoCustomChargeReady(
                state,
                local,
                chargePercent,
                elapsedMs,
                fallbackMs,
                minChargePercent);

            if (!chargeReady) {
                const DWORD now = GetTickCount();
                if (state.hanzoCustomLastLogTick == 0 ||
                    now - state.hanzoCustomLastLogTick >= 100) {
                    Diagnostics::Aim("hanzo.custom charge_wait charge=%.1f min=%.1f elapsedMs=%lu fallbackMs=%lu",
                        chargePercent,
                        minChargePercent,
                        static_cast<unsigned long>(elapsedMs),
                        static_cast<unsigned long>(fallbackMs));
                    state.hanzoCustomLastLogTick = now;
                }
                Sleep(1);
                RunAutoScaleFov();
                continue;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            if (IsZeroVector(vec)) {
                Diagnostics::Aim("hanzo.custom no_move reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::SnapshotEntities().size());
                Sleep(1);
                RunAutoScaleFov();
                continue;
            }

            c_entity target{};
            if (!IsPrimaryTargetActionable(target)) {
                Diagnostics::Aim("hanzo.custom no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
                Sleep(1);
                RunAutoScaleFov();
                continue;
            }

            if (!TargetDelayReady(&state, true, true))
                continue;
            if (!FlickTrajectoryWaitReady(state, target)) {
                Sleep(1);
                continue;
            }

            const Vector3 aimTarget = vec;
            const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                behavior,
                OW::Config::Flick_smooth);
            AimData aim = BuildAimData(
                aimTarget,
                true,
                smoothInput,
                OW::Config::AimBehaviorAcceleration(behavior));
            ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

            const float hitWindow = OW::Config::aimbotEffectiveHitWindow;
            const bool hitBeforeMove = OW::in_range(
                aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
            bool hitAfterMove = hitBeforeMove;

            if (!IsZeroVector(aim.smoothed_angle)) {
                MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                hitAfterMove = OW::in_range(
                    aim.smoothed_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
                if (OW::Config::aimVerboseLog) {
                    const float deltaDegX = RAD2DEG(aim.target_angle.X - aim.local_angle.X);
                    const float deltaDegY = RAD2DEG(aim.target_angle.Y - aim.local_angle.Y);
                    Diagnostics::Aim("hanzo.custom tick delta_deg=(%.4f,%.4f) hitbox=%.4f charge=%.1f elapsedMs=%lu",
                        deltaDegX,
                        deltaDegY,
                        hitWindow,
                        chargePercent,
                        static_cast<unsigned long>(elapsedMs));
                }
            } else if (OW::Config::aimVerboseLog) {
                Diagnostics::Aim("hanzo.custom no_move reason=smoothed_angle_zero hitBefore=%d",
                    hitBeforeMove ? 1 : 0);
            }

            if (hitBeforeMove || hitAfterMove) {
                Diagnostics::Aim("hanzo.custom fire hitbox_check=passed before=%d after=%d charge=%.1f elapsedMs=%lu",
                    hitBeforeMove ? 1 : 0,
                    hitAfterMove ? 1 : 0,
                    chargePercent,
                    static_cast<unsigned long>(elapsedMs));
                SetSensitivityLocked(true, origin_sens);
                FireHanzoCustomChargedShot(state);
                StampFlickFire(state);
                ++g_flickFires;
                SetSensitivityLocked(false, origin_sens);
                OW::Config::shooted = true;
                if (OW::Config::dontshot)
                    OW::Config::shotcount++;
                break;
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) {
                ResetHanzoCustomFlickState(state, "yield_secondary_aim");
                break;
            }
        }

        if (!IsAimKeyPressed())
            ResetHanzoCustomFlickState(state, "aim_key_released_after_loop");
        else if (OW::Config::reloading)
            ResetHanzoCustomFlickState(state, "reloading_after_loop");
        else if (OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim))
            ResetHanzoCustomFlickState(state, "sequence_block_after_loop");
    }

    inline void RunFlick(RuntimeState& state, float origin_sens) {
        if (InputSequenceBlocksAim("flick"))
            return;
        MaintainHanzoCustomFlickState(state);
        UpdateFlickShotCooldown(state);
        if (FlickPostFireLockoutActive(state))
            return;
        if (!IsAimKeyPressed() || OW::Config::shooted || OW::Config::reloading)
            return;
        g_flickAttempts++;
        const int behavior = OW::Config::ClampAimBehaviorIndex(OW::Config::aimBehavior);
        const c_entity localAtEntry = LocalEntity();

        Diagnostics::Aim("flick.enter originSens=%.6f shooted=%d reloading=%d scale=%.6f baseSpeed=%.6f method=%d acceleration=%.6f prediction=%d",
            origin_sens,
            OW::Config::shooted ? 1 : 0,
            OW::Config::reloading ? 1 : 0,
            OW::Config::Flick_smooth,
            OW::Config::AimBehaviorBaseSpeed(behavior),
            OW::Config::AimBehaviorMethod(behavior),
            OW::Config::AimBehaviorAcceleration(behavior),
            OW::Config::Prediction ? 1 : 0);

        if (!OW::Config::aimDryRun &&
            localAtEntry.HeroID == OW::eHero::HERO_HANJO &&
            !localAtEntry.skill2act) {
            Diagnostics::Aim("hanzo.custom route=primary_flick");
            RunHanzoCustomFlick(state, origin_sens);
            return;
        }

        // ---- Dry-run mode: log diagnostic info, don't move cursor ----
        if (OW::Config::aimDryRun) {
            static DWORD lastDryRunLog = 0;
            DWORD now = GetTickCount();
            if (now - lastDryRunLog >= (DWORD)OW::Config::aimDryRunLogIntervalMs) {
                lastDryRunLog = now;
                const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
                if (!IsZeroVector(vec)) {
                    AimData aim = BuildAimData(
                        vec,
                        true,
                        OW::Config::AimBehaviorSmoothInput(behavior, OW::Config::Flick_smooth),
                        OW::Config::AimBehaviorAcceleration(behavior));
                    const float hitWindow = OW::Config::aimbotEffectiveHitWindow;
                    const bool wouldHit = OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitWindow);
                    Diagnostics::Aim("dryrun.flick local_angle_deg=(%.4f,%.4f) target_angle_deg=(%.4f,%.4f) "
                        "delta_deg=(%.4f,%.4f) hitbox=%.4f would_hit=%d target_pos=(%.1f,%.1f,%.1f)",
                        RAD2DEG(aim.local_angle.X), RAD2DEG(aim.local_angle.Y),
                        RAD2DEG(aim.target_angle.X), RAD2DEG(aim.target_angle.Y),
                        RAD2DEG(aim.target_angle.X - aim.local_angle.X), RAD2DEG(aim.target_angle.Y - aim.local_angle.Y),
                        hitWindow, wouldHit ? 1 : 0,
                        vec.X, vec.Y, vec.Z);
                } else {
                    Diagnostics::Aim("dryrun.flick no_target_vector targetIndex=%d entities=%zu",
                        OW::Config::Targetenemyi,
                        OW::TargetingDetail::SnapshotEntities().size());
                }
            }
            Sleep(1);
            return; // Don't actually move cursor
        }

        ArmDelayedShot(state);
        const DWORD sessionStartedTick = GetTickCount();

        while (IsAimKeyPressed() &&
               !OW::Config::shooted &&
               !OW::Config::reloading &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            if (AimSessionTimedOut(sessionStartedTick, "flick"))
                break;
            if (LocalEntity().HeroID == OW::eHero::HERO_WIDOWMAKER && !IsInputVkDown(VK_RBUTTON)) {
                Diagnostics::Aim("flick no_move reason=widow_scope_not_held");
                Sleep(1);
                continue;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            if (IsZeroVector(vec)) {
                Diagnostics::Aim("flick break reason=no_target_vector targetIndex=%d entities=%zu",
                    OW::Config::Targetenemyi,
                    OW::TargetingDetail::SnapshotEntities().size());
                break;
            }

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                if (!TargetDelayReady(&state, true, true)) continue;
                if (!FlickTrajectoryWaitReady(state, target)) {
                    Sleep(1);
                    continue;
                }
                PrimeDelayedShot(state);

                const TwoStageAimPlan twoStagePlan = ResolveTwoStageAimPlan(vec, target, OW::Config::Prediction);
                const Vector3 aimTarget = twoStagePlan.active ? twoStagePlan.target : vec;
                const float smoothInput = OW::Config::AimBehaviorSmoothInput(
                    behavior,
                    OW::Config::Flick_smooth,
                    twoStagePlan.smoothScale);
                AimData aim = BuildAimData(
                    aimTarget,
                    true,
                    smoothInput,
                    OW::Config::AimBehaviorAcceleration(behavior),
                    twoStagePlan.methodOverride,
                    twoStagePlan.bezierSpeed);
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);
                const float hitWindow = OW::Config::aimbotEffectiveHitWindow;

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (DelayedShotTimedOut(state)) {
                        const c_entity local = LocalEntity();
                        Diagnostics::Aim("flick delayed_shot timeout target=%d localHero=0x%llX",
                            OW::Config::Targetenemyi,
                            static_cast<unsigned long long>(local.HeroID));
                        if (local.HeroID == OW::eHero::HERO_HANJO) {
                            FireHanzo();
                        } else {
                            ClickConfiguredFire();
                        }
                        StampFlickFire(state);
                        OW::Config::shooted = true;
                        continue;
                    }

                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::Config::aimVerboseLog) {
                        const float deltaDegX = RAD2DEG(aim.target_angle.X - aim.local_angle.X);
                        const float deltaDegY = RAD2DEG(aim.target_angle.Y - aim.local_angle.Y);
                        Diagnostics::Aim("flick.tick delta_deg=(%.4f,%.4f) hitbox=%.4f missbox=%.4f",
                            deltaDegX, deltaDegY, hitWindow, OW::Config::missbox);
                    }
                    const bool hitBeforeMove = OW::in_range(
                        aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
                    const bool hitAfterMove = OW::in_range(
                        aim.smoothed_angle, aim.target_angle, aim.local_pos, aimTarget, hitWindow);
                    if (!hitBeforeMove && hitAfterMove) {
                        Diagnostics::Aim("flick.fire predicted_post_move_hit target=%d hitbox=%.6f",
                            OW::Config::Targetenemyi,
                            hitWindow);
                    }
                    if ((hitBeforeMove || hitAfterMove) &&
                        (!twoStagePlan.active || twoStagePlan.triggerOpen)) {
                        if (OW::Config::aimVerboseLog) {
                            Diagnostics::Aim("flick.fire hitbox_check=passed before=%d after=%d",
                                hitBeforeMove ? 1 : 0,
                                hitAfterMove ? 1 : 0);
                        }
                        SetSensitivityLocked(true, origin_sens);
                        FirePrimaryNormal();
                        StampFlickFire(state);
                        g_flickFires++;
                        SetSensitivityLocked(false, origin_sens);
                        OW::Config::shooted = true;
                        if (OW::Config::dontshot) OW::Config::shotcount++;
                        break;
                    }

                    if (OW::Config::dontshot &&
                        OW::Config::shotcount >= OW::Config::shotmanydont &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, aimTarget, OW::Config::missbox)) {
                        const int previousShotCount = OW::Config::shotcount;
                        OW::Config::shotcount = 0;
                        const c_entity local = LocalEntity();
                        Diagnostics::Aim("flick dontshot forced_fire target=%d previousShotCount=%d missbox=%.6f",
                            OW::Config::Targetenemyi,
                            previousShotCount,
                            OW::Config::missbox);
                        if (local.HeroID == OW::eHero::HERO_HANJO) {
                            FireHanzo();
                        } else {
                            ClickConfiguredFire();
                        }
                        StampFlickFire(state);
                        OW::Config::shooted = true;
                        continue;
                    }
                } else {
                    Diagnostics::Aim("flick no_move reason=smoothed_angle_zero local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f)",
                        aim.local_angle.X,
                        aim.local_angle.Y,
                        aim.local_angle.Z,
                        aim.target_angle.X,
                        aim.target_angle.Y,
                        aim.target_angle.Z);
                }
            } else {
                Diagnostics::Aim("flick no_move reason=target_not_actionable targetIndex=%d vec=(%.9f,%.9f,%.9f)",
                    OW::Config::Targetenemyi,
                    vec.X,
                    vec.Y,
                    vec.Z);
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
    }

    inline void RunGenjiBlade() {
        c_entity local = LocalEntity();
        if (!OW::Config::GenjiBlade || !IsInputVkDown('Q') ||
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

                AimData aim = BuildAimData(
                    vec,
                    false,
                    OW::Config::AimBehaviorSmoothInput(0, speed),
                    OW::Config::AimBehaviorAcceleration(0));
                if (!IsZeroVector(aim.smoothed_angle)) {
                    const float dist2 = CameraPosition().DistTo(vec);
                    if ((!local.skillcd1 && dist2 < 20.f) || dist2 < 7.f) {
                        MoveAimDelta(aim.local_angle, aim.smoothed_angle);
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
        if (!IsZeroVector(vec) && CurrentTarget(target) && target.Team) {
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
        if (!IsZeroVector(vec) && CurrentTarget(target) && target.Team) {
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
        if (IsZeroVector(vec) || !CurrentTarget(target)) return;
        if (target.imort || target.barrprot) return;
        if (target.HeroID == 0x16dd || target.HeroID == 0x16ee) return;

        const c_entity local = LocalEntity();
        if (local.skillcd1) return;

        const float dist = CameraPosition().DistTo(vec);
        AimData aim = BuildAimData(
            vec,
            false,
            OW::Config::AimBehaviorSmoothInput(0, OW::Config::Tracking_smooth),
            OW::Config::AimBehaviorAcceleration(0));
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

    inline void ResetShootStateOnRelease(RuntimeState& state) {
        if (IsAimKeyPressed()) return;

        OW::Config::shooted = false;
        OW::Config::lasttime = 0;
        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
        OW::Config::Targetenemyi = -1;
        OW::TargetingDetail::ResetTargetLockRuntime();
        ResetTwoStageState();
        ResetTrackingSession(state);
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
        if (InputSequenceBlocksAim("secondaim"))
            return;

        const DWORD sessionStartedTick = GetTickCount();
        while (IsSecondAimKeyPressed() &&
               !OW::Config::shooted2 &&
               !OW::ShouldBlockForActiveSequence(ExecutionSource::GlobalAim)) {
            if (AimSessionTimedOut(sessionStartedTick, "secondaim")) {
                break;
            }
            const Vector3 vec = OW::GetVector3aim2(ResolveCurrentAimSlotPredictionEnabled());
            c_entity target{};
            if (!IsZeroVector(vec) && CurrentTarget(target) &&
                !(target.skill2act && target.HeroID == OW::eHero::HERO_GENJI)) {
                AimData aim{};
                if (OW::Config::Tracking2) {
                    const int method = OW::Config::SecondaryAimMethod(0);
                    aim = BuildAimData(
                        vec,
                        false,
                        OW::Config::AimBehaviorSmoothInput(0, OW::Config::Tracking_smooth2),
                        OW::Config::RuntimeAimMethodAcceleration(method),
                        method);
                } else if (OW::Config::Flick2) {
                    const int method = OW::Config::SecondaryAimMethod(1);
                    aim = BuildAimData(
                        vec,
                        true,
                        OW::Config::AimBehaviorSmoothInput(1, OW::Config::Flick_smooth2),
                        OW::Config::RuntimeAimMethodAcceleration(method),
                        method);
                } else
                    aim = BuildAimData(vec, false, 1.0f, 0.0f);

                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    RunCloseRangeActions(vec);
                    MoveAimDelta(aim.local_angle, aim.smoothed_angle);
                    if (OW::Config::Flick2 &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::aimbotEffectiveHitWindow2)) {
                        if (OW::Config::aimVerboseLog) {
                            const OW::c_entity local = LocalEntity();
                            const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(local.HeroID, OW::Config::aimbotAttack);
                            Diagnostics::Aim("secondaim.fire action=%d keyMask=0x%X",
                                OW::Config::aimbotAttack,
                                OW::ResolveGeneratedFireKeyMask(weapon, OW::Config::aimbotAttack));
                        }
                        ClickConfiguredFire();
                        Sleep(1);
                        OW::Config::shooted2 = true;
                    }
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }
            Sleep(1);
        }

        if (!IsSecondAimKeyPressed())
            OW::TargetingDetail::ResetTargetLockRuntime();

        if (OW::Config::shooted2 && !IsSecondAimKeyPressed())
            OW::Config::shooted2 = false;
    }

    inline void RunAimbotTick(RuntimeState& state, float& origin_sens) {
        if (InputSequenceBlocksAim("aimbot_tick"))
            return;

        if (OW::Config::AntiAFK) {
            OW::SetKey(0x57);
            Sleep(1000);
        }

        OW::RefreshAutoGameMouseSensitivity();

        if (!HasEntitySnapshot()) {
            if (IsAimKeyPressed()) {
                static DWORD lastNoSnapshotLogTick = 0;
                const DWORD now = GetTickCount();
                if (lastNoSnapshotLogTick == 0 || now - lastNoSnapshotLogTick >= 1000) {
                    Diagnostics::Aim("aimbot_tick early_return reason=no_entity_snapshot");
                    lastNoSnapshotLogTick = now;
                }
            }
            return;
        }

        MaintainSensitivity(origin_sens);

        {
            ScopedHeroTriggerPresetOverride triggerPresetOverride;
            if (OW::Config::triggerbot) RunTriggerbot(false, origin_sens);
            if (OW::Config::triggerbot2) RunTriggerbot(true, origin_sens);
        }

        if (OW::Config::Tracking) RunTracking(state, origin_sens);
        else if (OW::Config::Flick) RunFlick(state, origin_sens);

        RunGenjiBlade();
        RunAutoScaleFov();
        RunAutoMelee();
        RunAutoRmb();
        RunAutoShiftGenji();
        RunAutoSkill();
        RunAutoShootCooldown();
        ResetShootStateOnRelease(state);
        RunReaperReloadCancel();
        RunSecondAim();
    }

    inline void RunAimbotTickWithHeroPreset(RuntimeState& state, float& origin_sens) {
        if (InputSequenceBlocksAim("aimbot_preset"))
            return;

        ScopedHeroPresetOverride heroPresetOverride;
        RunAimbotTick(state, origin_sens);
    }
}

inline void aimbot_thread() {
    Diagnostics::ScopedDmaCallsite::Push(Diagnostics::DmaCallsite::Aimbot);
    __try {
        AimbotDetail::RuntimeState state{};
        static float origin_sens = 0.f;
        static uint64_t totalTicks = 0;
        static DWORD lastSummaryTick = 0;

        while (true) {
            if (!OW::ProcessConnection::IsConnected()) {
                Sleep(100);
                continue;
            }

            totalTicks++;

            // Periodic diagnostic summary every 5 seconds
            DWORD now = GetTickCount();
            if (lastSummaryTick == 0 || now - lastSummaryTick >= 5000) {
                Diagnostics::Aim("aimbot.summary totalTicks=%llu trackingAttempts=%llu trackingMoves=%llu "
                    "flickAttempts=%llu flickFires=%llu dryRun=%d",
                    (unsigned long long)totalTicks,
                    (unsigned long long)g_trackingAttempts,
                    (unsigned long long)g_trackingMoves,
                    (unsigned long long)g_flickAttempts,
                    (unsigned long long)g_flickFires,
                    OW::Config::aimDryRun ? 1 : 0);
                lastSummaryTick = now;
            }

            AimbotDetail::RunAimbotTickWithHeroPreset(state, origin_sens);
            Sleep(2);
        }
    } __except (1) {}
    Diagnostics::ScopedDmaCallsite::Pop();
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
        if (!OW::ProcessConnection::IsConnected()) {
            Sleep(100);
            continue;
        }

        const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
        const uint64_t currentHeroId = localSnapshot.HeroID;
        if (!OW::Config::Menu && currentHeroId != 0 && lastHeroId != currentHeroId) {
            if (lastHeroId != 0) {
                OW::Config::SaveConfigForHero(OW::Config::ConfigPath(), lastHeroId, localSnapshot.LinkBase);
            }

            OW::Config::LoadConfigForHero(OW::Config::ConfigPath(), currentHeroId, localSnapshot.LinkBase);
            lastHeroId = currentHeroId;
            OW::Config::nowhero = "Now using: " + OW::GetHeroEngNames(currentHeroId, localSnapshot.LinkBase);
        } else if (OW::Config::manualsave && lastHeroId != 0) {
            OW::Config::SaveConfigForHero(OW::Config::ConfigPath(), lastHeroId, localSnapshot.LinkBase);
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
                    WritePrivateProfileStringA(section, key, bufsave, OW::Config::ConfigPath().c_str());
                };
                auto saveHeroFloat = [&](const char* section, const char* key, float value) {
                    sprintf(bufsave, "%d",(int)(value * 10000));
                    WritePrivateProfileStringA(section, key, bufsave, OW::Config::ConfigPath().c_str());
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
                saveHeroFloat(sec, "hitboxScale", OW::Config::hitbox);
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

                int dec = OW::Config::Flick ? 1 : 0;
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
                saveHero(sec, "triggerbotIgnoreInvisible2", OW::Config::triggerbotIgnoreInvisible2);
                saveHero(sec, "Tracking2",    OW::Config::Tracking2);
                saveHero(sec, "Flick2",       OW::Config::Flick2);
                saveHero(sec, "aim_key2",     OW::Config::aim_key2);
                saveHero(sec, "togglekey",    OW::Config::togglekey);
                saveHeroFloat(sec, "Tracking_smooth2", OW::Config::Tracking_smooth2);
                saveHeroFloat(sec, "Flick_smooth2",    OW::Config::Flick_smooth2);
                saveHeroFloat(sec, "accvalue2",        OW::Config::accvalue2);
                saveHeroFloat(sec, "hitbox2Scale",     OW::Config::hitbox2);
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
                    WritePrivateProfileStringA(section, (std::string(prefix) + "x").c_str(), bufsave, OW::Config::ConfigPath().c_str());
                    sprintf(bufsave, "%d",(int)(c.y * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "y").c_str(), bufsave, OW::Config::ConfigPath().c_str());
                    sprintf(bufsave, "%d",(int)(c.z * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "z").c_str(), bufsave, OW::Config::ConfigPath().c_str());
                    sprintf(bufsave, "%d",(int)(c.w * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "w").c_str(), bufsave, OW::Config::ConfigPath().c_str());
                };
                saveColor("Global", "EnemyCol",    OW::Config::EnemyCol);
                saveColor("Global", "fovcol",      OW::Config::fovcol);
                saveColor("Global", "invisenargb", OW::Config::invisnenargb);
                saveColor("Global", "enargb",      OW::Config::enargb);
                saveColor("Global", "targetargb",  OW::Config::targetargb);
                saveColor("Global", "allyargb",    OW::Config::allyargb);

                std::string saveMsg = "Saved: " + heroName;
                // Notification is handled by overlay layer
            }

            // Load config for new hero
            auto loadHero = [&](const char* section, const char* key, int def) -> int {
                return GetPrivateProfileIntA(section, key, def, OW::Config::ConfigPath().c_str());
            };
            auto loadHeroFloat = [&](const char* section, const char* key, int def) -> float {
                return (float)GetPrivateProfileIntA(section, key, def, OW::Config::ConfigPath().c_str()) / 10000.f;
            };
            auto heroKeyExists = [&](const char* section, const char* key) -> bool {
                char value[2]{};
                return GetPrivateProfileStringA(section, key, "", value, sizeof(value), OW::Config::ConfigPath().c_str()) > 0;
            };
            auto legacyHitboxRadiusToScale = [](float radius) -> float {
                if (!std::isfinite(radius))
                    return OW::Config::kDefaultHitboxScalePercent;
                if (radius > 5.0f)
                    return OW::Config::ClampHitboxScalePercent(radius);
                if (radius <= 0.0f)
                    return OW::Config::kMinHitboxScalePercent;
                return OW::Config::ClampHitboxScalePercent(
                    (radius / OW::Config::kLegacyDefaultHitboxRadius) * 100.0f);
            };
            auto loadHeroHitboxScale = [&](const char* section,
                                           const char* scaleKey,
                                           const char* legacyKey,
                                           int legacyDef) -> float {
                if (heroKeyExists(section, scaleKey))
                    return OW::Config::ClampHitboxScalePercent(
                        loadHeroFloat(section, scaleKey, static_cast<int>(OW::Config::kDefaultHitboxScalePercent * 10000.0f)));
                return legacyHitboxRadiusToScale(loadHeroFloat(section, legacyKey, legacyDef));
            };

            std::string heroName = OW::GetHeroEngNames(OW::local_entity.HeroID, OW::local_entity.LinkBase);
            const char* sec = heroName.c_str();

            OW::Config::Fov             = (float)loadHero(sec, "FOV", 200);
            OW::Config::minFov1         = (float)loadHero(sec, "FOV", 200);
            OW::Config::comarea         = loadHeroFloat(sec, "comarea", 100);
            OW::Config::comspeed        = loadHeroFloat(sec, "comspeed", 5000);
            OW::Config::hitbox          = loadHeroHitboxScale(sec, "hitboxScale", "hitbox", 1300);
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
            OW::Config::triggerbotIgnoreInvisible2 = loadHero(sec, "triggerbotIgnoreInvisible2", 1);
            OW::Config::Tracking2       = loadHero(sec, "Tracking2", 0);
            OW::Config::Flick2          = loadHero(sec, "Flick2", 0);
            OW::Config::aim_key2        = loadHero(sec, "aim_key2", 5);
            OW::Config::togglekey       = loadHero(sec, "togglekey", 0);
            OW::Config::Tracking_smooth2 = loadHeroFloat(sec, "Tracking_smooth2", 1000);
            OW::Config::Flick_smooth2   = loadHeroFloat(sec, "Flick_smooth2", 1000);
            OW::Config::accvalue2       = loadHeroFloat(sec, "accvalue2", 1000);
            OW::Config::hitbox2         = loadHeroHitboxScale(sec, "hitbox2Scale", "hitbox2", 1300);
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
            loadColor("Global", "invisenargb", OW::Config::invisnenargb, 0.4f, 0.37f, 0.91f, 1.f);
            loadColor("Global", "enargb",      OW::Config::enargb,      1.f, 0.3f, 0.f, 1.f);
            loadColor("Global", "targetargb",  OW::Config::targetargb,  1.f, 1.f, 0.f, 0.8f);
            loadColor("Global", "allyargb",    OW::Config::allyargb,    0.4f, 1.f, 1.f, 0.4f);

            // Restore aim mode
            int dec = std::clamp(loadHero(sec, "Aim Mode", 0), 0, 1);
            OW::Config::Tracking = (dec == 0);
            OW::Config::Flick = (dec == 1);

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
    Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::Unknown);
    while (1) {
        Sleep(10);
    }
}
