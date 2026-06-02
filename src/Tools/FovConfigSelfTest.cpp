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
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(200.0f), 100.0f))
        return Fail();
    if (!NearlyEqual(LegacyFovApertureToAngleDeg(360.0f), 180.0f))
        return Fail();

    return EXIT_SUCCESS;
}
