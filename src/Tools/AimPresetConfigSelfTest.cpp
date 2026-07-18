#include "Utils/Config.hpp"
#include "Utils/InputLabels.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int Fail()
{
    return EXIT_FAILURE;
}

void ResetAimPresetState()
{
    using namespace OW::Config;
    aimMethodPresets.clear();
    aimBehaviorPresets.clear();
    dynamicFovPresets.clear();
    aimBehaviorPresetId = -1;
    aimBehavior = kAimBehaviorTracking;
    aimbotFovMode = kFovModeFixed;
    aimbotDynamicFovPresetId = -1;
    autoscalefov = false;
    aimBehaviorMethod = { 0, 0, 0, 0, 0 };
    aimBehaviorMethodPreset = { -1, -1, -1, -1, -1 };
    aimBehaviorBaseSpeed = { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
    aimBehaviorStartLimiterProfiles = {};
    aimBehaviorMoveSplitEnabled = { true, false, false, true, true };
    aimBehaviorMoveSplitMaxPixels = { 4, 50, 50, 4, 4 };
    aimBehaviorMoveSplitDelayUs = { 800, 0, 0, 800, 800 };
    aimConstantAngularSpeedDeg = 30.0f;
}

bool TestActivationKeySelectionIsExact()
{
    using namespace OW::Labels;

    constexpr int kMouse4 = 2;
    constexpr int kMouse5 = 3;

    int matchedKey = -1;
    auto onlyMouse4Down = [](int vk) {
        return vk == VK_XBUTTON1;
    };
    if (!TryMatchAimActivationKey(kMouse4, onlyMouse4Down, &matchedKey) || matchedKey != kMouse4)
        return false;
    if (TryMatchAimActivationKey(kMouse5, onlyMouse4Down, &matchedKey))
        return false;

    auto onlyMouse5Down = [](int vk) {
        return vk == VK_XBUTTON2;
    };
    if (!TryMatchAimActivationKey(kMouse5, onlyMouse5Down, &matchedKey) || matchedKey != kMouse5)
        return false;
    if (TryMatchAimActivationKey(kMouse4, onlyMouse5Down, &matchedKey))
        return false;

    return true;
}

bool TestAimScopeRequirementPolicy()
{
    using namespace OW::Config;

    constexpr AimScopeRequirement all = AimScopeRequirement::All;
    constexpr AimScopeRequirement scopedOnly = AimScopeRequirement::ScopedOnly;
    if (!AimScopeRequirementMatches(all, false) ||
        !AimScopeRequirementMatches(all, true) ||
        AimScopeRequirementMatches(scopedOnly, false) ||
        !AimScopeRequirementMatches(scopedOnly, true)) {
        return false;
    }

    if (AimScopePressedSelectionPriority(scopedOnly, false, 3) != -1 ||
        AimScopePressedSelectionPriority(all, false, 3) != 1 ||
        AimScopePressedSelectionPriority(scopedOnly, true, 0) != 2 ||
        AimScopePressedSelectionPriority(scopedOnly, true, 3) != 3 ||
        AimScopePressedSelectionPriority(scopedOnly, true, 3) <=
            AimScopePressedSelectionPriority(scopedOnly, true, 0) ||
        AimScopePressedSelectionPriority(scopedOnly, true, 0) <=
            AimScopePressedSelectionPriority(all, true, 3)) {
        return false;
    }

    if (AimScopeFallbackSelectionPriority(scopedOnly, false) != -1 ||
        AimScopeFallbackSelectionPriority(all, true) != 0 ||
        AimScopeFallbackSelectionPriority(scopedOnly, true) != 1) {
        return false;
    }

    constexpr uint64_t widow = OW::GameData::MakeHeroId(0x00A);
    constexpr uint64_t ana = OW::GameData::MakeHeroId(0x13B);
    constexpr uint64_t ashe = OW::GameData::MakeHeroId(0x200);
    constexpr uint64_t freja = OW::GameData::MakeHeroId(0x32A);
    constexpr uint64_t emre = OW::GameData::MakeHeroId(0x4D8);
    for (const uint64_t heroId : { widow, ana, ashe, freja }) {
        if (!HeroUsesScopedWeaponActionSplit(heroId) ||
            DefaultAimScopeRequirementForHeroAction(heroId, 0) != all ||
            DefaultAimScopeRequirementForHeroAction(heroId, 2) != scopedOnly) {
            return false;
        }
    }
    if (HeroUsesScopedWeaponActionSplit(emre) ||
        DefaultAimScopeRequirementForHeroAction(emre, 2) != all) {
        return false;
    }

    HeroSlotPreset triggerSlot{};
    return triggerSlot.scopeRequirement == all;
}

bool TestTriggerReloadGatePolicy()
{
    using namespace OW::Config;

    if (ShouldBlockTriggerForReload(false, false) ||
        ShouldBlockTriggerForReload(false, true) ||
        ShouldBlockTriggerForReload(true, false) ||
        !ShouldBlockTriggerForReload(true, true)) {
        return false;
    }

    TriggerPreset preset{};
    if (preset.disableWhileReloading)
        return false;
    preset.disableWhileReloading = true;
    return ShouldBlockTriggerForReload(
        preset.disableWhileReloading,
        true);
}

bool TestDefaultAimPolicy()
{
    using namespace OW::Config;

    const HeroPreset preset{};
    return NearlyEqual(kDefaultFovDeg, 15.0f) &&
        NearlyEqual(preset.fov, 15.0f) &&
        NearlyEqual(preset.smooth, 20.0f) &&
        preset.firePolicy == 0 &&
        !preset.keepFiring &&
        !preset.autoshot &&
        !preset.requireActionHeld;
}

bool TestAimStartLimiterPresetResolution()
{
    using namespace OW::Config;

    ResetAimPresetState();
    const OW::AimStartLimiterProfile defaults =
        ResolveAimStartLimiterProfile(kAimBehaviorTracking);
    if (defaults.enabled ||
        !NearlyEqual(defaults.initialCapDegPerSec, 60.0f) ||
        !NearlyEqual(defaults.capRiseDegPerSec2, 1200.0f) ||
        !defaults.restartOnTargetChange) {
        return false;
    }

    OW::AimStartLimiterProfile& classDefault =
        aimBehaviorStartLimiterProfiles[static_cast<size_t>(kAimBehaviorTracking)];
    classDefault.enabled = true;
    classDefault.initialCapDegPerSec = 90.0f;
    classDefault.capRiseDegPerSec2 = 800.0f;
    classDefault.restartOnTargetChange = false;
    const OW::AimStartLimiterProfile resolvedDefault =
        ResolveAimStartLimiterProfile(kAimBehaviorTracking);
    if (!resolvedDefault.enabled ||
        !NearlyEqual(resolvedDefault.initialCapDegPerSec, 90.0f) ||
        resolvedDefault.restartOnTargetChange) {
        return false;
    }

    AimBehaviorPreset trackingPreset{};
    trackingPreset.id = 401;
    trackingPreset.behavior = kAimBehaviorTracking;
    trackingPreset.startLimiter.enabled = true;
    trackingPreset.startLimiter.initialCapDegPerSec = 45.0f;
    trackingPreset.startLimiter.capRiseDegPerSec2 = 600.0f;
    trackingPreset.startLimiter.restartOnTargetChange = true;
    aimBehaviorPresets.push_back(trackingPreset);
    aimBehaviorPresetId = trackingPreset.id;

    const OW::AimStartLimiterProfile resolvedPreset =
        ResolveAimStartLimiterProfile(kAimBehaviorTracking);
    if (!resolvedPreset.enabled ||
        !NearlyEqual(resolvedPreset.initialCapDegPerSec, 45.0f) ||
        !NearlyEqual(resolvedPreset.capRiseDegPerSec2, 600.0f) ||
        !resolvedPreset.restartOnTargetChange) {
        return false;
    }

    aimBehaviorPresets[0].startLimiter.initialCapDegPerSec =
        std::numeric_limits<float>::quiet_NaN();
    aimBehaviorPresets[0].startLimiter.capRiseDegPerSec2 =
        std::numeric_limits<float>::infinity();
    const OW::AimStartLimiterProfile validatedPreset =
        ResolveAimStartLimiterProfile(kAimBehaviorTracking);
    return NearlyEqual(
               validatedPreset.initialCapDegPerSec,
               OW::kAimStartLimiterDefaultInitialCapDegPerSec) &&
        NearlyEqual(
            validatedPreset.capRiseDegPerSec2,
            OW::kAimStartLimiterDefaultCapRiseDegPerSec2);
}

} // namespace

int main()
{
    using namespace OW::Config;

    ResetAimPresetState();

    if (!TestDefaultAimPolicy())
        return Fail();

    if (!TestAimStartLimiterPresetResolution())
        return Fail();
    ResetAimPresetState();

    AimMethodPreset constant1800{};
    constant1800.id = 101;
    constant1800.name = "Constant 1800";
    constant1800.method = 5;
    constant1800.angularSpeedScale = 100.0f;
    constant1800.constantAngularSpeedDeg = 1800.0f;
    aimMethodPresets.push_back(constant1800);

    AimBehaviorPreset flickFast{};
    flickFast.id = 201;
    flickFast.name = "Fast Flick";
    flickFast.behavior = kAimBehaviorFlick;
    flickFast.method = 5;
    flickFast.methodPresetId = constant1800.id;
    flickFast.baseSpeed = 80.0f;
    flickFast.moveSplitEnabled = false;
    flickFast.moveSplitMaxPixels = 50;
    flickFast.moveSplitDelayUs = 0;
    aimBehaviorPresets.push_back(flickFast);

    aimBehavior = kAimBehaviorFlick;
    aimBehaviorPresetId = flickFast.id;

    if (AimBehaviorMethod(kAimBehaviorFlick) != 5)
        return Fail();
    if (!NearlyEqual(AimBehaviorBaseSpeed(kAimBehaviorFlick), 80.0f))
        return Fail();
    if (AimBehaviorMoveSplitEnabled(kAimBehaviorFlick))
        return Fail();
    if (!NearlyEqual(RuntimeAimConstantAngularSpeedDeg(), 1800.0f))
        return Fail();
    if (!NearlyEqual(ClampAimConstantAngularSpeedDeg(4000.0f), kAimConstantAngularSpeedMaxDeg))
        return Fail();

    aimBehaviorPresetId = -1;
    aimBehaviorMethodPreset[static_cast<size_t>(kAimBehaviorFlick)] = constant1800.id;
    if (AimBehaviorMethod(kAimBehaviorFlick) != 5)
        return Fail();
    if (!NearlyEqual(RuntimeAimConstantAngularSpeedDeg(), 1800.0f))
        return Fail();

    aimBehaviorMethodPreset[static_cast<size_t>(kAimBehaviorFlick)] = -1;
    if (AimBehaviorMethod(kAimBehaviorFlick) != 0)
        return Fail();
    if (!NearlyEqual(RuntimeAimConstantAngularSpeedDeg(), 30.0f))
        return Fail();

    if (!IsMagneticTriggerBehavior(kAimBehaviorMagneticTrigger))
        return Fail();
    if (!UsesTrackingDeadzone(kAimBehaviorMagneticTrigger))
        return Fail();
    if (!UsesFlickFireControls(kAimBehaviorMagneticTrigger))
        return Fail();
    if (IsTrackingBehavior(kAimBehaviorMagneticTrigger))
        return Fail();
    if (IsFlickBehavior(kAimBehaviorMagneticTrigger))
        return Fail();
    if (!AimBehaviorMoveSplitEnabled(kAimBehaviorMagneticTrigger))
        return Fail();
    if (!TestActivationKeySelectionIsExact())
        return Fail();
    if (!TestAimScopeRequirementPolicy())
        return Fail();
    if (!TestTriggerReloadGatePolicy())
        return Fail();

    DynamicFovPreset dynamic{};
    dynamic.id = 301;
    dynamic.pointCount = 3;
    dynamic.smooth = false;
    dynamic.points[0] = { 0.0f, 180.0f };
    dynamic.points[1] = { 5.0f, 180.0f };
    dynamic.points[2] = { 30.0f, 5.0f };
    dynamicFovPresets.push_back(dynamic);

    HeroPreset heroPreset{};
    heroPreset.fov = 60.0f;
    heroPreset.fovMode = kFovModeDynamicPreset;
    heroPreset.dynamicFovPresetId = dynamic.id;
    if (!NearlyEqual(ResolveHeroPresetFovForDistance(heroPreset, 2.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(ResolveHeroPresetFovForDistance(heroPreset, 30.0f), 5.0f))
        return Fail();
    if (!NearlyEqual(ResolveRuntimeHeroPresetFovForDistance(heroPreset, 30.0f), 5.0f))
        return Fail();
    constexpr float kCandidateAngleDeg = 45.0f;
    if (kCandidateAngleDeg > ClampFovDeg(heroPreset.fov) ||
        kCandidateAngleDeg <= ResolveRuntimeHeroPresetFovForDistance(heroPreset, 30.0f)) {
        return Fail();
    }
    autoscalefov = true;
    if (!NearlyEqual(ResolveRuntimeHeroPresetFovForDistance(heroPreset, 30.0f), 60.0f) ||
        kCandidateAngleDeg > ResolveRuntimeHeroPresetFovForDistance(heroPreset, 30.0f)) {
        return Fail();
    }
    autoscalefov = false;
    heroPreset.dynamicFovPresetId = 9999;
    if (!NearlyEqual(ResolveHeroPresetFovForDistance(heroPreset, 30.0f), 60.0f))
        return Fail();
    if (!NearlyEqual(ResolveRuntimeHeroPresetFovForDistance(heroPreset, 30.0f), 60.0f))
        return Fail();

    return EXIT_SUCCESS;
}
