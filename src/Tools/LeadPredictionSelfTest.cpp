#include "Game/AimArchitecture.hpp"
#include "Game/LeadPrediction.hpp"

#include <cmath>
#include <cstdlib>

Memory::~Memory() = default;

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int Fail()
{
    return EXIT_FAILURE;
}

} // namespace

int main()
{
    {
        OW::WeaponSpec projectileWeapon{};
        projectileWeapon.projectile = OW::ProjectileSpec{
            true,
            120.0f,
            true,
            0.2f,
            0.0f,
            0.0f,
            true
        };

        if (!OW::ResolvePredictionEnabled(OW::PredictionOverrideMode::Auto, &projectileWeapon, false))
            return Fail();
        if (!OW::ResolvePredictionEnabled(OW::PredictionOverrideMode::ForceOn, nullptr, false))
            return Fail();
        if (OW::ResolvePredictionEnabled(OW::PredictionOverrideMode::ForceOff, &projectileWeapon, true))
            return Fail();
        if (!OW::ResolvePredictionEnabled(OW::PredictionOverrideMode::Auto, nullptr, true))
            return Fail();
    }

    {
        OW::AimSettleEstimateInput input{};
        input.localAngle = OW::Vector3(0.0f, 0.0f, 0.0f);
        input.targetAngle = OW::Vector3(0.0f, 0.05235988f, 0.0f); // 3 degrees
        input.method = 5;
        input.slotSpeedScale = 1.0f;
        input.methodSpeedScale = 1.0f;
        input.constantAngularSpeedDeg = 30.0f;
        input.frameSeconds = 0.01f;

        if (!NearlyEqual(OW::EstimateAimSettleTimeMs(input), 100.0f))
            return Fail();
    }

    {
        OW::AimSettleEstimateInput input{};
        input.localAngle = OW::Vector3(0.0f, 0.0f, 0.0f);
        input.targetAngle = OW::Vector3(0.0f, 0.1f, 0.0f);
        input.method = 0;
        input.slotSpeedScale = 1.0f;
        input.methodSpeedScale = 1.0f;
        input.frameSeconds = 0.01f;

        if (!NearlyEqual(OW::EstimateAimSettleTimeMs(input), 10.0f))
            return Fail();
    }

    {
        const OW::LeadTimingEstimate timing = OW::BuildLeadTiming(100.0f, 5);
        if (!NearlyEqual(timing.estimatedSettleMs, 100.0f) ||
            timing.inputDelayMs != 5 ||
            !NearlyEqual(timing.preFireDelayMs, 105.0f))
            return Fail();

        const OW::LeadTimingEstimate clamped = OW::BuildLeadTiming(1000.0f, 50);
        if (!NearlyEqual(clamped.estimatedSettleMs, OW::kLeadMaxSettleMs) ||
            clamped.inputDelayMs != 20 ||
            !NearlyEqual(clamped.preFireDelayMs, 270.0f))
            return Fail();
    }

    {
        const OW::Vector3 shifted = OW::ApplyTargetMotionPreFireDelay(
            OW::Vector3(0.0f, 0.0f, 0.0f),
            OW::Vector3(10.0f, 0.0f, 0.0f),
            100.0f);
        if (!NearlyEqual(shifted.X, 1.0f))
            return Fail();

        const OW::Vector3 clamped = OW::ApplyTargetMotionPreFireDelay(
            OW::Vector3(0.0f, 0.0f, 0.0f),
            OW::Vector3(500.0f, 0.0f, 0.0f),
            100.0f);
        if (!NearlyEqual(clamped.X, 25.0f))
            return Fail();
    }

    return EXIT_SUCCESS;
}
