#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    constexpr double kPi = 3.1415926535897932384626433832795;
    constexpr double kTau = 2.0 * kPi;
    constexpr double kAngleEpsilon = 0.000001;
    constexpr double kPixelEpsilon = 0.000001;

    struct Vector3
    {
        double x;
        double y;
        double z;
    };

    struct FloatVector3
    {
        float x;
        float y;
        float z;
    };

    struct Angles
    {
        double pitch;
        double yaw;
    };

    struct AimData
    {
        Vector3 localAngle;
        Vector3 targetAngle;
        Vector3 delta;
    };

    struct PixelOutput
    {
        double scaledPitch;
        double scaledYaw;
        int pixelX;
        int pixelY;
    };

    struct PixelAccumulator
    {
        double accumX;
        double accumY;
    };

    struct DoubleRotator
    {
        double pitch;
        double yaw;
        double roll;
    };

    bool Near(double actual, double expected, double epsilon)
    {
        return std::fabs(actual - expected) <= epsilon;
    }

    bool NearFloat(float actual, float expected, float epsilon)
    {
        return std::fabs(actual - expected) <= epsilon;
    }

    const char* PassFail(bool pass)
    {
        return pass ? "PASS" : "FAIL";
    }

    double RadToDeg(double radians)
    {
        return radians * 180.0 / kPi;
    }

    Angles ComputeTargetAngles(const Vector3& cameraPos, const Vector3& targetPos)
    {
        const double dx = targetPos.x - cameraPos.x;
        const double dy = targetPos.y - cameraPos.y;
        const double dz = targetPos.z - cameraPos.z;
        const double horizontalDist = std::sqrt(dx * dx + dz * dz);

        Angles result{};
        result.pitch = -std::atan2(dy, horizontalDist);
        result.yaw = std::atan2(dx, dz);
        return result;
    }

    double WrapYawDelta(double localYaw, double targetYaw)
    {
        double delta = targetYaw - localYaw;
        while (delta > kPi)
            delta -= kTau;
        while (delta < -kPi)
            delta += kTau;
        return delta;
    }

    AimData BuildAimData_Standalone(
        double localPitch,
        double localYaw,
        double targetPitch,
        double targetYaw)
    {
        AimData data{};
        data.localAngle = Vector3{ localPitch, localYaw, 0.0 };
        data.targetAngle = Vector3{ targetPitch, targetYaw, 0.0 };
        data.delta = Vector3{
            data.targetAngle.x - data.localAngle.x,
            WrapYawDelta(data.localAngle.y, data.targetAngle.y),
            0.0
        };
        return data;
    }

    PixelOutput RadiansToPixels(double deltaPitch, double deltaYaw, double sensitivity)
    {
        PixelOutput result{};
        result.scaledYaw = deltaYaw * sensitivity;
        result.scaledPitch = deltaPitch * sensitivity;
        result.pixelX = static_cast<int>(result.scaledYaw);
        result.pixelY = static_cast<int>(result.scaledPitch);
        return result;
    }

    PixelOutput RadiansToPixelsAccumulated(
        double deltaPitch,
        double deltaYaw,
        double sensitivity,
        PixelAccumulator& accumulator)
    {
        const double scaledYaw = deltaYaw * sensitivity;
        const double scaledPitch = deltaPitch * sensitivity;
        accumulator.accumX += scaledYaw;
        accumulator.accumY += scaledPitch;

        PixelOutput result{};
        result.scaledYaw = scaledYaw;
        result.scaledPitch = scaledPitch;
        result.pixelX = static_cast<int>(accumulator.accumX);
        result.pixelY = static_cast<int>(accumulator.accumY);
        accumulator.accumX -= static_cast<double>(result.pixelX);
        accumulator.accumY -= static_cast<double>(result.pixelY);
        return result;
    }

    void PrintBytes(const unsigned char* bytes, int count)
    {
        for (int index = 0; index < count; ++index) {
            std::printf("%02X", static_cast<unsigned int>(bytes[index]));
            if (index + 1 < count)
                std::printf(" ");
        }
    }

    bool TestComputeTargetAngles()
    {
        const Vector3 cameraPos{ 0.0, 1.7, 0.0 };
        const Vector3 cameraForward{ 0.0, 0.0, 1.0 };
        const Vector3 cameraRight{ 1.0, 0.0, 0.0 };
        const Vector3 cameraUp{ 0.0, 1.0, 0.0 };
        const Vector3 targetPos{ 5.0, 2.0, 10.0 };
        const int screenWidth = 1920;
        const int screenHeight = 1080;

        const double expectedDx = 5.0;
        const double expectedDy = 0.3;
        const double expectedDz = 10.0;
        const double expectedHorizontalDist = 11.1803398874989;
        const double expectedPitch = -0.0268263786348187;
        const double expectedYaw = 0.463647609000806;

        const double actualDx = targetPos.x - cameraPos.x;
        const double actualDy = targetPos.y - cameraPos.y;
        const double actualDz = targetPos.z - cameraPos.z;
        const double actualHorizontalDist = std::sqrt(actualDx * actualDx + actualDz * actualDz);
        const Angles actual = ComputeTargetAngles(cameraPos, targetPos);

        const bool pass =
            Near(actualDx, expectedDx, kAngleEpsilon) &&
            Near(actualDy, expectedDy, kAngleEpsilon) &&
            Near(actualDz, expectedDz, kAngleEpsilon) &&
            Near(actualHorizontalDist, expectedHorizontalDist, kAngleEpsilon) &&
            Near(actual.pitch, expectedPitch, kAngleEpsilon) &&
            Near(actual.yaw, expectedYaw, kAngleEpsilon);

        std::printf(
            "[ComputeTargetAngles] input=cameraPos=(%.3f,%.3f,%.3f) forward=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f) targetPos=(%.3f,%.3f,%.3f) screen=%dx%d "
            "expected=(dx=%.6f dy=%.6f dz=%.6f horizontal=%.6f pitch=%.9f rad %.6f deg yaw=%.9f rad %.6f deg) "
            "actual=(dx=%.6f dy=%.6f dz=%.6f horizontal=%.6f pitch=%.9f rad %.6f deg yaw=%.9f rad %.6f deg) %s\n",
            cameraPos.x, cameraPos.y, cameraPos.z,
            cameraForward.x, cameraForward.y, cameraForward.z,
            cameraRight.x, cameraRight.y, cameraRight.z,
            cameraUp.x, cameraUp.y, cameraUp.z,
            targetPos.x, targetPos.y, targetPos.z,
            screenWidth, screenHeight,
            expectedDx, expectedDy, expectedDz, expectedHorizontalDist,
            expectedPitch, RadToDeg(expectedPitch),
            expectedYaw, RadToDeg(expectedYaw),
            actualDx, actualDy, actualDz, actualHorizontalDist,
            actual.pitch, RadToDeg(actual.pitch),
            actual.yaw, RadToDeg(actual.yaw),
            PassFail(pass));

        return pass;
    }

    bool TestWrapYawDelta()
    {
        const double localYaw = 3.0;
        const double targetYaw = -3.0;
        const double expectedRawDelta = -6.0;
        const double expectedWrappedDelta = 0.283185307179586;
        const double actualRawDelta = targetYaw - localYaw;
        const double actualWrappedDelta = WrapYawDelta(localYaw, targetYaw);

        const bool inRange = actualWrappedDelta >= -kPi && actualWrappedDelta <= kPi;
        const bool pass =
            Near(actualRawDelta, expectedRawDelta, kAngleEpsilon) &&
            Near(actualWrappedDelta, expectedWrappedDelta, kAngleEpsilon) &&
            inRange;

        std::printf(
            "[WrapYawDelta] input=(localYaw=%.9f targetYaw=%.9f) "
            "expected=(rawDelta=%.9f wrappedDelta=%.9f rad %.6f deg range=[-pi,pi]) "
            "actual=(rawDelta=%.9f wrappedDelta=%.9f rad %.6f deg inRange=%d) %s\n",
            localYaw, targetYaw,
            expectedRawDelta,
            expectedWrappedDelta, RadToDeg(expectedWrappedDelta),
            actualRawDelta,
            actualWrappedDelta, RadToDeg(actualWrappedDelta),
            inRange ? 1 : 0,
            PassFail(pass));

        return pass;
    }

    bool TestBuildAimData()
    {
        const double localPitch = 0.0;
        const double localYaw = 0.0;
        const double targetPitch = -0.0268;
        const double targetYaw = 0.4636;
        const double expectedDeltaPitch = -0.0268;
        const double expectedDeltaYaw = 0.4636;

        const AimData actual = BuildAimData_Standalone(localPitch, localYaw, targetPitch, targetYaw);
        const bool localMeaningPass =
            Near(actual.localAngle.x, localPitch, kAngleEpsilon) &&
            Near(actual.localAngle.y, localYaw, kAngleEpsilon) &&
            Near(actual.localAngle.z, 0.0, kAngleEpsilon);
        const bool targetMeaningPass =
            Near(actual.targetAngle.x, targetPitch, kAngleEpsilon) &&
            Near(actual.targetAngle.y, targetYaw, kAngleEpsilon) &&
            Near(actual.targetAngle.z, 0.0, kAngleEpsilon);
        const bool deltaPass =
            Near(actual.delta.x, expectedDeltaPitch, kAngleEpsilon) &&
            Near(actual.delta.y, expectedDeltaYaw, kAngleEpsilon) &&
            Near(actual.delta.z, 0.0, kAngleEpsilon);
        const bool pass = localMeaningPass && targetMeaningPass && deltaPass;

        std::printf(
            "[BuildAimData_Standalone] input=(localPitch=%.9f localYaw=%.9f targetPitch=%.9f targetYaw=%.9f) "
            "expected=(localAngle.Vector3.X=pitch localAngle.Vector3.Y=yaw localAngle.Vector3.Z=roll_or_unused delta=(%.9f,%.9f,0.000000000)) "
            "actual=(localAngle=(%.9f,%.9f,%.9f) targetAngle=(%.9f,%.9f,%.9f) delta=(%.9f,%.9f,%.9f) localMeaning=%d targetMeaning=%d) %s\n",
            localPitch, localYaw, targetPitch, targetYaw,
            expectedDeltaPitch, expectedDeltaYaw,
            actual.localAngle.x, actual.localAngle.y, actual.localAngle.z,
            actual.targetAngle.x, actual.targetAngle.y, actual.targetAngle.z,
            actual.delta.x, actual.delta.y, actual.delta.z,
            localMeaningPass ? 1 : 0,
            targetMeaningPass ? 1 : 0,
            PassFail(pass));

        return pass;
    }

    bool TestRadiansToPixels()
    {
        const double deltaPitch = -0.0268;
        const double deltaYaw = 0.4636;
        const double sensitivity = 1.0;
        const double expectedScaledPitch = -0.0268;
        const double expectedScaledYaw = 0.4636;
        const int expectedPixelX = 0;
        const int expectedPixelY = 0;

        const PixelOutput actual = RadiansToPixels(deltaPitch, deltaYaw, sensitivity);
        const bool directPass =
            Near(actual.scaledPitch, expectedScaledPitch, kPixelEpsilon) &&
            Near(actual.scaledYaw, expectedScaledYaw, kPixelEpsilon) &&
            actual.pixelX == expectedPixelX &&
            actual.pixelY == expectedPixelY;

        std::printf(
            "[RadiansToPixels.direct] input=(deltaPitch=%.9f deltaYaw=%.9f sensitivity=%.3f) "
            "expected=(scaledPitch=%.9f scaledYaw=%.9f pixelX=%d pixelY=%d note=pixelX=yaw pixelY=pitch integer_truncation_toward_zero) "
            "actual=(scaledPitch=%.9f scaledYaw=%.9f pixelX=%d pixelY=%d) %s\n",
            deltaPitch, deltaYaw, sensitivity,
            expectedScaledPitch, expectedScaledYaw, expectedPixelX, expectedPixelY,
            actual.scaledPitch, actual.scaledYaw, actual.pixelX, actual.pixelY,
            PassFail(directPass));

        PixelAccumulator accumulator{ 0.0, 0.0 };
        const double tinyDelta = 0.0001;
        bool accumPass = true;
        for (int step = 1; step <= 10; ++step) {
            const double beforeX = accumulator.accumX;
            const double beforeY = accumulator.accumY;
            const PixelOutput stepOutput =
                RadiansToPixelsAccumulated(tinyDelta, tinyDelta, sensitivity, accumulator);
            const bool stepPass =
                stepOutput.pixelX == 0 &&
                stepOutput.pixelY == 0 &&
                Near(accumulator.accumX, tinyDelta * static_cast<double>(step), kPixelEpsilon) &&
                Near(accumulator.accumY, tinyDelta * static_cast<double>(step), kPixelEpsilon);
            accumPass = accumPass && stepPass;

            std::printf(
                "[RadiansToPixels.accum] input=(step=%02d deltaPitch=%.9f deltaYaw=%.9f sensitivity=%.3f accumBefore=(%.9f,%.9f)) "
                "expected=(pixelX=0 pixelY=0 accumAfter=(%.9f,%.9f)) "
                "actual=(scaledPitch=%.9f scaledYaw=%.9f pixelX=%d pixelY=%d accumAfter=(%.9f,%.9f)) %s\n",
                step,
                tinyDelta, tinyDelta, sensitivity,
                beforeX, beforeY,
                tinyDelta * static_cast<double>(step),
                tinyDelta * static_cast<double>(step),
                stepOutput.scaledPitch,
                stepOutput.scaledYaw,
                stepOutput.pixelX,
                stepOutput.pixelY,
                accumulator.accumX,
                accumulator.accumY,
                PassFail(stepPass));
        }

        std::printf(
            "[RadiansToPixels.accum.summary] input=(delta=%.9f sensitivity=%.3f steps=10) "
            "expected=(allPixelsZero=1 finalAccum=(0.001000000,0.001000000)) "
            "actual=(allPixelsZero=%d finalAccum=(%.9f,%.9f)) %s\n",
            tinyDelta,
            sensitivity,
            accumPass ? 1 : 0,
            accumulator.accumX,
            accumulator.accumY,
            PassFail(accumPass));

        return directPass && accumPass;
    }

    bool TestFloatVsDouble()
    {
        const DoubleRotator stored{ 0.1, 0.5, 0.0 };
        unsigned char rawMemory[sizeof(DoubleRotator)] = {};
        std::memcpy(rawMemory, &stored, sizeof(stored));

        FloatVector3 readAsFloat{};
        DoubleRotator readAsDouble{};
        std::memcpy(&readAsFloat, rawMemory, sizeof(readAsFloat));
        std::memcpy(&readAsDouble, rawMemory, sizeof(readAsDouble));

        const bool doublePass =
            Near(readAsDouble.pitch, stored.pitch, kAngleEpsilon) &&
            Near(readAsDouble.yaw, stored.yaw, kAngleEpsilon) &&
            Near(readAsDouble.roll, stored.roll, kAngleEpsilon);
        const bool floatLooksCorrect =
            NearFloat(readAsFloat.x, static_cast<float>(stored.pitch), 0.000001f) &&
            NearFloat(readAsFloat.y, static_cast<float>(stored.yaw), 0.000001f) &&
            NearFloat(readAsFloat.z, static_cast<float>(stored.roll), 0.000001f);
        const bool pass = doublePass && !floatLooksCorrect;

        std::printf(
            "[FloatVsDouble] input=(storedDoubleFRotator pitch=%.9f yaw=%.9f roll=%.9f bytes=24) rawBytes=(",
            stored.pitch, stored.yaw, stored.roll);
        PrintBytes(rawMemory, static_cast<int>(sizeof(rawMemory)));
        std::printf(
            ") expected=(doubleRead=(%.9f,%.9f,%.9f) floatReadShouldDiffer=1 because 12-byte Vector3 reads halves_of_doubles) "
            "actual=(floatVector3Read bytes=12 value=(%.9f,%.9f,%.9f) doubleRead bytes=24 value=(%.9f,%.9f,%.9f) floatLooksCorrect=%d doubleCorrect=%d) %s\n",
            stored.pitch, stored.yaw, stored.roll,
            static_cast<double>(readAsFloat.x),
            static_cast<double>(readAsFloat.y),
            static_cast<double>(readAsFloat.z),
            readAsDouble.pitch,
            readAsDouble.yaw,
            readAsDouble.roll,
            floatLooksCorrect ? 1 : 0,
            doublePass ? 1 : 0,
            PassFail(pass));

        return pass;
    }
}

int main()
{
    TestComputeTargetAngles();
    TestWrapYawDelta();
    TestBuildAimData();
    TestRadiansToPixels();
    TestFloatVsDouble();
    std::printf("\nAll tests done.\n");
    return 0;
}
