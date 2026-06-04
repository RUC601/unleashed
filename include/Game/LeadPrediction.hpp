#pragma once

#include <algorithm>
#include <cmath>

#include "Utils/Types.hpp"

namespace OW {

struct ProjectileRuntimeSpec {
    float projectileSpeed = 0.0f;
    bool gravity = false;
    bool fromWeaponSpec = false;
};

struct LeadTimingEstimate {
    float estimatedSettleMs = 0.0f;
    int inputDelayMs = 0;
    float preFireDelayMs = 0.0f;
};

struct AimSettleEstimateInput {
    Vector3 localAngle{};
    Vector3 targetAngle{};
    int method = 0;
    float slotSpeedScale = 1.0f;
    float methodSpeedScale = 1.0f;
    float constantAngularSpeedDeg = 30.0f;
    float frameSeconds = 1.0f / 144.0f;
};

struct LeadPredictionResult {
    bool predictionEnabled = false;
    bool secondary = false;
    Vector3 rawAimPoint{};
    Vector3 preFireAimPoint{};
    Vector3 finalAimPoint{};
    Vector3 targetVelocity{};
    ProjectileRuntimeSpec projectile{};
    LeadTimingEstimate timing{};
    float distance = 0.0f;
};

inline constexpr float kLeadDefaultFrameSeconds = 1.0f / 144.0f;
inline constexpr float kLeadMinFrameSeconds = 1.0f / 1000.0f;
inline constexpr float kLeadMaxFrameSeconds = 0.1f;
inline constexpr float kLeadSettleEpsilonRad = 0.0005f;
inline constexpr float kLeadMaxSettleMs = 250.0f;
inline constexpr float kLeadMaxPreFireDelayMs = 300.0f;
inline constexpr float kLeadMaxTargetSpeed = 250.0f;
inline constexpr float kLeadRadToDeg = 57.29577951308232f;

inline bool IsFiniteLeadVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

inline Vector3 ClampVectorMagnitude(const Vector3& value, float maxLength)
{
    if (!IsFiniteLeadVector(value))
        return Vector3{};

    const float length = value.Size();
    if (!std::isfinite(length) || length <= maxLength || length <= 0.000001f)
        return value;

    return value * (maxLength / length);
}

inline float ClampLeadDelayMs(float value, float maxMs = kLeadMaxPreFireDelayMs)
{
    if (!std::isfinite(value) || value <= 0.0f)
        return 0.0f;
    return std::clamp(value, 0.0f, maxMs);
}

inline float ClampLeadFrameSeconds(float value)
{
    if (!std::isfinite(value) || value <= 0.0f)
        return kLeadDefaultFrameSeconds;
    return std::clamp(value, kLeadMinFrameSeconds, kLeadMaxFrameSeconds);
}

inline float EstimateAimSettleTimeMs(const AimSettleEstimateInput& input)
{
    const Vector3 error = input.targetAngle - input.localAngle;
    const float errorRad = error.Size();
    if (!std::isfinite(errorRad) || errorRad <= kLeadSettleEpsilonRad)
        return 0.0f;

    const int method = std::clamp(input.method, 0, 5);
    const float slotScale = std::isfinite(input.slotSpeedScale)
        ? std::clamp(input.slotSpeedScale, 0.0f, 2.0f)
        : 0.0f;
    const float methodScale = std::isfinite(input.methodSpeedScale)
        ? std::clamp(input.methodSpeedScale, 0.0f, 2.0f)
        : 0.0f;
    const float frameMs = ClampLeadFrameSeconds(input.frameSeconds) * 1000.0f;

    if (method == 5) {
        constexpr float kLeadMaxConstantAngularSpeedDeg = 3600.0f;
        const float baseDegPerSecond = std::isfinite(input.constantAngularSpeedDeg)
            ? std::clamp(input.constantAngularSpeedDeg, 0.0f, kLeadMaxConstantAngularSpeedDeg)
            : 30.0f;
        const float effectiveDegPerSecond = baseDegPerSecond * slotScale * methodScale;
        if (effectiveDegPerSecond <= 0.0001f)
            return kLeadMaxSettleMs;

        const float estimateMs = (errorRad * kLeadRadToDeg / effectiveDegPerSecond) * 1000.0f;
        return ClampLeadDelayMs(estimateMs, kLeadMaxSettleMs);
    }

    const float effectiveStep = std::clamp(slotScale * methodScale, 0.0f, 1.0f);
    if (effectiveStep <= 0.0001f)
        return kLeadMaxSettleMs;
    if (effectiveStep >= 0.9999f)
        return ClampLeadDelayMs(frameMs, kLeadMaxSettleMs);

    const float ratio = kLeadSettleEpsilonRad / errorRad;
    const float ticks = std::ceil(std::log(ratio) / std::log(1.0f - effectiveStep));
    const float estimateMs = (std::max)(1.0f, ticks) * frameMs;
    return ClampLeadDelayMs(estimateMs, kLeadMaxSettleMs);
}

inline LeadTimingEstimate BuildLeadTiming(float estimatedSettleMs, int inputDelayMs)
{
    LeadTimingEstimate timing{};
    timing.estimatedSettleMs = ClampLeadDelayMs(estimatedSettleMs, kLeadMaxSettleMs);
    timing.inputDelayMs = std::clamp(inputDelayMs, 0, 20);
    timing.preFireDelayMs = ClampLeadDelayMs(
        timing.estimatedSettleMs + static_cast<float>(timing.inputDelayMs),
        kLeadMaxPreFireDelayMs);
    return timing;
}

inline Vector3 ApplyTargetMotionPreFireDelay(const Vector3& rawAimPoint,
                                             const Vector3& targetVelocity,
                                             float preFireDelayMs,
                                             float maxPreFireDelayMs = kLeadMaxPreFireDelayMs)
{
    if (!IsFiniteLeadVector(rawAimPoint))
        return rawAimPoint;

    const float delaySeconds = ClampLeadDelayMs(preFireDelayMs, maxPreFireDelayMs) / 1000.0f;
    if (delaySeconds <= 0.0f)
        return rawAimPoint;

    const Vector3 safeVelocity = ClampVectorMagnitude(targetVelocity, kLeadMaxTargetSpeed);
    return rawAimPoint + safeVelocity * delaySeconds;
}

} // namespace OW
