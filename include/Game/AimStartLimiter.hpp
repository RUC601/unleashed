#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

#include "Utils/Types.hpp"

namespace OW {

inline constexpr float kAimStartLimiterDefaultInitialCapDegPerSec = 60.0f;
inline constexpr float kAimStartLimiterDefaultCapRiseDegPerSec2 = 1200.0f;
inline constexpr float kAimStartLimiterMaxCapDegPerSec = 3600.0f;
inline constexpr float kAimStartLimiterMaxCapRiseDegPerSec2 = 100000.0f;
inline constexpr float kAimStartLimiterDefaultDeltaTimeSeconds = 1.0f / 144.0f;
inline constexpr float kAimStartLimiterMinDeltaTimeSeconds = 0.001f;
inline constexpr float kAimStartLimiterMaxDeltaTimeSeconds = 0.050f;

struct AimStartLimiterProfile {
    bool enabled = false;
    float initialCapDegPerSec = kAimStartLimiterDefaultInitialCapDegPerSec;
    float capRiseDegPerSec2 = kAimStartLimiterDefaultCapRiseDegPerSec2;
    bool restartOnTargetChange = true;
};

struct AimStartLimiterState {
    bool started = false;
    std::uint64_t sessionGeneration = 0;
    std::uint64_t connectionEpoch = 0;
    std::uint64_t targetKey = 0;
    float currentCapDegPerSec = 0.0f;
    std::chrono::steady_clock::time_point lastLoopAt{};
};

enum class AimStartLimiterResetReason : std::uint8_t {
    None = 0,
    SessionStarted,
    SessionChanged,
    ConnectionChanged,
    TargetChanged,
    Disabled,
};

struct AimStartLimiterResult {
    Vector3 outputAngle{};
    float requestedSpeedDegPerSec = 0.0f;
    float appliedSpeedDegPerSec = 0.0f;
    float capDegPerSec = 0.0f;
    bool limited = false;
    AimStartLimiterResetReason resetReason = AimStartLimiterResetReason::None;
};

inline AimStartLimiterProfile ValidateAimStartLimiterProfile(
    AimStartLimiterProfile profile)
{
    profile.initialCapDegPerSec = std::isfinite(profile.initialCapDegPerSec)
        ? std::clamp(profile.initialCapDegPerSec, 0.0f, kAimStartLimiterMaxCapDegPerSec)
        : kAimStartLimiterDefaultInitialCapDegPerSec;
    profile.capRiseDegPerSec2 = std::isfinite(profile.capRiseDegPerSec2)
        ? std::clamp(profile.capRiseDegPerSec2, 0.0f, kAimStartLimiterMaxCapRiseDegPerSec2)
        : kAimStartLimiterDefaultCapRiseDegPerSec2;
    return profile;
}

inline float ClampAimStartLimiterDeltaTime(float deltaTimeSeconds)
{
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0f)
        return kAimStartLimiterDefaultDeltaTimeSeconds;
    return std::clamp(
        deltaTimeSeconds,
        kAimStartLimiterMinDeltaTimeSeconds,
        kAimStartLimiterMaxDeltaTimeSeconds);
}

inline void ResetAimStartLimiter(AimStartLimiterState& state)
{
    state = AimStartLimiterState{};
}

inline float ObserveAimStartLimiterLoop(
    AimStartLimiterState& state,
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now())
{
    float deltaTimeSeconds = kAimStartLimiterDefaultDeltaTimeSeconds;
    if (state.lastLoopAt.time_since_epoch().count() != 0 && now > state.lastLoopAt) {
        deltaTimeSeconds = std::chrono::duration<float>(now - state.lastLoopAt).count();
    }
    state.lastLoopAt = now;
    return ClampAimStartLimiterDeltaTime(deltaTimeSeconds);
}

inline const char* AimStartLimiterResetReasonName(AimStartLimiterResetReason reason)
{
    switch (reason) {
    case AimStartLimiterResetReason::SessionStarted: return "session_started";
    case AimStartLimiterResetReason::SessionChanged: return "session_changed";
    case AimStartLimiterResetReason::ConnectionChanged: return "connection_changed";
    case AimStartLimiterResetReason::TargetChanged: return "target_changed";
    case AimStartLimiterResetReason::Disabled: return "disabled";
    case AimStartLimiterResetReason::None:
    default:
        return "none";
    }
}

inline AimStartLimiterResult ApplyAimStartLimiter(
    const AimStartLimiterProfile& rawProfile,
    AimStartLimiterState& state,
    std::uint64_t sessionGeneration,
    std::uint64_t connectionEpoch,
    std::uint64_t targetKey,
    const Vector3& currentAngle,
    const Vector3& requestedAngle,
    float deltaTimeSeconds)
{
    const AimStartLimiterProfile profile = ValidateAimStartLimiterProfile(rawProfile);
    AimStartLimiterResult result{};
    result.outputAngle = requestedAngle;
    result.capDegPerSec = profile.initialCapDegPerSec;

    const bool finiteAngles =
        std::isfinite(currentAngle.X) && std::isfinite(currentAngle.Y) &&
        std::isfinite(requestedAngle.X) && std::isfinite(requestedAngle.Y);
    const float deltaTime = ClampAimStartLimiterDeltaTime(deltaTimeSeconds);
    const float pitchDelta = finiteAngles ? requestedAngle.X - currentAngle.X : 0.0f;
    const float yawDelta = finiteAngles ? requestedAngle.Y - currentAngle.Y : 0.0f;
    const float requestedRadians = finiteAngles
        ? std::sqrt(pitchDelta * pitchDelta + yawDelta * yawDelta)
        : 0.0f;

    if (!profile.enabled) {
        const bool wasStarted = state.started;
        ResetAimStartLimiter(state);
        if (wasStarted)
            result.resetReason = AimStartLimiterResetReason::Disabled;
        if (std::isfinite(requestedRadians) && requestedRadians > 0.000001f) {
            result.requestedSpeedDegPerSec = RAD2DEG(requestedRadians) / deltaTime;
            result.appliedSpeedDegPerSec = result.requestedSpeedDegPerSec;
        }
        return result;
    }

    if (!finiteAngles)
        return result;

    if (!std::isfinite(requestedRadians) || requestedRadians <= 0.000001f)
        return result;

    AimStartLimiterResetReason resetReason = AimStartLimiterResetReason::None;
    if (!state.started) {
        resetReason = AimStartLimiterResetReason::SessionStarted;
    } else if (state.sessionGeneration != sessionGeneration) {
        resetReason = AimStartLimiterResetReason::SessionChanged;
    } else if (state.connectionEpoch != connectionEpoch) {
        resetReason = AimStartLimiterResetReason::ConnectionChanged;
    } else if (profile.restartOnTargetChange &&
               targetKey != 0 && state.targetKey != 0 &&
               state.targetKey != targetKey) {
        resetReason = AimStartLimiterResetReason::TargetChanged;
    }

    if (resetReason != AimStartLimiterResetReason::None) {
        state.started = true;
        state.currentCapDegPerSec = profile.initialCapDegPerSec;
    }
    state.sessionGeneration = sessionGeneration;
    state.connectionEpoch = connectionEpoch;
    if (targetKey != 0)
        state.targetKey = targetKey;

    state.currentCapDegPerSec = std::isfinite(state.currentCapDegPerSec)
        ? std::clamp(state.currentCapDegPerSec, 0.0f, kAimStartLimiterMaxCapDegPerSec)
        : profile.initialCapDegPerSec;
    result.capDegPerSec = state.currentCapDegPerSec;
    result.resetReason = resetReason;

    result.requestedSpeedDegPerSec = RAD2DEG(requestedRadians) / deltaTime;
    float scale = 1.0f;
    if (result.requestedSpeedDegPerSec > result.capDegPerSec) {
        scale = result.capDegPerSec > 0.0f
            ? result.capDegPerSec / result.requestedSpeedDegPerSec
            : 0.0f;
        result.limited = true;
    }
    result.outputAngle = Vector3(
        currentAngle.X + pitchDelta * scale,
        currentAngle.Y + yawDelta * scale,
        requestedAngle.Z);
    result.appliedSpeedDegPerSec = result.requestedSpeedDegPerSec * scale;

    state.currentCapDegPerSec = std::clamp(
        state.currentCapDegPerSec + profile.capRiseDegPerSec2 * deltaTime,
        0.0f,
        kAimStartLimiterMaxCapDegPerSec);
    return result;
}

} // namespace OW
