#pragma once

#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <imgui.h>

// -----------------------------------------------------------------------
// OW::Config -- Single source of truth for all cheat configuration.
// All variables are inline (C++17) globals.
// -----------------------------------------------------------------------

namespace OW { namespace Config {

    inline constexpr int kAimBoneChest = 0;
    inline constexpr int kAimBoneHead  = 1;
    inline constexpr int kAimBoneNeck  = 2;

    int NormalizeAimBone(int aimBone);
    const char* AimBoneName(int aimBone);

    inline std::string configFileName = "config.ini";
    inline std::string lastConfigProfile = "config.ini";
    std::string ConfigPath();
    std::string HeroConfigPath();
    std::string HeroConfigPath(const std::string& configPath);

    // ---- Synchronisation ----
    inline std::mutex mutex;

    // ---- Aim modes ----
    inline bool enableAimbot  = true;
    inline bool triggerbot    = false;
    inline bool triggerbot2   = false;
    inline bool Tracking      = false;
    inline bool Tracking2     = false;
    inline bool Flick         = false;
    inline bool Flick2        = false;

    // ---- Triggerbot (primary) ----
    inline int   triggerbotMode = 0;          // 0=Hold, 1=Toggle, 2=Always
    inline int   triggerbotKey  = 1;          // key index (reuses activation key VK list; None maps to VK 0)
    inline float triggerbotShotInterval = 0.0f;   // scaled 0-100 → 0-500ms (slider value)
    inline bool  triggerbotChargeAware  = false;  // wait for charge before firing
    inline float triggerbotMinCharge    = 30.0f;  // minimum charge % (0-100, for charge-aware)
    inline bool  triggerbotIgnoreInvisible = true;

    // ---- Triggerbot (secondary / triggerbot2) ----
    inline int   triggerbotMode2 = 0;
    inline int   triggerbotKey2  = 1;
    inline float triggerbotShotInterval2 = 0.0f;
    inline bool  triggerbotChargeAware2  = false;
    inline float triggerbotMinCharge2    = 30.0f;
    inline bool  triggerbotIgnoreInvisible2 = true;

    // ---- Triggerbot runtime state (not persisted) ----
    inline bool  triggerbotToggleActive  = false;  // current toggle state (mode=Toggle)
    inline bool  triggerbotToggleActive2 = false;
    inline DWORD triggerbotLastFireTick  = 0;      // GetTickCount of last primary fire
    inline DWORD triggerbotLastFireTick2 = 0;      // GetTickCount of last secondary fire

    // ---- Prediction ----
    inline bool projectile_arc  = false; // Ballistic arc correction for projectiles
    inline bool Prediction      = false;
    inline bool Prediction2     = false;
    inline bool Gravitypredit   = false;
    inline bool Gravitypredit2  = false;
    inline float predit_level   = 110.f;
    inline float predit_level2  = 110.f;
    inline bool hanzoautospeed  = false;

    // ---- Keys ----
    inline int AimKey      = 0x01;   // VK_LBUTTON
    inline int aim_key     = 1;      // Left Mouse
    inline int aim_key2    = 1;      // Left Mouse
    inline int togglekey   = 0;
    inline int MenuToggleKey = VK_HOME;
    inline uint64_t gafAsyncKeyStateOffset = 0; // manual win32kbase.sys-relative host key-state RVA; 0 = auto
    inline int gafAsyncKeyStateSize = 256;      // manual 256-byte VK array or 64-byte compact bitmap
    inline int gafAsyncKeyStateSessionId = 0;   // 0 = auto from interactive proxy process

    // ---- General aim ----
    inline float Fov        = 200.f;
    inline float Fov2       = 200.f;
    inline float minFov1    = 200.f;
    inline float minFov2    = 200.f;
    inline float Smooth     = 5.0f;
    inline bool  autoscalefov = false;
    inline float hitbox     = 0.13f;
    inline float hitbox2    = 0.13f;
    inline float missbox    = 0.6f;

    inline float Tracking_smooth  = 0.1f;
    inline float Tracking_smooth2 = 0.1f;
    inline float Flick_smooth     = 0.1f;
    inline float Flick_smooth2    = 0.1f;
    inline float accvalue   = 0.1f;
    inline float accvalue2  = 0.1f;
    inline float bladespeed = 0.1f;

    inline int  TargetBone  = kAimBoneHead; // aim-bone choice mirror: 0=chest, 1=head, 2=neck
    inline int  Bone        = kAimBoneHead;
    inline int  Bone2       = kAimBoneHead;
    inline bool autobone    = false;
    inline bool autobone2   = false;
    inline bool switch_team  = false;
    inline bool switch_team2 = false;
    inline std::string BoneName  = "Head";
    inline std::string BoneName2 = "Head";

    inline bool lockontarget    = false;
    inline bool trackcompensate = false;
    inline float comarea   = 0.01f;
    inline float comspeed  = 0.5f;

    inline bool aiaim       = false;
    inline bool targetdelay = false;
    inline int  targetdelaytime = 200;
    inline bool hitboxdelayshoot = false;
    inline int  hiboxdelaytime   = 200;

    inline bool dontshot   = false;
    inline int  shotcount  = 0;
    inline int  shotmanydont = 3;

    // ---- Aimbot UI options ----
    inline bool  aimbotAutoshot = false;
    inline bool  aimbotKeepFiring = true;
    inline float aimbotTriggerDelay = 0.0f; // triggerbot delay in ms (scaled)
    inline float aimbotMaxHead = 100.0f;
    inline int   aimMethod = 0; // 0=Linear, 1=PID, 2=Bezier
    inline int   aimbotSmoothType = 0; // 0=Constant Speed, 1=Linear, 2=Bezier
    inline float aimPidP = 0.5f;
    inline float aimPidI = 0.01f;
    inline float aimPidD = 0.1f;
    inline float aimPidMaxIntegral = 10.0f;
    inline float aimPidDeadzone = 1.0f;
    inline int   aimBezierControlPoints = 2;
    inline float aimBezierCurvature = 0.5f;
    inline float aimBezierSpeed = 50.0f;
    inline float aimbotStickiness = 100.0f;
    inline float aimbotSmoothY = 50.0f;
    inline float aimbotMaxAim = 100.0f;
    inline float aimbotMinCharge = 5.0f;
    inline float aimbotMaxCharge = 100.0f;
    inline bool  aimbotIgnoreInvisible = true;
    inline int   aimbotTrace = 0; // 0=Strict, 1=Relaxed, 2=Off
    inline int   aimbotUnlock = 0; // 0=Anytime, 1=On Release, 2=Never
    inline float aimbotLockTime = 20.0f;
    inline float aimbotMaxDist = 100.0f;
    inline float aimbotMinDist = 0.0f;
    inline int   aimbotAttack = 0; // action profile: primary/secondary/scoped/unscoped/ability/ultimate
    inline int   aimbotTeam = 0; // 0=Enemies, 1=Allies, 2=All
    inline int   aimbotPriority = 0; // 0=FOV, 1=HP, 2=Distance

    // ---- Hero-specific ----
    inline bool GenjiBlade       = false;
    inline bool AutoShiftGenji   = false;
    inline bool widowautounscope = false;

    // ---- Shoot / fire ----
    inline bool  AutoShoot  = false;
    inline int   Shoottime  = 500;
    inline bool  shooted    = false;
    inline bool  shooted2   = false;
    inline int   lasttime   = 0;
    inline float lasthealth = 0.f;
    inline bool  skilled    = false;
    inline int   slasttime  = 0;
    inline bool  sskilled   = false;
    inline bool  reloading  = false;

    // ---- Blade / Genji ----
    inline int  Qstarttime  = 0;
    inline int  Qtime       = 0;
    inline int  lastenemy   = -1;

    inline bool doingdelay      = false;
    inline int  timebeforedelay = 0;

    // ---- Misc auto ----
    inline bool AutoMelee      = false;
    inline float meleehealth   = 30.f;
    inline float meleedistance = 5.f;
    inline bool AutoRMB        = false;
    inline float AutoRMBhealth   = 100.f;
    inline float AutoRMBdistance = 30.f;
    inline bool AutoSkill      = false;
    inline float SkillHealth   = 50.f;
    inline bool AntiAFK        = false;

    // ---- Secondary aim ----
    inline bool secondaim   = false;
    inline bool highPriority = false;

    // ---- ESP toggles ----
    inline bool draw_info       = true;
    inline bool drawbattletag   = false;
    inline bool drawhealth      = true;
    inline bool healthbar       = false;
    inline bool healthbar2      = false;
    inline float healthbartextsize = 16.f;
    inline bool dist            = true;
    inline float visualMaxDist  = 100.f;
    inline bool name            = false;
    inline bool ult             = true;
    inline bool draw_skel       = true;
    inline bool skillinfo       = false;
    // Ultimate / Skill display position
    // 0 = Above head (per-entity in PlayerInfo), 1 = Left side panel, 2 = Right side panel
    inline int ultimateDisplayMode = 0;
    inline int skillDisplayMode = 0;
    // Radar corner: 0=BottomRight, 1=BottomLeft, 2=TopRight, 3=TopLeft
    inline int radarCorner = 0;
    inline bool radar           = false;
    inline bool radarline       = false;
    inline bool drawline        = false;
    inline bool draw_fov        = false;
    inline bool draw_hp_pack    = false;
    inline bool crosscircle     = false;
    inline bool eyeray          = false;

    // Legacy compile shims for untouched Overwatch.hpp references. Not persisted.
    extern bool draw_edge;
    extern bool drawbox3d;
    extern bool manualsave;

    // ---- Outline colours (float4, 0-1 range) ----
    inline ImVec4 enargb        = ImVec4(1.f, 0.f, 0.f, 0.4f);
    inline ImVec4 invisnenargb  = ImVec4(1.f, 0.f, 0.f, 0.4f);
    inline ImVec4 targetargb    = ImVec4(0.f, 1.f, 0.f, 0.8f);
    inline ImVec4 allyargb      = ImVec4(0.f, 0.f, 1.f, 0.4f);

    // ---- Box/fov colours ----
    inline ImVec4 EnemyCol   = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 fovcol     = ImVec4(1.f, 1.f, 1.f, 1.f);

    // ---- Targeting state ----
    inline int  Targetenemyi    = -1;
    inline int  Targetenemyifov = -1;
    inline float health         = 0.f;

    // ---- Game state ----
    inline int  doingentity  = 1;
    inline int  lastheroid   = -2;
    inline bool Menu         = true;
    inline std::string nowhero = "Unknown";

    // ---- KMBox input output ----
    inline bool kmboxEnabled = false;
    inline int inputSource = 1; // 0=Auto (KMBox>DMA>Local), 1=KMBox, 2=Local, 3=DMA
    inline int  kmboxDeviceType = 0; // 0=Network/UDP, 1=Serial/COM
    inline char kmboxIp[32] = "192.168.2.188";
    inline int  kmboxPort = 8808;
    inline char kmboxMac[32] = "12525C53";
    inline char kmboxComPort[16] = "COM3";
    inline float kmboxAimSensitivity = 100.0f;
    inline float gameMouseSensitivity = 15.0f; // DMA-read, updated each tick
    inline float sensReference = 15.0f;        // game sens used when kmboxAimSensitivity was calibrated
    inline bool  autoSyncSensitivity = false;
    inline float hostMouseDpi = 1600.0f;       // manual/effective host mouse DPI fallback
    inline float detectedHostMouseDpi = 0.0f;  // runtime-only automatic detection result
    inline bool  hostMouseDpiAutoDetected = false;
    inline int   kmboxInputDelayMs = 0;
    inline bool kmboxDebugLog = false;

    // ---- Diagnostics / Dry-run ----
    inline bool aimDryRun = false;              // Dry-run mode: log everything, don't move cursor
    inline bool aimVerboseLog = false;          // Extra verbose per-tick logging
    inline int aimDryRunLogIntervalMs = 100;    // Min interval between dry-run log lines (avoid spam)

    // ---- Sensitivity auto-calibration ----
    inline bool calibrationInProgress = false;
    inline float calibratedPixelsPerRadian = 0.0f;  // 0 = not calibrated, use manual kmboxAimSensitivity
    inline float calibratedPixelsPerRadianPitch = 0.0f; // Separate pitch calibration if >5% different from yaw
    inline int calibrationMovePixels = 200;          // How many pixels to move for calibration
    inline int calibrationStabilityWaitMs = 50;      // Wait time before/after move for stable read
    inline int calibrationSampleCount = 3;           // Number of calibration samples to average

    // ---- Mouse movement splitting ----
    inline bool moveSplitEnabled = true;       // Enable micro-splitting of mouse moves
    inline int moveSplitMaxPixels = 4;         // Max pixels per micro-move chunk (1-20)
    inline int moveSplitDelayUs = 800;         // Microsecond delay between chunks (100-5000)

    // ---- Per-hero aim/trigger presets ----
    inline constexpr int kMaxHeroPresetSlots = 12;

    struct TriggerPreset {
        bool enabled = false;
        int action = 0;          // 0=Primary, 1=Secondary, 2=Scoped, 3=Unscoped, 4-6=Abilities, 7=Ultimate
        int mode = 0;            // 0=Hold, 1=Toggle, 2=Always
        int key = 1;             // key index (reuses activation key VK list; None maps to VK 0)
        float shotInterval = 0.0f;
        bool chargeAware = false;
        float minCharge = 30.0f;
        bool ignoreInvisible = true;
        bool drawHitbox = false;
    };

    struct HeroPreset {
        float fov = 200.f;       // FOV circle size
        float smooth = 5.0f;     // aim smoothing, 0-100
        int bone = kAimBoneHead;  // aim-bone choice: 0=chest, 1=head, 2=neck
        bool autoBone = false;    // true = choose closest visible skeleton bone at runtime
        float hitbox = 0.13f;    // hitbox radius/size
        int aimMode = 0;         // 0=Tracking, 1=Flick
        int aimMethod = 0;       // 0=Linear, 1=PID, 2=Bezier
        int smoothType = 0;      // 0=Constant Speed, 1=Linear, 2=Bezier
        float pidP = 0.5f;
        float pidI = 0.01f;
        float pidD = 0.1f;
        float pidMaxIntegral = 10.0f;
        float pidDeadzone = 1.0f;
        int bezierControlPoints = 2;
        float bezierCurvature = 0.5f;
        float bezierSpeed = 50.0f;
        int key = 1;             // aim activation key index (reuses activation key VK list)
        bool autoshot = false;
        bool keepFiring = true;
        bool prediction = false; // movement prediction
        float maxHeadDistance = 100.0f;
        float stickiness = 100.0f;
        int priority = 0;        // 0=FOV, 1=HP, 2=Distance
        int targetTeam = 0;      // 0=Enemies, 1=Allies, 2=All
        float maxAimTime = 100.0f;
        float minCharge = 5.0f;
        float maxCharge = 100.0f;
        bool ignoreInvisible = true;
        int traceCondition = 0;   // 0=Strict, 1=Relaxed, 2=Off
        int unlockCondition = 0;  // 0=Anytime, 1=On Release, 2=Never
        float lockTime = 20.0f;
        float maxDistance = 100.0f;
        float minDistance = 0.0f;
        TriggerPreset trigger{};
    };

    struct HeroSlotPreset {
        std::string name = "Preset";
        bool present = false;
        bool enabled = false;
        HeroPreset preset{};
    };

    enum class HeroSkillInputChannel : int {
        Primary = 0,
        Secondary = 1
    };

    inline constexpr int kMaxHeroSkillSequenceSteps = 64;

    struct HeroSkillSequenceStep {
        int buttonMask = 1;   // bit0=left, bit1=right, bit2=middle
        int durationMs = 0;
        float speedScale = 1.0f;
        int jitterMs = 0;
    };

    struct HeroSkillTrackingParams {
        int method = 0;
        float smooth = 0.0f;
        float fov = 0.0f;
        int bone = kAimBoneChest;
        float hitbox = 0.0f;
    };

    struct HeroSkillSettings {
        bool enabled = false;
        int key = 0;
        float healthThreshold = 50.0f;
        float enemyHealthThreshold = 50.0f;
        float allyHealthThreshold = 50.0f;
        float distance = 30.0f;
        int mode = 0;
        float cooldown = 0.0f;
        bool cooldownGuard = true;
        bool prediction = false;
        int minTargets = 1;
        float radius = 0.0f;
        std::vector<HeroSkillSequenceStep> sequenceSteps{};
        HeroSkillTrackingParams tracking{};
        int pitchDownDurationMs = 45;
        float pitchDownDurationJitter = 10.0f;
        float pitchDownTargetAngle = 90.0f;
        float pitchUpOffsetJitter = 1.5f;
        int fireDelayMs = 50;
        int jumpKeyCode = VK_SPACE;
        bool ammoGuard = false;
        int ammoGuardReserve = 1;
    };

    using HeroSkillPresetStore = std::unordered_map<uint64_t, std::unordered_map<std::string, HeroSkillSettings>>;

    inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroAimPresets;
    inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>> heroTriggerPresets;
    inline HeroSkillPresetStore heroSkillPresets;
    inline int targetPriority = 0;

    // UI-only placeholders for heroes not present in the current local eHero enum.
    inline constexpr uint64_t HERO_PRESET_FREJA  = 0xFFFFFFFFFFFF0001ull;
    inline constexpr uint64_t HERO_PRESET_HAZARD = 0xFFFFFFFFFFFF0002ull;
    inline constexpr uint64_t HERO_PRESET_JUNO   = 0xFFFFFFFFFFFF0003ull;

    // ---- Target-machine display fallback ----
    // Used only when the target viewport cannot be read through DMA.
    inline int manualScreenWidth = 1920;
    inline int manualScreenHeight = 1080;

    // ---- Crosshair tracking circle ----
    inline int locx = 0, locy = 0, therad = 0, pon = 0, crss = 0;

    // ---- Persistence ----
    HeroPreset MakeHeroPresetFromCurrent();
    HeroPreset MakeHeroAimPresetFromCurrent();
    HeroPreset MakeHeroTriggerPresetFromCurrent();
    bool TryGetHeroPreset(uint64_t heroId, HeroPreset& outPreset);
    bool TryGetHeroAimPreset(uint64_t heroId, HeroPreset& outPreset);
    bool TryGetHeroTriggerPreset(uint64_t heroId, HeroPreset& outPreset);
    bool HasHeroPreset(uint64_t heroId);
    bool HasHeroAimPreset(uint64_t heroId);
    bool HasHeroTriggerPreset(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId, int slotIndex);
    HeroPreset GetHeroAimPresetOrDefault(uint64_t heroId, int slotIndex);
    HeroPreset GetHeroTriggerPresetOrDefault(uint64_t heroId, int slotIndex);
    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset);
    void SetHeroPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    void SetHeroAimPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    void SetHeroTriggerPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    std::string GetHeroSlotName(uint64_t heroId, int slotIndex);
    std::string GetHeroAimSlotName(uint64_t heroId, int slotIndex);
    std::string GetHeroTriggerSlotName(uint64_t heroId, int slotIndex);
    bool TryGetHeroAimSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot);
    bool TryGetHeroTriggerSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot);
    bool IsHeroSlotEnabled(uint64_t heroId, int slotIndex);
    bool IsHeroAimSlotEnabled(uint64_t heroId, int slotIndex);
    bool IsHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex);
    void SetHeroSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    void SetHeroAimSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    void SetHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex, bool enabled);
    int GetHeroAimSlotCount(uint64_t heroId);
    int GetHeroTriggerSlotCount(uint64_t heroId);
    int AddHeroAimSlot(uint64_t heroId, const HeroPreset& seedPreset);
    int AddHeroTriggerSlot(uint64_t heroId, const HeroPreset& seedPreset);
    bool DeleteHeroAimSlot(uint64_t heroId, int slotIndex);
    bool DeleteHeroTriggerSlot(uint64_t heroId, int slotIndex);
    void NormalizeHeroPresets();
    void ApplyHeroPresetToGlobals(const HeroPreset& preset);
    void ApplyHeroAimPresetToGlobals(const HeroPreset& preset);
    void ApplyHeroTriggerPresetToGlobals(const HeroPreset& preset);
    void SaveHeroConfig(const std::string& path);
    void SaveHeroConfigForHero(const std::string& path, uint64_t heroId);
    void LoadHeroConfig(const std::string& path);
    void LoadHeroSkillConfig(const std::string& path);
    void SaveHeroSkillConfig(const std::string& path);
    bool TryGetHeroSkillSettings(uint64_t heroId, const std::string& skillId, HeroSkillSettings& outSettings);
    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId, const std::string& skillId);
    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId,
                                           const std::string& skillId,
                                           const HeroSkillSettings& defaultSettings);
    void SetHeroSkillSettings(uint64_t heroId, const std::string& skillId, const HeroSkillSettings& settings);
    void SaveHeroPresets(const std::string& path);
    void LoadHeroPresets(const std::string& path);
    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void SaveConfig(const std::string& path);
    void LoadConfig(const std::string& path);

}} // namespace OW::Config
