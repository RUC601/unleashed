#include "Utils/Config.hpp"

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
    if (!NearlyEqual(FovCircleRenderAngleDeg(10.0f), 20.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(100.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(FovCircleRenderAngleDeg(360.0f), 180.0f))
        return Fail();
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(200.0f), 100.0f))
        return Fail();
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(360.0f), 180.0f))
        return Fail();
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

    return EXIT_SUCCESS;
}
