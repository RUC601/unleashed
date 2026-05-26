#pragma once

#include <Windows.h>
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
    inline bool hanzo_flick   = false;
    inline bool silent        = false;
    inline bool Rage          = false;
    inline bool fakesilent    = false;

    // ---- Prediction ----
    inline bool Prediction      = false;
    inline bool Prediction2     = false;
    inline bool Gravitypredit   = false;
    inline bool Gravitypredit2  = false;
    inline float predit_level   = 110.f;
    inline float predit_level2  = 110.f;
    inline bool hanzoautospeed  = false;

    // ---- Keys ----
    inline int AimKey      = 0x01;   // VK_LBUTTON
    inline int aim_key     = 6;      // default 'F'
    inline int aim_key2    = 6;
    inline int togglekey   = 0;
    inline int MenuToggleKey = VK_HOME;

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

    // ---- Recoil ----
    inline bool norecoil    = false;
    inline float recoilnum  = 0.5f;
    inline bool horizonreco = false;

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

    inline bool enablechangefov = false;
    inline float CHANGEFOV    = 103.f;
    inline bool trackback     = false;

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
    inline bool outline         = false;
    inline bool externaloutline = false;
    inline bool teamoutline     = false;
    inline bool healthoutline   = false;
    inline bool rainbowoutline  = false;
    inline bool draw_edge       = false;
    inline bool drawbox3d       = false;
    inline bool radar           = false;
    inline bool radarline       = false;
    inline bool drawline        = false;
    inline bool draw_fov        = false;
    inline bool draw_hp_pack    = false;
    inline bool crosscircle     = false;
    inline bool eyeray          = false;
    inline bool testvalue       = false;

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

    // ---- Computed outline colors (DWORD) ----
    inline DWORD visenemy     = 0;
    inline DWORD invisenemy   = 0;
    inline DWORD targetenemy  = 0;
    inline DWORD targetenemy2 = 0;
    inline DWORD Allycolor    = 0;

    // ---- Rainbow ----
    inline int cps1 = 0, cps2 = 0, cps3 = 0;
    inline ImVec4 rainbowargb = ImVec4(0.f, 0.f, 0.f, 1.f);

    // ---- Targeting state ----
    inline int  Targetenemyi    = -1;
    inline int  Targetenemyifov = -1;
    inline float health         = 0.f;

    // ---- Game state ----
    inline int  doingentity  = 1;
    inline int  lastheroid   = -2;
    inline bool Menu         = true;
    inline bool manualsave   = false;
    inline bool loginornot   = false;
    inline std::string nowhero = "Unknown";

    // ---- Namespoofer ----
    inline bool namespoofer   = false;
    inline char fakename[64]  = "";

    // ---- KMBox input output ----
    inline bool kmboxEnabled = false;
    inline int  kmboxDeviceType = 0; // 0=Network/UDP, 1=Serial/COM
    inline char kmboxIp[32] = "192.168.2.188";
    inline int  kmboxPort = 8808;
    inline char kmboxMac[32] = "12525C53";
    inline char kmboxComPort[16] = "COM3";
    inline float kmboxAimSensitivity = 1.0f;
    inline int   kmboxInputDelayMs = 0;
    inline bool kmboxDebugLog = false;

    // ---- Per-hero aimbot presets ----
    struct HeroPreset {
        float fov = 200.f;       // FOV circle size
        float smooth = 5.0f;     // aim smoothing, 0-100
        int bone = 0;            // 0=head, 1=neck, 2=chest
        float hitbox = 0.13f;    // hitbox radius/size
        int aimMode = 0;         // 0=Tracking, 1=Flick, 2=HanzoFlick, 3=Silent
        bool prediction = false; // movement prediction
        int priority = 0;        // 0=FOV, 1=HP, 2=Distance
    };

    inline std::unordered_map<uint64_t, HeroPreset> heroPresets;
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
    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset);
    void ApplyHeroPresetToGlobals(const HeroPreset& preset);
    void SaveHeroPresets(const std::string& path);
    void LoadHeroPresets(const std::string& path);
    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void SaveConfig(const std::string& path);
    void LoadConfig(const std::string& path);

}} // namespace OW::Config
