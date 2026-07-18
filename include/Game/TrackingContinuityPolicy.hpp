#pragma once

#include <algorithm>
#include <cstdint>

namespace OW::TrackingContinuityPolicy {

inline constexpr std::uint32_t kTargetDropoutGraceMs = 80;
inline constexpr std::uint32_t kMaxSampleAgeLeadMs = 12;
inline constexpr std::uint32_t kMaxFreshPositionSampleAgeMs = 48;

inline float DistanceExitMargin(float configuredMaxDistance) noexcept
{
    if (configuredMaxDistance <= 0.0f)
        return 0.0f;
    return std::clamp(configuredMaxDistance * 0.05f, 0.5f, 2.0f);
}

inline float EffectiveMaxDistance(float configuredMaxDistance,
                                  bool currentTarget) noexcept
{
    if (!currentTarget || configuredMaxDistance <= 0.0f)
        return configuredMaxDistance;
    return configuredMaxDistance + DistanceExitMargin(configuredMaxDistance);
}

inline float FovExitMargin(float configuredFovDeg) noexcept
{
    if (configuredFovDeg <= 0.0f)
        return 0.0f;
    return std::clamp(configuredFovDeg * 0.10f, 0.35f, 1.5f);
}

inline float EffectiveFovDeg(float configuredFovDeg, bool currentTarget) noexcept
{
    if (!currentTarget || configuredFovDeg <= 0.0f)
        return configuredFovDeg;
    return configuredFovDeg + FovExitMargin(configuredFovDeg);
}

inline float SampleAgeSeconds(std::uint32_t nowTick,
                              std::uint32_t sampleTick,
                              std::uint32_t maxAgeMs = 16) noexcept
{
    if (sampleTick == 0 || maxAgeMs == 0)
        return 0.0f;
    const std::uint32_t ageMs = nowTick - sampleTick;
    return static_cast<float>((std::min)(ageMs, maxAgeMs)) / 1000.0f;
}

inline bool IsWithinTargetDropoutGrace(std::uint32_t nowTick,
                                       std::uint32_t lastSeenTick,
                                       std::uint32_t graceMs =
                                           kTargetDropoutGraceMs) noexcept
{
    return lastSeenTick != 0 && graceMs != 0 &&
        nowTick - lastSeenTick <= graceMs;
}

inline float FreshPositionSampleLeadSeconds(
    std::uint32_t nowTick,
    std::uint32_t sampleTick,
    std::uint32_t maxLeadMs = kMaxSampleAgeLeadMs,
    std::uint32_t maxFreshAgeMs = kMaxFreshPositionSampleAgeMs) noexcept
{
    if (sampleTick == 0 || maxLeadMs == 0 || maxFreshAgeMs == 0)
        return 0.0f;
    const std::uint32_t ageMs = nowTick - sampleTick;
    if (ageMs >= maxFreshAgeMs)
        return 0.0f;
    if (ageMs <= maxLeadMs || maxFreshAgeMs <= maxLeadMs)
        return static_cast<float>((std::min)(ageMs, maxLeadMs)) / 1000.0f;

    const float fade = static_cast<float>(maxFreshAgeMs - ageMs) /
        static_cast<float>(maxFreshAgeMs - maxLeadMs);
    return static_cast<float>(maxLeadMs) * std::clamp(fade, 0.0f, 1.0f) /
        1000.0f;
}

inline std::uint64_t ControlTargetKey(std::uint64_t stableKey,
                                      std::uint64_t actorKey) noexcept
{
    if (actorKey == 0)
        return stableKey;
    std::uint64_t combined = stableKey;
    combined ^= actorKey + 0x9e3779b97f4a7c15ULL +
        (combined << 6) + (combined >> 2);
    return combined != 0 ? combined : actorKey;
}

} // namespace OW::TrackingContinuityPolicy
