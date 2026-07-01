#include "Utils/Config.hpp"
#include "Game/GameData.hpp"
#include "Game/Offsets.hpp"

#include <cmath>
#include <cstdlib>

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

} // namespace

int main()
{
    using namespace OW::Config;

    if (!NearlyEqual(kMaxFovDeg, 180.0f))
        return Fail();
    if (!NearlyEqual(kDefaultFovDeg, 100.0f))
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
    if (OW::offset::TeamComparisonKeyFromFlags(0x208D4041) != 0x00800000)
        return Fail();
    if (OW::offset::TeamComparisonKeyFromFlags(0x210D4043) != 0x01000000)
        return Fail();
    if (OW::GameData::UnknownHeroFallbackName(0x1234) != "BzHero_1234")
        return Fail();

    return EXIT_SUCCESS;
}
