#include <Windows.h>

#include "Game/HeroSkills.hpp"

#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>

namespace OW {
namespace {

    using Clock = std::chrono::steady_clock;

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegToRad = kPi / 180.0f;
    constexpr float kRadToDeg = 180.0f / kPi;
    constexpr auto kBriefPulse = std::chrono::milliseconds(20);
    constexpr auto kActionPulseDebounce = std::chrono::milliseconds(500);
    constexpr float kPitchDoneEpsilonRad = 0.35f * kDegToRad;
    constexpr float kYawDoneEpsilonRad = 0.35f * kDegToRad;

    enum class SequencePhase {
        Hold,
        Release
    };

    struct SequenceRuntime {
        bool active = false;
        size_t stepIndex = 0;
        SequencePhase phase = SequencePhase::Hold;
        Clock::time_point phaseStarted{};
        Config::HeroSkillInputChannel activeChannel = Config::HeroSkillInputChannel::Primary;
        bool hasActiveChannel = false;
    };

    enum class ViewpointPhase {
        Idle,
        PitchDown,
        SecondaryDown,
        SecondaryUp,
        Delay,
        KeyboardDown,
        KeyboardUp,
        PitchUp,
        RestoreYaw,
        Completed,
        Cancelled
    };

    struct ViewpointRuntime {
        ViewpointPhase phase = ViewpointPhase::Idle;
        bool previousKeyDown = false;
        Clock::time_point phaseStarted{};
        Clock::time_point lastTick{};
        Vector3 initialAngles{};
        float pitchDownSpeedDeg = 0.0f;
        float pitchUpSpeedDeg = 0.0f;
        bool secondaryHeld = false;
        bool keyboardHeld = false;
        int keyboardVk = 0;
    };

    struct CandidateEntity {
        uint64_t entityAddress = 0;
        int entityIndex = -1;
        float distance = 0.0f;
        float vitalityPercent = 0.0f;
        float angularOffset = 0.0f;
        Vector3 position{};
        Vector3 velocity{};
    };

    struct TrajectoryParams {
        float speed = 60.0f;
        float maxRange = 45.0f;
        float gravity = 0.0f;
    };

    std::unordered_map<std::string, SequenceRuntime> g_sequences;
    std::unordered_map<std::string, ViewpointRuntime> g_viewpoints;
    std::unordered_map<std::string, Clock::time_point> g_lastActionExecutions;
    std::unordered_map<std::string, bool> g_timedActionsActive;
    std::mutex g_timedActionsMutex;
    std::mt19937 g_random{ std::random_device{}() };
    uint64_t g_lastHeroId = 0;

    bool SkillControls(const HeroSkillDefinition& definition, HeroSkillControlFlags control)
    {
        return HasHeroSkillControl(definition, control);
    }

    bool IsActivationKeyHeld(int activationKey)
    {
        const int vk = Labels::AimActivationKeyVk(activationKey);
        if (vk <= 0)
            return false;

        return AimbotDetail::IsInputVkDown(vk, activationKey);
    }

    int MouseButtonForChannel(Config::HeroSkillInputChannel channel)
    {
        return channel == Config::HeroSkillInputChannel::Secondary ? 1 : 0;
    }

    void SetOutputChannel(Config::HeroSkillInputChannel channel, bool down)
    {
        OW::SendMouseButton(MouseButtonForChannel(channel), down);
    }

    void ReleaseSequenceChannel(SequenceRuntime& runtime)
    {
        if (!runtime.hasActiveChannel)
            return;

        SetOutputChannel(runtime.activeChannel, false);
        runtime.hasActiveChannel = false;
    }

    void ReleaseAllOutputChannels()
    {
        OW::SendMouseButton(0, false);
        OW::SendMouseButton(1, false);
    }

    bool SendKeyboardState(int vk, bool down)
    {
        if (vk <= 0 || vk > 255)
            return false;

        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    }

    void ReleaseViewpointOutputs(ViewpointRuntime& runtime)
    {
        if (runtime.secondaryHeld) {
            SetOutputChannel(Config::HeroSkillInputChannel::Secondary, false);
            runtime.secondaryHeld = false;
        }
        if (runtime.keyboardHeld) {
            SendKeyboardState(runtime.keyboardVk, false);
            runtime.keyboardHeld = false;
        }
    }

    void CancelSequence(const std::string& skillId, SequenceRuntime& runtime, const char* reason)
    {
        if (runtime.active) {
            ReleaseSequenceChannel(runtime);
            Diagnostics::Info("Hero skill sequence cancelled. skill=%s reason=%s",
                skillId.c_str(), reason ? reason : "unknown");
        }
        runtime = SequenceRuntime{};
    }

    void CancelViewpoint(const std::string& skillId, ViewpointRuntime& runtime, const char* reason)
    {
        const bool wasActive = runtime.phase != ViewpointPhase::Idle &&
            runtime.phase != ViewpointPhase::Completed &&
            runtime.phase != ViewpointPhase::Cancelled;
        ReleaseViewpointOutputs(runtime);
        runtime.phase = ViewpointPhase::Cancelled;
        runtime.lastTick = Clock::now();
        if (wasActive) {
            Diagnostics::Warn("Hero skill viewpoint controller cancelled. skill=%s reason=%s",
                skillId.c_str(), reason ? reason : "unknown");
        }
    }

    bool TryReadCurrentViewAngles(Vector3& angles)
    {
        Vector3 direction = ReadPlayerControllerViewDirection(SDK ? SDK->g_player_controller : 0);
        float length = std::sqrt(direction.X * direction.X + direction.Y * direction.Y + direction.Z * direction.Z);
        if (std::isfinite(direction.X) && std::isfinite(direction.Y) &&
            std::isfinite(direction.Z) && length > 0.5f && length < 1.5f) {
            angles = DirectionToAimEuler(direction);
            return true;
        }

        Matrix view{}, viewXor{};
        GetViewMatricesSnapshot(view, viewXor);
        (void)view;
        const DirectX::XMFLOAT3 forward = viewXor.get_rotation();
        length = std::sqrt(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        if (!std::isfinite(forward.x) || !std::isfinite(forward.y) ||
            !std::isfinite(forward.z) || length <= 0.5f || length >= 1.5f) {
            return false;
        }

        angles = DirectionToAimEuler(Vector3(forward.x, forward.y, forward.z));
        return true;
    }

    float NormalizeAngle(float value)
    {
        while (value > kPi)
            value -= 2.0f * kPi;
        while (value < -kPi)
            value += 2.0f * kPi;
        return value;
    }

    float ClampDelta(float delta, float maxStep)
    {
        return std::clamp(delta, -maxStep, maxStep);
    }

    float DeltaSeconds(ViewpointRuntime& runtime, Clock::time_point now)
    {
        if (runtime.lastTick.time_since_epoch().count() == 0) {
            runtime.lastTick = now;
            return 1.0f / 144.0f;
        }

        const float dt = std::chrono::duration<float>(now - runtime.lastTick).count();
        runtime.lastTick = now;
        return std::clamp(dt, 1.0f / 1000.0f, 0.1f);
    }

    float PickPhaseSpeed(float baseSpeed, float randomRange)
    {
        baseSpeed = std::clamp(baseSpeed, 0.0f, 720.0f);
        randomRange = std::clamp(randomRange, 0.0f, 720.0f);
        if (randomRange <= 0.0f)
            return baseSpeed;

        const float low = (std::max)(0.0f, baseSpeed - randomRange);
        const float high = (std::min)(720.0f, baseSpeed + randomRange);
        if (high <= low)
            return low;

        std::uniform_real_distribution<float> distribution(low, high);
        return distribution(g_random);
    }

    void BeginViewpoint(const std::string& skillId,
                        ViewpointRuntime& runtime,
                        const Config::HeroSkillSettings& params)
    {
        Vector3 currentAngles{};
        if (!TryReadCurrentViewAngles(currentAngles)) {
            runtime.phase = ViewpointPhase::Cancelled;
            Diagnostics::Warn("Hero skill viewpoint start failed: no view angle. skill=%s", skillId.c_str());
            return;
        }

        runtime.initialAngles = currentAngles;
        runtime.pitchDownSpeedDeg = PickPhaseSpeed(params.pitchDownSpeed, params.pitchDownRandomRange);
        runtime.pitchUpSpeedDeg = PickPhaseSpeed(params.pitchUpSpeed, params.pitchUpRandomRange);
        runtime.keyboardVk = std::clamp(params.jumpKeyCode, 0, 255);
        runtime.phase = ViewpointPhase::PitchDown;
        runtime.phaseStarted = Clock::now();
        runtime.lastTick = runtime.phaseStarted;
        runtime.secondaryHeld = false;
        runtime.keyboardHeld = false;

        Diagnostics::Info("Hero skill viewpoint started. skill=%s downSpeed=%.2f upSpeed=%.2f initialPitch=%.2f initialYaw=%.2f",
            skillId.c_str(),
            runtime.pitchDownSpeedDeg,
            runtime.pitchUpSpeedDeg,
            runtime.initialAngles.X * kRadToDeg,
            runtime.initialAngles.Y * kRadToDeg);
    }

    bool MovePitchToward(float targetPitch, float speedDeg, float deltaSeconds)
    {
        Vector3 current{};
        if (!TryReadCurrentViewAngles(current))
            return false;

        const float deltaPitch = targetPitch - current.X;
        if (std::fabs(deltaPitch) <= kPitchDoneEpsilonRad)
            return true;

        const float maxStep = (std::max)(0.0f, speedDeg) * kDegToRad * deltaSeconds;
        if (maxStep <= 0.0f)
            return true;

        const float step = ClampDelta(deltaPitch, maxStep);
        SendMouseMove(Vector3(step, 0.0f, 0.0f));
        return std::fabs(deltaPitch) <= (maxStep + kPitchDoneEpsilonRad);
    }

    bool MoveYawToward(float targetYaw, float speedDeg, float deltaSeconds)
    {
        Vector3 current{};
        if (!TryReadCurrentViewAngles(current))
            return false;

        const float deltaYaw = NormalizeAngle(targetYaw - current.Y);
        if (std::fabs(deltaYaw) <= kYawDoneEpsilonRad)
            return true;

        const float maxStep = (std::max)(0.0f, speedDeg) * kDegToRad * deltaSeconds;
        if (maxStep <= 0.0f)
            return true;

        const float step = ClampDelta(deltaYaw, maxStep);
        SendMouseMove(Vector3(0.0f, step, 0.0f));
        return std::fabs(deltaYaw) <= (maxStep + kYawDoneEpsilonRad);
    }

    struct ScopedTrackingConfig {
        Config::HeroPreset originalPreset{};
        int originalAimMethod = 0;
        bool active = false;

        explicit ScopedTrackingConfig(const Config::HeroSkillTrackingParams& params)
        {
            originalPreset = Config::MakeHeroPresetFromCurrent();
            {
                std::lock_guard<std::mutex> lock(Config::mutex);
                originalAimMethod = Config::aimMethod;
            }

            Config::HeroPreset overlay = originalPreset;
            overlay.fov = params.fov;
            overlay.smooth = params.smooth;
            overlay.bone = params.bone;
            overlay.hitbox = params.hitbox;
            overlay.aimMode = 0;
            Config::ApplyHeroPresetToGlobals(overlay);
            {
                std::lock_guard<std::mutex> lock(Config::mutex);
                Config::aimMethod = params.method;
            }
            active = true;
        }

        ~ScopedTrackingConfig()
        {
            if (!active)
                return;

            Config::ApplyHeroPresetToGlobals(originalPreset);
            std::lock_guard<std::mutex> lock(Config::mutex);
            Config::aimMethod = originalAimMethod;
        }
    };

    void RunTrackingOverlayTick(const Config::HeroSkillTrackingParams& params)
    {
        if (params.fov <= 0.0f)
            return;

        ScopedTrackingConfig trackingOverride(params);
        const Vector3 targetVector = GetVector3(false);
        c_entity target{};
        if (AimbotDetail::IsZeroVector(targetVector) ||
            !AimbotDetail::IsPrimaryTargetActionable(target)) {
            return;
        }

        AimbotDetail::AimData aim = AimbotDetail::BuildAimData(
            targetVector,
            false,
            params.smooth / 10.0f,
            0.0f);
        if (!AimbotDetail::IsZeroVector(aim.smoothed_angle))
            AimbotDetail::MoveAimDelta(aim.local_angle, aim.smoothed_angle);
    }

    bool IsFiniteVector(const Vector3& value)
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    bool IsUsablePosition(const Vector3& value)
    {
        return IsFiniteVector(value) && value != Vector3(0.0f, 0.0f, 0.0f);
    }

    Vector3 SourcePositionForAction(const c_entity& local)
    {
        const Vector3 camera = TargetingDetail::CameraPosition();
        if (IsUsablePosition(camera))
            return camera;
        if (IsUsablePosition(local.head_pos))
            return local.head_pos;
        return local.pos;
    }

    Vector3 CandidatePositionForAction(const c_entity& entity)
    {
        if (IsUsablePosition(entity.head_pos))
            return entity.head_pos;
        if (IsUsablePosition(entity.chest_pos))
            return entity.chest_pos;
        if (IsUsablePosition(entity.neck_pos))
            return entity.neck_pos;
        return entity.pos;
    }

    float VitalityPercent(const c_entity& entity)
    {
        if (!std::isfinite(entity.PlayerHealth) || entity.PlayerHealth <= 0.0f)
            return 0.0f;

        float maxHealth = entity.PlayerHealthMax;
        if (!std::isfinite(maxHealth) || maxHealth <= 0.0f)
            maxHealth = entity.MaxHealth;
        if (!std::isfinite(maxHealth) || maxHealth <= 0.0f)
            return std::clamp(entity.PlayerHealth, 0.0f, 100.0f);

        return std::clamp((entity.PlayerHealth / maxHealth) * 100.0f, 0.0f, 100.0f);
    }

    float ApproximateAngularOffsetDegrees(const Vector3& position)
    {
        Matrix view{}, viewXor{};
        GetViewMatricesSnapshot(view, viewXor);
        (void)viewXor;

        Vector2 projected{};
        if (!view.WorldToScreen(position, &projected, Vector2(WX, WY)))
            return 0.0f;

        const Vector2 crosshair = TargetingDetail::CrosshairCenter();
        const float width = WX > 0.0f ? WX : static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
        const float height = WY > 0.0f ? WY : static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
        const float diagonal = std::sqrt(width * width + height * height);
        if (diagonal <= 0.0f)
            return 0.0f;

        return (crosshair.Distance(projected) / diagonal) * 90.0f;
    }

    TrajectoryParams GetTrajectoryParams(const char* skillId)
    {
        const std::string skill = skillId ? skillId : "";
        if (skill == "sleep-dart")
            return { 60.0f, 45.0f, 0.0f };
        if (skill == "chain-hook")
            return { 40.0f, 20.0f, 0.0f };
        if (skill == "pulse-bomb")
            return { 15.0f, 5.0f, 20.0f };
        return { 60.0f, 40.0f, 0.0f };
    }

    Vector3 ComputePredictedPosition(const Vector3& targetPos,
                                     const Vector3& targetVel,
                                     const Vector3& srcPos,
                                     float projectileSpeed,
                                     float gravity)
    {
        if (!IsFiniteVector(targetPos) || !IsFiniteVector(targetVel) ||
            !IsFiniteVector(srcPos) || projectileSpeed <= 0.0f) {
            return targetPos;
        }

        const float distance = srcPos.DistTo(targetPos);
        const float travelTime = distance / projectileSpeed;
        Vector3 predicted = targetPos + targetVel * travelTime;
        if (gravity > 0.0f) {
            // This project uses world Y as the vertical axis.
            predicted.Y += 0.5f * gravity * travelTime * travelTime;
        }
        return predicted;
    }

    CandidateEntity FindBestCandidate(const Config::HeroSkillSettings& settings, float maxRange)
    {
        CandidateEntity best{};
        if (!std::isfinite(maxRange) || maxRange <= 0.0f)
            return best;

        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        const Vector3 source = SourcePositionForAction(local);
        if (!IsUsablePosition(source))
            return best;

        const std::vector<c_entity> snapshot = TargetingDetail::SnapshotEntities();
        const int requiredTargets = (std::max)(1, settings.minTargets);
        int matchingTargets = 0;
        float bestDistance = maxRange;

        for (size_t index = 0; index < snapshot.size(); ++index) {
            const c_entity& entity = snapshot[index];
            if (!TargetingDetail::IsSelectableCandidate(entity, 0, local))
                continue;
            if (Config::aimbotIgnoreInvisible && !entity.Vis)
                continue;
            if ((entity.imort || entity.barrprot) && !Config::switch_team)
                continue;

            const Vector3 position = CandidatePositionForAction(entity);
            if (!IsUsablePosition(position))
                continue;

            const float distance = source.DistTo(position);
            if (!std::isfinite(distance) || distance > maxRange)
                continue;

            const float vitality = VitalityPercent(entity);
            if (vitality > settings.enemyHealthThreshold)
                continue;

            ++matchingTargets;
            if (best.entityIndex < 0 || distance < bestDistance) {
                bestDistance = distance;
                best.entityAddress = entity.address;
                best.entityIndex = static_cast<int>(index);
                best.distance = distance;
                best.vitalityPercent = vitality;
                best.angularOffset = ApproximateAngularOffsetDegrees(position);
                best.position = position;
                best.velocity = entity.velocity;
            }
        }

        if (matchingTargets < requiredTargets)
            return CandidateEntity{};
        return best;
    }

    int MapHotkeyToVK(int hotkey)
    {
        return Labels::AimActivationKeyVk(hotkey);
    }

    bool SendMouseVkState(int vk, bool down)
    {
        switch (vk) {
        case VK_LBUTTON:
            SendMouseButton(0, down);
            return true;
        case VK_RBUTTON:
            SendMouseButton(1, down);
            return true;
        case VK_MBUTTON:
            SendMouseButton(2, down);
            return true;
        case VK_XBUTTON1:
        case VK_XBUTTON2: {
            INPUT input{};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = vk == VK_XBUTTON1 ? XBUTTON1 : XBUTTON2;
            return SendInput(1, &input, sizeof(input)) == 1;
        }
        default:
            return false;
        }
    }

    bool SetHotkeyState(int vk, bool down)
    {
        if (vk <= 0)
            return false;
        if (SendMouseVkState(vk, down))
            return true;
        return SendKeyboardState(vk, down);
    }

    bool TryBeginTimedAction(const std::string& runtimeKey)
    {
        std::lock_guard<std::mutex> lock(g_timedActionsMutex);
        bool& active = g_timedActionsActive[runtimeKey];
        if (active)
            return false;
        active = true;
        return true;
    }

    void EndTimedAction(const std::string& runtimeKey)
    {
        std::lock_guard<std::mutex> lock(g_timedActionsMutex);
        g_timedActionsActive[runtimeKey] = false;
    }

    bool StartTimedHotkey(const std::string& runtimeKey, int vk, std::chrono::milliseconds duration)
    {
        if (vk <= 0 || !TryBeginTimedAction(runtimeKey))
            return false;

        duration = (std::max)(duration, kBriefPulse);
        std::thread([runtimeKey, vk, duration]() {
            if (SetHotkeyState(vk, true)) {
                std::this_thread::sleep_for(duration);
                SetHotkeyState(vk, false);
            }
            EndTimedAction(runtimeKey);
        }).detach();
        return true;
    }

    bool IsActionDebounced(const std::string& runtimeKey)
    {
        const auto item = g_lastActionExecutions.find(runtimeKey);
        if (item == g_lastActionExecutions.end())
            return false;

        return Clock::now() - item->second < kActionPulseDebounce;
    }

    void MarkActionExecuted(const std::string& runtimeKey)
    {
        g_lastActionExecutions[runtimeKey] = Clock::now();
    }

    bool IsRuntimeActionDefinition(const HeroSkillDefinition& definition)
    {
        const std::string skill = definition.skillId ? definition.skillId : "";
        return skill == "sleep-dart" ||
            skill == "chain-hook" ||
            skill == "rocket-punch" ||
            skill == "pulse-bomb";
    }

    bool SkillCooldownActive(float cooldown)
    {
        return std::isfinite(cooldown) && cooldown > 0.001f;
    }

    bool IsInRechargeInterval(const HeroSkillDefinition& definition,
                              const Config::HeroSkillSettings& settings,
                              const c_entity& local)
    {
        if (!settings.cooldownGuard)
            return false;

        switch (definition.inputAction) {
        case HeroSkillInputAction::Ability1:
            return SkillCooldownActive(local.skillcd1);
        case HeroSkillInputAction::Ability2:
        case HeroSkillInputAction::SecondaryFire:
            return SkillCooldownActive(local.skillcd2);
        case HeroSkillInputAction::Ultimate:
            return !std::isfinite(local.ultimate) || local.ultimate < 100.0f;
        default:
            return false;
        }
    }

    bool EvaluateTrajectoryAction(const std::string& runtimeKey,
                                  const HeroSkillDefinition& definition,
                                  const Config::HeroSkillSettings& settings,
                                  const TrajectoryParams& params)
    {
        const float effectiveRange = settings.distance > 0.0f
            ? (std::min)(settings.distance, params.maxRange)
            : params.maxRange;

        const CandidateEntity candidate = FindBestCandidate(settings, effectiveRange);
        if (candidate.entityIndex < 0 || IsActionDebounced(runtimeKey))
            return false;

        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        const Vector3 source = SourcePositionForAction(local);
        Vector3 targetPosition = candidate.position;
        if (settings.prediction) {
            targetPosition = ComputePredictedPosition(
                candidate.position,
                candidate.velocity,
                source,
                params.speed,
                params.gravity);
        }

        if (!IsUsablePosition(targetPosition) || source.DistTo(targetPosition) > effectiveRange)
            return false;

        const int vk = MapHotkeyToVK(settings.key);
        if (!StartTimedHotkey(runtimeKey, vk, kBriefPulse))
            return false;

        MarkActionExecuted(runtimeKey);
        Diagnostics::Info("Hero skill trajectory action fired. skill=%s targetIndex=%d distance=%.2f hp=%.1f offset=%.2f",
            definition.skillId ? definition.skillId : "",
            candidate.entityIndex,
            candidate.distance,
            candidate.vitalityPercent,
            candidate.angularOffset);
        return true;
    }

    bool EvaluateChargeReleaseAction(const std::string& runtimeKey,
                                     const HeroSkillDefinition& definition,
                                     const Config::HeroSkillSettings& settings)
    {
        const float effectiveRange = settings.distance > 0.0f
            ? (std::min)(settings.distance, 20.0f)
            : 20.0f;
        const CandidateEntity candidate = FindBestCandidate(settings, effectiveRange);
        if (candidate.entityIndex < 0 || IsActionDebounced(runtimeKey))
            return false;

        const float chargeScale = std::clamp(candidate.distance / effectiveRange, 0.0f, 1.0f);
        const int chargeMs = std::clamp(static_cast<int>(chargeScale * 1000.0f), 200, 1000);
        const int vk = MapHotkeyToVK(settings.key);
        if (!StartTimedHotkey(runtimeKey, vk, std::chrono::milliseconds(chargeMs)))
            return false;

        MarkActionExecuted(runtimeKey);
        Diagnostics::Info("Hero skill charge-release action fired. skill=%s targetIndex=%d distance=%.2f holdMs=%d",
            definition.skillId ? definition.skillId : "",
            candidate.entityIndex,
            candidate.distance,
            chargeMs);
        return true;
    }

    bool EvaluateChargedConditionAction(const std::string& runtimeKey,
                                        const HeroSkillDefinition& definition,
                                        const Config::HeroSkillSettings& settings)
    {
        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        if (!std::isfinite(local.ultimate) || local.ultimate < 100.0f)
            return false;

        const TrajectoryParams params = GetTrajectoryParams(definition.skillId);
        const float effectiveRange = settings.distance > 0.0f
            ? (std::min)(settings.distance, params.maxRange)
            : params.maxRange;
        const CandidateEntity candidate = FindBestCandidate(settings, effectiveRange);
        if (candidate.entityIndex < 0 || IsActionDebounced(runtimeKey))
            return false;

        const Vector3 source = SourcePositionForAction(local);
        Vector3 targetPosition = candidate.position;
        if (settings.prediction) {
            targetPosition = ComputePredictedPosition(
                candidate.position,
                candidate.velocity,
                source,
                params.speed,
                params.gravity);
        }
        if (!IsUsablePosition(targetPosition) || source.DistTo(targetPosition) > effectiveRange)
            return false;

        const int vk = MapHotkeyToVK(settings.key);
        if (!StartTimedHotkey(runtimeKey, vk, kBriefPulse))
            return false;

        MarkActionExecuted(runtimeKey);
        Diagnostics::Info("Hero skill charged-condition action fired. skill=%s targetIndex=%d distance=%.2f ultimate=%.1f",
            definition.skillId ? definition.skillId : "",
            candidate.entityIndex,
            candidate.distance,
            local.ultimate);
        return true;
    }

    void CancelSkill(const std::string& skillId)
    {
        auto sequence = g_sequences.find(skillId);
        if (sequence != g_sequences.end())
            CancelSequence(skillId, sequence->second, "disabled_or_inactive");

        auto viewpoint = g_viewpoints.find(skillId);
        if (viewpoint != g_viewpoints.end())
            CancelViewpoint(skillId, viewpoint->second, "disabled_or_inactive");

        g_lastActionExecutions.erase(skillId);
    }

    std::string RuntimeSkillKey(uint64_t heroId, const char* skillId)
    {
        return std::to_string(heroId) + ":" + (skillId ? skillId : "");
    }

} // namespace

void RunInputSequence(const std::string& skillId,
                      const std::vector<Config::HeroSkillSequenceStep>& steps,
                      int activationKey,
                      const Config::HeroSkillTrackingParams& trackingParams)
{
    SequenceRuntime& runtime = g_sequences[skillId];
    const bool held = IsActivationKeyHeld(activationKey);

    if (!held || steps.empty()) {
        if (runtime.active)
            CancelSequence(skillId, runtime, held ? "empty_steps" : "activation_released");
        return;
    }

    const Clock::time_point now = Clock::now();
    if (!runtime.active) {
        runtime.active = true;
        runtime.stepIndex = 0;
        runtime.phase = SequencePhase::Hold;
        runtime.phaseStarted = now;
        runtime.activeChannel = steps.front().channel;
        runtime.hasActiveChannel = true;
        SetOutputChannel(runtime.activeChannel, true);
        Diagnostics::Info("Hero skill sequence started. skill=%s steps=%zu activationKey=%d",
            skillId.c_str(), steps.size(), activationKey);
    }

    if (runtime.stepIndex >= steps.size())
        runtime.stepIndex = 0;

    const Config::HeroSkillSequenceStep& step = steps[runtime.stepIndex];
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runtime.phaseStarted);

    if (runtime.phase == SequencePhase::Hold) {
        if (elapsed.count() >= step.holdMs) {
            ReleaseSequenceChannel(runtime);
            runtime.phase = SequencePhase::Release;
            runtime.phaseStarted = now;
        }
    } else if (elapsed.count() >= step.releaseMs) {
        runtime.stepIndex = (runtime.stepIndex + 1) % steps.size();
        const Config::HeroSkillSequenceStep& nextStep = steps[runtime.stepIndex];
        runtime.phase = SequencePhase::Hold;
        runtime.phaseStarted = now;
        runtime.activeChannel = nextStep.channel;
        runtime.hasActiveChannel = true;
        SetOutputChannel(runtime.activeChannel, true);
    }

    RunTrackingOverlayTick(trackingParams);
}

HeroSkillRunState RunViewpointController(const std::string& skillId,
                                         const Config::HeroSkillSettings& params)
{
    ViewpointRuntime& runtime = g_viewpoints[skillId];
    const bool keyDown = IsActivationKeyHeld(params.activationKey);

    if (Config::doingentity == 0) {
        CancelViewpoint(skillId, runtime, "shutdown");
        runtime.previousKeyDown = keyDown;
        return HeroSkillRunState::Cancelled;
    }

    const bool active = runtime.phase != ViewpointPhase::Idle &&
        runtime.phase != ViewpointPhase::Completed &&
        runtime.phase != ViewpointPhase::Cancelled;
    if (!active && keyDown && !runtime.previousKeyDown)
        BeginViewpoint(skillId, runtime, params);

    runtime.previousKeyDown = keyDown;

    const Clock::time_point now = Clock::now();
    const float dt = DeltaSeconds(runtime, now);

    switch (runtime.phase) {
    case ViewpointPhase::Idle:
    case ViewpointPhase::Completed:
        return HeroSkillRunState::Completed;
    case ViewpointPhase::Cancelled:
        return HeroSkillRunState::Cancelled;
    case ViewpointPhase::PitchDown: {
        const float targetPitch = std::clamp(params.pitchDownTargetAngle, -89.0f, 89.0f) * kDegToRad;
        if (MovePitchToward(targetPitch, runtime.pitchDownSpeedDeg, dt)) {
            runtime.phase = ViewpointPhase::SecondaryDown;
            runtime.phaseStarted = now;
        }
        break;
    }
    case ViewpointPhase::SecondaryDown:
        SetOutputChannel(Config::HeroSkillInputChannel::Secondary, true);
        runtime.secondaryHeld = true;
        runtime.phase = ViewpointPhase::SecondaryUp;
        runtime.phaseStarted = now;
        break;
    case ViewpointPhase::SecondaryUp:
        if (now - runtime.phaseStarted >= kBriefPulse) {
            SetOutputChannel(Config::HeroSkillInputChannel::Secondary, false);
            runtime.secondaryHeld = false;
            runtime.phase = ViewpointPhase::Delay;
            runtime.phaseStarted = now;
        }
        break;
    case ViewpointPhase::Delay:
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - runtime.phaseStarted).count() >= params.fireDelayMs) {
            runtime.phase = ViewpointPhase::KeyboardDown;
            runtime.phaseStarted = now;
        }
        break;
    case ViewpointPhase::KeyboardDown:
        if (runtime.keyboardVk > 0) {
            if (!SendKeyboardState(runtime.keyboardVk, true)) {
                Diagnostics::Warn("Hero skill keyboard pulse failed. skill=%s vk=%d",
                    skillId.c_str(), runtime.keyboardVk);
            } else {
                runtime.keyboardHeld = true;
            }
        }
        runtime.phase = ViewpointPhase::KeyboardUp;
        runtime.phaseStarted = now;
        break;
    case ViewpointPhase::KeyboardUp:
        if (now - runtime.phaseStarted >= kBriefPulse) {
            if (runtime.keyboardHeld)
                SendKeyboardState(runtime.keyboardVk, false);
            runtime.keyboardHeld = false;
            runtime.phase = ViewpointPhase::PitchUp;
            runtime.phaseStarted = now;
        }
        break;
    case ViewpointPhase::PitchUp: {
        // DirectionToAimEuler uses positive pitch for downward movement, so
        // "pitch up" subtracts the configured positive offset from the start pitch.
        const float targetPitch = std::clamp(
            runtime.initialAngles.X - params.pitchUpOffsetAngle * kDegToRad,
            -89.0f * kDegToRad,
            89.0f * kDegToRad);
        if (MovePitchToward(targetPitch, runtime.pitchUpSpeedDeg, dt)) {
            runtime.phase = ViewpointPhase::RestoreYaw;
            runtime.phaseStarted = now;
        }
        break;
    }
    case ViewpointPhase::RestoreYaw:
        if (MoveYawToward(runtime.initialAngles.Y, runtime.pitchUpSpeedDeg, dt)) {
            ReleaseViewpointOutputs(runtime);
            runtime.phase = ViewpointPhase::Completed;
            Diagnostics::Info("Hero skill viewpoint completed. skill=%s", skillId.c_str());
            return HeroSkillRunState::Completed;
        }
        break;
    default:
        break;
    }

    return HeroSkillRunState::InProgress;
}

void CancelActiveSkill()
{
    for (auto& item : g_sequences)
        CancelSequence(item.first, item.second, "cancel_all");

    for (auto& item : g_viewpoints)
        CancelViewpoint(item.first, item.second, "cancel_all");

    ReleaseAllOutputChannels();
    g_lastActionExecutions.clear();
}

void ProcessHeroSkills()
{
    if (Config::doingentity == 0) {
        CancelActiveSkill();
        return;
    }

    const c_entity localSnapshot = TargetingDetail::SnapshotLocalEntity();
    const uint64_t heroId = localSnapshot.HeroID;
    if (heroId != g_lastHeroId) {
        CancelActiveSkill();
        g_lastHeroId = heroId;
    }

    if (heroId == 0)
        return;

    bool processedAnyForHero = false;
    for (const HeroSkillDefinition& definition : AllHeroSkillDefinitions()) {
        if (definition.heroId != heroId)
            continue;

        processedAnyForHero = true;
        const std::string skillKey = RuntimeSkillKey(heroId, definition.skillId);
        Config::HeroSkillSettings settings = Config::GetHeroSkillSettings(
            heroId,
            definition.skillId ? definition.skillId : "",
            definition.defaultSettings);

        if (!settings.enabled) {
            CancelSkill(skillKey);
            continue;
        }

        if (SkillControls(definition, HeroSkillControls::SequenceSteps)) {
            RunInputSequence(skillKey, settings.sequenceSteps, settings.activationKey, settings.tracking);
        }

        if (SkillControls(definition, HeroSkillControls::PitchControl) ||
            SkillControls(definition, HeroSkillControls::PhaseTiming)) {
            RunViewpointController(skillKey, settings);
        }

        if (!IsRuntimeActionDefinition(definition))
            continue;

        if (IsInRechargeInterval(definition, settings, localSnapshot))
            continue;

        if (definition.category == HeroSkillCategory::Ultimate) {
            EvaluateChargedConditionAction(skillKey, definition, settings);
        } else if (definition.inputAction == HeroSkillInputAction::SecondaryFire &&
                   std::string(definition.skillId ? definition.skillId : "") == "rocket-punch") {
            EvaluateChargeReleaseAction(skillKey, definition, settings);
        } else {
            EvaluateTrajectoryAction(skillKey, definition, settings, GetTrajectoryParams(definition.skillId));
        }
    }

    if (!processedAnyForHero)
        CancelActiveSkill();
}

} // namespace OW
