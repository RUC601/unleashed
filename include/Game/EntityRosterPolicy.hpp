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

inline constexpr bool ShouldRenderHeroAvatar(bool isEnemy) noexcept
{
    return isEnemy;
}

} // namespace OW::EntityRosterPolicy
