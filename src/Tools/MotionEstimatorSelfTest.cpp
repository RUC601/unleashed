#include "Game/Motion.hpp"

#include <cmath>
#include <cstdlib>

namespace {

struct MotionTestEntity {
    OW::Vector3 velocity{};
    OW::Vector3 previous_velocity{};
    OW::Vector3 pos{};
    OW::Vector3 previous_pos{};
    uint32_t render_sample_tick_ms = 0;
    uint32_t previous_render_sample_tick_ms = 0;
    bool has_previous_render_sample = false;
};

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.001f)
{
    return std::fabs(lhs - rhs) <= epsilon;
}

int Fail()
{
    return EXIT_FAILURE;
}

MotionTestEntity MakeSample(OW::Vector3 previousPos,
                            OW::Vector3 currentPos,
                            OW::Vector3 previousVelocity,
                            OW::Vector3 reportedVelocity,
                            uint32_t previousTick,
                            uint32_t currentTick)
{
    MotionTestEntity entity{};
    entity.previous_pos = previousPos;
    entity.pos = currentPos;
    entity.previous_velocity = previousVelocity;
    entity.velocity = reportedVelocity;
    entity.previous_render_sample_tick_ms = previousTick;
    entity.render_sample_tick_ms = currentTick;
    entity.has_previous_render_sample = true;
    return entity;
}

} // namespace

int main()
{
    {
        MotionTestEntity entity = MakeSample(
            OW::Vector3(10.0f, 0.0f, 0.0f),
            OW::Vector3(11.0f, 0.0f, 0.0f),
            OW::Vector3(0.0f, 0.0f, 0.0f),
            OW::Vector3(0.0f, 0.0f, 0.0f),
            1000,
            1100);

        const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
        if (!motion.valid || !motion.hasWorldDeltaVelocity || !motion.usedWorldDeltaFallback)
            return Fail();
        if (motion.velocitySource != OW::Motion::VelocitySource::WorldDeltaFallback)
            return Fail();
        if (!NearlyEqual(motion.worldDeltaVelocity.X, 10.0f) ||
            !NearlyEqual(motion.effectiveVelocity.X, 10.0f))
            return Fail();
    }

    {
        MotionTestEntity entity = MakeSample(
            OW::Vector3(10.0f, 0.0f, 0.0f),
            OW::Vector3(11.0f, 0.0f, 0.0f),
            OW::Vector3(4.0f, 0.0f, 0.0f),
            OW::Vector3(5.0f, 0.0f, 0.0f),
            1000,
            1100);

        const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
        if (!motion.valid || motion.usedWorldDeltaFallback)
            return Fail();
        if (motion.velocitySource != OW::Motion::VelocitySource::Reported)
            return Fail();
        if (!NearlyEqual(motion.effectiveVelocity.X, 5.0f) ||
            !NearlyEqual(motion.reportedAcceleration.X, 10.0f))
            return Fail();
    }

    {
        MotionTestEntity entity = MakeSample(
            OW::Vector3(10.0f, 0.0f, 0.0f),
            OW::Vector3(100.0f, 0.0f, 0.0f),
            OW::Vector3(0.0f, 0.0f, 0.0f),
            OW::Vector3(0.0f, 0.0f, 0.0f),
            1000,
            1100);

        const OW::Motion::EntityMotionEstimate motion = OW::Motion::EstimateEntityMotion(entity);
        if (!motion.valid || motion.hasWorldDeltaVelocity || motion.usedWorldDeltaFallback)
            return Fail();
        if (motion.velocitySource != OW::Motion::VelocitySource::Reported)
            return Fail();
    }

    return EXIT_SUCCESS;
}
