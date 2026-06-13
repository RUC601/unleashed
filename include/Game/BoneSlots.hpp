#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Game/Structs.hpp"

namespace OW::Plexies20260609 {

    enum class SkeletonSlot : std::size_t {
        Forehead = 0,
        Neck,
        Chest,
        Pelvis,
        UpperArmLeft,
        LowerArmLeft,
        HandLeft,
        UpperArmRight,
        LowerArmRight,
        HandRight,
        UpperLegLeft,
        LowerLegLeft,
        FootLeft,
        UpperLegRight,
        LowerLegRight,
        FootRight,
        Count,
    };

    inline constexpr std::size_t kSkeletonSlotCount =
        static_cast<std::size_t>(SkeletonSlot::Count);
    using SkeletonMap = std::array<int, kSkeletonSlotCount>;

    inline constexpr SkeletonMap kDefaultSkeletonMap{
        17, 16, 15, 2, 13, 14, 28, 54,
        55, 58, 85, 89, 90, 95, 99, 100,
    };

    inline constexpr SkeletonMap kBastionSkeletonMap{
        17, 16, 4, 3, 13, 14, 44, 54,
        55, 145, 89, 282, 90, 99, 283, 100,
    };

    inline constexpr SkeletonMap kDvaSkeletonMap{
        18, 16, 3, 2, 13, 14, 28, 54,
        55, 58, 85, 89, 282, 95, 99, 283,
    };

    inline constexpr SkeletonMap kShortHeroSkeletonMap{
        11, 16, 15, 2, 13, 14, 28, 54,
        55, 58, 85, 89, 90, 95, 99, 100,
    };

    inline constexpr SkeletonMap kCassidyTracerSkeletonMap{
        18, 16, 15, 2, 13, 14, 28, 54,
        55, 58, 85, 89, 90, 95, 99, 100,
    };

    inline constexpr SkeletonMap kTrainingBotSkeletonMap{
        105, 82, 82, 82, 80, 13, 386, 53,
        54, 387, 82, 82, 82, 82, 82, 82,
    };

    inline constexpr uint64_t kPlexiesTrainingBot4HeroId = GameData::MakeHeroId(0x16C);

    inline constexpr std::array<SkeletonSlot, 2> kUpperChain{
        SkeletonSlot::Neck,
        SkeletonSlot::Forehead,
    };

    inline constexpr std::array<SkeletonSlot, 3> kSpineChain{
        SkeletonSlot::Neck,
        SkeletonSlot::Chest,
        SkeletonSlot::Pelvis,
    };

    inline constexpr std::array<SkeletonSlot, 4> kRightArmChain{
        SkeletonSlot::Neck,
        SkeletonSlot::UpperArmRight,
        SkeletonSlot::LowerArmRight,
        SkeletonSlot::HandRight,
    };

    inline constexpr std::array<SkeletonSlot, 4> kLeftArmChain{
        SkeletonSlot::Neck,
        SkeletonSlot::UpperArmLeft,
        SkeletonSlot::LowerArmLeft,
        SkeletonSlot::HandLeft,
    };

    inline constexpr std::array<SkeletonSlot, 4> kLowerRightChain{
        SkeletonSlot::Pelvis,
        SkeletonSlot::UpperLegRight,
        SkeletonSlot::LowerLegRight,
        SkeletonSlot::FootRight,
    };

    inline constexpr std::array<SkeletonSlot, 4> kLowerLeftChain{
        SkeletonSlot::Pelvis,
        SkeletonSlot::UpperLegLeft,
        SkeletonSlot::LowerLegLeft,
        SkeletonSlot::FootLeft,
    };

    inline const SkeletonMap& ResolveSkeletonMap(uint64_t heroId)
    {
        switch (heroId) {
        case eHero::HERO_BASTION:
            return kBastionSkeletonMap;
        case eHero::HERO_DVA:
            return kDvaSkeletonMap;
        case eHero::HERO_WINSTON:
        case eHero::HERO_TORBJORN:
        case eHero::HERO_SYMMETRA:
            return kShortHeroSkeletonMap;
        case eHero::HERO_CASSIDY:
        case eHero::HERO_TRACER:
            return kCassidyTracerSkeletonMap;
        case eHero::HERO_TRAININGBOT1:
        case eHero::HERO_TRAININGBOT2:
        case eHero::HERO_TRAININGBOT3:
        case kPlexiesTrainingBot4HeroId:
            return kTrainingBotSkeletonMap;
        default:
            return kDefaultSkeletonMap;
        }
    }

} // namespace OW::Plexies20260609
