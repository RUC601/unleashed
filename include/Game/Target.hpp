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
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

extern std::mutex g_mutex;

namespace OW {

    // =========================================================================
    // Global extern declarations (defined in Overwatch.hpp)
    // =========================================================================

    extern Matrix viewMatrix;
    extern Matrix viewMatrix_xor;
    extern float WX;
    extern float WY;
    extern std::vector<c_entity> entities;
    extern c_entity local_entity;
    extern std::vector<hpanddy> hp_dy_entities;

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

    inline void SendMouseMove(const Vector3& delta, int moveTimeMs = -1) {
        if (moveTimeMs < 0) moveTimeMs = Config::kmboxInputDelayMs;
        Diagnostics::Aim("mouse.move request delta_rad=(%.9f,%.9f,%.9f) moveTimeMs=%d enabled=%d deviceType=%d",
            delta.X,
            delta.Y,
            delta.Z,
            moveTimeMs,
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
            // Use calibrated pixels-per-radian if available, otherwise fall back to manual kmboxAimSensitivity
            float baseSensitivity;
            if (Config::calibratedPixelsPerRadian > 0.0f) {
                baseSensitivity = std::clamp(Config::calibratedPixelsPerRadian, 0.1f, 20000.0f);
            } else {
                baseSensitivity = std::clamp(Config::kmboxAimSensitivity, 0.1f, 2000.0f);
            }
            float sensitivity = baseSensitivity;
            float pitchSensitivity = (Config::calibratedPixelsPerRadianPitch > 0.0f)
                ? Config::calibratedPixelsPerRadianPitch
                : sensitivity;
            float syncScale = 1.0f;
            if (Config::autoSyncSensitivity &&
                std::isfinite(Config::gameMouseSensitivity) &&
                std::isfinite(Config::sensReference) &&
                Config::gameMouseSensitivity > 0.0f &&
                Config::sensReference > 0.0f) {
                syncScale = Config::sensReference / Config::gameMouseSensitivity;
                sensitivity = baseSensitivity * syncScale;
                pitchSensitivity = (Config::calibratedPixelsPerRadianPitch > 0.0f)
                    ? Config::calibratedPixelsPerRadianPitch * syncScale
                    : sensitivity;
            }

            if (!std::isfinite(sensitivity) || sensitivity <= 0.0f) {
                Diagnostics::Aim("mouse.convert warning invalid_effective_sensitivity computed=%.9f fallback=%.9f autoSync=%d gameSens=%.9f refSens=%.9f pitchSens=%.9f",
                    sensitivity,
                    baseSensitivity,
                    Config::autoSyncSensitivity ? 1 : 0,
                    Config::gameMouseSensitivity,
                    Config::sensReference,
                    pitchSensitivity);
                sensitivity = baseSensitivity;
                pitchSensitivity = sensitivity;
                syncScale = 1.0f;
            }

            static float accumX = 0.0f;
            static float accumY = 0.0f;
            static int callCount = 0;

            const float scaledX = delta.X * pitchSensitivity;
            const float scaledY = delta.Y * sensitivity;
            const float accumBeforeX = accumX;
            const float accumBeforeY = accumY;
            accumX += scaledX;
            accumY += scaledY;
            int pixelX = static_cast<int>(accumX);
            int pixelY = static_cast<int>(accumY);
            accumX -= static_cast<float>(pixelX);
            accumY -= static_cast<float>(pixelY);

            ++callCount;
            Diagnostics::Aim("mouse.convert call=%d delta_rad=(%.9f,%.9f) baseSensitivity=%.6f effectiveSensitivity=%.6f autoSync=%d syncScale=%.6f scaled_pixels=(%.9f,%.9f) accum_before=(%.9f,%.9f) pixel=(%d,%d) accum_after=(%.9f,%.9f)",
                callCount,
                delta.X,
                delta.Y,
                baseSensitivity,
                sensitivity,
                Config::autoSyncSensitivity ? 1 : 0,
                syncScale,
                scaledX,
                scaledY,
                accumBeforeX,
                accumBeforeY,
                pixelX,
                pixelY,
                accumX,
                accumY);
            if (callCount <= 50 || pixelX != 0 || pixelY != 0) {
                std::printf("[KMBOX] #%d delta=(%.6f,%.6f) yawSens=%.1f pitchSens=%.1f px=(%d,%d) accum=(%.3f,%.3f)\n",
                    callCount, delta.X, delta.Y, sensitivity, pitchSensitivity,
                    pixelX, pixelY, accumX, accumY);
            }

            if (pixelX == 0 && pixelY == 0) {
                Diagnostics::Aim("mouse.move early_return reason=zero_pixels scaled_pixels=(%.9f,%.9f) accum_after=(%.9f,%.9f) note=integer_truncation_waiting_for_accumulator",
                    scaledX,
                    scaledY,
                    accumX,
                    accumY);
                return;
            }

            // ---- Micro-split mouse movement ----
            const int maxPixels = std::clamp(Config::moveSplitMaxPixels, 1, 50);
            const int delayUs = std::clamp(Config::moveSplitDelayUs, 0, 10000);
            const int absX = std::abs(pixelX);
            const int absY = std::abs(pixelY);
            const int maxComponent = (std::max)(absX, absY);
            const int steps = Config::moveSplitEnabled
                ? (std::max)(1, (maxComponent + maxPixels - 1) / maxPixels)
                : 1;

            if (steps <= 1) {
                // Single command (original behavior)
                if (Config::kmboxDeviceType == 0) {
                    const int status = kmbox::KmBoxMgr.Mouse.Move(pixelX, pixelY);
                    Diagnostics::Aim("mouse.enqueue network pixel=(%d,%d) status=%d",
                        pixelX, pixelY, status);
                } else {
                    kmbox::kmBoxBMgr.km_move(pixelX, pixelY);
                    Diagnostics::Aim("mouse.enqueue serial pixel=(%d,%d)", pixelX, pixelY);
                }
            } else {
                // Split into micro-movements
                Diagnostics::Aim("mouse.move.split total_pixels=(%d,%d) steps=%d max_pixels_per_step=%d delay_us=%d",
                    pixelX, pixelY, steps, maxPixels, delayUs);
                int remainingX = pixelX;
                int remainingY = pixelY;
                for (int i = 0; i < steps; i++) {
                    const int curX = remainingX / (steps - i);
                    const int curY = remainingY / (steps - i);
                    remainingX -= curX;
                    remainingY -= curY;

                    if (Config::kmboxDeviceType == 0) {
                        const int status = kmbox::KmBoxMgr.Mouse.Move(curX, curY);
                        Diagnostics::Aim("mouse.enqueue.split network step=%d/%d cur=(%d,%d) status=%d",
                            i + 1, steps, curX, curY, status);
                    } else {
                        kmbox::kmBoxBMgr.km_move(curX, curY);
                        Diagnostics::Aim("mouse.enqueue.split serial step=%d/%d cur=(%d,%d)",
                            i + 1, steps, curX, curY);
                    }

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

    inline float CalibrateSensitivity(bool calibrateBothAxes = true) {
        // 1. Set calibration flag
        Config::calibrationInProgress = true;

        // 2. Read initial view angle from remote memory (pitch, yaw, roll in radians)
        Vector3 angleBefore = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
        Sleep(Config::calibrationStabilityWaitMs);
        angleBefore = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170); // re-read for stability

        // 3. Send a known horizontal mouse move (only yaw matters)
        int moveX = Config::calibrationMovePixels;
        int moveY = 0;
        if (Config::kmboxDeviceType == 0)
            kmbox::KmBoxMgr.Mouse.Move(moveX, moveY);
        else
            kmbox::kmBoxBMgr.km_move(moveX, moveY);

        // 4. Wait for movement to register
        Sleep(Config::calibrationStabilityWaitMs);

        // 5. Read new view angle
        Vector3 angleAfter = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);

        // 6. Calculate delta in radians (only yaw for horizontal calibration)
        constexpr float kPi = 3.14159265358979323846f;
        float yawDelta = fabsf(angleAfter.Y - angleBefore.Y);
        // Handle angle wrapping (if angles wrap at +/-PI)
        if (yawDelta > kPi) yawDelta = 2.0f * kPi - yawDelta;

        // 7. Calculate pixels per radian
        if (yawDelta > 0.0001f) {
            Config::calibratedPixelsPerRadian = static_cast<float>(moveX) / yawDelta;
        } else {
            std::printf("[CALIBRATE] Yaw delta too small (%.9f rad) -- game may be in menu or not running\n", yawDelta);
            Config::calibrationInProgress = false;
            return 0.0f;
        }

        // 8. Also calibrate vertical (pitch) sensitivity
        if (calibrateBothAxes) {
            Sleep(Config::calibrationStabilityWaitMs);

            Vector3 pitchBefore = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
            Sleep(Config::calibrationStabilityWaitMs);
            pitchBefore = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);

            int pitchMoveX = 0;
            int pitchMoveY = Config::calibrationMovePixels;
            if (Config::kmboxDeviceType == 0)
                kmbox::KmBoxMgr.Mouse.Move(pitchMoveX, pitchMoveY);
            else
                kmbox::kmBoxBMgr.km_move(pitchMoveX, pitchMoveY);

            Sleep(Config::calibrationStabilityWaitMs);

            Vector3 pitchAfter = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);

            float pitchDelta = fabsf(pitchAfter.X - pitchBefore.X);
            if (pitchDelta > kPi) pitchDelta = 2.0f * kPi - pitchDelta;

            if (pitchDelta > 0.0001f) {
                float pitchRatio = static_cast<float>(pitchMoveY) / pitchDelta;
                // Store separate pitch sensitivity only if >5% different from yaw
                if (Config::calibratedPixelsPerRadian > 0.0f) {
                    float ratioDiff = fabsf(pitchRatio - Config::calibratedPixelsPerRadian) / Config::calibratedPixelsPerRadian;
                    if (ratioDiff > 0.05f) {
                        Config::calibratedPixelsPerRadianPitch = pitchRatio;
                        std::printf("[CALIBRATE] Pitch sensitivity differs from yaw by %.1f%% -- storing separate value\n", ratioDiff * 100.0f);
                    } else {
                        Config::calibratedPixelsPerRadianPitch = 0.0f;
                    }
                }
            } else {
                std::printf("[CALIBRATE] Pitch delta too small (%.9f rad) -- skipping pitch calibration\n", pitchDelta);
            }
        }

        std::printf("[CALIBRATE] Result: yaw=%.1f px/rad pitch=%.1f px/rad\n",
            Config::calibratedPixelsPerRadian,
            Config::calibratedPixelsPerRadianPitch > 0.0f ? Config::calibratedPixelsPerRadianPitch : Config::calibratedPixelsPerRadian);

        Config::calibrationInProgress = false;
        return Config::calibratedPixelsPerRadian;
    }

    inline float RunCalibrationSamples() {
        float total = 0.0f;
        int validSamples = 0;
        int numSamples = (std::max)(Config::calibrationSampleCount, 1);

        for (int i = 0; i < numSamples; ++i) {
            std::printf("[CALIBRATE] Sample %d/%d starting...\n", i + 1, numSamples);
            float result = CalibrateSensitivity(true);
            if (result > 0.0f) {
                total += result;
                ++validSamples;
                std::printf("[CALIBRATE] Sample %d/%d complete: %.1f px/rad\n", i + 1, numSamples, result);
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
            Config::calibratedPixelsPerRadian = avg;
            std::printf("[CALIBRATE] Averaged %d valid samples: %.1f px/rad\n", validSamples, avg);
        } else {
            std::printf("[CALIBRATE] All samples failed -- calibration unsuccessful\n");
            Config::calibratedPixelsPerRadian = 0.0f;
        }

        return Config::calibratedPixelsPerRadian;
    }

    inline void SendMouseButton(int button, bool down) {
        if (Config::kmboxEnabled) {
            if (Config::kmboxDebugLog) {
                std::printf("[KMBOX] mouse.button button=%d down=%d type=%d\n",
                    button, down ? 1 : 0, Config::kmboxDeviceType);
            }

            if (Config::kmboxDeviceType == 0) {
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
        int vk = 0;
        switch (setting) {
        case 0: vk = VK_RBUTTON; break;
        case 1: vk = VK_LBUTTON; break;
        case 2: vk = VK_XBUTTON1; break;
        case 3: vk = VK_XBUTTON2; break;
        case 4: vk = VK_LSHIFT; break;
        case 5: vk = VK_LMENU; break;
        case 6: vk = 0x56; break;          // 'V'
        case 7: vk = VK_LCONTROL; break;
        case 8: vk = VK_TAB; break;
        case 9: vk = 0x45; break;          // 'E'
        case 10: vk = 0x51; break;         // 'Q'
        case 11: vk = 0x46; break;         // 'F'
        case 12: vk = VK_CAPITAL; break;
        default: break;
        }
        return vk;
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
        float dist = LocalAngle.DistTo(TargetAngle);
        static float lastx = 0;
        static float lasty = 0;
        static float lastz = 0;

        Vector3 Result;
        float deltax = (TargetAngle.X - LocalAngle.X) * speed;
        float deltay = (TargetAngle.Y - LocalAngle.Y) * speed;
        float deltaz = (TargetAngle.Z - LocalAngle.Z) * speed;

        Result.X = LocalAngle.X + deltax;
        Result.Y = LocalAngle.Y + deltay;
        Result.Z = LocalAngle.Z + deltaz;

        if (Config::trackcompensate) {
            float radius = 1.f / dist;
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

    inline void AimCorrection(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed) {
        double G = 9.8;
        double A = viewMatrix_xor.get_location().x;
        double B = viewMatrix_xor.get_location().y;
        double C = viewMatrix_xor.get_location().z;
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
        InVecArg->X = (float)(viewMatrix_xor.get_location().x + (H + P * fBestRoot));
        if (Config::projectile_arc || local_entity.HeroID == eHero::HERO_HANJO || Config::Gravitypredit) {
            InVecArg->Y = (float)(viewMatrix_xor.get_location().y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(viewMatrix_xor.get_location().z + (J + R * fBestRoot));
    }

    inline void AimCorrection22(Vector3* InVecArg, Vector3 currVelocity, float Distance, float Bulletspeed) {
        double G = 9.8;
        double A = viewMatrix_xor.get_location().x;
        double B = viewMatrix_xor.get_location().y;
        double C = viewMatrix_xor.get_location().z;
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
        InVecArg->X = (float)(viewMatrix_xor.get_location().x + (H + P * fBestRoot));
        if (Config::Gravitypredit2) {
            InVecArg->Y = (float)(viewMatrix_xor.get_location().y + (K + Q * fBestRoot - L * fBestRoot * fBestRoot));
        } else {
            InVecArg->Y += (float)(fBestRoot * currVelocity.Y);
        }
        InVecArg->Z = (float)(viewMatrix_xor.get_location().z + (J + R * fBestRoot));
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
            const auto camera = viewMatrix_xor.get_location();
            return Vector3(camera.x, camera.y, camera.z);
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
            std::lock_guard<std::mutex> lock(::g_mutex);
            return local_entity;
        }

        inline bool IsValidIndex(int index, size_t size) {
            return index >= 0 && static_cast<size_t>(index) < size;
        }

        inline bool IsRuntimeTargetValid(const c_entity& entity, bool requireVisible = true) {
            if (!entity.address || !entity.Alive) return false;
            if (requireVisible && !entity.Vis) return false;
            return true;
        }

        inline bool TryEntityAt(int index, c_entity& entity, bool requireVisible = true) {
            const auto snapshot = SnapshotEntities();
            if (!IsValidIndex(index, snapshot.size())) return false;
            entity = snapshot[static_cast<size_t>(index)];
            return IsRuntimeTargetValid(entity, requireVisible);
        }

        inline bool CandidateTeamMatches(const c_entity& entity, bool switchTeam, const c_entity& local) {
            if (!switchTeam) return entity.Team;
            return !entity.Team && entity.address != local.address;
        }

        inline bool IsSelectableCandidate(const c_entity& entity, bool switchTeam, const c_entity& local) {
            return IsRuntimeTargetValid(entity, true) && CandidateTeamMatches(entity, switchTeam, local);
        }

        inline Vector3 ConfiguredBonePosition(c_entity& entity, int boneSetting) {
            if (boneSetting == 1) return entity.head_pos;
            if (boneSetting == 2) return entity.neck_pos;
            return entity.chest_pos;
        }

        inline Vector3 ClampMagnitude(const Vector3& value, float maxLength) {
            const float length = value.get_length();
            if (length <= maxLength) return value;
            return value * (maxLength / length);
        }

        inline Vector3 AccelerationAwareVelocity(const c_entity& entity, float distance, float projectileSpeed) {
            const uint64_t key = entity.address ? entity.address : entity.LinkBase;
            if (!key) return entity.velocity;

            const DWORD now = GetTickCount();
            const float safeSpeed = (std::max)(projectileSpeed, 1.0f);
            const float leadTime = std::clamp(distance / safeSpeed, 0.0f, 1.0f);
            Vector3 adjusted = entity.velocity;

            std::lock_guard<std::mutex> lock(velocity_history_mutex);
            if (velocity_history.size() > 256) velocity_history.clear();

            VelocitySample& sample = velocity_history[key];
            if (sample.initialized && now > sample.tick) {
                const float dt = static_cast<float>(now - sample.tick) / 1000.0f;
                if (dt > 0.001f && dt < 0.5f) {
                    Vector3 acceleration = (entity.velocity - sample.velocity) / dt;
                    acceleration = ClampMagnitude(acceleration, 250.0f);
                    adjusted += acceleration * std::clamp(leadTime * 0.5f, 0.0f, 0.25f);
                }
            }

            sample.velocity = entity.velocity;
            sample.tick = now;
            sample.initialized = true;
            return adjusted;
        }

        inline Vector3 ApplyPrediction(c_entity& entity, Vector3 position, bool predit, bool secondary) {
            if (!predit || IsZeroVector(position)) return position;

            const float distance = CameraPosition().DistTo(position);
            const float projectileSpeed = secondary ? Config::predit_level2 : Config::predit_level;
            const Vector3 velocity = AccelerationAwareVelocity(entity, distance, projectileSpeed);

            if (secondary) AimCorrection22(&position, velocity, distance, projectileSpeed);
            else AimCorrection(&position, velocity, distance, projectileSpeed);

            return position;
        }

        inline float CandidateScore(const Vector3& position, const Vector2& crosshair, bool useFov360) {
            if (useFov360) return CameraPosition().DistTo(position);
            return crosshair.Distance(viewMatrix.WorldToScreen(position));
        }

        inline SelectionResult SelectTargetFromSnapshot(const std::vector<c_entity>& snapshot,
                                                        const c_entity& local,
                                                        bool predit,
                                                        bool secondary,
                                                        bool switchTeam,
                                                        int boneSetting,
                                                        float fov,
                                                        bool useFov360) {
            SelectionResult result{};
            const Vector2 crosshair = CrosshairCenter();

            for (size_t i = 0; i < snapshot.size(); ++i) {
                c_entity entity = snapshot[i];
                if (!IsSelectableCandidate(entity, switchTeam, local)) continue;

                Vector3 rootPosition = ConfiguredBonePosition(entity, boneSetting);
                if (IsZeroVector(rootPosition)) continue;

                Vector3 aimPosition = ApplyPrediction(entity, rootPosition, predit, secondary);
                const float score = CandidateScore(aimPosition, crosshair, useFov360);
                if (score >= result.score) continue;
                if (!useFov360 && score > fov) continue;

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

            if (IsTrainingBot(entity.HeroID)) {
                const int botBones[] = { 17, 16, 3, 13, 54 };
                for (int bone : botBones) {
                    Vector3 bonePosition = entity.GetBonePos(bone);
                    if (IsZeroVector(bonePosition) || bonePosition == entity.pos) continue;
                    const float distance = crosshair.Distance(viewMatrix.WorldToScreen(bonePosition));
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
                    const float distance = crosshair.Distance(viewMatrix.WorldToScreen(bonePosition));
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

    inline bool TryGetTargetEntity(int index, c_entity& entity, bool requireVisible = true) {
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
        __try {
            uint64_t pSkill = SDK->RPM<uint64_t>(local_entity.SkillBase + 0x1848);
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

    inline Vector3 GetVector3(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto hp_dy_entities = TargetingDetail::SnapshotDynamicEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();

        float origin = 100000.f;
        size_t selectableCandidates = 0;
        float selectedCrossDist = 0.0f;
        bool targetFromBob = false;
        Diagnostics::Aim("target.primary start prediction=%d entities=%zu dynamic=%zu local_addr=0x%llX local_hero=0x%llX fov=%.6f fov360=%d bone=%d autobone=%d teamMode=%d crosshair=(%.3f,%.3f)",
            predit ? 1 : 0,
            entities.size(),
            hp_dy_entities.size(),
            static_cast<unsigned long long>(local_entity.address),
            static_cast<unsigned long long>(local_entity.HeroID),
            Config::Fov,
            Config::fov360 ? 1 : 0,
            Config::Bone,
            Config::autobone ? 1 : 0,
            Config::aimbotTeam,
            CrossHair.X,
            CrossHair.Y);
        if (entities.size() > 0) {
            if (Config::hanzoautospeed) {
                if (local_entity.HeroID == eHero::HERO_HANJO) {
                    Config::predit_level = readult(local_entity.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
                    if (local_entity.skill2act) Config::predit_level = 110.f;
                }
            }
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                bool teamPass = false;
                if (Config::aimbotTeam == 0) {
                    teamPass = !entities[i].Team && entities[i].address != local_entity.address;
                } else if (Config::aimbotTeam == 1) {
                    teamPass = entities[i].Team;
                } else if (Config::aimbotTeam == 2) {
                    teamPass = true;
                }
                if (entities[i].Alive && teamPass && entities[i].Vis) {
                    ++selectableCandidates;
                    if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                    else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                    else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }

                    Vel = entities[i].velocity;
                    if (predit) {
                        float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        Vel = TargetingDetail::AccelerationAwareVelocity(entities[i], dist, Config::predit_level);
                        AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                    }
                    Vector2 Vec2 = predit ? viewMatrix.WorldToScreen(PreditPos) : viewMatrix.WorldToScreen(RootPos);
                    float CrossDist = CrossHair.Distance(Vec2);
                    if (!Config::fov360) {
                        if (CrossDist <= Config::Fov) {
                            float score;
                            if (Config::aimbotPriority == 0) {
                                score = CrossDist;
                            } else if (Config::aimbotPriority == 1) {
                                score = entities[i].PlayerHealth;
                            } else {
                                score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            }
                            if (score < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = score;
                                selectedCrossDist = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    } else {
                        float worldDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (worldDist < origin) {
                            target = predit ? PreditPos : RootPos;
                            origin = worldDist;
                            selectedCrossDist = CrossDist;
                            TarGetIndex = i;
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
                                bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = entities[TarGetIndex].velocity;
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist, Config::predit_level);
                            AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                            target = PreditPos;
                        }
                    } else {
                        float distbone[12] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 12; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 12) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = entities[TarGetIndex].velocity;
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist, Config::predit_level);
                            AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                            target = PreditPos;
                        }
                    }
                }
            }
        }

        // Ashe B.O.B targeting
        if (local_entity.HeroID == eHero::HERO_ASHE) {
            for (hpanddy hppack : hp_dy_entities) {
                if (hppack.entityid == 0x400000000002533) {
                    Vector2 Vec2 = viewMatrix.WorldToScreen(Vector3(hppack.POS.x, hppack.POS.y, hppack.POS.z));
                    float CrossDist = CrossHair.Distance(Vec2);
                    if (CrossDist < origin && CrossDist <= Config::Fov) {
                        target = Vector3(hppack.POS.x, hppack.POS.y, hppack.POS.z);
                        origin = CrossDist;
                        selectedCrossDist = CrossDist;
                        targetFromBob = true;
                    } else if (Config::fov360) {
                        target = Vector3(hppack.POS.x, hppack.POS.y, hppack.POS.z);
                        targetFromBob = true;
                    }
                    break;
                }
            }
        }
        if (target == Vector3(0, 0, 0)) {
            Diagnostics::Aim("target.primary result none reason=no_selectable_target entities=%zu candidates=%zu fov=%.6f targetIndex=%d",
                entities.size(),
                selectableCandidates,
                Config::Fov,
                TarGetIndex);
        } else if (targetFromBob) {
            Diagnostics::Aim("target.primary result source=ashe_bob target=(%.9f,%.9f,%.9f) score=%.9f crossDist=%.9f",
                target.X,
                target.Y,
                target.Z,
                origin,
                selectedCrossDist);
        } else if (TarGetIndex >= 0 && static_cast<size_t>(TarGetIndex) < entities.size()) {
            const c_entity& selected = entities[static_cast<size_t>(TarGetIndex)];
            Diagnostics::Aim("target.primary result index=%d target=(%.9f,%.9f,%.9f) score=%.9f crossDist=%.9f health=%.3f hero=0x%llX address=0x%llX vis=%d team=%d",
                TarGetIndex,
                target.X,
                target.Y,
                target.Z,
                origin,
                selectedCrossDist,
                selected.PlayerHealth,
                static_cast<unsigned long long>(selected.HeroID),
                static_cast<unsigned long long>(selected.address),
                selected.Vis ? 1 : 0,
                selected.Team ? 1 : 0);
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for track back
    // =========================================================================

    inline Vector3 GetVector3fortrackback(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        if (local_entity.HeroID == eHero::HERO_HANJO) predit = true;
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 PreditPos, RootPos, Vel;
            for (size_t i = 0; i < entities.size(); i++) {
                bool teamPass = false;
                if (Config::aimbotTeam == 0) {
                    teamPass = !entities[i].Team && entities[i].address != local_entity.address;
                } else if (Config::aimbotTeam == 1) {
                    teamPass = entities[i].Team;
                } else if (Config::aimbotTeam == 2) {
                    teamPass = true;
                }
                if (entities[i].Alive && teamPass && entities[i].Vis) {
                    if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                    else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                    else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    Vel = entities[i].velocity;
                    if (predit) {
                        float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        Vel = TargetingDetail::AccelerationAwareVelocity(entities[i], dist, Config::predit_level);
                        AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                    }
                    Vector2 Vec2 = predit ? viewMatrix.WorldToScreen(PreditPos) : viewMatrix.WorldToScreen(RootPos);
                    float CrossDist = CrossHair.Distance(Vec2);
                    if (!Config::fov360) {
                        if (CrossDist <= Config::Fov) {
                            float score;
                            if (Config::aimbotPriority == 0) {
                                score = CrossDist;
                            } else if (Config::aimbotPriority == 1) {
                                score = entities[i].PlayerHealth;
                            } else {
                                score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            }
                            if (score < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = score;
                                TarGetIndex = i;
                            }
                        }
                    } else {
                        float worldDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (worldDist < origin) {
                            target = predit ? PreditPos : RootPos;
                            origin = worldDist;
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
                                bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = entities[TarGetIndex].velocity;
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist, Config::predit_level);
                            AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                            target = PreditPos;
                        }
                    } else {
                        float distbone[12] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 12; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 12) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            float dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = entities[TarGetIndex].velocity;
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist, Config::predit_level);
                            AimCorrection(&PreditPos, Vel, dist, Config::predit_level);
                            target = PreditPos;
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
        float origin = 100000.f;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                if (entities[i].HeroID == 0x16dd || entities[i].HeroID == 0x16ee) continue;
                if (entities[i].HeroID == eHero::HERO_GENJI && entities[i].skill2act) continue;
                bool teamPass = false;
                if (Config::aimbotTeam == 0) {
                    teamPass = !entities[i].Team && entities[i].address != local_entity.address;
                } else if (Config::aimbotTeam == 1) {
                    teamPass = entities[i].Team;
                } else if (Config::aimbotTeam == 2) {
                    teamPass = true;
                }
                if (entities[i].Alive && teamPass && entities[i].Vis) {
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
                        score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (entities[i].PlayerHealth > 200.f) score = entities[i].PlayerHealth;
                        if (entities[i].HeroID == eHero::HERO_ZENYATTA && entities[i].ultimate == 0.f) score = 1000.f;
                    } else if (Config::aimbotPriority == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
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

    inline Vector3 GetVector3aim2(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        float dist = 100000.f;
        Vector3 PreditPos, RootPos, Vel;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                bool teamPass = false;
                if (Config::aimbotTeam == 0) {
                    teamPass = !entities[i].Team && entities[i].address != local_entity.address;
                } else if (Config::aimbotTeam == 1) {
                    teamPass = entities[i].Team;
                } else if (Config::aimbotTeam == 2) {
                    teamPass = true;
                }
                if (entities[i].Alive && teamPass && entities[i].Vis) {
                    if (entities[i].Team) {
                        if (Config::Bone2 == 1)      { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone2 == 2) { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    } else {
                        if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                    }
                    Vel = entities[i].velocity;
                    if (predit) {
                        float dist2 = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        Vel = TargetingDetail::AccelerationAwareVelocity(entities[i], dist2, Config::predit_level2);
                        AimCorrection22(&PreditPos, Vel, dist2, Config::predit_level2);
                    }
                    Vector2 Vec2 = predit ? viewMatrix.WorldToScreen(PreditPos) : viewMatrix.WorldToScreen(RootPos);
                    float CrossDist = CrossHair.Distance(Vec2);
                    if (Config::fov360)
                        CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                    if (!Config::fov360) {
                        if (CrossDist <= Config::Fov) {
                            float score;
                            if (Config::aimbotPriority == 0) {
                                score = CrossDist;
                            } else if (Config::aimbotPriority == 1) {
                                score = entities[i].PlayerHealth;
                            } else {
                                score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            }
                            if (score < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = score;
                                TarGetIndex = i;
                            }
                        }
                    } else {
                        float worldDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (worldDist < origin) {
                            target = predit ? PreditPos : RootPos;
                            origin = worldDist;
                            TarGetIndex = i;
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
                                bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                                distbone[iii] = CrossHair.Distance(bonecrosspos);
                            }
                        }
                        int m = (int)(std::min_element(distbone, distbone + 5) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(index[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            Vel = entities[TarGetIndex].velocity;
                            float dist2 = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist2, Config::predit_level2);
                            AimCorrection22(&PreditPos, Vel, dist2, Config::predit_level2);
                            target = PreditPos;
                        }
                    } else {
                        float distbone[10] = { 0 };
                        Vector3 bonerootpos{};
                        Vector2 bonecrosspos{};
                        for (int iii = 0; iii < 10; iii++) {
                            bonerootpos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[iii]);
                            bonecrosspos = viewMatrix.WorldToScreen(bonerootpos);
                            distbone[iii] = CrossHair.Distance(bonecrosspos);
                        }
                        int m = (int)(std::min_element(distbone, distbone + 10) - distbone);
                        RootPos = entities[TarGetIndex].GetBonePos(entities[TarGetIndex].GetSkel()[m]);
                        PreditPos = RootPos;
                        target = RootPos;
                        if (predit) {
                            Vel = entities[TarGetIndex].velocity;
                            float dist2 = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[TarGetIndex], dist2, Config::predit_level2);
                            AimCorrection22(&PreditPos, Vel, dist2, Config::predit_level2);
                            target = PreditPos;
                        }
                    }
                }
            }
        }
        return target;
    }

    // =========================================================================
    // GetVector3 for auto-FOV target
    // =========================================================================

    inline Vector3 GetVector3forfov(bool predit = false) {
        int TarGetIndex = -1;
        Vector3 target{};
        Vector2 CrossHair = TargetingDetail::CrosshairCenter();
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                bool teamPass = false;
                if (Config::aimbotTeam == 0) {
                    teamPass = !entities[i].Team && entities[i].address != local_entity.address;
                } else if (Config::aimbotTeam == 1) {
                    teamPass = entities[i].Team;
                } else if (Config::aimbotTeam == 2) {
                    teamPass = true;
                }
                if (entities[i].Alive && teamPass && entities[i].Vis) {
                    PreditPos = entities[i].head_pos;
                    RootPos = entities[i].head_pos;
                    Vector2 Vec2 = viewMatrix.WorldToScreen(RootPos);
                    float CrossDist = CrossHair.Distance(Vec2);
                    float score;
                    if (Config::aimbotPriority == 0) {
                        score = CrossDist;
                    } else if (Config::aimbotPriority == 1) {
                        score = entities[i].PlayerHealth;
                    } else {
                        score = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
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

        inline PIDState& GetPIDState() {
            static PIDState state;
            return state;
        }

        inline BezierState& GetBezierState() {
            static BezierState state;
            return state;
        }

        inline void ResetPIDState() {
            GetPIDState() = PIDState{};
        }

        inline void ResetBezierState() {
            GetBezierState() = BezierState{};
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

        inline void GenerateBezierControlPoints(BezierState& state, const Vector3& current, const Vector3& target) {
            const int intermediateCount = std::clamp(Config::aimBezierControlPoints, 2, 6);
            const float curvature = std::clamp(Config::aimBezierCurvature, 0.0f, 1.0f);
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

    inline Vector3 SmoothPID(Vector3 current, Vector3 target, float deltaTime) {
        AimSmoothingDetail::PIDState& state = AimSmoothingDetail::GetPIDState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const Vector3 error = target - current;
        const float deadzone = std::clamp(Config::aimPidDeadzone, 0.0f, 10.0f);
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
        state.integral = AimSmoothingDetail::ClampIntegral(
            state.integral,
            std::clamp(Config::aimPidMaxIntegral, 1.0f, 50.0f)
        );

        const Vector3 derivative = resetState
            ? Vector3{}
            : (error - state.previousError) / deltaTime;
        state.previousError = error;

        const Vector3 output =
            error * std::clamp(Config::aimPidP, 0.0f, 2.0f) +
            state.integral * std::clamp(Config::aimPidI, 0.0f, 0.5f) +
            derivative * std::clamp(Config::aimPidD, 0.0f, 1.0f);

        return current + AimSmoothingDetail::ClampStepToError(output, error);
    }

    inline Vector3 SmoothBezier(Vector3 current, Vector3 target, float deltaTime, float speed) {
        AimSmoothingDetail::BezierState& state = AimSmoothingDetail::GetBezierState();
        deltaTime = AimSmoothingDetail::ClampDeltaTime(deltaTime);

        const bool resetState = !state.initialized ||
            state.controlPoints.empty() ||
            AimSmoothingDetail::IsRetarget(target, state.lastTarget);

        if (resetState)
            AimSmoothingDetail::GenerateBezierControlPoints(state, current, target);
        else {
            state.lastTarget = target;
            state.controlPoints.back() = target;
        }

        state.t = std::clamp(
            state.t + std::clamp(speed, 1.0f, 200.0f) * deltaTime,
            0.0f,
            1.0f
        );

        if (state.t >= 1.0f)
            return target;

        return AimSmoothingDetail::EvaluateBezier(state.controlPoints, state.t);
    }

    inline Vector3 SmoothDispatch(Vector3 local, Vector3 target, float speed, float accel) {
        (void)accel;

        const float deltaTime = AimSmoothingDetail::ComputeDeltaTime();
        const int method = std::clamp(Config::aimMethod, 0, 2);
        static int previousMethod = -1;

        if (method != previousMethod) {
            AimSmoothingDetail::ResetPIDState();
            AimSmoothingDetail::ResetBezierState();
            previousMethod = method;
        }

        const Vector3 error = target - local;
        Diagnostics::Aim("smooth.dispatch method=%d speed=%.9f accel=%.9f dt=%.9f local=(%.9f,%.9f,%.9f) target=(%.9f,%.9f,%.9f) error=(%.9f,%.9f,%.9f) error_len=%.9f",
            method,
            speed,
            accel,
            deltaTime,
            local.X,
            local.Y,
            local.Z,
            target.X,
            target.Y,
            target.Z,
            error.X,
            error.Y,
            error.Z,
            error.Size());

        Vector3 result{};
        switch (method) {
        case 0:
            result = SmoothLinear(local, target, speed);
            break;
        case 1:
            result = SmoothPID(local, target, deltaTime);
            break;
        case 2:
            result = SmoothBezier(local, target, deltaTime, Config::aimBezierSpeed);
            break;
        default:
            result = SmoothLinear(local, target, speed);
            break;
        }

        const Vector3 outputDelta = result - local;
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

} // namespace OW
