#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>
#include <string>
#include <ctime>
#include <limits>
#include <mutex>
#include <cstdio>
#include <unordered_map>
#include <windows.h>
#include <chrono>
#include <thread>

#include "Game/Decrypt.hpp"
#include "Game/Entity.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/LeadPrediction.hpp"
#include "Game/Motion.hpp"
#include "Game/WeaponSpec.hpp"
#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"

extern std::mutex g_mutex;

namespace OW {

    inline constexpr uint64_t kPlayerControllerViewDirectionOffset = 0x1260;

    inline Vector3 DirectionToAimEuler(const Vector3& direction) {
        return Vector3(
            -asinf(direction.Y),
            atan2f(direction.X, direction.Z),
            0.0f
        );
    }

    inline Vector3 ReadPlayerControllerViewDirection(uint64_t playerControllerBase) {
        if (!playerControllerBase)
            return Vector3{};
        return SDK->RPM<Vector3>(playerControllerBase + kPlayerControllerViewDirectionOffset);
    }

    // =========================================================================
    // Global extern declarations (defined in Overwatch.hpp)
    // =========================================================================

    extern Matrix viewMatrix;
    extern Matrix viewMatrix_xor;
    extern std::mutex g_viewMatrixMutex;
    extern float WX;
    extern float WY;
    extern std::vector<c_entity> entities;
    extern c_entity local_entity;
    extern std::vector<hpanddy> hp_dy_entities;

    inline void SnapshotViewMatrices(Matrix& view, Matrix& viewXor) {
        std::lock_guard<std::mutex> lock(g_viewMatrixMutex);
        view = viewMatrix;
        viewXor = viewMatrix_xor;
    }

    inline Matrix SnapshotViewMatrix() {
        Matrix view{}, viewXor{};
        SnapshotViewMatrices(view, viewXor);
        return view;
    }

    inline Vector3 SnapshotCameraPosition() {
        Matrix view{}, viewXor{};
        SnapshotViewMatrices(view, viewXor);
        const auto camera = viewXor.get_location();
        return Vector3(camera.x, camera.y, camera.z);
    }

    inline c_entity SnapshotLocalEntity() {
        std::lock_guard<std::mutex> lock(::g_mutex);
        return local_entity;
    }

    inline uint64_t SnapshotLocalSkillBase() {
        std::lock_guard<std::mutex> lock(::g_mutex);
        return local_entity.SkillBase;
    }

    // =========================================================================
    // Color4 = ImVec4 (Config uses ImVec4 directly)

    // =========================================================================
    // Config variables are in Utils/Config.hpp
    // =========================================================================

    // =========================================================================
    // Keyboard / input helpers
    // =========================================================================

    inline bool DmaKeyToMouseButton(uint32_t key, int& button) {
        switch (key) {
        case 0x1: button = 0; return true;
        case 0x2: button = 1; return true;
        case 0x4: button = 2; return true;
        default: return false;
        }
    }

    inline int ClampKmboxAutomoveRuntimeMs(int moveTimeMs) {
        return std::clamp(moveTimeMs, 0, 20);
    }

    inline const char* KmboxTransportName() {
        if (Config::kmboxDeviceType == 2)
            return "mock";
        return Config::kmboxDeviceType == 0 ? "network" : "serial";
    }

    inline int EnqueueKmboxPixelMove(int pixelX, int pixelY, int automoveRuntimeMs) {
        if (Config::kmboxDeviceType == 2)
            return kmbox::MockHardwareMgr.RecordMove(pixelX, pixelY, automoveRuntimeMs);

        if (Config::kmboxDeviceType == 0) {
            return automoveRuntimeMs > 0
                ? kmbox::KmBoxMgr.Mouse.Move_Auto(pixelX, pixelY, automoveRuntimeMs)
                : kmbox::KmBoxMgr.Mouse.Move(pixelX, pixelY);
        }

        if (automoveRuntimeMs > 0)
            kmbox::kmBoxBMgr.km_move_auto(pixelX, pixelY, automoveRuntimeMs);
        else
            kmbox::kmBoxBMgr.km_move(pixelX, pixelY);

        return success;
    }

    inline void SendMouseMove(const Vector3& delta, int moveTimeMs = -1) {
        if (moveTimeMs < 0) moveTimeMs = Config::kmboxInputDelayMs;
        const int automoveRuntimeMs = ClampKmboxAutomoveRuntimeMs(moveTimeMs);
        Diagnostics::Aim("mouse.move request delta_rad=(%.9f,%.9f,%.9f) moveTimeMs=%d automoveRuntimeMs=%d enabled=%d deviceType=%d",
            delta.X,
            delta.Y,
            delta.Z,
            moveTimeMs,
            automoveRuntimeMs,
            Config::kmboxEnabled ? 1 : 0,
            Config::kmboxDeviceType);
        if (!Config::kmboxEnabled) {
            static bool once = false;
            if (!once) {
                std::printf("[KMBOX] SendMouseMove called but kmboxEnabled is false!\n");
                once = true;
            }
            Diagnostics::Aim("mouse.move early_return reason=kmbox_disabled");
            return;
        }

        {
            const float baseCountsPerRadian = Config::KmboxBaseCountsPerRadian();
            const float syncScale = Config::KmboxGameSensitivityScale();
            float sensitivity = Config::KmboxYawCountsPerRadian();
            float pitchSensitivity = Config::KmboxPitchCountsPerRadian();

            if (!std::isfinite(sensitivity) || sensitivity <= 0.0f) {
                Diagnostics::Aim("mouse.convert warning invalid_counts_per_rad computed=%.9f fallback=%.9f autoScale=%d gameSens=%.9f effectiveGameSens=%.9f refSens=%.9f pitchCountsPerRad=%.9f",
                    sensitivity,
                    baseCountsPerRadian,
                    Config::autoScaleByGameSensitivity ? 1 : 0,
                    Config::gameMouseSensitivity,
                    Config::EffectiveGameMouseSensitivity(),
                    Config::referenceGameSensitivity,
                    pitchSensitivity);
                sensitivity = baseCountsPerRadian;
                pitchSensitivity = sensitivity;
            }

            static float accumX = 0.0f;
            static float accumY = 0.0f;
            static int callCount = 0;

            // delta.X = pitch (vertical), delta.Y = yaw (horizontal).
            // Positive KMBox X drives the measured yaw negative, so yaw correction is inverted here.
            const float scaledYaw   = -delta.Y * sensitivity;         // yaw -> horizontal X
            const float pitchScale = std::clamp(Config::aimbotPitchScale, 0.1f, 3.0f);
            const float scaledPitch = delta.X * pitchSensitivity * pitchScale; // pitch -> vertical Y
            const float accumBeforeX = accumX;
            const float accumBeforeY = accumY;
            accumX += scaledYaw;
            accumY += scaledPitch;
            int pixelX = static_cast<int>(accumX);
            int pixelY = static_cast<int>(accumY);
            accumX -= static_cast<float>(pixelX);
            accumY -= static_cast<float>(pixelY);

            ++callCount;
            Diagnostics::Aim("mouse.convert call=%d delta_rad_pitch=%.9f delta_rad_yaw=%.9f baseCountsPerRad=%.6f yawCountsPerRad=%.6f pitchCountsPerRad=%.6f autoScale=%d syncScale=%.6f pitchScale=%.6f scaled_counts=(yaw=%.9f,pitch=%.9f) accum_before=(%.9f,%.9f) counts=(%d,%d) accum_after=(%.9f,%.9f)",
                callCount,
                delta.X,
                delta.Y,
                baseCountsPerRadian,
                sensitivity,
                pitchSensitivity,
                Config::autoScaleByGameSensitivity ? 1 : 0,
                syncScale,
                pitchScale,
                scaledYaw,
                scaledPitch,
                accumBeforeX,
                accumBeforeY,
                pixelX,
                pixelY,
                accumX,
                accumY);
            if (callCount <= 50 || pixelX != 0 || pixelY != 0) {
                std::printf("[KMBOX] #%d pitch=%.6f yaw=%.6f yawCountsPerRad=%.1f pitchCountsPerRad=%.1f counts=(%d,%d) accum=(%.3f,%.3f)\n",
                    callCount, delta.X, delta.Y, sensitivity, pitchSensitivity,
                    pixelX, pixelY, accumX, accumY);
            }

            if (pixelX == 0 && pixelY == 0) {
                Diagnostics::Aim("mouse.move early_return reason=zero_counts scaled=(yaw_%.9f,pitch_%.9f) accum_after=(%.9f,%.9f) note=integer_truncation_waiting_for_accumulator",
                    scaledYaw,
                    scaledPitch,
                    accumX,
                    accumY);
                return;
            }

            // ---- Micro-split mouse movement ----
            const int splitBehavior = Config::ClampAimBehaviorIndex(Config::aimBehavior);
            const bool splitEnabled = Config::AimBehaviorMoveSplitEnabled(splitBehavior);
            const int maxPixels = Config::AimBehaviorMoveSplitMaxPixels(splitBehavior);
            const int delayUs = Config::AimBehaviorMoveSplitDelayUs(splitBehavior);
            const int absX = std::abs(pixelX);
            const int absY = std::abs(pixelY);
            const int maxComponent = (std::max)(absX, absY);
            const int steps = splitEnabled
                ? (std::max)(1, (maxComponent + maxPixels - 1) / maxPixels)
                : 1;

            if (steps <= 1) {
                const int status = EnqueueKmboxPixelMove(pixelX, pixelY, automoveRuntimeMs);
                Diagnostics::Aim("mouse.enqueue transport=%s command=%s pixel=(%d,%d) runtimeMs=%d behavior=%d splitEnabled=%d status=%d",
                    KmboxTransportName(),
                    automoveRuntimeMs > 0 ? "automove" : "move",
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    splitBehavior,
                    splitEnabled ? 1 : 0,
                    status);
            } else {
                // Split into micro-movements
                Diagnostics::Aim("mouse.move.split total_pixels=(%d,%d) steps=%d max_pixels_per_step=%d delay_us=%d runtimeMs=%d command=%s behavior=%d",
                    pixelX,
                    pixelY,
                    steps,
                    maxPixels,
                    delayUs,
                    automoveRuntimeMs,
                    automoveRuntimeMs > 0 ? "automove" : "move",
                    splitBehavior);
                int remainingX = pixelX;
                int remainingY = pixelY;
                for (int i = 0; i < steps; i++) {
                    const int curX = remainingX / (steps - i);
                    const int curY = remainingY / (steps - i);
                    remainingX -= curX;
                    remainingY -= curY;

                    const int status = EnqueueKmboxPixelMove(curX, curY, automoveRuntimeMs);
                    Diagnostics::Aim("mouse.enqueue.split transport=%s command=%s step=%d/%d cur=(%d,%d) runtimeMs=%d status=%d",
                        KmboxTransportName(),
                        automoveRuntimeMs > 0 ? "automove" : "move",
                        i + 1,
                        steps,
                        curX,
                        curY,
                        automoveRuntimeMs,
                        status);

                    if (i < steps - 1 && delayUs > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
                    }
                }
                Diagnostics::Aim("mouse.move.split_complete steps=%d total=(%d,%d)",
                    steps, pixelX, pixelY);
            }
            return;
        }
    }

    inline void SendMouseMove(float deltaX, float deltaY, int moveTimeMs = -1) {
        SendMouseMove(Vector3(deltaX, deltaY, 0.0f), moveTimeMs);
    }

    // =========================================================================
    // Sensitivity auto-calibration
    // =========================================================================

    inline float ResolveCalibrationReferenceGameSensitivity(float referenceOverride = 0.0f) {
        if (std::isfinite(referenceOverride) && referenceOverride > 0.0f)
            return referenceOverride;
        if (std::isfinite(Config::gameMouseSensitivity) && Config::gameMouseSensitivity > 0.0f)
            return Config::gameMouseSensitivity;
        return Config::referenceGameSensitivity;
    }

    inline float CalibrateSensitivity(bool calibrateBothAxes = true,
                                      float referenceGameSensitivityOverride = 0.0f) {
        // 1. Set calibration flag
        Config::calibrationInProgress = true;

        // 2. Read initial view direction from remote memory and convert to Euler radians.
        Vector3 angleBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));
        Sleep(Config::calibrationStabilityWaitMs);
        angleBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller)); // re-read for stability

        // 3. Send a known horizontal mouse move (only yaw matters)
        int moveX = Config::calibrationMovePixels;
        int moveY = 0;
        if (Config::kmboxDeviceType == 2)
            kmbox::MockHardwareMgr.RecordMove(moveX, moveY, 0);
        else if (Config::kmboxDeviceType == 0)
            kmbox::KmBoxMgr.Mouse.Move(moveX, moveY);
        else
            kmbox::kmBoxBMgr.km_move(moveX, moveY);

        // 4. Wait for movement to register
        Sleep(Config::calibrationStabilityWaitMs);

        // 5. Read new view direction and convert to Euler radians.
        Vector3 angleAfter = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));

        // 6. Calculate delta in radians (only yaw for horizontal calibration)
        constexpr float kPi = 3.14159265358979323846f;
        float yawDelta = fabsf(angleAfter.Y - angleBefore.Y);
        // Handle angle wrapping (if angles wrap at +/-PI)
        if (yawDelta > kPi) yawDelta = 2.0f * kPi - yawDelta;

        // 7. Calculate KMBox mouse counts per radian and bind it to the current game sensitivity.
        if (yawDelta > 0.0001f) {
            Config::calibratedCountsPerRadian = static_cast<float>(moveX) / yawDelta;
            Config::referenceGameSensitivity =
                ResolveCalibrationReferenceGameSensitivity(referenceGameSensitivityOverride);
        } else {
            std::printf("[CALIBRATE] Yaw delta too small (%.9f rad) -- game may be in menu or not running\n", yawDelta);
            Config::calibrationInProgress = false;
            return 0.0f;
        }

        // 8. Also calibrate vertical (pitch) sensitivity
        if (calibrateBothAxes) {
            Sleep(Config::calibrationStabilityWaitMs);

            Vector3 pitchBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));
            Sleep(Config::calibrationStabilityWaitMs);
            pitchBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));

            int pitchMoveX = 0;
            int pitchMoveY = Config::calibrationMovePixels;
            if (Config::kmboxDeviceType == 2)
                kmbox::MockHardwareMgr.RecordMove(pitchMoveX, pitchMoveY, 0);
            else if (Config::kmboxDeviceType == 0)
                kmbox::KmBoxMgr.Mouse.Move(pitchMoveX, pitchMoveY);
            else
                kmbox::kmBoxBMgr.km_move(pitchMoveX, pitchMoveY);

            Sleep(Config::calibrationStabilityWaitMs);

            Vector3 pitchAfter = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));

            float pitchDelta = fabsf(pitchAfter.X - pitchBefore.X);
            if (pitchDelta > kPi) pitchDelta = 2.0f * kPi - pitchDelta;

            if (pitchDelta > 0.0001f) {
                float pitchRatio = static_cast<float>(pitchMoveY) / pitchDelta;
                // Store separate pitch sensitivity only if >5% different from yaw
                if (Config::calibratedCountsPerRadian > 0.0f) {
                    float ratioDiff = fabsf(pitchRatio - Config::calibratedCountsPerRadian) / Config::calibratedCountsPerRadian;
                    if (ratioDiff > 0.05f) {
                        Config::calibratedPitchCountsPerRadian = pitchRatio;
                        std::printf("[CALIBRATE] Pitch sensitivity differs from yaw by %.1f%% -- storing separate value\n", ratioDiff * 100.0f);
                    } else {
                        Config::calibratedPitchCountsPerRadian = 0.0f;
                    }
                }
            } else {
                std::printf("[CALIBRATE] Pitch delta too small (%.9f rad) -- skipping pitch calibration\n", pitchDelta);
            }
        }

        std::printf("[CALIBRATE] Result: yaw=%.1f counts/rad pitch=%.1f counts/rad refGameSens=%.3f\n",
            Config::calibratedCountsPerRadian,
            Config::calibratedPitchCountsPerRadian > 0.0f ? Config::calibratedPitchCountsPerRadian : Config::calibratedCountsPerRadian,
            Config::referenceGameSensitivity);

        Config::calibrationInProgress = false;
        return Config::calibratedCountsPerRadian;
    }

    inline float RunCalibrationSamples(float referenceGameSensitivityOverride = 0.0f) {
        float total = 0.0f;
        int validSamples = 0;
        int numSamples = (std::max)(Config::calibrationSampleCount, 1);

        for (int i = 0; i < numSamples; ++i) {
            std::printf("[CALIBRATE] Sample %d/%d starting...\n", i + 1, numSamples);
            float result = CalibrateSensitivity(true, referenceGameSensitivityOverride);
            if (result > 0.0f) {
                total += result;
                ++validSamples;
                std::printf("[CALIBRATE] Sample %d/%d complete: %.1f counts/rad\n", i + 1, numSamples, result);
            } else {
                std::printf("[CALIBRATE] Sample %d/%d failed (zero delta)\n", i + 1, numSamples);
            }

            // Brief pause between samples to let view settle
            if (i < numSamples - 1) {
                Sleep(Config::calibrationStabilityWaitMs * 2);
            }
        }

        if (validSamples > 0) {
            float avg = total / static_cast<float>(validSamples);
            Config::calibratedCountsPerRadian = avg;
            Config::referenceGameSensitivity =
                ResolveCalibrationReferenceGameSensitivity(referenceGameSensitivityOverride);
            std::printf("[CALIBRATE] Averaged %d valid samples: %.1f counts/rad refGameSens=%.3f\n",
                validSamples, avg, Config::referenceGameSensitivity);
        } else {
            std::printf("[CALIBRATE] All samples failed -- calibration unsuccessful\n");
            Config::calibratedCountsPerRadian = 0.0f;
        }

        return Config::calibratedCountsPerRadian;
    }

    inline void SendMouseButton(int button, bool down) {
        if (Config::kmboxEnabled) {
            if (Config::kmboxDebugLog) {
                std::printf("[KMBOX] mouse.button button=%d down=%d type=%d\n",
                    button, down ? 1 : 0, Config::kmboxDeviceType);
            }

            if (Config::kmboxDeviceType == 2) {
                kmbox::MockHardwareMgr.RecordButton(button, down);
            } else if (Config::kmboxDeviceType == 0) {
                if (button == 0) kmbox::KmBoxMgr.Mouse.Left(down);
                else if (button == 1) kmbox::KmBoxMgr.Mouse.Right(down);
                else if (button == 2) kmbox::KmBoxMgr.Mouse.Middle(down);
            } else {
                if (button == 0) kmbox::kmBoxBMgr.km_left(down);
                else if (button == 1) kmbox::kmBoxBMgr.km_right(down);
                else if (button == 2) kmbox::kmBoxBMgr.km_middle(down);
            }
            return;
        }
    }

    inline void ForceReleaseMouseButtons() {
        if (!Config::kmboxEnabled)
            return;

        if (Config::kmboxDeviceType == 2) {
            kmbox::MockHardwareMgr.ForceReleaseMouseButtons();
        } else if (Config::kmboxDeviceType == 0) {
            kmbox::KmBoxMgr.ForceReleaseMouseButtons();
        } else {
            kmbox::kmBoxBMgr.km_left(false);
            kmbox::kmBoxBMgr.km_right(false);
            kmbox::kmBoxBMgr.km_middle(false);
        }
    }

    inline void ForceReleaseMouseButton(int button) {
        if (!Config::kmboxEnabled)
            return;

        if (Config::kmboxDeviceType == 2) {
            kmbox::MockHardwareMgr.ForceReleaseMouseButton(button);
        } else if (Config::kmboxDeviceType == 0) {
            kmbox::KmBoxMgr.ForceReleaseMouseButton(button);
        } else {
            switch (button) {
            case 0:
                kmbox::kmBoxBMgr.km_left(false);
                break;
            case 1:
                kmbox::kmBoxBMgr.km_right(false);
                break;
            case 2:
                kmbox::kmBoxBMgr.km_middle(false);
                break;
            default:
                break;
            }
        }
    }

    inline bool SendMouseButtonStateMask(uint32_t stateMask, bool force = false) {
        if (!Config::kmboxEnabled)
            return false;

        if (Config::kmboxDeviceType == 2)
            return kmbox::MockHardwareMgr.SetMouseButtonStateMask(stateMask & 0x7u, force) == success;
        if (Config::kmboxDeviceType != 0)
            return false;
        return kmbox::KmBoxMgr.SetMouseButtonStateMask(stateMask & 0x7u, force) == success;
    }

    inline bool MaskPhysicalMouseButtons(uint32_t mask) {
        if (!Config::kmboxEnabled)
            return false;

        if (Config::kmboxDeviceType == 2)
            return kmbox::MockHardwareMgr.MaskMouse(mask & 0x7Fu) == success;
        if (Config::kmboxDeviceType != 0)
            return false;
        return kmbox::KmBoxMgr.MaskMouse(mask & 0x7Fu) == success;
    }

    inline bool UnmaskPhysicalMouseButtons() {
        if (!Config::kmboxEnabled)
            return false;

        if (Config::kmboxDeviceType == 2)
            return kmbox::MockHardwareMgr.UnmaskAll() == success;
        if (Config::kmboxDeviceType != 0)
            return false;
        return kmbox::KmBoxMgr.UnmaskAll() == success;
    }

    inline bool SendMouseButtonMask(uint32_t keyMask, bool down) {
        if (!Config::kmboxEnabled || keyMask == 0 || (keyMask & ~0x7u) != 0)
            return false;

        bool sent = false;
        if (keyMask & 0x1u) { SendMouseButton(0, down); sent = true; }
        if (keyMask & 0x2u) { SendMouseButton(1, down); sent = true; }
        if (keyMask & 0x4u) { SendMouseButton(2, down); sent = true; }
        return sent;
    }

    inline DWORD HoldDurationMs(float duration) {
        if (duration <= 0.0f)
            return 0;
        return static_cast<DWORD>(duration);
    }

    inline void SetKey(uint32_t key) {
        if (SendMouseButtonMask(key, true)) {
            Sleep(10);
            SendMouseButtonMask(key, false);
        }
    }

    inline int get_bind_id(int setting) {
        return Labels::AimActivationKeyVk(setting);
    }

    inline unsigned int convertToHex(const ImVec4& color) {
        unsigned int red   = static_cast<unsigned int>(color.x * 255.f);
        unsigned int green = static_cast<unsigned int>(color.y * 255.f);
        unsigned int blue  = static_cast<unsigned int>(color.z * 255.f);
        unsigned int alpha = static_cast<unsigned int>(color.w * 255.f);
        return (alpha << 24) | (blue << 16) | (green << 8) | red;
    }

    // =========================================================================
    // Simple math helpers
    // =========================================================================

    inline std::pair<double, double> dd(double a, double b, double c, double d, double m, double n) {
        return std::pair<double, double>(
            (m * d - c * n) / (a * d - c * b),
            (a * n - b * m) / (a * d - c * b)
        );
    }

    // =========================================================================
    // Aim smoothing
    // =========================================================================

    inline Vector3 SmoothLinear(Vector3 LocalAngle, Vector3 TargetAngle, float speed) {
        const float factor = std::isfinite(speed) ? std::clamp(speed, 0.0f, 1.0f) : 0.0f;
        static float lastx = 0;
        static float lasty = 0;
        static float lastz = 0;

        Vector3 Result;
        float deltax = (TargetAngle.X - LocalAngle.X) * factor;
        float deltay = (TargetAngle.Y - LocalAngle.Y) * factor;
        float deltaz = (TargetAngle.Z - LocalAngle.Z) * factor;

        Result.X = LocalAngle.X + deltax;
        Result.Y = LocalAngle.Y + deltay;
        Result.Z = LocalAngle.Z + deltaz;

        if (Config::trackcompensate) {
            Vector3 realresult{
                LocalAngle.X + TargetAngle.X - lastx,
                LocalAngle.Y + TargetAngle.Y - lasty,
                LocalAngle.Z + TargetAngle.Z - lastz
            };
            if (Result.DistTo(realresult) <= Config::comarea) {
                if (fabsf(deltax) < fabsf(TargetAngle.X - lastx))
                    deltax = (TargetAngle.X - lastx) * Config::comspeed;
                if (fabsf(deltaz) < fabsf(TargetAngle.Z - lastz))
                    deltaz = (TargetAngle.Z - lastz) * Config::comspeed;
            }
            lastx = TargetAngle.X;
            lasty = TargetAngle.Y;
            lastz = TargetAngle.Z;

            Result.X = LocalAngle.X + deltax;
            Result.Y = LocalAngle.Y + deltay;
            Result.Z = LocalAngle.Z + deltaz;
        }
        return Result;
    }

    inline Vector3 SmoothAccelerate(Vector3 LocalAngle, Vector3 TargetAngle, float Speed, float Acc) {
        Vector3 Result = LocalAngle;
        __try {
            Vector3 delta = TargetAngle - LocalAngle;
            float tmp = Acc / delta.get_length();
            tmp = tmp * tmp * 0.005f;
            float c = Speed + tmp * Speed;
            if (c >= 1.f) c = 1.f;
            Result.X += delta.X * c;
            Result.Y += delta.Y * c;
            Result.Z += delta.Z * c;
            return Result;
        } __except (1) {
            return Result;
        }
    }

    // =========================================================================
    // Key hold helpers
    // =========================================================================

    inline void SetKeyscopeHold(int Key, float duration) {
        const uint32_t keyMask = static_cast<uint32_t>(Key + 2);
        if (!SendMouseButtonMask(keyMask, true))
            return;
        Sleep(HoldDurationMs(duration));
        SendMouseButtonMask(keyMask, false);
    }

    inline void SetKeyHold(int Key, float duration) {
        const uint32_t keyMask = static_cast<uint32_t>(Key);
        if (!SendMouseButtonMask(keyMask, true))
            return;
        Sleep(HoldDurationMs(duration));
        SendMouseButtonMask(keyMask, false);
    }

    // =========================================================================
    // Projectile prediction (quartic solver)
    // =========================================================================

    inline void SolveQuartic(const std::complex<float> coefficients[5], std::complex<float> roots[4]) {
        const std::complex<float> a = coefficients[4];
        const std::complex<float> b = coefficients[3] / a;
        const std::complex<float> c = coefficients[2] / a;
        const std::complex<float> d = coefficients[1] / a;
        const std::complex<float> e = coefficients[0] / a;

        const std::complex<float> Q1 = c * c - 3.f * b * d + 12.f * e;
        const std::complex<float> Q2 = 2.f * c * c * c - 9.f * b * c * d + 27.f * d * d + 27.f * b * b * e - 72.f * c * e;
        const std::complex<float> Q3 = 8.f * b * c - 16.f * d - 2.f * b * b * b;
        const std::complex<float> Q4 = 3.f * b * b - 8.f * c;

        const std::complex<float> Q5 = std::pow(Q2 / 2.f + std::sqrt(Q2 * Q2 / 4.f - Q1 * Q1 * Q1), 1.f / 3.f);
        const std::complex<float> Q6 = (Q1 / Q5 + Q5) / 3.f;
        const std::complex<float> Q7 = 2.f * std::sqrt(Q4 / 12.f + Q6);

        roots[0] = (-b - Q7 - std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 - Q3 / Q7)) / 4.f;
        roots[1] = (-b - Q7 + std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 - Q3 / Q7)) / 4.f;
        roots[2] = (-b + Q7 - std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 + Q3 / Q7)) / 4.f;
        roots[3] = (-b + Q7 + std::sqrt(4.f * Q4 / 6.f - 4.f * Q6 + Q3 / Q7)) / 4.f;
    }

    inline void AimCorrection(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed, bool gravityEnabled) {
        double G = 9.8;
        const Vector3 camera = SnapshotCameraPosition();
        double A = camera.X;
        double B = camera.Y;
        double C = camera.Z;
        double M = InVecArg->X;
        double N = InVecArg->Y;
        double O = InVecArg->Z;
        double P = currVelocity.X;
        double Q = currVelocity.Y;
        double R = currVelocity.Z;
        double S = Bulletspeed;
        double H = M - A;
        double J = O - C;
        double K = N - B;
        double L = -0.5 * G;
        double c4 = L * L;
        double c3 = -2.0 * Q * L;
        double c2 = (Q * Q) - (2.0 * K * L) - (S * S) + (P * P) + (R * R);
        double c1 = (2.0 * K * Q) + (2.0 * H * P) + (2.0 * J * R);
        double c0 = (K * K) + (H * H) + (J * J);

        std::complex<float> pOutRoots[4];
        const std::complex<float> pInCoeffs[5] = { (float)c0, (float)c1, (float)c2, (float)c3, (float)c4 };
        SolveQuartic(pInCoeffs, pOutRoots);
        float fBestRoot = FLT_MAX;
        for (int i = 0; i < 4; i++) {
            if (pOutRoots[i].real() > 0.f && std::abs(pOutRoots[i].imag()) < 0.0001f && pOutRoots[i].real() < fBestRoot) {
                fBestRoot = pOutRoots[i].real();
            }
        }
        InVecArg->X = (float)(camera.X + (H + P * fBestRoot));
        if (gravityEnabled) {
            InVecArg->Y = (float)(camera.Y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(camera.Z + (J + R * fBestRoot));
    }

    inline void AimCorrection22(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed, bool gravityEnabled) {
        double G = 9.8;
        const Vector3 camera = SnapshotCameraPosition();
        double A = camera.X;
        double B = camera.Y;
        double C = camera.Z;
        double M = InVecArg->X;
        double N = InVecArg->Y;
        double O = InVecArg->Z;
        double P = currVelocity.X;
        double Q = currVelocity.Y;
        double R = currVelocity.Z;
        double S = Bulletspeed;
        double H = M - A;
        double J = O - C;
        double K = N - B;
        double L = -0.5 * G;
        double c4 = L * L;
        double c3 = -2.0 * Q * L;
        double c2 = (Q * Q) - (2.0 * K * L) - (S * S) + (P * P) + (R * R);
        double c1 = (2.0 * K * Q) + (2.0 * H * P) + (2.0 * J * R);
        double c0 = (K * K) + (H * H) + (J * J);

        std::complex<float> pOutRoots[4];
        const std::complex<float> pInCoeffs[5] = { (float)c0, (float)c1, (float)c2, (float)c3, (float)c4 };
        SolveQuartic(pInCoeffs, pOutRoots);
        float fBestRoot = FLT_MAX;
        for (int i = 0; i < 4; i++) {
            if (pOutRoots[i].real() > 0.f && std::abs(pOutRoots[i].imag()) < 0.0001f && pOutRoots[i].real() < fBestRoot) {
                fBestRoot = pOutRoots[i].real();
            }
        }
        InVecArg->X = (float)(camera.X + (H + P * fBestRoot));
        if (gravityEnabled) {
            InVecArg->Y = (float)(camera.Y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(camera.Z + (J + R * fBestRoot));
    }

    namespace TargetingDetail {

        constexpr float kNoTargetScore = 100000.f;

        struct SelectionResult {
            int index = -1;
            Vector3 target{};
            c_entity entity{};
            float score = kNoTargetScore;
        };

        struct VelocitySample {
            Vector3 velocity{};
            DWORD tick = 0;
            bool initialized = false;
        };

        inline std::mutex velocity_history_mutex;
        inline std::unordered_map<uint64_t, VelocitySample> velocity_history;

        struct TargetLockRuntime {
            uint64_t entityKey = 0;
            int entityIndex = -1;
            DWORD lockStartedTick = 0;
            DWORD lastSeenTick = 0;
            float lastScore = kNoTargetScore;
            bool active = false;
        };

        inline std::mutex target_lock_mutex;
        inline TargetLockRuntime target_lock_runtime;

        inline bool IsZeroVector(const Vector3& value) {
            return value == Vector3(0, 0, 0);
        }

        inline Vector2 CrosshairCenter() {
            float width = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
            float height = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
            if (WX > 0.0f && WY > 0.0f) {
                width = WX;
                height = WY;
            } else if (Config::manualScreenWidth > 0 && Config::manualScreenHeight > 0) {
                width = static_cast<float>(Config::manualScreenWidth);
                height = static_cast<float>(Config::manualScreenHeight);
            }
            return Vector2(
                width * 0.5f,
                height * 0.5f
            );
        }

        inline Vector3 CameraPosition() {
            return OW::SnapshotCameraPosition();
        }

        struct FovRuntimeContext {
            Vector3 camera{};
            Vector3 forward{};
            bool valid = false;
        };

        inline bool IsFiniteVector(const Vector3& value) {
            return std::isfinite(value.X) &&
                   std::isfinite(value.Y) &&
                   std::isfinite(value.Z);
        }

        inline Vector3 NormalizeVector(const Vector3& value) {
            const float length = value.Size();
            if (!IsFiniteVector(value) || length <= 0.0001f)
                return Vector3{};
            return value / length;
        }

        inline FovRuntimeContext SnapshotFovRuntimeContext() {
            Matrix view{}, viewXor{};
            SnapshotViewMatrices(view, viewXor);

            const auto camera = viewXor.get_location();
            const auto forward = viewXor.get_rotation();

            FovRuntimeContext context{};
            context.camera = Vector3(camera.x, camera.y, camera.z);
            context.forward = NormalizeVector(Vector3(forward.x, forward.y, forward.z));
            context.valid = IsFiniteVector(context.camera) &&
                            !IsZeroVector(context.camera) &&
                            !IsZeroVector(context.forward);
            return context;
        }

        inline float FovAngleLimitDeg(float fovDeg) {
            return Config::ClampFovDeg(fovDeg);
        }

        inline float AngularSeparationDegFromCamera(const Vector3& camera,
                                                    const Vector3& a,
                                                    const Vector3& b) {
            const Vector3 dirA = NormalizeVector(a - camera);
            const Vector3 dirB = NormalizeVector(b - camera);
            if (IsZeroVector(dirA) || IsZeroVector(dirB))
                return (std::numeric_limits<float>::max)();

            const float dot = std::clamp(dirA | dirB, -1.0f, 1.0f);
            return RAD2DEG(std::acos(dot));
        }

        inline float FovScoreDeg(const FovRuntimeContext& context, const Vector3& position) {
            if (!context.valid)
                return (std::numeric_limits<float>::max)();

            const Vector3 targetDir = NormalizeVector(position - context.camera);
            if (IsZeroVector(targetDir))
                return (std::numeric_limits<float>::max)();

            const float dot = std::clamp(context.forward | targetDir, -1.0f, 1.0f);
            return RAD2DEG(std::acos(dot));
        }

        inline bool IsWithinFovDeg(const FovRuntimeContext& context,
                                   const Vector3& position,
                                   float fovDeg,
                                   float* outScoreDeg = nullptr) {
            const float scoreDeg = FovScoreDeg(context, position);
            if (outScoreDeg)
                *outScoreDeg = scoreDeg;
            return scoreDeg <= FovAngleLimitDeg(fovDeg) + 0.0001f;
        }

        inline bool IsTrainingBot(uint64_t heroId) {
            return GameData::IsTrainingBotHeroId(heroId);
        }

        inline bool IsSpecialAimEntity(uint64_t heroId) {
            return heroId == 0x16dd || heroId == 0x16ee;
        }

        inline std::vector<c_entity> SnapshotEntities() {
            std::lock_guard<std::mutex> lock(::g_mutex);
            return entities;
        }

        inline std::vector<hpanddy> SnapshotDynamicEntities() {
            std::lock_guard<std::mutex> lock(::g_mutex);
            return hp_dy_entities;
        }

        inline c_entity SnapshotLocalEntity() {
            return OW::SnapshotLocalEntity();
        }

        inline bool IsValidIndex(int index, size_t size) {
            return index >= 0 && static_cast<size_t>(index) < size;
        }

        inline bool IsRuntimeTargetValid(const c_entity& entity, bool requireVisible = true) {
            if (entity.roster_state != EntityRosterState::Fresh) return false;
            if (!entity.address || !entity.Alive) return false;
            if (requireVisible && !entity.Vis) return false;
            return true;
        }

        inline bool TryEntityAt(int index, c_entity& entity, bool requireVisible = false) {
            const auto snapshot = SnapshotEntities();
            if (!IsValidIndex(index, snapshot.size())) return false;
            entity = snapshot[static_cast<size_t>(index)];
            return IsRuntimeTargetValid(entity, requireVisible);
        }

        inline bool TargetTeamMatches(const c_entity& entity, int teamMode, const c_entity& local) {
            if (entity.address == local.address)
                return false;

            switch (teamMode) {
            case 0:  return entity.Team;  // Enemies
            case 1:  return !entity.Team; // Allies
            case 2:  return true;         // All
            default: return entity.Team;
            }
        }

        inline bool IsSelectableCandidate(const c_entity& entity, int teamMode, const c_entity& local) {
            return IsRuntimeTargetValid(entity, false) && TargetTeamMatches(entity, teamMode, local);
        }

        inline Vector3 ConfiguredBonePosition(c_entity& entity, int boneSetting) {
            if (boneSetting == 1) return entity.head_pos;
            if (boneSetting == 2) return entity.neck_pos;
            return entity.chest_pos;
        }

        inline int ResolveAimBoneForDistance(int configuredBone, float distance) {
            const int normalized = Config::NormalizeAimBone(configuredBone);
            const bool headRequested = normalized == Config::kAimBoneHead;
            const float headGate = std::clamp(Config::aimbotMaxHead, 0.0f, 500.0f);
            if (headRequested && headGate > 0.0f && distance > headGate)
                return Config::kAimBoneNeck;
            return normalized;
        }

        inline bool DistancePassesAimFilter(float distance) {
            const float minDistance = std::clamp(Config::aimbotMinDist, 0.0f, 500.0f);
            const float maxDistance = std::clamp(Config::aimbotMaxDist, 0.0f, 500.0f);
            if (minDistance > 0.0f && distance < minDistance)
                return false;
            if (maxDistance > 0.0f && distance > maxDistance)
                return false;
            return true;
        }

        inline DWORD ResolvePolicySliderMs(float value) {
            return static_cast<DWORD>(std::clamp(value, 0.0f, 5000.0f) * 10.0f);
        }

        inline DWORD ResolveAimSessionTimeoutMs() {
            const float value = std::clamp(Config::aimbotMaxAim, 0.0f, 100.0f);
            if (value <= 0.0f || value >= 100.0f)
                return 0;
            return static_cast<DWORD>(value * 20.0f);
        }

        inline TargetLockPolicy ResolveTargetLockPolicy() {
            TargetLockPolicy policy{};
            policy.traceMode = ClampTraceMode(Config::aimbotTrace);
            policy.unlockMode = ClampUnlockMode(Config::aimbotUnlock);
            policy.minLockMs = static_cast<float>(ResolvePolicySliderMs(Config::aimbotLockTime));
            policy.maxLockMs = static_cast<float>(ResolveAimSessionTimeoutMs());
            policy.retargetHysteresis = std::clamp(Config::aimbotStickiness, 0.0f, 100.0f);
            return policy;
        }

        inline TargetLockRuntime SnapshotTargetLockRuntime() {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            return target_lock_runtime;
        }

        inline void ResetTargetLockRuntime() {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            target_lock_runtime = TargetLockRuntime{};
        }

        inline bool TargetLockExpired(const TargetLockRuntime& state,
                                      const TargetLockPolicy& policy,
                                      DWORD now) {
            if (!state.active || state.entityKey == 0)
                return true;
            if (policy.maxLockMs <= 0.0f)
                return false;
            return now - state.lockStartedTick >= static_cast<DWORD>(policy.maxLockMs);
        }

        inline bool CandidateCanBypassTrace(const TargetLockPolicy& policy,
                                            const TargetLockRuntime& state,
                                            uint64_t candidateKey) {
            if (policy.traceMode == TraceMode::Off)
                return true;
            if (policy.traceMode == TraceMode::Relaxed &&
                state.active &&
                candidateKey != 0 &&
                candidateKey == state.entityKey) {
                return true;
            }
            return false;
        }

        inline bool CandidateBlockedByMinLock(const TargetLockPolicy& policy,
                                              const TargetLockRuntime& state,
                                              uint64_t candidateKey,
                                              DWORD now) {
            if (!state.active || state.entityKey == 0 || candidateKey == 0 ||
                candidateKey == state.entityKey) {
                return false;
            }
            const DWORD minLockMs = static_cast<DWORD>(policy.minLockMs);
            return minLockMs > 0 && now - state.lockStartedTick < minLockMs;
        }

        inline float ApplyRetargetHysteresis(float score,
                                             const TargetLockPolicy& policy,
                                             const TargetLockRuntime& state,
                                             uint64_t candidateKey) {
            if (!state.active || state.entityKey == 0 || candidateKey == state.entityKey)
                return score;
            const float multiplier = 1.0f + std::clamp(policy.retargetHysteresis, 0.0f, 100.0f) * 0.01f;
            return score * multiplier;
        }

        inline void CommitTargetLockRuntime(const TargetCandidate& candidate,
                                            float selectedScore,
                                            const TargetLockPolicy& policy,
                                            DWORD now) {
            std::lock_guard<std::mutex> lock(target_lock_mutex);
            if (!candidate.valid || candidate.entityKey == 0) {
                if (policy.unlockMode == UnlockMode::Anytime ||
                    (target_lock_runtime.active && TargetLockExpired(target_lock_runtime, policy, now))) {
                    target_lock_runtime = TargetLockRuntime{};
                }
                return;
            }

            if (!target_lock_runtime.active ||
                target_lock_runtime.entityKey != candidate.entityKey ||
                TargetLockExpired(target_lock_runtime, policy, now)) {
                target_lock_runtime.entityKey = candidate.entityKey;
                target_lock_runtime.entityIndex = candidate.entityIndex;
                target_lock_runtime.lockStartedTick = now;
                target_lock_runtime.active = true;
            }

            target_lock_runtime.lastSeenTick = now;
            target_lock_runtime.lastScore = selectedScore;
            target_lock_runtime.entityIndex = candidate.entityIndex;
        }

        inline EntityMotionState EstimateMotionState(const c_entity& entity, const Vector2& screenPoint) {
            (void)screenPoint;

            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const Vector3 velocity = motion.valid ? motion.effectiveVelocity : entity.velocity;

            EntityMotionState state{};
            state.worldVelocity = velocity;
            state.verticalVelocity = velocity.Y;

            const float horizontalSpeed = sqrtf(
                velocity.X * velocity.X +
                velocity.Z * velocity.Z);
            const float verticalSpeed = velocity.Y;

            if (!std::isfinite(horizontalSpeed) || !std::isfinite(verticalSpeed) ||
                horizontalSpeed > 250.0f || fabsf(verticalSpeed) > 250.0f) {
                state.kind = EntityMotionState::Kind::TeleportOrInvalid;
                state.confidence = 0.25f;
                return state;
            }

            if (verticalSpeed > 1.0f) {
                state.kind = EntityMotionState::Kind::AirborneRising;
                state.confidence = 0.55f;
            } else if (verticalSpeed < -1.0f) {
                state.kind = EntityMotionState::Kind::AirborneFalling;
                state.confidence = 0.55f;
            } else if (horizontalSpeed > 2.0f) {
                state.kind = EntityMotionState::Kind::Strafing;
                state.confidence = motion.usedWorldDeltaFallback ? 0.50f : 0.45f;
            } else {
                state.kind = EntityMotionState::Kind::Grounded;
                state.confidence = 0.35f;
            }

            return state;
        }

        inline Vector3 ClampMagnitude(const Vector3& value, float maxLength) {
            const float length = value.get_length();
            if (length <= maxLength) return value;
            return value * (maxLength / length);
        }

        inline Vector3 AccelerationAwareVelocity(const c_entity& entity, float distance, float projectileSpeed) {
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            const Vector3 baseVelocity = motion.valid ? motion.effectiveVelocity : entity.velocity;
            const uint64_t key = entity.address ? entity.address : entity.LinkBase;
            if (!key) return baseVelocity;

            const DWORD now = entity.render_sample_tick_ms
                ? static_cast<DWORD>(entity.render_sample_tick_ms)
                : GetTickCount();
            const float safeSpeed = (std::max)(projectileSpeed, 1.0f);
            const float leadTime = std::clamp(distance / safeSpeed, 0.0f, 1.0f);
            Vector3 adjusted = baseVelocity;

            std::lock_guard<std::mutex> lock(velocity_history_mutex);
            if (velocity_history.size() > 256) velocity_history.clear();

            VelocitySample& sample = velocity_history[key];
            if (sample.initialized && now > sample.tick) {
                const float dt = static_cast<float>(now - sample.tick) / 1000.0f;
                if (dt > 0.001f && dt < 0.5f) {
                    Vector3 acceleration = (baseVelocity - sample.velocity) / dt;
                    acceleration = ClampMagnitude(acceleration, 250.0f);
                    adjusted += acceleration * std::clamp(leadTime * 0.5f, 0.0f, 0.25f);
                }
            }

            sample.velocity = baseVelocity;
            sample.tick = now;
            sample.initialized = true;
            return adjusted;
        }

        inline ProjectileRuntimeSpec ResolveProjectileRuntimeSpec(const WeaponSpec* weapon,
                                                                  const c_entity& local,
                                                                  bool secondary) {
            if (Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO) {
                Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
                if (local.skill2act) Config::predit_level = 110.f;
            }

            const float fallbackSpeed = Config::predit_level;
            const bool fallbackGravity = Config::projectile_arc ||
                local.HeroID == eHero::HERO_HANJO ||
                Config::Gravitypredit;

            if (!secondary &&
                Config::hanzoautospeed &&
                local.HeroID == eHero::HERO_HANJO &&
                fallbackSpeed > 0.0f) {
                return ProjectileRuntimeSpec{
                    fallbackSpeed,
                    weapon ? weapon->projectile.gravity : true,
                    weapon != nullptr
                };
            }

            if (weapon && weapon->projectile.projectileSpeed > 0.0f) {
                return ProjectileRuntimeSpec{
                    weapon->projectile.projectileSpeed,
                    weapon->projectile.gravity,
                    true
                };
            }

            return ProjectileRuntimeSpec{
                fallbackSpeed,
                fallbackGravity,
                false
            };
        }

        inline Vector3 SnapshotLocalAimAngleForLeadEstimate() {
            const uint64_t playerControllerBase = SDK ? SDK->g_player_controller : 0;
            if (playerControllerBase) {
                const Vector3 viewDir = ReadPlayerControllerViewDirection(playerControllerBase);
                const float dirLen = viewDir.Size();
                if (std::isfinite(viewDir.X) &&
                    std::isfinite(viewDir.Y) &&
                    std::isfinite(viewDir.Z) &&
                    dirLen > 0.5f &&
                    dirLen < 1.5f) {
                    return DirectionToAimEuler(viewDir);
                }
            }

            Matrix view{}, viewXor{};
            SnapshotViewMatrices(view, viewXor);
            const XMFLOAT3 forward = viewXor.get_rotation();
            const float forwardLen = sqrtf(
                forward.x * forward.x +
                forward.y * forward.y +
                forward.z * forward.z);
            if (std::isfinite(forward.x) &&
                std::isfinite(forward.y) &&
                std::isfinite(forward.z) &&
                forwardLen > 0.5f &&
                forwardLen < 1.5f) {
                return DirectionToAimEuler(Vector3(forward.x, forward.y, forward.z));
            }

            return Vector3{};
        }

        inline Vector3 AimAngleForWorldPoint(const Vector3& worldPoint) {
            const Vector3 camera = CameraPosition();
            const Vector3 delta = worldPoint - camera;
            const float distance = delta.Size();
            if (!std::isfinite(distance) || distance <= 0.0001f)
                return Vector3{};
            return DirectionToAimEuler(delta / distance);
        }

        inline int ResolveLeadAimMethod(bool secondary) {
            if (secondary) {
                return Config::SecondaryAimMethod(Config::Flick2 ? 1 : 0);
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::aimBehavior);
            return Config::AimBehaviorMethod(behavior);
        }

        inline float ResolveLeadSmoothInput(bool secondary) {
            if (secondary) {
                if (Config::Flick2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorFlick, Config::Flick_smooth2);
                if (Config::Tracking2)
                    return Config::AimBehaviorSmoothInput(Config::kAimBehaviorTracking, Config::Tracking_smooth2);
                return 1.0f;
            }

            const int behavior = Config::ClampAimBehaviorIndex(Config::aimBehavior);
            const float scalePercent = Config::IsFlickBehavior(behavior)
                ? Config::Flick_smooth
                : Config::Tracking_smooth;
            return Config::AimBehaviorSmoothInput(behavior, scalePercent);
        }

        inline LeadTimingEstimate EstimateLeadTimingForAimPoint(const Vector3& rawAimPoint, bool secondary) {
            const int method = ResolveLeadAimMethod(secondary);
            AimSettleEstimateInput input{};
            input.localAngle = SnapshotLocalAimAngleForLeadEstimate();
            input.targetAngle = AimAngleForWorldPoint(rawAimPoint);
            input.method = method;
            input.slotSpeedScale = ResolveLeadSmoothInput(secondary);
            input.methodSpeedScale = Config::RuntimeAimMethodAngularSpeedScale(method);
            input.constantAngularSpeedDeg = Config::RuntimeAimConstantAngularSpeedDeg();
            input.frameSeconds = kLeadDefaultFrameSeconds;

            return BuildLeadTiming(
                EstimateAimSettleTimeMs(input),
                Config::kmboxInputDelayMs);
        }

        inline const char* MotionVelocitySourceName(Motion::VelocitySource source) {
            switch (source) {
            case Motion::VelocitySource::Reported:
                return "reported";
            case Motion::VelocitySource::WorldDeltaFallback:
                return "world_delta";
            case Motion::VelocitySource::None:
            default:
                return "none";
            }
        }

        inline LeadPredictionResult ResolveLeadPrediction(c_entity& entity,
                                                          Vector3 rawAimPoint,
                                                          const ProjectileRuntimeSpec& projectile,
                                                          bool predictionEnabled,
                                                          bool secondary,
                                                          float extraPreFireDelayMs = 0.0f) {
            LeadPredictionResult result{};
            result.predictionEnabled = predictionEnabled;
            result.secondary = secondary;
            result.rawAimPoint = rawAimPoint;
            result.preFireAimPoint = rawAimPoint;
            result.finalAimPoint = rawAimPoint;
            result.projectile = projectile;

            if (!predictionEnabled || IsZeroVector(rawAimPoint))
                return result;

            const Vector3 camera = CameraPosition();
            result.distance = camera.DistTo(rawAimPoint);
            const Motion::EntityMotionEstimate motion = Motion::EstimateEntityMotion(entity);
            result.targetVelocity = AccelerationAwareVelocity(entity, result.distance, projectile.projectileSpeed);
            result.timing = EstimateLeadTimingForAimPoint(rawAimPoint, secondary);
            const float preFireMaxMs = extraPreFireDelayMs > 0.0f ? 1000.0f : kLeadMaxPreFireDelayMs;
            result.timing.preFireDelayMs = ClampLeadDelayMs(
                result.timing.preFireDelayMs + extraPreFireDelayMs,
                preFireMaxMs);
            result.preFireAimPoint = ApplyTargetMotionPreFireDelay(
                rawAimPoint,
                result.targetVelocity,
                result.timing.preFireDelayMs,
                preFireMaxMs);
            result.finalAimPoint = result.preFireAimPoint;

            if (projectile.projectileSpeed > 0.0f) {
                const float leadDistance = camera.DistTo(result.preFireAimPoint);
                if (secondary) {
                    AimCorrection22(
                        &result.finalAimPoint,
                        result.targetVelocity,
                        leadDistance,
                        projectile.projectileSpeed,
                        projectile.gravity);
                } else {
                    AimCorrection(
                        &result.finalAimPoint,
                        result.targetVelocity,
                        leadDistance,
                        projectile.projectileSpeed,
                        projectile.gravity);
                }
            }

            Diagnostics::Aim("lead.solve enabled=%d secondary=%d projectile(source=%s speed=%.3f gravity=%d) motion(source=%s confidence=%.3f velocity=(%.3f,%.3f,%.3f)) timing(settleMs=%.3f inputMs=%d extraPreFireMs=%.3f preFireMs=%.3f) raw=(%.3f,%.3f,%.3f) preFire=(%.3f,%.3f,%.3f) final=(%.3f,%.3f,%.3f)",
                predictionEnabled ? 1 : 0,
                secondary ? 1 : 0,
                projectile.fromWeaponSpec ? "weapon" : "legacy",
                projectile.projectileSpeed,
                projectile.gravity ? 1 : 0,
                MotionVelocitySourceName(motion.velocitySource),
                motion.confidence,
                result.targetVelocity.X,
                result.targetVelocity.Y,
                result.targetVelocity.Z,
                result.timing.estimatedSettleMs,
                result.timing.inputDelayMs,
                extraPreFireDelayMs,
                result.timing.preFireDelayMs,
                result.rawAimPoint.X,
                result.rawAimPoint.Y,
                result.rawAimPoint.Z,
                result.preFireAimPoint.X,
                result.preFireAimPoint.Y,
                result.preFireAimPoint.Z,
                result.finalAimPoint.X,
                result.finalAimPoint.Y,
                result.finalAimPoint.Z);

            return result;
        }

        inline Vector3 ApplyPrediction(c_entity& entity, Vector3 position, bool predit, bool secondary) {
            if (IsZeroVector(position)) return position;

            const c_entity local = SnapshotLocalEntity();
            const WeaponSpec* weapon = ResolveWeaponSpec(local.HeroID, Config::aimbotAttack);
            const bool resolvedPrediction = ResolvePredictionEnabled(
                ClampPredictionOverride(Config::aimbotPredictionMode),
                weapon,
                predit);
            if (!resolvedPrediction) return position;

            const ProjectileRuntimeSpec projectile = ResolveProjectileRuntimeSpec(weapon, local, secondary);
            return ResolveLeadPrediction(entity, position, projectile, true, secondary).finalAimPoint;
        }

        inline float CandidateScore(const Vector3& position, const FovRuntimeContext& fovContext) {
            return FovScoreDeg(fovContext, position);
        }

        inline SelectionResult SelectTargetFromSnapshot(const std::vector<c_entity>& snapshot,
                                                        const c_entity& local,
                                                        bool predit,
                                                        bool secondary,
                                                        int teamMode,
                                                        int boneSetting,
                                                        float fov) {
            SelectionResult result{};
            const FovRuntimeContext fovContext = SnapshotFovRuntimeContext();

            for (size_t i = 0; i < snapshot.size(); ++i) {
                c_entity entity = snapshot[i];
                if (!IsSelectableCandidate(entity, teamMode, local)) continue;

                Vector3 rootPosition = ConfiguredBonePosition(entity, boneSetting);
                if (IsZeroVector(rootPosition)) continue;

                Vector3 aimPosition = ApplyPrediction(entity, rootPosition, predit, secondary);
                const float score = CandidateScore(aimPosition, fovContext);
                if (score >= result.score) continue;
                if (score > FovAngleLimitDeg(fov)) continue;

                result.index = static_cast<int>(i);
                result.target = aimPosition;
                result.entity = entity;
                result.score = score;
            }

            return result;
        }

        inline Vector3 SelectAutoBone(c_entity entity, const Vector2& crosshair, bool predit, bool secondary, size_t maxSkeletonBones) {
            float bestDistance = (std::numeric_limits<float>::max)();
            Vector3 bestRoot{};
            const Matrix view = OW::SnapshotViewMatrix();
            const FovRuntimeContext fovContext = SnapshotFovRuntimeContext();

            if (IsTrainingBot(entity.HeroID)) {
                const int botBones[] = { 17, 16, 3, 13, 54 };
                for (int bone : botBones) {
                    Vector3 bonePosition = entity.GetBonePos(bone);
                    if (IsZeroVector(bonePosition) || bonePosition == entity.pos) continue;
                    const float distance = fovContext.valid
                        ? FovScoreDeg(fovContext, bonePosition)
                        : crosshair.Distance(view.WorldToScreen(bonePosition));
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestRoot = bonePosition;
                    }
                }
            } else {
                const auto skeleton = entity.GetSkel();
                const size_t limit = (std::min)(maxSkeletonBones, skeleton.size());
                for (size_t i = 0; i < limit; ++i) {
                    Vector3 bonePosition = entity.GetBonePos(skeleton[i]);
                    if (IsZeroVector(bonePosition) || bonePosition == entity.pos) continue;
                    const float distance = fovContext.valid
                        ? FovScoreDeg(fovContext, bonePosition)
                        : crosshair.Distance(view.WorldToScreen(bonePosition));
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        bestRoot = bonePosition;
                    }
                }
            }

            return ApplyPrediction(entity, bestRoot, predit, secondary);
        }

        inline void ApplyTargetOutline(const c_entity& entity, bool secondary) {
            (void)entity;
            (void)secondary;
        }

        inline void UpdateHanzoAutoSpeed(const c_entity& local) {
            if (!Config::hanzoautospeed || local.HeroID != eHero::HERO_HANJO) return;
            Config::predit_level = readult(local.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
            if (local.skill2act) Config::predit_level = 110.f;
        }
    }

    inline bool IsValidTargetIndex(int index) {
        c_entity target{};
        return TargetingDetail::TryEntityAt(index, target, false);
    }

    inline bool TryGetTargetEntity(int index, c_entity& entity, bool requireVisible = false) {
        return TargetingDetail::TryEntityAt(index, entity, requireVisible);
    }

    // =========================================================================
    // Angle helpers
    // =========================================================================

    inline bool in_range(Vector3 MyAngle, Vector3 EnemyAngle, Vector3 MyPosition, Vector3 EnemyPosition, float radius) {
        float dist = MyPosition.DistTo(EnemyPosition);
        radius /= dist;
        return MyAngle.DistTo(EnemyAngle) <= radius;
    }

    inline XMFLOAT3 CalcAngle(XMFLOAT3 Target, XMFLOAT3 CameraPos) {
        XMFLOAT3 Result;
        float Distance = XMVectorGetX(XMVector3Length(XMLoadFloat3(&Target) - XMLoadFloat3(&CameraPos)));
        if (Distance < 0.0001f) Distance = 0.0001f;
        XMStoreFloat3(&Result, (XMLoadFloat3(&Target) - XMLoadFloat3(&CameraPos)) / Distance);
        return Result;
    }

    // =========================================================================
    // Skill lookup (hanzo projectile speed)
    // =========================================================================

    inline float GetLookUpSkill(uint16_t a1) {
        const uint64_t localSkillBase = SnapshotLocalSkillBase();
        if (!localSkillBase)
            return 0.f;

        __try {
            uint64_t pSkill = SDK->RPM<uint64_t>(localSkillBase + 0x1848);
            uint64_t SkillRawList = SDK->RPM<uint64_t>(pSkill + 0x10);
            uint32_t SkillSize = SDK->RPM<uint32_t>(pSkill + 0x18);
            for (uint32_t i = 0; i < SkillSize; i++) {
                if (SDK->RPM<uint16_t>(SkillRawList + (i * 0x80)) == a1) {
                    float val = SDK->RPM<float>(SkillRawList + (i * 0x80) + 0x30);
                    if (val >= 0.5f) return val;
                }
            }
        } __except (1) {}
        return 0.f;
    }

    // =========================================================================
    // Main target selection (GetVector3)
    // =========================================================================

    inline TargetCandidate AcquireTarget(bool predit = false, bool ignoreInvisible = Config::aimbotIgnoreInvisible) {
        TargetCandidate best{};
        int TarGetIndex = -1;
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        auto entities = TargetingDetail::SnapshotEntities();
        auto hp_dy_entities = TargetingDetail::SnapshotDynamicEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
        const TargetLockPolicy lockPolicy = TargetingDetail::ResolveTargetLockPolicy();
        const DWORD now = GetTickCount();
        TargetingDetail::TargetLockRuntime activeLock = TargetingDetail::SnapshotTargetLockRuntime();
        if (TargetingDetail::TargetLockExpired(activeLock, lockPolicy, now))
            activeLock.active = false;
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::aimbotPredictionMode),
            weaponSpec,
            predit);

        float origin = 100000.f;
        size_t selectableCandidates = 0;
        float selectedFovScoreDeg = 0.0f;
        bool targetFromBob = false;
        Diagnostics::Aim("target.primary start prediction=%d predictionMode=%d weapon=%s entities=%zu dynamic=%zu local_addr=0x%llX local_hero=0x%llX fovDeg=%.6f bone=%d autobone=%d teamMode=%d distance=(%.2f,%.2f) lock=(active=%d key=0x%llX minMs=%.0f maxMs=%.0f hysteresis=%.1f trace=%d unlock=%d) crosshair=(%.3f,%.3f)",
            resolvedPrediction ? 1 : 0,
            Config::aimbotPredictionMode,
            weaponSpec ? weaponSpec->weaponId.data() : "none",
            entities.size(),
            hp_dy_entities.size(),
            static_cast<unsigned long long>(local_entity.address),
            static_cast<unsigned long long>(local_entity.HeroID),
            Config::Fov,
            Config::Bone,
            Config::autobone ? 1 : 0,
            Config::aimbotTeam,
            Config::aimbotMinDist,
            Config::aimbotMaxDist,
            activeLock.active ? 1 : 0,
            static_cast<unsigned long long>(activeLock.entityKey),
            lockPolicy.minLockMs,
            lockPolicy.maxLockMs,
            lockPolicy.retargetHysteresis,
            static_cast<int>(lockPolicy.traceMode),
            static_cast<int>(lockPolicy.unlockMode),
            CrossHair.X,
            CrossHair.Y);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);
        if (entities.size() > 0) {
            Vector3 PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::aimbotTeam, local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    const uint64_t candidateKey = entities[i].address ? entities[i].address : entities[i].LinkBase;
                    if (ignoreInvisible && !entities[i].Vis &&
                        !TargetingDetail::CandidateCanBypassTrace(lockPolicy, activeLock, candidateKey)) {
                        continue;
                    }
                    if (TargetingDetail::CandidateBlockedByMinLock(lockPolicy, activeLock, candidateKey, now))
                        continue;
                    ++selectableCandidates;
                    const float initialDistance = TargetingDetail::CameraPosition().DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(initialDistance))
                        continue;

                    const int resolvedAimBone = TargetingDetail::ResolveAimBoneForDistance(Config::Bone, initialDistance);
                    PreditPos = TargetingDetail::ConfiguredBonePosition(entities[i], resolvedAimBone);
                    RootPos = PreditPos;

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        RootPos,
                        projectileSpec,
                        resolvedPrediction,
                        false);
                    PreditPos = lead.finalAimPoint;
                    const Vector3 fovPoint = lead.finalAimPoint;
                    float fovScoreDeg = 0.0f;
                    if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, Config::Fov, &fovScoreDeg)) {
                        const float distance = TargetingDetail::CameraPosition().DistTo(RootPos);
                        if (!TargetingDetail::DistancePassesAimFilter(distance))
                            continue;

                        Vector2 Vec2 = CrossHair;
                        aimViewMatrix.WorldToScreen(fovPoint, &Vec2, Vector2(OW::WX, OW::WY));
                        float score;
                        if (Config::aimbotPriority == 0) {
                            score = fovScoreDeg;
                        } else if (Config::aimbotPriority == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = distance;
                        }
                        score = TargetingDetail::ApplyRetargetHysteresis(score, lockPolicy, activeLock, candidateKey);
                        if (score < origin) {
                            origin = score;
                            selectedFovScoreDeg = fovScoreDeg;
                            TarGetIndex = i;
                            best.valid = true;
                            best.entityIndex = static_cast<int>(i);
                            best.entityKey = candidateKey;
                            best.entitySnapshot = entities[i];
                            best.boneId = AimBoneToSkeletonBoneId(resolvedAimBone);
                            best.rawAimPoint = RootPos;
                            best.predictedAimPoint = lead.finalAimPoint;
                            best.aimPoint = lead.finalAimPoint;
                            best.screenPoint = Vec2;
                            best.distance = distance;
                            best.fovScore = fovScoreDeg;
                            best.motion = TargetingDetail::EstimateMotionState(entities[i], Vec2);
                            best.lockPolicy = lockPolicy;
                            best.weaponSpec = weaponSpec;
                            best.effectiveHitWindow = ResolveEffectiveHitWindow(
                                entities[i].HeroID,
                                best.boneId,
                                weaponSpec,
                                Config::hitbox,
                                Config::kLegacyDefaultHitboxRadius);
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
                if (Config::autobone && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = GameData::IsTrainingBotHeroId(entities[TarGetIndex].HeroID);
                    if (isBot) {
                        float distbone[5] = { 0 };
                        int index[] = { 17, 16, 3, 13, 54 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 5; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(index[iii]);
                            if (bonerootpos == entities[TarGetIndex].pos) distbone[iii] = 100000;
                            else {
                                bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = fovContext.valid
                                    ? TargetingDetail::FovScoreDeg(fovContext, bonerootpos)
                                    : CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        best.boneId = index[m];
                        best.rawAimPoint = RootPos;
                        best.aimPoint = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                false);
                            PreditPos = lead.finalAimPoint;
                            best.predictedAimPoint = lead.finalAimPoint;
                            best.aimPoint = lead.finalAimPoint;
                        }
                        best.screenPoint = CrossHair;
                        aimViewMatrix.WorldToScreen(best.aimPoint, &best.screenPoint, Vector2(OW::WX, OW::WY));
                        best.fovScore = TargetingDetail::FovScoreDeg(fovContext, best.aimPoint);
                        selectedFovScoreDeg = best.fovScore;
                        best.distance = TargetingDetail::CameraPosition().DistTo(RootPos);
                        best.effectiveHitWindow = ResolveEffectiveHitWindow(
                            entities[TarGetIndex].HeroID,
                            best.boneId,
                            weaponSpec,
                            Config::hitbox,
                            Config::kLegacyDefaultHitboxRadius);
                    } else {
                        float distbone[12] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 12; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = fovContext.valid
                                ? TargetingDetail::FovScoreDeg(fovContext, bonerootpos)
                                : CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 12) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        best.boneId = entities[TarGetIndex].GetSkel()[m];
                        best.rawAimPoint = RootPos;
                        best.aimPoint = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                false);
                            PreditPos = lead.finalAimPoint;
                            best.predictedAimPoint = lead.finalAimPoint;
                            best.aimPoint = lead.finalAimPoint;
                        }
                        best.screenPoint = CrossHair;
                        aimViewMatrix.WorldToScreen(best.aimPoint, &best.screenPoint, Vector2(OW::WX, OW::WY));
                        best.fovScore = TargetingDetail::FovScoreDeg(fovContext, best.aimPoint);
                        selectedFovScoreDeg = best.fovScore;
                        best.distance = TargetingDetail::CameraPosition().DistTo(RootPos);
                        best.effectiveHitWindow = ResolveEffectiveHitWindow(
                            entities[TarGetIndex].HeroID,
                            best.boneId,
                            weaponSpec,
                            Config::hitbox,
                            Config::kLegacyDefaultHitboxRadius);
                    }
                }
            }
        }

        // Ashe B.O.B targeting
        if (local_entity.HeroID == eHero::HERO_ASHE) {
            for (hpanddy hppack : hp_dy_entities) {
                if (hppack.entityid == 0x400000000002533) {
                    if (TargetingDetail::CandidateBlockedByMinLock(lockPolicy, activeLock, hppack.entityid, now))
                        continue;
                    const Vector3 bobPosition(hppack.POS.x, hppack.POS.y, hppack.POS.z);
                    const float bobDistance = TargetingDetail::CameraPosition().DistTo(bobPosition);
                    if (!TargetingDetail::DistancePassesAimFilter(bobDistance))
                        break;
                    float fovScoreDeg = 0.0f;
                    if (!TargetingDetail::IsWithinFovDeg(fovContext, bobPosition, Config::Fov, &fovScoreDeg))
                        break;
                    Vector2 Vec2 = CrossHair;
                    aimViewMatrix.WorldToScreen(bobPosition, &Vec2, Vector2(OW::WX, OW::WY));
                    const float lockAdjustedScore = TargetingDetail::ApplyRetargetHysteresis(fovScoreDeg, lockPolicy, activeLock, hppack.entityid);
                    if (lockAdjustedScore < origin) {
                        best = TargetCandidate{};
                        best.valid = true;
                        best.entityIndex = -1;
                        best.entityKey = hppack.entityid;
                        best.boneId = BONE_CHEST;
                        best.rawAimPoint = bobPosition;
                        best.predictedAimPoint = best.rawAimPoint;
                        best.aimPoint = best.rawAimPoint;
                        best.screenPoint = Vec2;
                        best.distance = bobDistance;
                        best.fovScore = fovScoreDeg;
                        best.lockPolicy = lockPolicy;
                        best.weaponSpec = weaponSpec;
                        best.effectiveHitWindow = ResolveEffectiveHitWindow(
                            local_entity.HeroID,
                            best.boneId,
                            weaponSpec,
                            Config::hitbox,
                            Config::kLegacyDefaultHitboxRadius);
                        origin = lockAdjustedScore;
                        selectedFovScoreDeg = fovScoreDeg;
                        targetFromBob = true;
                    }
                    break;
                }
            }
        }
        Config::aimbotEffectiveHitWindow = best.valid
            ? best.effectiveHitWindow
            : 0.0f;
        TargetingDetail::CommitTargetLockRuntime(best, origin, lockPolicy, now);

        if (!best.valid) {
            Diagnostics::Aim("target.primary result none reason=no_selectable_target entities=%zu candidates=%zu fovDeg=%.6f targetIndex=%d",
                entities.size(),
                selectableCandidates,
                Config::Fov,
                TarGetIndex);
        } else if (targetFromBob) {
            Diagnostics::Aim("target.primary result source=ashe_bob target=(%.9f,%.9f,%.9f) score=%.9f fovScoreDeg=%.9f",
                best.aimPoint.X,
                best.aimPoint.Y,
                best.aimPoint.Z,
                origin,
                selectedFovScoreDeg);
        } else if (TarGetIndex >= 0 && static_cast<size_t>(TarGetIndex) < entities.size()) {
            const c_entity& selected = entities[static_cast<size_t>(TarGetIndex)];
            Diagnostics::Aim("target.primary result index=%d target=(%.9f,%.9f,%.9f) score=%.9f fovScoreDeg=%.9f distance=%.3f bone=%d hitWindow=%.4f health=%.3f hero=0x%llX address=0x%llX vis=%d team=%d",
                TarGetIndex,
                best.aimPoint.X,
                best.aimPoint.Y,
                best.aimPoint.Z,
                origin,
                selectedFovScoreDeg,
                best.distance,
                best.boneId,
                best.effectiveHitWindow,
                selected.PlayerHealth,
                static_cast<unsigned long long>(selected.HeroID),
                static_cast<unsigned long long>(selected.address),
                selected.Vis ? 1 : 0,
                selected.Team ? 1 : 0);
        }
        return best;
    }

    inline Vector3 GetVector3(bool predit = false, bool ignoreInvisible = Config::aimbotIgnoreInvisible) {
        const TargetCandidate candidate = AcquireTarget(predit, ignoreInvisible);
        return candidate.valid ? candidate.aimPoint : Vector3{};
    }

    // =========================================================================
    // GetVector3 for track back
    // =========================================================================

    inline Vector3 GetVector3fortrackback(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        if (local_entity.HeroID == eHero::HERO_HANJO) predit = true;
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::aimbotPredictionMode),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, false);
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        auto entities = TargetingDetail::SnapshotEntities();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::aimbotTeam, local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                    else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                    else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        RootPos,
                        projectileSpec,
                        resolvedPrediction,
                        false);
                    PreditPos = lead.finalAimPoint;
                    const Vector3 fovPoint = lead.finalAimPoint;
                    float fovScoreDeg = 0.0f;
                    if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, Config::Fov, &fovScoreDeg)) {
                        float score;
                        if (Config::aimbotPriority == 0) {
                            score = fovScoreDeg;
                        } else if (Config::aimbotPriority == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = TargetingDetail::CameraPosition().DistTo(PreditPos);
                        }
                        if (score < origin) {
                            target = lead.finalAimPoint;
                            origin = score;
                            TarGetIndex = i;
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                if ((Config::autobone || Config::autobone2) && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = GameData::IsTrainingBotHeroId(entities[TarGetIndex].HeroID);
                    if (isBot) {
                        float distbone[5] = { 0 };
                        int index[] = { 17, 16, 3, 13, 54 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 5; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(index[iii]);
                            if (bonerootpos == entities[TarGetIndex].pos) distbone[iii] = 100000;
                            else {
                                bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = fovContext.valid
                                    ? TargetingDetail::FovScoreDeg(fovContext, bonerootpos)
                                    : CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                false);
                            PreditPos = lead.finalAimPoint;
                            target = lead.finalAimPoint;
                        }
                    } else {
                        float distbone[12] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 12; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = fovContext.valid
                                ? TargetingDetail::FovScoreDeg(fovContext, bonerootpos)
                                : CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 12) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                false);
                            PreditPos = lead.finalAimPoint;
                            target = lead.finalAimPoint;
                        }
                    }
                }
                Config::health = entities[TarGetIndex].PlayerHealth;
            }
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for Genji blade
    // =========================================================================

    inline Vector3 GetVector3forgenji(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        const Vector3 camera = TargetingDetail::CameraPosition();
        float origin = 100000.f;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                if (entities[i].HeroID == 0x16dd || entities[i].HeroID == 0x16ee) continue;
                if (entities[i].HeroID == eHero::HERO_GENJI && entities[i].skill2act) continue;
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::aimbotTeam, local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    Vector3 PreditPos;
                    if (!local_entity.skillcd1)
                        PreditPos = entities[i].GetBonePos(entities[i].GetSkel()[16]);
                    else
                        PreditPos = entities[i].GetBonePos(entities[i].GetSkel()[2]);
                    bool isBot = GameData::IsTrainingBotHeroId(entities[i].HeroID);
                    if (isBot) {
                        PreditPos = entities[i].GetBonePos(3);
                        if (!local_entity.skillcd1) PreditPos.Y -= 0.4f;
                    }
                    Vector3 RootPos = PreditPos;
                    if (entities[i].HeroID == eHero::HERO_WRECKINGBALL) {
                        PreditPos.Y -= 0.7f;
                        RootPos.Y -= 0.7f;
                    }
                    float score;
                    if (Config::aimbotPriority == 0) {
                        score = camera.DistTo(PreditPos);
                        if (entities[i].PlayerHealth > 200.f) score = entities[i].PlayerHealth;
                        if (entities[i].HeroID == eHero::HERO_ZENYATTA && entities[i].ultimate == 0.f) score = 1000.f;
                    } else if (Config::aimbotPriority == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = camera.DistTo(PreditPos);
                    }
                    if (score < origin && score <= 6000.f) {
                        target = RootPos;
                        origin = score;
                        TarGetIndex = i;
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
            }
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for secondary aim
    // =========================================================================

    inline Vector3 GetVector3aim2(bool predit = false, bool ignoreInvisible = Config::aimbotIgnoreInvisible) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        const Matrix aimViewMatrix = OW::SnapshotViewMatrix();
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        const Vector3 camera = TargetingDetail::CameraPosition();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(local_entity.HeroID, Config::aimbotAttack);
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::aimbotPredictionMode),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, local_entity, true);
        float origin = 100000.f;
        Vector3 PreditPos, RootPos;
        int selectedBoneId = AimBoneToSkeletonBoneId(Config::Bone2);
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::aimbotTeam, local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    if (ignoreInvisible && !entities[i].Vis)
                        continue;
                    const float initialDistance = camera.DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(initialDistance))
                        continue;
                    if (entities[i].Team) {
                        if (Config::Bone2 == 1)      { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone2 == 2) { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    } else {
                        if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    }
                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        RootPos,
                        projectileSpec,
                        resolvedPrediction,
                        true);
                    PreditPos = lead.finalAimPoint;
                    const Vector3 fovPoint = lead.finalAimPoint;
                    float fovScoreDeg = 0.0f;
                    if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, Config::Fov2, &fovScoreDeg)) {
                        const float distance = camera.DistTo(RootPos);
                        if (!TargetingDetail::DistancePassesAimFilter(distance))
                            continue;
                        float score;
                        if (Config::aimbotPriority == 0) {
                            score = fovScoreDeg;
                        } else if (Config::aimbotPriority == 1) {
                            score = entities[i].PlayerHealth;
                        } else {
                            score = distance;
                        }
                        if (score < origin) {
                            target = lead.finalAimPoint;
                            origin = score;
                            TarGetIndex = i;
                            selectedBoneId = AimBoneToSkeletonBoneId(entities[i].Team ? Config::Bone2 : Config::Bone);
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
                if (Config::autobone2 && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = GameData::IsTrainingBotHeroId(entities[TarGetIndex].HeroID);
                    if (isBot) {
                        float distbone[5] = { 0 };
                        int index[] = { 17, 16, 3, 13, 54 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 5; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(index[iii]);
                            if (bonerootpos == entities[TarGetIndex].pos) distbone[iii] = 100000;
                            else {
                                bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        selectedBoneId = index[m];
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                true);
                            PreditPos = lead.finalAimPoint;
                            target = lead.finalAimPoint;
                        }
                    } else {
                        float distbone[10] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 10; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = aimViewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 10) - distbone);
                        selectedBoneId = entities[TarGetIndex].GetSkel()[m];
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (resolvedPrediction) {
                            const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                                entities[TarGetIndex],
                                RootPos,
                                projectileSpec,
                                true,
                                true);
                            PreditPos = lead.finalAimPoint;
                            target = lead.finalAimPoint;
                        }
                    }
                }
                Config::aimbotEffectiveHitWindow2 = ResolveEffectiveHitWindow(
                    entities[TarGetIndex].HeroID,
                    selectedBoneId,
                    weaponSpec,
                    Config::hitbox2,
                    Config::kLegacyDefaultHitboxRadius);
            }
        } else {
            Config::aimbotEffectiveHitWindow2 = 0.0f;
        }
        if (TarGetIndex == -1)
            Config::aimbotEffectiveHitWindow2 = 0.0f;
        return target;
    }

    // =========================================================================
    // GetVector3 for auto-FOV target
    // =========================================================================

    inline Vector3 GetVector3forfov(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        const TargetingDetail::FovRuntimeContext fovContext = TargetingDetail::SnapshotFovRuntimeContext();
        const Vector3 camera = TargetingDetail::CameraPosition();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                const bool teamPass = TargetingDetail::TargetTeamMatches(
                    entities[i], Config::aimbotTeam, local_entity);
                if (TargetingDetail::IsRuntimeTargetValid(entities[i], false) && teamPass) {
                    const float distance = camera.DistTo(entities[i].chest_pos);
                    if (!TargetingDetail::DistancePassesAimFilter(distance))
                        continue;
                    PreditPos = entities[i].head_pos;
                    RootPos = entities[i].head_pos;
                    float score;
                    if (Config::aimbotPriority == 0) {
                        score = TargetingDetail::FovScoreDeg(fovContext, RootPos);
                    } else if (Config::aimbotPriority == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = distance;
                    }
                    if (score < origin) {
                        target = RootPos;
                        origin = score;
                        TarGetIndex = i;
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyifov = TarGetIndex;
            }
        }
        return target;
    }

    namespace AimSmoothingDetail {

        constexpr float kDefaultDeltaTime = 1.0f / 144.0f;
        constexpr float kMinDeltaTime = 1.0f / 1000.0f;
        constexpr float kMaxDeltaTime = 0.1f;
        constexpr float kRetargetAngleThreshold = 2.0f;

        struct PIDState {
            Vector3 integral{};
            Vector3 previousError{};
            Vector3 lastTarget{};
            bool initialized = false;
        };

        struct BezierState {
            std::vector<Vector3> controlPoints{};
            Vector3 lastTarget{};
            float t = 0.0f;
            bool initialized = false;
        };

        struct OvershootState {
            Vector3 offset{};
            Vector3 lastTarget{};
            bool initialized = false;
        };

        inline PIDState& GetPIDState() {
            static PIDState state;
            return state;
        }

        inline BezierState& GetBezierState() {
            static BezierState state;
            return state;
        }

        inline OvershootState& GetOvershootState() {
            static OvershootState state;
            return state;
        }

        inline void ResetPIDState() {
            GetPIDState() = PIDState{};
        }

        inline void ResetBezierState() {
            GetBezierState() = BezierState{};
        }

        inline void ResetOvershootState() {
            GetOvershootState() = OvershootState{};
        }

        inline float ClampDeltaTime(float deltaTime) {
            if (!std::isfinite(deltaTime) || deltaTime <= 0.0f)
                return kDefaultDeltaTime;
            return std::clamp(deltaTime, kMinDeltaTime, kMaxDeltaTime);
        }

        inline float ComputeDeltaTime() {
            static ULONGLONG lastTick = 0;

            const ULONGLONG currentTick = GetTickCount64();
            if (lastTick == 0) {
                lastTick = currentTick;
                return kDefaultDeltaTime;
            }

            const float deltaTime = static_cast<float>(currentTick - lastTick) / 1000.0f;
            lastTick = currentTick;
            return ClampDeltaTime(deltaTime);
        }

        inline bool IsRetarget(const Vector3& target, const Vector3& lastTarget) {
            return target.DistTo(lastTarget) > kRetargetAngleThreshold;
        }

        inline float EstimateMovePixels(const Vector3& angleDelta) {
            const float sensitivity = Config::KmboxYawCountsPerRadian();
            const float pitchSensitivity = Config::KmboxPitchCountsPerRadian();
            const float pitchScale = std::clamp(Config::aimbotPitchScale, 0.1f, 3.0f);
            const float pixelX = -angleDelta.Y * sensitivity;
            const float pixelY = angleDelta.X * pitchSensitivity * pitchScale;
            return sqrtf(pixelX * pixelX + pixelY * pixelY);
        }

        inline Vector3 ApplyOvershootCurveTarget(const Vector3& local, const Vector3& target) {
            if (!Config::aimOvershootCurve)
                return target;

            OvershootState& state = GetOvershootState();
            if (!state.initialized || IsRetarget(target, state.lastTarget)) {
                state = OvershootState{};
                state.initialized = true;
            }

            state.lastTarget = target;
            const float resetPixels = std::clamp(Config::aimOvershootResetPixels, 1.0f, 250.0f);
            if (EstimateMovePixels(state.offset) > resetPixels)
                state.offset = Vector3{};

            const Vector3 adjusted = target + state.offset;
            Diagnostics::Aim("smooth.overshoot apply offset=(%.9f,%.9f,%.9f) offset_pixels=%.3f local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) adjusted=(%.9f,%.9f,%.9f)",
                state.offset.X,
                state.offset.Y,
                state.offset.Z,
                EstimateMovePixels(state.offset),
                local.X,
                local.Y,
                local.Z,
                target.X,
                target.Y,
                target.Z,
                adjusted.X,
                adjusted.Y,
                adjusted.Z);
            return adjusted;
        }

        inline void CommitOvershootStep(const Vector3& outputDelta) {
            if (!Config::aimOvershootCurve) {
                ResetOvershootState();
                return;
            }

            OvershootState& state = GetOvershootState();
            const float gain = std::clamp(Config::aimOvershootGain, 0.0f, 1.0f);
            const float resetPixels = std::clamp(Config::aimOvershootResetPixels, 1.0f, 250.0f);
            state.offset = (state.offset + outputDelta * gain) * 0.82f;
            if (EstimateMovePixels(state.offset) > resetPixels)
                state.offset = Vector3{};

            Diagnostics::Aim("smooth.overshoot commit output_delta=(%.9f,%.9f,%.9f) gain=%.3f offset=(%.9f,%.9f,%.9f) offset_pixels=%.3f resetPixels=%.3f",
                outputDelta.X,
                outputDelta.Y,
                outputDelta.Z,
                gain,
                state.offset.X,
                state.offset.Y,
                state.offset.Z,
                EstimateMovePixels(state.offset),
                resetPixels);
        }

        inline float ClampIntegralComponent(float value, float maxIntegral) {
            return std::clamp(value, -maxIntegral, maxIntegral);
        }

        inline Vector3 ClampIntegral(const Vector3& value, float maxIntegral) {
            return Vector3(
                ClampIntegralComponent(value.X, maxIntegral),
                ClampIntegralComponent(value.Y, maxIntegral),
                ClampIntegralComponent(value.Z, maxIntegral)
            );
        }

        inline float ClampStepComponent(float output, float error) {
            const float maxStep = std::fabs(error);
            if (maxStep <= 0.0f)
                return 0.0f;
            return std::clamp(output, -maxStep, maxStep);
        }

        inline Vector3 ClampStepToError(const Vector3& output, const Vector3& error) {
            return Vector3(
                ClampStepComponent(output.X, error.X),
                ClampStepComponent(output.Y, error.Y),
                ClampStepComponent(output.Z, error.Z)
            );
        }

        inline Vector3 Lerp(const Vector3& a, const Vector3& b, float t) {
            return a * (1.0f - t) + b * t;
        }

        inline Vector3 PerpendicularTo(const Vector3& value) {
            Vector3 perpendicular(-value.Y, value.X, 0.0f);
            if (perpendicular.Size() <= 0.0001f)
                perpendicular = Vector3(0.0f, -value.Z, value.Y);

            const float length = perpendicular.get_length();
            return perpendicular / length;
        }

        inline Vector3 EvaluateBezier(const std::vector<Vector3>& controlPoints, float t) {
            if (controlPoints.empty())
                return Vector3{};

            std::vector<Vector3> points = controlPoints;
            t = std::clamp(t, 0.0f, 1.0f);

            for (size_t count = points.size(); count > 1; --count) {
                for (size_t index = 0; index + 1 < count; ++index)
                    points[index] = Lerp(points[index], points[index + 1], t);
            }

            return points.front();
        }

        inline void GenerateBezierControlPoints(BezierState& state,
                                                const Vector3& current,
                                                const Vector3& target,
                                                const Config::AimMethodPreset* methodPreset) {
            const int intermediateCount = std::clamp(
                methodPreset ? methodPreset->bezierControlPoints : Config::aimBezierControlPoints,
                2,
                6);
            const float rawCurvature = methodPreset ? methodPreset->bezierCurvature : Config::aimBezierCurvature;
            const float curvature = std::isfinite(rawCurvature) ? std::clamp(rawCurvature, 0.0f, 1.0f) : 0.5f;
            const Vector3 delta = target - current;
            const float distance = delta.Size();

            state.controlPoints.clear();
            state.controlPoints.reserve(static_cast<size_t>(intermediateCount) + 2);
            state.controlPoints.push_back(current);

            if (distance > 0.0001f) {
                const Vector3 perpendicular = PerpendicularTo(delta);
                const float offsetScale = distance * curvature * 0.35f;

                for (int index = 1; index <= intermediateCount; ++index) {
                    const float alpha = static_cast<float>(index) / static_cast<float>(intermediateCount + 1);
                    const float wave = sinf(alpha * M_PI_F);
                    const Vector3 base = current + delta * alpha;
                    state.controlPoints.push_back(base + perpendicular * (offsetScale * wave));
                }
            }

            state.controlPoints.push_back(target);
            state.lastTarget = target;
            state.t = 0.0f;
            state.initialized = true;
        }

    } // namespace AimSmoothingDetail

    inline void ResetAimSmoothingState() {
        AimSmoothingDetail::ResetPIDState();
        AimSmoothingDetail::ResetBezierState();
        AimSmoothingDetail::ResetOvershootState();
    }

    inline Vector3 SmoothPID(Vector3 current,
                             Vector3 target,
                             float deltaTime,
                             const Config::AimMethodPreset* methodPreset) {
        AimSmoothingDetail::PIDState& state = AimSmoothingDetail::GetPIDState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const Vector3 error = target - current;
        const float rawDeadzone = methodPreset ? methodPreset->pidDeadzone : Config::aimPidDeadzone;
        const float deadzone = std::clamp(
            (std::isfinite(rawDeadzone) ? rawDeadzone : 1.0f) * (M_PI_F / 180.0f),
            0.0f,
            M_PI_F / 2.0f);
        const float errorLength = error.Size();

        if (errorLength < deadzone) {
            Diagnostics::Aim("smooth.pid early_return reason=deadzone current=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) error=(%.9f,%.9f,%.9f) error_len=%.9f deadzone=%.9f",
                current.X,
                current.Y,
                current.Z,
                target.X,
                target.Y,
                target.Z,
                error.X,
                error.Y,
                error.Z,
                errorLength,
                deadzone);
            state.integral = Vector3{};
            state.previousError = error;
            state.lastTarget = target;
            state.initialized = true;
            return current;
        }

        const bool resetState = !state.initialized ||
            AimSmoothingDetail::IsRetarget(target, state.lastTarget);

        if (resetState) {
            state.integral = Vector3{};
            state.previousError = error;
            state.initialized = true;
        }

        state.lastTarget = target;
        state.integral += error * deltaTime;
        const float rawMaxIntegral = methodPreset ? methodPreset->pidMaxIntegral : Config::aimPidMaxIntegral;
        state.integral = AimSmoothingDetail::ClampIntegral(
            state.integral,
            std::clamp(std::isfinite(rawMaxIntegral) ? rawMaxIntegral : 10.0f, 1.0f, 50.0f)
        );

        const Vector3 derivative = resetState
            ? Vector3{}
            : (error - state.previousError) / deltaTime;
        state.previousError = error;

        const float rawP = methodPreset ? methodPreset->pidP : Config::aimPidP;
        const float rawI = methodPreset ? methodPreset->pidI : Config::aimPidI;
        const float rawD = methodPreset ? methodPreset->pidD : Config::aimPidD;
        const Vector3 output =
            error * std::clamp(std::isfinite(rawP) ? rawP : 0.5f, 0.0f, 2.0f) +
            state.integral * std::clamp(std::isfinite(rawI) ? rawI : 0.01f, 0.0f, 0.5f) +
            derivative * std::clamp(std::isfinite(rawD) ? rawD : 0.1f, 0.0f, 1.0f);

        return current + AimSmoothingDetail::ClampStepToError(output, error);
    }

    inline Vector3 SmoothBezier(Vector3 current,
                                Vector3 target,
                                float deltaTime,
                                float speed,
                                const Config::AimMethodPreset* methodPreset) {
        AimSmoothingDetail::BezierState& state = AimSmoothingDetail::GetBezierState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const bool resetState = !state.initialized ||
            state.controlPoints.empty() ||
            AimSmoothingDetail::IsRetarget(target, state.lastTarget);

        if (resetState)
            AimSmoothingDetail::GenerateBezierControlPoints(state, current, target, methodPreset);
        else {
            state.lastTarget = target;
            state.controlPoints.back() = target;
        }

        state.t = std::clamp(
            state.t + std::clamp(speed, 0.0f, 200.0f) * deltaTime,
            0.0f,
            1.0f
        );

        if (state.t >= 1.0f)
            return target;

        return AimSmoothingDetail::EvaluateBezier(state.controlPoints, state.t);
    }

    inline Vector3 SmoothPiecewise(Vector3 current,
                                   Vector3 target,
                                   float speed,
                                   const Config::AimMethodPreset* methodPreset) {
        const Vector3 error = target - current;
        const float errorDegrees = RAD2DEG(error.Size());
        float scale = Config::AimPiecewiseNearScale(methodPreset);
        if (errorDegrees > Config::AimPiecewiseFarDegrees(methodPreset))
            scale = 1.00f;
        else if (errorDegrees > Config::AimPiecewiseMidDegrees(methodPreset))
            scale = Config::AimPiecewiseFarScale(methodPreset);
        else if (errorDegrees > Config::AimPiecewiseNearDegrees(methodPreset))
            scale = Config::AimPiecewiseMidScale(methodPreset);

        return SmoothLinear(current, target, std::clamp(speed * scale, 0.0f, 1.0f));
    }

    inline Vector3 SmoothConstantAngularVelocity(Vector3 current,
                                                 Vector3 target,
                                                 float deltaTime,
                                                 float degreesPerSecond) {
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);
        const Vector3 error = target - current;
        const float errorLength = error.Size();
        const float angularSpeedRad = DEG2RAD(std::clamp(
            std::isfinite(degreesPerSecond) ? degreesPerSecond : 30.0f,
            0.0f,
            Config::kAimConstantAngularSpeedMaxDeg));
        const float maxStep = angularSpeedRad * deltaTime;

        if (errorLength <= 0.000001f || maxStep <= 0.0f)
            return current;
        if (errorLength <= maxStep)
            return target;

        return current + error * (maxStep / errorLength);
    }

    inline Vector3 SmoothDispatchWithMethod(Vector3 local,
                                            Vector3 target,
                                            float speed,
                                            float accel,
                                            int methodOverride,
                                            float bezierSpeedOverride = -1.0f) {
        const float deltaTime = AimSmoothingDetail::ComputeDeltaTime();
        const int method = std::clamp(methodOverride, 0, Config::kAimMethodCount - 1);
        static int previousMethod = -1;

        if (method != previousMethod) {
            AimSmoothingDetail::ResetPIDState();
            AimSmoothingDetail::ResetBezierState();
            AimSmoothingDetail::ResetOvershootState();
            previousMethod = method;
        }

        const Vector3 adjustedTarget = AimSmoothingDetail::ApplyOvershootCurveTarget(local, target);
        const Vector3 error = adjustedTarget - local;
        const Config::AimMethodPreset* methodPreset = Config::ActiveAimMethodPreset(method);
        const float methodSpeedScale = Config::RuntimeAimMethodAngularSpeedScale(method);
        const float slotSpeedScale = std::isfinite(speed) ? std::clamp(speed, 0.0f, 2.0f) : 0.0f;
        const float effectiveSpeed = std::clamp(slotSpeedScale * methodSpeedScale, 0.0f, 1.0f);
        const float effectiveAccel = method == 4 ? Config::RuntimeAimMethodAcceleration(method) : accel;
        const float presetBezierSpeed = methodPreset ? methodPreset->bezierSpeed : Config::aimBezierSpeed;
        const float bezierSpeed = bezierSpeedOverride > 0.0f
            ? bezierSpeedOverride
            : presetBezierSpeed;
        const float effectiveBezierSpeed = std::clamp(bezierSpeed * slotSpeedScale * methodSpeedScale, 0.0f, 200.0f);
        const float constantAngularSpeedDeg = Config::RuntimeAimConstantAngularSpeedDeg();
        const float effectiveConstantAngularSpeedDeg = std::clamp(
            constantAngularSpeedDeg * slotSpeedScale * methodSpeedScale,
            0.0f,
            Config::kAimConstantAngularSpeedMaxDeg);
        Diagnostics::Aim("smooth.dispatch method=%d speed=%.9f slotSpeedScale=%.9f speedScale=%.9f effectiveSpeed=%.9f accel=%.9f effectiveAccel=%.9f dt=%.9f bezierSpeed=%.9f constantDegPerSec=%.9f local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) adjusted=(%.9f,%.9f,%.9f) error=(%.9f,%.9f,%.9f) error_len=%.9f",
            method,
            speed,
            slotSpeedScale,
            methodSpeedScale,
            effectiveSpeed,
            accel,
            effectiveAccel,
            deltaTime,
            effectiveBezierSpeed,
            effectiveConstantAngularSpeedDeg,
            local.X,
            local.Y,
            local.Z,
            target.X,
            target.Y,
            target.Z,
            adjustedTarget.X,
            adjustedTarget.Y,
            adjustedTarget.Z,
            error.X,
            error.Y,
            error.Z,
            error.Size());

        auto scaleCandidateStep = [&](const Vector3& candidate) {
            const Vector3 candidateDelta = candidate - local;
            return local + AimSmoothingDetail::ClampStepToError(candidateDelta * effectiveSpeed, error);
        };

        Vector3 result{};
        switch (method) {
        case 0:
            result = SmoothLinear(local, adjustedTarget, effectiveSpeed);
            break;
        case 1:
            result = scaleCandidateStep(SmoothPID(local, adjustedTarget, deltaTime, methodPreset));
            break;
        case 2:
            result = SmoothBezier(local, adjustedTarget, deltaTime, effectiveBezierSpeed, methodPreset);
            break;
        case 3:
            result = SmoothPiecewise(local, adjustedTarget, effectiveSpeed, methodPreset);
            break;
        case 4:
            result = SmoothAccelerate(local, adjustedTarget, effectiveSpeed, effectiveAccel);
            break;
        case 5:
            result = SmoothConstantAngularVelocity(local, adjustedTarget, deltaTime, effectiveConstantAngularSpeedDeg);
            break;
        default:
            result = SmoothLinear(local, adjustedTarget, effectiveSpeed);
            break;
        }

        const Vector3 outputDelta = result - local;
        AimSmoothingDetail::CommitOvershootStep(outputDelta);
        Diagnostics::Aim("smooth.result method=%d result=(%.9f,%.9f,%.9f) output_delta=(%.9f,%.9f,%.9f) output_len=%.9f",
            method,
            result.X,
            result.Y,
            result.Z,
            outputDelta.X,
            outputDelta.Y,
            outputDelta.Z,
            outputDelta.Size());
        return result;
    }

    inline Vector3 SmoothDispatch(Vector3 local, Vector3 target, float speed, float accel) {
        return SmoothDispatchWithMethod(
            local,
            target,
            speed,
            accel,
            Config::AimBehaviorMethod(Config::aimBehavior),
            Config::aimBezierSpeed);
    }

} // namespace OW
