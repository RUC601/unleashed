#include "Utils/Config.hpp"

#include <cmath>
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

} // namespace

int main()
{
    using namespace OW::Config;

    if (!NearlyEqual(TrackingDeadzoneDampingScale(0.0f, 0.0f), 1.0f))
        return Fail();

    if (!NearlyEqual(TrackingDeadzoneDampingWidthPixels(20.0f), 10.0f))
        return Fail();

    if (!NearlyEqual(TrackingDeadzoneDampingScale(19.99f, 20.0f), 0.0f))
        return Fail();
    if (!NearlyEqual(TrackingDeadzoneDampingScale(20.0f, 20.0f), 0.0f))
        return Fail();
    if (!NearlyEqual(TrackingDeadzoneDampingScale(25.0f, 20.0f), 0.5f))
        return Fail();
    if (!NearlyEqual(TrackingDeadzoneDampingScale(30.0f, 20.0f), 1.0f))
        return Fail();
    if (!NearlyEqual(TrackingDeadzoneDampingScale(100.0f, 20.0f), 1.0f))
        return Fail();

    if (!NearlyEqual(TrackingDeadzoneDampingWidthPixels(4.0f), 8.0f))
        return Fail();
    if (!NearlyEqual(TrackingDeadzoneDampingScale(8.0f, 4.0f), 0.5f))
        return Fail();

    if (!NearlyEqual(TrackingDeadzoneDampingScale(
            std::numeric_limits<float>::quiet_NaN(), 20.0f), 0.0f))
        return Fail();

    return EXIT_SUCCESS;
}
