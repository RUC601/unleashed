#include "Utils/Config.hpp"

#include <cmath>
#include <cstdlib>

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
    aimBehaviorPresetId = -1;
    aimBehavior = kAimBehaviorTracking;
    aimBehaviorMethod = { 0, 0, 0, 0 };
    aimBehaviorMethodPreset = { -1, -1, -1, -1 };
    aimBehaviorBaseSpeed = { 100.0f, 100.0f, 100.0f, 100.0f };
    aimBehaviorMoveSplitEnabled = { true, false, false, true };
    aimBehaviorMoveSplitMaxPixels = { 4, 50, 50, 4 };
    aimBehaviorMoveSplitDelayUs = { 800, 0, 0, 800 };
    aimConstantAngularSpeedDeg = 30.0f;
}

} // namespace

int main()
{
    using namespace OW::Config;

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

    return EXIT_SUCCESS;
}
