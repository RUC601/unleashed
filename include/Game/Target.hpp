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

#include "Game/Decrypt.hpp"
#include "Game/Entity.hpp"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Utils/Config.hpp"

extern std::mutex g_mutex;

namespace OW {

    // =========================================================================
    // Global extern declarations (defined in Overwatch.hpp)
    // =========================================================================

    extern Matrix viewMatrix;
    extern Matrix viewMatrix_xor;
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
        if (Config::kmboxEnabled) {
            const float sensitivity = std::clamp(Config::kmboxAimSensitivity, 0.1f, 5.0f);
            const int pixelX = static_cast<int>(std::lround(delta.X * sensitivity));
            const int pixelY = static_cast<int>(std::lround(delta.Y * sensitivity));

            if (Config::kmboxDebugLog) {
                std::printf("[KMBOX] mouse.move delta=(%.6f, %.6f, %.6f) pixels=(%d,%d) time=%dms type=%d\n",
                    delta.X, delta.Y, delta.Z, pixelX, pixelY, moveTimeMs, Config::kmboxDeviceType);
            }

            if (pixelX == 0 && pixelY == 0)
                return;

            if (Config::kmboxDeviceType == 0)
                kmbox::KmBoxMgr.Mouse.Move(pixelX, pixelY);
            else
                kmbox::kmBoxBMgr.km_move(pixelX, pixelY);
            return;
        }
    }

    inline void SendMouseMove(float deltaX, float deltaY, int moveTimeMs = -1) {
        SendMouseMove(Vector3(deltaX, deltaY, 0.0f), moveTimeMs);
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
        int vk;
        switch (setting) {
        case 0: break;
        case 1: vk = VK_LBUTTON; break;
        case 2: vk = VK_RBUTTON; break;
        case 3: vk = VK_MBUTTON; break;
        case 4: vk = VK_XBUTTON1; break;
        case 5: vk = VK_XBUTTON2; break;
        case 6: vk = 0x41; break;  case 7: vk = 0x42; break;
        case 8: vk = 0x43; break;  case 9: vk = 0x44; break;
        case 10: vk = 0x45; break; case 11: vk = 0x46; break;
        case 12: vk = 0x47; break; case 13: vk = 0x48; break;
        case 14: vk = 0x49; break; case 15: vk = 0x4A; break;
        case 16: vk = 0x4B; break; case 17: vk = 0x4C; break;
        case 18: vk = 0x4D; break; case 19: vk = 0x4E; break;
        case 20: vk = 0x4F; break; case 21: vk = 0x50; break;
        case 22: vk = 0x51; break; case 23: vk = 0x52; break;
        case 24: vk = 0x53; break; case 25: vk = 0x54; break;
        case 26: vk = 0x55; break; case 27: vk = 0x56; break;
        case 28: vk = 0x57; break; case 29: vk = 0x58; break;
        case 30: vk = 0x59; break; case 31: vk = 0x5A; break;
        case 32: vk = 0x31; break; case 33: vk = 0x32; break;
        case 34: vk = 0x33; break; case 35: vk = 0x34; break;
        case 36: vk = 0x35; break; case 37: vk = 0x36; break;
        case 38: vk = 0x37; break; case 39: vk = 0x38; break;
        case 40: vk = 0x39; break; case 41: vk = 0x30; break;
        case 42: vk = VK_F1; break;  case 43: vk = VK_F2; break;
        case 44: vk = VK_F3; break;  case 45: vk = VK_F4; break;
        case 46: vk = VK_F5; break;  case 47: vk = VK_F6; break;
        case 48: vk = VK_F7; break;  case 49: vk = VK_F8; break;
        case 50: vk = VK_F9; break;  case 51: vk = VK_F10; break;
        case 52: vk = VK_F11; break; case 53: vk = VK_F12; break;
        case 54: vk = VK_MENU; break;
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
        if (Config::hanzo_flick || local_entity.HeroID == eHero::HERO_HANJO || Config::Gravitypredit) {
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
            return Vector2(
                (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
                (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
            );
        }

        inline Vector3 CameraPosition() {
            const auto camera = viewMatrix_xor.get_location();
            return Vector3(camera.x, camera.y, camera.z);
        }

        inline bool IsTrainingBot(uint64_t heroId) {
            return heroId == eHero::HERO_TRAININGBOT1 ||
                   heroId == eHero::HERO_TRAININGBOT2 ||
                   heroId == eHero::HERO_TRAININGBOT3 ||
                   heroId == eHero::HERO_TRAININGBOT4 ||
                   heroId == eHero::HERO_TRAININGBOT5 ||
                   heroId == eHero::HERO_TRAININGBOT6 ||
                   heroId == eHero::HERO_TRAININGBOT7;
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
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        auto entities = TargetingDetail::SnapshotEntities();
        auto hp_dy_entities = TargetingDetail::SnapshotDynamicEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();

        float origin = 100000.f;
        if (entities.size() > 0) {
            if (Config::hanzoautospeed) {
                if (local_entity.HeroID == eHero::HERO_HANJO) {
                    Config::predit_level = readult(local_entity.SkillBase + 0x40, 0xB, 0x2A5) * 85.f + 25.f;
                    if (local_entity.skill2act) Config::predit_level = 110.f;
                }
            }
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                if (!Config::switch_team) {
                    if (entities[i].Alive && entities[i].Team && entities[i].Vis) {
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
                        if (CrossDist < origin && CrossDist <= Config::Fov && !Config::fov360) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        } else if (Config::fov360) {
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            if (CrossDist < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    }
                } else if (Config::switch_team) {
                    if (entities[i].Alive && !entities[i].Team && entities[i].Vis && entities[i].address != local_entity.address) {
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
                        if (CrossDist < origin && CrossDist <= Config::Fov && !Config::fov360) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        } else if (Config::fov360) {
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            if (CrossDist < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
                if (Config::autobone && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = (entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT1 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT2 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT3 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT4 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT5 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT6 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT7);
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
                    } else if (Config::fov360) {
                        target = Vector3(hppack.POS.x, hppack.POS.y, hppack.POS.z);
                    }
                    break;
                }
            }
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
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 PreditPos, RootPos, Vel;
            for (size_t i = 0; i < entities.size(); i++) {
                if (!Config::switch_team) {
                    if (entities[i].Alive && entities[i].Team && entities[i].Vis) {
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
                        if (CrossDist < origin && CrossDist <= Config::Fov) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        }
                    }
                } else if (Config::switch_team) {
                    if (entities[i].Alive && !entities[i].Team && entities[i].Vis && entities[i].address != local_entity.address) {
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
                        if (CrossDist < origin && CrossDist <= Config::Fov && !Config::fov360) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        } else if (Config::fov360) {
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            if (CrossDist < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                if ((Config::autobone || Config::autobone2) && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = (entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT1 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT2 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT3 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT4 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT5 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT6 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT7);
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
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                if (entities[i].HeroID == 0x16dd || entities[i].HeroID == 0x16ee) continue;
                if (entities[i].HeroID == eHero::HERO_GENJI && entities[i].skill2act) continue;
                if (!Config::switch_team) {
                    if (entities[i].Alive && entities[i].Team && entities[i].Vis) {
                        Vector3 PreditPos;
                        if (!local_entity.skillcd1)
                            PreditPos = entities[i].GetBonePos(entities[i].GetSkel()[16]);
                        else
                            PreditPos = entities[i].GetBonePos(entities[i].GetSkel()[2]);
                        bool isBot = (entities[i].HeroID == eHero::HERO_TRAININGBOT1 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT2 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT3 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT4 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT5 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT6 ||
                                      entities[i].HeroID == eHero::HERO_TRAININGBOT7);
                        if (isBot) {
                            PreditPos = entities[i].GetBonePos(3);
                            if (!local_entity.skillcd1) PreditPos.Y -= 0.4f;
                        }
                        Vector3 RootPos = PreditPos;
                        if (entities[i].HeroID == eHero::HERO_WRECKINGBALL) {
                            PreditPos.Y -= 0.7f;
                            RootPos.Y -= 0.7f;
                        }
                        float CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (entities[i].PlayerHealth > 200.f) CrossDist = entities[i].PlayerHealth;
                        if (entities[i].HeroID == eHero::HERO_ZENYATTA && entities[i].ultimate == 0.f) CrossDist = 1000.f;
                        if (CrossDist < origin && CrossDist <= 6000.f) {
                            target = RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        }
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
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        float dist = 100000.f;
        Vector3 PreditPos, RootPos, Vel;
        if (entities.size() > 0) {
            for (size_t i = 0; i < entities.size(); i++) {
                if (!Config::switch_team2) {
                    if (entities[i].Alive && entities[i].Team && entities[i].Vis) {
                        if (Config::Bone2 == 1)      { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone2 == 2) { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                        Vel = entities[i].velocity;
                        if (predit) {
                            dist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[i], dist, Config::predit_level2);
                            AimCorrection22(&PreditPos, Vel, dist, Config::predit_level2);
                        }
                        Vector2 Vec2 = predit ? viewMatrix.WorldToScreen(PreditPos) : viewMatrix.WorldToScreen(RootPos);
                        float CrossDist = CrossHair.Distance(Vec2);
                        if (Config::fov360)
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                        if (CrossDist < origin && CrossDist <= Config::Fov && !Config::fov360) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        } else if (Config::fov360) {
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            if (CrossDist < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    }
                } else if (Config::switch_team2) {
                    if (entities[i].Alive && !entities[i].Team && entities[i].Vis && entities[i].address != local_entity.address) {
                        if (Config::Bone == 1)       { PreditPos = entities[i].head_pos; RootPos = entities[i].head_pos; }
                        else if (Config::Bone == 2)  { PreditPos = entities[i].neck_pos; RootPos = entities[i].neck_pos; }
                        else                         { PreditPos = entities[i].chest_pos; RootPos = entities[i].chest_pos; }
                        Vel = entities[i].velocity;
                        if (predit) {
                            float dist2 = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            Vel = TargetingDetail::AccelerationAwareVelocity(entities[i], dist2, Config::predit_level2);
                            AimCorrection22(&PreditPos, Vel, dist2, Config::predit_level2);
                        }
                        Vector2 Vec2 = predit ? viewMatrix.WorldToScreen(PreditPos) : viewMatrix.WorldToScreen(RootPos);
                        float CrossDist = CrossHair.Distance(Vec2);
                        if (CrossDist < origin && CrossDist <= Config::Fov && !Config::fov360) {
                            target = predit ? PreditPos : RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        } else if (Config::fov360) {
                            CrossDist = Vector3(viewMatrix_xor.get_location().x, viewMatrix_xor.get_location().y, viewMatrix_xor.get_location().z).DistTo(PreditPos);
                            if (CrossDist < origin) {
                                target = predit ? PreditPos : RootPos;
                                origin = CrossDist;
                                TarGetIndex = i;
                            }
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
                if (Config::autobone2 && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    bool isBot = (entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT1 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT2 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT3 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT4 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT5 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT6 ||
                                  entities[TarGetIndex].HeroID == eHero::HERO_TRAININGBOT7);
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
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        auto entities = TargetingDetail::SnapshotEntities();
        auto local_entity = TargetingDetail::SnapshotLocalEntity();
        float origin = 100000.f;
        if (entities.size() > 0) {
            Vector3 Vel, PreditPos, RootPos;
            for (size_t i = 0; i < entities.size(); i++) {
                if (!Config::switch_team) {
                    if (entities[i].Alive && entities[i].Team && entities[i].Vis) {
                        PreditPos = entities[i].head_pos;
                        RootPos = entities[i].head_pos;
                        Vector2 Vec2 = viewMatrix.WorldToScreen(RootPos);
                        float CrossDist = CrossHair.Distance(Vec2);
                        if (CrossDist < origin) {
                            target = RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        }
                    }
                } else if (Config::switch_team) {
                    if (entities[i].Alive && !entities[i].Team && entities[i].Vis && entities[i].address != local_entity.address) {
                        PreditPos = entities[i].head_pos;
                        RootPos = entities[i].head_pos;
                        Vector2 Vec2 = viewMatrix.WorldToScreen(RootPos);
                        float CrossDist = CrossHair.Distance(Vec2);
                        if (CrossDist < origin) {
                            target = RootPos;
                            origin = CrossDist;
                            TarGetIndex = i;
                        }
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

} // namespace OW
