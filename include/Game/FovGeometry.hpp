#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include "Utils/Types.hpp"

namespace OW::FovGeometry {

inline constexpr float kMaxFovAngleDeg = 180.0f;
inline constexpr float kFovComparisonEpsilon = 0.0001f;
inline constexpr float kRayForwardAlignmentEpsilon = 0.000001f;

struct RuntimeContext {
    Vector3 camera{};
    Vector3 forward{};
    bool valid = false;
};

inline bool IsFiniteVector(const Vector3& value)
{
    return std::isfinite(value.X) &&
           std::isfinite(value.Y) &&
           std::isfinite(value.Z);
}

inline Vector3 NormalizeVector(const Vector3& value)
{
    const float length = value.Size();
    if (!IsFiniteVector(value) || !std::isfinite(length) || length <= 0.0001f)
        return Vector3{};
    return value / length;
}

inline bool IsProjectionInverseUsable(float determinant, bool inverseFinite)
{
    return std::isfinite(determinant) && determinant != 0.0f && inverseFinite;
}

struct HomogeneousPoint {
    Vector3 xyz{};
    float w = (std::numeric_limits<float>::quiet_NaN)();
};

inline bool IsFiniteHomogeneousPoint(const HomogeneousPoint& value)
{
    return IsFiniteVector(value.xyz) && std::isfinite(value.w);
}

inline bool TryHomogeneousPoint(float x,
                                float y,
                                float z,
                                float w,
                                Vector3& outPoint)
{
    outPoint = Vector3{};
    if (!std::isfinite(x) ||
        !std::isfinite(y) ||
        !std::isfinite(z) ||
        !std::isfinite(w) ||
        w == 0.0f) {
        return false;
    }

    const Vector3 point(x / w, y / w, z / w);
    if (!IsFiniteVector(point))
        return false;

    outPoint = point;
    return true;
}

inline bool OrientDirectionToForward(const Vector3& rawDirection,
                                     const Vector3& expectedForward,
                                     Vector3& outDirection)
{
    outDirection = Vector3{};
    const Vector3 forward = NormalizeVector(expectedForward);
    Vector3 direction = NormalizeVector(rawDirection);
    if (forward == Vector3{} || direction == Vector3{})
        return false;

    const float alignment = direction | forward;
    if (!std::isfinite(alignment) || std::fabs(alignment) <= kRayForwardAlignmentEpsilon)
        return false;

    if (alignment < 0.0f)
        direction = direction * -1.0f;
    if (!IsFiniteVector(direction) || (direction | forward) <= kRayForwardAlignmentEpsilon)
        return false;

    outDirection = direction;
    return true;
}

inline bool OrientProjectionRay(const Vector3& depthZeroPoint,
                                const Vector3& depthOnePoint,
                                const Vector3& expectedForward,
                                Vector3& outDirection)
{
    return OrientDirectionToForward(
        depthOnePoint - depthZeroPoint,
        expectedForward,
        outDirection);
}

inline bool ResolveProjectionRay(const HomogeneousPoint& depthZero,
                                 const HomogeneousPoint& depthOne,
                                 const Vector3& expectedForward,
                                 Vector3& outDirection)
{
    outDirection = Vector3{};
    if (!IsFiniteHomogeneousPoint(depthZero) || !IsFiniteHomogeneousPoint(depthOne))
        return false;

    const bool depthZeroFinite = depthZero.w != 0.0f;
    const bool depthOneFinite = depthOne.w != 0.0f;
    if (depthZeroFinite && depthOneFinite) {
        Vector3 depthZeroPoint{};
        Vector3 depthOnePoint{};
        if (!TryHomogeneousPoint(
                depthZero.xyz.X,
                depthZero.xyz.Y,
                depthZero.xyz.Z,
                depthZero.w,
                depthZeroPoint) ||
            !TryHomogeneousPoint(
                depthOne.xyz.X,
                depthOne.xyz.Y,
                depthOne.xyz.Z,
                depthOne.w,
                depthOnePoint)) {
            return false;
        }
        return OrientProjectionRay(
            depthZeroPoint,
            depthOnePoint,
            expectedForward,
            outDirection);
    }

    // An infinite-far perspective projection legitimately produces one endpoint
    // with w=0. Its xyz components are the world-space direction at infinity.
    if (depthZeroFinite == depthOneFinite)
        return false;
    const HomogeneousPoint& directionAtInfinity = depthZeroFinite ? depthOne : depthZero;
    return OrientDirectionToForward(
        directionAtInfinity.xyz,
        expectedForward,
        outDirection);
}

inline float AngularSeparationDegFromCamera(const Vector3& camera,
                                            const Vector3& a,
                                            const Vector3& b)
{
    const Vector3 dirA = NormalizeVector(a - camera);
    const Vector3 dirB = NormalizeVector(b - camera);
    if (dirA == Vector3{} || dirB == Vector3{})
        return (std::numeric_limits<float>::max)();

    const float dot = std::clamp(dirA | dirB, -1.0f, 1.0f);
    return RAD2DEG(std::acos(dot));
}

inline float FovScoreDeg(const RuntimeContext& context, const Vector3& position)
{
    if (!context.valid)
        return (std::numeric_limits<float>::max)();

    const Vector3 targetDirection = NormalizeVector(position - context.camera);
    if (targetDirection == Vector3{})
        return (std::numeric_limits<float>::max)();

    const float dot = std::clamp(context.forward | targetDirection, -1.0f, 1.0f);
    return RAD2DEG(std::acos(dot));
}

inline bool IsWithinFovDeg(const RuntimeContext& context,
                           const Vector3& position,
                           float fovDeg,
                           float* outScoreDeg = nullptr)
{
    const float scoreDeg = FovScoreDeg(context, position);
    if (outScoreDeg)
        *outScoreDeg = scoreDeg;

    if (!std::isfinite(scoreDeg) || !std::isfinite(fovDeg))
        return false;
    const float limitDeg = std::clamp(fovDeg, 0.0f, kMaxFovAngleDeg);
    return scoreDeg <= limitDeg + kFovComparisonEpsilon;
}

struct CandidateFovEvaluation {
    float distance = 0.0f;
    float effectiveFovDeg = 0.0f;
    float scoreDeg = (std::numeric_limits<float>::max)();
};

struct PredictedEntryEvaluation {
    float distance = 0.0f;
    float effectiveFovDeg = 0.0f;
    float currentScoreDeg = (std::numeric_limits<float>::max)();
    float projectedScoreDeg = (std::numeric_limits<float>::max)();
    Vector3 acceptedAimPoint{};
    bool currentlyInside = false;
    bool acceptedByPrediction = false;
};

using ResolveFovForDistanceFn = float (*)(float fixedFovDeg, float distance);

inline bool EvaluateCandidateFov(const RuntimeContext& context,
                                 const Vector3& rawAimPoint,
                                 const Vector3& finalAimPoint,
                                 float fixedFovDeg,
                                 ResolveFovForDistanceFn resolveFov,
                                 CandidateFovEvaluation& outEvaluation)
{
    outEvaluation = CandidateFovEvaluation{};
    if (!context.valid ||
        !IsFiniteVector(rawAimPoint) ||
        !IsFiniteVector(finalAimPoint)) {
        return false;
    }

    outEvaluation.distance = context.camera.DistTo(rawAimPoint);
    if (!std::isfinite(outEvaluation.distance) || outEvaluation.distance <= 0.0001f)
        return false;

    outEvaluation.effectiveFovDeg = resolveFov
        ? resolveFov(fixedFovDeg, outEvaluation.distance)
        : std::clamp(fixedFovDeg, 0.0f, kMaxFovAngleDeg);
    return IsWithinFovDeg(
        context,
        finalAimPoint,
        outEvaluation.effectiveFovDeg,
        &outEvaluation.scoreDeg);
}

inline bool EvaluateCandidateFovWithPredictedEntry(
    const RuntimeContext& context,
    const Vector3& rawAimPoint,
    const Vector3& currentAimPoint,
    const Vector3& projectedAimPoint,
    float fixedFovDeg,
    ResolveFovForDistanceFn resolveFov,
    bool predictionEnabled,
    float maxOutsideDeg,
    PredictedEntryEvaluation& outEvaluation)
{
    outEvaluation = PredictedEntryEvaluation{};
    if (!context.valid ||
        !IsFiniteVector(rawAimPoint) ||
        !IsFiniteVector(currentAimPoint) ||
        !IsFiniteVector(projectedAimPoint)) {
        return false;
    }

    outEvaluation.distance = context.camera.DistTo(rawAimPoint);
    if (!std::isfinite(outEvaluation.distance) || outEvaluation.distance <= 0.0001f)
        return false;

    outEvaluation.effectiveFovDeg = resolveFov
        ? resolveFov(fixedFovDeg, outEvaluation.distance)
        : std::clamp(fixedFovDeg, 0.0f, kMaxFovAngleDeg);
    outEvaluation.currentlyInside = IsWithinFovDeg(
        context,
        currentAimPoint,
        outEvaluation.effectiveFovDeg,
        &outEvaluation.currentScoreDeg);
    if (outEvaluation.currentlyInside) {
        outEvaluation.projectedScoreDeg = outEvaluation.currentScoreDeg;
        outEvaluation.acceptedAimPoint = currentAimPoint;
        return true;
    }

    if (!predictionEnabled)
        return false;

    const float outsideMargin = std::clamp(
        std::isfinite(maxOutsideDeg) ? maxOutsideDeg : 0.0f,
        0.0f,
        30.0f);
    if (outEvaluation.currentScoreDeg >
        outEvaluation.effectiveFovDeg + outsideMargin + kFovComparisonEpsilon) {
        return false;
    }

    const bool projectedInside = IsWithinFovDeg(
        context,
        projectedAimPoint,
        outEvaluation.effectiveFovDeg,
        &outEvaluation.projectedScoreDeg);
    if (!projectedInside ||
        outEvaluation.projectedScoreDeg >= outEvaluation.currentScoreDeg - kFovComparisonEpsilon) {
        return false;
    }

    outEvaluation.acceptedAimPoint = projectedAimPoint;
    outEvaluation.acceptedByPrediction = true;
    return true;
}

enum class RingProjectionStatus {
    Drawable,
    OutsideViewport,
    Unavailable,
};

struct RingProjectionResult {
    RingProjectionStatus status = RingProjectionStatus::Unavailable;
    float radius = 0.0f;
    float centerAngleDeg = (std::numeric_limits<float>::quiet_NaN)();
    float edgeAngleDeg = (std::numeric_limits<float>::quiet_NaN)();

    bool IsDrawable() const
    {
        return status == RingProjectionStatus::Drawable &&
               std::isfinite(radius) &&
               radius > 0.0f;
    }
};

template <typename AngleSampler>
RingProjectionResult ResolveRingProjection(float targetAngleDeg,
                                           float maxRadius,
                                           AngleSampler&& sampleAngleDeg)
{
    RingProjectionResult result{};
    if (!std::isfinite(targetAngleDeg) ||
        !std::isfinite(maxRadius) ||
        targetAngleDeg <= 0.0f ||
        maxRadius <= 0.0f) {
        return result;
    }

    float lowAngleDeg = 0.0f;
    float highAngleDeg = 0.0f;
    if (!sampleAngleDeg(0.0f, lowAngleDeg) || !std::isfinite(lowAngleDeg))
        return result;
    result.centerAngleDeg = lowAngleDeg;

    if (!sampleAngleDeg(maxRadius, highAngleDeg) || !std::isfinite(highAngleDeg))
        return result;
    result.edgeAngleDeg = highAngleDeg;

    if (highAngleDeg <= lowAngleDeg + kFovComparisonEpsilon)
        return result;

    if (targetAngleDeg <= lowAngleDeg + kFovComparisonEpsilon)
        return result;
    if (targetAngleDeg >= highAngleDeg - kFovComparisonEpsilon) {
        result.status = RingProjectionStatus::OutsideViewport;
        return result;
    }

    float lowRadius = 0.0f;
    float highRadius = maxRadius;
    for (int iteration = 0; iteration < 24; ++iteration) {
        const float midRadius = (lowRadius + highRadius) * 0.5f;
        float midAngleDeg = 0.0f;
        if (!sampleAngleDeg(midRadius, midAngleDeg) ||
            !std::isfinite(midAngleDeg) ||
            midAngleDeg < lowAngleDeg - kFovComparisonEpsilon ||
            midAngleDeg > highAngleDeg + kFovComparisonEpsilon) {
            return result;
        }

        if (midAngleDeg < targetAngleDeg) {
            lowRadius = midRadius;
            lowAngleDeg = midAngleDeg;
        } else {
            highRadius = midRadius;
            highAngleDeg = midAngleDeg;
        }
    }

    result.status = RingProjectionStatus::Drawable;
    result.radius = highRadius;
    return result;
}

} // namespace OW::FovGeometry
