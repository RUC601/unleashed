#include "Game/TriggerBoneSelection.hpp"

#include <cstdio>

int main()
{
    using namespace OW;

    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (condition)
            return;
        std::fprintf(stderr, "[FAIL] %s\n", message);
        ++failures;
    };

    expect(NormalizeTriggerBoneMask(0) == kTriggerBoneHead,
           "empty masks must use the conservative head-only default");
    expect(NormalizeTriggerBoneMask(0x80000000u | kTriggerBoneChest) == kTriggerBoneChest,
           "unknown mask bits must be discarded");
    expect(IsAllTriggerBonesSelected(kTriggerBoneAllMask),
           "the full mask must report All");

    const TriggerBoneMask headOnly = ToggleTriggerBoneGroup(kTriggerBoneAllMask, kTriggerBoneHead);
    expect(headOnly == kTriggerBoneHead,
           "choosing Head while All is active must produce Head only");

    const TriggerBoneMask headAndFeet = ToggleTriggerBoneGroup(headOnly, kTriggerBoneFeet);
    expect(headAndFeet == (kTriggerBoneHead | kTriggerBoneFeet),
           "Head and Feet must be freely combinable");
    expect(TriggerBoneMaskIncludesRenderSlot(headAndFeet, 0),
           "Head + Feet must include the head render slot");
    expect(TriggerBoneMaskIncludesRenderSlot(headAndFeet, 14) &&
           TriggerBoneMaskIncludesRenderSlot(headAndFeet, 15),
           "Feet must include both foot render slots");
    expect(!TriggerBoneMaskIncludesRenderSlot(headAndFeet, 1) &&
           !TriggerBoneMaskIncludesRenderSlot(headAndFeet, 2),
           "Head + Feet must not include neck or chest");
    expect(ToggleTriggerBoneGroup(headOnly, kTriggerBoneHead) == headOnly,
           "the last selected group must not be removable");

    const SkeletonBoneMask aimHeadAndFeet =
        ToggleSkeletonBoneGroup(kDefaultAimBoneMask, kSkeletonBoneFeet);
    expect(aimHeadAndFeet == (kSkeletonBoneHead | kSkeletonBoneFeet),
           "Aim and Trigger must share the same freely combinable skeleton mask");
    expect(SkeletonBoneMaskIncludesRenderSlot(aimHeadAndFeet, 0) &&
           SkeletonBoneMaskIncludesRenderSlot(aimHeadAndFeet, 15),
           "the shared Aim mask must resolve selected head and foot slots");

    if (failures != 0)
        return 1;

    std::printf("TriggerBoneSelectionSelfTest passed\n");
    return 0;
}
