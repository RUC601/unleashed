#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace OW {

    // Grouped skeleton masks are shared by Trigger and Aim Slot. The trigger
    // names remain as compatibility aliases because this model was introduced
    // for Trigger first.
    using TriggerBoneMask = std::uint32_t;

    inline constexpr TriggerBoneMask kTriggerBoneHead      = 1u << 0;
    inline constexpr TriggerBoneMask kTriggerBoneNeck      = 1u << 1;
    inline constexpr TriggerBoneMask kTriggerBoneChest     = 1u << 2;
    inline constexpr TriggerBoneMask kTriggerBonePelvis    = 1u << 3;
    inline constexpr TriggerBoneMask kTriggerBoneShoulders = 1u << 4;
    inline constexpr TriggerBoneMask kTriggerBoneElbows    = 1u << 5;
    inline constexpr TriggerBoneMask kTriggerBoneHands     = 1u << 6;
    inline constexpr TriggerBoneMask kTriggerBoneKnees     = 1u << 7;
    inline constexpr TriggerBoneMask kTriggerBoneShanks    = 1u << 8;
    inline constexpr TriggerBoneMask kTriggerBoneFeet      = 1u << 9;

    inline constexpr TriggerBoneMask kTriggerBoneAllMask =
        kTriggerBoneHead |
        kTriggerBoneNeck |
        kTriggerBoneChest |
        kTriggerBonePelvis |
        kTriggerBoneShoulders |
        kTriggerBoneElbows |
        kTriggerBoneHands |
        kTriggerBoneKnees |
        kTriggerBoneShanks |
        kTriggerBoneFeet;

    // Keep upgraded configurations conservative: the old default trigger point
    // was the configured (normally head) aim point.
    inline constexpr TriggerBoneMask kDefaultTriggerBoneMask = kTriggerBoneHead;

    struct TriggerBoneGroupDefinition {
        TriggerBoneMask mask = 0;
        const char* label = "";
    };

    inline constexpr std::array<TriggerBoneGroupDefinition, 10> kTriggerBoneGroups{{
        { kTriggerBoneHead, "Head" },
        { kTriggerBoneNeck, "Neck" },
        { kTriggerBoneChest, "Chest" },
        { kTriggerBonePelvis, "Pelvis" },
        { kTriggerBoneShoulders, "Shoulders" },
        { kTriggerBoneElbows, "Elbows" },
        { kTriggerBoneHands, "Hands" },
        { kTriggerBoneKnees, "Knees" },
        { kTriggerBoneShanks, "Shanks" },
        { kTriggerBoneFeet, "Feet" },
    }};

    // c_entity::skeleton_bones uses the 18-slot render layout. The last two
    // slots are intentionally unused.
    inline constexpr std::array<TriggerBoneMask, 18> kTriggerBoneGroupForRenderSlot{{
        kTriggerBoneHead,
        kTriggerBoneNeck,
        kTriggerBoneChest,
        kTriggerBonePelvis,
        kTriggerBoneShoulders,
        kTriggerBoneShoulders,
        kTriggerBoneElbows,
        kTriggerBoneElbows,
        kTriggerBoneKnees,
        kTriggerBoneKnees,
        kTriggerBoneShanks,
        kTriggerBoneShanks,
        kTriggerBoneHands,
        kTriggerBoneHands,
        kTriggerBoneFeet,
        kTriggerBoneFeet,
        0,
        0,
    }};

    inline constexpr TriggerBoneMask NormalizeTriggerBoneMask(TriggerBoneMask mask)
    {
        mask &= kTriggerBoneAllMask;
        return mask != 0 ? mask : kDefaultTriggerBoneMask;
    }

    inline constexpr bool IsAllTriggerBonesSelected(TriggerBoneMask mask)
    {
        return NormalizeTriggerBoneMask(mask) == kTriggerBoneAllMask;
    }

    inline constexpr bool TriggerBoneMaskIncludesGroup(TriggerBoneMask mask,
                                                        TriggerBoneMask group)
    {
        return (NormalizeTriggerBoneMask(mask) & group) != 0;
    }

    inline constexpr bool TriggerBoneMaskIncludesRenderSlot(TriggerBoneMask mask,
                                                             std::size_t slot)
    {
        return slot < kTriggerBoneGroupForRenderSlot.size() &&
            TriggerBoneMaskIncludesGroup(mask, kTriggerBoneGroupForRenderSlot[slot]);
    }

    // UI behavior: selecting a group while All is active starts a focused
    // selection; subsequent groups are freely combined. The last active group
    // cannot be removed, so persisted masks are never empty.
    inline constexpr TriggerBoneMask ToggleTriggerBoneGroup(TriggerBoneMask current,
                                                             TriggerBoneMask group)
    {
        current = NormalizeTriggerBoneMask(current);
        group &= kTriggerBoneAllMask;
        if (group == 0)
            return current;
        if (current == kTriggerBoneAllMask)
            return group;
        if ((current & group) == 0)
            return NormalizeTriggerBoneMask(current | group);

        const TriggerBoneMask reduced = current & ~group;
        return reduced != 0 ? reduced : current;
    }

    static_assert(NormalizeTriggerBoneMask(0) == kDefaultTriggerBoneMask);
    static_assert(TriggerBoneMaskIncludesRenderSlot(kTriggerBoneHead | kTriggerBoneFeet, 0));
    static_assert(TriggerBoneMaskIncludesRenderSlot(kTriggerBoneHead | kTriggerBoneFeet, 14));
    static_assert(!TriggerBoneMaskIncludesRenderSlot(kTriggerBoneHead | kTriggerBoneFeet, 2));

    using SkeletonBoneMask = TriggerBoneMask;
    using SkeletonBoneGroupDefinition = TriggerBoneGroupDefinition;

    inline constexpr SkeletonBoneMask kSkeletonBoneHead = kTriggerBoneHead;
    inline constexpr SkeletonBoneMask kSkeletonBoneNeck = kTriggerBoneNeck;
    inline constexpr SkeletonBoneMask kSkeletonBoneChest = kTriggerBoneChest;
    inline constexpr SkeletonBoneMask kSkeletonBonePelvis = kTriggerBonePelvis;
    inline constexpr SkeletonBoneMask kSkeletonBoneShoulders = kTriggerBoneShoulders;
    inline constexpr SkeletonBoneMask kSkeletonBoneElbows = kTriggerBoneElbows;
    inline constexpr SkeletonBoneMask kSkeletonBoneHands = kTriggerBoneHands;
    inline constexpr SkeletonBoneMask kSkeletonBoneKnees = kTriggerBoneKnees;
    inline constexpr SkeletonBoneMask kSkeletonBoneShanks = kTriggerBoneShanks;
    inline constexpr SkeletonBoneMask kSkeletonBoneFeet = kTriggerBoneFeet;
    inline constexpr SkeletonBoneMask kSkeletonBoneAllMask = kTriggerBoneAllMask;
    inline constexpr SkeletonBoneMask kDefaultAimBoneMask = kSkeletonBoneHead;
    inline constexpr const auto& kSkeletonBoneGroups = kTriggerBoneGroups;
    inline constexpr const auto& kSkeletonBoneGroupForRenderSlot = kTriggerBoneGroupForRenderSlot;

    inline constexpr SkeletonBoneMask NormalizeSkeletonBoneMask(SkeletonBoneMask mask)
    {
        return NormalizeTriggerBoneMask(mask);
    }

    inline constexpr bool IsAllSkeletonBonesSelected(SkeletonBoneMask mask)
    {
        return IsAllTriggerBonesSelected(mask);
    }

    inline constexpr bool SkeletonBoneMaskIncludesGroup(SkeletonBoneMask mask,
                                                         SkeletonBoneMask group)
    {
        return TriggerBoneMaskIncludesGroup(mask, group);
    }

    inline constexpr bool SkeletonBoneMaskIncludesRenderSlot(SkeletonBoneMask mask,
                                                              std::size_t slot)
    {
        return TriggerBoneMaskIncludesRenderSlot(mask, slot);
    }

    inline constexpr SkeletonBoneMask ToggleSkeletonBoneGroup(SkeletonBoneMask current,
                                                               SkeletonBoneMask group)
    {
        return ToggleTriggerBoneGroup(current, group);
    }

} // namespace OW
