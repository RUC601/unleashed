#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

#include <Windows.h>

#include "Game/Entity.hpp"
#include "Utils/Types.hpp"

namespace OW {

enum class AimClass : int {
    HitscanSingle = 0,
    HitscanAuto,
    HitscanBurst,
    ProjectileSingle,
    ProjectileAuto,
    ProjectileExplosive,
    Shotgun,
    Beam,
    Targeted,
    Melee,
    Movement,
    Unknown,
};

enum class PredictionOverrideMode : int {
    Auto = 0,
    ForceOn = 1,
    ForceOff = 2,
};

enum class AimBehaviorType : int {
    Tracking = 0,
    Flick = 1,
    FlickClamp = 2,
    FlickDelay = 3,
    ReacquireAtApex = 4,
};

enum class FirePolicyType : int {
    ManualOnly = 0,
    HoldWhileTracking = 1,
    TapOnHitWindow = 2,
    ReleaseAfterDelay = 3,
    TimedBurst = 4,
    ChargeRelease = 5,
};

enum class TraceMode : int {
    Strict = 0,
    Relaxed = 1,
    Off = 2,
};

enum class UnlockMode : int {
    Anytime = 0,
    OnRelease = 1,
    NeverUntilTimeout = 2,
};

enum class SmoothingControllerType : int {
    Linear = 0,
    PID = 1,
    Bezier = 2,
    PiecewiseCurve = 3,
    AccelLimited = 4,
    Constant = 5,
};

enum class ExecutionSource : int {
    GlobalAim = 0,
    Trigger = 1,
    HeroSkill = 2,
    SequenceInternal = 3,
};

struct PredictionSpec {
    bool enabledByDefault = false;
    float projectileSpeed = 0.0f;
    bool gravity = false;
    float projectileRadius = 0.0f;
    float chargeMinSpeed = 0.0f;
    float chargeMaxSpeed = 0.0f;
    bool directHitOnly = true;
};

struct FirePolicy {
    FirePolicyType type = FirePolicyType::ManualOnly;
    float releaseDelayMs = 0.0f;
    float burstDurationMs = 0.0f;
    float minCharge = 0.0f;
    float maxCharge = 100.0f;
};

struct AimBehavior {
    AimBehaviorType type = AimBehaviorType::Tracking;
    float behaviorTolerance = 0.0f;
    float maxAimTimeMs = 0.0f;
    float minDistance = 0.0f;
    float maxDistance = 0.0f;
    float headshotDistanceGate = 0.0f;
};

struct TargetLockPolicy {
    TraceMode traceMode = TraceMode::Strict;
    UnlockMode unlockMode = UnlockMode::Anytime;
    float minLockMs = 0.0f;
    float maxLockMs = 0.0f;
    float retargetHysteresis = 0.0f;
};

struct SmoothingControllerProfile {
    SmoothingControllerType type = SmoothingControllerType::Linear;
    float speed = 0.0f;
    float acceleration = 0.0f;
    float pitchScale = 1.0f;
};

struct WeaponSpec {
    uint64_t heroId = 0;
    std::string_view heroName{};
    std::string_view weaponId{};
    std::string_view weaponName{};
    int action = 0;
    int order = 0;
    AimClass aimClass = AimClass::Unknown;
    PredictionSpec prediction{};
    FirePolicy firePolicy{};
    AimBehaviorType defaultBehavior = AimBehaviorType::Tracking;
    std::string_view sourceUrl{};
    std::string_view sourceNote{};
    float confidence = 0.0f;
};

struct BoneHitboxSpec {
    int boneId = 0;
    std::string_view boneName{};
    float radiusWorld = 0.0f;
    std::string_view shape{"sphere"};
    std::string_view sourceUrl{};
    std::string_view sourceNote{};
    float confidence = 0.0f;
};

struct HeroGeometrySpec {
    uint64_t heroId = 0;
    std::string_view heroName{};
    const BoneHitboxSpec* bones = nullptr;
    int boneCount = 0;
};

struct EntityMotionState {
    enum class Kind : int {
        Unknown,
        Grounded,
        AirborneRising,
        AirborneApex,
        AirborneFalling,
        Strafing,
        SuddenStop,
        TeleportOrInvalid,
    };

    Kind kind = Kind::Unknown;
    Vector3 worldVelocity{};
    Vector2 screenVelocity{};
    float verticalVelocity = 0.0f;
    float confidence = 0.0f;
};

struct TargetCandidate {
    bool valid = false;
    int entityIndex = -1;
    uint64_t entityKey = 0;
    c_entity entitySnapshot{};
    int boneId = 0;
    Vector3 rawAimPoint{};
    Vector3 predictedAimPoint{};
    Vector3 aimPoint{};
    Vector2 screenPoint{};
    float distance = 0.0f;
    float fovScore = (std::numeric_limits<float>::max)();
    float effectiveHitWindow = 0.0f;
    EntityMotionState motion{};
    TargetLockPolicy lockPolicy{};
    const WeaponSpec* weaponSpec = nullptr;
};

inline PredictionOverrideMode ClampPredictionOverride(int value)
{
    return static_cast<PredictionOverrideMode>(std::clamp(value, 0, 2));
}

inline AimBehaviorType ClampAimBehavior(int value)
{
    return static_cast<AimBehaviorType>(std::clamp(value, 0, 4));
}

inline FirePolicyType ClampFirePolicy(int value)
{
    return static_cast<FirePolicyType>(std::clamp(value, 0, 5));
}

inline TraceMode ClampTraceMode(int value)
{
    return static_cast<TraceMode>(std::clamp(value, 0, 2));
}

inline UnlockMode ClampUnlockMode(int value)
{
    return static_cast<UnlockMode>(std::clamp(value, 0, 2));
}

inline SmoothingControllerType ClampSmoothingController(int value)
{
    return static_cast<SmoothingControllerType>(std::clamp(value, 0, 5));
}

inline bool ResolvePredictionEnabled(PredictionOverrideMode mode,
                                     const WeaponSpec* weapon,
                                     bool legacyFallback)
{
    switch (mode) {
    case PredictionOverrideMode::ForceOn:
        return true;
    case PredictionOverrideMode::ForceOff:
        return false;
    case PredictionOverrideMode::Auto:
    default:
        return weapon ? weapon->prediction.enabledByDefault : legacyFallback;
    }
}

inline int LegacyAimModeFromBehavior(AimBehaviorType behavior)
{
    return behavior == AimBehaviorType::Tracking ? 0 : 1;
}

inline FirePolicyType DefaultFirePolicyForBehavior(AimBehaviorType behavior)
{
    switch (behavior) {
    case AimBehaviorType::Tracking:
        return FirePolicyType::HoldWhileTracking;
    case AimBehaviorType::FlickDelay:
    case AimBehaviorType::ReacquireAtApex:
        return FirePolicyType::ReleaseAfterDelay;
    case AimBehaviorType::Flick:
    case AimBehaviorType::FlickClamp:
    default:
        return FirePolicyType::TapOnHitWindow;
    }
}

} // namespace OW
