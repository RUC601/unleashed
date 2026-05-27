#pragma once

#include <Windows.h>
#include <array>
#include <cstdint>
#include <string>
#include <mutex>
#include <unordered_map>
#include <imgui.h>

// -----------------------------------------------------------------------
// OW::Config -- Single source of truth for all cheat configuration.
// All variables are inline (C++17) globals.
// -----------------------------------------------------------------------

namespace OW { namespace Config {

    inline std::string configFileName = "config.ini";
    inline std::string lastConfigProfile = "config.ini";
    std::string ConfigPath();

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
    inline uint64_t gafAsyncKeyStateOffset = 0; // win32kbase.sys-relative host key-state offset
    inline int gafAsyncKeyStateSize = 256;      // 256-byte VK array or 64-byte compact bitmap

    // ---- General aim ----
    inline float Fov        = 200.f;
    inline float Fov2       = 200.f;
    inline float minFov1    = 200.f;
    inline float minFov2    = 200.f;
    inline float Smooth     = 5.0f;
    inline bool  fov360     = false;
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

    inline int  TargetBone  = 0;   // 0=head, 1=neck, 2=chest
    inline int  Bone        = 1;
    inline int  Bone2       = 1;
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
    inline bool  aimbotIgnoreInvisible = false;
    inline int   aimbotTrace = 0; // 0=Strict, 1=Relaxed, 2=Off
    inline int   aimbotUnlock = 0; // 0=Anytime, 1=On Release, 2=Never
    inline float aimbotLockTime = 20.0f;
    inline float aimbotMaxDist = 100.0f;
    inline float aimbotMinDist = 0.0f;
    inline int   aimbotAttack = 0; // 0=Shoot, 1=Ability1, 2=Ability2
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
    inline bool healthbar       = true;
    inline bool healthbar2      = false;
    inline float healthbartextsize = 16.f;
    inline bool dist            = true;
    inline float visualMaxDist  = 100.f;
    inline bool name            = true;
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
    inline ImVec4 targetargb2   = ImVec4(0.f, 1.f, 0.f, 0.8f);
    inline ImVec4 allyargb      = ImVec4(0.f, 0.f, 1.f, 0.4f);

    // ---- Box/fov colours ----
    inline ImVec4 EnemyCol   = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 fovcol     = ImVec4(1.f, 1.f, 1.f, 1.f);
    inline ImVec4 fovcol2    = ImVec4(1.f, 1.f, 1.f, 1.f);

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
    inline int  kmboxDeviceType = 0; // 0=Network/UDP, 1=Serial/COM
    inline char kmboxIp[32] = "192.168.2.188";
    inline int  kmboxPort = 8808;
    inline char kmboxMac[32] = "12525C53";
    inline char kmboxComPort[16] = "COM3";
    inline float kmboxAimSensitivity = 100.0f;
    inline float gameMouseSensitivity = 15.0f; // DMA-read, updated each tick
    inline float sensReference = 15.0f;        // game sens used when kmboxAimSensitivity was calibrated
    inline bool  autoSyncSensitivity = false;
    inline int   kmboxInputDelayMs = 0;
    inline bool kmboxDebugLog = false;

    // ---- Per-hero aimbot presets ----
    struct HeroPreset {
        float fov = 200.f;       // FOV circle size
        float smooth = 5.0f;     // aim smoothing, 0-100
        int bone = 0;            // 0=head, 1=neck, 2=chest
        float hitbox = 0.13f;    // hitbox radius/size
        int aimMode = 0;         // 0=Tracking, 1=Flick
        bool prediction = false; // movement prediction
        int priority = 0;        // 0=FOV, 1=HP, 2=Distance
    };

    struct HeroSlotPreset {
        std::string name = "Preset";
        HeroPreset preset{};
    };

    inline std::unordered_map<uint64_t, std::array<HeroSlotPreset, 7>> heroPresets;
    inline int targetPriority = 0;

    // UI-only placeholders for heroes not present in the current local eHero enum.
    inline constexpr uint64_t HERO_PRESET_FREJA  = 0xFFFFFFFFFFFF0001ull;
    inline constexpr uint64_t HERO_PRESET_HAZARD = 0xFFFFFFFFFFFF0002ull;
    inline constexpr uint64_t HERO_PRESET_JUNO   = 0xFFFFFFFFFFFF0003ull;

    // ---- Primary-machine display ----
    inline int manualScreenWidth = 1920;
    inline int manualScreenHeight = 1080;

    // ---- Crosshair tracking circle ----
    inline int locx = 0, locy = 0, therad = 0, pon = 0, crss = 0;

    // ---- Persistence ----
    HeroPreset MakeHeroPresetFromCurrent();
    bool TryGetHeroPreset(uint64_t heroId, HeroPreset& outPreset);
    bool HasHeroPreset(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId);
    HeroPreset GetHeroPresetOrDefault(uint64_t heroId, int slotIndex);
    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset);
    void SetHeroPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset);
    std::string GetHeroSlotName(uint64_t heroId, int slotIndex);
    void ApplyHeroPresetToGlobals(const HeroPreset& preset);
    void SaveHeroPresets(const std::string& path);
    void LoadHeroPresets(const std::string& path);
    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void SaveConfig(const std::string& path);
    void LoadConfig(const std::string& path);

}} // namespace OW::Config
