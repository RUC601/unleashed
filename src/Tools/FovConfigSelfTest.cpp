#include "Utils/Config.hpp"
#include "Game/FovGeometry.hpp"
#include "Game/GameData.hpp"
#include "Game/Offsets.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>

namespace {

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

bool ColorNearlyEqual(const ImVec4& color, float x, float y, float z, float w)
{
    return NearlyEqual(color.x, x) &&
           NearlyEqual(color.y, y) &&
           NearlyEqual(color.z, z) &&
           NearlyEqual(color.w, w);
}

int Fail()
{
    return EXIT_FAILURE;
}

OW::Vector3 PointAtAngleDeg(const OW::Vector3& camera, float angleDeg, float distance)
{
    const float angleRad = DEG2RAD(angleDeg);
    return camera + OW::Vector3(
        std::sin(angleRad) * distance,
        0.0f,
        std::cos(angleRad) * distance);
}

float ResolveThirtyDegreeFovAtRange(float fixedFovDeg, float distance)
{
    return distance >= 5.0f ? 30.0f : fixedFovDeg;
}

} // namespace

int main()
{
    using namespace OW::Config;

    if (!NearlyEqual(kMaxFovDeg, 180.0f))
        return Fail();
    if (!NearlyEqual(kDefaultFovDeg, 15.0f))
        return Fail();
    if (!NearlyEqual(ClampFovDeg(360.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(ClampFovDeg(60.0f), 60.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(10.0f), 10.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(100.0f), 100.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(103.0f), 103.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(360.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(200.0f), 100.0f))
        return Fail();
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(360.0f), 180.0f))
        return Fail();

    {
        constexpr float kWidth = 1920.0f;
        constexpr float kHeight = 1080.0f;
        constexpr float kFocalLength = 763.58f;
        const float maxRadius = std::hypot(kWidth, kHeight) * 0.5f;
        const auto sampleAngle = [=](float radius, float& outAngleDeg) {
            outAngleDeg = RAD2DEG(std::atan(radius / kFocalLength));
            return true;
        };

        const OW::FovGeometry::RingProjectionResult fivePointFour =
            OW::FovGeometry::ResolveRingProjection(5.4f, maxRadius, sampleAngle);
        if (!fivePointFour.IsDrawable() ||
            !NearlyEqual(
                fivePointFour.radius,
                kFocalLength * std::tan(DEG2RAD(5.4f)),
                0.25f) ||
            !NearlyEqual(fivePointFour.centerAngleDeg, 0.0f) ||
            !NearlyEqual(
                fivePointFour.edgeAngleDeg,
                RAD2DEG(std::atan(maxRadius / kFocalLength)))) {
            return Fail();
        }

        const OW::FovGeometry::RingProjectionResult fifteen =
            OW::FovGeometry::ResolveRingProjection(15.0f, maxRadius, sampleAngle);
        if (!fifteen.IsDrawable() ||
            !NearlyEqual(fifteen.radius, kFocalLength * std::tan(DEG2RAD(15.0f)), 0.25f)) {
            return Fail();
        }

        const OW::FovGeometry::RingProjectionResult fiftyFive =
            OW::FovGeometry::ResolveRingProjection(55.0f, maxRadius, sampleAngle);
        if (!fiftyFive.IsDrawable() ||
            !NearlyEqual(fiftyFive.radius, kFocalLength * std::tan(DEG2RAD(55.0f)), 0.5f)) {
            return Fail();
        }

        const float cornerAngleDeg = RAD2DEG(std::atan(maxRadius / kFocalLength));
        for (float angleDeg : { cornerAngleDeg, 80.0f, 100.0f, 180.0f }) {
            const OW::FovGeometry::RingProjectionResult outside =
                OW::FovGeometry::ResolveRingProjection(angleDeg, maxRadius, sampleAngle);
            if (outside.status != OW::FovGeometry::RingProjectionStatus::OutsideViewport ||
                outside.IsDrawable()) {
                return Fail();
            }
        }

        const OW::FovGeometry::RingProjectionResult zero =
            OW::FovGeometry::ResolveRingProjection(0.0f, maxRadius, sampleAngle);
        if (zero.status != OW::FovGeometry::RingProjectionStatus::Unavailable || zero.IsDrawable())
            return Fail();

        const OW::FovGeometry::RingProjectionResult invalid =
            OW::FovGeometry::ResolveRingProjection(
                15.0f,
                maxRadius,
                [](float, float&) { return false; });
        if (invalid.status != OW::FovGeometry::RingProjectionStatus::Unavailable || invalid.IsDrawable())
            return Fail();
    }

    {
        const OW::Vector3 forward(0.0f, 0.0f, 1.0f);
        OW::Vector3 direction{};

        if (!OW::FovGeometry::OrientProjectionRay(
                OW::Vector3(0.0f, 0.0f, 1.0f),
                OW::Vector3(0.0f, 0.0f, 10.0f),
                forward,
                direction) ||
            !NearlyEqual(direction | forward, 1.0f)) {
            return Fail();
        }

        if (!OW::FovGeometry::OrientProjectionRay(
                OW::Vector3(0.0f, 0.0f, 10.0f),
                OW::Vector3(0.0f, 0.0f, 1.0f),
                forward,
                direction) ||
            !NearlyEqual(direction | forward, 1.0f) ||
            !NearlyEqual(RAD2DEG(std::acos(std::clamp(direction | forward, -1.0f, 1.0f))), 0.0f)) {
            return Fail();
        }

        if (OW::FovGeometry::OrientProjectionRay(
                OW::Vector3(1.0f, 2.0f, 3.0f),
                OW::Vector3(1.0f, 2.0f, 3.0f),
                forward,
                direction)) {
            return Fail();
        }

        OW::FovGeometry::HomogeneousPoint infiniteFar{};
        infiniteFar.xyz = OW::Vector3(0.0f, 0.0f, -1.0f);
        infiniteFar.w = 0.0f;
        OW::FovGeometry::HomogeneousPoint finiteNear{};
        finiteNear.xyz = OW::Vector3(0.0f, 0.0f, 1.0f);
        finiteNear.w = 1.0f;
        if (!OW::FovGeometry::ResolveProjectionRay(
                infiniteFar,
                finiteNear,
                forward,
                direction) ||
            !NearlyEqual(direction | forward, 1.0f)) {
            return Fail();
        }

        OW::FovGeometry::HomogeneousPoint invalidInfinite = infiniteFar;
        invalidInfinite.xyz = OW::Vector3{};
        if (OW::FovGeometry::ResolveProjectionRay(
                invalidInfinite,
                finiteNear,
                forward,
                direction) ||
            OW::FovGeometry::ResolveProjectionRay(
                infiniteFar,
                infiniteFar,
                forward,
                direction)) {
            return Fail();
        }

        OW::Vector3 homogeneousPoint{};
        if (!OW::FovGeometry::TryHomogeneousPoint(
                2.0f, 4.0f, 6.0f, 2.0f, homogeneousPoint) ||
            homogeneousPoint != OW::Vector3(1.0f, 2.0f, 3.0f) ||
            OW::FovGeometry::TryHomogeneousPoint(
                1.0f, 2.0f, 3.0f, 0.0f, homogeneousPoint) ||
            OW::FovGeometry::TryHomogeneousPoint(
                (std::numeric_limits<float>::infinity)(),
                2.0f,
                3.0f,
                1.0f,
                homogeneousPoint)) {
            return Fail();
        }

        if (!OW::FovGeometry::IsProjectionInverseUsable(0.00000001f, true) ||
            OW::FovGeometry::IsProjectionInverseUsable(0.0f, true) ||
            OW::FovGeometry::IsProjectionInverseUsable(
                (std::numeric_limits<float>::infinity)(), true) ||
            OW::FovGeometry::IsProjectionInverseUsable(1.0f, false)) {
            return Fail();
        }
    }

    {
        OW::FovGeometry::RuntimeContext context{};
        context.camera = OW::Vector3(10.0f, 0.0f, 0.0f);
        context.forward = OW::Vector3(0.0f, 0.0f, 1.0f);
        context.valid = true;

        const OW::Vector3 front = PointAtAngleDeg(context.camera, 0.0f, 10.0f);
        const OW::Vector3 side = PointAtAngleDeg(context.camera, 90.0f, 10.0f);
        const OW::Vector3 back = PointAtAngleDeg(context.camera, 180.0f, 10.0f);
        if (!NearlyEqual(OW::FovGeometry::FovScoreDeg(context, front), 0.0f) ||
            !NearlyEqual(OW::FovGeometry::FovScoreDeg(context, side), 90.0f) ||
            !NearlyEqual(OW::FovGeometry::FovScoreDeg(context, back), 180.0f)) {
            return Fail();
        }
        if (!OW::FovGeometry::IsWithinFovDeg(context, side, 100.0f) ||
            OW::FovGeometry::IsWithinFovDeg(context, back, 100.0f) ||
            !OW::FovGeometry::IsWithinFovDeg(context, back, 180.0f)) {
            return Fail();
        }

        const OW::Vector3 rawAimPoint = PointAtAngleDeg(context.camera, 0.0f, 10.0f);
        const OW::Vector3 configuredPoint = PointAtAngleDeg(context.camera, 10.0f, 10.0f);
        const OW::Vector3 autoboneOutside = PointAtAngleDeg(context.camera, 40.0f, 10.0f);
        const OW::Vector3 fallbackCandidate = PointAtAngleDeg(context.camera, 20.0f, 10.0f);
        OW::FovGeometry::CandidateFovEvaluation evaluation{};
        if (!OW::FovGeometry::EvaluateCandidateFov(
                context,
                rawAimPoint,
                configuredPoint,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                evaluation) ||
            !NearlyEqual(evaluation.effectiveFovDeg, 30.0f)) {
            return Fail();
        }
        if (OW::FovGeometry::EvaluateCandidateFov(
                context,
                rawAimPoint,
                autoboneOutside,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                evaluation)) {
            return Fail();
        }

        int selectedCandidate = -1;
        float bestScoreDeg = (std::numeric_limits<float>::max)();
        if (OW::FovGeometry::EvaluateCandidateFov(
                context,
                rawAimPoint,
                fallbackCandidate,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                evaluation) &&
            evaluation.scoreDeg < bestScoreDeg) {
            bestScoreDeg = evaluation.scoreDeg;
            selectedCandidate = 1;
        }
        if (selectedCandidate != 1 || !NearlyEqual(bestScoreDeg, 20.0f))
            return Fail();

        OW::FovGeometry::PredictedEntryEvaluation entry{};
        const OW::Vector3 justOutside = PointAtAngleDeg(context.camera, 31.0f, 10.0f);
        const OW::Vector3 entering = PointAtAngleDeg(context.camera, 29.0f, 10.0f);
        if (!OW::FovGeometry::EvaluateCandidateFovWithPredictedEntry(
                context,
                rawAimPoint,
                justOutside,
                entering,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                true,
                2.0f,
                entry) ||
            !entry.acceptedByPrediction ||
            entry.currentlyInside ||
            !NearlyEqual(entry.currentScoreDeg, 31.0f) ||
            !NearlyEqual(entry.projectedScoreDeg, 29.0f)) {
            return Fail();
        }
        if (OW::FovGeometry::EvaluateCandidateFovWithPredictedEntry(
                context,
                rawAimPoint,
                justOutside,
                entering,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                false,
                2.0f,
                entry) ||
            OW::FovGeometry::EvaluateCandidateFovWithPredictedEntry(
                context,
                rawAimPoint,
                PointAtAngleDeg(context.camera, 34.0f, 10.0f),
                entering,
                100.0f,
                &ResolveThirtyDegreeFovAtRange,
                true,
                2.0f,
                entry)) {
            return Fail();
        }
    }

    DynamicFovPreset dynamic{};
    dynamic.id = 77;
    dynamic.pointCount = 5;
    dynamic.smooth = false;
    dynamic.points[0] = { 0.0f, 180.0f };
    dynamic.points[1] = { 5.0f, 180.0f };
    dynamic.points[2] = { 10.0f, 30.0f };
    dynamic.points[3] = { 20.0f, 8.0f };
    dynamic.points[4] = { 30.0f, 5.0f };
    if (!NearlyEqual(EvaluateDynamicFovPreset(dynamic, 0.0f, 100.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(EvaluateDynamicFovPreset(dynamic, 4.9f, 100.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(EvaluateDynamicFovPreset(dynamic, 7.5f, 100.0f), 105.0f))
        return Fail();
    if (!NearlyEqual(EvaluateDynamicFovPreset(dynamic, 35.0f, 100.0f), 5.0f))
        return Fail();

    dynamicFovPresets.clear();
    aimbotFovMode = kFovModeFixed;
    aimbotDynamicFovPresetId = -1;
    autoscalefov = false;
    if (!NearlyEqual(ResolveDynamicAimFovForDistance(60.0f, 30.0f), 60.0f))
        return Fail();
    dynamicFovPresets.push_back(dynamic);
    aimbotFovMode = kFovModeDynamicPreset;
    aimbotDynamicFovPresetId = dynamic.id;
    if (!NearlyEqual(ResolveDynamicAimFovForDistance(60.0f, 30.0f), 5.0f))
        return Fail();
    autoscalefov = true;
    if (!NearlyEqual(ResolveDynamicAimFovForDistance(60.0f, 30.0f), 60.0f))
        return Fail();
    autoscalefov = false;

    if (drawhealth)
        return Fail();
    if (!ColorNearlyEqual(EnemyCol, 1.0f, 0.0f, 0.0f, 0.0f))
        return Fail();
    if (!ColorNearlyEqual(enargb, 1.0f, 0.0f, 0.0f, 1.0f))
        return Fail();
    if (!ColorNearlyEqual(invisnenargb, 1.0f, 1.0f, 1.0f, 1.0f))
        return Fail();
    if (!ColorNearlyEqual(targetargb, 1.0f, 1.0f, 0.0f, 1.0f))
        return Fail();
    if (!ColorNearlyEqual(allyargb, 0.0f, 0.0f, 1.0f, 0.0f))
        return Fail();

    OW::offset::SetActiveProfile(OW::offset::RuntimeProfile::CnNe);
    if (OW::offset::Active().typeTeam != 0x20 ||
        OW::offset::Active().typePlayerController != 0x42 ||
        OW::offset::Active().typeRotation != 0x2E ||
        OW::offset::Active().typeLink != 0x33 ||
        OW::offset::Active().typeHealth != 0x3A ||
        OW::offset::Active().typeHeroId != 0x53)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x208D4041) != 0x00800000)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x208D4043) != 0x00800000)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x210D4043) != 0x01000000)
        return Fail();
    if (OW::offset::TeamRawComparisonKeyFromFlags(0x208D4041) != 0x8D4041)
        return Fail();
    if (OW::offset::TeamRawComparisonKeyFromFlags(0x208D4043) != 0x8D4043)
        return Fail();
    if (OW::offset::TeamRawComparisonKeyFromFlags(0x210D4043) != 0x0D4043)
        return Fail();
    if (OW::offset::TeamRelationCodeFromFlags(0x208D4041) != 0x41)
        return Fail();
    if (OW::offset::TeamRelationCodeFromFlags(0x210D4047) != 0x47)
        return Fail();
    if (!OW::GameData::IsFriendlyTrainingBotHeroId(OW::GameData::MakeHeroId(0x4E7)))
        return Fail();
    if (!OW::GameData::IsFriendlyTrainingBotHeroId(OW::GameData::MakeHeroId(0x363)))
        return Fail();
    if (OW::GameData::IsFriendlyTrainingBotHeroId(OW::GameData::MakeHeroId(0x33C)))
        return Fail();
    if (OW::GameData::UnknownHeroFallbackName(0x1234) != "BzHero_1234")
        return Fail();

    OW::offset::SetActiveProfile(OW::offset::RuntimeProfile::WorldBz);
    if (OW::offset::Active().typeTeam != 0x20 ||
        OW::offset::Active().typePlayerController != 0x42 ||
        OW::offset::Active().typeRotation != 0x2E ||
        OW::offset::Active().typeLink != 0x33 ||
        OW::offset::Active().typeHealth != 0x3A ||
        OW::offset::Active().typeHeroId != 0x53)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x208D4041) != 0x00800000)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x210D4045) != 0x01000000)
        return Fail();
    if (OW::GameData::UnknownHeroFallbackName(0x1234) != "BzHero_1234")
        return Fail();

    return EXIT_SUCCESS;
}
