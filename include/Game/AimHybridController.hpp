#pragma once

#include <algorithm>
#include <cmath>

#include "Utils/Types.hpp"

namespace OW::AimHybrid {

inline constexpr float kDegToRad = M_PI_F / 180.0f;
inline constexpr float kRadToDeg = 180.0f / M_PI_F;

struct Tuning {
    float p = 0.45f;
    float i = 0.01f;
    float d = 0.04f;
    float maxIntegral = 10.0f;
    float deadzoneDeg = 0.10f;
    float constantSpeedDegPerSec = 45.0f;
    float maxSpeedDegPerSec = 720.0f;
    float accelerationDegPerSec2 = 1800.0f;
    float decelerationDegPerSec2 = 2600.0f;
    float nearRadiusDeg = 3.0f;
    float targetMotionGain = 0.35f;
    float suddenMotionBoost = 2.5f;
    float retargetResetDeg = 12.0f;
};

struct State {
    Vector3 integral{};
    Vector3 previousError{};
    Vector3 lastTarget{};
    Vector3 targetVelocity{};
    Vector3 outputVelocity{};
    bool initialized = false;
};

struct StepResult {
    Vector3 value{};
    float outputSpeedDegPerSec = 0.0f;
    float targetSpeedDegPerSec = 0.0f;
    float targetAccelerationDegPerSec2 = 0.0f;
    float accelerationLimitDegPerSec2 = 0.0f;
    bool reset = false;
};

inline float ClampFinite(float value, float low, float high, float fallback)
{
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

inline Vector3 ClampMagnitude(const Vector3& value, float limit)
{
    const float magnitude = value.Size();
    if (!std::isfinite(magnitude) || magnitude <= 0.000001f || limit <= 0.0f)
        return Vector3{};
    if (magnitude <= limit)
        return value;
    return value * (limit / magnitude);
}

inline Vector3 ClampIntegral(const Vector3& value, float maxIntegral)
{
    return Vector3(
        std::clamp(value.X, -maxIntegral, maxIntegral),
        std::clamp(value.Y, -maxIntegral, maxIntegral),
        std::clamp(value.Z, -maxIntegral, maxIntegral));
}

inline float ClampStepComponent(float step, float error)
{
    if (error > 0.0f)
        return std::clamp(step, 0.0f, error);
    if (error < 0.0f)
        return std::clamp(step, error, 0.0f);
    return 0.0f;
}

inline Tuning Sanitize(Tuning tuning)
{
    tuning.p = ClampFinite(tuning.p, 0.0f, 2.0f, 0.45f);
    tuning.i = ClampFinite(tuning.i, 0.0f, 0.5f, 0.01f);
    tuning.d = ClampFinite(tuning.d, 0.0f, 1.0f, 0.04f);
    tuning.maxIntegral = ClampFinite(tuning.maxIntegral, 0.0f, 50.0f, 10.0f);
    tuning.deadzoneDeg = ClampFinite(tuning.deadzoneDeg, 0.0f, 10.0f, 0.10f);
    tuning.constantSpeedDegPerSec = ClampFinite(
        tuning.constantSpeedDegPerSec, 0.0f, 3600.0f, 45.0f);
    tuning.maxSpeedDegPerSec = ClampFinite(
        tuning.maxSpeedDegPerSec, 1.0f, 3600.0f, 720.0f);
    tuning.accelerationDegPerSec2 = ClampFinite(
        tuning.accelerationDegPerSec2, 1.0f, 100000.0f, 1800.0f);
    tuning.decelerationDegPerSec2 = ClampFinite(
        tuning.decelerationDegPerSec2, 1.0f, 100000.0f, 2600.0f);
    tuning.nearRadiusDeg = ClampFinite(tuning.nearRadiusDeg, 0.05f, 30.0f, 3.0f);
    tuning.targetMotionGain = ClampFinite(tuning.targetMotionGain, 0.0f, 2.0f, 0.35f);
    tuning.suddenMotionBoost = ClampFinite(tuning.suddenMotionBoost, 1.0f, 8.0f, 2.5f);
    tuning.retargetResetDeg = ClampFinite(tuning.retargetResetDeg, 1.0f, 90.0f, 12.0f);
    return tuning;
}

inline StepResult Advance(Vector3 current,
                          Vector3 target,
                          float deltaTime,
                          const Tuning& rawTuning,
                          State& state)
{
    const Tuning tuning = Sanitize(rawTuning);
    const float dt = ClampFinite(deltaTime, 0.001f, 0.050f, 1.0f / 144.0f);
    const Vector3 error = target - current;
    const float errorMagnitude = error.Size();

    StepResult result{};
    result.value = current;
    if (!std::isfinite(errorMagnitude) || errorMagnitude <= 0.000001f) {
        state.outputVelocity = Vector3{};
        return result;
    }

    const bool retarget = state.initialized &&
        state.lastTarget.DistTo(target) * kRadToDeg > tuning.retargetResetDeg;
    if (!state.initialized || retarget) {
        state = State{};
        state.initialized = true;
        state.previousError = error;
        state.lastTarget = target;
        result.reset = true;
    }

    const float deadzone = tuning.deadzoneDeg * kDegToRad;
    if (errorMagnitude <= deadzone) {
        state.integral = Vector3{};
        state.previousError = error;
        state.lastTarget = target;
        state.outputVelocity = Vector3{};
        return result;
    }

    const Vector3 previousTargetVelocity = state.targetVelocity;
    state.targetVelocity = (target - state.lastTarget) / dt;
    const Vector3 targetAcceleration = (state.targetVelocity - previousTargetVelocity) / dt;
    result.targetSpeedDegPerSec = state.targetVelocity.Size() * kRadToDeg;
    result.targetAccelerationDegPerSec2 = targetAcceleration.Size() * kRadToDeg;

    state.integral = ClampIntegral(
        state.integral + error * dt,
        tuning.maxIntegral);
    const Vector3 derivative = (error - state.previousError) / dt;
    const Vector3 pidCommand =
        error * tuning.p +
        state.integral * tuning.i +
        derivative * tuning.d;

    const Vector3 direction = error / errorMagnitude;
    const float nearScale = std::clamp(
        errorMagnitude * kRadToDeg / tuning.nearRadiusDeg,
        0.0f,
        1.0f);
    const Vector3 constantVelocity = direction *
        (tuning.constantSpeedDegPerSec * nearScale * kDegToRad);
    Vector3 desiredVelocity =
        pidCommand / dt +
        constantVelocity +
        state.targetVelocity * tuning.targetMotionGain;

    const float maxSpeed = tuning.maxSpeedDegPerSec * kDegToRad;
    const float deceleration = tuning.decelerationDegPerSec2 * kDegToRad;
    const float targetFeedSpeed = state.targetVelocity.Size() * tuning.targetMotionGain;
    const float brakingSpeed = std::sqrt((std::max)(0.0f, 2.0f * deceleration * errorMagnitude));
    desiredVelocity = ClampMagnitude(
        desiredVelocity,
        (std::min)(maxSpeed, brakingSpeed + targetFeedSpeed));

    const float suddenScale = std::clamp(
        result.targetAccelerationDegPerSec2 / 720.0f,
        0.0f,
        1.0f);
    const float accelerationBoost =
        1.0f + (tuning.suddenMotionBoost - 1.0f) * suddenScale;
    const bool speedingUp = desiredVelocity.Size() > state.outputVelocity.Size() &&
        (desiredVelocity | state.outputVelocity) >= 0.0f;
    const float rateLimit = (speedingUp
        ? tuning.accelerationDegPerSec2 * accelerationBoost
        : tuning.decelerationDegPerSec2) * kDegToRad;
    result.accelerationLimitDegPerSec2 = rateLimit * kRadToDeg;

    const Vector3 velocityDelta = ClampMagnitude(
        desiredVelocity - state.outputVelocity,
        rateLimit * dt);
    state.outputVelocity = ClampMagnitude(state.outputVelocity + velocityDelta, maxSpeed);

    const Vector3 rawStep = ClampMagnitude(state.outputVelocity * dt, errorMagnitude);
    const Vector3 step(
        ClampStepComponent(rawStep.X, error.X),
        ClampStepComponent(rawStep.Y, error.Y),
        ClampStepComponent(rawStep.Z, error.Z));
    // Keep the state aligned with what was actually emitted. In particular,
    // a sudden target reversal must not retain a hidden velocity that moves
    // away from the new error on the next tick.
    state.outputVelocity = step / dt;
    result.value = current + step;
    result.outputSpeedDegPerSec = state.outputVelocity.Size() * kRadToDeg;

    state.previousError = error;
    state.lastTarget = target;
    return result;
}

} // namespace OW::AimHybrid
