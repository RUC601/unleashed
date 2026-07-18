#pragma once

#include <cstdint>
#include <string_view>

namespace OW::EntityRosterPolicy {

inline constexpr uint64_t kParticipantRosterPrefix = 0x4000000000000000ull;
inline constexpr uint64_t kRosterPayloadMask = 0x0FFFFFFFFFFFFFFFull;

inline uint64_t ParticipantRosterKey(std::string_view participantId) noexcept
{
    if (participantId.size() < 2 || participantId.size() > 63)
        return 0;

    bool hasVisibleByte = false;
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char value : participantId) {
        if (value == 0 || value < 0x20 || value == 0x7F)
            return 0;
        hasVisibleByte = true;
        hash ^= static_cast<uint64_t>(value);
        hash *= 1099511628211ull;
    }

    if (!hasVisibleByte)
        return 0;
    return kParticipantRosterPrefix | (hash & kRosterPayloadMask);
}

enum class SameParticipantObservationAction : uint8_t {
    ReplaceExisting,
    KeepExisting,
    UseActorKey,
};

enum class UltimateRosterFilter : uint8_t {
    All = 0,
    Enemy = 1,
    Ally = 2,
};

enum class RespawnRescanDue : uint8_t {
    None = 0,
    First,
    Retry,
};

inline SameParticipantObservationAction ResolveSameParticipantObservation(
    bool existingSeenThisCycle,
    bool sameActor,
    bool existingFresh,
    bool incomingFresh) noexcept
{
    if (!existingSeenThisCycle || sameActor)
        return SameParticipantObservationAction::ReplaceExisting;
    if (incomingFresh && !existingFresh)
        return SameParticipantObservationAction::ReplaceExisting;
    if (!incomingFresh && existingFresh)
        return SameParticipantObservationAction::KeepExisting;
    if (incomingFresh)
        return SameParticipantObservationAction::UseActorKey;
    return SameParticipantObservationAction::KeepExisting;
}

inline constexpr bool ShouldRenderWorldHeroAvatar(bool isEnemy) noexcept
{
    return isEnemy;
}

inline constexpr RespawnRescanDue ResolveRespawnRescanDue(
    bool dead,
    uint32_t watchStartedTick,
    bool firstScanRequested,
    uint32_t lastScanRequestTick,
    uint32_t nowTick,
    uint32_t firstDelayMs,
    uint32_t retryMs,
    uint32_t watchMs) noexcept
{
    if (!dead || watchStartedTick == 0)
        return RespawnRescanDue::None;
    const uint32_t deadAge = nowTick - watchStartedTick;
    if (deadAge < firstDelayMs || deadAge > watchMs)
        return RespawnRescanDue::None;
    if (!firstScanRequested)
        return RespawnRescanDue::First;
    if (lastScanRequestTick == 0 ||
        nowTick - lastScanRequestTick >= retryMs) {
        return RespawnRescanDue::Retry;
    }
    return RespawnRescanDue::None;
}

inline constexpr bool ShouldRenderUltimateRosterEntry(int filter,
                                                       bool isEnemy) noexcept
{
    switch (static_cast<UltimateRosterFilter>(filter)) {
    case UltimateRosterFilter::All:
        return true;
    case UltimateRosterFilter::Ally:
        return !isEnemy;
    case UltimateRosterFilter::Enemy:
    default:
        return isEnemy;
    }
}

} // namespace OW::EntityRosterPolicy
