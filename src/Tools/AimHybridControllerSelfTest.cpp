#include <cmath>
#include <cstdio>

#include "Game/AimHybridController.hpp"

namespace {

int Fail(const char* message)
{
    std::fprintf(stderr, "AimHybridControllerSelfTest failed: %s\n", message);
    return 1;
}

OW::Vector3 Deg(float yaw)
{
    return OW::Vector3(0.0f, yaw * OW::AimHybrid::kDegToRad, 0.0f);
}

} // namespace

int main()
{
    constexpr float dt = 0.01f;
    OW::AimHybrid::Tuning tuning{};
    tuning.p = 0.12f;
    tuning.i = 0.0f;
    tuning.d = 0.0f;
    tuning.constantSpeedDegPerSec = 30.0f;
    tuning.maxSpeedDegPerSec = 500.0f;
    tuning.accelerationDegPerSec2 = 600.0f;
    tuning.decelerationDegPerSec2 = 1200.0f;
    tuning.targetMotionGain = 0.4f;

    OW::AimHybrid::State state{};
    OW::Vector3 current{};
    float firstSpeed = 0.0f;
    float laterSpeed = 0.0f;
    for (int i = 0; i < 30; ++i) {
        const auto step = OW::AimHybrid::Advance(current, Deg(20.0f), dt, tuning, state);
        current = step.value;
        if (i == 0) firstSpeed = step.outputSpeedDegPerSec;
        if (i == 15) laterSpeed = step.outputSpeedDegPerSec;
        if (current.Y > Deg(20.0f).Y + 0.000001f)
            return Fail("controller overshot a stationary target");
    }
    if (!(firstSpeed > 0.0f && laterSpeed > firstSpeed))
        return Fail("output did not ramp up under the acceleration limit");

    float previousError = std::fabs(Deg(20.0f).Y - current.Y);
    for (int i = 0; i < 300; ++i) {
        const auto step = OW::AimHybrid::Advance(current, Deg(20.0f), dt, tuning, state);
        current = step.value;
        const float error = std::fabs(Deg(20.0f).Y - current.Y);
        if (error > previousError + 0.000001f)
            return Fail("braking phase increased stationary-target error");
        previousError = error;
    }
    if (previousError * OW::AimHybrid::kRadToDeg > tuning.deadzoneDeg + 0.05f)
        return Fail("controller did not settle inside the deadzone");

    OW::AimHybrid::State calmState{};
    OW::AimHybrid::State boostedState{};
    OW::AimHybrid::Tuning calm = tuning;
    calm.suddenMotionBoost = 1.0f;
    OW::AimHybrid::Tuning boosted = tuning;
    boosted.suddenMotionBoost = 4.0f;
    OW::Vector3 calmCurrent{};
    OW::Vector3 boostedCurrent{};
    (void)OW::AimHybrid::Advance(calmCurrent, Deg(5.0f), dt, calm, calmState);
    (void)OW::AimHybrid::Advance(boostedCurrent, Deg(5.0f), dt, boosted, boostedState);
    const auto calmJump = OW::AimHybrid::Advance(calmCurrent, Deg(8.0f), dt, calm, calmState);
    const auto boostedJump = OW::AimHybrid::Advance(boostedCurrent, Deg(8.0f), dt, boosted, boostedState);
    if (!(boostedJump.accelerationLimitDegPerSec2 > calmJump.accelerationLimitDegPerSec2 &&
          boostedJump.outputSpeedDegPerSec > calmJump.outputSpeedDegPerSec)) {
        return Fail("sudden target motion did not increase the catch-up allowance");
    }

    OW::AimHybrid::State reversalState{};
    OW::Vector3 reversalCurrent{};
    for (int i = 0; i < 10; ++i)
        reversalCurrent = OW::AimHybrid::Advance(
            reversalCurrent, Deg(5.0f), dt, tuning, reversalState).value;
    const float reversalErrorBefore = std::fabs(Deg(-5.0f).Y - reversalCurrent.Y);
    const auto reversed = OW::AimHybrid::Advance(
        reversalCurrent, Deg(-5.0f), dt, tuning, reversalState);
    const float reversalErrorAfter = std::fabs(Deg(-5.0f).Y - reversed.value.Y);
    if (reversalErrorAfter > reversalErrorBefore + 0.000001f)
        return Fail("retained velocity moved away from a reversing target");

    std::puts("AimHybridControllerSelfTest PASS");
    return 0;
}
