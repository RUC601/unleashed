#include <cstdio>

#include "Game/AimFirePhase.hpp"

namespace {
int Fail(const char* message)
{
    std::fprintf(stderr, "AimFirePhaseSelfTest failed: %s\n", message);
    return 1;
}
}

int main()
{
    OW::AimFirePhase::Tuning tuning{};
    tuning.shotIntervalMs = 200.0f;
    tuning.pauseMs = 40.0f;
    tuning.recoveryMs = 80.0f;
    tuning.pauseYawScale = 0.4f;
    tuning.pausePitchScale = 0.0f;
    tuning.preFireBoostWindowMs = 50.0f;
    tuning.preFireBoostScale = 1.5f;

    const auto pause = OW::AimFirePhase::Resolve(20.0f, true, tuning);
    if (!pause.paused || pause.pitchScale != 0.0f || pause.yawScale != 0.4f || pause.readyToFire)
        return Fail("post-fire pause did not preserve the configured axis policy");

    const auto recovery = OW::AimFirePhase::Resolve(80.0f, true, tuning);
    if (!recovery.recovering || recovery.pitchScale <= 0.0f || recovery.pitchScale >= 1.0f)
        return Fail("recovery did not ramp the paused axis");

    const auto boost = OW::AimFirePhase::Resolve(175.0f, true, tuning);
    if (!boost.preFireBoost || boost.trackingScale <= 1.0f || boost.readyToFire)
        return Fail("pre-fire window did not increase tracking gain");

    const auto ready = OW::AimFirePhase::Resolve(200.0f, true, tuning);
    if (!ready.readyToFire)
        return Fail("shot interval did not re-arm firing");

    tuning.shotIntervalMs = 3220.0f;
    if (OW::AimFirePhase::Resolve(3219.0f, true, tuning).readyToFire ||
        !OW::AimFirePhase::Resolve(3220.0f, true, tuning).readyToFire) {
        return Fail("long charged-volley cadence was truncated below 3220 ms");
    }
    tuning.shotIntervalMs = 200.0f;

    const auto initial = OW::AimFirePhase::Resolve(0.0f, false, tuning);
    if (!initial.readyToFire || initial.paused || initial.recovering)
        return Fail("unlatched state should be immediately ready");

    const auto irregularPause = OW::AimFirePhase::ResolveTimeline(
        20.0f, 210.0f, true, tuning);
    if (!irregularPause.paused || irregularPause.pitchScale != 0.0f)
        return Fail("irregular timeline did not apply post-fire axis pause");

    const auto irregularBoost = OW::AimFirePhase::ResolveTimeline(
        180.0f, 25.0f, true, tuning);
    if (!irregularBoost.preFireBoost || irregularBoost.trackingScale <= 1.0f)
        return Fail("irregular timeline did not use the measured next-shot window");

    const auto firstShotBoost = OW::AimFirePhase::ResolveTimeline(
        0.0f, 25.0f, false, tuning);
    if (!firstShotBoost.preFireBoost || firstShotBoost.trackingScale <= 1.0f ||
        firstShotBoost.paused || firstShotBoost.recovering) {
        return Fail("first-shot timeline did not pre-accelerate without a prior shot");
    }

    std::puts("AimFirePhaseSelfTest PASS");
    return 0;
}
