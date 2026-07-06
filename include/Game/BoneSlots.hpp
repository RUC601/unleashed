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
    using RenderSkeletonMap = std::array<int, 18>;

    inline constexpr int kUnusedRenderSkeletonBone = -1;

    inline constexpr RenderSkeletonMap kRenderSkeletonHitboxBones{
        BONE_HEAD,
        BONE_NECK,
        BONE_CHEST,
        BONE_PELVIS,
        BONE_L_SHOULDER,
        BONE_R_SHOULDER,
        BONE_L_ELBOW,
        BONE_R_ELBOW,
        BONE_L_KNEE,
        BONE_R_KNEE,
        BONE_L_SHANK,
        BONE_R_SHANK,
        BONE_L_HAND,
        BONE_R_HAND,
        BONE_L_ANKLE,
        BONE_R_ANKLE,
        kUnusedRenderSkeletonBone,
        kUnusedRenderSkeletonBone,
    };

    inline constexpr int HitboxBoneIdForRenderSlot(std::size_t slot)
    {
        return slot < kRenderSkeletonHitboxBones.size()
            ? kRenderSkeletonHitboxBones[slot]
            : kUnusedRenderSkeletonBone;
    }

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

    inline constexpr RenderSkeletonMap ToRenderSkeletonMap(const SkeletonMap& map)
    {
        return {
            map[static_cast<std::size_t>(SkeletonSlot::Forehead)],
            map[static_cast<std::size_t>(SkeletonSlot::Neck)],
            map[static_cast<std::size_t>(SkeletonSlot::Chest)],
            map[static_cast<std::size_t>(SkeletonSlot::Pelvis)],
            map[static_cast<std::size_t>(SkeletonSlot::UpperArmLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::UpperArmRight)],
            map[static_cast<std::size_t>(SkeletonSlot::LowerArmLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::LowerArmRight)],
            map[static_cast<std::size_t>(SkeletonSlot::UpperLegLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::UpperLegRight)],
            map[static_cast<std::size_t>(SkeletonSlot::LowerLegLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::LowerLegRight)],
            map[static_cast<std::size_t>(SkeletonSlot::HandLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::HandRight)],
            map[static_cast<std::size_t>(SkeletonSlot::FootLeft)],
            map[static_cast<std::size_t>(SkeletonSlot::FootRight)],
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
        };
    }

    inline RenderSkeletonMap ResolveRenderSkeletonMap(uint64_t heroId)
    {
        return ToRenderSkeletonMap(ResolveSkeletonMap(heroId));
    }

    inline constexpr RenderSkeletonMap ToCoreRenderSkeletonMap(const SkeletonMap& map)
    {
        return {
            map[static_cast<std::size_t>(SkeletonSlot::Forehead)],
            map[static_cast<std::size_t>(SkeletonSlot::Neck)],
            map[static_cast<std::size_t>(SkeletonSlot::Chest)],
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
            kUnusedRenderSkeletonBone,
        };
    }

    inline RenderSkeletonMap ResolveCoreRenderSkeletonMap(uint64_t heroId)
    {
        return ToCoreRenderSkeletonMap(ResolveSkeletonMap(heroId));
    }

} // namespace OW::Plexies20260609
