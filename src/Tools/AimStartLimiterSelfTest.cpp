#include "Game/AimStartLimiter.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool SameAngle(const OW::Vector3& lhs, const OW::Vector3& rhs)
{
    return NearlyEqual(lhs.X, rhs.X) &&
        NearlyEqual(lhs.Y, rhs.Y) &&
        NearlyEqual(lhs.Z, rhs.Z);
}

OW::Vector3 YawDegrees(float degrees)
{
    return OW::Vector3(0.0f, DEG2RAD(degrees), 0.0f);
}

bool TestDisabledPassthroughAndReset()
{
    OW::AimStartLimiterProfile profile{};
    OW::AimStartLimiterState state{};
    state.started = true;
    state.currentCapDegPerSec = 200.0f;
    const OW::Vector3 requested = YawDegrees(12.0f);
    const auto result = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 10, OW::Vector3{}, requested, 0.01f);
    return SameAngle(result.outputAngle, requested) &&
        result.resetReason == OW::AimStartLimiterResetReason::Disabled &&
        !state.started;
}

bool TestFirstFrameAndRamp()
{
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    profile.initialCapDegPerSec = 60.0f;
    profile.capRiseDegPerSec2 = 1200.0f;
    OW::AimStartLimiterState state{};

    const auto first = OW::ApplyAimStartLimiter(
        profile, state, 1, 7, 10, OW::Vector3{}, YawDegrees(10.0f), 0.01f);
    if (!first.limited || !NearlyEqual(first.capDegPerSec, 60.0f) ||
        !NearlyEqual(first.appliedSpeedDegPerSec, 60.0f) ||
        first.resetReason != OW::AimStartLimiterResetReason::SessionStarted) {
        return false;
    }

    const auto second = OW::ApplyAimStartLimiter(
        profile, state, 1, 7, 10, OW::Vector3{}, YawDegrees(10.0f), 0.01f);
    return second.limited &&
        NearlyEqual(second.capDegPerSec, 72.0f) &&
        NearlyEqual(second.appliedSpeedDegPerSec, 72.0f) &&
        state.currentCapDegPerSec > second.capDegPerSec;
}

bool TestSlowRequestIsNotAccelerated()
{
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    profile.initialCapDegPerSec = 100.0f;
    OW::AimStartLimiterState state{};
    const OW::Vector3 requested = YawDegrees(0.5f);
    const auto result = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 1, OW::Vector3{}, requested, 0.01f);
    return !result.limited && SameAngle(result.outputAngle, requested) &&
        NearlyEqual(result.requestedSpeedDegPerSec, 50.0f) &&
        NearlyEqual(result.appliedSpeedDegPerSec, 50.0f);
}

bool TestSessionConnectionAndTargetResets()
{
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    OW::AimStartLimiterState state{};
    const OW::Vector3 requested = YawDegrees(10.0f);

    OW::ApplyAimStartLimiter(profile, state, 1, 1, 10, {}, requested, 0.01f);
    OW::ApplyAimStartLimiter(profile, state, 1, 1, 10, {}, requested, 0.01f);
    const auto target = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 11, {}, requested, 0.01f);
    if (target.resetReason != OW::AimStartLimiterResetReason::TargetChanged ||
        !NearlyEqual(target.capDegPerSec, profile.initialCapDegPerSec)) {
        return false;
    }

    const auto connection = OW::ApplyAimStartLimiter(
        profile, state, 1, 2, 11, {}, requested, 0.01f);
    if (connection.resetReason != OW::AimStartLimiterResetReason::ConnectionChanged ||
        !NearlyEqual(connection.capDegPerSec, profile.initialCapDegPerSec)) {
        return false;
    }

    const auto session = OW::ApplyAimStartLimiter(
        profile, state, 2, 2, 11, {}, requested, 0.01f);
    return session.resetReason == OW::AimStartLimiterResetReason::SessionChanged &&
        NearlyEqual(session.capDegPerSec, profile.initialCapDegPerSec);
}

bool TestTargetRestartCanBeDisabled()
{
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    profile.restartOnTargetChange = false;
    OW::AimStartLimiterState state{};
    const OW::Vector3 requested = YawDegrees(10.0f);
    OW::ApplyAimStartLimiter(profile, state, 1, 1, 10, {}, requested, 0.01f);
    const auto second = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 11, {}, requested, 0.01f);
    return second.resetReason == OW::AimStartLimiterResetReason::None &&
        second.capDegPerSec > profile.initialCapDegPerSec &&
        state.targetKey == 11;
}

bool TestPausedLoopsDoNotAdvanceCap()
{
    using Clock = std::chrono::steady_clock;
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    OW::AimStartLimiterState state{};
    const OW::Vector3 requested = YawDegrees(10.0f);
    const Clock::time_point start{};

    OW::ObserveAimStartLimiterLoop(state, start + std::chrono::milliseconds(1));
    OW::ApplyAimStartLimiter(profile, state, 1, 1, 10, {}, requested, 0.01f);
    const float capAfterFirst = state.currentCapDegPerSec;

    OW::ObserveAimStartLimiterLoop(state, start + std::chrono::seconds(1));
    OW::ObserveAimStartLimiterLoop(state, start + std::chrono::seconds(2));
    if (!NearlyEqual(state.currentCapDegPerSec, capAfterFirst))
        return false;

    const float nextDt = OW::ObserveAimStartLimiterLoop(
        state, start + std::chrono::seconds(2) + std::chrono::milliseconds(10));
    const auto resumed = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 10, {}, requested, nextDt);
    return NearlyEqual(nextDt, 0.01f) &&
        NearlyEqual(resumed.capDegPerSec, capAfterFirst);
}

bool TestValidationAndDeltaTimeBounds()
{
    OW::AimStartLimiterProfile profile{};
    profile.initialCapDegPerSec = std::numeric_limits<float>::quiet_NaN();
    profile.capRiseDegPerSec2 = std::numeric_limits<float>::infinity();
    const auto validated = OW::ValidateAimStartLimiterProfile(profile);
    return NearlyEqual(
               validated.initialCapDegPerSec,
               OW::kAimStartLimiterDefaultInitialCapDegPerSec) &&
        NearlyEqual(
            validated.capRiseDegPerSec2,
            OW::kAimStartLimiterDefaultCapRiseDegPerSec2) &&
        NearlyEqual(
            OW::ClampAimStartLimiterDeltaTime(10.0f),
            OW::kAimStartLimiterMaxDeltaTimeSeconds) &&
        NearlyEqual(
            OW::ClampAimStartLimiterDeltaTime(0.0f),
            OW::kAimStartLimiterDefaultDeltaTimeSeconds);
}

bool TestZeroAndNonFiniteAnglesDoNotStartRamp()
{
    OW::AimStartLimiterProfile profile{};
    profile.enabled = true;
    OW::AimStartLimiterState state{};

    const auto zero = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 10, {}, {}, 0.01f);
    if (!SameAngle(zero.outputAngle, {}) || state.started)
        return false;

    const OW::Vector3 invalid(
        0.0f,
        std::numeric_limits<float>::quiet_NaN(),
        0.0f);
    const auto nonFinite = OW::ApplyAimStartLimiter(
        profile, state, 1, 1, 10, {}, invalid, 0.01f);
    return std::isnan(nonFinite.outputAngle.Y) && !state.started;
}

} // namespace

int main()
{
    return TestDisabledPassthroughAndReset() &&
            TestFirstFrameAndRamp() &&
            TestSlowRequestIsNotAccelerated() &&
            TestSessionConnectionAndTargetResets() &&
            TestTargetRestartCanBeDisabled() &&
            TestPausedLoopsDoNotAdvanceCap() &&
            TestValidationAndDeltaTimeBounds() &&
            TestZeroAndNonFiniteAnglesDoNotStartRamp()
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
