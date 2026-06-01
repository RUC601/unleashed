#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Utils/Types.hpp"

namespace OW::Motion {

enum class VelocitySource : int {
    None,
    Reported,
    WorldDeltaFallback,
};

struct EntityMotionEstimate {
    Vector3 reportedVelocity{};
    Vector3 worldDeltaVelocity{};
    Vector3 effectiveVelocity{};
    Vector3 reportedAcceleration{};
    float sampleSeconds = 0.0f;
    float reportedSpeed = 0.0f;
    float worldDeltaSpeed = 0.0f;
    float effectiveSpeed = 0.0f;
    float confidence = 0.0f;
    VelocitySource velocitySource = VelocitySource::None;
    bool hasReportedVelocity = false;
    bool hasWorldDeltaVelocity = false;
    bool usedWorldDeltaFallback = false;
    bool valid = false;
};

inline constexpr float kMinSampleSeconds = 0.004f;
inline constexpr float kMaxSampleSeconds = 0.250f;
inline constexpr float kMaxReasonableSpeed = 250.0f;
inline constexpr float kReportedStillSpeed = 0.75f;
inline constexpr float kWorldDeltaFallbackMinSpeed = 1.0f;

inline bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
}

inline bool IsNonZeroVector(const Vector3& value)
{
    return value != Vector3(0, 0, 0);
}

inline float VectorLength(const Vector3& value)
{
    return std::sqrt(value.X * value.X + value.Y * value.Y + value.Z * value.Z);
}

inline bool IsReasonableVelocity(const Vector3& velocity, float* speedOut = nullptr)
{
    if (!IsFiniteVector(velocity)) {
        if (speedOut) *speedOut = 0.0f;
        return false;
    }

    const float speed = VectorLength(velocity);
    if (speedOut) *speedOut = speed;
    return std::isfinite(speed) && speed <= kMaxReasonableSpeed;
}

template <typename EntityLike>
inline float SampleSeconds(const EntityLike& entity)
{
    if (!entity.has_previous_render_sample ||
        entity.previous_render_sample_tick_ms == 0 ||
        entity.render_sample_tick_ms == 0 ||
        entity.render_sample_tick_ms <= entity.previous_render_sample_tick_ms) {
        return 0.0f;
    }

    const float dt = static_cast<float>(
        entity.render_sample_tick_ms - entity.previous_render_sample_tick_ms) / 1000.0f;
    if (dt < kMinSampleSeconds || dt > kMaxSampleSeconds)
        return 0.0f;

    return dt;
}

template <typename EntityLike>
inline EntityMotionEstimate EstimateEntityMotion(const EntityLike& entity)
{
    EntityMotionEstimate estimate{};
    estimate.reportedVelocity = entity.velocity;
    estimate.sampleSeconds = SampleSeconds(entity);
    estimate.hasReportedVelocity =
        IsReasonableVelocity(estimate.reportedVelocity, &estimate.reportedSpeed);

    if (estimate.sampleSeconds > 0.0f &&
        IsFiniteVector(entity.pos) &&
        IsFiniteVector(entity.previous_pos) &&
        IsNonZeroVector(entity.pos) &&
        IsNonZeroVector(entity.previous_pos)) {
        estimate.worldDeltaVelocity = (entity.pos - entity.previous_pos) / estimate.sampleSeconds;
        estimate.hasWorldDeltaVelocity =
            IsReasonableVelocity(estimate.worldDeltaVelocity, &estimate.worldDeltaSpeed);

        float previousReportedSpeed = 0.0f;
        if (estimate.hasReportedVelocity &&
            IsReasonableVelocity(entity.previous_velocity, &previousReportedSpeed)) {
            estimate.reportedAcceleration =
                (estimate.reportedVelocity - entity.previous_velocity) / estimate.sampleSeconds;
        }
    }

    if (estimate.hasWorldDeltaVelocity &&
        (!estimate.hasReportedVelocity ||
         (estimate.reportedSpeed <= kReportedStillSpeed &&
          estimate.worldDeltaSpeed >= kWorldDeltaFallbackMinSpeed))) {
        estimate.effectiveVelocity = estimate.worldDeltaVelocity;
        estimate.effectiveSpeed = estimate.worldDeltaSpeed;
        estimate.velocitySource = VelocitySource::WorldDeltaFallback;
        estimate.usedWorldDeltaFallback = true;
        estimate.confidence = estimate.hasReportedVelocity ? 0.60f : 0.50f;
        estimate.valid = true;
        return estimate;
    }

    if (estimate.hasReportedVelocity) {
        estimate.effectiveVelocity = estimate.reportedVelocity;
        estimate.effectiveSpeed = estimate.reportedSpeed;
        estimate.velocitySource = VelocitySource::Reported;
        estimate.confidence = 0.70f;
        estimate.valid = true;
        return estimate;
    }

    estimate.confidence = 0.10f;
    return estimate;
}

} // namespace OW::Motion
