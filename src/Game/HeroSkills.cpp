#include <Windows.h>

#include "Game/HeroSkills.hpp"

#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Kmbox/KmBoxNetManager.h"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <limits>
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
    constexpr float kViewpointPitchLimitRad = 89.85f * kDegToRad;
    constexpr float kViewpointFloorReadyPitchRad = 88.5f * kDegToRad;
    constexpr float kViewpointReturnDoneEpsilonRad = 2.0f * kDegToRad;
    constexpr float kViewpointYawReturnEpsilonRad = 2.0f * kDegToRad;
    constexpr auto kViewpointFloorSettle = std::chrono::milliseconds(18);
    constexpr auto kViewpointReturnTimeout = std::chrono::milliseconds(450);
    constexpr auto kViewpointYawRestoreBudget = std::chrono::milliseconds(140);
    constexpr auto kViewpointDebugInterval = std::chrono::milliseconds(100);
    constexpr uint16_t kStateScriptAmmo = 0x35;
    constexpr uint16_t kReloadStateSkill = 0x4BF;
    constexpr int kZaryaAmmoClipSize = 100;
    constexpr double kZaryaPrimaryAmmoPerSecond = 20.0;
    constexpr double kZaryaSecondaryAmmoCost = 25.0;
    constexpr int kZaryaAutoRightThresholdMin = 1;
    constexpr int kZaryaAutoRightThresholdMax = 5;
    constexpr int kZaryaAutoRightThresholdLeadAmmo = 3;
    constexpr int kZaryaAutoRightHoldBaseMs = 200;
    constexpr int kZaryaAutoRightHoldJitterMs = 10;
    constexpr int kZaryaAutoRightLeftReleaseBaseMs = 100;
    constexpr int kZaryaAutoRightLeftReleaseJitterMs = 5;
    constexpr auto kZaryaAutoRightRestoreSettle = std::chrono::milliseconds(10);
    constexpr auto kZaryaReloadSuccessMinDuration = std::chrono::milliseconds(900);
    constexpr auto kZaryaBudgetLogInterval = std::chrono::milliseconds(250);
    struct SequenceRuntime {
        bool active = false;
        size_t stepIndex = 0;
        int currentMask = 0;
        Clock::time_point stepStarted{};
        int effectiveDurationMs = 0;
        std::jthread worker{};
        Clock::time_point hitMonitorStarted{};
        Clock::time_point hitLastChange{};
        uint64_t hitTargetAddress = 0;
        float hitLastHealth = std::numeric_limits<float>::quiet_NaN();
        int hitDamageEvents = 0;
        bool hitHasSample = false;
    };

    struct SequenceSelfTestRuntime {
        bool enabled = false;
        int key = 0;
        uint64_t heroId = 0;
        std::string skillId{};
        bool forceSkill = false;
        Clock::time_point pressAt{};
        Clock::time_point releaseAt{};
        Clock::time_point reloadPressAt{};
        Clock::time_point reloadReleaseAt{};
        bool reloadEnabled = false;
        int reloadKeyVk = 0x52;
        bool startLogged = false;
        bool stopLogged = false;
        bool reloadPressLogged = false;
        bool reloadReleaseLogged = false;
    };

    namespace ViewpointPhase {
        constexpr int Idle = 0;
        constexpr int PitchDown = 1;
        constexpr int PitchUp = 2;
        constexpr int Completed = 3;
        constexpr int Cancelled = 4;
    }

    struct ViewpointRuntime {
        int phase = 0;        // 0=idle, 1=pitchDown, 2=pitchUp, 3=completed, 4=cancelled
        bool prevKeyDown = false;
        Clock::time_point phaseStarted{};
        Clock::time_point lastTick{};
        Clock::time_point lastDebugLog{};
        Clock::time_point floorReadyStarted{};
        Clock::time_point secondaryPressedAt{};
        Clock::time_point fireDelayStarted{};
        Clock::time_point jumpPressedAt{};
        Vector3 initialAngles{};
        float pitchDownSpeedDeg = 0.0f;
        float pitchUpTargetAngle = 0.0f;
        float pitchUpSpeedDeg = 0.0f;
        int skillVk = VK_RBUTTON;
        int jumpVk = 0;
        bool fired = false;
        bool jumped = false;
        bool secondaryDown = false;
        bool jumpDown = false;
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
        float projectileRadius = 0.0f;
        float preFireDelayMs = 0.0f;
    };

    struct SkillProjectileRuntime {
        float speed = 0.0f;
        float maxRange = 0.0f;
        bool gravity = false;
        float projectileRadius = 0.0f;
        float preFireDelayMs = 0.0f;
    };

    struct SkillAimCandidate {
        bool valid = false;
        c_entity entity{};
        int entityIndex = -1;
        uint64_t entityKey = 0;
        int boneId = 0;
        float distance = 0.0f;
        float vitalityPercent = 0.0f;
        float fovScore = 0.0f;
        float hitWindow = 0.0f;
        Vector3 rawAimPoint{};
        Vector3 aimPoint{};
    };

    struct AmmoGuardSample {
        int ammo = -1;
        int stateAmmo = -1;
        int reserveAmmoPath = -1;
        int reserveState = -1;
        int rawStateAmmo = -1;
        int rawReserveState = -1;
        int budgetAmmo = -1;
        int source = 0;
        uint64_t skillBase = 0;
        uint64_t heroId = 0;
    };

    struct AmmoGuardLogState {
        int ammo = -2;
        int stateAmmo = -2;
        int reserveAmmoPath = -2;
        int reserveState = -2;
        int rawStateAmmo = -2;
        int rawReserveState = -2;
        int budgetAmmo = -2;
        int source = -1;
        Clock::time_point lastLog{};
    };

    struct ZaryaAmmoProbeRuntime {
        bool lastReloading = false;
        Clock::time_point reloadStarted{};
        bool reloadQualified = false;
        bool budgetArmed = false;
        double ammo = -1.0;
        bool leftWasDown = false;
        bool rightWasDown = false;
        bool autoRightSent = false;
        int autoRightThreshold = 0;
        Clock::time_point lastObserved{};
        Clock::time_point lastBudgetLog{};
        int lastLoggedAmmo = -1000;
    };

    struct ZaryaAutoRightTiming {
        std::chrono::milliseconds rightHold{};
        std::chrono::milliseconds leftReleaseDelay{};
        std::chrono::milliseconds afterLeftRelease{};
    };

    struct SequenceAmmoBudgetState {
        int remaining = -1;
        int reserve = 1;
        bool blocked = false;
        bool reloadObserved = false;
        bool lastReloading = false;
    };

    std::unordered_map<std::string, SequenceRuntime> g_sequences;
    std::unordered_map<std::string, ViewpointRuntime> g_viewpoints;
    std::unordered_map<std::string, Clock::time_point> g_lastActionExecutions;
    std::unordered_map<std::string, Clock::time_point> g_lastSkillGuardLogs;
    std::unordered_map<std::string, AmmoGuardLogState> g_lastAmmoGuardSamples;
    std::unordered_map<std::string, ZaryaAmmoProbeRuntime> g_zaryaAmmoProbes;
    std::unordered_map<std::string, SequenceAmmoBudgetState> g_sequenceAmmoBudgets;
    std::mutex g_sequenceAmmoBudgetMutex;
    std::unordered_map<std::string, bool> g_timedActionsActive;
    std::mutex g_timedActionsMutex;
    std::mt19937 g_random{ std::random_device{}() };
    std::mutex g_randomMutex;
    std::atomic<bool> g_anyInputSequenceActive{ false };
    uint64_t g_lastHeroId = 0;

    void RefreshAnyInputSequenceActive();
    bool ConsumeSequenceAmmoBudgetForStep(const std::string& runtimeKey,
                                          int previousMask,
                                          int nextMask,
                                          bool ammoGuardEnabled,
                                          int reserveAmmo);

    int ReadEnvInt(const char* name, int fallback, int minValue, int maxValue)
    {
        char buffer[64] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const long value = std::strtol(buffer, &end, 10);
        if (!end || *end != '\0')
            return fallback;

        return std::clamp(static_cast<int>(value), minValue, maxValue);
    }

    uint64_t ReadEnvUInt64(const char* name, uint64_t fallback)
    {
        char buffer[64] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return fallback;

        char* end = nullptr;
        const unsigned long long value = std::strtoull(buffer, &end, 0);
        if (!end || *end != '\0')
            return fallback;

        return static_cast<uint64_t>(value);
    }

    std::string ReadEnvString(const char* name)
    {
        char buffer[128] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return {};

        return std::string(buffer, static_cast<size_t>(length));
    }

    bool SendKeyboardState(int vk, bool down);
    int MapHotkeyToVK(int hotkey);
    bool SetHotkeyState(int vk, bool down);

    SequenceSelfTestRuntime& SequenceSelfTest()
    {
        static SequenceSelfTestRuntime runtime = [] {
            SequenceSelfTestRuntime value{};
            char enabled[16] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UNLEASHED_SEQUENCE_TEST",
                enabled,
                static_cast<DWORD>(sizeof(enabled)));
            if (length == 0 || length >= sizeof(enabled) || enabled[0] == '0')
                return value;

            const int delayMs = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_DELAY_MS", 8000, 0, 60000);
            const int durationMs = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_DURATION_MS", 10000, 100, 60000);
            const int reloadDelayMs = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_RELOAD_DELAY_MS", 250, 0, 5000);
            const int reloadTapMs = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_RELOAD_TAP_MS", 80, 10, 1000);
            value.enabled = true;
            value.key = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_KEY", HeroSkillHotkey::Mouse4, 0, 14);
            value.heroId = ReadEnvUInt64("UNLEASHED_SEQUENCE_TEST_HERO", 0);
            value.skillId = ReadEnvString("UNLEASHED_SEQUENCE_TEST_SKILL");
            value.forceSkill = value.heroId != 0 && !value.skillId.empty();
            value.reloadEnabled = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_RELOAD", 1, 0, 1) != 0;
            value.reloadKeyVk = ReadEnvInt("UNLEASHED_SEQUENCE_TEST_RELOAD_VK", 0x52, 1, 255);
            const Clock::time_point now = Clock::now();
            if (value.reloadEnabled) {
                value.reloadPressAt = now + std::chrono::milliseconds(reloadDelayMs);
                value.reloadReleaseAt = value.reloadPressAt + std::chrono::milliseconds(reloadTapMs);
                value.pressAt = value.reloadReleaseAt + std::chrono::milliseconds(delayMs);
            } else {
                value.pressAt = now + std::chrono::milliseconds(delayMs);
                value.reloadPressAt = {};
                value.reloadReleaseAt = {};
            }
            value.releaseAt = value.pressAt + std::chrono::milliseconds(durationMs);
            Diagnostics::Aim("sequence.self_test armed key=%d delayMs=%d durationMs=%d reload=%d reloadVk=0x%X reloadDelayMs=%d reloadTapMs=%d forceHero=0x%llX forceSkill=%s",
                value.key,
                delayMs,
                durationMs,
                value.reloadEnabled ? 1 : 0,
                value.reloadKeyVk,
                reloadDelayMs,
                reloadTapMs,
                static_cast<unsigned long long>(value.heroId),
                value.skillId.c_str());
            return value;
        }();

        return runtime;
    }

    bool UseSequenceWorker()
    {
        static const bool enabled = [] {
            char value[16] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UNLEASHED_SEQUENCE_WORKER",
                value,
                static_cast<DWORD>(sizeof(value)));
            const bool workerEnabled = length == 0 || length >= sizeof(value) || value[0] != '0';
            Diagnostics::Aim("sequence.worker_mode enabled=%d", workerEnabled ? 1 : 0);
            return workerEnabled;
        }();

        return enabled;
    }

    bool SkillControls(const HeroSkillDefinition& definition, HeroSkillControlFlags control)
    {
        return HasHeroSkillControl(definition, control);
    }

    bool IsZaryaReloadAmmoProbeDefinition(const HeroSkillDefinition& definition)
    {
        return definition.heroId == static_cast<uint64_t>(eHero::HERO_ZARYA) &&
            std::string(definition.skillId ? definition.skillId : "") == "reload-ammo-probe";
    }

    int PickZaryaAutoRightThreshold()
    {
        std::uniform_int_distribution<int> distribution(
            kZaryaAutoRightThresholdMin,
            kZaryaAutoRightThresholdMax);
        std::lock_guard<std::mutex> lock(g_randomMutex);
        return distribution(g_random);
    }

    int PickZaryaJitteredMs(int baseMs, int jitterMs, int minMs, int maxMs)
    {
        std::uniform_int_distribution<int> distribution(-jitterMs, jitterMs);
        std::lock_guard<std::mutex> lock(g_randomMutex);
        return std::clamp(baseMs + distribution(g_random), minMs, maxMs);
    }

    ZaryaAutoRightTiming PickZaryaAutoRightTiming()
    {
        const int rightHoldMs = PickZaryaJitteredMs(
            kZaryaAutoRightHoldBaseMs,
            kZaryaAutoRightHoldJitterMs,
            1,
            1000);
        const int leftReleaseDelayMs = PickZaryaJitteredMs(
            kZaryaAutoRightLeftReleaseBaseMs,
            kZaryaAutoRightLeftReleaseJitterMs,
            0,
            rightHoldMs);
        return {
            std::chrono::milliseconds(rightHoldMs),
            std::chrono::milliseconds(leftReleaseDelayMs),
            std::chrono::milliseconds(rightHoldMs - leftReleaseDelayMs)
        };
    }

    bool IsActivationKeyHeld(int activationKey)
    {
        const int vk = Labels::AimActivationKeyVk(activationKey);
        if (vk <= 0)
            return false;

        return AimbotDetail::IsInputVkDown(vk, activationKey);
    }

    bool IsSequenceSelfTestHeld(int activationKey)
    {
        SequenceSelfTestRuntime& runtime = SequenceSelfTest();
        if (!runtime.enabled || runtime.key != activationKey)
            return false;

        const Clock::time_point now = Clock::now();
        if (now < runtime.pressAt)
            return false;

        if (now < runtime.releaseAt) {
            if (!runtime.startLogged) {
                runtime.startLogged = true;
                Diagnostics::Aim("sequence.self_test press key=%d", runtime.key);
            }
            return true;
        }

        if (!runtime.stopLogged) {
            runtime.stopLogged = true;
            Diagnostics::Aim("sequence.self_test release key=%d", runtime.key);
        }
        return false;
    }

    void UpdateSequenceSelfTestReload()
    {
        SequenceSelfTestRuntime& runtime = SequenceSelfTest();
        if (!runtime.enabled || !runtime.reloadEnabled)
            return;

        const Clock::time_point now = Clock::now();
        if (!runtime.reloadPressLogged && now >= runtime.reloadPressAt) {
            if (SendKeyboardState(runtime.reloadKeyVk, true)) {
                runtime.reloadPressLogged = true;
                Diagnostics::Aim("sequence.self_test reload_press vk=0x%X", runtime.reloadKeyVk);
            } else {
                runtime.reloadPressLogged = true;
                runtime.reloadReleaseLogged = true;
                Diagnostics::Aim("sequence.self_test reload_failed vk=0x%X", runtime.reloadKeyVk);
            }
        }

        if (runtime.reloadPressLogged && !runtime.reloadReleaseLogged &&
            now >= runtime.reloadReleaseAt) {
            SendKeyboardState(runtime.reloadKeyVk, false);
            runtime.reloadReleaseLogged = true;
            Diagnostics::Aim("sequence.self_test reload_release vk=0x%X", runtime.reloadKeyVk);
        }
    }

    bool SequenceDiagnosticsEnabled()
    {
        return Config::aimVerboseLog || SequenceSelfTest().enabled;
    }

    void ApplyButtonMaskDiff(const std::string& skillId, int prevMask, int newMask)
    {
        newMask &= 0x07;
        prevMask &= 0x07;
        const int changed = prevMask ^ newMask;
        if (changed == 0)
            return;

        const bool rightStateInvolved = ((prevMask | newMask) & 0x02) != 0;
        if (rightStateInvolved &&
            OW::SendMouseButtonStateMask(static_cast<uint32_t>(newMask))) {
            if (SequenceDiagnosticsEnabled()) {
                Diagnostics::Aim("sequence.button_state skill=%s prevMask=0x%02X newMask=0x%02X fullState=1",
                    skillId.c_str(),
                    prevMask,
                    newMask);
            }
            return;
        }

        for (int bit = 0; bit < 3; ++bit) {
            const int bitFlag = 1 << bit;
            if (changed & bitFlag) {
                if (SequenceDiagnosticsEnabled()) {
                    Diagnostics::Aim("sequence.button_diff skill=%s prevMask=0x%02X newMask=0x%02X button=%d down=%d fullState=0",
                        skillId.c_str(),
                        prevMask,
                        newMask,
                        bit,
                        (newMask & bitFlag) != 0 ? 1 : 0);
                }
                OW::SendMouseButton(bit, (newMask & bitFlag) != 0);
            }
        }
    }

    void ReleaseAllButtons()
    {
        OW::ForceReleaseMouseButtons();
    }

    // VK → USB HID key code for the most common game keys.
    // Unmapped VK codes return 0 (no-op).
    unsigned char VkToHidKeyCode(int vk)
    {
        switch (vk) {
        case VK_SPACE:  return 0x2C; // KEY_SPACEBAR
        case VK_LCONTROL:
        case VK_RCONTROL: return 0xE0; // KEY_LEFT_CONTROL
        case VK_LSHIFT:
        case VK_RSHIFT:   return 0xE1; // KEY_LEFT_SHIFT
        case VK_LMENU:
        case VK_RMENU:    return 0xE2; // KEY_LEFT_ALT
        case 0x46:        return 0x09; // F key
        case 0x45:        return 0x08; // E key
        case 0x51:        return 0x14; // Q key
        case 0x52:        return 0x15; // R key
        case 0x56:        return 0x19; // V key
        case VK_TAB:      return 0x2B; // KEY_TAB
        case VK_CAPITAL:  return 0x39; // KEY_CAPS_LOCK
        default:          return 0;
        }
    }

    bool SendKeyboardState(int vk, bool down)
    {
        const unsigned char hidCode = VkToHidKeyCode(vk);
        if (hidCode == 0)
            return false;

        if (Config::kmboxEnabled && Config::kmboxDeviceType == 0) {
            kmbox::KmBoxMgr.SendKeyboardKey(hidCode, down);
            return true;
        }

        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(vk);
        input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
        return SendInput(1, &input, sizeof(input)) == 1;
    }

    void BeginSecondaryPulse(const std::string& skillId, ViewpointRuntime& runtime, Clock::time_point now)
    {
        if (runtime.secondaryDown)
            return;

        if (runtime.skillVk <= 0 || !SetHotkeyState(runtime.skillVk, true)) {
            Diagnostics::Warn("Hero skill viewpoint skill pulse failed. skill=%s vk=%d",
                skillId.c_str(),
                runtime.skillVk);
            return;
        }

        Diagnostics::Info("Hero skill viewpoint skill pulse start. skill=%s vk=%d",
            skillId.c_str(),
            runtime.skillVk);
        runtime.secondaryDown = true;
        runtime.secondaryPressedAt = now;
    }

    bool BeginKeyboardPulse(const std::string& skillId,
                            ViewpointRuntime& runtime,
                            int vk,
                            Clock::time_point now)
    {
        if (vk <= 0 || vk > 255)
            return false;
        if (runtime.jumpDown)
            return true;
        if (!SendKeyboardState(vk, true))
            return false;

        Diagnostics::Info("Hero skill viewpoint keyboard pulse start. skill=%s vk=%d",
            skillId.c_str(), vk);
        runtime.jumpDown = true;
        runtime.jumpPressedAt = now;
        return true;
    }

    void UpdateViewpointPulses(const std::string& skillId, ViewpointRuntime& runtime, Clock::time_point now)
    {
        if (runtime.secondaryDown && now - runtime.secondaryPressedAt >= kBriefPulse) {
            SetHotkeyState(runtime.skillVk, false);
            runtime.secondaryDown = false;
            runtime.fired = true;
            runtime.fireDelayStarted = now;
            Diagnostics::Info("Hero skill viewpoint secondary pulse end. skill=%s", skillId.c_str());
        }

        if (runtime.jumpDown && now - runtime.jumpPressedAt >= kBriefPulse) {
            SendKeyboardState(runtime.jumpVk, false);
            runtime.jumpDown = false;
            Diagnostics::Info("Hero skill viewpoint keyboard pulse end. skill=%s vk=%d",
                skillId.c_str(), runtime.jumpVk);
        }
    }

    long long MillisecondsSince(Clock::time_point start, Clock::time_point now)
    {
        if (start.time_since_epoch().count() == 0)
            return 0;
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }

    void LogSequenceHitSummary(const std::string& skillId, const SequenceRuntime& runtime)
    {
        if (!runtime.hitHasSample)
            return;

        const Clock::time_point now = Clock::now();
        Diagnostics::Aim("sequence.hit_summary skill=%s target=0x%llX damageEvents=%d durationMs=%lld lastHealth=%.1f",
            skillId.c_str(),
            static_cast<unsigned long long>(runtime.hitTargetAddress),
            runtime.hitDamageEvents,
            MillisecondsSince(runtime.hitMonitorStarted, now),
            runtime.hitLastHealth);
    }

    bool IsSequenceHitCandidate(const c_entity& entity, const c_entity& local, bool requireEnemyTeam)
    {
        if (!entity.address || entity.address == local.address)
            return false;
        if (!entity.Alive)
            return false;
        if (!std::isfinite(entity.PlayerHealth) || entity.PlayerHealth <= 0.0f)
            return false;
        if (requireEnemyTeam && !TargetingDetail::TargetTeamMatches(entity, 0, local))
            return false;
        return true;
    }

    Vector3 SequenceHitProbePosition(const c_entity& entity)
    {
        if (!TargetingDetail::IsZeroVector(entity.chest_pos))
            return entity.chest_pos;
        if (!TargetingDetail::IsZeroVector(entity.neck_pos))
            return entity.neck_pos;
        if (!TargetingDetail::IsZeroVector(entity.head_pos))
            return entity.head_pos;
        return entity.pos;
    }

    bool TryPickSequenceHitTarget(const std::vector<c_entity>& snapshot,
                                  const c_entity& local,
                                  const SequenceRuntime& runtime,
                                  c_entity& outTarget,
                                  int& outIndex)
    {
        if (runtime.hitTargetAddress != 0) {
            for (size_t index = 0; index < snapshot.size(); ++index) {
                const c_entity& entity = snapshot[index];
                if (entity.address == runtime.hitTargetAddress &&
                    IsSequenceHitCandidate(entity, local, false)) {
                    outTarget = entity;
                    outIndex = static_cast<int>(index);
                    return true;
                }
            }
        }

        const int configuredIndex = Config::Targetenemyi;
        if (TargetingDetail::IsValidIndex(configuredIndex, snapshot.size())) {
            const c_entity& configured = snapshot[static_cast<size_t>(configuredIndex)];
            if (IsSequenceHitCandidate(configured, local, false)) {
                outTarget = configured;
                outIndex = configuredIndex;
                return true;
            }
        }

        const Vector2 crosshair = TargetingDetail::CrosshairCenter();
        const Matrix view = SnapshotViewMatrix();
        float bestScore = (std::numeric_limits<float>::max)();
        int bestIndex = -1;
        bool found = false;

        auto scan = [&](bool requireEnemyTeam) {
            for (size_t index = 0; index < snapshot.size(); ++index) {
                const c_entity& entity = snapshot[index];
                if (!IsSequenceHitCandidate(entity, local, requireEnemyTeam))
                    continue;

                const Vector3 position = SequenceHitProbePosition(entity);
                if (TargetingDetail::IsZeroVector(position))
                    continue;

                Vector2 projected{};
                if (!view.WorldToScreen(position, &projected, Vector2(WX, WY), false))
                    continue;

                if (!std::isfinite(projected.X) || !std::isfinite(projected.Y))
                    continue;

                const float score = crosshair.Distance(projected);
                if (score < bestScore) {
                    bestScore = score;
                    bestIndex = static_cast<int>(index);
                    found = true;
                }
            }
        };

        scan(true);
        if (!found)
            scan(false);

        if (!found || !TargetingDetail::IsValidIndex(bestIndex, snapshot.size()))
            return false;

        outTarget = snapshot[static_cast<size_t>(bestIndex)];
        outIndex = bestIndex;
        return true;
    }

    void UpdateSequenceHitTiming(const std::string& skillId, SequenceRuntime& runtime)
    {
        const std::vector<c_entity> snapshot = TargetingDetail::SnapshotEntities();
        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        c_entity target{};
        int targetIndex = -1;
        if (!TryPickSequenceHitTarget(snapshot, local, runtime, target, targetIndex))
            return;

        const Clock::time_point now = Clock::now();
        if (runtime.hitMonitorStarted.time_since_epoch().count() == 0)
            runtime.hitMonitorStarted = now;

        if (!runtime.hitHasSample || runtime.hitTargetAddress != target.address) {
            runtime.hitTargetAddress = target.address;
            runtime.hitLastHealth = target.PlayerHealth;
            runtime.hitLastChange = now;
            runtime.hitHasSample = true;
            runtime.hitDamageEvents = 0;
            Diagnostics::Aim("sequence.hit_target skill=%s index=%d target=0x%llX health=%.1f hero=0x%llX",
                skillId.c_str(),
                targetIndex,
                static_cast<unsigned long long>(target.address),
                target.PlayerHealth,
                static_cast<unsigned long long>(target.HeroID));
            return;
        }

        const float delta = target.PlayerHealth - runtime.hitLastHealth;
        if (std::fabs(delta) < 0.5f)
            return;

        if (delta < 0.0f)
            ++runtime.hitDamageEvents;

        Diagnostics::Aim("sequence.hit_timing skill=%s target=0x%llX index=%d tMs=%lld dtMs=%lld health=%.1f delta=%.1f damageEvents=%d",
            skillId.c_str(),
            static_cast<unsigned long long>(target.address),
            targetIndex,
            MillisecondsSince(runtime.hitMonitorStarted, now),
            MillisecondsSince(runtime.hitLastChange, now),
            target.PlayerHealth,
            delta,
            runtime.hitDamageEvents);

        runtime.hitLastHealth = target.PlayerHealth;
        runtime.hitLastChange = now;
    }

    void ReleaseViewpointOutputs(ViewpointRuntime& runtime)
    {
        if (runtime.secondaryDown) {
            SetHotkeyState(runtime.skillVk, false);
            runtime.secondaryDown = false;
        }
        if (runtime.jumpDown) {
            SendKeyboardState(runtime.jumpVk, false);
            runtime.jumpDown = false;
        }
    }

    void CancelSequence(const std::string& skillId, SequenceRuntime& runtime, const char* reason)
    {
        const bool hadWorker = runtime.worker.joinable();
        const bool shouldForceRelease = runtime.active || hadWorker || runtime.currentMask != 0;
        if (hadWorker) {
            runtime.worker.request_stop();
            runtime.worker.join();
        }

        if (runtime.currentMask != 0) {
            ApplyButtonMaskDiff(skillId, runtime.currentMask, 0);
            runtime.currentMask = 0;
        }
        if (shouldForceRelease)
            ReleaseAllButtons();

        LogSequenceHitSummary(skillId, runtime);

        if (runtime.active || hadWorker) {
            Diagnostics::Info("Hero skill sequence cancelled. skill=%s reason=%s",
                skillId.c_str(), reason ? reason : "unknown");
        }
        runtime = SequenceRuntime{};
        RefreshAnyInputSequenceActive();
    }

    void CancelViewpoint(const std::string& skillId, ViewpointRuntime& runtime, const char* reason)
    {
        const bool wasActive = runtime.phase == ViewpointPhase::PitchDown ||
            runtime.phase == ViewpointPhase::PitchUp;
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

    float ClampViewPitchTarget(float pitchRad)
    {
        return std::clamp(pitchRad, -kViewpointPitchLimitRad, kViewpointPitchLimitRad);
    }

    const char* ViewpointPhaseName(int phase)
    {
        switch (phase) {
        case ViewpointPhase::Idle: return "idle";
        case ViewpointPhase::PitchDown: return "pitchDown";
        case ViewpointPhase::PitchUp: return "pitchUp";
        case ViewpointPhase::Completed: return "completed";
        case ViewpointPhase::Cancelled: return "cancelled";
        default: return "unknown";
        }
    }

    bool ShouldLogViewpointTick(ViewpointRuntime& runtime, Clock::time_point now)
    {
        if (runtime.lastDebugLog.time_since_epoch().count() != 0 &&
            now - runtime.lastDebugLog < kViewpointDebugInterval) {
            return false;
        }

        runtime.lastDebugLog = now;
        return true;
    }

    bool IsPitchWithin(float targetPitch, float epsilonRad)
    {
        Vector3 current{};
        return TryReadCurrentViewAngles(current) &&
            std::fabs(targetPitch - current.X) <= epsilonRad;
    }

    bool IsYawWithin(float targetYaw, float epsilonRad)
    {
        Vector3 current{};
        return TryReadCurrentViewAngles(current) &&
            std::fabs(NormalizeAngle(targetYaw - current.Y)) <= epsilonRad;
    }

    void LogViewpointTick(const std::string& skillId,
                          ViewpointRuntime& runtime,
                          float targetPitch,
                          float targetYaw,
                          float speedDeg,
                          float deltaSeconds,
                          Clock::time_point now)
    {
        if (!Config::aimVerboseLog)
            return;
        if (!ShouldLogViewpointTick(runtime, now))
            return;

        Vector3 current{};
        if (!TryReadCurrentViewAngles(current)) {
            Diagnostics::Warn("Hero skill viewpoint tick read failed. skill=%s phase=%s",
                skillId.c_str(), ViewpointPhaseName(runtime.phase));
            return;
        }

        Diagnostics::Info("Hero skill viewpoint tick. skill=%s phase=%s currentPitch=%.2f targetPitch=%.2f deltaPitch=%.2f currentYaw=%.2f targetYaw=%.2f deltaYaw=%.2f speed=%.2f dt=%.4f fired=%d jumped=%d",
            skillId.c_str(),
            ViewpointPhaseName(runtime.phase),
            current.X * kRadToDeg,
            targetPitch * kRadToDeg,
            (targetPitch - current.X) * kRadToDeg,
            current.Y * kRadToDeg,
            targetYaw * kRadToDeg,
            NormalizeAngle(targetYaw - current.Y) * kRadToDeg,
            speedDeg,
            deltaSeconds,
            runtime.fired ? 1 : 0,
            runtime.jumped ? 1 : 0);
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
        baseSpeed = std::clamp(baseSpeed, 0.0f, 4000.0f);
        randomRange = std::clamp(randomRange, 0.0f, 4000.0f);
        if (randomRange <= 0.0f)
            return baseSpeed;

        const float low = (std::max)(0.0f, baseSpeed - randomRange);
        const float high = (std::min)(4000.0f, baseSpeed + randomRange);
        if (high <= low)
            return low;

        std::uniform_real_distribution<float> distribution(low, high);
        std::lock_guard<std::mutex> lock(g_randomMutex);
        return distribution(g_random);
    }

    float PickSignedJitter(float range)
    {
        range = std::clamp(range, 0.0f, 20.0f);
        if (range <= 0.0f)
            return 0.0f;

        std::uniform_real_distribution<float> distribution(-range, range);
        std::lock_guard<std::mutex> lock(g_randomMutex);
        return distribution(g_random);
    }

    int PickSequenceDurationMs(const Config::HeroSkillSequenceStep& step)
    {
        float speedScale = step.speedScale;
        if (!std::isfinite(speedScale))
            speedScale = 1.0f;
        speedScale = std::clamp(speedScale, 0.5f, 2.0f);

        float duration = static_cast<float>(std::clamp(step.durationMs, 0, 1000)) * speedScale;
        const int jitterMs = std::clamp(step.jitterMs, 0, 50);
        if (jitterMs > 0) {
            std::uniform_int_distribution<int> distribution(-jitterMs, jitterMs);
            std::lock_guard<std::mutex> lock(g_randomMutex);
            duration += static_cast<float>(distribution(g_random));
        }

        return std::clamp(static_cast<int>(std::lround(duration)), 5, 1000);
    }

    void EnterSequenceStep(const std::string& skillId,
                           int& currentMask,
                           const Config::HeroSkillSequenceStep& step)
    {
        ApplyButtonMaskDiff(skillId, currentMask, step.buttonMask);
        currentMask = step.buttonMask;
    }

    void SleepUntilSequenceDeadline(std::stop_token stopToken, Clock::time_point deadline)
    {
        while (!stopToken.stop_requested()) {
            const Clock::time_point now = Clock::now();
            if (now >= deadline)
                return;

            const auto remaining = deadline - now;
            if (remaining > std::chrono::milliseconds(2))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            else
                std::this_thread::yield();
        }
    }

    void RunSequenceWorker(std::stop_token stopToken,
                           std::string skillId,
                           std::vector<Config::HeroSkillSequenceStep> steps,
                           bool ammoGuardEnabled,
                           int ammoGuardReserve)
    {
        timeBeginPeriod(1);

        int currentMask = 0;
        size_t stepIndex = 0;
        Diagnostics::Info("Hero skill sequence worker started. skill=%s steps=%zu",
            skillId.c_str(), steps.size());

        while (!stopToken.stop_requested() && !steps.empty()) {
            if (stepIndex >= steps.size())
                stepIndex = 0;

            const Config::HeroSkillSequenceStep& step = steps[stepIndex];
            const int durationMs = PickSequenceDurationMs(step);
            if (ConsumeSequenceAmmoBudgetForStep(skillId, currentMask, step.buttonMask, ammoGuardEnabled, ammoGuardReserve))
                break;
            EnterSequenceStep(skillId, currentMask, step);

            if (SequenceDiagnosticsEnabled()) {
                Diagnostics::Aim("sequence.step skill=%s step=%zu mask=0x%02X durationMs=%d baseMs=%d scale=%.3f jitterMs=%d",
                    skillId.c_str(),
                    stepIndex + 1,
                    step.buttonMask,
                    durationMs,
                    step.durationMs,
                    step.speedScale,
                    step.jitterMs);
            }

            SleepUntilSequenceDeadline(stopToken, Clock::now() + std::chrono::milliseconds(durationMs));
            ++stepIndex;
        }

        ApplyButtonMaskDiff(skillId, currentMask, 0);
        ReleaseAllButtons();
        timeEndPeriod(1);
        Diagnostics::Info("Hero skill sequence worker stopped. skill=%s", skillId.c_str());
    }

    void BeginViewpoint(const std::string& skillId,
                        ViewpointRuntime& runtime,
                        const Config::HeroSkillSettings& params)
    {
        const bool prevKeyDown = runtime.prevKeyDown;
        runtime = ViewpointRuntime{};
        runtime.prevKeyDown = prevKeyDown;

        Vector3 currentAngles{};
        if (!TryReadCurrentViewAngles(currentAngles)) {
            runtime.phase = ViewpointPhase::Cancelled;
            Diagnostics::Warn("Hero skill viewpoint start failed: no view angle. skill=%s", skillId.c_str());
            return;
        }

        runtime.initialAngles = currentAngles;

        const float pitchDownTargetDeg = std::clamp(params.pitchDownTargetAngle, 0.0f, 180.0f);
        const float effectivePitchDownTarget = ClampViewPitchTarget(pitchDownTargetDeg * kDegToRad);
        const float effectivePitchDownTargetDeg = effectivePitchDownTarget * kRadToDeg;
        const int durationBaseMs = std::clamp(params.pitchDownDurationMs, 20, 100);
        const float durationJitterMs = std::clamp(params.pitchDownDurationJitter, 0.0f, 50.0f);
        const float pitchUpReturnJitterDeg = PickSignedJitter(params.pitchUpOffsetJitter);
        const float durationMs = (std::max)(
            1.0f,
            PickPhaseSpeed(static_cast<float>(durationBaseMs),
                           durationJitterMs));

        runtime.pitchDownSpeedDeg = std::clamp((pitchDownTargetDeg / durationMs) * 1000.0f, 0.0f, 4000.0f);
        runtime.pitchUpTargetAngle = ClampViewPitchTarget(runtime.initialAngles.X + pitchUpReturnJitterDeg * kDegToRad);
        const float pitchUpTravelDeg = std::fabs(effectivePitchDownTargetDeg - runtime.pitchUpTargetAngle * kRadToDeg);
        runtime.pitchUpSpeedDeg = std::clamp((pitchUpTravelDeg / durationMs) * 1000.0f, 0.0f, 4000.0f);
        runtime.skillVk = MapHotkeyToVK(params.skillKey >= 0 ? params.skillKey : params.key);
        runtime.jumpVk = std::clamp(params.jumpKeyCode, 0, 255);
        runtime.phase = ViewpointPhase::PitchDown;
        runtime.phaseStarted = Clock::now();
        runtime.lastTick = runtime.phaseStarted;

        Diagnostics::Info("Hero skill viewpoint started. skill=%s durationMs=%.2f requestedDownTarget=%.2f effectiveDownTarget=%.2f downSpeed=%.2f returnJitter=%.2f effectiveUpTarget=%.2f upSpeed=%.2f initialPitch=%.2f initialYaw=%.2f",
            skillId.c_str(),
            durationMs,
            pitchDownTargetDeg,
            effectivePitchDownTargetDeg,
            runtime.pitchDownSpeedDeg,
            pitchUpReturnJitterDeg,
            runtime.pitchUpTargetAngle * kRadToDeg,
            runtime.pitchUpSpeedDeg,
            runtime.initialAngles.X * kRadToDeg,
            runtime.initialAngles.Y * kRadToDeg);
    }

    // Rough pixels-per-degree constant for fast viewpoint moves.
    // Bypasses sensitivity calibration and move-splitting — we just want raw speed.
    constexpr float kViewpointPixelsPerDegree = 18.0f;

    void FastRawMouseMove(float pitchDeg, float yawDeg)
    {
        const int px = static_cast<int>(std::lround(yawDeg * kViewpointPixelsPerDegree));
        const int py = static_cast<int>(std::lround(pitchDeg * kViewpointPixelsPerDegree));

        if (Config::kmboxEnabled) {
            if (Config::kmboxDeviceType == 0) {
                if (px != 0 || py != 0)
                    kmbox::KmBoxMgr.Mouse.Move(px, py);
            } else {
                if (px != 0 || py != 0)
                    kmbox::kmBoxBMgr.km_move(px, py);
            }
            return;
        }

        // Non-KMBox fallback: use the normal pipeline (local testing only).
        if (px != 0 || py != 0)
            SendMouseMove(Vector3(pitchDeg * kDegToRad, yawDeg * kDegToRad, 0.0f), 0);
    }

    bool MovePitchToward(float targetPitch, float speedDeg, float deltaSeconds)
    {
        Vector3 current{};
        if (!TryReadCurrentViewAngles(current))
            return false;

        const float deltaPitch = targetPitch - current.X;
        if (std::fabs(deltaPitch) <= kPitchDoneEpsilonRad)
            return true;

        const float maxStepRad = (std::max)(0.0f, speedDeg) * kDegToRad * deltaSeconds;
        if (maxStepRad <= 0.0f)
            return false;

        const float stepRad = ClampDelta(deltaPitch, maxStepRad);
        FastRawMouseMove(stepRad * kRadToDeg, 0.0f);

        return false;
    }

    bool MoveYawToward(float targetYaw, float speedDeg, float deltaSeconds)
    {
        Vector3 current{};
        if (!TryReadCurrentViewAngles(current))
            return false;

        const float deltaYaw = NormalizeAngle(targetYaw - current.Y);
        if (std::fabs(deltaYaw) <= kYawDoneEpsilonRad)
            return true;

        const float maxStepRad = (std::max)(0.0f, speedDeg) * kDegToRad * deltaSeconds;
        if (maxStepRad <= 0.0f)
            return false;

        const float stepRad = ClampDelta(deltaYaw, maxStepRad);
        FastRawMouseMove(0.0f, stepRad * kRadToDeg);

        return false;
    }

    struct ScopedTrackingConfig {
        Config::HeroPreset originalPreset{};
        bool active = false;

        explicit ScopedTrackingConfig(const Config::HeroSkillTrackingParams& params)
        {
            originalPreset = Config::MakeHeroPresetFromCurrent();

            Config::HeroPreset overlay = originalPreset;
            overlay.aimBehavior = Config::ClampAimBehaviorIndex(params.aimBehavior);
            overlay.aimMode = Config::IsTrackingBehavior(overlay.aimBehavior) ? 0 : 1;
            overlay.fov = params.fov;
            overlay.smooth = params.speedScale;
            overlay.bone = params.bone;
            overlay.hitbox = params.hitbox;
            Config::ApplyHeroPresetToGlobals(overlay);
            active = true;
        }

        ~ScopedTrackingConfig()
        {
            if (!active)
                return;

            Config::ApplyHeroPresetToGlobals(originalPreset);
        }
    };

    void RunTrackingOverlayTick(const std::string& skillId,
                                const Config::HeroSkillTrackingParams& params,
                                bool prediction)
    {
        const int behavior = Config::ClampAimBehaviorIndex(params.aimBehavior);
        if (params.fov <= 0.0f || params.speedScale <= 0.0f)
            return;

        ScopedTrackingConfig trackingOverride(params);
        const Vector3 targetVector = GetVector3(prediction);
        c_entity target{};
        if (AimbotDetail::IsZeroVector(targetVector) ||
            !AimbotDetail::IsPrimaryTargetActionable(target)) {
            return;
        }

        const float smoothInput = Config::AimBehaviorSmoothInput(behavior, params.speedScale);
        AimbotDetail::AimData aim = AimbotDetail::BuildAimData(
            targetVector,
            Config::IsFlickBehavior(behavior),
            smoothInput,
            Config::AimBehaviorAcceleration(behavior),
            Config::AimBehaviorMethod(behavior));
        if (!AimbotDetail::IsZeroVector(aim.smoothed_angle)) {
            AimbotDetail::MoveAimDelta(aim.local_angle, aim.smoothed_angle);
            if (Config::aimVerboseLog) {
                Diagnostics::Aim("sequence.aim_tick skill=%s behavior=%d speedScale=%.3f fov=%.3f prediction=%d targetIndex=%d",
                    skillId.c_str(),
                    behavior,
                    params.speedScale,
                    params.fov,
                    prediction ? 1 : 0,
                    Config::Targetenemyi);
            }
        }
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
            return { 60.0f, 45.0f, 0.0f, 0.2f, 320.0f };
        if (skill == "chain-hook")
            return { 62.0f, 20.0f, 0.0f, 0.5f, 100.0f };
        if (skill == "pulse-bomb")
            return { 15.0f, 5.0f, 20.0f, 0.0f, 0.0f };
        return { 60.0f, 40.0f, 0.0f, 0.0f, 0.0f };
    }

    SkillProjectileRuntime ResolveSkillProjectileRuntime(const Config::HeroSkillSettings& settings,
                                                         const TrajectoryParams& params)
    {
        SkillProjectileRuntime runtime{};
        runtime.speed = settings.projectileSpeed > 0.0f ? settings.projectileSpeed : params.speed;
        runtime.maxRange = params.maxRange;
        runtime.gravity = settings.projectileGravity || params.gravity > 0.0f;
        runtime.projectileRadius = settings.projectileRadius > 0.0f
            ? settings.projectileRadius
            : params.projectileRadius;
        runtime.preFireDelayMs = settings.preFireDelayMs > 0.0f
            ? settings.preFireDelayMs
            : params.preFireDelayMs;
        return runtime;
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

    Vector3 SkillAimPointForEntity(c_entity& entity, int boneSetting)
    {
        Vector3 aimPoint = TargetingDetail::ConfiguredBonePosition(entity, boneSetting);
        if (IsUsablePosition(aimPoint))
            return aimPoint;
        return CandidatePositionForAction(entity);
    }

    float ResolveSkillHitWindow(const c_entity& entity,
                                int boneId,
                                const SkillProjectileRuntime& projectile,
                                float hitboxScalePercent)
    {
        const float boneRadius = ResolveBoneHitboxRadius(
            entity.HeroID,
            boneId,
            Config::kLegacyDefaultHitboxRadius);
        const float resolvedWindow = (std::max)(0.0f, boneRadius + projectile.projectileRadius);
        return resolvedWindow * Config::HitboxScaleMultiplier(hitboxScalePercent);
    }

    float AutoMeleeBoneScore(const TargetingDetail::FovRuntimeContext& fovContext,
                             const Vector3& source,
                             const Vector3& point)
    {
        if (fovContext.valid) {
            const float score = TargetingDetail::FovScoreDeg(fovContext, point);
            if (std::isfinite(score))
                return score;
        }

        return source.DistTo(point);
    }

    bool ResolveAutoMeleeBone(c_entity& entity,
                              int boneSetting,
                              const TargetingDetail::FovRuntimeContext& fovContext,
                              const Vector3& source,
                              int& outBoneId,
                              Vector3& outPoint,
                              float& outScore)
    {
        bool found = false;
        float bestScore = (std::numeric_limits<float>::max)();

        auto considerBone = [&](int boneId, const Vector3& point) {
            if (!IsUsablePosition(point) || point == entity.pos)
                return;

            const float score = AutoMeleeBoneScore(fovContext, source, point);
            if (!std::isfinite(score))
                return;

            if (!found || score < bestScore) {
                found = true;
                bestScore = score;
                outBoneId = boneId;
                outPoint = point;
                outScore = score;
            }
        };

        if (boneSetting == Config::kAimBoneClosest) {
            const auto skeleton = entity.GetSkel();
            for (int boneId : skeleton)
                considerBone(boneId, entity.GetBonePos(boneId));
            if (found)
                return true;
        }

        const int normalizedBone = Config::NormalizeAimBone(boneSetting);
        const int boneId = AimBoneToSkeletonBoneId(normalizedBone);
        Vector3 point = TargetingDetail::ConfiguredBonePosition(entity, normalizedBone);
        if (!IsUsablePosition(point))
            point = CandidatePositionForAction(entity);
        considerBone(boneId, point);
        return found;
    }

    bool IsAutoMeleeHitReady(const SkillAimCandidate& candidate)
    {
        if (!candidate.valid || !IsUsablePosition(candidate.aimPoint) ||
            candidate.hitWindow <= 0.0f) {
            return false;
        }

        const AimbotDetail::AimData aim = AimbotDetail::BuildAimData(
            candidate.aimPoint,
            false,
            1.0f,
            0.0f);
        if (!IsFiniteVector(aim.local_angle) ||
            !IsFiniteVector(aim.target_angle) ||
            !IsFiniteVector(aim.local_pos)) {
            return false;
        }

        return OW::in_range(
            aim.local_angle,
            aim.target_angle,
            aim.local_pos,
            candidate.aimPoint,
            candidate.hitWindow);
    }

    SkillAimCandidate FindBestAutoMeleeCandidate(const Config::HeroSkillSettings& settings)
    {
        SkillAimCandidate best{};
        const float effectiveRange = settings.distance;
        if (!std::isfinite(effectiveRange) || effectiveRange <= 0.0f)
            return best;

        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        const Vector3 source = SourcePositionForAction(local);
        if (!IsUsablePosition(source))
            return best;

        const std::vector<c_entity> snapshot = TargetingDetail::SnapshotEntities();
        const TargetingDetail::FovRuntimeContext fovContext =
            TargetingDetail::SnapshotFovRuntimeContext();
        const SkillProjectileRuntime meleeProjectile{};
        const float maxHealth = std::clamp(settings.enemyHealthThreshold, 0.0f, 500.0f);

        float bestScore = (std::numeric_limits<float>::max)();
        float bestDistance = (std::numeric_limits<float>::max)();

        for (size_t index = 0; index < snapshot.size(); ++index) {
            c_entity entity = snapshot[index];
            if (!TargetingDetail::IsSelectableCandidate(entity, 0, local))
                continue;
            if (Config::aimbotIgnoreInvisible && !entity.Vis)
                continue;
            if ((entity.imort || entity.barrprot) && !Config::switch_team)
                continue;
            if (entity.skill1act && entity.HeroID == eHero::HERO_VENTURE)
                continue;
            if (!std::isfinite(entity.PlayerHealth) ||
                entity.PlayerHealth <= 0.0f ||
                entity.PlayerHealth > maxHealth) {
                continue;
            }

            int boneId = BONE_CHEST;
            Vector3 aimPoint{};
            float fovScore = 0.0f;
            if (!ResolveAutoMeleeBone(
                    entity,
                    settings.tracking.bone,
                    fovContext,
                    source,
                    boneId,
                    aimPoint,
                    fovScore)) {
                continue;
            }

            const float distance = source.DistTo(aimPoint);
            if (!std::isfinite(distance) || distance > effectiveRange)
                continue;

            SkillAimCandidate candidate{};
            candidate.valid = true;
            candidate.entity = entity;
            candidate.entityIndex = static_cast<int>(index);
            candidate.entityKey = entity.address ? entity.address : entity.LinkBase;
            candidate.boneId = boneId;
            candidate.distance = distance;
            candidate.vitalityPercent = entity.PlayerHealth;
            candidate.fovScore = fovScore;
            candidate.hitWindow = ResolveSkillHitWindow(
                entity,
                boneId,
                meleeProjectile,
                settings.tracking.hitbox);
            candidate.rawAimPoint = aimPoint;
            candidate.aimPoint = aimPoint;

            if (!IsAutoMeleeHitReady(candidate))
                continue;

            if (fovScore < bestScore ||
                (std::fabs(fovScore - bestScore) <= 0.001f && distance < bestDistance)) {
                best = candidate;
                bestScore = fovScore;
                bestDistance = distance;
            }
        }

        return best;
    }

    SkillAimCandidate FindBestSkillAimCandidate(const Config::HeroSkillSettings& settings,
                                                const SkillProjectileRuntime& projectile)
    {
        SkillAimCandidate best{};
        const float effectiveRange = settings.distance > 0.0f
            ? (std::min)(settings.distance, projectile.maxRange)
            : projectile.maxRange;
        if (!std::isfinite(effectiveRange) || effectiveRange <= 0.0f)
            return best;

        const c_entity local = TargetingDetail::SnapshotLocalEntity();
        const Vector3 source = SourcePositionForAction(local);
        if (!IsUsablePosition(source))
            return best;

        const std::vector<c_entity> snapshot = TargetingDetail::SnapshotEntities();
        const int requiredTargets = (std::max)(1, settings.minTargets);
        const int boneSetting = Config::NormalizeAimBone(settings.tracking.bone);
        const int boneId = AimBoneToSkeletonBoneId(boneSetting);
        const TargetingDetail::FovRuntimeContext fovContext =
            TargetingDetail::SnapshotFovRuntimeContext();
        const ProjectileRuntimeSpec projectileSpec{
            projectile.speed,
            projectile.gravity,
            false
        };

        int matchingTargets = 0;
        float bestScore = (std::numeric_limits<float>::max)();
        float bestDistance = (std::numeric_limits<float>::max)();

        for (size_t index = 0; index < snapshot.size(); ++index) {
            c_entity entity = snapshot[index];
            if (!TargetingDetail::IsSelectableCandidate(entity, 0, local))
                continue;
            if (Config::aimbotIgnoreInvisible && !entity.Vis)
                continue;
            if ((entity.imort || entity.barrprot) && !Config::switch_team)
                continue;

            const float vitality = VitalityPercent(entity);
            if (vitality > settings.enemyHealthThreshold)
                continue;

            const Vector3 rawAimPoint = SkillAimPointForEntity(entity, boneSetting);
            if (!IsUsablePosition(rawAimPoint))
                continue;

            const float rawDistance = source.DistTo(rawAimPoint);
            if (!std::isfinite(rawDistance) || rawDistance > effectiveRange)
                continue;

            ++matchingTargets;

            Vector3 aimPoint = rawAimPoint;
            if (settings.prediction && projectile.speed > 0.0f) {
                const LeadPredictionResult lead = TargetingDetail::ResolveLeadPrediction(
                    entity,
                    rawAimPoint,
                    projectileSpec,
                    true,
                    false,
                    projectile.preFireDelayMs);
                aimPoint = lead.finalAimPoint;
            }

            if (!IsUsablePosition(aimPoint))
                continue;

            const float aimedDistance = source.DistTo(aimPoint);
            if (!std::isfinite(aimedDistance) ||
                aimedDistance > effectiveRange + projectile.projectileRadius) {
                continue;
            }

            float fovScore = 0.0f;
            if (!TargetingDetail::IsWithinFovDeg(
                    fovContext,
                    aimPoint,
                    settings.tracking.fov,
                    &fovScore)) {
                continue;
            }

            if (fovScore < bestScore ||
                (std::fabs(fovScore - bestScore) <= 0.001f && aimedDistance < bestDistance)) {
                best.valid = true;
                best.entity = entity;
                best.entityIndex = static_cast<int>(index);
                best.entityKey = entity.address ? entity.address : entity.LinkBase;
                best.boneId = boneId;
                best.distance = aimedDistance;
                best.vitalityPercent = vitality;
                best.fovScore = fovScore;
                best.rawAimPoint = rawAimPoint;
                best.aimPoint = aimPoint;
                best.hitWindow = ResolveSkillHitWindow(
                    entity,
                    boneId,
                    projectile,
                    settings.tracking.hitbox);
                bestScore = fovScore;
                bestDistance = aimedDistance;
            }
        }

        if (matchingTargets < requiredTargets)
            return SkillAimCandidate{};
        return best;
    }

    bool MoveSkillAimAndCheckReady(const std::string& runtimeKey,
                                   const Config::HeroSkillSettings& settings,
                                   const SkillAimCandidate& candidate,
                                   const SkillProjectileRuntime& projectile)
    {
        if (!candidate.valid || !IsUsablePosition(candidate.aimPoint))
            return false;

        const int behavior = Config::ClampAimBehaviorIndex(settings.tracking.aimBehavior);
        if (settings.tracking.fov <= 0.0f || settings.tracking.speedScale <= 0.0f)
            return false;

        const float smoothInput = Config::AimBehaviorSmoothInput(
            behavior,
            settings.tracking.speedScale);
        AimbotDetail::AimData aim = AimbotDetail::BuildAimData(
            candidate.aimPoint,
            Config::IsFlickBehavior(behavior),
            smoothInput,
            Config::AimBehaviorAcceleration(behavior),
            Config::AimBehaviorMethod(behavior));
        if (!IsFiniteVector(aim.local_angle) ||
            !IsFiniteVector(aim.target_angle) ||
            !IsFiniteVector(aim.local_pos) ||
            candidate.hitWindow <= 0.0f) {
            return false;
        }

        const bool hitBeforeMove = OW::in_range(
            aim.local_angle,
            aim.target_angle,
            aim.local_pos,
            candidate.aimPoint,
            candidate.hitWindow);

        bool hitAfterMove = hitBeforeMove;
        if (!AimbotDetail::IsZeroVector(aim.smoothed_angle)) {
            AimbotDetail::MoveAimDelta(aim.local_angle, aim.smoothed_angle);
            hitAfterMove = OW::in_range(
                aim.smoothed_angle,
                aim.target_angle,
                aim.local_pos,
                candidate.aimPoint,
                candidate.hitWindow);
        }

        if (Config::aimVerboseLog) {
            Diagnostics::Aim("skill.aim_tick skill=%s behavior=%d speedScale=%.3f fov=%.3f prediction=%d projectileSpeed=%.3f preFireMs=%.3f targetIndex=%d fovScore=%.3f distance=%.3f hitWindow=%.3f ready[before/after]=%d/%d raw=(%.3f,%.3f,%.3f) aim=(%.3f,%.3f,%.3f)",
                runtimeKey.c_str(),
                behavior,
                settings.tracking.speedScale,
                settings.tracking.fov,
                settings.prediction ? 1 : 0,
                projectile.speed,
                projectile.preFireDelayMs,
                candidate.entityIndex,
                candidate.fovScore,
                candidate.distance,
                candidate.hitWindow,
                hitBeforeMove ? 1 : 0,
                hitAfterMove ? 1 : 0,
                candidate.rawAimPoint.X,
                candidate.rawAimPoint.Y,
                candidate.rawAimPoint.Z,
                candidate.aimPoint.X,
                candidate.aimPoint.Y,
                candidate.aimPoint.Z);
        }

        return hitBeforeMove || hitAfterMove;
    }

    int MapHotkeyToVK(int hotkey)
    {
        return Labels::AimActivationKeyVk(hotkey);
    }

    int SkillOutputVk(const Config::HeroSkillSettings& settings)
    {
        return MapHotkeyToVK(settings.skillKey >= 0 ? settings.skillKey : settings.key);
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

    bool StartZaryaAutoRightPulse(const std::string& runtimeKey,
                                  bool releaseLeftDuringPulse,
                                  ZaryaAutoRightTiming timing)
    {
        if (!TryBeginTimedAction(runtimeKey))
            return false;

        std::thread([runtimeKey, releaseLeftDuringPulse, timing]() {
            bool releasedLeft = false;
            bool rightPressed = false;
            bool restoredLeft = false;

            rightPressed = SetHotkeyState(VK_RBUTTON, true);
            if (rightPressed) {
                if (releaseLeftDuringPulse) {
                    std::this_thread::sleep_for(timing.leftReleaseDelay);
                    releasedLeft = SetHotkeyState(VK_LBUTTON, false);
                    std::this_thread::sleep_for(timing.afterLeftRelease);
                } else {
                    std::this_thread::sleep_for(timing.rightHold);
                }
                SetHotkeyState(VK_RBUTTON, false);
            }

            if (releasedLeft) {
                std::this_thread::sleep_for(kZaryaAutoRightRestoreSettle);
                if (AimbotDetail::IsInputVkDownQuiet(VK_LBUTTON))
                    restoredLeft = SetHotkeyState(VK_LBUTTON, true);
            }

            Diagnostics::Aim(
                "zarya.ammo_probe pulse_done skill=%s releaseLeft=%d releasedLeft=%d rightPressed=%d restoredLeft=%d xMs=%lld yMs=%lld zMs=%lld",
                runtimeKey.c_str(),
                releaseLeftDuringPulse ? 1 : 0,
                releasedLeft ? 1 : 0,
                rightPressed ? 1 : 0,
                restoredLeft ? 1 : 0,
                static_cast<long long>(timing.rightHold.count()),
                static_cast<long long>(timing.leftReleaseDelay.count()),
                static_cast<long long>(timing.afterLeftRelease.count()));
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

    bool ReadLocalReloadingFast(const c_entity& local, bool& reloading)
    {
        reloading = Config::reloading;
        if (!SDK || !local.SkillBase)
            return false;

        __try {
            reloading = OW::IsSkillActivate1(local.SkillBase + 0x40, 0, kReloadStateSkill);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            reloading = Config::reloading;
            return false;
        }
    }

    void ResetZaryaAmmoProbeBudgetLog(ZaryaAmmoProbeRuntime& runtime)
    {
        runtime.lastBudgetLog = {};
        runtime.lastLoggedAmmo = -1000;
    }

    void LogZaryaAmmoProbeBudget(const std::string& runtimeKey,
                                 ZaryaAmmoProbeRuntime& runtime,
                                 const char* reason,
                                 bool leftDown,
                                 bool rightDown,
                                 Clock::time_point now)
    {
        const int ammoInt = runtime.budgetArmed
            ? static_cast<int>(std::floor(runtime.ammo + 0.001))
            : -1;
        const bool forced = reason && reason[0] != '\0';
        const bool changed = ammoInt != runtime.lastLoggedAmmo;
        const bool periodic = runtime.lastBudgetLog.time_since_epoch().count() == 0 ||
            now - runtime.lastBudgetLog >= kZaryaBudgetLogInterval;
        if (!forced && (!changed || !periodic))
            return;

        runtime.lastBudgetLog = now;
        runtime.lastLoggedAmmo = ammoInt;
        Diagnostics::Aim("zarya.ammo_probe budget skill=%s reason=%s ammo=%.1f threshold=%d left=%d right=%d autoRight=%d",
            runtimeKey.c_str(),
            forced ? reason : "tick",
            runtime.ammo,
            runtime.autoRightThreshold,
            leftDown ? 1 : 0,
            rightDown ? 1 : 0,
            runtime.autoRightSent ? 1 : 0);
    }

    void ArmZaryaAmmoProbeBudget(const std::string& runtimeKey,
                                 ZaryaAmmoProbeRuntime& runtime,
                                 Clock::time_point now,
                                 bool leftDown,
                                 bool rightDown,
                                 long long reloadDurationMs)
    {
        runtime.budgetArmed = true;
        runtime.ammo = static_cast<double>(kZaryaAmmoClipSize);
        runtime.leftWasDown = leftDown;
        runtime.rightWasDown = rightDown;
        runtime.autoRightSent = false;
        runtime.autoRightThreshold = PickZaryaAutoRightThreshold();
        runtime.lastObserved = now;
        ResetZaryaAmmoProbeBudgetLog(runtime);

        Diagnostics::Aim("zarya.ammo_probe reload_success skill=%s durationMs=%lld seedAmmo=%d threshold=%d",
            runtimeKey.c_str(),
            reloadDurationMs,
            kZaryaAmmoClipSize,
            runtime.autoRightThreshold);
        LogZaryaAmmoProbeBudget(runtimeKey, runtime, "reload_seed", leftDown, rightDown, now);
    }

    void UpdateZaryaAmmoProbe(const std::string& runtimeKey,
                              const Config::HeroSkillSettings& settings,
                              const c_entity& local)
    {
        ZaryaAmmoProbeRuntime& runtime = g_zaryaAmmoProbes[runtimeKey];
        const Clock::time_point now = Clock::now();
        const bool leftDown = AimbotDetail::IsInputVkDown(VK_LBUTTON);
        const bool rightDown = AimbotDetail::IsInputVkDown(VK_RBUTTON);

        bool reloading = false;
        const bool fastReloadRead = ReadLocalReloadingFast(local, reloading);
        if (reloading) {
            if (!runtime.lastReloading) {
                runtime.reloadStarted = now;
                runtime.reloadQualified = false;
                runtime.budgetArmed = false;
                runtime.ammo = -1.0;
                runtime.autoRightSent = false;
                runtime.autoRightThreshold = 0;
                ResetZaryaAmmoProbeBudgetLog(runtime);
                Diagnostics::Aim("zarya.ammo_probe reload_start skill=%s source=%s skillBase=0x%llX",
                    runtimeKey.c_str(),
                    fastReloadRead ? "fast" : "cache",
                    static_cast<unsigned long long>(local.SkillBase));
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - runtime.reloadStarted);
            if (!runtime.reloadQualified && elapsed >= kZaryaReloadSuccessMinDuration) {
                runtime.reloadQualified = true;
                Diagnostics::Aim("zarya.ammo_probe reload_qualified skill=%s durationMs=%lld",
                    runtimeKey.c_str(),
                    static_cast<long long>(elapsed.count()));
            }

            runtime.lastReloading = true;
            runtime.leftWasDown = leftDown;
            runtime.rightWasDown = rightDown;
            runtime.lastObserved = now;
            return;
        }

        if (runtime.lastReloading) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - runtime.reloadStarted);
            if (runtime.reloadQualified || elapsed >= kZaryaReloadSuccessMinDuration) {
                ArmZaryaAmmoProbeBudget(
                    runtimeKey,
                    runtime,
                    now,
                    leftDown,
                    rightDown,
                    static_cast<long long>(elapsed.count()));
            } else {
                Diagnostics::Aim("zarya.ammo_probe reload_too_short skill=%s durationMs=%lld",
                    runtimeKey.c_str(),
                    static_cast<long long>(elapsed.count()));
                runtime.budgetArmed = false;
                runtime.ammo = -1.0;
                runtime.autoRightThreshold = 0;
            }

            runtime.lastReloading = false;
            runtime.reloadStarted = {};
            runtime.reloadQualified = false;
            runtime.leftWasDown = leftDown;
            runtime.rightWasDown = rightDown;
            runtime.lastObserved = now;
            return;
        }

        runtime.lastReloading = false;
        if (!runtime.budgetArmed) {
            runtime.leftWasDown = leftDown;
            runtime.rightWasDown = rightDown;
            runtime.lastObserved = now;
            return;
        }

        if (runtime.lastObserved.time_since_epoch().count() == 0)
            runtime.lastObserved = now;

        double elapsedSeconds = std::chrono::duration<double>(now - runtime.lastObserved).count();
        elapsedSeconds = std::clamp(elapsedSeconds, 0.0, 0.25);

        if (runtime.leftWasDown && elapsedSeconds > 0.0) {
            runtime.ammo -= kZaryaPrimaryAmmoPerSecond * elapsedSeconds;
        }

        const bool manualRightEdge = !runtime.rightWasDown && rightDown;
        if (manualRightEdge)
            runtime.ammo -= kZaryaSecondaryAmmoCost;

        runtime.ammo = std::clamp(runtime.ammo, 0.0, static_cast<double>(kZaryaAmmoClipSize));
        runtime.lastObserved = now;
        runtime.leftWasDown = leftDown;
        runtime.rightWasDown = rightDown;

        LogZaryaAmmoProbeBudget(
            runtimeKey,
            runtime,
            manualRightEdge ? "manual_right" : "",
            leftDown,
            rightDown,
            now);

        const int thresholdAmmo = runtime.autoRightThreshold > 0
            ? runtime.autoRightThreshold
            : kZaryaAutoRightThresholdMin;
        const int triggerAmmo = leftDown
            ? std::clamp(thresholdAmmo + kZaryaAutoRightThresholdLeadAmmo, 1, kZaryaAmmoClipSize)
            : thresholdAmmo;
        if (!runtime.autoRightSent && runtime.ammo <= static_cast<double>(triggerAmmo) && !rightDown) {
            const std::string pulseKey = runtimeKey + ":auto_right";
            runtime.autoRightSent = true;
            const bool releaseLeftDuringPulse = leftDown;
            const ZaryaAutoRightTiming timing = PickZaryaAutoRightTiming();
            if (StartZaryaAutoRightPulse(pulseKey, releaseLeftDuringPulse, timing)) {
                runtime.ammo = std::clamp(
                    runtime.ammo - kZaryaSecondaryAmmoCost,
                    0.0,
                    static_cast<double>(kZaryaAmmoClipSize));
                MarkActionExecuted(runtimeKey);
                Diagnostics::Aim("zarya.ammo_probe auto_right skill=%s threshold=%d triggerAmmo=%d xMs=%lld yMs=%lld zMs=%lld releaseLeft=%d ammoAfter=%.1f",
                    runtimeKey.c_str(),
                    thresholdAmmo,
                    triggerAmmo,
                    static_cast<long long>(timing.rightHold.count()),
                    static_cast<long long>(timing.leftReleaseDelay.count()),
                    static_cast<long long>(timing.afterLeftRelease.count()),
                    releaseLeftDuringPulse ? 1 : 0,
                    runtime.ammo);
                LogZaryaAmmoProbeBudget(runtimeKey, runtime, "auto_right", leftDown, true, now);
            } else {
                Diagnostics::Aim("zarya.ammo_probe auto_right_skip skill=%s threshold=%d triggerAmmo=%d xMs=%lld yMs=%lld zMs=%lld releaseLeft=%d ammo=%.1f reason=timed_action_active",
                    runtimeKey.c_str(),
                    thresholdAmmo,
                    triggerAmmo,
                    static_cast<long long>(timing.rightHold.count()),
                    static_cast<long long>(timing.leftReleaseDelay.count()),
                    static_cast<long long>(timing.afterLeftRelease.count()),
                    releaseLeftDuringPulse ? 1 : 0,
                    runtime.ammo);
            }
        }
    }

    bool IsRuntimeActionDefinition(const HeroSkillDefinition& definition)
    {
        const std::string skill = definition.skillId ? definition.skillId : "";
        return skill == "sleep-dart" ||
            skill == "chain-hook" ||
            skill == "rocket-punch" ||
            skill == "pulse-bomb";
    }

    bool IsAutoMeleeDefinition(const HeroSkillDefinition& definition)
    {
        return std::string(definition.skillId ? definition.skillId : "") == "auto-melee";
    }

    bool SkillCooldownReadySentinel(float cooldown)
    {
        return std::isfinite(cooldown) && std::fabs(cooldown - 1.0f) <= 0.001f;
    }

    bool SkillCooldownActive(bool active, float cooldown)
    {
        return !active && std::isfinite(cooldown) && cooldown > 0.05f &&
            !SkillCooldownReadySentinel(cooldown);
    }

    bool IsInRechargeInterval(const HeroSkillDefinition& definition,
                              const Config::HeroSkillSettings& settings,
                              const c_entity& local)
    {
        if (!settings.cooldownGuard)
            return false;

        switch (definition.inputAction) {
        case HeroSkillInputAction::PrimaryFire:
            return Config::reloading;
        case HeroSkillInputAction::Ability1:
            return SkillCooldownActive(local.skill1act, local.skillcd1);
        case HeroSkillInputAction::Ability2:
        case HeroSkillInputAction::SecondaryFire:
            return SkillCooldownActive(local.skill2act, local.skillcd2);
        case HeroSkillInputAction::Ultimate:
            return !std::isfinite(local.ultimate) || local.ultimate < 100.0f;
        default:
            return false;
        }
    }

    bool IsManualCooldownActive(const std::string& runtimeKey,
                                const Config::HeroSkillSettings& settings)
    {
        if (!std::isfinite(settings.cooldown) || settings.cooldown <= 0.001f)
            return false;

        const auto item = g_lastActionExecutions.find(runtimeKey);
        if (item == g_lastActionExecutions.end())
            return false;

        const auto cooldown = std::chrono::duration<float>(settings.cooldown);
        return Clock::now() - item->second < cooldown;
    }

    bool ShouldLogSkillGuard(const std::string& runtimeKey)
    {
        Clock::time_point& lastLog = g_lastSkillGuardLogs[runtimeKey];
        const Clock::time_point now = Clock::now();
        if (lastLog.time_since_epoch().count() != 0 &&
            now - lastLog < std::chrono::milliseconds(500)) {
            return false;
        }
        lastLog = now;
        return true;
    }

    bool IsSequenceActive(const std::string& runtimeKey)
    {
        const auto item = g_sequences.find(runtimeKey);
        return item != g_sequences.end() && item->second.active;
    }

    void RefreshAnyInputSequenceActive()
    {
        bool active = false;
        for (const auto& item : g_sequences) {
            const SequenceRuntime& runtime = item.second;
            if (runtime.active || runtime.worker.joinable() || runtime.currentMask != 0) {
                active = true;
                break;
            }
        }
        g_anyInputSequenceActive.store(active, std::memory_order_release);
    }

    bool UsesAsheAmmoBudgetFallback(const std::string& runtimeKey)
    {
        static const std::string asheFirePatternKey =
            std::to_string(static_cast<uint64_t>(eHero::HERO_ASHE)) + ":fire-pattern";
        return runtimeKey == asheFirePatternKey;
    }

    int AsheAmmoClipSize()
    {
        return 12;
    }

    int ClampSequenceReserve(int reserveAmmo)
    {
        return std::clamp(reserveAmmo, 0, 50);
    }

    int CountSequenceShotEvents(int previousMask, int nextMask)
    {
        const bool previousLeft = (previousMask & 0x01) != 0;
        const bool previousRight = (previousMask & 0x02) != 0;
        const bool nextLeft = (nextMask & 0x01) != 0;
        const bool nextRight = (nextMask & 0x02) != 0;

        const bool hipShot = !previousLeft && nextLeft && !nextRight;
        const bool scopedReleaseShot = previousLeft && previousRight && !nextLeft && nextRight;
        return (hipShot ? 1 : 0) + (scopedReleaseShot ? 1 : 0);
    }

    void ResetSequenceAmmoBudget(const std::string& runtimeKey)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return;

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        SequenceAmmoBudgetState& budget = g_sequenceAmmoBudgets[runtimeKey];
        budget.remaining = AsheAmmoClipSize();
        budget.blocked = false;
    }

    void UpdateSequenceAmmoBudgetReloadState(const std::string& runtimeKey,
                                             const c_entity& local,
                                             int reserveAmmo)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return;

        bool reloading = Config::reloading;
        (void)ReadLocalReloadingFast(local, reloading);

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        SequenceAmmoBudgetState& budget = g_sequenceAmmoBudgets[runtimeKey];
        budget.reserve = ClampSequenceReserve(reserveAmmo);
        const bool wasReloading = budget.reloadObserved && budget.lastReloading;

        if (wasReloading && !reloading) {
            budget.remaining = AsheAmmoClipSize();
            budget.blocked = false;
            Diagnostics::Aim("sequence.ammo_budget reload_reset skill=%s remaining=%d reserve=%d",
                runtimeKey.c_str(),
                budget.remaining,
                budget.reserve);
        }

        budget.lastReloading = reloading;
        budget.reloadObserved = true;
    }

    void UpdateSequenceAmmoBudgetFromRead(const std::string& runtimeKey, int reserveAmmo, int ammo)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return;

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        SequenceAmmoBudgetState& budget = g_sequenceAmmoBudgets[runtimeKey];
        budget.reserve = ClampSequenceReserve(reserveAmmo);
        if (ammo >= 0) {
            budget.remaining = ammo;
            budget.blocked = ammo <= budget.reserve;
        } else if (budget.remaining < 0) {
            budget.remaining = AsheAmmoClipSize();
            budget.blocked = false;
        }
    }

    int SequenceAmmoBudgetRemaining(const std::string& runtimeKey)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return -1;

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        const auto item = g_sequenceAmmoBudgets.find(runtimeKey);
        if (item == g_sequenceAmmoBudgets.end())
            return -1;
        return item->second.remaining;
    }

    bool IsSequenceAmmoBudgetBlocking(const std::string& runtimeKey, int reserveAmmo)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return false;

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        SequenceAmmoBudgetState& budget = g_sequenceAmmoBudgets[runtimeKey];
        budget.reserve = ClampSequenceReserve(reserveAmmo);
        if (budget.remaining < 0)
            budget.remaining = AsheAmmoClipSize();
        budget.blocked = budget.remaining <= budget.reserve;
        return budget.blocked;
    }

    bool ConsumeSequenceAmmoBudgetForStep(const std::string& runtimeKey,
                                          int previousMask,
                                          int nextMask,
                                          bool ammoGuardEnabled,
                                          int reserveAmmo)
    {
        if (!UsesAsheAmmoBudgetFallback(runtimeKey))
            return false;

        const int shotEvents = CountSequenceShotEvents(previousMask, nextMask);
        if (shotEvents <= 0)
            return false;

        std::lock_guard<std::mutex> lock(g_sequenceAmmoBudgetMutex);
        SequenceAmmoBudgetState& budget = g_sequenceAmmoBudgets[runtimeKey];
        budget.reserve = ClampSequenceReserve(reserveAmmo);
        if (budget.remaining < 0)
            budget.remaining = AsheAmmoClipSize();

        if (ammoGuardEnabled && budget.remaining <= budget.reserve) {
            budget.blocked = true;
            Diagnostics::Aim("sequence.ammo_budget block skill=%s remaining=%d reserve=%d shotEvents=%d",
                runtimeKey.c_str(),
                budget.remaining,
                budget.reserve,
                shotEvents);
            return true;
        }

        budget.remaining = (std::max)(0, budget.remaining - shotEvents);
        budget.blocked = ammoGuardEnabled && budget.remaining <= budget.reserve;
        Diagnostics::Aim("sequence.ammo_budget consume skill=%s remaining=%d reserve=%d shotEvents=%d blocked=%d enabled=%d",
            runtimeKey.c_str(),
            budget.remaining,
            budget.reserve,
            shotEvents,
            budget.blocked ? 1 : 0,
            ammoGuardEnabled ? 1 : 0);
        return false;
    }

    int RoundedAmmoValue(float value)
    {
        if (!std::isfinite(value) || value < 0.0f || value > 300.0f)
            return -1;
        if (value > 0.0f && value < 0.01f)
            return -1;
        return static_cast<int>(std::lround(value));
    }

    bool IsClipAmmoCandidate(int value)
    {
        return value >= 0 && value <= AsheAmmoClipSize();
    }

    int ReadRawSkillListAmmoValue(uint64_t skillBase, uint16_t wantedId)
    {
        if (!SDK || !skillBase)
            return -1;

        __try {
            const uint64_t skillContainer = SDK->RPM<uint64_t>(skillBase + 0x1848);
            if (!skillContainer)
                return -1;

            const uint64_t rawList = SDK->RPM<uint64_t>(skillContainer + 0x10);
            const uint32_t rawSize = SDK->RPM<uint32_t>(skillContainer + 0x18);
            if (!rawList || rawSize == 0 || rawSize > 512)
                return -1;

            constexpr int kOffsets[] = { 0x30, 0x34, 0x38, 0x3C, 0x40, 0x48, 0x50, 0x60 };
            int zeroCandidate = -1;
            for (uint32_t index = 0; index < rawSize; ++index) {
                const uint64_t entry = rawList + static_cast<uint64_t>(index) * 0x80;
                if (SDK->RPM<uint16_t>(entry) != wantedId)
                    continue;

                for (const int offset : kOffsets) {
                    const int floatValue = RoundedAmmoValue(SDK->RPM<float>(entry + offset));
                    if (IsClipAmmoCandidate(floatValue)) {
                        if (floatValue > 0)
                            return floatValue;
                        zeroCandidate = 0;
                    }

                    const int intValue = SDK->RPM<int>(entry + offset);
                    if (IsClipAmmoCandidate(intValue)) {
                        if (intValue > 0)
                            return intValue;
                        zeroCandidate = 0;
                    }
                }
            }
            return zeroCandidate;
        } __except (1) {
            return -1;
        }
    }

    AmmoGuardSample TryReadLocalAmmoRemaining(const c_entity& local)
    {
        AmmoGuardSample sample{};
        sample.skillBase = local.SkillBase;
        sample.heroId = local.HeroID;
        if (!local.SkillBase)
            return sample;

        const uint64_t base = local.SkillBase + 0x40;
        sample.stateAmmo = OW::readammo(base, 0, kStateScriptAmmo);
        sample.reserveAmmoPath = OW::readammo(base, 0x0B, kStateScriptAmmo);
        sample.reserveState = OW::readammo(base, 0, 0x0B);
        sample.rawStateAmmo = ReadRawSkillListAmmoValue(local.SkillBase, kStateScriptAmmo);
        sample.rawReserveState = ReadRawSkillListAmmoValue(local.SkillBase, 0x0B);

        const int candidates[] = {
            sample.stateAmmo,
            sample.reserveAmmoPath,
            sample.reserveState,
            sample.rawStateAmmo,
            sample.rawReserveState,
        };

        for (size_t index = 0; index < std::size(candidates); ++index) {
            const int candidate = candidates[index];
            if (candidate >= 0 && candidate <= 12) {
                sample.ammo = candidate;
                sample.source = static_cast<int>(index) + 1;
                return sample;
            }
        }

        for (size_t index = 0; index < std::size(candidates); ++index) {
            const int candidate = candidates[index];
            if (candidate >= 0) {
                sample.ammo = candidate;
                sample.source = static_cast<int>(index) + 1;
                return sample;
            }
        }

        return sample;
    }

    void LogAmmoGuardSample(const std::string& runtimeKey,
                            const AmmoGuardSample& sample,
                            int reserveAmmo,
                            bool blocking)
    {
        AmmoGuardLogState& state = g_lastAmmoGuardSamples[runtimeKey];
        const Clock::time_point now = Clock::now();
        const bool changed =
            state.ammo != sample.ammo ||
            state.stateAmmo != sample.stateAmmo ||
            state.reserveAmmoPath != sample.reserveAmmoPath ||
            state.reserveState != sample.reserveState ||
            state.rawStateAmmo != sample.rawStateAmmo ||
            state.rawReserveState != sample.rawReserveState ||
            state.budgetAmmo != sample.budgetAmmo ||
            state.source != sample.source;
        const bool periodic = state.lastLog.time_since_epoch().count() == 0 ||
            now - state.lastLog >= std::chrono::milliseconds(500);

        if (!changed && !blocking && !periodic)
            return;

        state.ammo = sample.ammo;
        state.stateAmmo = sample.stateAmmo;
        state.reserveAmmoPath = sample.reserveAmmoPath;
        state.reserveState = sample.reserveState;
        state.rawStateAmmo = sample.rawStateAmmo;
        state.rawReserveState = sample.rawReserveState;
        state.budgetAmmo = sample.budgetAmmo;
        state.source = sample.source;
        state.lastLog = now;

        Diagnostics::Aim("sequence.ammo_guard skill=%s ammo=%d reserve=%d blocking=%d source=%d stateAmmo=%d reserveAmmoPath=%d reserveState=%d rawStateAmmo=%d rawReserveState=%d budgetAmmo=%d hero=0x%llX skillBase=0x%llX",
            runtimeKey.c_str(),
            sample.ammo,
            reserveAmmo,
            blocking ? 1 : 0,
            sample.source,
            sample.stateAmmo,
            sample.reserveAmmoPath,
            sample.reserveState,
            sample.rawStateAmmo,
            sample.rawReserveState,
            sample.budgetAmmo,
            static_cast<unsigned long long>(sample.heroId),
            static_cast<unsigned long long>(sample.skillBase));
    }

    bool IsAmmoGuardBlocking(const std::string& runtimeKey,
                             const Config::HeroSkillSettings& settings,
                             const c_entity& local)
    {
        if (!settings.ammoGuard)
            return false;

        const int reserveAmmo = std::clamp(settings.ammoGuardReserve, 0, 50);
        AmmoGuardSample sample = TryReadLocalAmmoRemaining(local);
        UpdateSequenceAmmoBudgetFromRead(runtimeKey, reserveAmmo, sample.ammo);

        if (sample.ammo < 0 && UsesAsheAmmoBudgetFallback(runtimeKey)) {
            sample.budgetAmmo = SequenceAmmoBudgetRemaining(runtimeKey);
            sample.ammo = sample.budgetAmmo;
            sample.source = 6;
        }

        const bool blocking = sample.ammo >= 0 &&
            (sample.ammo <= reserveAmmo || IsSequenceAmmoBudgetBlocking(runtimeKey, reserveAmmo));
        LogAmmoGuardSample(runtimeKey, sample, reserveAmmo, blocking);

        if (sample.ammo < 0)
            return false;

        if (!blocking)
            return false;

        return true;
    }

    bool EvaluateTrajectoryAction(const std::string& runtimeKey,
                                  const HeroSkillDefinition& definition,
                                  const Config::HeroSkillSettings& settings,
                                  const TrajectoryParams& params)
    {
        if (!IsActivationKeyHeld(settings.key))
            return false;

        if (SkillControls(definition, HeroSkillControls::TrackingOverlay)) {
            const SkillProjectileRuntime projectile =
                ResolveSkillProjectileRuntime(settings, params);
            ScopedTrackingConfig trackingOverride(settings.tracking);
            const SkillAimCandidate candidate = FindBestSkillAimCandidate(settings, projectile);
            if (!candidate.valid)
                return false;

            if (!MoveSkillAimAndCheckReady(runtimeKey, settings, candidate, projectile))
                return false;

            if (IsActionDebounced(runtimeKey))
                return false;

            const int vk = SkillOutputVk(settings);
            if (!StartTimedHotkey(runtimeKey, vk, kBriefPulse))
                return false;

            MarkActionExecuted(runtimeKey);
            Diagnostics::Info("Hero skill aimed trajectory fired. skill=%s targetIndex=%d distance=%.2f hp=%.1f fovScore=%.2f hitWindow=%.3f projectileSpeed=%.1f preFireMs=%.1f vk=%d",
                definition.skillId ? definition.skillId : "",
                candidate.entityIndex,
                candidate.distance,
                candidate.vitalityPercent,
                candidate.fovScore,
                candidate.hitWindow,
                projectile.speed,
                projectile.preFireDelayMs,
                vk);
            return true;
        }

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

        const int vk = SkillOutputVk(settings);
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

    bool EvaluateAutoMeleeAction(const std::string& runtimeKey,
                                 const HeroSkillDefinition& definition,
                                 const Config::HeroSkillSettings& settings)
    {
        const SkillAimCandidate candidate = FindBestAutoMeleeCandidate(settings);
        if (!candidate.valid || IsActionDebounced(runtimeKey))
            return false;

        const int vk = SkillOutputVk(settings);
        if (!StartTimedHotkey(runtimeKey, vk, kBriefPulse))
            return false;

        MarkActionExecuted(runtimeKey);
        Diagnostics::Info("Hero skill auto melee fired. skill=%s targetIndex=%d distance=%.2f hp=%.1f fovScore=%.2f bone=%d hitWindow=%.3f vk=%d",
            definition.skillId ? definition.skillId : "",
            candidate.entityIndex,
            candidate.distance,
            candidate.vitalityPercent,
            candidate.fovScore,
            candidate.boneId,
            candidate.hitWindow,
            vk);
        return true;
    }

    bool EvaluateChargeReleaseAction(const std::string& runtimeKey,
                                     const HeroSkillDefinition& definition,
                                     const Config::HeroSkillSettings& settings)
    {
        if (!IsActivationKeyHeld(settings.key))
            return false;

        const float effectiveRange = settings.distance > 0.0f
            ? (std::min)(settings.distance, 20.0f)
            : 20.0f;
        const CandidateEntity candidate = FindBestCandidate(settings, effectiveRange);
        if (candidate.entityIndex < 0 || IsActionDebounced(runtimeKey))
            return false;

        const float chargeScale = std::clamp(candidate.distance / effectiveRange, 0.0f, 1.0f);
        const int chargeMs = std::clamp(static_cast<int>(chargeScale * 1000.0f), 200, 1000);
        const int vk = SkillOutputVk(settings);
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
        if (!IsActivationKeyHeld(settings.key))
            return false;

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

        const int vk = SkillOutputVk(settings);
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
        g_lastSkillGuardLogs.erase(skillId);
        g_lastAmmoGuardSamples.erase(skillId);
        g_zaryaAmmoProbes.erase(skillId);
    }

    std::string RuntimeSkillKey(uint64_t heroId, const char* skillId)
    {
        return std::to_string(heroId) + ":" + (skillId ? skillId : "");
    }

} // namespace

bool AnyInputSequenceActive()
{
    return g_anyInputSequenceActive.load(std::memory_order_acquire);
}

ExecutionToken ActiveInputSequenceToken()
{
    return MakeExecutionToken(ExecutionSource::SequenceInternal, AnyInputSequenceActive());
}

bool ShouldBlockForActiveSequence(ExecutionSource requester)
{
    if (requester == ExecutionSource::SequenceInternal)
        return false;

    return ShouldYieldToExecution(MakeExecutionToken(requester), ActiveInputSequenceToken());
}

void RunInputSequence(const std::string& skillId,
                      const std::vector<Config::HeroSkillSequenceStep>& steps,
                      int key,
                      const Config::HeroSkillTrackingParams& trackingParams,
                      bool prediction,
                      bool ammoGuardEnabled,
                      int ammoGuardReserve)
{
    SequenceRuntime& runtime = g_sequences[skillId];
    const bool held = IsSequenceSelfTestHeld(key) || IsActivationKeyHeld(key);
    const bool useWorker = UseSequenceWorker();

    if (!held || steps.empty()) {
        if (runtime.active)
            CancelSequence(skillId, runtime, held ? "empty_steps" : "activation_released");
        return;
    }

    if (!runtime.active) {
        runtime.active = true;
        runtime.stepIndex = 0;
        runtime.currentMask = 0;
        runtime.stepStarted = Clock::now();
        runtime.effectiveDurationMs = PickSequenceDurationMs(steps.front());
        g_anyInputSequenceActive.store(true, std::memory_order_release);
        MarkActionExecuted(skillId);

        if (useWorker) {
            runtime.worker = std::jthread(RunSequenceWorker,
                skillId,
                std::vector<Config::HeroSkillSequenceStep>(steps.begin(), steps.end()),
                ammoGuardEnabled,
                ammoGuardReserve);
        } else {
            if (ConsumeSequenceAmmoBudgetForStep(
                    skillId, runtime.currentMask, steps.front().buttonMask, ammoGuardEnabled, ammoGuardReserve)) {
                CancelSequence(skillId, runtime, "ammo_guard_budget");
                return;
            }
            EnterSequenceStep(skillId, runtime.currentMask, steps.front());
        }

        Diagnostics::Info("Hero skill sequence started. skill=%s steps=%zu activationKey=%d",
            skillId.c_str(), steps.size(), key);

        if (SequenceDiagnosticsEnabled() && !useWorker) {
            const Config::HeroSkillSequenceStep& step = steps.front();
            Diagnostics::Aim("sequence.step skill=%s step=%zu mask=0x%02X durationMs=%d baseMs=%d scale=%.3f jitterMs=%d",
                skillId.c_str(),
                runtime.stepIndex + 1,
                step.buttonMask,
                runtime.effectiveDurationMs,
                step.durationMs,
                step.speedScale,
                step.jitterMs);
        }
    } else if (!useWorker) {
        if (runtime.stepIndex >= steps.size()) {
            runtime.stepIndex = 0;
            runtime.stepStarted = Clock::now();
            runtime.effectiveDurationMs = PickSequenceDurationMs(steps.front());
            if (ConsumeSequenceAmmoBudgetForStep(
                    skillId, runtime.currentMask, steps.front().buttonMask, ammoGuardEnabled, ammoGuardReserve)) {
                CancelSequence(skillId, runtime, "ammo_guard_budget");
                return;
            }
            EnterSequenceStep(skillId, runtime.currentMask, steps.front());
        }

        const Clock::time_point now = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runtime.stepStarted);
        if (elapsed.count() >= runtime.effectiveDurationMs) {
            runtime.stepIndex = (runtime.stepIndex + 1) % steps.size();
            const Config::HeroSkillSequenceStep& nextStep = steps[runtime.stepIndex];
            runtime.stepStarted = now;
            runtime.effectiveDurationMs = PickSequenceDurationMs(nextStep);
            if (ConsumeSequenceAmmoBudgetForStep(
                    skillId, runtime.currentMask, nextStep.buttonMask, ammoGuardEnabled, ammoGuardReserve)) {
                CancelSequence(skillId, runtime, "ammo_guard_budget");
                return;
            }
            EnterSequenceStep(skillId, runtime.currentMask, nextStep);

            if (SequenceDiagnosticsEnabled()) {
                Diagnostics::Aim("sequence.step skill=%s step=%zu mask=0x%02X durationMs=%d baseMs=%d scale=%.3f jitterMs=%d",
                    skillId.c_str(),
                    runtime.stepIndex + 1,
                    nextStep.buttonMask,
                    runtime.effectiveDurationMs,
                    nextStep.durationMs,
                    nextStep.speedScale,
                    nextStep.jitterMs);
            }
        }
    }

    RunTrackingOverlayTick(skillId, trackingParams, prediction);
    UpdateSequenceHitTiming(skillId, runtime);
}

HeroSkillRunState RunViewpointController(const std::string& skillId,
                                         const Config::HeroSkillSettings& params)
{
    ViewpointRuntime& runtime = g_viewpoints[skillId];
    const bool keyDown = IsActivationKeyHeld(params.key);

    if (Config::doingentity == 0) {
        CancelViewpoint(skillId, runtime, "shutdown");
        runtime.prevKeyDown = keyDown;
        return HeroSkillRunState::Cancelled;
    }

    const bool active = runtime.phase == ViewpointPhase::PitchDown ||
        runtime.phase == ViewpointPhase::PitchUp;
    if (!active && keyDown && !runtime.prevKeyDown)
        BeginViewpoint(skillId, runtime, params);

    runtime.prevKeyDown = keyDown;

    const Clock::time_point now = Clock::now();
    UpdateViewpointPulses(skillId, runtime, now);

    switch (runtime.phase) {
    case ViewpointPhase::Idle:
    case ViewpointPhase::Completed:
        return HeroSkillRunState::Completed;
    case ViewpointPhase::Cancelled:
        return HeroSkillRunState::Cancelled;
    case ViewpointPhase::PitchDown: {
        const float dt = DeltaSeconds(runtime, now);
        const float targetPitch = ClampViewPitchTarget(
            std::clamp(params.pitchDownTargetAngle, 0.0f, 180.0f) * kDegToRad);
        LogViewpointTick(skillId, runtime, targetPitch, runtime.initialAngles.Y, runtime.pitchDownSpeedDeg, dt, now);
        const bool pitchDone = MovePitchToward(targetPitch, runtime.pitchDownSpeedDeg, dt);

        Vector3 current{};
        const bool haveCurrent = TryReadCurrentViewAngles(current);
        const bool nearFloor = haveCurrent && current.X >= kViewpointFloorReadyPitchRad;
        if (nearFloor) {
            if (runtime.floorReadyStarted.time_since_epoch().count() == 0)
                runtime.floorReadyStarted = now;
        } else {
            runtime.floorReadyStarted = {};
        }

        const bool floorSettled = nearFloor && now - runtime.floorReadyStarted >= kViewpointFloorSettle;
        if (pitchDone || floorSettled) {
            Diagnostics::Info("Hero skill viewpoint pitch down reached. skill=%s targetPitch=%.2f currentPitch=%.2f settled=%d",
                skillId.c_str(),
                targetPitch * kRadToDeg,
                haveCurrent ? current.X * kRadToDeg : 0.0f,
                floorSettled ? 1 : 0);
            BeginSecondaryPulse(skillId, runtime, now);
            runtime.phase = ViewpointPhase::PitchUp;
            runtime.phaseStarted = now;
            runtime.lastTick = runtime.phaseStarted;
            runtime.lastDebugLog = {};
            runtime.floorReadyStarted = {};
        }
        break;
    }
    case ViewpointPhase::PitchUp: {
        const float dt = DeltaSeconds(runtime, now);
        const float downTargetPitch = ClampViewPitchTarget(
            std::clamp(params.pitchDownTargetAngle, 0.0f, 180.0f) * kDegToRad);
        if (!runtime.fired) {
            LogViewpointTick(skillId, runtime, downTargetPitch, runtime.initialAngles.Y, runtime.pitchDownSpeedDeg, dt, now);
            MovePitchToward(downTargetPitch, runtime.pitchDownSpeedDeg, dt);
            break;
        }

        const int fireDelayMs = std::clamp(params.fireDelayMs, 0, 100);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - runtime.fireDelayStarted);
        if (!runtime.jumped && elapsed.count() < fireDelayMs) {
            LogViewpointTick(skillId, runtime, downTargetPitch, runtime.initialAngles.Y, runtime.pitchDownSpeedDeg, dt, now);
            MovePitchToward(downTargetPitch, runtime.pitchDownSpeedDeg, dt);
            break;
        }

        if (!runtime.jumped) {
            Vector3 verifiedAngles{};
            if (TryReadCurrentViewAngles(verifiedAngles)) {
                if (runtime.jumpVk > 0 && !BeginKeyboardPulse(skillId, runtime, runtime.jumpVk, now)) {
                    Diagnostics::Warn("Hero skill keyboard pulse failed. skill=%s vk=%d",
                        skillId.c_str(), runtime.jumpVk);
                }
                runtime.jumped = true;
                Diagnostics::Info("Hero skill viewpoint jump gate passed. skill=%s pitch=%.2f yaw=%.2f elapsedMs=%lld",
                    skillId.c_str(),
                    verifiedAngles.X * kRadToDeg,
                    verifiedAngles.Y * kRadToDeg,
                    static_cast<long long>(elapsed.count()));
            }
            break;
        }

        LogViewpointTick(skillId, runtime, runtime.pitchUpTargetAngle, runtime.initialAngles.Y, runtime.pitchUpSpeedDeg, dt, now);
        const auto returnElapsed = now - runtime.phaseStarted;
        const bool returnTimedOut = returnElapsed >= kViewpointReturnTimeout;
        bool pitchDone = IsPitchWithin(runtime.pitchUpTargetAngle, kViewpointReturnDoneEpsilonRad);
        if (!pitchDone && !returnTimedOut)
            pitchDone = MovePitchToward(runtime.pitchUpTargetAngle, runtime.pitchUpSpeedDeg, dt);

        bool yawDone = IsYawWithin(runtime.initialAngles.Y, kViewpointYawReturnEpsilonRad);
        if (pitchDone && !yawDone && !returnTimedOut &&
            returnElapsed < kViewpointYawRestoreBudget) {
            yawDone = MoveYawToward(runtime.initialAngles.Y, runtime.pitchUpSpeedDeg, dt);
        }

        if (runtime.jumped && !runtime.jumpDown &&
            (returnTimedOut || (pitchDone && (yawDone || returnElapsed >= kViewpointYawRestoreBudget)))) {
            runtime.phase = ViewpointPhase::Completed;
            Diagnostics::Info("Hero skill viewpoint completed. skill=%s timeout=%d pitchDone=%d yawDone=%d",
                skillId.c_str(),
                returnTimedOut ? 1 : 0,
                pitchDone ? 1 : 0,
                yawDone ? 1 : 0);
            return HeroSkillRunState::Completed;
        }
        break;
    }
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

    ReleaseAllButtons();
    g_lastActionExecutions.clear();
    g_zaryaAmmoProbes.clear();
    RefreshAnyInputSequenceActive();
}

void ProcessHeroSkills()
{
    if (Config::doingentity == 0 || !ProcessConnection::IsConnected()) {
        CancelActiveSkill();
        return;
    }

    const c_entity localSnapshot = TargetingDetail::SnapshotLocalEntity();
    const SequenceSelfTestRuntime& selfTest = SequenceSelfTest();
    UpdateSequenceSelfTestReload();
    const bool forceSelfTestSkill = selfTest.enabled && selfTest.forceSkill;
    const uint64_t heroId = forceSelfTestSkill ? selfTest.heroId : localSnapshot.HeroID;
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
        if (forceSelfTestSkill &&
            std::string(definition.skillId ? definition.skillId : "") != selfTest.skillId) {
            continue;
        }

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

        if (IsZaryaReloadAmmoProbeDefinition(definition)) {
            UpdateZaryaAmmoProbe(skillKey, settings, localSnapshot);
            continue;
        }

        const bool sequenceSkill = SkillControls(definition, HeroSkillControls::SequenceSteps);
        const bool sequenceActive = sequenceSkill && IsSequenceActive(skillKey);
        if (sequenceSkill)
            UpdateSequenceAmmoBudgetReloadState(skillKey, localSnapshot, settings.ammoGuardReserve);
        if (sequenceSkill && !sequenceActive && AimbotDetail::IsInputVkDown('R'))
            ResetSequenceAmmoBudget(skillKey);

        if (!sequenceActive && IsInRechargeInterval(definition, settings, localSnapshot)) {
            CancelSkill(skillKey);
            if (ShouldLogSkillGuard(skillKey)) {
                Diagnostics::Aim("skill.cooldown_guard block skill=%s input=%d reloading=%d skillcd1=%.3f skillcd2=%.3f ultimate=%.1f",
                    skillKey.c_str(),
                    static_cast<int>(definition.inputAction),
                    Config::reloading ? 1 : 0,
                    localSnapshot.skillcd1,
                    localSnapshot.skillcd2,
                    localSnapshot.ultimate);
            }
            continue;
        }

        if (!sequenceActive && IsManualCooldownActive(skillKey, settings)) {
            if (ShouldLogSkillGuard(skillKey)) {
                Diagnostics::Aim("skill.manual_cooldown block skill=%s cooldown=%.3f",
                    skillKey.c_str(),
                    settings.cooldown);
            }
            continue;
        }

        if (IsAmmoGuardBlocking(skillKey, settings, localSnapshot)) {
            CancelSkill(skillKey);
            continue;
        }

        if (IsAutoMeleeDefinition(definition)) {
            EvaluateAutoMeleeAction(skillKey, definition, settings);
            continue;
        }

        if (sequenceSkill) {
            RunInputSequence(skillKey,
                             settings.sequenceSteps,
                             settings.key,
                             settings.tracking,
                             settings.prediction,
                             settings.ammoGuard,
                             settings.ammoGuardReserve);
        }

        if (SkillControls(definition, HeroSkillControls::PitchControl) ||
            SkillControls(definition, HeroSkillControls::PhaseTiming)) {
            RunViewpointController(skillKey, settings);
        }

        if (!IsRuntimeActionDefinition(definition))
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
