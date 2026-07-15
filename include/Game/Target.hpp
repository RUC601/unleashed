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
#include <cctype>
#include <cstdlib>
#include <atomic>
#include <functional>
#include <map>
#include <unordered_map>
#include <windows.h>
#include <chrono>
#include <optional>
#include <thread>
#include <utility>

#include "Game/Decrypt.hpp"
#include "Game/Entity.hpp"
#include "Game/FovGeometry.hpp"
#include "Game/HeroGeometrySpec.hpp"
#include "Game/InputOrchestrator.hpp"
#include "Game/LeadPrediction.hpp"
#include "Game/Motion.hpp"
#include "Game/TriggerBoneSelection.hpp"
#include "Game/WeaponSpec.hpp"
#include "Kmbox/KmboxRuntime.hpp"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"

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
    extern std::vector<c_entity> present_entities;
    extern std::vector<hpanddy> present_hp_dy_entities;
    extern std::mutex g_presentEntityMutex;
    extern std::vector<c_entity> present_render_entities;
    extern std::vector<hpanddy> present_render_hp_dy_entities;
    extern std::mutex g_presentRenderEntityMutex;

    inline int ReadPresentEnvFlagState(const char* name, int defaultValue)
    {
        char buffer[32]{};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0)
            return defaultValue;
        if (length >= sizeof(buffer))
            return defaultValue;

        std::string value(buffer, buffer + (std::min<DWORD>)(length, sizeof(buffer) - 1));
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (value == "1" || value == "true" || value == "yes" || value == "on")
            return 1;
        if (value == "0" || value == "false" || value == "no" || value == "off")
            return 0;
        return defaultValue;
    }

    inline DWORD ReadPresentEnvDword(const char* name, DWORD fallback, DWORD minValue, DWORD maxValue)
    {
        char buffer[32]{};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0)
            return fallback;
        if (length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer)
            return fallback;

        return std::clamp<DWORD>(static_cast<DWORD>(parsed), minValue, maxValue);
    }

    inline bool PresentInterpolationEnabled()
    {
        static const bool enabled = ReadPresentEnvFlagState("UN_DMA_PRESENT_INTERP", 0) == 1;
        return enabled;
    }

    inline bool PresentUseForRenderEnabled()
    {
        static const bool enabled =
            PresentInterpolationEnabled() &&
            ReadPresentEnvFlagState("UN_DMA_PRESENT_USE_FOR_RENDER", 1) == 1;
        return enabled;
    }

    inline DWORD PresentEntityHz()
    {
        static const DWORD hz = ReadPresentEnvDword("UN_DMA_ENTITY_PRESENT_HZ", 120, 30, 144);
        return hz;
    }

    inline DWORD PresentEntityDelayMs()
    {
        static const DWORD delayMs = ReadPresentEnvDword("UN_DMA_ENTITY_PRESENT_DELAY_MS", 16, 0, 48);
        return delayMs;
    }

    inline DWORD PresentRenderEntityDelayMs()
    {
        static const DWORD delayMs = ReadPresentEnvDword(
            "UN_DMA_ENTITY_PRESENT_RENDER_DELAY_MS",
            24,
            0,
            64);
        return delayMs;
    }

    inline DWORD PresentMaxExtrapolationMs()
    {
        static const DWORD maxMs = ReadPresentEnvDword("UN_DMA_PRESENT_MAX_EXTRAP_MS", 8, 0, 16);
        return maxMs;
    }

    inline DWORD PresentRenderMicroExtrapolationMs()
    {
        static const DWORD maxMs = ReadPresentEnvDword("UN_DMA_RENDER_PRESENT_MICRO_EXTRAP_MS", 8, 0, 16);
        return maxMs;
    }

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
        switch (kmbox::RuntimeController().Applied().descriptor.backend) {
        case kmbox::KmboxRuntimeBackend::Network: return "network";
        case kmbox::KmboxRuntimeBackend::Serial:  return "serial";
        case kmbox::KmboxRuntimeBackend::Mock:    return "mock";
        case kmbox::KmboxRuntimeBackend::None:    return "none";
        default:                                  return "unknown";
        }
    }

    inline bool ShouldSuppressKmboxOutput(
        const char* action,
        KmBoxOutputIntent intent = KmBoxOutputIntent::Normal) {
        if (intent == KmBoxOutputIntent::Normal &&
            !RuntimeOutputSessionEnabled()) {
            static std::atomic_flag sessionLogged = ATOMIC_FLAG_INIT;
            if (!sessionLogged.test_and_set(std::memory_order_relaxed)) {
                Diagnostics::Aim(
                    "kmbox.output suppressed action=%s reason=process_session_closed intent=%s",
                    action ? action : "unknown",
                    ToString(intent));
            }
            return true;
        }
        if (!ShouldSuppressOutputForMenu(Config::KmboxOutputSuppressedByMenu(), intent))
            return false;

        static std::atomic_flag logged = ATOMIC_FLAG_INIT;
        if (!logged.test_and_set(std::memory_order_relaxed)) {
            Diagnostics::Aim("kmbox.output suppressed action=%s reason=menu_open intent=%s",
                action ? action : "unknown",
                ToString(intent));
        }
        return true;
    }

    inline int EnqueueKmboxPixelMoveForGeneration(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs,
        std::uint64_t expectedGeneration,
        std::uint64_t expectedConnectionEpoch = 0) {
        if (ShouldSuppressKmboxOutput("mouse_move"))
            return Config::kKmboxOutputSuppressedStatus;

        const OutputRuntimeState runtime = CurrentKmboxOutputRuntimeState();
        if (!runtime.outputGateOpen || expectedGeneration == 0 ||
            runtime.backendGeneration != expectedGeneration ||
            (expectedConnectionEpoch != 0 &&
             (!ProcessConnection::IsConnected() ||
              ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch))) {
            return err_queue_stopped;
        }
        return kmbox::DispatchMouseMoveForGeneration(
            expectedGeneration,
            pixelX,
            pixelY,
            automoveRuntimeMs);
    }

    inline int EnqueueKmboxPixelMove(
        int pixelX,
        int pixelY,
        int automoveRuntimeMs) {
        const OutputRuntimeState runtime = CurrentKmboxOutputRuntimeState();
        return EnqueueKmboxPixelMoveForGeneration(
            pixelX,
            pixelY,
            automoveRuntimeMs,
            runtime.backendGeneration);
    }

    struct MouseMoveQuantizationState {
        float residualX = 0.0f;
        float residualY = 0.0f;
        std::uint64_t sessionGeneration = 0;
        std::uint64_t connectionEpoch = 0;
        std::uint64_t targetKey = 0;
        int lastForcedAxis = -1;
        bool initialized = false;
    };

    struct MouseMoveQuantizationOptions {
        MouseMoveQuantizationState* state = nullptr;
        Vector3 fallbackDirectionDelta{};
        bool forceNonZero = false;
    };

    struct MouseMoveQuantizationResult {
        int pixelX = 0;
        int pixelY = 0;
        int status = 0;
        bool forcedMinimumStep = false;
        bool dispatched = false;
    };

    inline void ResetMouseMoveQuantizationState(MouseMoveQuantizationState& state) {
        state = MouseMoveQuantizationState{};
    }

    inline bool HasMouseMoveQuantizationState(const MouseMoveQuantizationState& state) {
        return state.initialized ||
            state.residualX != 0.0f ||
            state.residualY != 0.0f;
    }

    inline bool PrepareMouseMoveQuantizationState(
        MouseMoveQuantizationState& state,
        std::uint64_t sessionGeneration,
        std::uint64_t connectionEpoch,
        std::uint64_t targetKey) {
        const bool changed =
            !state.initialized ||
            state.sessionGeneration != sessionGeneration ||
            state.connectionEpoch != connectionEpoch ||
            state.targetKey != targetKey;
        if (!changed)
            return false;

        ResetMouseMoveQuantizationState(state);
        state.sessionGeneration = sessionGeneration;
        state.connectionEpoch = connectionEpoch;
        state.targetKey = targetKey;
        state.initialized = true;
        return true;
    }

    inline MouseMoveQuantizationResult QuantizeMouseMoveCounts(
        float scaledX,
        float scaledY,
        float fallbackScaledX,
        float fallbackScaledY,
        MouseMoveQuantizationState& state,
        bool forceNonZero) {
        MouseMoveQuantizationResult result{};
        constexpr float kDirectionEpsilon = 0.0000001f;

        if (!std::isfinite(scaledX))
            scaledX = 0.0f;
        if (!std::isfinite(scaledY))
            scaledY = 0.0f;
        if (!std::isfinite(fallbackScaledX))
            fallbackScaledX = 0.0f;
        if (!std::isfinite(fallbackScaledY))
            fallbackScaledY = 0.0f;
        if (!std::isfinite(state.residualX))
            state.residualX = 0.0f;
        if (!std::isfinite(state.residualY))
            state.residualY = 0.0f;

        const float directionX = std::fabs(scaledX) > kDirectionEpsilon
            ? scaledX
            : fallbackScaledX;
        const float directionY = std::fabs(scaledY) > kDirectionEpsilon
            ? scaledY
            : fallbackScaledY;

        if (forceNonZero) {
            if (std::fabs(directionX) <= kDirectionEpsilon ||
                state.residualX * directionX < 0.0f) {
                state.residualX = 0.0f;
            }
            if (std::fabs(directionY) <= kDirectionEpsilon ||
                state.residualY * directionY < 0.0f) {
                state.residualY = 0.0f;
            }
        }

        state.residualX += scaledX;
        state.residualY += scaledY;
        result.pixelX = static_cast<int>(state.residualX);
        result.pixelY = static_cast<int>(state.residualY);
        state.residualX -= static_cast<float>(result.pixelX);
        state.residualY -= static_cast<float>(result.pixelY);

        if (!forceNonZero || result.pixelX != 0 || result.pixelY != 0)
            return result;

        // Pick the minimum-step axis from the current error, not from an older
        // residual.  The residual on the other axis is still retained below.
        const float candidateX = directionX;
        const float candidateY = directionY;

        const float absX = std::fabs(candidateX);
        const float absY = std::fabs(candidateY);
        if (absX <= kDirectionEpsilon && absY <= kDirectionEpsilon)
            return result;

        bool forceX = absX > absY;
        if (std::fabs(absX - absY) <= kDirectionEpsilon)
            forceX = state.lastForcedAxis != 0;
        if (forceX && absX <= kDirectionEpsilon)
            forceX = false;
        else if (!forceX && absY <= kDirectionEpsilon)
            forceX = true;

        if (forceX) {
            result.pixelX = candidateX > 0.0f ? 1 : -1;
            state.residualX = 0.0f;
            state.lastForcedAxis = 0;
        } else {
            result.pixelY = candidateY > 0.0f ? 1 : -1;
            state.residualY = 0.0f;
            state.lastForcedAxis = 1;
        }
        result.forcedMinimumStep = true;
        return result;
    }

    inline MouseMoveQuantizationState& DefaultMouseMoveQuantizationState() {
        static MouseMoveQuantizationState state{};
        return state;
    }

    namespace OutputMotionTelemetry {

        struct Sample {
            uint64_t sequence = 0;
            uint64_t capturedAtMs = 0;
            float deltaPitchRad = 0.0f;
            float deltaYawRad = 0.0f;
            int pixelX = 0;
            int pixelY = 0;
            int automoveRuntimeMs = 0;
            int status = 0;
            bool split = false;
            int steps = 1;
            float speedDegS = 0.0f;
            float accelDegS2 = 0.0f;
            float jerkDegS3 = 0.0f;
        };

        inline uint64_t TimestampMs() {
            const auto now = std::chrono::system_clock::now();
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        }

        inline std::mutex& Mutex() {
            static std::mutex mutex;
            return mutex;
        }

        inline std::vector<Sample>& Samples() {
            static std::vector<Sample> samples;
            return samples;
        }

        inline uint64_t& Sequence() {
            static uint64_t sequence = 0;
            return sequence;
        }

        inline void RecordMouseMove(
            const Vector3& delta,
            int pixelX,
            int pixelY,
            int automoveRuntimeMs,
            int status,
            bool split,
            int steps) {
            constexpr float kRadToDeg = 57.29577951308232f;
            const uint64_t nowMs = TimestampMs();
            const float deltaMagnitudeDeg = std::sqrt(delta.X * delta.X + delta.Y * delta.Y) * kRadToDeg;

            std::lock_guard<std::mutex> lock(Mutex());
            auto& samples = Samples();
            Sample sample{};
            sample.sequence = ++Sequence();
            sample.capturedAtMs = nowMs;
            sample.deltaPitchRad = delta.X;
            sample.deltaYawRad = delta.Y;
            sample.pixelX = pixelX;
            sample.pixelY = pixelY;
            sample.automoveRuntimeMs = automoveRuntimeMs;
            sample.status = status;
            sample.split = split;
            sample.steps = (std::max)(1, steps);

            if (!samples.empty()) {
                const Sample& previous = samples.back();
                constexpr float kMinTelemetryDtS = 0.008f;
                float dt = kMinTelemetryDtS;
                if (nowMs > previous.capturedAtMs)
                    dt = (std::max)(kMinTelemetryDtS, static_cast<float>(nowMs - previous.capturedAtMs) / 1000.0f);
                sample.speedDegS = deltaMagnitudeDeg / dt;
                sample.accelDegS2 = (sample.speedDegS - previous.speedDegS) / dt;
                sample.jerkDegS3 = (sample.accelDegS2 - previous.accelDegS2) / dt;
            }

            samples.push_back(sample);
            constexpr size_t kCapacity = 512;
            if (samples.size() > kCapacity) {
                samples.erase(samples.begin(),
                    samples.begin() + static_cast<std::ptrdiff_t>(samples.size() - kCapacity));
            }
        }

        inline std::vector<Sample> SnapshotSince(uint64_t capturedAtMs, uint64_t windowMs) {
            std::vector<Sample> snapshot;
            std::lock_guard<std::mutex> lock(Mutex());
            const auto& samples = Samples();
            snapshot.reserve(samples.size());
            for (const Sample& sample : samples) {
                if (sample.capturedAtMs <= capturedAtMs && capturedAtMs - sample.capturedAtMs <= windowMs)
                    snapshot.push_back(sample);
            }
            return snapshot;
        }

    } // namespace OutputMotionTelemetry

    inline MouseMoveQuantizationResult SendMouseMove(
        const Vector3& delta,
        int moveTimeMs,
        const MouseMoveQuantizationOptions& quantization) {
        MouseMoveQuantizationResult result{};
        const std::uint64_t connectionEpoch =
            ProcessConnection::ConnectionEpoch();
        const OutputRuntimeState outputRuntime =
            CurrentKmboxOutputRuntimeState();
        if (connectionEpoch == 0 || !ProcessConnection::IsConnected() ||
            !outputRuntime.outputGateOpen ||
            outputRuntime.backendGeneration == 0) {
            Diagnostics::Aim(
                "mouse.move early_return reason=stale_or_unavailable_output_session connectionEpoch=%llu generation=%llu gate=%d",
                static_cast<unsigned long long>(connectionEpoch),
                static_cast<unsigned long long>(outputRuntime.backendGeneration),
                outputRuntime.outputGateOpen ? 1 : 0);
            return result;
        }
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
        const kmbox::KmboxRuntimeAppliedState applied = kmbox::RuntimeController().Applied();
        if (applied.descriptor.backend == kmbox::KmboxRuntimeBackend::None ||
            !kmbox::RuntimeController().CanDispatch(applied.generation)) {
            static bool once = false;
            if (!once) {
                std::printf("[KMBOX] SendMouseMove called without an active KMBox runtime!\n");
                once = true;
            }
            Diagnostics::Aim("mouse.move early_return reason=no_active_runtime lastError=%d",
                kmbox::RuntimeController().LastError());
            return result;
        }
        if (ShouldSuppressKmboxOutput("mouse_move")) {
            Diagnostics::Aim("mouse.move early_return reason=menu_open_suppressed");
            return result;
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

            static int callCount = 0;
            MouseMoveQuantizationState& quantizationState = quantization.state
                ? *quantization.state
                : DefaultMouseMoveQuantizationState();

            // delta.X = pitch (vertical), delta.Y = yaw (horizontal).
            // Positive KMBox X drives the measured yaw negative, so yaw correction is inverted here.
            const float scaledYaw   = -delta.Y * sensitivity;         // yaw -> horizontal X
            const float pitchScale = std::clamp(Config::aimbotPitchScale, 0.1f, 3.0f);
            const float scaledPitch = delta.X * pitchSensitivity * pitchScale; // pitch -> vertical Y
            const float fallbackScaledYaw =
                -quantization.fallbackDirectionDelta.Y * sensitivity;
            const float fallbackScaledPitch =
                quantization.fallbackDirectionDelta.X * pitchSensitivity * pitchScale;
            const float accumBeforeX = quantizationState.residualX;
            const float accumBeforeY = quantizationState.residualY;
            result = QuantizeMouseMoveCounts(
                scaledYaw,
                scaledPitch,
                fallbackScaledYaw,
                fallbackScaledPitch,
                quantizationState,
                quantization.forceNonZero);
            const int pixelX = result.pixelX;
            const int pixelY = result.pixelY;

            ++callCount;
            Diagnostics::Aim("mouse.convert call=%d delta_rad_pitch=%.9f delta_rad_yaw=%.9f baseCountsPerRad=%.6f yawCountsPerRad=%.6f pitchCountsPerRad=%.6f autoScale=%d syncScale=%.6f pitchScale=%.6f scaled_counts=(yaw=%.9f,pitch=%.9f) fallback_counts=(yaw=%.9f,pitch=%.9f) accum_before=(%.9f,%.9f) counts=(%d,%d) accum_after=(%.9f,%.9f) forced_min_step=%d",
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
                fallbackScaledYaw,
                fallbackScaledPitch,
                accumBeforeX,
                accumBeforeY,
                pixelX,
                pixelY,
                quantizationState.residualX,
                quantizationState.residualY,
                result.forcedMinimumStep ? 1 : 0);
            if (callCount <= 50 || pixelX != 0 || pixelY != 0) {
                std::printf("[KMBOX] #%d pitch=%.6f yaw=%.6f yawCountsPerRad=%.1f pitchCountsPerRad=%.1f counts=(%d,%d) accum=(%.3f,%.3f)\n",
                    callCount, delta.X, delta.Y, sensitivity, pitchSensitivity,
                    pixelX, pixelY,
                    quantizationState.residualX,
                    quantizationState.residualY);
            }

            if (pixelX == 0 && pixelY == 0) {
                Diagnostics::Aim("mouse.move early_return reason=zero_counts scaled=(yaw_%.9f,pitch_%.9f) accum_after=(%.9f,%.9f) forced_min_step=%d note=%s",
                    scaledYaw,
                    scaledPitch,
                    quantizationState.residualX,
                    quantizationState.residualY,
                    result.forcedMinimumStep ? 1 : 0,
                    quantization.forceNonZero
                        ? "magnetic_no_valid_direction"
                        : "integer_truncation_waiting_for_accumulator");
                return result;
            }

            if (result.forcedMinimumStep) {
                static DWORD lastForcedMinimumStepLogTick = 0;
                const DWORD now = GetTickCount();
                if (Config::aimVerboseLog ||
                    lastForcedMinimumStepLogTick == 0 ||
                    now - lastForcedMinimumStepLogTick >= 250) {
                    Diagnostics::Aim("mouse.move magnetic_quantize forced_min_step=1 counts=(%d,%d) raw_delta=(%.9f,%.9f) residual=(%.9f,%.9f)",
                        pixelX,
                        pixelY,
                        quantization.fallbackDirectionDelta.X,
                        quantization.fallbackDirectionDelta.Y,
                        quantizationState.residualX,
                        quantizationState.residualY);
                    lastForcedMinimumStepLogTick = now;
                }
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
                const int status = EnqueueKmboxPixelMoveForGeneration(
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    outputRuntime.backendGeneration,
                    connectionEpoch);
                result.status = status;
                result.dispatched = status == success;
                Diagnostics::Aim("mouse.enqueue transport=%s command=%s pixel=(%d,%d) runtimeMs=%d behavior=%d splitEnabled=%d status=%d",
                    KmboxTransportName(),
                    automoveRuntimeMs > 0 ? "automove" : "move",
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    splitBehavior,
                    splitEnabled ? 1 : 0,
                    status);
                OutputMotionTelemetry::RecordMouseMove(
                    delta,
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    status,
                    false,
                    1);
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
                int lastStatus = 0;
                int completedSteps = 0;
                for (int i = 0; i < steps; i++) {
                    const int curX = remainingX / (steps - i);
                    const int curY = remainingY / (steps - i);
                    remainingX -= curX;
                    remainingY -= curY;

                    const int status = EnqueueKmboxPixelMoveForGeneration(
                        curX,
                        curY,
                        automoveRuntimeMs,
                        outputRuntime.backendGeneration,
                        connectionEpoch);
                    lastStatus = status;
                    Diagnostics::Aim("mouse.enqueue.split transport=%s command=%s step=%d/%d cur=(%d,%d) runtimeMs=%d status=%d",
                        KmboxTransportName(),
                        automoveRuntimeMs > 0 ? "automove" : "move",
                        i + 1,
                        steps,
                        curX,
                        curY,
                        automoveRuntimeMs,
                        status);

                    if (status != success) {
                        Diagnostics::Aim(
                            "mouse.move.split_abort step=%d/%d status=%d expectedGeneration=%llu connectionEpoch=%llu",
                            i + 1,
                            steps,
                            status,
                            static_cast<unsigned long long>(
                                outputRuntime.backendGeneration),
                            static_cast<unsigned long long>(connectionEpoch));
                        break;
                    }
                    ++completedSteps;

                    if (i < steps - 1 && delayUs > 0) {
                        std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
                    }
                }
                Diagnostics::Aim("mouse.move.split_complete steps=%d total=(%d,%d)",
                    completedSteps, pixelX, pixelY);
                OutputMotionTelemetry::RecordMouseMove(
                    delta,
                    pixelX,
                    pixelY,
                    automoveRuntimeMs,
                    lastStatus,
                    true,
                    completedSteps);
                result.status = lastStatus;
                result.dispatched = completedSteps > 0;
            }
            return result;
        }
    }

    inline void SendMouseMove(const Vector3& delta, int moveTimeMs = -1) {
        MouseMoveQuantizationOptions quantization{};
        quantization.state = &DefaultMouseMoveQuantizationState();
        (void)SendMouseMove(delta, moveTimeMs, quantization);
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
        if (ShouldSuppressKmboxOutput("calibration_move")) {
            Diagnostics::Aim("kmbox.calibration skipped reason=normal_output_suppressed");
            return 0.0f;
        }
        const OutputRuntimeState outputRuntime =
            CurrentKmboxOutputRuntimeState();
        const std::uint64_t connectionEpoch =
            ProcessConnection::ConnectionEpoch();
        if (connectionEpoch == 0 || !ProcessConnection::IsConnected() ||
            !outputRuntime.outputGateOpen ||
            outputRuntime.backendGeneration == 0) {
            Diagnostics::Warn(
                "[CALIBRATE] Calibration output is unavailable for the current runtime session.");
            return 0.0f;
        }

        // 1. Set calibration flag
        Config::calibrationInProgress = true;

        // 2. Read initial view direction from remote memory and convert to Euler radians.
        Vector3 angleBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller));
        Sleep(Config::calibrationStabilityWaitMs);
        angleBefore = DirectionToAimEuler(ReadPlayerControllerViewDirection(SDK->g_player_controller)); // re-read for stability

        // 3. Send a known horizontal mouse move (only yaw matters)
        int moveX = Config::calibrationMovePixels;
        int moveY = 0;
        if (EnqueueKmboxPixelMoveForGeneration(
                moveX,
                moveY,
                0,
                outputRuntime.backendGeneration,
                connectionEpoch) != success) {
            Diagnostics::Warn("[CALIBRATE] Horizontal calibration output was rejected by the active runtime.");
            Config::calibrationInProgress = false;
            return 0.0f;
        }

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
            if (EnqueueKmboxPixelMoveForGeneration(
                    pitchMoveX,
                    pitchMoveY,
                    0,
                    outputRuntime.backendGeneration,
                    connectionEpoch) != success) {
                Diagnostics::Warn("[CALIBRATE] Vertical calibration output was rejected by the active runtime.");
                Config::calibrationInProgress = false;
                return 0.0f;
            }

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

    enum class GameAction : uint8_t {
        PrimaryFire = 0,
        SecondaryFire,
        MiddleMouse,
        Ability1,
        Ability2,
        Ultimate,
        Melee,
        MoveForward
    };

    enum class ActionOutputStatus : uint8_t {
        Sent = 0,
        Disabled,
        Suppressed,
        UnsupportedTransport,
        InvalidAction,
        TransportError
    };

    inline bool ActionOutputSucceeded(ActionOutputStatus status) {
        return status == ActionOutputStatus::Sent;
    }

    inline ActionOutputStatus UnavailableActionOutputStatus() {
        return kmbox::RuntimeController().Desired().backend == kmbox::KmboxRuntimeBackend::None
            ? ActionOutputStatus::Disabled
            : ActionOutputStatus::TransportError;
    }

    inline std::string DefaultMouseButtonOwnerKey(OutputOwnerSource ownerSource, int button) {
        return "target.mouse." +
            std::to_string(static_cast<unsigned int>(ownerSource)) + "." +
            std::to_string(button);
    }

    inline const char* GameActionOwnerKey(GameAction action) {
        switch (action) {
        case GameAction::PrimaryFire:   return "game_action.primary_fire";
        case GameAction::SecondaryFire: return "game_action.secondary_fire";
        case GameAction::MiddleMouse:   return "game_action.middle_mouse";
        case GameAction::Ability1:      return "game_action.ability1";
        case GameAction::Ability2:      return "game_action.ability2";
        case GameAction::Ultimate:      return "game_action.ultimate";
        case GameAction::Melee:         return "game_action.melee";
        case GameAction::MoveForward:   return "game_action.move_forward";
        default:                        return nullptr;
        }
    }

    inline ActionOutputStatus SetOwnedOutputControl(
        const std::string& ownerKey,
        OutputOwnerSource ownerSource,
        OutputControl control,
        bool down,
        const char* outputName,
        bool requireKeyboardOutput = false,
        OutputUnownedControlCallback ownerlessRelease = {},
        std::uint64_t expectedGeneration = 0) {
        const kmbox::KmboxRuntimeAppliedState applied = kmbox::RuntimeController().Applied();
        if (applied.descriptor.backend == kmbox::KmboxRuntimeBackend::None ||
            !kmbox::RuntimeController().CanDispatch(applied.generation) ||
            (expectedGeneration != 0 &&
             applied.generation != expectedGeneration)) {
            return UnavailableActionOutputStatus();
        }
        const KmBoxOutputIntent intent = OutputIntentForState(down);
        if (ShouldSuppressKmboxOutput(outputName, intent))
            return ActionOutputStatus::Suppressed;
        if (requireKeyboardOutput &&
            !kmbox::SupportsKeyboardOutput(applied.descriptor.backend)) {
            return ActionOutputStatus::UnsupportedTransport;
        }

        OutputScheduler& scheduler = RuntimeOutputScheduler();
        if (expectedGeneration != 0 && down &&
            !scheduler.SynchronizeRuntime(
                { expectedGeneration, true })) {
            return ActionOutputStatus::TransportError;
        }
        bool accepted = false;
        if (expectedGeneration != 0) {
            accepted = !down && ownerlessRelease
                ? scheduler.ReleaseManualControlOrExecuteIfUnownedForGeneration(
                    ownerKey,
                    ownerSource,
                    control,
                    expectedGeneration,
                    std::move(ownerlessRelease))
                : scheduler.SetManualControlsForGeneration(
                    ownerKey,
                    ownerSource,
                    down
                        ? std::vector<OutputControl>{ control }
                        : std::vector<OutputControl>{},
                    expectedGeneration);
        } else {
            accepted = !down && ownerlessRelease
                ? scheduler.ReleaseManualControlOrExecuteIfUnowned(
                    ownerKey,
                    ownerSource,
                    control,
                    std::move(ownerlessRelease))
                : scheduler.SetManualControl(
                    ownerKey,
                    ownerSource,
                    control,
                    down);
        }
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    inline ActionOutputStatus SendMouseButtonActionState(
        int button,
        bool down,
        OutputOwnerSource ownerSource = OutputOwnerSource::GlobalAim,
        const char* ownerKey = nullptr,
        std::uint64_t expectedGeneration = 0) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;

        if (Config::kmboxDebugLog) {
            std::printf("[KMBOX] mouse.button button=%d down=%d transport=%s generation=%llu\n",
                button,
                down ? 1 : 0,
                KmboxTransportName(),
                static_cast<unsigned long long>(
                    kmbox::RuntimeController().Applied().generation));
        }

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        return SetOwnedOutputControl(
            resolvedOwnerKey,
            ownerSource,
            { OutputControlKind::MouseButton,
              static_cast<std::uint16_t>(button) },
            down,
            "mouse_button",
            false,
            {},
            expectedGeneration);
    }

    inline ActionOutputStatus ReleaseMouseButtonActionStateOrPhysicalFallback(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration,
        bool* physicalFallbackReleased = nullptr) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;
        if (physicalFallbackReleased)
            *physicalFallbackReleased = false;

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        const kmbox::KmboxRuntimeAppliedState applied =
            kmbox::RuntimeController().Applied();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            applied.descriptor.backend == kmbox::KmboxRuntimeBackend::None ||
            !kmbox::RuntimeController().CanDispatch(applied.generation)) {
            return UnavailableActionOutputStatus();
        }
        if (ShouldSuppressKmboxOutput(
                "mouse_button",
                KmBoxOutputIntent::SafetyRelease)) {
            return ActionOutputStatus::Suppressed;
        }

        const bool accepted = RuntimeOutputScheduler()
            .ReleaseManualControlOrExecuteIfUnownedForGeneration(
            resolvedOwnerKey,
            ownerSource,
            {
                OutputControlKind::MouseButton,
                static_cast<std::uint16_t>(button)
            },
            expectedGeneration,
            [button, physicalFallbackReleased](
                std::uint64_t backendGeneration) {
                const bool released =
                    kmbox::DispatchForceReleaseMouseButtonForGeneration(
                        backendGeneration,
                        button) == success;
                if (physicalFallbackReleased)
                    *physicalFallbackReleased = released;
                return released;
            });
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    inline ActionOutputStatus ReleaseMouseButtonActionStateForGeneration(
        int button,
        OutputOwnerSource ownerSource,
        const char* ownerKey,
        std::uint64_t expectedGeneration) {
        if (button < 0 || button > 2)
            return ActionOutputStatus::InvalidAction;

        const kmbox::KmboxRuntimeAppliedState applied =
            kmbox::RuntimeController().Applied();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            applied.descriptor.backend == kmbox::KmboxRuntimeBackend::None ||
            !kmbox::RuntimeController().CanDispatch(applied.generation)) {
            return UnavailableActionOutputStatus();
        }
        if (ShouldSuppressKmboxOutput(
                "mouse_button",
                KmBoxOutputIntent::SafetyRelease)) {
            return ActionOutputStatus::Suppressed;
        }

        const std::string resolvedOwnerKey = ownerKey && *ownerKey
            ? std::string(ownerKey)
            : DefaultMouseButtonOwnerKey(ownerSource, button);
        const bool accepted = RuntimeOutputScheduler()
            .ReleaseManualControlOrExecuteIfUnownedForGeneration(
            resolvedOwnerKey,
            ownerSource,
            {
                OutputControlKind::MouseButton,
                static_cast<std::uint16_t>(button)
            },
            expectedGeneration,
            [](std::uint64_t) { return true; });
        return accepted
            ? ActionOutputStatus::Sent
            : ActionOutputStatus::TransportError;
    }

    inline void SendMouseButton(
        int button,
        bool down,
        OutputOwnerSource ownerSource = OutputOwnerSource::GlobalAim,
        const char* ownerKey = nullptr) {
        (void)SendMouseButtonActionState(button, down, ownerSource, ownerKey);
    }

    inline unsigned char GameActionKeyboardHid(GameAction action) {
        switch (action) {
        case GameAction::Ability1:    return KEY_LEFTSHIFT;
        case GameAction::Ability2:    return KEY_E;
        case GameAction::Ultimate:    return KEY_Q;
        case GameAction::Melee:       return KEY_V;
        case GameAction::MoveForward: return KEY_W;
        default:                      return 0;
        }
    }

    inline ActionOutputStatus SetActionStateForGeneration(
        GameAction action,
        bool down,
        std::uint64_t expectedGeneration) {
        const char* ownerKey = GameActionOwnerKey(action);
        if (!ownerKey)
            return ActionOutputStatus::InvalidAction;

        switch (action) {
        case GameAction::PrimaryFire:
            return SendMouseButtonActionState(
                0, down, OutputOwnerSource::GameAction, ownerKey,
                expectedGeneration);
        case GameAction::SecondaryFire:
            return SendMouseButtonActionState(
                1, down, OutputOwnerSource::GameAction, ownerKey,
                expectedGeneration);
        case GameAction::MiddleMouse:
            return SendMouseButtonActionState(
                2, down, OutputOwnerSource::GameAction, ownerKey,
                expectedGeneration);
        default:
            break;
        }

        const unsigned char hidCode = GameActionKeyboardHid(action);
        if (hidCode == 0)
            return ActionOutputStatus::InvalidAction;
        return SetOwnedOutputControl(
            ownerKey,
            OutputOwnerSource::GameAction,
            {
                hidCode >= 0xE0 && hidCode <= 0xE7
                    ? OutputControlKind::KeyboardModifier
                    : OutputControlKind::KeyboardUsage,
                hidCode
            },
            down,
            "keyboard",
            true,
            {},
            expectedGeneration);
    }

    inline ActionOutputStatus SetActionState(GameAction action, bool down) {
        return SetActionStateForGeneration(action, down, 0);
    }

    inline ActionOutputStatus PulseAction(GameAction action, DWORD durationMs = 10) {
        const std::uint64_t operationGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        const ActionOutputStatus pressStatus = SetActionStateForGeneration(
            action,
            true,
            operationGeneration);
        if (!ActionOutputSucceeded(pressStatus))
            return pressStatus;

        Sleep(durationMs);
        return SetActionStateForGeneration(
            action,
            false,
            operationGeneration);
    }

    inline void ForceReleaseMouseButtons() {
        if (ShouldSuppressKmboxOutput(
                "force_release_buttons", KmBoxOutputIntent::SafetyRelease))
            return;
        (void)kmbox::DispatchForceReleaseMouseButtons();
    }

    inline void ForceReleaseMouseButton(int button) {
        if (ShouldSuppressKmboxOutput(
                "force_release_button", KmBoxOutputIntent::SafetyRelease))
            return;
        (void)kmbox::DispatchForceReleaseMouseButton(button);
    }

    inline bool SendMouseButtonStateMask(uint32_t stateMask, bool force = false) {
        if (ShouldSuppressKmboxOutput("mouse_button_state"))
            return false;
        return kmbox::DispatchMouseButtonStateMask(stateMask & 0x7u, force) == success;
    }

    inline bool MaskPhysicalMouseButtons(uint32_t mask) {
        if (ShouldSuppressKmboxOutput("mouse_mask"))
            return false;
        return kmbox::DispatchMouseMask(mask & 0x7Fu) == success;
    }

    inline bool UnmaskPhysicalMouseButtons() {
        if (ShouldSuppressKmboxOutput(
                "mouse_unmask", KmBoxOutputIntent::SafetyRelease))
            return false;
        return kmbox::DispatchMouseUnmask() == success;
    }

    struct PhysicalMouseMaskLeaseOwner {
        OutputOwnerSource source = OutputOwnerSource::GlobalAim;
        std::string key;

        bool operator<(const PhysicalMouseMaskLeaseOwner& other) const noexcept {
            if (source != other.source) {
                return static_cast<std::uint8_t>(source) <
                    static_cast<std::uint8_t>(other.source);
            }
            return key < other.key;
        }
    };

    class PhysicalMouseMaskLeaseCoordinator {
    public:
        using MaskDispatch =
            std::function<int(std::uint64_t generation, std::uint32_t mask)>;
        using UnmaskDispatch =
            std::function<int(std::uint64_t generation)>;
        using RuntimeSource = std::function<OutputRuntimeState()>;

        PhysicalMouseMaskLeaseCoordinator(
            MaskDispatch maskDispatch,
            UnmaskDispatch unmaskDispatch,
            RuntimeSource runtimeSource)
            : maskDispatch_(std::move(maskDispatch)),
              unmaskDispatch_(std::move(unmaskDispatch)),
              runtimeSource_(std::move(runtimeSource))
        {
        }

        bool Acquire(
            std::string ownerKey,
            OutputOwnerSource source,
            std::uint32_t mask)
        {
            if (!runtimeSource_)
                return false;
            const std::uint64_t expectedGeneration =
                runtimeSource_().backendGeneration;
            return Acquire(
                std::move(ownerKey),
                source,
                mask,
                expectedGeneration);
        }

        bool Acquire(
            std::string ownerKey,
            OutputOwnerSource source,
            std::uint32_t mask,
            std::uint64_t expectedGeneration)
        {
            const std::uint32_t effectiveMask = mask & 0x7Fu;
            if (ownerKey.empty() || effectiveMask == 0 ||
                expectedGeneration == 0 ||
                !maskDispatch_ || !unmaskDispatch_ || !runtimeSource_) {
                return false;
            }

            std::unique_lock<std::mutex> operationLock(operationMutex_);
            if (runtimeSource_().backendGeneration != expectedGeneration)
                return false;
            if (!RetryPendingWhileOperationLocked())
                return false;

            const OutputRuntimeState runtime = runtimeSource_();
            if (runtime.backendGeneration != expectedGeneration)
                return false;
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            if (!runtime.outputGateOpen || runtime.backendGeneration == 0)
                return false;

            const PhysicalMouseMaskLeaseOwner owner{
                source, std::move(ownerKey) };
            std::optional<std::uint32_t> previousMask;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                const auto existing = leases_.find(owner);
                if (existing != leases_.end()) {
                    if (existing->second == effectiveMask)
                        return true;
                    previousMask = existing->second;
                    existing->second = effectiveMask;
                } else {
                    leases_.emplace(owner, effectiveMask);
                }
            }

            if (ReconcileWhileOperationLocked(runtime.backendGeneration)) {
                return true;
            }

            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ == runtime.backendGeneration) {
                    if (previousMask.has_value())
                        leases_[owner] = *previousMask;
                    else
                        leases_.erase(owner);
                    RefreshPendingLocked();
                }
            }
            return false;
        }

        bool Release(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            if (!runtimeSource_)
                return false;
            const std::uint64_t expectedGeneration =
                runtimeSource_().backendGeneration;
            return Release(ownerKey, source, expectedGeneration);
        }

        bool Release(
            const std::string& ownerKey,
            OutputOwnerSource source,
            std::uint64_t expectedGeneration)
        {
            if (ownerKey.empty() || expectedGeneration == 0 || !runtimeSource_)
                return false;

            std::unique_lock<std::mutex> operationLock(operationMutex_);
            const OutputRuntimeState runtime = runtimeSource_();
            if (runtime.backendGeneration != expectedGeneration)
                return false;
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);

            bool removed = false;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                removed = leases_.erase({ source, ownerKey }) != 0;
                if (removed)
                    RefreshPendingLocked();
            }
            if (!removed)
                return RetryPendingWhileOperationLocked();
            return ReconcileWhileOperationLocked(runtime.backendGeneration);
        }

        bool RetryPending()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            return RetryPendingWhileOperationLocked();
        }

        bool IsActive(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return leases_.find({ source, ownerKey }) != leases_.end();
        }

        bool IsConfirmedActive(
            const std::string& ownerKey,
            OutputOwnerSource source)
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            const auto lease = leases_.find({ source, ownerKey });
            return lease != leases_.end() && hardwareKnown_ &&
                (appliedMask_ & lease->second) == lease->second;
        }

        std::uint32_t AggregateMask()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            (void)RetryPendingWhileOperationLocked();
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return DesiredAggregateLocked();
        }

        bool HasPending()
        {
            std::unique_lock<std::mutex> operationLock(operationMutex_);
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            return pending_;
        }

    private:
        std::uint32_t DesiredAggregateLocked() const
        {
            std::uint32_t aggregate = 0;
            for (const auto& [owner, mask] : leases_) {
                (void)owner;
                aggregate |= mask;
            }
            return aggregate & 0x7Fu;
        }

        void RefreshPendingLocked()
        {
            pending_ = !hardwareKnown_ ||
                appliedMask_ != DesiredAggregateLocked();
        }

        void NormalizeGenerationWhileOperationLocked(
            std::uint64_t generation)
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            if (generation_ == generation)
                return;
            generation_ = generation;
            leases_.clear();
            appliedMask_ = 0;
            hardwareKnown_ = true;
            pending_ = false;
        }

        void MarkHardwareUnknown(std::uint64_t generation)
        {
            std::lock_guard<std::mutex> stateLock(stateMutex_);
            if (generation_ != generation)
                return;
            hardwareKnown_ = false;
            pending_ = true;
        }

        bool ReconcileWhileOperationLocked(std::uint64_t generation)
        {
            std::uint32_t desiredMask = 0;
            std::uint32_t appliedMask = 0;
            bool hardwareKnown = true;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ != generation)
                    return false;
                desiredMask = DesiredAggregateLocked();
                appliedMask = appliedMask_;
                hardwareKnown = hardwareKnown_;
            }

            const bool needsBaseline = !hardwareKnown ||
                (appliedMask & ~desiredMask) != 0;
            if (needsBaseline) {
                if (unmaskDispatch_(generation) != success) {
                    MarkHardwareUnknown(generation);
                    return false;
                }
                {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ != generation)
                        return false;
                    appliedMask_ = 0;
                    hardwareKnown_ = true;
                    pending_ = desiredMask != 0;
                }
                appliedMask = 0;
            }

            if (desiredMask != appliedMask) {
                const OutputRuntimeState observed = runtimeSource_();
                if (observed != OutputRuntimeState{ generation, true }) {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ == generation)
                        pending_ = true;
                    return false;
                }
                if (maskDispatch_(generation, desiredMask) != success) {
                    MarkHardwareUnknown(generation);
                    return false;
                }
                {
                    std::lock_guard<std::mutex> stateLock(stateMutex_);
                    if (generation_ != generation)
                        return false;
                    appliedMask_ = desiredMask;
                    hardwareKnown_ = true;
                    pending_ = false;
                }
                return true;
            }

            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                if (generation_ != generation)
                    return false;
                appliedMask_ = desiredMask;
                hardwareKnown_ = true;
                pending_ = false;
            }
            return true;
        }

        bool RetryPendingWhileOperationLocked()
        {
            const OutputRuntimeState runtime = runtimeSource_();
            NormalizeGenerationWhileOperationLocked(runtime.backendGeneration);
            bool pending = false;
            {
                std::lock_guard<std::mutex> stateLock(stateMutex_);
                pending = pending_;
            }
            if (!pending)
                return true;
            return ReconcileWhileOperationLocked(runtime.backendGeneration);
        }

        MaskDispatch maskDispatch_;
        UnmaskDispatch unmaskDispatch_;
        RuntimeSource runtimeSource_;
        std::mutex operationMutex_;
        std::mutex stateMutex_;
        std::map<PhysicalMouseMaskLeaseOwner, std::uint32_t> leases_;
        std::uint64_t generation_ = 0;
        std::uint32_t appliedMask_ = 0;
        bool hardwareKnown_ = true;
        bool pending_ = false;
    };

    inline PhysicalMouseMaskLeaseCoordinator& RuntimePhysicalMouseMaskLeases()
    {
        static PhysicalMouseMaskLeaseCoordinator coordinator(
            [](std::uint64_t generation, std::uint32_t mask) {
                return kmbox::DispatchMouseMaskAndWaitForGeneration(
                    generation,
                    mask);
            },
            [](std::uint64_t generation) {
                return kmbox::DispatchMouseUnmaskAndWaitForGeneration(
                    generation);
            },
            []() { return CurrentKmboxOutputRuntimeState(); });
        return coordinator;
    }

    inline bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask,
        std::uint64_t expectedGeneration)
    {
        const kmbox::KmboxRuntimeAppliedState applied =
            kmbox::ActiveRuntimeSnapshot();
        if (expectedGeneration == 0 ||
            applied.generation != expectedGeneration ||
            (applied.descriptor.backend != kmbox::KmboxRuntimeBackend::Network &&
             applied.descriptor.backend != kmbox::KmboxRuntimeBackend::Mock)) {
            return false;
        }
        return RuntimePhysicalMouseMaskLeases().Acquire(
            std::move(ownerKey),
            source,
            mask,
            expectedGeneration);
    }

    inline bool AcquirePhysicalMouseMaskLease(
        std::string ownerKey,
        OutputOwnerSource source,
        std::uint32_t mask)
    {
        const std::uint64_t expectedGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        return AcquirePhysicalMouseMaskLease(
            std::move(ownerKey),
            source,
            mask,
            expectedGeneration);
    }

    inline bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source,
        std::uint64_t expectedGeneration)
    {
        return RuntimePhysicalMouseMaskLeases().Release(
            ownerKey,
            source,
            expectedGeneration);
    }

    inline bool ReleasePhysicalMouseMaskLease(
        const std::string& ownerKey,
        OutputOwnerSource source)
    {
        const std::uint64_t expectedGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        return ReleasePhysicalMouseMaskLease(
            ownerKey,
            source,
            expectedGeneration);
    }

    inline bool RetryPendingPhysicalMouseMaskLeases()
    {
        return RuntimePhysicalMouseMaskLeases().RetryPending();
    }

    inline bool IsPhysicalMouseMaskLeaseActive(
        const std::string& ownerKey,
        OutputOwnerSource source)
    {
        return RuntimePhysicalMouseMaskLeases().IsActive(ownerKey, source);
    }

    inline bool IsPhysicalMouseMaskLeaseConfirmed(
        const std::string& ownerKey,
        OutputOwnerSource source)
    {
        return RuntimePhysicalMouseMaskLeases().IsConfirmedActive(
            ownerKey,
            source);
    }

    inline std::uint32_t PhysicalMouseMaskAggregate()
    {
        return RuntimePhysicalMouseMaskLeases().AggregateMask();
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
        inline std::atomic<size_t> published_entity_count{0};

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

        using FovRuntimeContext = FovGeometry::RuntimeContext;

        inline bool IsFiniteVector(const Vector3& value) {
            return FovGeometry::IsFiniteVector(value);
        }

        inline Vector3 NormalizeVector(const Vector3& value) {
            return FovGeometry::NormalizeVector(value);
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
            return FovGeometry::AngularSeparationDegFromCamera(camera, a, b);
        }

        inline float FovScoreDeg(const FovRuntimeContext& context, const Vector3& position) {
            return FovGeometry::FovScoreDeg(context, position);
        }

        inline bool IsWithinFovDeg(const FovRuntimeContext& context,
                                   const Vector3& position,
                                   float fovDeg,
                                   float* outScoreDeg = nullptr) {
            return FovGeometry::IsWithinFovDeg(
                context,
                position,
                FovAngleLimitDeg(fovDeg),
                outScoreDeg);
        }

        using CandidateFovEvaluation = FovGeometry::CandidateFovEvaluation;

        inline bool EvaluateCandidateFov(const FovRuntimeContext& context,
                                         const Vector3& rawAimPoint,
                                         const Vector3& finalAimPoint,
                                         float fixedFovDeg,
                                         FovGeometry::ResolveFovForDistanceFn resolveFov,
                                         CandidateFovEvaluation& outEvaluation) {
            return FovGeometry::EvaluateCandidateFov(
                context,
                rawAimPoint,
                finalAimPoint,
                fixedFovDeg,
                resolveFov,
                outEvaluation);
        }

        inline bool IsTrainingBot(uint64_t heroId) {
            return GameData::IsTrainingBotHeroId(heroId);
        }

        inline bool IsSpecialAimEntity(uint64_t heroId) {
            return heroId == 0x16dd || heroId == 0x16ee;
        }

        inline void SetPublishedEntityCount(size_t count) {
            published_entity_count.store(count, std::memory_order_release);
            Diagnostics::RecordEntityPublish(count);
        }

        inline size_t PublishedEntityCount() {
            return published_entity_count.load(std::memory_order_acquire);
        }

        inline bool HasPublishedEntities() {
            return PublishedEntityCount() > 0;
        }

        inline bool TryPublishEntitySnapshots(
            std::vector<c_entity>&& nextEntities,
            std::vector<hpanddy>&& nextDynamicEntities,
            uint64_t expectedConnectionEpoch,
            size_t& publishedCount)
        {
            const size_t count = nextEntities.size();
            {
                std::lock_guard<std::mutex> lock(::g_mutex);
                if (expectedConnectionEpoch != 0 &&
                    (!ProcessConnection::IsConnected() ||
                     ProcessConnection::ConnectionEpoch() != expectedConnectionEpoch)) {
                    return false;
                }
                entities = std::move(nextEntities);
                hp_dy_entities = std::move(nextDynamicEntities);
                SetPublishedEntityCount(count);
                Diagnostics::SetEntityCount(count);
            }

            publishedCount = count;
            return true;
        }

        inline size_t PublishEntitySnapshots(std::vector<c_entity>&& nextEntities,
                                             std::vector<hpanddy>&& nextDynamicEntities) {
            size_t publishedCount = 0;
            TryPublishEntitySnapshots(
                std::move(nextEntities),
                std::move(nextDynamicEntities),
                0,
                publishedCount);
            return publishedCount;
        }

        inline std::vector<c_entity> SnapshotEntities() {
            const auto startedAt = std::chrono::steady_clock::now();
            std::vector<c_entity> snapshot;
            {
                std::lock_guard<std::mutex> lock(::g_mutex);
                snapshot = entities;
            }
            Diagnostics::RecordEntitySnapshotCopy(
                snapshot.size(),
                std::chrono::steady_clock::now() - startedAt);
            return snapshot;
        }

        inline std::vector<c_entity> SnapshotPresentEntities() {
            std::vector<c_entity> snapshot;
            if (!OW::PresentInterpolationEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
            snapshot = OW::present_entities;
            return snapshot;
        }

        inline std::vector<c_entity> SnapshotPresentRenderEntities() {
            std::vector<c_entity> snapshot;
            if (!OW::PresentUseForRenderEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
            snapshot = OW::present_render_entities;
            return snapshot;
        }

        inline std::vector<hpanddy> SnapshotDynamicEntities() {
            const auto startedAt = std::chrono::steady_clock::now();
            std::vector<hpanddy> snapshot;
            {
                std::lock_guard<std::mutex> lock(::g_mutex);
                snapshot = hp_dy_entities;
            }
            Diagnostics::RecordDynamicSnapshotCopy(
                snapshot.size(),
                std::chrono::steady_clock::now() - startedAt);
            return snapshot;
        }

        inline std::vector<hpanddy> SnapshotPresentDynamicEntities() {
            std::vector<hpanddy> snapshot;
            if (!OW::PresentInterpolationEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentEntityMutex);
            snapshot = OW::present_hp_dy_entities;
            return snapshot;
        }

        inline std::vector<hpanddy> SnapshotPresentRenderDynamicEntities() {
            std::vector<hpanddy> snapshot;
            if (!OW::PresentUseForRenderEnabled())
                return snapshot;

            std::lock_guard<std::mutex> lock(OW::g_presentRenderEntityMutex);
            snapshot = OW::present_render_hp_dy_entities;
            return snapshot;
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

        inline Vector3 CoreBoneRawPosition(const c_entity& entity, int boneId) {
            if (boneId == BONE_HEAD) return entity.head_pos;
            if (boneId == BONE_NECK) return entity.neck_pos;
            return entity.chest_pos;
        }

        inline Vector3 CoreBoneHitboxCenter(const c_entity& entity, int boneId) {
            return ResolveBoneHitboxCenter(entity, boneId, CoreBoneRawPosition(entity, boneId));
        }

        inline bool TryResolveSkeletonBonePoint(const c_entity& entity,
                                                std::size_t renderSlot,
                                                int& outBoneId,
                                                Vector3& outPoint) {
            if (renderSlot >= entity.skeleton_bones.size() ||
                renderSlot >= entity.skeleton_bone_valid.size()) {
                return false;
            }

            const int boneId = Plexies20260609::HitboxBoneIdForRenderSlot(renderSlot);
            if (boneId == Plexies20260609::kUnusedRenderSkeletonBone)
                return false;

            Vector3 rawPoint{};
            if (renderSlot == 0) {
                rawPoint = entity.head_pos;
            } else if (renderSlot == 1) {
                rawPoint = entity.neck_pos;
            } else if (renderSlot == 2) {
                rawPoint = entity.chest_pos;
            } else {
                if (!entity.skeleton_bone_valid[renderSlot])
                    return false;
                rawPoint = entity.skeleton_bones[renderSlot];
            }

            if (IsZeroVector(rawPoint) || rawPoint == entity.pos || !IsFiniteVector(rawPoint))
                return false;

            const Vector3 hitboxCenter = ResolveBoneHitboxCenter(entity, boneId, rawPoint);
            if (IsZeroVector(hitboxCenter) || !IsFiniteVector(hitboxCenter))
                return false;

            outBoneId = boneId;
            outPoint = hitboxCenter;
            return true;
        }

        inline SkeletonBoneMask EffectiveAimBoneMaskForDistance(SkeletonBoneMask rawMask,
                                                                 float distance) {
            SkeletonBoneMask mask = NormalizeSkeletonBoneMask(rawMask);
            const float headGate = std::clamp(Config::aimbotMaxHead, 0.0f, 500.0f);
            if (headGate > 0.0f &&
                std::isfinite(distance) &&
                distance > headGate &&
                SkeletonBoneMaskIncludesGroup(mask, kSkeletonBoneHead)) {
                mask &= ~kSkeletonBoneHead;
                // Preserve the legacy Head-only behavior: beyond Max Head
                // Distance it falls back to Neck instead of losing the target.
                if (mask == 0)
                    mask = kSkeletonBoneNeck;
            }
            return NormalizeSkeletonBoneMask(mask);
        }

        inline bool TrySelectClosestSelectedSkeletonPoint(
            const c_entity& entity,
            SkeletonBoneMask rawMask,
            const FovRuntimeContext& fovContext,
            const Matrix& view,
            const Vector2& crosshair,
            int& outBoneId,
            Vector3& outPoint,
            float* outScore = nullptr) {
            const SkeletonBoneMask mask = NormalizeSkeletonBoneMask(rawMask);
            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();

            for (std::size_t renderSlot = 0;
                 renderSlot < kSkeletonBoneGroupForRenderSlot.size();
                 ++renderSlot) {
                if (!SkeletonBoneMaskIncludesRenderSlot(mask, renderSlot))
                    continue;

                int boneId = Plexies20260609::kUnusedRenderSkeletonBone;
                Vector3 point{};
                if (!TryResolveSkeletonBonePoint(entity, renderSlot, boneId, point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outBoneId = boneId;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
        }

        inline Vector3 ConfiguredBonePosition(const c_entity& entity, int boneSetting) {
            const int normalized = Config::NormalizeAimBone(boneSetting);
            const int boneId = AimBoneToSkeletonBoneId(normalized);
            return CoreBoneHitboxCenter(entity, boneId);
        }

        inline int ResolveAimBoneForDistance(int configuredBone, float distance) {
            const int normalized = Config::NormalizeAimBone(configuredBone);
            const bool headRequested = normalized == Config::kAimBoneHead;
            const float headGate = std::clamp(Config::aimbotMaxHead, 0.0f, 500.0f);
            if (headRequested && headGate > 0.0f && distance > headGate)
                return Config::kAimBoneNeck;
            return normalized;
        }

        inline bool TrySelectClosestCoreAimPoint(const c_entity& entity,
                                                 const FovRuntimeContext& fovContext,
                                                 const Matrix& view,
                                                 const Vector2& crosshair,
                                                 int& outBoneId,
                                                 Vector3& outPoint,
                                                 float* outScore = nullptr) {
            static constexpr std::array<int, 3> kCoreBones{
                BONE_HEAD,
                BONE_NECK,
                BONE_CHEST,
            };

            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();
            for (int boneId : kCoreBones) {
                const Vector3 point = CoreBoneHitboxCenter(entity, boneId);
                if (IsZeroVector(point) || point == entity.pos || !IsFiniteVector(point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outBoneId = boneId;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
        }

        inline bool TrySelectClosestSkeletonAimPoint(const c_entity& entity,
                                                     const FovRuntimeContext& fovContext,
                                                     const Matrix& view,
                                                     const Vector2& crosshair,
                                                     size_t maxSkeletonBones,
                                                     Vector3& outPoint,
                                                     float* outScore = nullptr) {
            const size_t limit = (std::min)(
                maxSkeletonBones,
                (std::min)(entity.skeleton_bones.size(), entity.skeleton_bone_valid.size()));
            if (limit == 0)
                return false;

            bool found = false;
            float bestScore = (std::numeric_limits<float>::max)();
            for (size_t i = 0; i < limit; ++i) {
                if (!entity.skeleton_bone_valid[i])
                    continue;

                const Vector3 point = entity.skeleton_bones[i];
                if (IsZeroVector(point) || point == entity.pos || !IsFiniteVector(point))
                    continue;

                const float score = fovContext.valid
                    ? TargetingDetail::FovScoreDeg(fovContext, point)
                    : crosshair.Distance(view.WorldToScreen(point));
                if (!std::isfinite(score))
                    continue;

                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    outPoint = point;
                }
            }

            if (found && outScore)
                *outScore = bestScore;
            return found;
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

        inline std::mutex& LastTargetCandidateMutex() {
            static std::mutex mutex;
            return mutex;
        }

        inline TargetCandidate& LastTargetCandidateStorage() {
            static TargetCandidate candidate{};
            return candidate;
        }

        inline void StoreLastTargetCandidate(const TargetCandidate& candidate) {
            std::lock_guard<std::mutex> lock(LastTargetCandidateMutex());
            LastTargetCandidateStorage() = candidate;
        }

        inline TargetCandidate SnapshotLastTargetCandidate() {
            std::lock_guard<std::mutex> lock(LastTargetCandidateMutex());
            return LastTargetCandidateStorage();
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
            return TargetingDetail::FovScoreDeg(fovContext, position);
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
                const float distance = fovContext.valid
                    ? fovContext.camera.DistTo(rootPosition)
                    : local.pos.DistTo(rootPosition);
                const float effectiveFov = Config::ResolveDynamicAimFovForDistance(fov, distance);
                if (score > FovAngleLimitDeg(effectiveFov)) continue;

                result.index = static_cast<int>(i);
                result.target = aimPosition;
                result.entity = entity;
                result.score = score;
            }

            return result;
        }

        inline Vector3 SelectAutoBone(c_entity entity, const Vector2& crosshair, bool predit, bool secondary, size_t maxSkeletonBones) {
            const Matrix view = OW::SnapshotViewMatrix();
            const FovRuntimeContext fovContext = SnapshotFovRuntimeContext();
            int bestBoneId = BONE_CHEST;
            Vector3 bestRoot{};
            if (!TrySelectClosestCoreAimPoint(entity, fovContext, view, crosshair, bestBoneId, bestRoot)) {
                const Vector3 configuredRoot =
                    ConfiguredBonePosition(entity, secondary ? Config::Bone2 : Config::Bone);
                if (!IsZeroVector(configuredRoot) &&
                    configuredRoot != entity.pos &&
                    IsFiniteVector(configuredRoot)) {
                    bestRoot = configuredRoot;
                } else if (!TrySelectClosestSkeletonAimPoint(
                    entity, fovContext, view, crosshair, maxSkeletonBones, bestRoot)) {
                    return Vector3{};
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
        Diagnostics::Aim("target.primary start prediction=%d predictionMode=%d weapon=%s entities=%zu dynamic=%zu local_addr=0x%llX local_hero=0x%llX fovDeg=%.6f bone=%d autobone=%d boneMask=0x%03X teamMode=%d distance=(%.2f,%.2f) lock=(active=%d key=0x%llX minMs=%.0f maxMs=%.0f hysteresis=%.1f trace=%d unlock=%d) crosshair=(%.3f,%.3f)",
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
            static_cast<unsigned int>(Config::aimbotBoneMask),
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

                    const SkeletonBoneMask candidateMask =
                        TargetingDetail::EffectiveAimBoneMaskForDistance(
                            Config::aimbotBoneMask,
                            initialDistance);
                    int candidateBoneId = BONE_HEAD;
                    Vector3 rootPosition{};
                    if (!TargetingDetail::TrySelectClosestSelectedSkeletonPoint(
                            entities[i],
                            candidateMask,
                            fovContext,
                            aimViewMatrix,
                            CrossHair,
                            candidateBoneId,
                            rootPosition)) {
                        continue;
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        rootPosition,
                        projectileSpec,
                        resolvedPrediction,
                        false);
                    TargetingDetail::CandidateFovEvaluation fovEvaluation{};
                    if (TargetingDetail::EvaluateCandidateFov(
                            fovContext,
                            rootPosition,
                            lead.finalAimPoint,
                            Config::Fov,
                            &Config::ResolveDynamicAimFovForDistance,
                            fovEvaluation)) {
                        const float distance = fovEvaluation.distance;
                        const float effectiveFovDeg = fovEvaluation.effectiveFovDeg;
                        const float fovScoreDeg = fovEvaluation.scoreDeg;
                        if (!TargetingDetail::DistancePassesAimFilter(distance))
                            continue;

                        Vector2 Vec2 = CrossHair;
                        aimViewMatrix.WorldToScreen(lead.finalAimPoint, &Vec2, Vector2(OW::WX, OW::WY));
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
                            best.boneId = candidateBoneId;
                            best.rawAimPoint = rootPosition;
                            best.predictedAimPoint = lead.finalAimPoint;
                            best.aimPoint = lead.finalAimPoint;
                            best.screenPoint = Vec2;
                            best.distance = distance;
                            best.fovScore = fovScoreDeg;
                            best.effectiveFovDeg = effectiveFovDeg;
                            best.dynamicFov = Config::IsDynamicAimFovActive();
                            best.dynamicFovPresetId = best.dynamicFov ? Config::aimbotDynamicFovPresetId : -1;
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
                    const float effectiveFovDeg =
                        Config::ResolveDynamicAimFovForDistance(Config::Fov, bobDistance);
                    float fovScoreDeg = 0.0f;
                    if (!TargetingDetail::IsWithinFovDeg(fovContext, bobPosition, effectiveFovDeg, &fovScoreDeg))
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
                        best.effectiveFovDeg = effectiveFovDeg;
                        best.dynamicFov = Config::IsDynamicAimFovActive();
                        best.dynamicFovPresetId = best.dynamicFov ? Config::aimbotDynamicFovPresetId : -1;
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
        if (best.valid) {
            best.effectiveFovDeg = Config::ResolveDynamicAimFovForDistance(Config::Fov, best.distance);
            best.dynamicFov = Config::IsDynamicAimFovActive();
            best.dynamicFovPresetId = best.dynamicFov ? Config::aimbotDynamicFovPresetId : -1;
            const Config::RuntimeDrawFovState drawState = Config::SnapshotRuntimeDrawFov();
            if (drawState.active &&
                drawState.slotKind == static_cast<int>(Config::FovRingSlotKind::Aim)) {
                Config::SetRuntimeDrawFov(best.effectiveFovDeg, drawState.slotKind, drawState.slotIndex);
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
        TargetingDetail::StoreLastTargetCandidate(best);
        return best;
    }

    inline TargetCandidate AcquireTriggerTarget(TriggerBoneMask rawBoneMask,
                                                bool predit = false,
                                                bool ignoreInvisible = Config::triggerbotIgnoreInvisible,
                                                bool secondary = false) {
        TargetCandidate best{};
        const TriggerBoneMask boneMask = NormalizeTriggerBoneMask(rawBoneMask);
        auto entities = TargetingDetail::SnapshotEntities();
        auto localEntity = TargetingDetail::SnapshotLocalEntity();
        const Matrix view = OW::SnapshotViewMatrix();
        const TargetingDetail::FovRuntimeContext fovContext =
            TargetingDetail::SnapshotFovRuntimeContext();
        const WeaponSpec* weaponSpec = ResolveWeaponSpec(localEntity.HeroID, Config::aimbotAttack);
        const bool resolvedPrediction = ResolvePredictionEnabled(
            ClampPredictionOverride(Config::aimbotPredictionMode),
            weaponSpec,
            predit);
        const ProjectileRuntimeSpec projectileSpec =
            TargetingDetail::ResolveProjectileRuntimeSpec(weaponSpec, localEntity, secondary);
        const float hitboxScale = secondary ? Config::hitbox2 : Config::hitbox;
        float bestNormalizedScore = (std::numeric_limits<float>::max)();

        if (fovContext.valid) {
            for (std::size_t entityIndex = 0; entityIndex < entities.size(); ++entityIndex) {
                c_entity& entity = entities[entityIndex];
                if (!TargetingDetail::IsSelectableCandidate(entity, Config::aimbotTeam, localEntity))
                    continue;
                if (ignoreInvisible && !entity.Vis)
                    continue;
                if (entity.skill2act &&
                    entity.HeroID == eHero::HERO_GENJI &&
                    !entity.SameTeamAs(localEntity)) {
                    continue;
                }

                const Vector3 distanceAnchor = !TargetingDetail::IsZeroVector(entity.chest_pos)
                    ? entity.chest_pos
                    : entity.pos;
                const float initialDistance = fovContext.camera.DistTo(distanceAnchor);
                if (!TargetingDetail::DistancePassesAimFilter(initialDistance))
                    continue;

                for (std::size_t renderSlot = 0;
                     renderSlot < kTriggerBoneGroupForRenderSlot.size();
                     ++renderSlot) {
                    if (!TriggerBoneMaskIncludesRenderSlot(boneMask, renderSlot))
                        continue;

                    int boneId = Plexies20260609::kUnusedRenderSkeletonBone;
                    Vector3 rawAimPoint{};
                    if (!TargetingDetail::TryResolveSkeletonBonePoint(
                            entity,
                            renderSlot,
                            boneId,
                            rawAimPoint)) {
                        continue;
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entity,
                        rawAimPoint,
                        projectileSpec,
                        resolvedPrediction,
                        secondary);
                    const Vector3 aimPoint = lead.finalAimPoint;
                    if (TargetingDetail::IsZeroVector(aimPoint) ||
                        !TargetingDetail::IsFiniteVector(aimPoint))
                        continue;

                    const float hitWindow = ResolveEffectiveHitWindow(
                        entity.HeroID,
                        boneId,
                        weaponSpec,
                        hitboxScale,
                        Config::kLegacyDefaultHitboxRadius);
                    const float distance = fovContext.camera.DistTo(aimPoint);
                    if (!std::isfinite(hitWindow) || hitWindow <= 0.0f ||
                        !std::isfinite(distance) || distance <= 0.0001f) {
                        continue;
                    }

                    const float angularWindowDeg = RAD2DEG(hitWindow / distance);
                    const float angularScoreDeg = TargetingDetail::FovScoreDeg(fovContext, aimPoint);
                    if (!std::isfinite(angularWindowDeg) || angularWindowDeg <= 0.0f ||
                        !std::isfinite(angularScoreDeg) ||
                        angularScoreDeg > angularWindowDeg + 0.0001f) {
                        continue;
                    }

                    const float normalizedScore = angularScoreDeg /
                        (std::max)(angularWindowDeg, 0.0001f);
                    if (best.valid && normalizedScore >= bestNormalizedScore)
                        continue;

                    bestNormalizedScore = normalizedScore;
                    best.valid = true;
                    best.entityIndex = static_cast<int>(entityIndex);
                    best.entityKey = entity.address ? entity.address : entity.LinkBase;
                    best.entitySnapshot = entity;
                    best.boneId = boneId;
                    best.rawAimPoint = rawAimPoint;
                    best.predictedAimPoint = aimPoint;
                    best.aimPoint = aimPoint;
                    best.screenPoint = TargetingDetail::CrosshairCenter();
                    view.WorldToScreen(
                        aimPoint,
                        &best.screenPoint,
                        Vector2(OW::WX, OW::WY));
                    best.distance = distance;
                    best.fovScore = angularScoreDeg;
                    best.effectiveFovDeg = secondary
                        ? Config::ResolveDynamicAimFovForDistance(Config::Fov2, distance)
                        : Config::ResolveDynamicAimFovForDistance(Config::Fov, distance);
                    best.dynamicFov = Config::IsDynamicAimFovActive();
                    best.dynamicFovPresetId = best.dynamicFov
                        ? Config::aimbotDynamicFovPresetId
                        : -1;
                    best.effectiveHitWindow = hitWindow;
                    best.motion = TargetingDetail::EstimateMotionState(entity, best.screenPoint);
                    best.weaponSpec = weaponSpec;
                }
            }
        }

        Config::Targetenemyi = best.valid ? best.entityIndex : -1;
        if (best.valid)
            Config::health = best.entitySnapshot.PlayerHealth;
        if (secondary)
            Config::aimbotEffectiveHitWindow2 = best.valid ? best.effectiveHitWindow : 0.0f;
        else
            Config::aimbotEffectiveHitWindow = best.valid ? best.effectiveHitWindow : 0.0f;

        TargetingDetail::StoreLastTargetCandidate(best);
        Diagnostics::Aim(
            "target.trigger result=%s secondary=%d boneMask=0x%03X entityIndex=%d bone=%d scoreDeg=%.6f hitWindow=%.6f",
            best.valid ? "hit" : "none",
            secondary ? 1 : 0,
            static_cast<unsigned int>(boneMask),
            best.entityIndex,
            best.boneId,
            best.fovScore,
            best.effectiveHitWindow);
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
                    const float distance = TargetingDetail::CameraPosition().DistTo(RootPos);
                    const float effectiveFovDeg =
                        Config::ResolveDynamicAimFovForDistance(Config::Fov, distance);
                    float fovScoreDeg = 0.0f;
                    if (TargetingDetail::IsWithinFovDeg(fovContext, fovPoint, effectiveFovDeg, &fovScoreDeg)) {
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
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                if ((Config::autobone || Config::autobone2) && entities[TarGetIndex].HeroID != 0x16dd && entities[TarGetIndex].HeroID != 0x16ee) {
                    int closestBoneId = BONE_CHEST;
                    Vector3 closestRoot{};
                    if (TargetingDetail::TrySelectClosestCoreAimPoint(
                            entities[TarGetIndex],
                            fovContext,
                            aimViewMatrix,
                            CrossHair,
                            closestBoneId,
                            closestRoot)) {
                        RootPos = closestRoot;
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
                    Vector3 rootPosition{};
                    if (entities[i].Team) {
                        if (Config::Bone2 == 1)      rootPosition = entities[i].head_pos;
                        else if (Config::Bone2 == 2) rootPosition = entities[i].neck_pos;
                        else                         rootPosition = entities[i].chest_pos;
                    } else {
                        if (Config::Bone == 1)       rootPosition = entities[i].head_pos;
                        else if (Config::Bone == 2)  rootPosition = entities[i].neck_pos;
                        else                         rootPosition = entities[i].chest_pos;
                    }

                    int candidateBoneId = AimBoneToSkeletonBoneId(
                        entities[i].Team ? Config::Bone2 : Config::Bone);
                    if (Config::autobone2 &&
                        entities[i].HeroID != 0x16dd &&
                        entities[i].HeroID != 0x16ee) {
                        int closestBoneId = BONE_CHEST;
                        Vector3 closestRoot{};
                        if (TargetingDetail::TrySelectClosestCoreAimPoint(
                                entities[i],
                                fovContext,
                                aimViewMatrix,
                                CrossHair,
                                closestBoneId,
                                closestRoot)) {
                            candidateBoneId = closestBoneId;
                            rootPosition = closestRoot;
                        }
                    }

                    const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                        entities[i],
                        rootPosition,
                        projectileSpec,
                        resolvedPrediction,
                        true);
                    TargetingDetail::CandidateFovEvaluation fovEvaluation{};
                    if (TargetingDetail::EvaluateCandidateFov(
                            fovContext,
                            rootPosition,
                            lead.finalAimPoint,
                            Config::Fov2,
                            &Config::ResolveDynamicAimFovForDistance,
                            fovEvaluation)) {
                        const float distance = fovEvaluation.distance;
                        const float fovScoreDeg = fovEvaluation.scoreDeg;
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
                            selectedBoneId = candidateBoneId;
                        }
                    }
                }
            }
            if (TarGetIndex != -1) {
                Config::health = entities[TarGetIndex].PlayerHealth;
                Config::Targetenemyi = TarGetIndex;
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
                                            float bezierSpeedOverride = -1.0f,
                                            bool commitOvershootStep = true) {
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
        if (commitOvershootStep)
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

    inline void CommitAimSmoothingOutput(const Vector3& outputDelta) {
        AimSmoothingDetail::CommitOvershootStep(outputDelta);
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
