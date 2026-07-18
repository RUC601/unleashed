#pragma once

#include <algorithm>
#include <cmath>

namespace OW::AimSmoothingTiming {

inline constexpr float kReferenceDeltaTimeSeconds = 1.0f / 144.0f;

inline float NormalizePerTickFactor(float factor,
                                    float deltaTimeSeconds,
                                    float referenceDeltaTimeSeconds = kReferenceDeltaTimeSeconds) noexcept
{
    const float clampedFactor = std::isfinite(factor)
        ? std::clamp(factor, 0.0f, 1.0f)
        : 0.0f;
    if (clampedFactor <= 0.0f || clampedFactor >= 1.0f)
        return clampedFactor;

    const float referenceDelta =
        std::isfinite(referenceDeltaTimeSeconds) && referenceDeltaTimeSeconds > 0.0f
            ? referenceDeltaTimeSeconds
            : kReferenceDeltaTimeSeconds;
    const float delta = std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0f
        ? deltaTimeSeconds
        : referenceDelta;
    const float referenceSteps = delta / referenceDelta;
    return std::clamp(
        1.0f - std::pow(1.0f - clampedFactor, referenceSteps),
        0.0f,
        1.0f);
}

} // namespace OW::AimSmoothingTiming
