#pragma once

#include <algorithm>
#include <cmath>

namespace OW::AimFirePhase {

struct Tuning {
    float shotIntervalMs = 250.0f;
    float pauseMs = 35.0f;
    float recoveryMs = 90.0f;
    float pauseYawScale = 0.35f;
    float pausePitchScale = 0.0f;
    float preFireBoostWindowMs = 45.0f;
    float preFireBoostScale = 1.35f;
};

struct Phase {
    float trackingScale = 1.0f;
    float yawScale = 1.0f;
    float pitchScale = 1.0f;
    bool paused = false;
    bool recovering = false;
    bool preFireBoost = false;
    bool readyToFire = true;
};

inline float Finite(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

inline Phase Resolve(float elapsedSinceShotMs, bool shotLatched, const Tuning& raw)
{
    Phase phase{};
    if (!shotLatched)
        return phase;

    const float interval = std::clamp(Finite(raw.shotIntervalMs, 250.0f), 1.0f, 5000.0f);
    const float pause = std::clamp(Finite(raw.pauseMs, 35.0f), 0.0f, interval);
    const float recovery = std::clamp(Finite(raw.recoveryMs, 90.0f), 0.0f, 1000.0f);
    const float yawFloor = std::clamp(Finite(raw.pauseYawScale, 0.35f), 0.0f, 1.0f);
    const float pitchFloor = std::clamp(Finite(raw.pausePitchScale, 0.0f), 0.0f, 1.0f);
    const float elapsed = (std::max)(0.0f, Finite(elapsedSinceShotMs, 0.0f));
    phase.readyToFire = elapsed >= interval;

    if (elapsed < pause) {
        phase.paused = true;
        phase.yawScale = yawFloor;
        phase.pitchScale = pitchFloor;
    } else if (recovery > 0.0f && elapsed < pause + recovery) {
        phase.recovering = true;
        const float t = std::clamp((elapsed - pause) / recovery, 0.0f, 1.0f);
        const float smooth = t * t * (3.0f - 2.0f * t);
        phase.yawScale = yawFloor + (1.0f - yawFloor) * smooth;
        phase.pitchScale = pitchFloor + (1.0f - pitchFloor) * smooth;
    }

    const float boostWindow = std::clamp(
        Finite(raw.preFireBoostWindowMs, 45.0f), 0.0f, interval);
    const float remaining = interval - elapsed;
    if (!phase.readyToFire && boostWindow > 0.0f && remaining <= boostWindow) {
        const float progress = std::clamp(1.0f - remaining / boostWindow, 0.0f, 1.0f);
        phase.preFireBoost = true;
        phase.trackingScale = 1.0f +
            (std::clamp(Finite(raw.preFireBoostScale, 1.35f), 1.0f, 3.0f) - 1.0f) * progress;
    }

    return phase;
}

// Resolve the same post-fire pause/recovery and pre-fire boost for an
// irregular sequence. The caller supplies the measured time on both sides of
// the current point, so a mixed hip/scoped cadence does not have to pretend it
// is a fixed-rate weapon.
inline Phase ResolveTimeline(float elapsedSinceShotMs,
                             float timeUntilNextShotMs,
                             bool shotObserved,
                             const Tuning& raw)
{
    const float elapsed = (std::max)(0.0f, Finite(elapsedSinceShotMs, 0.0f));
    const float remaining = (std::max)(0.0f, Finite(timeUntilNextShotMs, 0.0f));
    if (!shotObserved) {
        Phase phase{};
        phase.readyToFire = remaining <= 0.0f;
        const float boostWindow = std::clamp(
            Finite(raw.preFireBoostWindowMs, 45.0f), 0.0f, 2000.0f);
        if (boostWindow > 0.0f && remaining > 0.0f && remaining <= boostWindow) {
            const float progress = std::clamp(1.0f - remaining / boostWindow, 0.0f, 1.0f);
            phase.preFireBoost = true;
            phase.trackingScale = 1.0f +
                (std::clamp(Finite(raw.preFireBoostScale, 1.35f), 1.0f, 3.0f) - 1.0f) * progress;
        }
        return phase;
    }

    Tuning tuning = raw;
    tuning.shotIntervalMs = (std::max)(1.0f, elapsed + remaining);
    Phase phase = Resolve(elapsed, true, tuning);
    phase.readyToFire = remaining <= 0.0f;
    return phase;
}

} // namespace OW::AimFirePhase
