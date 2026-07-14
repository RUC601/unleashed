#ifndef _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#endif

#include "Utils/Config.hpp"

#include "Game/HeroSkills.hpp"
#include "Game/Structs.hpp"
#include "Game/Target.hpp"
#include "Game/WeaponSpec.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// =====================================================================
// OW::Config -- robust INI persistence
//
// Compatibility notes:
// - Existing section/key names are preserved.
// - Existing float values are still written as fixed-point integers
//   multiplied by 10000 because older config.ini files used that format.
// - Missing or invalid values fall back to documented defaults and are
//   reported through Diagnostics/console logging.
// =====================================================================

namespace OW { namespace Config {

    int config_version = 12;
    bool draw_edge = false;
    bool drawbox3d = false;
    bool manualsave = false;

    namespace {

        bool PathIsRegularFile(const std::string& path)
        {
            const DWORD attributes = GetFileAttributesA(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        bool PathIsDirectory(const std::string& path)
        {
            const DWORD attributes = GetFileAttributesA(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        std::string JoinPathRaw(const std::string& directory, const std::string& child)
        {
            if (directory.empty())
                return child;

            const char tail = directory.back();
            if (tail == '\\' || tail == '/')
                return directory + child;

            return directory + "\\" + child;
        }

        std::string ExecutableDirectoryPath()
        {
            std::vector<char> buffer(MAX_PATH);
            DWORD length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            while (length == buffer.size()) {
                buffer.resize(buffer.size() * 2);
                length = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            }

            if (length == 0)
                return ".";

            std::string path(buffer.data(), length);
            const size_t slash = path.find_last_of("\\/");
            return slash == std::string::npos ? "." : path.substr(0, slash);
        }

        std::string CurrentDirectoryPathRaw()
        {
            std::vector<char> buffer(MAX_PATH);
            DWORD length = GetCurrentDirectoryA(static_cast<DWORD>(buffer.size()), buffer.data());
            while (length >= buffer.size()) {
                buffer.resize(static_cast<size_t>(length) + 1);
                length = GetCurrentDirectoryA(static_cast<DWORD>(buffer.size()), buffer.data());
            }

            if (length == 0)
                return ".";

            return std::string(buffer.data(), length);
        }

        bool SamePathText(std::string lhs, std::string rhs)
        {
            std::replace(lhs.begin(), lhs.end(), '/', '\\');
            std::replace(rhs.begin(), rhs.end(), '/', '\\');
            std::transform(lhs.begin(), lhs.end(), lhs.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            std::transform(rhs.begin(), rhs.end(), rhs.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return lhs == rhs;
        }

        FILETIME LastWriteTimeOrZero(const std::string& path)
        {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &data))
                return FILETIME{};
            return data.ftLastWriteTime;
        }

        bool FileTimeGreater(FILETIME lhs, FILETIME rhs)
        {
            return CompareFileTime(&lhs, &rhs) > 0;
        }

        std::string HeroSidecarFileName(const std::string& profileFileName)
        {
            const size_t dot = profileFileName.find_last_of('.');
            const std::string stem = dot == std::string::npos
                ? profileFileName
                : profileFileName.substr(0, dot);
            return stem + ".heroes.json";
        }

        void AddUniquePath(std::vector<std::string>& paths, const std::string& path)
        {
            for (const std::string& existing : paths) {
                if (SamePathText(existing, path))
                    return;
            }
            paths.emplace_back(path);
        }

        void CopyNewestLegacyFile(const std::vector<std::string>& sourceDirectories,
                                  const std::string& targetDirectory,
                                  const std::string& fileName)
        {
            const std::string targetPath = JoinPathRaw(targetDirectory, fileName);
            if (PathIsRegularFile(targetPath))
                return;

            std::string newestSource;
            FILETIME newestTime{};
            for (const std::string& sourceDirectory : sourceDirectories) {
                const std::string sourcePath = JoinPathRaw(sourceDirectory, fileName);
                if (!PathIsRegularFile(sourcePath))
                    continue;

                const FILETIME writeTime = LastWriteTimeOrZero(sourcePath);
                if (newestSource.empty() || FileTimeGreater(writeTime, newestTime)) {
                    newestSource = sourcePath;
                    newestTime = writeTime;
                }
            }

            if (!newestSource.empty())
                CopyFileA(newestSource.c_str(), targetPath.c_str(), TRUE);
        }

        void MigrateLegacyConfigFiles(const std::string& targetDirectory)
        {
            static bool migrated = false;
            if (migrated)
                return;
            migrated = true;

            std::vector<std::string> sourceDirectories;
            AddUniquePath(sourceDirectories, ExecutableDirectoryPath());
            AddUniquePath(sourceDirectories, CurrentDirectoryPathRaw());

            std::vector<std::string> profileNames;
            for (const std::string& sourceDirectory : sourceDirectories) {
                WIN32_FIND_DATAA findData{};
                const std::string searchPath = JoinPathRaw(sourceDirectory, "*.ini");
                HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
                if (findHandle == INVALID_HANDLE_VALUE)
                    continue;

                do {
                    if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
                        const std::string profileName = findData.cFileName;
                        if (std::find(profileNames.begin(), profileNames.end(), profileName) == profileNames.end())
                            profileNames.emplace_back(profileName);
                    }
                } while (FindNextFileA(findHandle, &findData));
                FindClose(findHandle);
            }

            if (std::find(profileNames.begin(), profileNames.end(), "config.ini") == profileNames.end())
                profileNames.emplace_back("config.ini");

            for (const std::string& profileName : profileNames) {
                CopyNewestLegacyFile(sourceDirectories, targetDirectory, profileName);
                CopyNewestLegacyFile(sourceDirectories, targetDirectory, HeroSidecarFileName(profileName));
            }
        }

    } // anonymous namespace

    std::string ConfigDirectoryPath()
    {
        const std::string directory = JoinPathRaw(ExecutableDirectoryPath(), "config");
        if (!PathIsDirectory(directory))
            CreateDirectoryA(directory.c_str(), nullptr);
        MigrateLegacyConfigFiles(directory);
        return directory;
    }

    std::string ConfigPath()
    {
        return JoinPathRaw(ConfigDirectoryPath(), configFileName);
    }

    std::string HeroConfigPath(const std::string& configPath)
    {
        const size_t slash = configPath.find_last_of("\\/");
        const size_t dot = configPath.find_last_of('.');
        const bool hasExtension = dot != std::string::npos &&
            (slash == std::string::npos || dot > slash);
        const std::string stem = hasExtension
            ? configPath.substr(0, dot)
            : configPath;
        return stem + ".heroes.json";
    }

    std::string HeroConfigPath()
    {
        return HeroConfigPath(ConfigPath());
    }

    int NormalizeAimBone(int aimBone)
    {
        switch (aimBone) {
        case kAimBoneChest:
        case kAimBoneHead:
        case kAimBoneNeck:
            return aimBone;
        default:
            return kAimBoneHead;
        }
    }

    const char* AimBoneName(int aimBone)
    {
        if (aimBone == kAimBoneClosest)
            return "Closest";

        switch (NormalizeAimBone(aimBone)) {
        case kAimBoneChest: return "Chest";
        case kAimBoneNeck:  return "Neck";
        case kAimBoneHead:
        default:           return "Head";
        }
    }

    namespace {

        constexpr int kCurrentConfigVersion = 12;
        constexpr int kPresetBonesStoredAsAimBonesVersion = 3;
        constexpr int kFovAnglesStoredAsHalfAnglesVersion = 5;
        constexpr int kAimBehaviorSemanticsVersion = 6;
        constexpr int kVisualOverlayCleanupVersion = 8;
        constexpr int kCurrentHeroConfigVersion = 14;
        constexpr int kHeroFovAnglesStoredAsHalfAnglesVersion = 2;
        constexpr int kHeroAimBehaviorSemanticsVersion = 3;
        constexpr int kRoadhogChainHookFullHealthDefaultVersion = 5;
        constexpr int kBadAnaAbilityKeyLayoutVersion = 7;
        constexpr int kAnaSleepDartFullHealthDefaultVersion = 8;
        constexpr int kDocumentedHeroSlotDefaultsVersion = 9;
        constexpr int kAsheFirePatternStableTrackingVersion = 13;
        constexpr int kAimScopeRequirementVersion = 14;
        constexpr const char* kMetaSection = "Meta";
        constexpr const char* kVersionKey = "config_version";
        constexpr const char* kAimbotSection = "Aimbot";
        constexpr const char* kAimMethodSection = "AimMethod";
        constexpr int kMaxAimCustomPresets = 64;
        constexpr const char* kDefaultKmboxIp = "192.168.2.188";
        constexpr int kDefaultKmboxPort = 8808;
        constexpr int kDefaultKmboxMonitorPort = kDefaultKmboxPort + 1;
        constexpr const char* kDefaultKmboxMac = "12525C53";
        constexpr const char* kDefaultKmboxComPort = "COM3";
        constexpr int kDefaultKmboxInputDelayMs = 0;
        constexpr float kDefaultHostMouseDpi = 1600.0f;

        using SectionValues = std::unordered_map<std::string, std::string>;

        void LogConfig(Diagnostics::LogLevel level, const char* fmt, ...);

        void LogKmboxMonitorPortAdjustmentUnlocked(
            const char* reason,
            const int oldMonitorPort,
            const int newMonitorPort,
            Diagnostics::LogLevel level,
            const char* message)
        {
            LogConfig(level,
                "%s oldMonitorPort=%d newMonitorPort=%d commandPort=%d manualOverride=%s.",
                message ? message : "Adjusted kmboxMonitorPort.",
                oldMonitorPort,
                newMonitorPort,
                kmboxPort,
                kmboxMonitorPortManualOverride ? "true" : "false");
            Diagnostics::Aim("config.kmbox_monitor_port_adjusted reason=%s manualOverride=%d commandPort=%d oldMonitorPort=%d newMonitorPort=%d recommendedMonitorPort=%d",
                reason ? reason : "unknown",
                kmboxMonitorPortManualOverride ? 1 : 0,
                kmboxPort,
                oldMonitorPort,
                newMonitorPort,
                RecommendedKmboxMonitorPort(kmboxPort));
        }

        void SetKmboxMonitorPortUnlocked(
            int newMonitorPort,
            const char* reason,
            Diagnostics::LogLevel level,
            const char* message)
        {
            if (kmboxMonitorPort == newMonitorPort)
                return;

            const int oldMonitorPort = kmboxMonitorPort;
            kmboxMonitorPort = newMonitorPort;
            LogKmboxMonitorPortAdjustmentUnlocked(reason, oldMonitorPort, newMonitorPort, level, message);
        }

        void NormalizeKmboxPortsUnlocked(const char* reason)
        {
            const int recommendedMonitorPort = RecommendedKmboxMonitorPort(kmboxPort);

            if (!kmboxMonitorPortManualOverride) {
                SetKmboxMonitorPortUnlocked(
                    recommendedMonitorPort,
                    reason,
                    Diagnostics::LogLevel::Info,
                    "Auto-synced kmboxMonitorPort to the recommended KMBox monitor port.");
                return;
            }

            if (!IsValidKmboxUdpPort(kmboxMonitorPort)) {
                SetKmboxMonitorPortUnlocked(
                    recommendedMonitorPort,
                    reason,
                    Diagnostics::LogLevel::Warn,
                    "kmboxMonitorPort was invalid; using the recommended KMBox monitor port.");
                return;
            }

            if (kmboxMonitorPort == kmboxPort) {
                SetKmboxMonitorPortUnlocked(
                    recommendedMonitorPort,
                    reason,
                    Diagnostics::LogLevel::Warn,
                    "kmboxMonitorPort matched kmboxPort; using the recommended separate KMBox monitor port.");
            }
        }

        int MigrateLegacyAimBehavior(int value)
        {
            switch (value) {
            case 0: return kAimBehaviorTracking;
            case 1:
            case 2:
            case 3: return kAimBehaviorFlick;
            case 4: return kAimBehaviorReacquire;
            default: return kAimBehaviorTracking;
            }
        }

        int NormalizeAimBehaviorForLoad(int value, bool legacySemantics)
        {
            return ClampAimBehaviorIndex(legacySemantics
                ? MigrateLegacyAimBehavior(value)
                : value);
        }

        std::string NormalizeAimPresetName(std::string value, const char* fallback, int fallbackIndex)
        {
            size_t first = 0;
            while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first])))
                ++first;
            size_t last = value.size();
            while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])))
                --last;
            value = value.substr(first, last - first);
            if (value.empty()) {
                value = fallback ? fallback : "Preset";
                if (fallbackIndex >= 0) {
                    value += " ";
                    value += std::to_string(fallbackIndex + 1);
                }
            }
            if (value.size() > 63)
                value.resize(63);
            return value;
        }

        AimMethodPreset ValidateAimMethodPresetValue(AimMethodPreset preset, int fallbackIndex = -1)
        {
            preset.name = NormalizeAimPresetName(preset.name, "Method Preset", fallbackIndex);
            preset.method = ClampAimMethodIndex(preset.method);
            preset.angularSpeedScale = ClampAimMethodAngularSpeedScalePercent(preset.angularSpeedScale);
            preset.pidP = std::isfinite(preset.pidP) ? std::clamp(preset.pidP, 0.0f, 2.0f) : 0.5f;
            preset.pidI = std::isfinite(preset.pidI) ? std::clamp(preset.pidI, 0.0f, 0.5f) : 0.01f;
            preset.pidD = std::isfinite(preset.pidD) ? std::clamp(preset.pidD, 0.0f, 1.0f) : 0.1f;
            preset.pidMaxIntegral = std::isfinite(preset.pidMaxIntegral)
                ? std::clamp(preset.pidMaxIntegral, 1.0f, 50.0f)
                : 10.0f;
            preset.pidDeadzone = std::isfinite(preset.pidDeadzone)
                ? std::clamp(preset.pidDeadzone, 0.0f, 10.0f)
                : 1.0f;
            preset.bezierControlPoints = std::clamp(preset.bezierControlPoints, 2, 6);
            preset.bezierCurvature = std::isfinite(preset.bezierCurvature)
                ? std::clamp(preset.bezierCurvature, 0.0f, 1.0f)
                : 0.5f;
            preset.bezierSpeed = std::isfinite(preset.bezierSpeed)
                ? std::clamp(preset.bezierSpeed, 1.0f, 200.0f)
                : 50.0f;
            preset.piecewiseNearDegrees = std::isfinite(preset.piecewiseNearDegrees)
                ? std::clamp(preset.piecewiseNearDegrees, 0.0f, 30.0f)
                : 2.0f;
            preset.piecewiseMidDegrees = std::isfinite(preset.piecewiseMidDegrees)
                ? std::clamp(preset.piecewiseMidDegrees, preset.piecewiseNearDegrees, 45.0f)
                : 6.0f;
            preset.piecewiseFarDegrees = std::isfinite(preset.piecewiseFarDegrees)
                ? std::clamp(preset.piecewiseFarDegrees, preset.piecewiseMidDegrees, 60.0f)
                : 12.0f;
            preset.piecewiseNearScale = std::isfinite(preset.piecewiseNearScale)
                ? std::clamp(preset.piecewiseNearScale, 0.0f, 1.0f)
                : 0.20f;
            preset.piecewiseMidScale = std::isfinite(preset.piecewiseMidScale)
                ? std::clamp(preset.piecewiseMidScale, 0.0f, 1.0f)
                : 0.45f;
            preset.piecewiseFarScale = std::isfinite(preset.piecewiseFarScale)
                ? std::clamp(preset.piecewiseFarScale, 0.0f, 1.0f)
                : 0.75f;
            preset.accelLimitedAcceleration = std::isfinite(preset.accelLimitedAcceleration)
                ? std::clamp(preset.accelLimitedAcceleration, 0.0f, 20.0f)
                : 0.1f;
            preset.constantAngularSpeedDeg = ClampAimConstantAngularSpeedDeg(preset.constantAngularSpeedDeg);
            return preset;
        }

        AimBehaviorPreset ValidateAimBehaviorPresetValue(AimBehaviorPreset preset, int fallbackIndex = -1)
        {
            preset.name = NormalizeAimPresetName(preset.name, "Behavior Preset", fallbackIndex);
            preset.behavior = ClampAimBehaviorIndex(preset.behavior);
            preset.methodPresetId = ClampAimMethodPresetId(preset.methodPresetId);
            if (const AimMethodPreset* methodPreset = FindAimMethodPreset(preset.methodPresetId))
                preset.method = ClampAimMethodIndex(methodPreset->method);
            else
                preset.method = ClampAimMethodIndex(preset.method);

            preset.baseSpeed = std::isfinite(preset.baseSpeed)
                ? std::clamp(preset.baseSpeed, 0.0f, 100.0f)
                : 100.0f;
            preset.startLimiter = ValidateAimStartLimiterProfile(preset.startLimiter);
            preset.moveSplitMaxPixels = ClampMoveSplitMaxPixels(preset.moveSplitMaxPixels);
            preset.moveSplitDelayUs = ClampMoveSplitDelayUs(preset.moveSplitDelayUs);
            return preset;
        }

        DynamicFovPreset ValidateDynamicFovPresetForConfig(DynamicFovPreset preset, int fallbackIndex = -1)
        {
            preset.name = NormalizeAimPresetName(preset.name, "Dynamic FOV", fallbackIndex);
            return ValidateDynamicFovPresetValue(preset, fallbackIndex);
        }

        int NextCustomPresetId(const std::vector<AimMethodPreset>& presets)
        {
            int nextId = 1;
            for (const AimMethodPreset& preset : presets)
                nextId = (std::max)(nextId, preset.id + 1);
            return nextId;
        }

        int NextCustomPresetId(const std::vector<AimBehaviorPreset>& presets)
        {
            int nextId = 1;
            for (const AimBehaviorPreset& preset : presets)
                nextId = (std::max)(nextId, preset.id + 1);
            return nextId;
        }

        int NextCustomPresetId(const std::vector<DynamicFovPreset>& presets)
        {
            int nextId = 1;
            for (const DynamicFovPreset& preset : presets)
                nextId = (std::max)(nextId, preset.id + 1);
            return nextId;
        }

        struct HeroPresetDefinition {
            uint64_t heroId;
            const char* presetName;
            const char* legacyName;
            const char* legacyAlias;
        };

        constexpr HeroPresetDefinition kHeroPresetDefinitions[] = {
            { OW::eHero::HERO_TRACER,       "Tracer",       "Tracer",         nullptr },
            { OW::eHero::HERO_WIDOWMAKER,   "Widowmaker",   "Widowmaker",     nullptr },
            { OW::eHero::HERO_SOLDIER76,    "Soldier76",    "Soldier 76",     nullptr },
            { OW::eHero::HERO_GENJI,        "Genji",        "Genji",          nullptr },
            { OW::eHero::HERO_HANJO,        "Hanzo",        "Hanzo",          nullptr },
            { OW::eHero::HERO_MCCREE,       "Cassidy",      "McCree",         "Cassidy" },
            { OW::eHero::HERO_PHARAH,       "Pharah",       "Pharah",         nullptr },
            { OW::eHero::HERO_REAPER,       "Reaper",       "Reaper",         nullptr },
            { OW::eHero::HERO_SOMBRA,       "Sombra",       "Sombra",         nullptr },
            { OW::eHero::HERO_SYMMETRA,     "Symmetra",     "Symmetra",       nullptr },
            { OW::eHero::HERO_TORBJORN,     "Torbjorn",     "Torbjorn",       nullptr },
            { OW::eHero::HERO_BASTION,      "Bastion",      "Bastion",        nullptr },
            { OW::eHero::HERO_JUNKRAT,      "Junkrat",      "Junkrat",        nullptr },
            { OW::eHero::HERO_MEI,          "Mei",          "Mei",            nullptr },
            { OW::eHero::HERO_ASHE,         "Ashe",         "Ashe",           nullptr },
            { OW::eHero::HERO_SOJOURN,      "Sojourn",      "Sojourn",        nullptr },
            { OW::eHero::HERO_VENTURE,      "Venture",      "Venture",        nullptr },
            { OW::eHero::HERO_ECHO,         "Echo",         "Echo",           nullptr },
            { OW::eHero::HERO_EMRE,         "Emre",         nullptr,          nullptr },
            { OW::eHero::HERO_FREJA,        "Freja",        nullptr,          nullptr },
            { OW::eHero::HERO_SIERRA,       "Sierra",       nullptr,          nullptr },
            { OW::eHero::HERO_VENDETTA,     "Vendetta",     nullptr,          nullptr },
            { OW::eHero::HERO_ANRAN,        "Anran",        nullptr,          nullptr },
            { OW::eHero::HERO_SHION,        "Shion",        nullptr,          nullptr },
            { OW::eHero::HERO_REINHARDT,    "Reinhardt",    "Reinhardt",      nullptr },
            { OW::eHero::HERO_WINSTON,      "Winston",      "Winston",        nullptr },
            { OW::eHero::HERO_ZARYA,        "Zarya",        "Zarya",          nullptr },
            { OW::eHero::HERO_DVA,          "DVa",          "D.Va",           "Hana" },
            { OW::eHero::HERO_ROADHOG,      "Roadhog",      "Roadhog",        nullptr },
            { OW::eHero::HERO_ORISA,        "Orisa",        "Orisa",          nullptr },
            { OW::eHero::HERO_WRECKINGBALL, "WreckingBall", "Wrecking Ball",  nullptr },
            { OW::eHero::HERO_SIGMA,        "Sigma",        "Sigma",          nullptr },
            { OW::eHero::HERO_DOOMFIST,     "Doomfist",     "Doomfist",       nullptr },
            { OW::eHero::HERO_RAMATTRA,     "Ramattra",     "Ramattra",       nullptr },
            { OW::eHero::HERO_JUNKERQUEEN,  "JunkerQueen",  "Junker Queen",   nullptr },
            { OW::eHero::HERO_MAUGA,        "Mauga",        "Mauga",          nullptr },
            { OW::eHero::HERO_HAZARD,       "Hazard",       nullptr,          nullptr },
            { OW::eHero::HERO_MERCY,        "Mercy",        "Mercy",          nullptr },
            { OW::eHero::HERO_LUCIO,        "Lucio",        "Lucio",          nullptr },
            { OW::eHero::HERO_ZENYATTA,     "Zenyatta",     "Zenyatta",       nullptr },
            { OW::eHero::HERO_ANA,          "Ana",          "Ana",            nullptr },
            { OW::eHero::HERO_BRIGITTE,     "Brigitte",     "Brigitte",       nullptr },
            { OW::eHero::HERO_MOIRA,        "Moira",        "Moira",          nullptr },
            { OW::eHero::HERO_BAPTISTE,     "Baptiste",     "Baptiste",       nullptr },
            { OW::eHero::HERO_KIRIKO,       "Kiriko",       "Kiriko",         nullptr },
            { OW::eHero::HERO_LIFEWEAVER,   "Lifeweaver",   "LifeWeaver",     "Lifeweaver" },
            { OW::eHero::HERO_ILLARI,       "Illari",       "Illari",         nullptr },
            { OW::eHero::HERO_MIZUKI,       "Mizuki",       nullptr,          nullptr },
            { OW::eHero::HERO_JUNO,         "Juno",         nullptr,          nullptr },
            { OW::eHero::HERO_WUYANG,       "Wuyang",       nullptr,          nullptr },
            { OW::eHero::HERO_JETPACKCAT,  "JetpackCat",   "Jetpack Cat",    nullptr },
        };

        void ApplyAimMode(int mode);
        int CurrentAimMode();
        HeroPreset MakeHeroAimPresetFromCurrentUnlocked();
        HeroPreset MakeHeroTriggerPresetFromCurrentUnlocked();
        TriggerPreset ValidateTriggerPresetValue(TriggerPreset preset);
        HeroPreset ValidateHeroPresetValue(HeroPreset preset);
        HeroPreset ValidateHeroPresetValueForHero(uint64_t heroId, HeroPreset preset);
        bool gLastHeroConfigHadAimOrTriggerPresets = false;
        bool gLastHeroConfigNeedsSave = false;
        int gLoadedHeroConfigVersion = kCurrentHeroConfigVersion;

        struct IniFile {
            std::unordered_map<std::string, SectionValues> sections;

            bool TryGet(const char* section, const char* key, std::string& value) const
            {
                const auto sec = sections.find(Normalize(section));
                if (sec == sections.end())
                    return false;

                const auto item = sec->second.find(Normalize(key));
                if (item == sec->second.end())
                    return false;

                value = item->second;
                return true;
            }

            static std::string Normalize(const std::string& input)
            {
                std::string out = Trim(input);
                std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                return out;
            }

            static std::string Trim(const std::string& input)
            {
                size_t first = 0;
                while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first])))
                    ++first;

                size_t last = input.size();
                while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1])))
                    --last;

                return input.substr(first, last - first);
            }
        };

        const char* LevelName(Diagnostics::LogLevel level)
        {
            return Diagnostics::ToString(level);
        }

        void LogConfig(Diagnostics::LogLevel level, const char* fmt, ...)
        {
            char message[2048] = {};

            va_list args;
            va_start(args, fmt);
            std::vsnprintf(message, sizeof(message), fmt, args);
            va_end(args);

            if (Diagnostics::IsInitialized()) {
                Diagnostics::Log(level, "[CONFIG] %s", message);
            } else {
                std::printf("[CONFIG] [%s] %s\n", LevelName(level), message);
            }
        }

        bool FileExists(const std::string& path)
        {
            const DWORD attributes = GetFileAttributesA(path.c_str());
            return attributes != INVALID_FILE_ATTRIBUTES &&
                (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
        }

        std::string HeroSectionName(uint64_t heroId, uint64_t linkBase)
        {
            std::string name = OW::GetHeroEngNames(heroId, linkBase);
            if (name.empty())
                name = "Unknown";
            return name;
        }

        bool LoadIniFile(const std::string& path, IniFile& out)
        {
            std::ifstream file(path);
            if (!file.is_open())
                return false;

            std::string section;
            std::string line;
            int lineNumber = 0;
            while (std::getline(file, line)) {
                ++lineNumber;

                if (!line.empty() && static_cast<unsigned char>(line[0]) == 0xEF && line.size() >= 3 &&
                    static_cast<unsigned char>(line[1]) == 0xBB &&
                    static_cast<unsigned char>(line[2]) == 0xBF) {
                    line.erase(0, 3);
                }

                std::string trimmed = IniFile::Trim(line);
                if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                    continue;

                if (trimmed.front() == '[' && trimmed.back() == ']') {
                    section = IniFile::Trim(trimmed.substr(1, trimmed.size() - 2));
                    continue;
                }

                const size_t equals = trimmed.find('=');
                if (equals == std::string::npos) {
                    LogConfig(Diagnostics::LogLevel::Warn,
                        "Ignoring malformed INI line %d in %s: %s",
                        lineNumber, path.c_str(), trimmed.c_str());
                    continue;
                }

                const std::string key = IniFile::Trim(trimmed.substr(0, equals));
                const std::string value = IniFile::Trim(trimmed.substr(equals + 1));
                if (section.empty() || key.empty()) {
                    LogConfig(Diagnostics::LogLevel::Warn,
                        "Ignoring INI line %d in %s because it has no section or key.",
                        lineNumber, path.c_str());
                    continue;
                }

                out.sections[IniFile::Normalize(section)][IniFile::Normalize(key)] = value;
            }

            return true;
        }

        bool ParseStrictInt(const std::string& raw, int& out)
        {
            const std::string value = IniFile::Trim(raw);
            if (value.empty())
                return false;

            errno = 0;
            char* end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (errno == ERANGE || end == value.c_str())
                return false;

            while (end && *end != '\0') {
                if (!std::isspace(static_cast<unsigned char>(*end)))
                    return false;
                ++end;
            }

            if (parsed < (std::numeric_limits<int>::min)() || parsed > (std::numeric_limits<int>::max)())
                return false;

            out = static_cast<int>(parsed);
            return true;
        }

        bool ParseStrictUint64(const std::string& raw, uint64_t& out)
        {
            const std::string value = IniFile::Trim(raw);
            if (value.empty())
                return false;

            int base = 10;
            const char* start = value.c_str();
            if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
                base = 16;
                start += 2;
                if (*start == '\0')
                    return false;
            }

            errno = 0;
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(start, &end, base);
            if (errno == ERANGE || end == start)
                return false;

            while (end && *end != '\0') {
                if (!std::isspace(static_cast<unsigned char>(*end)))
                    return false;
                ++end;
            }

            out = static_cast<uint64_t>(parsed);
            return true;
        }

        bool ParseStrictFloat(const std::string& raw, float& out)
        {
            const std::string value = IniFile::Trim(raw);
            if (value.empty())
                return false;

            errno = 0;
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (errno == ERANGE || end == value.c_str())
                return false;

            while (end && *end != '\0') {
                if (!std::isspace(static_cast<unsigned char>(*end)))
                    return false;
                ++end;
            }

            if (!std::isfinite(parsed))
                return false;

            out = parsed;
            return true;
        }

        bool ParseBoolValue(const std::string& raw, bool& out)
        {
            std::string value = IniFile::Normalize(raw);
            if (value == "1" || value == "true" || value == "yes" || value == "on") {
                out = true;
                return true;
            }
            if (value == "0" || value == "false" || value == "no" || value == "off") {
                out = false;
                return true;
            }
            return false;
        }

        void LogLoaded(const char* section, const char* key, const std::string& value)
        {
            LogConfig(Diagnostics::LogLevel::Info,
                "Loaded [%s] %s=%s from config.ini.",
                section, key, value.c_str());
        }

        void LogDefault(const char* section, const char* key, const std::string& value)
        {
            LogConfig(Diagnostics::LogLevel::Info,
                "Using default [%s] %s=%s.",
                section, key, value.c_str());
        }

        void LogInvalid(const char* section, const char* key, const std::string& raw, const std::string& fallback)
        {
            LogConfig(Diagnostics::LogLevel::Warn,
                "Invalid [%s] %s=%s; using default %s.",
                section, key, raw.c_str(), fallback.c_str());
        }

        std::string ToText(bool value)
        {
            return value ? "true" : "false";
        }

        std::string ToText(int value)
        {
            return std::to_string(value);
        }

        std::string ToText(uint64_t value)
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
            return buffer;
        }

        std::string ToText(float value)
        {
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
            return buffer;
        }

        bool IsAimDiagnosticSetting(const char* name)
        {
            const std::string setting = name ? name : "";
            return setting == "Tracking_smooth" ||
                   setting == "Tracking_smooth2" ||
                   setting == "Flick_smooth" ||
                   setting == "Flick_smooth2" ||
                   setting == "accvalue" ||
                   setting == "accvalue2" ||
                   setting == "aimPidDeadzone" ||
                   setting == "aimBezierSpeed" ||
                   setting == "aimPiecewiseNearDegrees" ||
                   setting == "aimPiecewiseMidDegrees" ||
                   setting == "aimPiecewiseFarDegrees" ||
                   setting == "aimAccelLimitedAcceleration" ||
                   setting == "aimConstantAngularSpeedDeg" ||
                   setting == "linearAngularSpeedScale" ||
                   setting == "pidAngularSpeedScale" ||
                   setting == "bezierAngularSpeedScale" ||
                   setting == "piecewiseAngularSpeedScale" ||
                   setting == "accelLimitedAngularSpeedScale" ||
                   setting == "constantAngularSpeedScale" ||
                   setting == "aim_key" ||
                   setting == "aim_key2" ||
                   setting == "kmboxDeviceType" ||
                   setting == "kmboxPort" ||
                   setting == "kmboxMonitorPort" ||
                   setting == "kmboxInputDelayMs" ||
                   setting == "kmboxCountsPerRadian" ||
                   setting == "kmboxAimSensitivity" ||
                   setting == "gameMouseSensitivity" ||
                   setting == "autoReadGameMouseSensitivity" ||
                   setting == "hostMouseDpi" ||
                   setting == "referenceGameSensitivity" ||
                   setting == "autoScaleByGameSensitivity" ||
                   setting == "sensReference";
        }

        std::string ToText(const char* value)
        {
            return value ? std::string(value) : std::string();
        }

        float DecodeFovDeg(float value, bool legacyApertureValue, const char* section, const char* key)
        {
            const float decoded = legacyApertureValue
                ? LegacyFovApertureToAngleDeg(value)
                : ClampFovDeg(value);
            if (legacyApertureValue && std::isfinite(value)) {
                LogConfig(Diagnostics::LogLevel::Info,
                    "Migrated legacy [%s] %s FOV aperture %.4f deg to angle %.4f deg.",
                    section,
                    key,
                    value,
                    decoded);
            }
            return decoded;
        }

        template <size_t N>
        void CopyString(char (&dest)[N], const std::string& value)
        {
            std::snprintf(dest, N, "%s", value.c_str());
            dest[N - 1] = '\0';
        }

        int ReadInt(const IniFile& ini, const char* section, const char* key, int def)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def;
            }

            int value = def;
            if (!ParseStrictInt(raw, value)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            LogLoaded(section, key, ToText(value));
            return value;
        }

        uint64_t ReadUInt64(const IniFile& ini, const char* section, const char* key, uint64_t def)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def;
            }

            uint64_t value = def;
            if (!ParseStrictUint64(raw, value)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            LogLoaded(section, key, ToText(value));
            return value;
        }

        bool ReadBool(const IniFile& ini, const char* section, const char* key, bool def)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def;
            }

            bool value = def;
            if (!ParseBoolValue(raw, value)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            LogLoaded(section, key, ToText(value));
            return value;
        }

        std::string ReadString(const IniFile& ini, const char* section, const char* key, const char* def)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def ? std::string(def) : std::string();
            }

            LogLoaded(section, key, raw);
            return raw;
        }

        float ReadFixedFloat(const IniFile& ini, const char* section, const char* key, float def)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def;
            }

            if (raw.find('.') != std::string::npos) {
                float plainValue = def;
                if (!ParseStrictFloat(raw, plainValue)) {
                    LogInvalid(section, key, raw, ToText(def));
                    return def;
                }

                LogLoaded(section, key, ToText(plainValue));
                return plainValue;
            }

            int fixedValue = 0;
            if (!ParseStrictInt(raw, fixedValue)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            const float value = static_cast<float>(fixedValue) / 10000.0f;
            LogLoaded(section, key, ToText(value));
            return value;
        }

        float ReadFov2Compat(const IniFile& ini,
                             const char* section,
                             const char* key,
                             float def,
                             bool legacyApertureValue = false)
        {
            std::string raw;
            if (!ini.TryGet(section, key, raw)) {
                LogDefault(section, key, ToText(def));
                return def;
            }

            if (raw.find('.') != std::string::npos) {
                float plainValue = def;
                if (!ParseStrictFloat(raw, plainValue)) {
                    LogInvalid(section, key, raw, ToText(def));
                    return def;
                }
                const float decoded = legacyApertureValue
                    ? DecodeFovDeg(plainValue, true, section, key)
                    : plainValue;
                LogLoaded(section, key, ToText(decoded));
                return decoded;
            }

            int intValue = 0;
            if (!ParseStrictInt(raw, intValue)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            const float value = std::abs(intValue) > 10000
                ? static_cast<float>(intValue) / 10000.0f
                : static_cast<float>(intValue);
            const float decoded = legacyApertureValue
                ? DecodeFovDeg(value, true, section, key)
                : value;
            LogLoaded(section, key, ToText(decoded));
            return decoded;
        }

        bool WriteValue(const std::string& path, const char* section, const char* key, const char* value)
        {
            if (!WritePrivateProfileStringA(section, key, value, path.c_str())) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Failed to write [%s] %s to %s (GetLastError=%lu).",
                    section, key, path.c_str(), static_cast<unsigned long>(GetLastError()));
                return false;
            }
            return true;
        }

        bool DeleteSection(const std::string& path, const char* section)
        {
            if (!WritePrivateProfileStringA(section, nullptr, nullptr, path.c_str())) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Failed to delete [%s] from %s (GetLastError=%lu).",
                    section, path.c_str(), static_cast<unsigned long>(GetLastError()));
                return false;
            }
            return true;
        }

        bool DeleteValue(const std::string& path, const char* section, const char* key)
        {
            if (!WritePrivateProfileStringA(section, key, nullptr, path.c_str())) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Failed to delete [%s] %s from %s (GetLastError=%lu).",
                    section, key, path.c_str(), static_cast<unsigned long>(GetLastError()));
                return false;
            }
            return true;
        }

        void WriteIntValue(const std::string& path, const char* section, const char* key, int value)
        {
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            WriteValue(path, section, key, buffer);
        }

        void WriteUInt64Value(const std::string& path, const char* section, const char* key, uint64_t value)
        {
            WriteValue(path, section, key, ToText(value).c_str());
        }

        void WriteBoolValue(const std::string& path, const char* section, const char* key, bool value)
        {
            WriteIntValue(path, section, key, value ? 1 : 0);
        }

        void WriteStringValue(const std::string& path, const char* section, const char* key, const char* value)
        {
            WriteValue(path, section, key, value ? value : "");
        }

        void WriteFixedFloatValue(const std::string& path, const char* section, const char* key, float value)
        {
            const int fixedValue = static_cast<int>(std::lround(value * 10000.0f));
            WriteIntValue(path, section, key, fixedValue);
        }

        void WritePlainFloatValue(const std::string& path, const char* section, const char* key, float value)
        {
            WriteValue(path, section, key, ToText(value).c_str());
        }

        void WriteColor(const std::string& path, const char* section, const char* prefix, const ImVec4& color)
        {
            const std::string x = std::string(prefix) + "x";
            const std::string y = std::string(prefix) + "y";
            const std::string z = std::string(prefix) + "z";
            const std::string w = std::string(prefix) + "w";
            WriteFixedFloatValue(path, section, x.c_str(), color.x);
            WriteFixedFloatValue(path, section, y.c_str(), color.y);
            WriteFixedFloatValue(path, section, z.c_str(), color.z);
            WriteFixedFloatValue(path, section, w.c_str(), color.w);
        }

        bool SectionExists(const IniFile& ini, const char* section)
        {
            return ini.sections.find(IniFile::Normalize(section)) != ini.sections.end();
        }

        bool KeyExists(const IniFile& ini, const char* section, const char* key)
        {
            std::string value;
            return ini.TryGet(section, key, value);
        }

        float LegacyHitboxRadiusToScalePercent(float radius)
        {
            if (!std::isfinite(radius))
                return kDefaultHitboxScalePercent;
            if (radius > 5.0f)
                return ClampHitboxScalePercent(radius);
            if (radius <= 0.0f)
                return kMinHitboxScalePercent;
            return ClampHitboxScalePercent((radius / kLegacyDefaultHitboxRadius) * 100.0f);
        }

        float ReadHitboxScaleCompat(const IniFile& ini,
                                    const char* section,
                                    const char* scaleKey,
                                    const char* legacyRadiusKey,
                                    float def)
        {
            if (KeyExists(ini, section, scaleKey))
                return ClampHitboxScalePercent(ReadFixedFloat(ini, section, scaleKey, def));
            if (KeyExists(ini, section, legacyRadiusKey)) {
                const float legacyRadius = ReadFixedFloat(
                    ini,
                    section,
                    legacyRadiusKey,
                    kLegacyDefaultHitboxRadius);
                return LegacyHitboxRadiusToScalePercent(legacyRadius);
            }

            LogDefault(section, scaleKey, ToText(def));
            return ClampHitboxScalePercent(def);
        }

        constexpr int kHeroPresetSlotCount = kMaxHeroPresetSlots;

        enum class HeroPresetSlotKind {
            Aim,
            Trigger
        };

        using HeroPresetStore = std::unordered_map<uint64_t, std::array<HeroSlotPreset, kMaxHeroPresetSlots>>;

        HeroPresetStore& PresetStore(HeroPresetSlotKind kind)
        {
            return kind == HeroPresetSlotKind::Aim ? heroAimPresets : heroTriggerPresets;
        }

        const HeroPresetStore& PresetStoreConst(HeroPresetSlotKind kind)
        {
            return kind == HeroPresetSlotKind::Aim ? heroAimPresets : heroTriggerPresets;
        }

        int ClampHeroPresetSlotIndex(int slotIndex)
        {
            return std::clamp(slotIndex, 0, kHeroPresetSlotCount - 1);
        }

        std::string DefaultHeroSlotName(int slotIndex)
        {
            const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
            return std::string("Slot ") + std::to_string(clampedSlotIndex + 1);
        }

        bool IsNumberedDefaultHeroSlotName(const std::string& name)
        {
            static constexpr const char* kPrefix = "Slot ";
            if (name.rfind(kPrefix, 0) != 0)
                return false;

            const std::string number = name.substr(std::char_traits<char>::length(kPrefix));
            if (number.empty())
                return false;

            return std::all_of(number.begin(), number.end(),
                [](unsigned char ch) { return std::isdigit(ch) != 0; });
        }

        std::string NormalizeHeroSlotName(const std::string& name, int slotIndex)
        {
            const std::string trimmed = IniFile::Trim(name);
            if (trimmed.empty() || trimmed == "Preset" || IsNumberedDefaultHeroSlotName(trimmed))
                return DefaultHeroSlotName(slotIndex);
            static constexpr const char* kLegacyActionSlotNames[] = {
                "Primary Fire",
                "Secondary Fire",
                "Scoped",
                "Unscoped",
                "Ability 1",
                "Ability 2 / Ability 3",
                "Ultimate"
            };
            const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
            constexpr int legacyNameCount =
                static_cast<int>(sizeof(kLegacyActionSlotNames) / sizeof(kLegacyActionSlotNames[0]));
            for (int legacyIndex = 0; legacyIndex < legacyNameCount; ++legacyIndex) {
                if (trimmed == kLegacyActionSlotNames[legacyIndex])
                    return DefaultHeroSlotName(clampedSlotIndex);
            }
            return trimmed;
        }

        std::string HeroPresetSectionName(const char* presetName)
        {
            return std::string("Hero_") + (presetName ? presetName : "Unknown");
        }

        std::string HeroPresetSlotSectionName(const char* presetName, int slotIndex)
        {
            return HeroPresetSectionName(presetName) + "_" +
                std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
        }

        const char* HeroPresetKindSectionToken(HeroPresetSlotKind kind)
        {
            return kind == HeroPresetSlotKind::Aim ? "Aim" : "Trigger";
        }

        std::string HeroPresetSlotSectionName(const char* presetName, HeroPresetSlotKind kind, int slotIndex)
        {
            return HeroPresetSectionName(presetName) + "_" +
                HeroPresetKindSectionToken(kind) + "_" +
                std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
        }

        std::string HeroPresetSectionName(uint64_t heroId)
        {
            for (const HeroPresetDefinition& def : kHeroPresetDefinitions) {
                if (def.heroId == heroId)
                    return HeroPresetSectionName(def.presetName);
            }
            return std::string("Hero_") + std::to_string(heroId);
        }

        std::string HeroPresetSlotSectionName(uint64_t heroId, int slotIndex)
        {
            return HeroPresetSectionName(heroId) + "_" +
                std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
        }

        std::string HeroPresetSlotSectionName(uint64_t heroId, HeroPresetSlotKind kind, int slotIndex)
        {
            return HeroPresetSectionName(heroId) + "_" +
                HeroPresetKindSectionToken(kind) + "_" +
                std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
        }

        int LegacyPresetBoneToAimBone(int presetBone)
        {
            if (presetBone == 0) return kAimBoneHead;
            if (presetBone == 1) return kAimBoneNeck;
            return kAimBoneChest;
        }

        SkeletonBoneMask AimBoneMaskFromLegacy(int configuredBone, bool autoBone)
        {
            if (autoBone) {
                return kSkeletonBoneHead |
                    kSkeletonBoneNeck |
                    kSkeletonBoneChest;
            }

            switch (NormalizeAimBone(configuredBone)) {
            case kAimBoneChest: return kSkeletonBoneChest;
            case kAimBoneNeck: return kSkeletonBoneNeck;
            case kAimBoneHead:
            default: return kSkeletonBoneHead;
            }
        }

        int MaxActivationKeyIndex()
        {
            return (std::max)(0, OW::Labels::AimActivationKeyCount() - 1);
        }

        std::vector<int> HeroWeaponActions(uint64_t heroId)
        {
            std::vector<int> actions;
            if (heroId == 0)
                return actions;

            for (const WeaponSpec* spec = WeaponSpecsBegin(); spec != WeaponSpecsEnd(); ++spec) {
                if (spec->heroId != heroId ||
                    spec->action < 0 ||
                    spec->action >= OW::Labels::AttackActionCount())
                    continue;

                if (std::find(actions.begin(), actions.end(), spec->action) == actions.end())
                    actions.push_back(spec->action);
            }
            return actions;
        }

        bool IsHeroWeaponActionValid(uint64_t heroId, int action)
        {
            if (action < 0 || action >= OW::Labels::AttackActionCount())
                return false;
            if (heroId == 0)
                return true;

            const std::vector<int> actions = HeroWeaponActions(heroId);
            if (actions.empty())
                return true;

            return std::find(actions.begin(), actions.end(), action) != actions.end();
        }

        int DefaultHeroWeaponAction(uint64_t heroId)
        {
            const std::vector<int> actions = HeroWeaponActions(heroId);
            return actions.empty() ? 0 : actions.front();
        }

        int NormalizeHeroWeaponAction(uint64_t heroId, int action)
        {
            if (IsHeroWeaponActionValid(heroId, action))
                return std::clamp(action, 0, OW::Labels::AttackActionCount() - 1);
            return DefaultHeroWeaponAction(heroId);
        }

        int DefaultAimActivationKeyForAction(int action)
        {
            switch (action) {
            case 1: // Secondary Fire
            case 2: // Scoped
                return 0; // Right Mouse
            default:
                return 1; // Left Mouse
            }
        }

        int DefaultTriggerActivationKeyForAction(int action)
        {
            switch (action) {
            case 1: // Secondary Fire
            case 2: // Scoped
                return 3; // Mouse 5
            default:
                return 2; // Mouse 4
            }
        }

        std::string BasicHeroSlotName(uint64_t heroId, int action, int slotIndex)
        {
            if (action >= 0 && action < OW::Labels::AttackActionCount())
                return OW::AttackActionCompactNameForHero(heroId, action);
            return DefaultHeroSlotName(slotIndex);
        }

        enum : int {
            kActionPrimary = 0,
            kActionSecondary = 1,
            kActionScoped = 2,
        };

        enum : int {
            kKeyRightMouse = 0,
            kKeyLeftMouse = 1,
            kKeyMouse4 = 2,
            kKeyMouse5 = 3,
        };

        enum : int {
            kTargetEnemies = 0,
            kTargetAllies = 1,
        };

        constexpr int kFirePolicyAuto = -1;
        constexpr float kDocumentedUnsetFloat = -1.0f;
        constexpr float kTrackingDefaultDeadzonePx = 7.0f;

        struct DocumentedAimSlotSpec {
            const char* name = "Primary";
            int action = kActionPrimary;
            int aimBehavior = kAimBehaviorTracking;
            int key = kKeyLeftMouse;
            bool enabled = true;
            AimScopeRequirement scopeRequirement = AimScopeRequirement::All;
            int firePolicy = kFirePolicyAuto;
            int targetTeam = kTargetEnemies;
            float minDistance = kDocumentedUnsetFloat;
            float maxDistance = kDocumentedUnsetFloat;
            float maxAimTime = kDocumentedUnsetFloat;
            float smooth = kDocumentedUnsetFloat;
        };

        struct DocumentedTriggerSlotSpec {
            const char* name = "Primary Trigger";
            int action = kActionPrimary;
            int key = kKeyMouse4;
            bool enabled = true;
            int targetTeam = kTargetEnemies;
            bool chargeAware = false;
            float minCharge = 30.0f;
        };

        DocumentedAimSlotSpec AimSlot(const char* name,
                                      int action,
                                      int behavior,
                                      int key,
                                      bool enabled = true)
        {
            DocumentedAimSlotSpec spec{};
            spec.name = name;
            spec.action = action;
            spec.aimBehavior = behavior;
            spec.key = key;
            spec.enabled = enabled;
            return spec;
        }

        DocumentedAimSlotSpec DisabledAimSlot(const char* name = "No Aim")
        {
            DocumentedAimSlotSpec spec = AimSlot(name, kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse, false);
            spec.firePolicy = static_cast<int>(OW::FirePolicyType::ManualOnly);
            return spec;
        }

        DocumentedAimSlotSpec ScopedAimSlot(const char* name,
                                            int behavior,
                                            int key,
                                            bool enabled = true)
        {
            DocumentedAimSlotSpec spec = AimSlot(
                name,
                kActionScoped,
                behavior,
                key,
                enabled);
            spec.scopeRequirement = AimScopeRequirement::ScopedOnly;
            return spec;
        }

        DocumentedTriggerSlotSpec TriggerSlot(const char* name,
                                              int action,
                                              int key,
                                              bool enabled = true)
        {
            DocumentedTriggerSlotSpec spec{};
            spec.name = name;
            spec.action = action;
            spec.key = key;
            spec.enabled = enabled;
            return spec;
        }

        DocumentedTriggerSlotSpec DisabledTriggerSlot(const char* name = "No Trigger")
        {
            return TriggerSlot(name, kActionPrimary, kKeyMouse4, false);
        }

        std::vector<DocumentedAimSlotSpec> DocumentedAimSlotSpecs(uint64_t heroId)
        {
            switch (heroId) {
            case static_cast<uint64_t>(OW::eHero::HERO_REAPER):
                return { AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_TRACER): {
                auto primary = AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse);
                primary.maxDistance = 25.0f;
                return { primary };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_MERCY):
            case static_cast<uint64_t>(OW::eHero::HERO_REINHARDT):
            case static_cast<uint64_t>(OW::eHero::HERO_MOIRA):
            case static_cast<uint64_t>(OW::eHero::HERO_WUYANG):
            case static_cast<uint64_t>(OW::eHero::HERO_JETPACKCAT):
                return { DisabledAimSlot() };
            case static_cast<uint64_t>(OW::eHero::HERO_HANJO):
                return { AimSlot("Primary Hanzo Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_TORBJORN):
                return {
                    AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyLeftMouse),
                    AimSlot("Secondary Tracking", kActionSecondary, kAimBehaviorTracking, kKeyRightMouse),
                    AimSlot("Turret Primary", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse, false),
                    AimSlot("Turret Secondary", kActionSecondary, kAimBehaviorTracking, kKeyRightMouse, false),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_PHARAH):
                return { AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_WINSTON):
                return { AimSlot("Secondary Flick", kActionSecondary, kAimBehaviorFlick, kKeyRightMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_WIDOWMAKER): {
                auto scopedTrack = ScopedAimSlot("Scoped Tracking", kAimBehaviorTracking, kKeyRightMouse);
                scopedTrack.firePolicy = static_cast<int>(OW::FirePolicyType::ManualOnly);
                scopedTrack.maxAimTime = 30.0f;
                auto fastFlick = ScopedAimSlot("Scoped Fast Flick", kAimBehaviorFlick, kKeyMouse4);
                fastFlick.smooth = 100.0f;
                return {
                    AimSlot("Unscoped Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse),
                    scopedTrack,
                    ScopedAimSlot("Scoped Flick", kAimBehaviorFlick, kKeyMouse5),
                    fastFlick,
                };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_BASTION):
                return { AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_SYMMETRA):
                return {
                    AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse),
                    AimSlot("Secondary Flick", kActionSecondary, kAimBehaviorFlick, kKeyRightMouse),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_ZENYATTA):
                return {
                    AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyLeftMouse),
                    AimSlot("Secondary Flick", kActionSecondary, kAimBehaviorFlick, kKeyRightMouse),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_GENJI):
                return { AimSlot("Secondary Tracking", kActionSecondary, kAimBehaviorTracking, kKeyRightMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_ROADHOG): {
                auto primary = AimSlot("Primary Close Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5);
                primary.maxDistance = 15.0f;
                auto secondary = AimSlot("Secondary Far Flick", kActionSecondary, kAimBehaviorFlick, kKeyMouse5);
                secondary.minDistance = 15.0f;
                return { primary, secondary };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_MCCREE):
                return {
                    AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse),
                    AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5),
                    AimSlot("Secondary Tracking", kActionSecondary, kAimBehaviorTracking, kKeyRightMouse),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_JUNKRAT):
                return { AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_ZARYA):
            case static_cast<uint64_t>(OW::eHero::HERO_SOLDIER76):
            case static_cast<uint64_t>(OW::eHero::HERO_LUCIO):
            case static_cast<uint64_t>(OW::eHero::HERO_DVA):
            case static_cast<uint64_t>(OW::eHero::HERO_SOMBRA):
            case static_cast<uint64_t>(OW::eHero::HERO_DOOMFIST):
            case static_cast<uint64_t>(OW::eHero::HERO_ORISA):
            case static_cast<uint64_t>(OW::eHero::HERO_WRECKINGBALL):
            case static_cast<uint64_t>(OW::eHero::HERO_SOJOURN):
            case static_cast<uint64_t>(OW::eHero::HERO_BAPTISTE):
            case static_cast<uint64_t>(OW::eHero::HERO_RAMATTRA):
            case static_cast<uint64_t>(OW::eHero::HERO_LIFEWEAVER):
            case static_cast<uint64_t>(OW::eHero::HERO_HAZARD):
            case static_cast<uint64_t>(OW::eHero::HERO_JUNO):
                return { AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_MEI):
                return { AimSlot("Secondary Flick", kActionSecondary, kAimBehaviorFlick, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_ANA): {
                auto scopedTrack = ScopedAimSlot("Scoped Tracking", kAimBehaviorTracking, kKeyRightMouse);
                scopedTrack.firePolicy = static_cast<int>(OW::FirePolicyType::ManualOnly);
                scopedTrack.maxAimTime = 30.0f;
                return {
                    AimSlot("Unscoped Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5),
                    ScopedAimSlot("Scoped Flick", kAimBehaviorFlick, kKeyMouse5),
                    scopedTrack,
                };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_BRIGITTE):
                return { DisabledAimSlot() };
            case static_cast<uint64_t>(OW::eHero::HERO_ASHE):
                return {
                    AimSlot("Unscoped Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5),
                    ScopedAimSlot("Scoped Flick", kAimBehaviorFlick, kKeyMouse5),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_ECHO):
                return {
                    AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse),
                    AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_JUNKERQUEEN):
            case static_cast<uint64_t>(OW::eHero::HERO_ILLARI):
                return { DisabledAimSlot() };
            case static_cast<uint64_t>(OW::eHero::HERO_SIGMA):
                return { AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_MAUGA):
                return { AimSlot("Secondary Tracking", kActionSecondary, kAimBehaviorTracking, kKeyRightMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_FREJA):
                return {
                    AimSlot("Unscoped Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse),
                    ScopedAimSlot("Scoped Flick", kAimBehaviorFlick, kKeyMouse5),
                };
            case static_cast<uint64_t>(OW::eHero::HERO_EMRE):
                return { AimSlot("All Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_SIERRA):
                return { AimSlot("Primary Tracking", kActionPrimary, kAimBehaviorTracking, kKeyLeftMouse) };
            case static_cast<uint64_t>(OW::eHero::HERO_VENTURE):
                return { AimSlot("Primary Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5) };
            default:
                return {};
            }
        }

        std::vector<DocumentedAimSlotSpec> LegacyAimScopeLayoutSpecsV13(uint64_t heroId)
        {
            switch (heroId) {
            case static_cast<uint64_t>(OW::eHero::HERO_ASHE):
                return { AimSlot("Unscoped Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_EMRE):
                return {
                    AimSlot("Unscoped Flick", kActionPrimary, kAimBehaviorFlick, kKeyMouse5),
                    AimSlot("Scoped Flick", kActionScoped, kAimBehaviorFlick, kKeyMouse5),
                };
            default:
                return {};
            }
        }

        std::vector<DocumentedTriggerSlotSpec> DocumentedTriggerSlotSpecs(uint64_t heroId)
        {
            switch (heroId) {
            case static_cast<uint64_t>(OW::eHero::HERO_KIRIKO):
                return {};
            case static_cast<uint64_t>(OW::eHero::HERO_REAPER):
                return { TriggerSlot("Primary Trigger", kActionPrimary, kKeyMouse4) };
            case static_cast<uint64_t>(OW::eHero::HERO_HANJO): {
                auto trigger = TriggerSlot("Primary Trigger", kActionPrimary, kKeyMouse4);
                trigger.chargeAware = true;
                trigger.minCharge = 65.0f;
                return { trigger };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_WIDOWMAKER):
                return { TriggerSlot("Scoped Trigger", kActionScoped, kKeyMouse5) };
            case static_cast<uint64_t>(OW::eHero::HERO_ANA): {
                auto trigger = TriggerSlot("Allies Trigger", kActionPrimary, kKeyMouse4);
                trigger.targetTeam = kTargetAllies;
                return { trigger };
            }
            case static_cast<uint64_t>(OW::eHero::HERO_JUNKERQUEEN):
                return { TriggerSlot("Primary Trigger", kActionPrimary, kKeyMouse4) };
            case static_cast<uint64_t>(OW::eHero::HERO_ILLARI):
                return { TriggerSlot("Primary Trigger", kActionPrimary, kKeyMouse4) };
            default: {
                const std::vector<DocumentedAimSlotSpec> aimSpecs = DocumentedAimSlotSpecs(heroId);
                return aimSpecs.empty() ? std::vector<DocumentedTriggerSlotSpec>{}
                    : std::vector<DocumentedTriggerSlotSpec>{ DisabledTriggerSlot() };
            }
            }
        }

        int DefaultDocumentedFirePolicy(uint64_t heroId, int action, int behavior)
        {
            if (IsTrackingBehavior(behavior))
                return static_cast<int>(OW::FirePolicyType::HoldWhileTracking);
            if (IsMagneticTriggerBehavior(behavior))
                return static_cast<int>(OW::FirePolicyType::TapOnHitWindow);

            const OW::WeaponSpec* weapon = OW::ResolveWeaponSpec(heroId, action);
            if (weapon)
                return static_cast<int>(weapon->firePolicy.type);
            return static_cast<int>(OW::FirePolicyType::TapOnHitWindow);
        }

        HeroPreset MakeDocumentedAimPreset(uint64_t heroId,
                                           const HeroPreset& basePreset,
                                           const DocumentedAimSlotSpec& spec)
        {
            HeroPreset preset = basePreset;
            preset.trigger.action = NormalizeHeroWeaponAction(heroId, spec.action);
            preset.key = std::clamp(spec.key, 0, MaxActivationKeyIndex());
            preset.aimBehavior = ClampAimBehaviorIndex(spec.aimBehavior);
            preset.aimMode = IsTrackingBehavior(preset.aimBehavior) ? 0 : 1;
            preset.firePolicy = spec.firePolicy >= 0
                ? spec.firePolicy
                : DefaultDocumentedFirePolicy(heroId, preset.trigger.action, preset.aimBehavior);
            preset.keepFiring = preset.firePolicy == static_cast<int>(OW::FirePolicyType::HoldWhileTracking);
            preset.autoshot = preset.firePolicy != static_cast<int>(OW::FirePolicyType::ManualOnly) &&
                preset.firePolicy != static_cast<int>(OW::FirePolicyType::HoldWhileTracking);
            preset.targetTeam = spec.targetTeam;
            preset.trackingDeadzone = UsesTrackingDeadzone(preset.aimBehavior)
                ? kTrackingDefaultDeadzonePx
                : basePreset.trackingDeadzone;
            if (spec.minDistance >= 0.0f)
                preset.minDistance = spec.minDistance;
            if (spec.maxDistance >= 0.0f)
                preset.maxDistance = spec.maxDistance;
            if (spec.maxAimTime >= 0.0f)
                preset.maxAimTime = spec.maxAimTime;
            if (spec.smooth >= 0.0f)
                preset.smooth = spec.smooth;
            return ValidateHeroPresetValueForHero(heroId, preset);
        }

        HeroPreset MakeDocumentedTriggerPreset(uint64_t heroId,
                                               const HeroPreset& basePreset,
                                               const DocumentedTriggerSlotSpec& spec)
        {
            HeroPreset preset = basePreset;
            preset.targetTeam = spec.targetTeam;
            preset.trigger.enabled = spec.enabled;
            preset.trigger.action = NormalizeHeroWeaponAction(heroId, spec.action);
            preset.trigger.key = std::clamp(spec.key, 0, MaxActivationKeyIndex());
            preset.trigger.mode = 0;
            preset.trigger.chargeAware = spec.chargeAware;
            preset.trigger.minCharge = spec.minCharge;
            return ValidateHeroPresetValueForHero(heroId, preset);
        }

        TriggerPreset ValidateTriggerPresetValue(TriggerPreset preset)
        {
            if (!std::isfinite(preset.shotInterval)) preset.shotInterval = 0.0f;
            if (!std::isfinite(preset.minCharge)) preset.minCharge = 30.0f;

            preset.action = std::clamp(preset.action, 0, 7);
            preset.mode = std::clamp(preset.mode, 0, 2);
            preset.key = std::clamp(preset.key, 0, MaxActivationKeyIndex());
            preset.shotInterval = std::clamp(preset.shotInterval, 0.0f, 100.0f);
            preset.minCharge = std::clamp(preset.minCharge, 0.0f, 100.0f);
            preset.boneMask = NormalizeTriggerBoneMask(preset.boneMask);
            return preset;
        }

        HeroPreset ValidateHeroPresetValue(HeroPreset preset)
        {
            if (!std::isfinite(preset.fov)) preset.fov = kDefaultFovDeg;
            if (!std::isfinite(preset.smooth)) preset.smooth = 5.0f;
            if (!std::isfinite(preset.hitbox)) preset.hitbox = kDefaultHitboxScalePercent;
            if (!std::isfinite(preset.pidP)) preset.pidP = 0.5f;
            if (!std::isfinite(preset.pidI)) preset.pidI = 0.01f;
            if (!std::isfinite(preset.pidD)) preset.pidD = 0.1f;
            if (!std::isfinite(preset.pidMaxIntegral)) preset.pidMaxIntegral = 10.0f;
            if (!std::isfinite(preset.pidDeadzone)) preset.pidDeadzone = 1.0f;
            if (!std::isfinite(preset.bezierCurvature)) preset.bezierCurvature = 0.5f;
            if (!std::isfinite(preset.bezierSpeed)) preset.bezierSpeed = 50.0f;
            if (!std::isfinite(preset.maxHeadDistance)) preset.maxHeadDistance = 100.0f;
            if (!std::isfinite(preset.stickiness)) preset.stickiness = 100.0f;
            if (!std::isfinite(preset.pitchScale)) preset.pitchScale = 1.0f;
            if (!std::isfinite(preset.maxAimTime)) preset.maxAimTime = 100.0f;
            if (!std::isfinite(preset.minCharge)) preset.minCharge = 5.0f;
            if (!std::isfinite(preset.maxCharge)) preset.maxCharge = 100.0f;
            if (!std::isfinite(preset.lockTime)) preset.lockTime = 20.0f;
            if (!std::isfinite(preset.maxDistance)) preset.maxDistance = 100.0f;
            if (!std::isfinite(preset.minDistance)) preset.minDistance = 0.0f;
            if (!std::isfinite(preset.trackingDeadzone)) preset.trackingDeadzone = 0.0f;
            if (!std::isfinite(preset.flickShotClampMs)) preset.flickShotClampMs = 0.0f;
            if (!std::isfinite(preset.flickPostFireDelayMs)) preset.flickPostFireDelayMs = 0.0f;
            if (!std::isfinite(preset.flickTrajectoryWaitMs)) preset.flickTrajectoryWaitMs = 120.0f;
            if (!std::isfinite(preset.flickTrajectoryApexWindowMs)) preset.flickTrajectoryApexWindowMs = 60.0f;
            if (!std::isfinite(preset.flick2ndBoxPadding)) preset.flick2ndBoxPadding = 8.0f;
            if (!std::isfinite(preset.flick2ndInnerRadius)) preset.flick2ndInnerRadius = 34.0f;
            if (!std::isfinite(preset.flick2ndInnerSmoothScale)) preset.flick2ndInnerSmoothScale = 0.55f;

            preset.fov = ClampFovDeg(preset.fov);
            preset.fovMode = ClampFovMode(preset.fovMode);
            preset.dynamicFovPresetId = preset.fovMode == kFovModeDynamicPreset
                ? ClampDynamicFovPresetId(preset.dynamicFovPresetId)
                : -1;
            if (preset.fovMode == kFovModeDynamicPreset && preset.dynamicFovPresetId < 0)
                preset.fovMode = kFovModeFixed;
            preset.smooth = std::clamp(preset.smooth, 0.0f, 100.0f);
            preset.bone = NormalizeAimBone(preset.bone);
            preset.aimBoneMask = NormalizeSkeletonBoneMask(preset.aimBoneMask);
            preset.hitbox = ClampHitboxScalePercent(preset.hitbox);
            preset.aimMode = std::clamp(preset.aimMode, 0, 1);
            if (preset.aimMode == 1 && preset.aimBehavior == 0)
                preset.aimBehavior = kAimBehaviorFlick;
            preset.aimBehavior = ClampAimBehaviorIndex(preset.aimBehavior);
            preset.aimBehaviorPresetId = ClampAimBehaviorPresetId(preset.aimBehaviorPresetId);
            if (const AimBehaviorPreset* behaviorPreset = FindAimBehaviorPreset(preset.aimBehaviorPresetId))
                preset.aimBehavior = ClampAimBehaviorIndex(behaviorPreset->behavior);
            preset.aimMode = IsTrackingBehavior(preset.aimBehavior) ? 0 : 1;
            preset.aimMethod = ClampAimMethodIndex(preset.aimMethod);
            preset.smoothType = std::clamp(preset.smoothType, 0, 2);
            preset.pidP = std::clamp(preset.pidP, 0.0f, 2.0f);
            preset.pidI = std::clamp(preset.pidI, 0.0f, 0.5f);
            preset.pidD = std::clamp(preset.pidD, 0.0f, 1.0f);
            preset.pidMaxIntegral = std::clamp(preset.pidMaxIntegral, 1.0f, 50.0f);
            preset.pidDeadzone = std::clamp(preset.pidDeadzone, 0.0f, 10.0f);
            preset.bezierControlPoints = std::clamp(preset.bezierControlPoints, 2, 6);
            preset.bezierCurvature = std::clamp(preset.bezierCurvature, 0.0f, 1.0f);
            preset.bezierSpeed = std::clamp(preset.bezierSpeed, 1.0f, 200.0f);
            preset.key = std::clamp(preset.key, 0, MaxActivationKeyIndex());
            preset.predictionMode = std::clamp(preset.predictionMode, 0, 2);
            if (preset.firePolicy == 0 && preset.keepFiring)
                preset.firePolicy = 1;
            else if (preset.firePolicy == 0 && preset.autoshot)
                preset.firePolicy = 2;
            preset.firePolicy = std::clamp(preset.firePolicy, 0, 5);
            preset.maxHeadDistance = std::clamp(preset.maxHeadDistance, 0.0f, 500.0f);
            preset.stickiness = std::clamp(preset.stickiness, 0.0f, 100.0f);
            preset.pitchScale = std::clamp(preset.pitchScale, 0.1f, 3.0f);
            preset.priority = std::clamp(preset.priority, 0, 2);
            preset.targetTeam = std::clamp(preset.targetTeam, 0, 2);
            preset.maxAimTime = std::clamp(preset.maxAimTime, 0.0f, 100.0f);
            preset.minCharge = std::clamp(preset.minCharge, 0.0f, 100.0f);
            preset.maxCharge = std::clamp(preset.maxCharge, 0.0f, 100.0f);
            preset.traceCondition = std::clamp(preset.traceCondition, 0, 2);
            preset.unlockCondition = std::clamp(preset.unlockCondition, 0, 2);
            preset.lockTime = std::clamp(preset.lockTime, 0.0f, 5000.0f);
            preset.maxDistance = std::clamp(preset.maxDistance, 0.0f, 500.0f);
            preset.minDistance = std::clamp(preset.minDistance, 0.0f, 500.0f);
            preset.trackingDeadzone = ClampTrackingDeadzonePixels(preset.trackingDeadzone);
            preset.flickShotClampMs = ClampFlickShotClampMs(preset.flickShotClampMs);
            preset.flickPostFireDelayMs = ClampFlickPostFireDelayMs(preset.flickPostFireDelayMs);
            preset.flickTrajectoryWaitMs = ClampTrajectoryWaitMs(preset.flickTrajectoryWaitMs);
            preset.flickTrajectoryApexWindowMs = ClampTrajectoryApexWindowMs(preset.flickTrajectoryApexWindowMs);
            preset.flick2ndBoxPadding = std::clamp(preset.flick2ndBoxPadding, 0.0f, 80.0f);
            preset.flick2ndInnerRadius = std::clamp(preset.flick2ndInnerRadius, 0.0f, 250.0f);
            preset.flick2ndInnerSmoothScale = std::clamp(preset.flick2ndInnerSmoothScale, 0.1f, 1.0f);
            preset.flick2ndInnerMethod = ClampAimMethodIndex(preset.flick2ndInnerMethod);
            preset.trigger = ValidateTriggerPresetValue(preset.trigger);
            return preset;
        }

        HeroPreset ValidateHeroPresetValueForHero(uint64_t heroId, HeroPreset preset)
        {
            preset = ValidateHeroPresetValue(preset);
            preset.trigger.action = NormalizeHeroWeaponAction(heroId, preset.trigger.action);
            preset.key = std::clamp(preset.key, 0, MaxActivationKeyIndex());
            preset.trigger.key = std::clamp(preset.trigger.key, 0, MaxActivationKeyIndex());
            return preset;
        }

        HeroSkillSettings ValidateHeroSkillSettingsValue(HeroSkillSettings settings)
        {
            if (!std::isfinite(settings.healthThreshold)) settings.healthThreshold = 50.0f;
            if (!std::isfinite(settings.enemyHealthThreshold)) settings.enemyHealthThreshold = 50.0f;
            if (!std::isfinite(settings.allyHealthThreshold)) settings.allyHealthThreshold = 50.0f;
            if (!std::isfinite(settings.distance)) settings.distance = 30.0f;
            if (!std::isfinite(settings.cooldown)) settings.cooldown = 0.0f;
            if (!std::isfinite(settings.radius)) settings.radius = 0.0f;
            if (!std::isfinite(settings.tracking.smooth)) settings.tracking.smooth = 0.0f;
            if (!std::isfinite(settings.tracking.speedScale)) settings.tracking.speedScale = 100.0f;
            if (!std::isfinite(settings.tracking.fov)) settings.tracking.fov = 0.0f;
            if (!std::isfinite(settings.tracking.hitbox)) settings.tracking.hitbox = 0.0f;
            if (!std::isfinite(settings.pitchDownDurationJitter)) settings.pitchDownDurationJitter = 10.0f;
            if (!std::isfinite(settings.pitchDownTargetAngle)) settings.pitchDownTargetAngle = 90.0f;
            if (!std::isfinite(settings.pitchUpOffsetJitter)) settings.pitchUpOffsetJitter = 1.5f;
            if (!std::isfinite(settings.projectileSpeed)) settings.projectileSpeed = 0.0f;
            if (!std::isfinite(settings.projectileRadius)) settings.projectileRadius = 0.0f;
            if (!std::isfinite(settings.preFireDelayMs)) settings.preFireDelayMs = 0.0f;

            settings.key = std::clamp(settings.key, 0, MaxActivationKeyIndex());
            settings.skillKey = settings.skillKey < 0
                ? settings.key
                : std::clamp(settings.skillKey, 0, MaxActivationKeyIndex());
            settings.healthThreshold = std::clamp(settings.healthThreshold, 0.0f, 500.0f);
            settings.enemyHealthThreshold = std::clamp(settings.enemyHealthThreshold, 0.0f, 500.0f);
            settings.allyHealthThreshold = std::clamp(settings.allyHealthThreshold, 0.0f, 500.0f);
            settings.distance = std::clamp(settings.distance, 0.0f, 100.0f);
            settings.mode = std::clamp(settings.mode, 0, 2);
            settings.cooldown = std::clamp(settings.cooldown, 0.0f, 60.0f);
            settings.minTargets = std::clamp(settings.minTargets, 1, 8);
            settings.radius = std::clamp(settings.radius, 0.0f, 30.0f);
            if (settings.sequenceSteps.size() > static_cast<size_t>(kMaxHeroSkillSequenceSteps))
                settings.sequenceSteps.resize(static_cast<size_t>(kMaxHeroSkillSequenceSteps));
            for (HeroSkillSequenceStep& step : settings.sequenceSteps) {
                if (!std::isfinite(step.speedScale)) step.speedScale = 1.0f;
                step.buttonMask = std::clamp(step.buttonMask, 0, 7);
                step.durationMs = std::clamp(step.durationMs, 0, 1000);
                step.speedScale = std::clamp(step.speedScale, 0.5f, 2.0f);
                step.jitterMs = std::clamp(step.jitterMs, 0, 50);
            }
            settings.tracking.aimBehavior = ClampAimBehaviorIndex(settings.tracking.aimBehavior);
            settings.tracking.aimBehaviorPresetId = ClampAimBehaviorPresetId(settings.tracking.aimBehaviorPresetId);
            if (const AimBehaviorPreset* behaviorPreset = FindAimBehaviorPreset(settings.tracking.aimBehaviorPresetId))
                settings.tracking.aimBehavior = ClampAimBehaviorIndex(behaviorPreset->behavior);
            settings.tracking.method = ClampAimMethodIndex(settings.tracking.method);
            settings.tracking.smooth = std::clamp(settings.tracking.smooth, 0.0f, 100.0f);
            settings.tracking.speedScale = std::clamp(settings.tracking.speedScale, 0.0f, 100.0f);
            settings.tracking.fov = ClampFovDeg(settings.tracking.fov);
            settings.tracking.bone = settings.tracking.bone == kAimBoneClosest
                ? kAimBoneClosest
                : NormalizeAimBone(settings.tracking.bone);
            settings.tracking.hitbox = ClampHitboxScalePercent(settings.tracking.hitbox);
            settings.pitchDownDurationMs = std::clamp(settings.pitchDownDurationMs, 20, 100);
            settings.pitchDownDurationJitter = std::clamp(settings.pitchDownDurationJitter, 0.0f, 50.0f);
            settings.pitchDownTargetAngle = std::clamp(settings.pitchDownTargetAngle, 0.0f, 180.0f);
            settings.pitchUpOffsetJitter = std::clamp(settings.pitchUpOffsetJitter, 0.0f, 20.0f);
            settings.fireDelayMs = std::clamp(settings.fireDelayMs, 0, 100);
            settings.jumpKeyCode = std::clamp(settings.jumpKeyCode, 0, 255);
            settings.ammoGuardReserve = std::clamp(settings.ammoGuardReserve, 0, 50);
            settings.projectileSpeed = std::clamp(settings.projectileSpeed, 0.0f, 300.0f);
            settings.projectileRadius = std::clamp(settings.projectileRadius, 0.0f, 2.0f);
            settings.preFireDelayMs = std::clamp(settings.preFireDelayMs, 0.0f, 1000.0f);
            return settings;
        }

        void ResetHeroPresetSlot(HeroSlotPreset& slot, int slotIndex)
        {
            slot = HeroSlotPreset{};
            slot.name = DefaultHeroSlotName(slotIndex);
            slot.present = false;
            slot.enabled = false;
            slot.preset = ValidateHeroPresetValue(slot.preset);
        }

        void InitializeHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                       int presentCount,
                                       bool enabled,
                                       bool autoBone = false)
        {
            presentCount = std::clamp(presentCount, 0, kHeroPresetSlotCount);
            for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                HeroSlotPreset& slot = slots[static_cast<size_t>(slotIndex)];
                ResetHeroPresetSlot(slot, slotIndex);
                slot.preset.autoBone = autoBone;
                slot.preset.aimBoneMask = AimBoneMaskFromLegacy(slot.preset.bone, autoBone);
                if (slotIndex < presentCount) {
                    slot.present = true;
                    slot.enabled = enabled;
                }
            }
        }

        void InitializeWeaponGeneratedHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                                      uint64_t heroId,
                                                      HeroPresetSlotKind kind)
        {
            InitializeHeroPresetSlots(slots, 0, false);

            std::vector<int> actions = HeroWeaponActions(heroId);
            if (actions.empty())
                actions.push_back(0);

            const int presentCount = (std::min)(
                static_cast<int>(actions.size()),
                kHeroPresetSlotCount);
            const HeroPreset basePreset = kind == HeroPresetSlotKind::Aim
                ? MakeHeroAimPresetFromCurrentUnlocked()
                : MakeHeroTriggerPresetFromCurrentUnlocked();

            for (int slotIndex = 0; slotIndex < presentCount; ++slotIndex) {
                const int action = actions[static_cast<size_t>(slotIndex)];
                HeroSlotPreset& slot = slots[static_cast<size_t>(slotIndex)];
                ResetHeroPresetSlot(slot, slotIndex);
                slot.name = BasicHeroSlotName(heroId, action, slotIndex);
                slot.present = true;
                slot.enabled = true;
                slot.preset = basePreset;
                slot.preset.key = DefaultAimActivationKeyForAction(action);
                slot.preset.trigger.action = action;
                slot.preset.trigger.key = DefaultTriggerActivationKeyForAction(action);
                slot.preset = ValidateHeroPresetValueForHero(heroId, slot.preset);
                slot.scopeRequirement = kind == HeroPresetSlotKind::Aim
                    ? DefaultAimScopeRequirementForHeroAction(heroId, slot.preset.trigger.action)
                    : AimScopeRequirement::All;
            }
        }

        bool InitializeAimPresetSlotsFromSpecs(
            std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
            uint64_t heroId,
            const std::vector<DocumentedAimSlotSpec>& specs)
        {
            InitializeHeroPresetSlots(slots, 0, false);
            if (specs.empty())
                return false;

            const HeroPreset basePreset = MakeHeroAimPresetFromCurrentUnlocked();
            const int presentCount = (std::min)(
                static_cast<int>(specs.size()),
                kHeroPresetSlotCount);
            for (int slotIndex = 0; slotIndex < presentCount; ++slotIndex) {
                const DocumentedAimSlotSpec& spec = specs[static_cast<size_t>(slotIndex)];
                HeroSlotPreset& slot = slots[static_cast<size_t>(slotIndex)];
                ResetHeroPresetSlot(slot, slotIndex);
                slot.name = spec.name ? spec.name : BasicHeroSlotName(heroId, spec.action, slotIndex);
                slot.present = true;
                slot.enabled = spec.enabled;
                slot.scopeRequirement = spec.scopeRequirement;
                slot.preset = MakeDocumentedAimPreset(heroId, basePreset, spec);
            }
            return true;
        }

        bool InitializeDocumentedHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                                  uint64_t heroId,
                                                  HeroPresetSlotKind kind)
        {
            if (kind == HeroPresetSlotKind::Aim)
                return InitializeAimPresetSlotsFromSpecs(slots, heroId, DocumentedAimSlotSpecs(heroId));

            const HeroPreset basePreset = MakeHeroTriggerPresetFromCurrentUnlocked();
            InitializeHeroPresetSlots(slots, 0, false);
            const std::vector<DocumentedTriggerSlotSpec> specs = DocumentedTriggerSlotSpecs(heroId);
            if (specs.empty())
                return false;

            const int presentCount = (std::min)(
                static_cast<int>(specs.size()),
                kHeroPresetSlotCount);
            for (int slotIndex = 0; slotIndex < presentCount; ++slotIndex) {
                const DocumentedTriggerSlotSpec& spec = specs[static_cast<size_t>(slotIndex)];
                HeroSlotPreset& slot = slots[static_cast<size_t>(slotIndex)];
                ResetHeroPresetSlot(slot, slotIndex);
                slot.name = spec.name ? spec.name : BasicHeroSlotName(heroId, spec.action, slotIndex);
                slot.present = true;
                slot.enabled = spec.enabled;
                slot.preset = MakeDocumentedTriggerPreset(heroId, basePreset, spec);
            }
            return true;
        }

        void InitializeBasicHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                            uint64_t heroId,
                                            HeroPresetSlotKind kind)
        {
            if (InitializeDocumentedHeroPresetSlots(slots, heroId, kind))
                return;
            InitializeWeaponGeneratedHeroPresetSlots(slots, heroId, kind);
        }

        int CountHeroPresetSlots(const std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots)
        {
            int count = 0;
            for (const HeroSlotPreset& slot : slots) {
                if (slot.present)
                    ++count;
            }
            return count;
        }

        bool SameFloatForDefaultMigration(float lhs, float rhs)
        {
            return std::fabs(lhs - rhs) <= 0.001f;
        }

        bool SameTriggerPresetForDefaultMigration(const TriggerPreset& lhs,
                                                  const TriggerPreset& rhs)
        {
            return lhs.enabled == rhs.enabled &&
                lhs.action == rhs.action &&
                lhs.mode == rhs.mode &&
                lhs.key == rhs.key &&
                SameFloatForDefaultMigration(lhs.shotInterval, rhs.shotInterval) &&
                lhs.chargeAware == rhs.chargeAware &&
                SameFloatForDefaultMigration(lhs.minCharge, rhs.minCharge) &&
                lhs.disableWhileReloading == rhs.disableWhileReloading &&
                lhs.ignoreInvisible == rhs.ignoreInvisible &&
                lhs.boneMask == rhs.boneMask &&
                lhs.drawHitbox == rhs.drawHitbox;
        }

        bool SameHeroPresetForDefaultMigration(const HeroPreset& lhs,
                                               const HeroPreset& rhs)
        {
            return SameFloatForDefaultMigration(lhs.fov, rhs.fov) &&
                SameFloatForDefaultMigration(lhs.smooth, rhs.smooth) &&
                lhs.bone == rhs.bone &&
                lhs.autoBone == rhs.autoBone &&
                lhs.aimBoneMask == rhs.aimBoneMask &&
                SameFloatForDefaultMigration(lhs.hitbox, rhs.hitbox) &&
                lhs.aimMode == rhs.aimMode &&
                lhs.aimBehavior == rhs.aimBehavior &&
                lhs.aimMethod == rhs.aimMethod &&
                lhs.smoothType == rhs.smoothType &&
                lhs.key == rhs.key &&
                lhs.autoshot == rhs.autoshot &&
                lhs.keepFiring == rhs.keepFiring &&
                lhs.prediction == rhs.prediction &&
                lhs.predictionMode == rhs.predictionMode &&
                lhs.firePolicy == rhs.firePolicy &&
                SameFloatForDefaultMigration(lhs.maxHeadDistance, rhs.maxHeadDistance) &&
                SameFloatForDefaultMigration(lhs.stickiness, rhs.stickiness) &&
                SameFloatForDefaultMigration(lhs.pitchScale, rhs.pitchScale) &&
                lhs.priority == rhs.priority &&
                lhs.targetTeam == rhs.targetTeam &&
                SameFloatForDefaultMigration(lhs.maxAimTime, rhs.maxAimTime) &&
                SameFloatForDefaultMigration(lhs.minCharge, rhs.minCharge) &&
                SameFloatForDefaultMigration(lhs.maxCharge, rhs.maxCharge) &&
                lhs.ignoreInvisible == rhs.ignoreInvisible &&
                lhs.traceCondition == rhs.traceCondition &&
                lhs.unlockCondition == rhs.unlockCondition &&
                SameFloatForDefaultMigration(lhs.lockTime, rhs.lockTime) &&
                SameFloatForDefaultMigration(lhs.maxDistance, rhs.maxDistance) &&
                SameFloatForDefaultMigration(lhs.minDistance, rhs.minDistance) &&
                SameFloatForDefaultMigration(lhs.trackingDeadzone, rhs.trackingDeadzone) &&
                SameFloatForDefaultMigration(lhs.flickShotClampMs, rhs.flickShotClampMs) &&
                SameFloatForDefaultMigration(lhs.flickPostFireDelayMs, rhs.flickPostFireDelayMs) &&
                lhs.flickTrajectoryWait == rhs.flickTrajectoryWait &&
                SameFloatForDefaultMigration(lhs.flickTrajectoryWaitMs, rhs.flickTrajectoryWaitMs) &&
                SameFloatForDefaultMigration(lhs.flickTrajectoryApexWindowMs, rhs.flickTrajectoryApexWindowMs) &&
                lhs.flick2ndTriggerGate == rhs.flick2ndTriggerGate &&
                SameFloatForDefaultMigration(lhs.flick2ndBoxPadding, rhs.flick2ndBoxPadding) &&
                SameFloatForDefaultMigration(lhs.flick2ndInnerRadius, rhs.flick2ndInnerRadius) &&
                SameFloatForDefaultMigration(lhs.flick2ndInnerSmoothScale, rhs.flick2ndInnerSmoothScale) &&
                lhs.flick2ndInnerMethod == rhs.flick2ndInnerMethod &&
                SameTriggerPresetForDefaultMigration(lhs.trigger, rhs.trigger);
        }

        bool SameHeroSlotForDefaultMigration(const HeroSlotPreset& lhs,
                                             const HeroSlotPreset& rhs)
        {
            if (lhs.present != rhs.present)
                return false;
            if (!lhs.present)
                return true;
            return lhs.enabled == rhs.enabled &&
                lhs.name == rhs.name &&
                SameHeroPresetForDefaultMigration(
                    ValidateHeroPresetValue(lhs.preset),
                    ValidateHeroPresetValue(rhs.preset));
        }

        bool SlotsStillMatchLegacyGeneratedDefaults(
            const std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
            uint64_t heroId,
            HeroPresetSlotKind kind)
        {
            std::array<HeroSlotPreset, kHeroPresetSlotCount> expected{};
            InitializeWeaponGeneratedHeroPresetSlots(expected, heroId, kind);
            for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                if (!SameHeroSlotForDefaultMigration(
                        slots[static_cast<size_t>(slotIndex)],
                        expected[static_cast<size_t>(slotIndex)])) {
                    return false;
                }
            }
            return true;
        }

        bool MigrateDocumentedHeroSlotsIfLegacyDefault(
            std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
            uint64_t heroId,
            HeroPresetSlotKind kind)
        {
            if (gLoadedHeroConfigVersion >= kDocumentedHeroSlotDefaultsVersion)
                return false;
            if ((kind == HeroPresetSlotKind::Aim && DocumentedAimSlotSpecs(heroId).empty()) ||
                (kind == HeroPresetSlotKind::Trigger && DocumentedTriggerSlotSpecs(heroId).empty())) {
                return false;
            }
            if (!SlotsStillMatchLegacyGeneratedDefaults(slots, heroId, kind))
                return false;

            InitializeBasicHeroPresetSlots(slots, heroId, kind);
            gLastHeroConfigNeedsSave = true;
            return true;
        }

        bool MigrateAimScopeLayoutIfV13Default(
            std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
            uint64_t heroId,
            HeroPresetSlotKind kind)
        {
            if (kind != HeroPresetSlotKind::Aim ||
                gLoadedHeroConfigVersion >= kAimScopeRequirementVersion) {
                return false;
            }

            const std::vector<DocumentedAimSlotSpec> legacySpecs =
                LegacyAimScopeLayoutSpecsV13(heroId);
            if (legacySpecs.empty())
                return false;

            std::array<HeroSlotPreset, kHeroPresetSlotCount> expected{};
            InitializeAimPresetSlotsFromSpecs(expected, heroId, legacySpecs);
            for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                if (!SameHeroSlotForDefaultMigration(
                        slots[static_cast<size_t>(slotIndex)],
                        expected[static_cast<size_t>(slotIndex)])) {
                    return false;
                }
            }

            InitializeBasicHeroPresetSlots(slots, heroId, kind);
            gLastHeroConfigNeedsSave = true;
            return true;
        }

        bool MigrateHeroSlotsIfLegacyDefault(
            std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
            uint64_t heroId,
            HeroPresetSlotKind kind)
        {
            if (MigrateDocumentedHeroSlotsIfLegacyDefault(slots, heroId, kind))
                return true;
            return MigrateAimScopeLayoutIfV13Default(slots, heroId, kind);
        }

        void NormalizeHeroPresetSlots(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                      uint64_t heroId,
                                      HeroPresetSlotKind kind)
        {
            std::array<HeroSlotPreset, kHeroPresetSlotCount> normalized{};
            InitializeHeroPresetSlots(normalized, 0, false);

            int writeIndex = 0;
            for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount && writeIndex < kHeroPresetSlotCount; ++slotIndex) {
                const HeroSlotPreset& source = slots[static_cast<size_t>(slotIndex)];
                if (!source.present)
                    continue;

                HeroSlotPreset slot = source;
                if (!IsHeroWeaponActionValid(heroId, slot.preset.trigger.action))
                    continue;

                slot.present = true;
                slot.preset = ValidateHeroPresetValueForHero(heroId, slot.preset);
                slot.scopeRequirement = kind == HeroPresetSlotKind::Aim
                    ? DefaultAimScopeRequirementForHeroAction(
                        heroId,
                        slot.preset.trigger.action)
                    : AimScopeRequirement::All;
                slot.name = NormalizeHeroSlotName(slot.name, writeIndex);
                if (slot.name == DefaultHeroSlotName(writeIndex))
                    slot.name = BasicHeroSlotName(heroId, slot.preset.trigger.action, writeIndex);
                normalized[static_cast<size_t>(writeIndex)] = slot;
                ++writeIndex;
            }

            if (writeIndex == 0) {
                InitializeBasicHeroPresetSlots(normalized, heroId, kind);
            }

            slots = normalized;
        }

        void ValidateHeroPresetsUnlocked()
        {
            auto validateStore = [](HeroPresetStore& store, HeroPresetSlotKind kind) {
                for (auto& item : store) {
                    if (!MigrateHeroSlotsIfLegacyDefault(item.second, item.first, kind))
                        NormalizeHeroPresetSlots(item.second, item.first, kind);
                }
            };
            validateStore(heroAimPresets, HeroPresetSlotKind::Aim);
            validateStore(heroTriggerPresets, HeroPresetSlotKind::Trigger);

            auto ensureBasicStore = [](HeroPresetStore& store, HeroPresetSlotKind kind) {
                for (const HeroPresetDefinition& def : kHeroPresetDefinitions) {
                    auto [item, inserted] = store.try_emplace(def.heroId);
                    if (inserted)
                        InitializeBasicHeroPresetSlots(item->second, def.heroId, kind);
                    else if (!MigrateHeroSlotsIfLegacyDefault(item->second, def.heroId, kind))
                        NormalizeHeroPresetSlots(item->second, def.heroId, kind);
                }
            };
            ensureBasicStore(heroAimPresets, HeroPresetSlotKind::Aim);
            ensureBasicStore(heroTriggerPresets, HeroPresetSlotKind::Trigger);
        }

        void ValidateHeroSkillPresetsUnlocked()
        {
            for (auto hero = heroSkillPresets.begin(); hero != heroSkillPresets.end(); ) {
                for (auto skill = hero->second.begin(); skill != hero->second.end(); ) {
                    if (skill->first.empty()) {
                        skill = hero->second.erase(skill);
                        continue;
                    }

                    skill->second = ValidateHeroSkillSettingsValue(skill->second);
                    ++skill;
                }

                if (hero->second.empty())
                    hero = heroSkillPresets.erase(hero);
                else
                    ++hero;
            }
        }

        void ValidateAimCustomPresetsUnlocked()
        {
            auto idUsed = [](const auto& presets, int id, size_t beforeIndex) {
                for (size_t index = 0; index < beforeIndex; ++index) {
                    if (presets[index].id == id)
                        return true;
                }
                return false;
            };

            int nextMethodId = NextCustomPresetId(aimMethodPresets);
            if (aimMethodPresets.size() > static_cast<size_t>(kMaxAimCustomPresets))
                aimMethodPresets.resize(static_cast<size_t>(kMaxAimCustomPresets));
            for (size_t index = 0; index < aimMethodPresets.size(); ++index) {
                if (aimMethodPresets[index].id < 0 || idUsed(aimMethodPresets, aimMethodPresets[index].id, index))
                    aimMethodPresets[index].id = nextMethodId++;
                aimMethodPresets[index] = ValidateAimMethodPresetValue(
                    aimMethodPresets[index],
                    static_cast<int>(index));
            }

            int nextBehaviorId = NextCustomPresetId(aimBehaviorPresets);
            if (aimBehaviorPresets.size() > static_cast<size_t>(kMaxAimCustomPresets))
                aimBehaviorPresets.resize(static_cast<size_t>(kMaxAimCustomPresets));
            for (size_t index = 0; index < aimBehaviorPresets.size(); ++index) {
                if (aimBehaviorPresets[index].id < 0 || idUsed(aimBehaviorPresets, aimBehaviorPresets[index].id, index))
                    aimBehaviorPresets[index].id = nextBehaviorId++;
                aimBehaviorPresets[index] = ValidateAimBehaviorPresetValue(
                    aimBehaviorPresets[index],
                    static_cast<int>(index));
            }

            int nextDynamicFovId = NextCustomPresetId(dynamicFovPresets);
            if (dynamicFovPresets.size() > static_cast<size_t>(kMaxAimCustomPresets))
                dynamicFovPresets.resize(static_cast<size_t>(kMaxAimCustomPresets));
            for (size_t index = 0; index < dynamicFovPresets.size(); ++index) {
                if (dynamicFovPresets[index].id < 0 || idUsed(dynamicFovPresets, dynamicFovPresets[index].id, index))
                    dynamicFovPresets[index].id = nextDynamicFovId++;
                dynamicFovPresets[index] = ValidateDynamicFovPresetForConfig(
                    dynamicFovPresets[index],
                    static_cast<int>(index));
            }

            for (int& presetId : aimBehaviorMethodPreset)
                presetId = ClampAimMethodPresetId(presetId);
            aimBehaviorPresetId = ClampAimBehaviorPresetId(aimBehaviorPresetId);
            aimbotFovMode = ClampFovMode(aimbotFovMode);
            aimbotDynamicFovPresetId = aimbotFovMode == kFovModeDynamicPreset
                ? ClampDynamicFovPresetId(aimbotDynamicFovPresetId)
                : -1;
        }

        void ResetHeroDefaultsUnlocked()
        {
            // Defaults match Config.hpp inline initializers for per-hero/runtime aim settings.
            enableAimbot = true;          // default: true
            triggerbot = false;           // default: false
            triggerbot2 = false;          // default: false
            Tracking = false;             // default: false
            Tracking2 = false;            // default: false
            Flick = false;                // default: false
            Flick2 = false;               // default: false

            projectile_arc = false;       // default: false
            Prediction = false;           // default: false
            Gravitypredit = false;        // default: false
            predit_level = 110.0f;        // default: 110
            hanzoautospeed = false;       // default: false

            AimKey = 0x01;                // default: VK_LBUTTON
            aim_key = 1;                  // default: Left Mouse
            aim_key2 = 1;                 // default: Left Mouse
            togglekey = 0;                // default: disabled

            Fov = kDefaultFovDeg;         // default: 100 deg
            Fov2 = kDefaultFovDeg;        // default: 100 deg
            minFov1 = kDefaultFovDeg;     // default: 100 deg
            minFov2 = kDefaultFovDeg;     // default: 100 deg
            Smooth = 5.0f;                // default: 5
            autoscalefov = false;         // default: false
            hitbox = kDefaultHitboxScalePercent;  // default: 100%
            hitbox2 = kDefaultHitboxScalePercent; // default: 100%
            missbox = 0.6f;               // default: 0.6
            Tracking_smooth = 0.1f;       // default: 0.1
            Tracking_smooth2 = 0.1f;      // default: 0.1
            Flick_smooth = 0.1f;          // default: 0.1
            Flick_smooth2 = 0.1f;         // default: 0.1
            accvalue = 0.1f;              // default: 0.1
            accvalue2 = 0.1f;             // default: 0.1
            bladespeed = 0.1f;            // default: 0.1

            TargetBone = kAimBoneHead;    // default: head
            Bone = kAimBoneHead;          // default: head
            Bone2 = kAimBoneHead;         // default: head
            autobone = false;             // default: false
            autobone2 = false;            // default: false
            aimbotBoneMask = kDefaultAimBoneMask;
            aimbotBoneMask2 = kDefaultAimBoneMask;
            switch_team = false;          // default: false
            switch_team2 = false;         // default: false
            BoneName = "Head";            // default: Head
            BoneName2 = "Head";           // default: Head

            lockontarget = false;         // default: false
            trackcompensate = false;      // default: false
            comarea = 0.01f;              // default: 0.01
            comspeed = 0.5f;              // default: 0.5
            aiaim = false;                // default: false
            targetdelay = false;          // default: false
            targetdelaytime = 200;        // default: 200 ms
            hitboxdelayshoot = false;     // default: false
            hiboxdelaytime = 200;         // default: 200 ms
            dontshot = false;             // default: false
            shotcount = 0;                // default: 0
            shotmanydont = 3;             // default: 3

            GenjiBlade = false;           // default: false
            AutoShiftGenji = false;       // default: false
            widowautounscope = false;     // default: false

            AutoShoot = false;            // default: false
            Shoottime = 500;              // default: 500 ms
            shooted = false;              // default: false
            shooted2 = false;             // default: false
            lasttime = 0;                 // default: 0
            lasthealth = 0.0f;            // default: 0
            skilled = false;              // default: false
            slasttime = 0;                // default: 0
            sskilled = false;             // default: false
            reloading = false;            // default: false
            Qstarttime = 0;               // default: 0
            Qtime = 0;                    // default: 0
            lastenemy = -1;               // default: -1
            doingdelay = false;           // default: false
            timebeforedelay = 0;          // default: 0

            AutoMelee = false;            // default: false
            meleehealth = 30.0f;          // default: 30
            meleedistance = 5.0f;         // default: 5
            AutoRMB = false;              // default: false
            AutoRMBhealth = 100.0f;       // default: 100
            AutoRMBdistance = 30.0f;      // default: 30
            AutoSkill = false;            // default: false
            SkillHealth = 50.0f;          // default: 50
            AntiAFK = false;              // default: false

            secondaim = false;            // default: false
            highPriority = false;         // default: false
            targetPriority = 0;           // default: FOV priority

            Targetenemyi = -1;            // default: -1
            Targetenemyifov = -1;         // default: -1
            health = 0.0f;                // default: 0
        }

        void ResetAimbotDefaultsUnlocked()
        {
            aimbotAutoshot = false;
            aimbotKeepFiring = true;
            aimbotPredictionMode = 0;
            aimBehavior = 0;
            aimbotFirePolicy = 1;
            aimbotTriggerDelay = 0.0f;
            aimbotMaxHead = 100.0f;
            aimbotSmoothType = 0;
            aimbotStickiness = 100.0f;
            aimbotSmoothY = 50.0f;
            aimbotPitchScale = 1.0f;
            aimbotMaxAim = 100.0f;
            aimbotMinCharge = 5.0f;
            aimbotMaxCharge = 100.0f;
            aimbotIgnoreInvisible = true;
            aimbotTrace = 0;
            aimbotUnlock = 0;
            aimbotLockTime = 20.0f;
            aimbotMaxDist = 100.0f;
            aimbotMinDist = 0.0f;
            aimbotAttack = 0;
            aimbotTeam = 0;
            aimbotPriority = 0;
            aimbotTrackingDeadzone = 0.0f;
            aimbotFlickShotClampMs = 0.0f;
            aimbotFlickPostFireDelayMs = 0.0f;
            aimbotFlickTrajectoryWait = false;
            aimbotFlickTrajectoryWaitMs = 120.0f;
            aimbotFlickTrajectoryApexWindowMs = 60.0f;
            aimbotFlick2ndTriggerGate = true;
            aimbotFlick2ndBoxPadding = 8.0f;
            aimbotFlick2ndInnerRadius = 34.0f;
            aimbotFlick2ndInnerSmoothScale = 0.55f;
            aimbotFlick2ndInnerMethod = 2;
            aimbotTwoStage = false;
            aimbotTwoStageTriggerGate = true;
            aimbotTwoStageBoxPadding = 8.0f;
            aimbotTwoStageInnerRadius = 34.0f;
            aimbotTwoStageInnerSmoothScale = 0.55f;
            aimOvershootCurve = false;
            aimOvershootGain = 0.25f;
            aimOvershootResetPixels = 56.0f;
            aimDryRun = false;
            aimVerboseLog = false;
            aimDryRunLogIntervalMs = 100;
            triggerbotMode = 0;
            triggerbotKey = 1;
            triggerbotShotInterval = 0.0f;
            triggerbotChargeAware = false;
            triggerbotMinCharge = 30.0f;
            triggerbotDisableWhileReloading = false;
            triggerbotIgnoreInvisible = true;
            triggerbotBoneMask = kDefaultTriggerBoneMask;
            triggerbotMode2 = 0;
            triggerbotKey2 = 1;
            triggerbotShotInterval2 = 0.0f;
            triggerbotChargeAware2 = false;
            triggerbotMinCharge2 = 30.0f;
            triggerbotDisableWhileReloading2 = false;
            triggerbotIgnoreInvisible2 = true;
            triggerbotBoneMask2 = kDefaultTriggerBoneMask;
        }

        void ResetAimMethodDefaultsUnlocked()
        {
            aimMethod = 0;
            aimBehaviorMethod = { 0, 0, 0, 0, 0 };
            aimBehaviorMethodPreset = { -1, -1, -1, -1, -1 };
            aimBehaviorBaseSpeed = { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
            aimBehaviorStartLimiterProfiles = {};
            aimBehaviorAcceleration = { 0.1f, 0.1f, 0.1f, 0.1f, 0.1f };
            aimBehaviorMoveSplitEnabled = { true, false, false, true, true };
            aimBehaviorMoveSplitMaxPixels = { 4, 50, 50, 4, 4 };
            aimBehaviorMoveSplitDelayUs = { 800, 0, 0, 800, 800 };
            aimMethodAngularSpeedScale = { 100.0f, 100.0f, 100.0f, 100.0f, 100.0f, 100.0f };
            secondaryAimMethodOverride = { -1, -1 };
            aimPidP = 0.5f;
            aimPidI = 0.01f;
            aimPidD = 0.1f;
            aimPidMaxIntegral = 10.0f;
            aimPidDeadzone = 1.0f;
            aimBezierControlPoints = 2;
            aimBezierCurvature = 0.5f;
            aimBezierSpeed = 50.0f;
            aimPiecewiseNearDegrees = 2.0f;
            aimPiecewiseMidDegrees = 6.0f;
            aimPiecewiseFarDegrees = 12.0f;
            aimPiecewiseNearScale = 0.20f;
            aimPiecewiseMidScale = 0.45f;
            aimPiecewiseFarScale = 0.75f;
            aimAccelLimitedAcceleration = 0.1f;
            aimConstantAngularSpeedDeg = 30.0f;
            aimBehaviorPresetId = -1;
            aimMethodPresets.clear();
            aimBehaviorPresets.clear();
            dynamicFovPresets.clear();
            aimbotFovMode = kFovModeFixed;
            aimbotDynamicFovPresetId = -1;
        }

        void ResetGlobalDefaultsUnlocked()
        {
            // Defaults match Config.hpp inline initializers for global visual/UI settings.
            draw_info = true;             // default: true
            drawbattletag = false;        // default: false
            drawhealth = false;           // current box theme: disable health-gradient line colours
            healthbar = false;            // legacy vertical healthbar is disabled
            healthbar2 = false;           // default: false
            healthbartextsize = 16.0f;    // default: 16
            dist = true;                  // default: true
            visualMaxDist = 100.0f;       // default: 100m
            name = false;                 // name labels are no longer shown in the UI
            ult = true;                   // default: true
            draw_skel = false;            // default: incomplete skeleton overlay is opt-in
            draw_avatar = true;           // default: preserve box avatar overlay
            draw_hitbox = false;          // default: hitbox overlay is opt-in
            skillinfo = false;            // default: false
            ultimateDisplayMode = 1;      // default: left side panel
            skillDisplayMode = 0;         // default: Above head
            radarCorner = 0;              // default: Bottom Right
            radar = false;                // default: false
            radarline = false;            // default: false
            drawline = false;             // default: false
            draw_fov = false;             // default: false
            drawTrackingDeadzones = false; // default: false
            draw_hp_pack = false;         // default: false
            crosscircle = false;          // default: false
            eyeray = false;               // default: false
            MenuToggleKey = VK_HOME;      // default: VK_HOME
            lastConfigProfile = "config.ini";

            enargb = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);       // current box theme: visible fill red
            invisnenargb = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // current box theme: hidden fill white
            targetargb = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);   // current box theme: target fill yellow
            allyargb = ImVec4(0.0f, 0.0f, 1.0f, 0.0f);     // current box theme: ally fill transparent blue
            EnemyCol = ImVec4(1.0f, 0.0f, 0.0f, 0.0f);     // current box theme: transparent red outline
            fovcol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);       // default: 1,1,1,1
            aimFovRingStyles = MakeDefaultFovRingStyles(FovRingSlotKind::Aim);

            kmboxEnabled = false;         // default: disabled
            kmboxDeviceType = 0;          // default: Network/UDP
            CopyString(kmboxIp, kDefaultKmboxIp);
            kmboxPort = kDefaultKmboxPort;
            kmboxMonitorPort = kDefaultKmboxMonitorPort;
            kmboxMonitorPortManualOverride = false;
            CopyString(kmboxMac, kDefaultKmboxMac);
            CopyString(kmboxComPort, kDefaultKmboxComPort);
            kmboxCountsPerRadian = 100.0f;       // default: reference-project manual baseline
            gameMouseSensitivity = 15.0f;        // default manual/effective current game sensitivity
            referenceGameSensitivity = 15.0f;    // default calibration point
            autoScaleByGameSensitivity = false;  // default: manual counts-per-radian only
            autoReadGameMouseSensitivity = false; // default: preserve manual game sensitivity
            detectedGameMouseSensitivity = 0.0f;
            gameMouseSensitivityAutoDetected = false;
            calibratedCountsPerRadian = 0.0f;
            calibratedPitchCountsPerRadian = 0.0f;
            hostMouseDpi = kDefaultHostMouseDpi;
            detectedHostMouseDpi = 0.0f;
            hostMouseDpiAutoDetected = false;
            kmboxInputDelayMs = kDefaultKmboxInputDelayMs;
            kmboxDebugLog = false;        // default: off
            kmboxSuppressOutputWhileMenuOpen = false;

            manualScreenWidth = 1920;      // default: 1920 px
            manualScreenHeight = 1080;     // default: 1080 px

            locx = 0;                     // default: 0
            locy = 0;                     // default: 0
            therad = 0;                   // default: 0
            pon = 0;                      // default: 0
            crss = 0;                     // default: 0
        }

        void ApplyAimMode(int mode)
        {
            Tracking = false;
            Flick = false;

            switch (std::clamp(mode, 0, 1)) {
            case 0: Tracking = true; break;
            case 1: Flick = true; break;
            default: break;
            }
        }

        int CurrentAimMode()
        {
            if (Tracking) return 0;
            if (Flick) return 1;
            return 0;
        }

        HeroPreset MakeHeroPresetFromCurrentUnlocked()
        {
            HeroPreset preset{};
            preset.fov = Fov;
            preset.fovMode = ClampFovMode(aimbotFovMode);
            preset.dynamicFovPresetId = preset.fovMode == kFovModeDynamicPreset
                ? ClampDynamicFovPresetId(aimbotDynamicFovPresetId)
                : -1;
            preset.smooth = Smooth;
            if (preset.smooth <= 0.0f)
                preset.smooth = Tracking_smooth > 0.0f ? Tracking_smooth : Flick_smooth;
            preset.bone = NormalizeAimBone(Bone);
            preset.autoBone = autobone;
            preset.aimBoneMask = aimbotBoneMask;
            preset.hitbox = hitbox;
            preset.aimMode = CurrentAimMode();
            if (preset.aimMode < 0 || preset.aimMode > 1)
                preset.aimMode = 0;
            preset.aimBehavior = aimBehavior;
            preset.aimBehaviorPresetId = aimBehaviorPresetId;
            preset.aimMethod = AimBehaviorMethod(aimBehavior);
            preset.smoothType = aimbotSmoothType;
            preset.pidP = aimPidP;
            preset.pidI = aimPidI;
            preset.pidD = aimPidD;
            preset.pidMaxIntegral = aimPidMaxIntegral;
            preset.pidDeadzone = aimPidDeadzone;
            preset.bezierControlPoints = aimBezierControlPoints;
            preset.bezierCurvature = aimBezierCurvature;
            preset.bezierSpeed = aimBezierSpeed;
            preset.key = aim_key;
            preset.autoshot = aimbotAutoshot;
            preset.keepFiring = aimbotKeepFiring;
            preset.prediction = Prediction;
            preset.predictionMode = aimbotPredictionMode;
            preset.firePolicy = aimbotFirePolicy;
            preset.maxHeadDistance = aimbotMaxHead;
            preset.stickiness = aimbotStickiness;
            preset.pitchScale = aimbotPitchScale;
            preset.priority = aimbotPriority;
            preset.targetTeam = aimbotTeam;
            preset.maxAimTime = aimbotMaxAim;
            preset.minCharge = aimbotMinCharge;
            preset.maxCharge = aimbotMaxCharge;
            preset.ignoreInvisible = aimbotIgnoreInvisible;
            preset.traceCondition = aimbotTrace;
            preset.unlockCondition = aimbotUnlock;
            preset.lockTime = aimbotLockTime;
            preset.maxDistance = aimbotMaxDist;
            preset.minDistance = aimbotMinDist;
            preset.trackingDeadzone = aimbotTrackingDeadzone;
            preset.flickShotClampMs = aimbotFlickShotClampMs;
            preset.flickPostFireDelayMs = aimbotFlickPostFireDelayMs;
            preset.flickTrajectoryWait = aimbotFlickTrajectoryWait;
            preset.flickTrajectoryWaitMs = aimbotFlickTrajectoryWaitMs;
            preset.flickTrajectoryApexWindowMs = aimbotFlickTrajectoryApexWindowMs;
            preset.flick2ndTriggerGate = aimbotFlick2ndTriggerGate;
            preset.flick2ndBoxPadding = aimbotFlick2ndBoxPadding;
            preset.flick2ndInnerRadius = aimbotFlick2ndInnerRadius;
            preset.flick2ndInnerSmoothScale = aimbotFlick2ndInnerSmoothScale;
            preset.flick2ndInnerMethod = aimbotFlick2ndInnerMethod;
            preset.trigger.enabled = triggerbot;
            preset.trigger.action = aimbotAttack;
            preset.trigger.mode = triggerbotMode;
            preset.trigger.key = triggerbotKey;
            preset.trigger.shotInterval = triggerbotShotInterval;
            preset.trigger.chargeAware = triggerbotChargeAware;
            preset.trigger.minCharge = triggerbotMinCharge;
            preset.trigger.disableWhileReloading = triggerbotDisableWhileReloading;
            preset.trigger.ignoreInvisible = triggerbotIgnoreInvisible;
            preset.trigger.boneMask = triggerbotBoneMask;
            return ValidateHeroPresetValue(preset);
        }

        HeroPreset MakeHeroAimPresetFromCurrentUnlocked()
        {
            return MakeHeroPresetFromCurrentUnlocked();
        }

        HeroPreset MakeHeroTriggerPresetFromCurrentUnlocked()
        {
            return MakeHeroPresetFromCurrentUnlocked();
        }

        void ApplyHeroAimPresetUnlocked(const HeroPreset& rawPreset)
        {
            const HeroPreset preset = ValidateHeroPresetValue(rawPreset);
            Fov = preset.fov;
            minFov1 = preset.fov;
            aimbotFovMode = preset.fovMode;
            aimbotDynamicFovPresetId = preset.dynamicFovPresetId;
            Smooth = preset.smooth;
            Tracking_smooth = preset.smooth;
            Flick_smooth = preset.smooth;
            Bone = NormalizeAimBone(preset.bone);
            TargetBone = Bone;
            BoneName = AimBoneName(Bone);
            autobone = preset.autoBone;
            aimbotBoneMask = preset.aimBoneMask;
            hitbox = preset.hitbox;
            Prediction = preset.prediction;
            aimbotPredictionMode = preset.predictionMode;
            aimbotPriority = preset.priority;
            aimbotTeam = preset.targetTeam;
            aimbotAttack = preset.trigger.action;
            aimBehavior = preset.aimBehavior;
            aimBehaviorPresetId = preset.aimBehaviorPresetId;
            aimbotSmoothType = preset.smoothType;
            aim_key = preset.key;
            aimbotAutoshot = preset.autoshot;
            aimbotKeepFiring = preset.keepFiring;
            aimbotFirePolicy = preset.firePolicy;
            aimbotMaxHead = preset.maxHeadDistance;
            aimbotStickiness = preset.stickiness;
            aimbotPitchScale = preset.pitchScale;
            aimbotMaxAim = preset.maxAimTime;
            aimbotMinCharge = preset.minCharge;
            aimbotMaxCharge = preset.maxCharge;
            aimbotIgnoreInvisible = preset.ignoreInvisible;
            aimbotTrace = preset.traceCondition;
            aimbotUnlock = preset.unlockCondition;
            aimbotLockTime = preset.lockTime;
            aimbotMaxDist = preset.maxDistance;
            aimbotMinDist = preset.minDistance;
            aimbotTrackingDeadzone = preset.trackingDeadzone;
            aimbotFlickShotClampMs = preset.flickShotClampMs;
            aimbotFlickPostFireDelayMs = preset.flickPostFireDelayMs;
            aimbotFlickTrajectoryWait = preset.flickTrajectoryWait;
            aimbotFlickTrajectoryWaitMs = preset.flickTrajectoryWaitMs;
            aimbotFlickTrajectoryApexWindowMs = preset.flickTrajectoryApexWindowMs;
            aimbotFlick2ndTriggerGate = preset.flick2ndTriggerGate;
            aimbotFlick2ndBoxPadding = preset.flick2ndBoxPadding;
            aimbotFlick2ndInnerRadius = preset.flick2ndInnerRadius;
            aimbotFlick2ndInnerSmoothScale = preset.flick2ndInnerSmoothScale;
            aimbotFlick2ndInnerMethod = preset.flick2ndInnerMethod;
            aimbotTwoStage = IsFlick2ndBehavior(preset.aimBehavior);
            aimbotTwoStageTriggerGate = aimbotFlick2ndTriggerGate;
            aimbotTwoStageBoxPadding = aimbotFlick2ndBoxPadding;
            aimbotTwoStageInnerRadius = aimbotFlick2ndInnerRadius;
            aimbotTwoStageInnerSmoothScale = aimbotFlick2ndInnerSmoothScale;
            ApplyAimMode(IsTrackingBehavior(preset.aimBehavior) ? 0 : 1);
        }

        void ApplyHeroTriggerPresetUnlocked(const HeroPreset& rawPreset)
        {
            const HeroPreset preset = ValidateHeroPresetValue(rawPreset);
            triggerbot = preset.trigger.enabled;
            aimbotAttack = preset.trigger.action;
            triggerbotMode = preset.trigger.mode;
            triggerbotKey = preset.trigger.key;
            triggerbotShotInterval = preset.trigger.shotInterval;
            triggerbotChargeAware = preset.trigger.chargeAware;
            triggerbotMinCharge = preset.trigger.minCharge;
            triggerbotDisableWhileReloading = preset.trigger.disableWhileReloading;
            triggerbotIgnoreInvisible = preset.trigger.ignoreInvisible;
            triggerbotBoneMask = preset.trigger.boneMask;
            triggerbot2 = false;
            triggerbotToggleActive2 = false;
            hitbox = preset.hitbox;
            Prediction = preset.prediction;
            aimbotTeam = preset.targetTeam;
        }

        void ApplyHeroPresetUnlocked(const HeroPreset& rawPreset)
        {
            ApplyHeroAimPresetUnlocked(rawPreset);
            ApplyHeroTriggerPresetUnlocked(rawPreset);
        }

        HeroSlotPreset ReadHeroPresetSectionUnlocked(const IniFile& ini, const char* section,
                                                     HeroSlotPreset defaults, int slotIndex,
                                                     bool storedBoneUsesLegacyPresetIndex,
                                                     bool legacyFovApertureValues,
                                                     bool legacyAimBehaviorValues)
        {
            HeroSlotPreset slot = defaults;
            slot.name = NormalizeHeroSlotName(
                ReadString(ini, section, "name", DefaultHeroSlotName(slotIndex).c_str()),
                slotIndex);
            const bool hasPresentKey = KeyExists(ini, section, "present");
            slot.enabled = ReadBool(ini, section, "enabled", slot.enabled);
            slot.present = hasPresentKey
                ? ReadBool(ini, section, "present", slot.present)
                : slot.enabled;
            slot.scopeRequirement = NormalizeAimScopeRequirement(ReadInt(
                ini,
                section,
                "scopeRequirement",
                static_cast<int>(slot.scopeRequirement)));
            slot.preset.fov = ReadFov2Compat(ini, section, "fov", slot.preset.fov, legacyFovApertureValues);
            slot.preset.fovMode = ReadInt(ini, section, "fovMode", slot.preset.fovMode);
            slot.preset.dynamicFovPresetId = ReadInt(ini, section, "dynamicFovPresetId", slot.preset.dynamicFovPresetId);
            slot.preset.smooth = ReadFov2Compat(ini, section, "smooth", slot.preset.smooth);
            slot.preset.bone = ReadInt(ini, section, "bone", slot.preset.bone);
            if (storedBoneUsesLegacyPresetIndex)
                slot.preset.bone = LegacyPresetBoneToAimBone(slot.preset.bone);
            slot.preset.autoBone = ReadBool(ini, section, "autoBone", slot.preset.autoBone);
            slot.preset.aimBoneMask = KeyExists(ini, section, "aimBoneMask")
                ? static_cast<SkeletonBoneMask>(ReadInt(
                    ini, section, "aimBoneMask", static_cast<int>(slot.preset.aimBoneMask)))
                : AimBoneMaskFromLegacy(slot.preset.bone, slot.preset.autoBone);
            slot.preset.hitbox = ReadHitboxScaleCompat(
                ini,
                section,
                "hitboxScale",
                "hitbox",
                slot.preset.hitbox);
            slot.preset.aimMode = ReadInt(ini, section, "aimMode", slot.preset.aimMode);
            const bool hasAimBehavior = KeyExists(ini, section, "aimBehavior");
            slot.preset.aimBehavior = NormalizeAimBehaviorForLoad(ReadInt(
                ini,
                section,
                "aimBehavior",
                hasAimBehavior ? slot.preset.aimBehavior : slot.preset.aimMode),
                legacyAimBehaviorValues);
            slot.preset.aimBehaviorPresetId = ReadInt(
                ini,
                section,
                "aimBehaviorPresetId",
                slot.preset.aimBehaviorPresetId);
            slot.preset.aimMethod = ReadInt(ini, section, "aimMethod", slot.preset.aimMethod);
            slot.preset.smoothType = ReadInt(ini, section, "aimbotSmoothType", slot.preset.smoothType);
            slot.preset.pidP = ReadFixedFloat(ini, section, "aimPidP", slot.preset.pidP);
            slot.preset.pidI = ReadFixedFloat(ini, section, "aimPidI", slot.preset.pidI);
            slot.preset.pidD = ReadFixedFloat(ini, section, "aimPidD", slot.preset.pidD);
            slot.preset.pidMaxIntegral = ReadFixedFloat(ini, section, "aimPidMaxIntegral", slot.preset.pidMaxIntegral);
            slot.preset.pidDeadzone = ReadFixedFloat(ini, section, "aimPidDeadzone", slot.preset.pidDeadzone);
            slot.preset.bezierControlPoints = ReadInt(ini, section, "aimBezierControlPoints", slot.preset.bezierControlPoints);
            slot.preset.bezierCurvature = ReadFixedFloat(ini, section, "aimBezierCurvature", slot.preset.bezierCurvature);
            slot.preset.bezierSpeed = ReadFixedFloat(ini, section, "aimBezierSpeed", slot.preset.bezierSpeed);
            if (KeyExists(ini, section, "key"))
                slot.preset.key = ReadInt(ini, section, "key", slot.preset.key);
            else
                slot.preset.key = ReadInt(ini, section, "aim_key", slot.preset.key);
            slot.preset.autoshot = ReadBool(ini, section, "aimbotAutoshot", slot.preset.autoshot);
            slot.preset.keepFiring = ReadBool(ini, section, "aimbotKeepFiring", slot.preset.keepFiring);
            slot.preset.prediction = ReadBool(ini, section, "prediction", slot.preset.prediction);
            const bool hasPredictionMode = KeyExists(ini, section, "aimbotPredictionMode");
            slot.preset.predictionMode = ReadInt(
                ini,
                section,
                "aimbotPredictionMode",
                hasPredictionMode ? slot.preset.predictionMode : (slot.preset.prediction ? 1 : 2));
            slot.preset.firePolicy = ReadInt(ini, section, "aimbotFirePolicy", slot.preset.firePolicy);
            slot.preset.maxHeadDistance = ReadFixedFloat(ini, section, "aimbotMaxHead", slot.preset.maxHeadDistance);
            slot.preset.stickiness = ReadFixedFloat(ini, section, "aimbotStickiness", slot.preset.stickiness);
            slot.preset.pitchScale = ReadFixedFloat(ini, section, "aimbotPitchScale", slot.preset.pitchScale);
            slot.preset.priority = ReadInt(ini, section, "priority", slot.preset.priority);
            slot.preset.targetTeam = ReadInt(ini, section, "targetTeam", slot.preset.targetTeam);
            slot.preset.maxAimTime = ReadFixedFloat(ini, section, "aimbotMaxAim", slot.preset.maxAimTime);
            slot.preset.minCharge = ReadFixedFloat(ini, section, "aimbotMinCharge", slot.preset.minCharge);
            slot.preset.maxCharge = ReadFixedFloat(ini, section, "aimbotMaxCharge", slot.preset.maxCharge);
            slot.preset.ignoreInvisible = ReadBool(ini, section, "aimbotIgnoreInvisible", slot.preset.ignoreInvisible);
            slot.preset.traceCondition = ReadInt(ini, section, "aimbotTrace", slot.preset.traceCondition);
            slot.preset.unlockCondition = ReadInt(ini, section, "aimbotUnlock", slot.preset.unlockCondition);
            slot.preset.lockTime = ReadFixedFloat(ini, section, "aimbotLockTime", slot.preset.lockTime);
            slot.preset.maxDistance = ReadFixedFloat(ini, section, "aimbotMaxDist", slot.preset.maxDistance);
            slot.preset.minDistance = ReadFixedFloat(ini, section, "aimbotMinDist", slot.preset.minDistance);
            slot.preset.trackingDeadzone = ReadFixedFloat(ini, section, "trackingDeadzone", slot.preset.trackingDeadzone);
            slot.preset.flickShotClampMs = ReadFixedFloat(ini, section, "flickShotClampMs", slot.preset.flickShotClampMs);
            slot.preset.flickPostFireDelayMs = ReadFixedFloat(ini, section, "flickPostFireDelayMs", slot.preset.flickPostFireDelayMs);
            slot.preset.flickTrajectoryWait = ReadBool(ini, section, "flickTrajectoryWait", slot.preset.flickTrajectoryWait);
            slot.preset.flickTrajectoryWaitMs = ReadFixedFloat(ini, section, "flickTrajectoryWaitMs", slot.preset.flickTrajectoryWaitMs);
            slot.preset.flickTrajectoryApexWindowMs = ReadFixedFloat(ini, section, "flickTrajectoryApexWindowMs", slot.preset.flickTrajectoryApexWindowMs);
            slot.preset.flick2ndTriggerGate = ReadBool(ini, section, "flick2ndTriggerGate",
                ReadBool(ini, section, "aimbotTwoStageTriggerGate", slot.preset.flick2ndTriggerGate));
            slot.preset.flick2ndBoxPadding = ReadFixedFloat(ini, section, "flick2ndBoxPadding",
                ReadFixedFloat(ini, section, "aimbotTwoStageBoxPadding", slot.preset.flick2ndBoxPadding));
            slot.preset.flick2ndInnerRadius = ReadFixedFloat(ini, section, "flick2ndInnerRadius",
                ReadFixedFloat(ini, section, "aimbotTwoStageInnerRadius", slot.preset.flick2ndInnerRadius));
            slot.preset.flick2ndInnerSmoothScale = ReadFixedFloat(ini, section, "flick2ndInnerSmoothScale",
                ReadFixedFloat(ini, section, "aimbotTwoStageInnerSmoothScale", slot.preset.flick2ndInnerSmoothScale));
            slot.preset.flick2ndInnerMethod = ReadInt(ini, section, "flick2ndInnerMethod", slot.preset.flick2ndInnerMethod);
            if (KeyExists(ini, section, "aimbotTwoStage") &&
                ReadBool(ini, section, "aimbotTwoStage", false) &&
                IsFlickBehavior(slot.preset.aimBehavior)) {
                slot.preset.aimBehavior = kAimBehaviorFlick2nd;
            }
            slot.preset.trigger.enabled = ReadBool(ini, section, "triggerEnabled", slot.preset.trigger.enabled);
            slot.preset.trigger.action = ReadInt(ini, section, "triggerAction", slot.preset.trigger.action);
            slot.preset.trigger.mode = ReadInt(ini, section, "triggerMode", slot.preset.trigger.mode);
            slot.preset.trigger.key = ReadInt(ini, section, "triggerKey", slot.preset.trigger.key);
            slot.preset.trigger.shotInterval = ReadFixedFloat(ini, section, "triggerShotInterval", slot.preset.trigger.shotInterval);
            slot.preset.trigger.chargeAware = ReadBool(ini, section, "triggerChargeAware", slot.preset.trigger.chargeAware);
            slot.preset.trigger.minCharge = ReadFixedFloat(ini, section, "triggerMinCharge", slot.preset.trigger.minCharge);
            slot.preset.trigger.disableWhileReloading = ReadBool(
                ini,
                section,
                "triggerDisableWhileReloading",
                slot.preset.trigger.disableWhileReloading);
            slot.preset.trigger.ignoreInvisible = ReadBool(ini, section, "triggerIgnoreInvisible", slot.preset.trigger.ignoreInvisible);
            slot.preset.trigger.boneMask = static_cast<TriggerBoneMask>(
                ReadInt(ini, section, "triggerBoneMask", static_cast<int>(slot.preset.trigger.boneMask)));
            slot.preset.trigger.drawHitbox = ReadBool(ini, section, "triggerDrawHitbox", slot.preset.trigger.drawHitbox);
            slot.preset = ValidateHeroPresetValue(slot.preset);
            return slot;
        }

        HeroPreset ReadLegacyHeroPresetSectionUnlocked(const IniFile& ini,
                                                       const char* section,
                                                       HeroPreset defaults,
                                                       bool legacyFovApertureValues,
                                                       bool legacyAimBehaviorValues)
        {
            HeroPreset preset = defaults;
            preset.fov = ReadFov2Compat(ini, section, "FOV", preset.fov, legacyFovApertureValues);
            preset.fovMode = ReadInt(ini, section, "fovMode", preset.fovMode);
            preset.dynamicFovPresetId = ReadInt(ini, section, "dynamicFovPresetId", preset.dynamicFovPresetId);
            const int aimMode = std::clamp(ReadInt(ini, section, "Aim Mode", preset.aimMode), 0, 1);
            const float trackingSmooth = ReadFixedFloat(ini, section, "Tracking_smooth", preset.smooth);
            const float flickSmooth = ReadFixedFloat(ini, section, "Flick_smooth", trackingSmooth);
            preset.smooth = aimMode == 1 ? flickSmooth : trackingSmooth;
            preset.bone = NormalizeAimBone(ReadInt(ini, section, "Bone", preset.bone));
            preset.autoBone = ReadBool(ini, section, "autobone", preset.autoBone);
            preset.aimBoneMask = KeyExists(ini, section, "aimBoneMask")
                ? static_cast<SkeletonBoneMask>(ReadInt(
                    ini, section, "aimBoneMask", static_cast<int>(preset.aimBoneMask)))
                : AimBoneMaskFromLegacy(preset.bone, preset.autoBone);
            preset.hitbox = ReadHitboxScaleCompat(
                ini,
                section,
                "hitboxScale",
                "hitbox",
                preset.hitbox);
            preset.aimMode = aimMode;
            preset.aimBehavior = NormalizeAimBehaviorForLoad(
                ReadInt(ini, section, "aimBehavior", preset.aimMode),
                legacyAimBehaviorValues);
            preset.aimBehaviorPresetId = ReadInt(
                ini,
                section,
                "aimBehaviorPresetId",
                preset.aimBehaviorPresetId);
            preset.aimMethod = ReadInt(ini, section, "aimMethod", preset.aimMethod);
            preset.smoothType = ReadInt(ini, section, "aimbotSmoothType", preset.smoothType);
            preset.pidP = ReadFixedFloat(ini, section, "aimPidP", preset.pidP);
            preset.pidI = ReadFixedFloat(ini, section, "aimPidI", preset.pidI);
            preset.pidD = ReadFixedFloat(ini, section, "aimPidD", preset.pidD);
            preset.pidMaxIntegral = ReadFixedFloat(ini, section, "aimPidMaxIntegral", preset.pidMaxIntegral);
            preset.pidDeadzone = ReadFixedFloat(ini, section, "aimPidDeadzone", preset.pidDeadzone);
            preset.bezierControlPoints = ReadInt(ini, section, "aimBezierControlPoints", preset.bezierControlPoints);
            preset.bezierCurvature = ReadFixedFloat(ini, section, "aimBezierCurvature", preset.bezierCurvature);
            preset.bezierSpeed = ReadFixedFloat(ini, section, "aimBezierSpeed", preset.bezierSpeed);
            preset.key = ReadInt(ini, section, "aim_key", preset.key);
            preset.autoshot = ReadBool(ini, section, "aimbotAutoshot", preset.autoshot);
            preset.keepFiring = ReadBool(ini, section, "aimbotKeepFiring", preset.keepFiring);
            preset.prediction = ReadBool(ini, section, "predictdec", preset.prediction);
            preset.predictionMode = ReadInt(ini, section, "aimbotPredictionMode", preset.prediction ? 1 : 2);
            preset.firePolicy = ReadInt(ini, section, "aimbotFirePolicy", preset.firePolicy);
            preset.maxHeadDistance = ReadFixedFloat(ini, section, "aimbotMaxHead", preset.maxHeadDistance);
            preset.stickiness = ReadFixedFloat(ini, section, "aimbotStickiness", preset.stickiness);
            preset.pitchScale = ReadFixedFloat(ini, section, "aimbotPitchScale", preset.pitchScale);
            preset.targetTeam = ReadInt(ini, section, "aimbotTeam", preset.targetTeam);
            preset.maxAimTime = ReadFixedFloat(ini, section, "aimbotMaxAim", preset.maxAimTime);
            preset.minCharge = ReadFixedFloat(ini, section, "aimbotMinCharge", preset.minCharge);
            preset.maxCharge = ReadFixedFloat(ini, section, "aimbotMaxCharge", preset.maxCharge);
            preset.ignoreInvisible = ReadBool(ini, section, "aimbotIgnoreInvisible", preset.ignoreInvisible);
            preset.traceCondition = ReadInt(ini, section, "aimbotTrace", preset.traceCondition);
            preset.unlockCondition = ReadInt(ini, section, "aimbotUnlock", preset.unlockCondition);
            preset.lockTime = ReadFixedFloat(ini, section, "aimbotLockTime", preset.lockTime);
            preset.maxDistance = ReadFixedFloat(ini, section, "aimbotMaxDist", preset.maxDistance);
            preset.minDistance = ReadFixedFloat(ini, section, "aimbotMinDist", preset.minDistance);
            preset.trackingDeadzone = ReadFixedFloat(ini, section, "trackingDeadzone", preset.trackingDeadzone);
            preset.flickShotClampMs = ReadFixedFloat(ini, section, "flickShotClampMs", preset.flickShotClampMs);
            preset.flickPostFireDelayMs = ReadFixedFloat(ini, section, "flickPostFireDelayMs", preset.flickPostFireDelayMs);
            preset.flickTrajectoryWait = ReadBool(ini, section, "flickTrajectoryWait", preset.flickTrajectoryWait);
            preset.flickTrajectoryWaitMs = ReadFixedFloat(ini, section, "flickTrajectoryWaitMs", preset.flickTrajectoryWaitMs);
            preset.flickTrajectoryApexWindowMs = ReadFixedFloat(ini, section, "flickTrajectoryApexWindowMs", preset.flickTrajectoryApexWindowMs);
            preset.flick2ndTriggerGate = ReadBool(ini, section, "flick2ndTriggerGate",
                ReadBool(ini, section, "aimbotTwoStageTriggerGate", preset.flick2ndTriggerGate));
            preset.flick2ndBoxPadding = ReadFixedFloat(ini, section, "flick2ndBoxPadding",
                ReadFixedFloat(ini, section, "aimbotTwoStageBoxPadding", preset.flick2ndBoxPadding));
            preset.flick2ndInnerRadius = ReadFixedFloat(ini, section, "flick2ndInnerRadius",
                ReadFixedFloat(ini, section, "aimbotTwoStageInnerRadius", preset.flick2ndInnerRadius));
            preset.flick2ndInnerSmoothScale = ReadFixedFloat(ini, section, "flick2ndInnerSmoothScale",
                ReadFixedFloat(ini, section, "aimbotTwoStageInnerSmoothScale", preset.flick2ndInnerSmoothScale));
            preset.flick2ndInnerMethod = ReadInt(ini, section, "flick2ndInnerMethod", preset.flick2ndInnerMethod);
            if (KeyExists(ini, section, "aimbotTwoStage") &&
                ReadBool(ini, section, "aimbotTwoStage", false) &&
                IsFlickBehavior(preset.aimBehavior)) {
                preset.aimBehavior = kAimBehaviorFlick2nd;
            }
            preset.trigger.enabled = ReadBool(ini, section, "triggerbot", preset.trigger.enabled);
            preset.trigger.action = ReadInt(ini, section, "aimbotAttack", preset.trigger.action);
            preset.trigger.ignoreInvisible = ReadBool(ini, section, "triggerIgnoreInvisible", preset.trigger.ignoreInvisible);
            preset.trigger.boneMask = static_cast<TriggerBoneMask>(
                ReadInt(ini, section, "triggerBoneMask", static_cast<int>(preset.trigger.boneMask)));
            return ValidateHeroPresetValue(preset);
        }

        void WriteHeroPresetSection(const std::string& path, const char* section,
                                    const HeroSlotPreset& rawSlot, int slotIndex)
        {
            const std::string name = NormalizeHeroSlotName(rawSlot.name, slotIndex);
            const HeroPreset preset = ValidateHeroPresetValue(rawSlot.preset);
            WriteBoolValue(path, section, "present", rawSlot.present);
            WriteStringValue(path, section, "name", name.c_str());
            WriteBoolValue(path, section, "enabled", rawSlot.enabled);
            WriteIntValue(path, section, "scopeRequirement", static_cast<int>(
                NormalizeAimScopeRequirement(static_cast<int>(rawSlot.scopeRequirement))));
            WritePlainFloatValue(path, section, "fov", preset.fov);
            WriteIntValue(path, section, "fovMode", preset.fovMode);
            WriteIntValue(path, section, "dynamicFovPresetId", preset.dynamicFovPresetId);
            WritePlainFloatValue(path, section, "smooth", preset.smooth);
            WriteIntValue(path, section, "bone", preset.bone);
            WriteBoolValue(path, section, "autoBone", preset.autoBone);
            WriteIntValue(path, section, "aimBoneMask", static_cast<int>(preset.aimBoneMask));
            WritePlainFloatValue(path, section, "hitboxScale", preset.hitbox);
            WriteIntValue(path, section, "aimMode", preset.aimMode);
            WriteIntValue(path, section, "aimBehavior", preset.aimBehavior);
            WriteIntValue(path, section, "aimBehaviorPresetId", preset.aimBehaviorPresetId);
            WriteIntValue(path, section, "key", preset.key);
            WriteBoolValue(path, section, "aimbotAutoshot", preset.autoshot);
            WriteBoolValue(path, section, "aimbotKeepFiring", preset.keepFiring);
            const std::string prediction = ToText(preset.prediction);
            WriteStringValue(path, section, "prediction", prediction.c_str());
            WriteIntValue(path, section, "aimbotPredictionMode", preset.predictionMode);
            WriteIntValue(path, section, "aimbotFirePolicy", preset.firePolicy);
            WriteFixedFloatValue(path, section, "aimbotMaxHead", preset.maxHeadDistance);
            WriteFixedFloatValue(path, section, "aimbotStickiness", preset.stickiness);
            WriteFixedFloatValue(path, section, "aimbotPitchScale", preset.pitchScale);
            WriteIntValue(path, section, "priority", preset.priority);
            WriteIntValue(path, section, "targetTeam", preset.targetTeam);
            WriteFixedFloatValue(path, section, "aimbotMaxAim", preset.maxAimTime);
            WriteFixedFloatValue(path, section, "aimbotMinCharge", preset.minCharge);
            WriteFixedFloatValue(path, section, "aimbotMaxCharge", preset.maxCharge);
            WriteBoolValue(path, section, "aimbotIgnoreInvisible", preset.ignoreInvisible);
            WriteIntValue(path, section, "aimbotTrace", preset.traceCondition);
            WriteIntValue(path, section, "aimbotUnlock", preset.unlockCondition);
            WriteFixedFloatValue(path, section, "aimbotLockTime", preset.lockTime);
            WriteFixedFloatValue(path, section, "aimbotMaxDist", preset.maxDistance);
            WriteFixedFloatValue(path, section, "aimbotMinDist", preset.minDistance);
            WriteFixedFloatValue(path, section, "trackingDeadzone", preset.trackingDeadzone);
            WriteFixedFloatValue(path, section, "flickShotClampMs", preset.flickShotClampMs);
            WriteFixedFloatValue(path, section, "flickPostFireDelayMs", preset.flickPostFireDelayMs);
            WriteBoolValue(path, section, "flickTrajectoryWait", preset.flickTrajectoryWait);
            WriteFixedFloatValue(path, section, "flickTrajectoryWaitMs", preset.flickTrajectoryWaitMs);
            WriteFixedFloatValue(path, section, "flickTrajectoryApexWindowMs", preset.flickTrajectoryApexWindowMs);
            WriteBoolValue(path, section, "flick2ndTriggerGate", preset.flick2ndTriggerGate);
            WriteFixedFloatValue(path, section, "flick2ndBoxPadding", preset.flick2ndBoxPadding);
            WriteFixedFloatValue(path, section, "flick2ndInnerRadius", preset.flick2ndInnerRadius);
            WriteFixedFloatValue(path, section, "flick2ndInnerSmoothScale", preset.flick2ndInnerSmoothScale);
            WriteIntValue(path, section, "flick2ndInnerMethod", preset.flick2ndInnerMethod);
            WriteBoolValue(path, section, "triggerEnabled", preset.trigger.enabled);
            WriteIntValue(path, section, "triggerAction", preset.trigger.action);
            WriteIntValue(path, section, "triggerMode", preset.trigger.mode);
            WriteIntValue(path, section, "triggerKey", preset.trigger.key);
            WriteFixedFloatValue(path, section, "triggerShotInterval", preset.trigger.shotInterval);
            WriteBoolValue(path, section, "triggerChargeAware", preset.trigger.chargeAware);
            WriteFixedFloatValue(path, section, "triggerMinCharge", preset.trigger.minCharge);
            WriteBoolValue(path, section, "triggerDisableWhileReloading", preset.trigger.disableWhileReloading);
            WriteBoolValue(path, section, "triggerIgnoreInvisible", preset.trigger.ignoreInvisible);
            WriteIntValue(path, section, "triggerBoneMask", static_cast<int>(preset.trigger.boneMask));
            WriteBoolValue(path, section, "triggerDrawHitbox", preset.trigger.drawHitbox);
        }

        template <typename Allocator>
        void AddJsonString(rapidjson::Value& object, const char* key, const std::string& value, Allocator& allocator)
        {
            rapidjson::Value jsonKey;
            jsonKey.SetString(key, allocator);
            rapidjson::Value jsonValue;
            jsonValue.SetString(value.c_str(), static_cast<rapidjson::SizeType>(value.size()), allocator);
            object.AddMember(jsonKey, jsonValue, allocator);
        }

        template <typename Allocator>
        void AddJsonBool(rapidjson::Value& object, const char* key, bool value, Allocator& allocator)
        {
            rapidjson::Value jsonKey;
            jsonKey.SetString(key, allocator);
            object.AddMember(jsonKey, value, allocator);
        }

        template <typename Allocator>
        void AddJsonInt(rapidjson::Value& object, const char* key, int value, Allocator& allocator)
        {
            rapidjson::Value jsonKey;
            jsonKey.SetString(key, allocator);
            object.AddMember(jsonKey, value, allocator);
        }

        template <typename Allocator>
        void AddJsonFloat(rapidjson::Value& object, const char* key, float value, Allocator& allocator)
        {
            rapidjson::Value jsonKey;
            jsonKey.SetString(key, allocator);
            object.AddMember(jsonKey, value, allocator);
        }

        template <typename Allocator>
        rapidjson::Value TriggerPresetToJson(const TriggerPreset& rawPreset, Allocator& allocator)
        {
            const TriggerPreset preset = ValidateTriggerPresetValue(rawPreset);
            rapidjson::Value value(rapidjson::kObjectType);
            AddJsonBool(value, "enabled", preset.enabled, allocator);
            AddJsonInt(value, "action", preset.action, allocator);
            AddJsonInt(value, "mode", preset.mode, allocator);
            AddJsonInt(value, "key", preset.key, allocator);
            AddJsonFloat(value, "shotInterval", preset.shotInterval, allocator);
            AddJsonBool(value, "chargeAware", preset.chargeAware, allocator);
            AddJsonFloat(value, "minCharge", preset.minCharge, allocator);
            AddJsonBool(value, "disableWhileReloading", preset.disableWhileReloading, allocator);
            AddJsonBool(value, "ignoreInvisible", preset.ignoreInvisible, allocator);
            AddJsonInt(value, "boneMask", static_cast<int>(preset.boneMask), allocator);
            AddJsonBool(value, "drawHitbox", preset.drawHitbox, allocator);
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroPresetToJson(const HeroPreset& rawPreset, Allocator& allocator)
        {
            const HeroPreset preset = ValidateHeroPresetValue(rawPreset);
            rapidjson::Value value(rapidjson::kObjectType);
            AddJsonFloat(value, "fov", preset.fov, allocator);
            AddJsonInt(value, "fovMode", preset.fovMode, allocator);
            AddJsonInt(value, "dynamicFovPresetId", preset.dynamicFovPresetId, allocator);
            AddJsonFloat(value, "smooth", preset.smooth, allocator);
            AddJsonInt(value, "bone", preset.bone, allocator);
            AddJsonBool(value, "autoBone", preset.autoBone, allocator);
            AddJsonInt(value, "aimBoneMask", static_cast<int>(preset.aimBoneMask), allocator);
            AddJsonFloat(value, "hitboxScale", preset.hitbox, allocator);
            AddJsonInt(value, "aimMode", preset.aimMode, allocator);
            AddJsonInt(value, "aimBehavior", preset.aimBehavior, allocator);
            AddJsonInt(value, "aimBehaviorPresetId", preset.aimBehaviorPresetId, allocator);
            AddJsonInt(value, "key", preset.key, allocator);
            AddJsonBool(value, "autoshot", preset.autoshot, allocator);
            AddJsonBool(value, "keepFiring", preset.keepFiring, allocator);
            AddJsonBool(value, "prediction", preset.prediction, allocator);
            AddJsonInt(value, "predictionMode", preset.predictionMode, allocator);
            AddJsonInt(value, "firePolicy", preset.firePolicy, allocator);
            AddJsonFloat(value, "maxHeadDistance", preset.maxHeadDistance, allocator);
            AddJsonFloat(value, "stickiness", preset.stickiness, allocator);
            AddJsonFloat(value, "pitchScale", preset.pitchScale, allocator);
            AddJsonInt(value, "priority", preset.priority, allocator);
            AddJsonInt(value, "targetTeam", preset.targetTeam, allocator);
            AddJsonFloat(value, "maxAimTime", preset.maxAimTime, allocator);
            AddJsonFloat(value, "minCharge", preset.minCharge, allocator);
            AddJsonFloat(value, "maxCharge", preset.maxCharge, allocator);
            AddJsonBool(value, "ignoreInvisible", preset.ignoreInvisible, allocator);
            AddJsonInt(value, "traceCondition", preset.traceCondition, allocator);
            AddJsonInt(value, "unlockCondition", preset.unlockCondition, allocator);
            AddJsonFloat(value, "lockTime", preset.lockTime, allocator);
            AddJsonFloat(value, "maxDistance", preset.maxDistance, allocator);
            AddJsonFloat(value, "minDistance", preset.minDistance, allocator);
            AddJsonFloat(value, "trackingDeadzone", preset.trackingDeadzone, allocator);
            AddJsonFloat(value, "flickShotClampMs", preset.flickShotClampMs, allocator);
            AddJsonFloat(value, "flickPostFireDelayMs", preset.flickPostFireDelayMs, allocator);
            AddJsonBool(value, "flickTrajectoryWait", preset.flickTrajectoryWait, allocator);
            AddJsonFloat(value, "flickTrajectoryWaitMs", preset.flickTrajectoryWaitMs, allocator);
            AddJsonFloat(value, "flickTrajectoryApexWindowMs", preset.flickTrajectoryApexWindowMs, allocator);
            AddJsonBool(value, "flick2ndTriggerGate", preset.flick2ndTriggerGate, allocator);
            AddJsonFloat(value, "flick2ndBoxPadding", preset.flick2ndBoxPadding, allocator);
            AddJsonFloat(value, "flick2ndInnerRadius", preset.flick2ndInnerRadius, allocator);
            AddJsonFloat(value, "flick2ndInnerSmoothScale", preset.flick2ndInnerSmoothScale, allocator);
            AddJsonInt(value, "flick2ndInnerMethod", preset.flick2ndInnerMethod, allocator);
            rapidjson::Value trigger = TriggerPresetToJson(preset.trigger, allocator);
            rapidjson::Value triggerKey;
            triggerKey.SetString("trigger", allocator);
            value.AddMember(triggerKey, trigger, allocator);
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSlotToJson(const HeroSlotPreset& rawSlot, int slotIndex, Allocator& allocator)
        {
            HeroSlotPreset slot = rawSlot;
            slot.name = NormalizeHeroSlotName(slot.name, slotIndex);
            slot.preset = ValidateHeroPresetValue(slot.preset);

            rapidjson::Value value(rapidjson::kObjectType);
            AddJsonString(value, "name", slot.name, allocator);
            AddJsonBool(value, "present", slot.present, allocator);
            AddJsonBool(value, "enabled", slot.enabled, allocator);
            AddJsonInt(value, "scopeRequirement", static_cast<int>(
                NormalizeAimScopeRequirement(static_cast<int>(slot.scopeRequirement))), allocator);
            rapidjson::Value preset = HeroPresetToJson(slot.preset, allocator);
            rapidjson::Value presetKey;
            presetKey.SetString("preset", allocator);
            value.AddMember(presetKey, preset, allocator);
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSlotsToJson(const std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                         Allocator& allocator)
        {
            rapidjson::Value heroObject(rapidjson::kObjectType);
            rapidjson::Value slotArray(rapidjson::kArrayType);
            for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                rapidjson::Value slot = HeroSlotToJson(
                    slots[static_cast<size_t>(slotIndex)],
                    slotIndex,
                    allocator);
                slotArray.PushBack(slot, allocator);
            }

            rapidjson::Value slotsKey;
            slotsKey.SetString("slots", allocator);
            heroObject.AddMember(slotsKey, slotArray, allocator);
            return heroObject;
        }

        template <typename Allocator>
        rapidjson::Value HeroPresetStoreToJson(const HeroPresetStore& store, Allocator& allocator)
        {
            rapidjson::Value value(rapidjson::kObjectType);
            for (const auto& item : store) {
                const std::string heroKeyText = ToText(item.first);
                rapidjson::Value heroKey;
                heroKey.SetString(heroKeyText.c_str(),
                                  static_cast<rapidjson::SizeType>(heroKeyText.size()),
                                  allocator);
                rapidjson::Value heroSlots = HeroSlotsToJson(item.second, allocator);
                value.AddMember(heroKey, heroSlots, allocator);
            }
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSkillSequenceStepToJson(const HeroSkillSequenceStep& step, Allocator& allocator)
        {
            rapidjson::Value value(rapidjson::kObjectType);
            AddJsonInt(value, "buttonMask", step.buttonMask, allocator);
            AddJsonInt(value, "durationMs", step.durationMs, allocator);
            AddJsonFloat(value, "speedScale", step.speedScale, allocator);
            AddJsonInt(value, "jitterMs", step.jitterMs, allocator);
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSkillSettingsToJson(const HeroSkillSettings& rawSettings, Allocator& allocator)
        {
            const HeroSkillSettings settings = ValidateHeroSkillSettingsValue(rawSettings);
            rapidjson::Value value(rapidjson::kObjectType);
            AddJsonBool(value, "enabled", settings.enabled, allocator);
            AddJsonInt(value, "key", settings.key, allocator);
            AddJsonInt(value, "skillKey", settings.skillKey, allocator);
            AddJsonFloat(value, "healthThreshold", settings.healthThreshold, allocator);
            AddJsonFloat(value, "enemyHealthThreshold", settings.enemyHealthThreshold, allocator);
            AddJsonFloat(value, "allyHealthThreshold", settings.allyHealthThreshold, allocator);
            AddJsonFloat(value, "distance", settings.distance, allocator);
            AddJsonInt(value, "mode", settings.mode, allocator);
            AddJsonFloat(value, "cooldown", settings.cooldown, allocator);
            AddJsonBool(value, "cooldownGuard", settings.cooldownGuard, allocator);
            AddJsonBool(value, "prediction", settings.prediction, allocator);
            AddJsonInt(value, "minTargets", settings.minTargets, allocator);
            AddJsonFloat(value, "radius", settings.radius, allocator);

            rapidjson::Value sequenceSteps(rapidjson::kArrayType);
            for (const HeroSkillSequenceStep& step : settings.sequenceSteps)
                sequenceSteps.PushBack(HeroSkillSequenceStepToJson(step, allocator), allocator);
            rapidjson::Value sequenceStepsKey;
            sequenceStepsKey.SetString("sequenceSteps", allocator);
            value.AddMember(sequenceStepsKey, sequenceSteps, allocator);

            AddJsonInt(value, "trackingAimBehavior", settings.tracking.aimBehavior, allocator);
            AddJsonInt(value, "trackingAimBehaviorPresetId", settings.tracking.aimBehaviorPresetId, allocator);
            AddJsonFloat(value, "trackingSpeedScale", settings.tracking.speedScale, allocator);
            AddJsonInt(value, "trackingMethod", settings.tracking.method, allocator);
            AddJsonFloat(value, "trackingSmooth", settings.tracking.smooth, allocator);
            AddJsonFloat(value, "trackingFov", settings.tracking.fov, allocator);
            AddJsonInt(value, "trackingBone", settings.tracking.bone, allocator);
            AddJsonFloat(value, "trackingHitboxScale", settings.tracking.hitbox, allocator);
            AddJsonInt(value, "pitchDownDurationMs", settings.pitchDownDurationMs, allocator);
            AddJsonFloat(value, "pitchDownDurationJitter", settings.pitchDownDurationJitter, allocator);
            AddJsonFloat(value, "pitchDownTargetAngle", settings.pitchDownTargetAngle, allocator);
            AddJsonFloat(value, "pitchUpOffsetJitter", settings.pitchUpOffsetJitter, allocator);
            AddJsonInt(value, "fireDelayMs", settings.fireDelayMs, allocator);
            AddJsonInt(value, "jumpKeyCode", settings.jumpKeyCode, allocator);
            AddJsonBool(value, "ammoGuard", settings.ammoGuard, allocator);
            AddJsonInt(value, "ammoGuardReserve", settings.ammoGuardReserve, allocator);
            AddJsonFloat(value, "projectileSpeed", settings.projectileSpeed, allocator);
            AddJsonFloat(value, "projectileRadius", settings.projectileRadius, allocator);
            AddJsonBool(value, "projectileGravity", settings.projectileGravity, allocator);
            AddJsonFloat(value, "preFireDelayMs", settings.preFireDelayMs, allocator);
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSkillMapToJson(const std::unordered_map<std::string, HeroSkillSettings>& skills,
                                            Allocator& allocator)
        {
            rapidjson::Value value(rapidjson::kObjectType);
            for (const auto& skill : skills) {
                if (skill.first.empty())
                    continue;

                rapidjson::Value skillKey;
                skillKey.SetString(skill.first.c_str(),
                                   static_cast<rapidjson::SizeType>(skill.first.size()),
                                   allocator);
                rapidjson::Value settings = HeroSkillSettingsToJson(skill.second, allocator);
                value.AddMember(skillKey, settings, allocator);
            }
            return value;
        }

        template <typename Allocator>
        rapidjson::Value HeroSkillPresetStoreToJson(const HeroSkillPresetStore& store, Allocator& allocator)
        {
            rapidjson::Value value(rapidjson::kObjectType);
            for (const auto& item : store) {
                if (item.first == 0 || item.second.empty())
                    continue;

                const std::string heroKeyText = ToText(item.first);
                rapidjson::Value heroKey;
                heroKey.SetString(heroKeyText.c_str(),
                                  static_cast<rapidjson::SizeType>(heroKeyText.size()),
                                  allocator);
                rapidjson::Value heroSkills = HeroSkillMapToJson(item.second, allocator);
                value.AddMember(heroKey, heroSkills, allocator);
            }
            return value;
        }

        bool WriteJsonDocument(const std::string& path, const rapidjson::Document& document)
        {
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            writer.SetIndent(' ', 2);
            if (!document.Accept(writer)) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Failed to serialize hero config JSON for %s.", path.c_str());
                return false;
            }

            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Failed to open hero config JSON for write: %s.", path.c_str());
                return false;
            }

            file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
            return file.good();
        }

        bool LoadJsonDocument(const std::string& path, rapidjson::Document& document)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                return false;

            std::ostringstream buffer;
            buffer << file.rdbuf();
            const std::string text = buffer.str();
            document.Parse(text.c_str(), text.size());
            if (document.HasParseError() || !document.IsObject()) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "Ignoring malformed hero config JSON: %s.", path.c_str());
                return false;
            }
            return true;
        }

        rapidjson::Value* FindOrAddObjectMember(rapidjson::Document& document, const char* key)
        {
            auto& allocator = document.GetAllocator();
            auto member = document.FindMember(key);
            if (member != document.MemberEnd()) {
                if (!member->value.IsObject())
                    member->value.SetObject();
                return &member->value;
            }

            rapidjson::Value jsonKey;
            jsonKey.SetString(key, allocator);
            rapidjson::Value value(rapidjson::kObjectType);
            document.AddMember(jsonKey, value, allocator);
            member = document.FindMember(key);
            return member != document.MemberEnd() ? &member->value : nullptr;
        }

        void UpsertJsonMember(rapidjson::Value& object,
                              const std::string& keyText,
                              rapidjson::Value&& value,
                              rapidjson::Document::AllocatorType& allocator)
        {
            rapidjson::Value key;
            key.SetString(keyText.c_str(), static_cast<rapidjson::SizeType>(keyText.size()), allocator);
            auto member = object.FindMember(key);
            if (member != object.MemberEnd()) {
                member->value = value;
                return;
            }
            object.AddMember(key, value, allocator);
        }

        void AddHeroConfigMetadata(rapidjson::Document& document)
        {
            auto& allocator = document.GetAllocator();
            auto version = document.FindMember("version");
            if (version != document.MemberEnd())
                version->value.SetInt(kCurrentHeroConfigVersion);
            else
                AddJsonInt(document, "version", kCurrentHeroConfigVersion, allocator);

            auto profile = document.FindMember("profile");
            if (profile != document.MemberEnd()) {
                profile->value.SetString(configFileName.c_str(),
                                         static_cast<rapidjson::SizeType>(configFileName.size()),
                                         allocator);
            } else {
                AddJsonString(document, "profile", configFileName, allocator);
            }
        }

        void SaveHeroPresetsUnlocked(const std::string& heroConfigPath)
        {
            ValidateHeroPresetsUnlocked();
            ValidateHeroSkillPresetsUnlocked();

            rapidjson::Document document;
            document.SetObject();
            auto& allocator = document.GetAllocator();
            AddHeroConfigMetadata(document);

            rapidjson::Value aimStore = HeroPresetStoreToJson(heroAimPresets, allocator);
            rapidjson::Value triggerStore = HeroPresetStoreToJson(heroTriggerPresets, allocator);
            rapidjson::Value skillStore = HeroSkillPresetStoreToJson(heroSkillPresets, allocator);
            rapidjson::Value aimKey;
            aimKey.SetString("heroAimPresets", allocator);
            rapidjson::Value triggerKey;
            triggerKey.SetString("heroTriggerPresets", allocator);
            rapidjson::Value skillKey;
            skillKey.SetString("heroSkillPresets", allocator);
            document.AddMember(aimKey, aimStore, allocator);
            document.AddMember(triggerKey, triggerStore, allocator);
            document.AddMember(skillKey, skillStore, allocator);

            WriteJsonDocument(heroConfigPath, document);
        }

        void EnsureCurrentHeroEntryForSaveUnlocked(uint64_t heroId)
        {
            if (heroId == 0)
                return;

            auto ensureStore = [&](HeroPresetSlotKind kind) {
                auto& store = PresetStore(kind);
                auto item = store.find(heroId);
                if (item != store.end()) {
                    NormalizeHeroPresetSlots(item->second, heroId, kind);
                    return;
                }

                auto& slots = store[heroId];
                InitializeBasicHeroPresetSlots(slots, heroId, kind);
            };

            ensureStore(HeroPresetSlotKind::Aim);
            ensureStore(HeroPresetSlotKind::Trigger);
        }

        void SaveHeroConfigForHeroUnlocked(const std::string& heroConfigPath, uint64_t heroId)
        {
            if (heroId == 0) {
                SaveHeroPresetsUnlocked(heroConfigPath);
                return;
            }

            EnsureCurrentHeroEntryForSaveUnlocked(heroId);
            ValidateHeroPresetsUnlocked();
            ValidateHeroSkillPresetsUnlocked();

            rapidjson::Document document;
            if (!LoadJsonDocument(heroConfigPath, document))
                document.SetObject();
            AddHeroConfigMetadata(document);

            auto& allocator = document.GetAllocator();
            rapidjson::Value* aimObject = FindOrAddObjectMember(document, "heroAimPresets");
            rapidjson::Value* triggerObject = FindOrAddObjectMember(document, "heroTriggerPresets");
            rapidjson::Value* skillObject = FindOrAddObjectMember(document, "heroSkillPresets");
            const std::string heroKey = ToText(heroId);

            if (aimObject) {
                const auto item = heroAimPresets.find(heroId);
                if (item != heroAimPresets.end()) {
                    rapidjson::Value heroSlots = HeroSlotsToJson(item->second, allocator);
                    UpsertJsonMember(*aimObject, heroKey, std::move(heroSlots), allocator);
                }
            }

            if (triggerObject) {
                const auto item = heroTriggerPresets.find(heroId);
                if (item != heroTriggerPresets.end()) {
                    rapidjson::Value heroSlots = HeroSlotsToJson(item->second, allocator);
                    UpsertJsonMember(*triggerObject, heroKey, std::move(heroSlots), allocator);
                }
            }

            if (skillObject) {
                const auto item = heroSkillPresets.find(heroId);
                if (item != heroSkillPresets.end()) {
                    rapidjson::Value heroSkills = HeroSkillMapToJson(item->second, allocator);
                    UpsertJsonMember(*skillObject, heroKey, std::move(heroSkills), allocator);
                }
            }

            WriteJsonDocument(heroConfigPath, document);
        }

        void SaveHeroSkillConfigUnlocked(const std::string& heroConfigPath)
        {
            ValidateHeroSkillPresetsUnlocked();

            rapidjson::Document document;
            if (!LoadJsonDocument(heroConfigPath, document))
                document.SetObject();
            AddHeroConfigMetadata(document);

            auto& allocator = document.GetAllocator();
            rapidjson::Value* skillObject = FindOrAddObjectMember(document, "heroSkillPresets");
            if (skillObject) {
                rapidjson::Value skillStore = HeroSkillPresetStoreToJson(heroSkillPresets, allocator);
                skillObject->Swap(skillStore);
            }

            WriteJsonDocument(heroConfigPath, document);
        }

        bool TryLoadLegacyPresetForDefinition(const IniFile& ini,
                                              const HeroPresetDefinition& def,
                                              HeroPreset defaults,
                                              bool legacyFovApertureValues,
                                              bool legacyAimBehaviorValues,
                                              HeroPreset& outPreset)
        {
            if (def.legacyName && SectionExists(ini, def.legacyName)) {
                outPreset = ReadLegacyHeroPresetSectionUnlocked(
                    ini,
                    def.legacyName,
                    defaults,
                    legacyFovApertureValues,
                    legacyAimBehaviorValues);
                return true;
            }
            if (def.legacyAlias && SectionExists(ini, def.legacyAlias)) {
                outPreset = ReadLegacyHeroPresetSectionUnlocked(
                    ini,
                    def.legacyAlias,
                    defaults,
                    legacyFovApertureValues,
                    legacyAimBehaviorValues);
                return true;
            }
            return false;
        }

        bool ReadJsonBool(const rapidjson::Value& object, const char* key, bool def)
        {
            const auto item = object.FindMember(key);
            return item != object.MemberEnd() && item->value.IsBool()
                ? item->value.GetBool()
                : def;
        }

        int ReadJsonInt(const rapidjson::Value& object, const char* key, int def)
        {
            const auto item = object.FindMember(key);
            return item != object.MemberEnd() && item->value.IsInt()
                ? item->value.GetInt()
                : def;
        }

        float ReadJsonFloat(const rapidjson::Value& object, const char* key, float def)
        {
            const auto item = object.FindMember(key);
            return item != object.MemberEnd() && item->value.IsNumber()
                ? item->value.GetFloat()
                : def;
        }

        float ReadJsonFov(const rapidjson::Value& object,
                          const char* key,
                          float def,
                          bool legacyApertureValue)
        {
            const auto item = object.FindMember(key);
            if (item == object.MemberEnd() || !item->value.IsNumber())
                return def;
            const float value = item->value.GetFloat();
            return legacyApertureValue
                ? DecodeFovDeg(value, true, "HeroConfig", key)
                : value;
        }

        float ReadJsonHitboxScaleCompat(const rapidjson::Value& object,
                                        const char* scaleKey,
                                        const char* legacyRadiusKey,
                                        float def)
        {
            const auto scale = object.FindMember(scaleKey);
            if (scale != object.MemberEnd() && scale->value.IsNumber())
                return ClampHitboxScalePercent(scale->value.GetFloat());

            const auto legacy = object.FindMember(legacyRadiusKey);
            if (legacy != object.MemberEnd() && legacy->value.IsNumber())
                return LegacyHitboxRadiusToScalePercent(legacy->value.GetFloat());

            return ClampHitboxScalePercent(def);
        }

        std::string ReadJsonString(const rapidjson::Value& object, const char* key, const std::string& def)
        {
            const auto item = object.FindMember(key);
            return item != object.MemberEnd() && item->value.IsString()
                ? std::string(item->value.GetString(), item->value.GetStringLength())
                : def;
        }

        HeroSkillSequenceStep ReadHeroSkillSequenceStepJson(const rapidjson::Value& value,
                                                            HeroSkillSequenceStep defaults)
        {
            if (!value.IsObject())
                return defaults;

            defaults.buttonMask = ReadJsonInt(value, "buttonMask", defaults.buttonMask);
            defaults.durationMs = ReadJsonInt(value, "durationMs", defaults.durationMs);
            defaults.speedScale = ReadJsonFloat(value, "speedScale", defaults.speedScale);
            defaults.jitterMs = ReadJsonInt(value, "jitterMs", defaults.jitterMs);
            return defaults;
        }

        HeroSkillSettings ReadHeroSkillSettingsJson(const rapidjson::Value& value,
                                                    HeroSkillSettings defaults,
                                                    bool legacyFovApertureValues)
        {
            if (!value.IsObject())
                return ValidateHeroSkillSettingsValue(defaults);

            defaults.enabled = ReadJsonBool(value, "enabled", defaults.enabled);
            const auto keyItem = value.FindMember("key");
            defaults.key = keyItem != value.MemberEnd()
                ? ReadJsonInt(value, "key", defaults.key)
                : ReadJsonInt(value, "activationKey", defaults.key);
            defaults.skillKey = ReadJsonInt(value, "skillKey", defaults.skillKey);
            defaults.healthThreshold = ReadJsonFloat(value, "healthThreshold", defaults.healthThreshold);
            defaults.enemyHealthThreshold = ReadJsonFloat(value, "enemyHealthThreshold", defaults.enemyHealthThreshold);
            defaults.allyHealthThreshold = ReadJsonFloat(value, "allyHealthThreshold", defaults.allyHealthThreshold);
            defaults.distance = ReadJsonFloat(value, "distance", defaults.distance);
            defaults.mode = ReadJsonInt(value, "mode", defaults.mode);
            defaults.cooldown = ReadJsonFloat(value, "cooldown", defaults.cooldown);
            defaults.cooldownGuard = ReadJsonBool(value, "cooldownGuard", defaults.cooldownGuard);
            defaults.prediction = ReadJsonBool(value, "prediction", defaults.prediction);
            defaults.minTargets = ReadJsonInt(value, "minTargets", defaults.minTargets);
            defaults.radius = ReadJsonFloat(value, "radius", defaults.radius);

            const auto sequenceSteps = value.FindMember("sequenceSteps");
            if (sequenceSteps != value.MemberEnd() && sequenceSteps->value.IsArray()) {
                defaults.sequenceSteps.clear();
                const rapidjson::SizeType count = (std::min)(
                    sequenceSteps->value.Size(),
                    static_cast<rapidjson::SizeType>(kMaxHeroSkillSequenceSteps));
                defaults.sequenceSteps.reserve(static_cast<size_t>(count));
                for (rapidjson::SizeType index = 0; index < count; ++index) {
                    defaults.sequenceSteps.push_back(
                        ReadHeroSkillSequenceStepJson(sequenceSteps->value[index], HeroSkillSequenceStep{}));
                }
            }

            defaults.tracking.aimBehavior = ClampAimBehaviorIndex(
                ReadJsonInt(value, "trackingAimBehavior", defaults.tracking.aimBehavior));
            defaults.tracking.aimBehaviorPresetId = ReadJsonInt(
                value,
                "trackingAimBehaviorPresetId",
                defaults.tracking.aimBehaviorPresetId);
            defaults.tracking.speedScale = ReadJsonFloat(
                value,
                "trackingSpeedScale",
                defaults.tracking.speedScale);
            defaults.tracking.method = ReadJsonInt(value, "trackingMethod", defaults.tracking.method);
            defaults.tracking.smooth = ReadJsonFloat(value, "trackingSmooth", defaults.tracking.smooth);
            defaults.tracking.fov = ReadJsonFov(
                value,
                "trackingFov",
                defaults.tracking.fov,
                legacyFovApertureValues);
            defaults.tracking.bone = ReadJsonInt(value, "trackingBone", defaults.tracking.bone);
            defaults.tracking.hitbox = ReadJsonHitboxScaleCompat(
                value,
                "trackingHitboxScale",
                "trackingHitbox",
                defaults.tracking.hitbox);
            defaults.pitchDownDurationMs = ReadJsonInt(value, "pitchDownDurationMs", defaults.pitchDownDurationMs);
            defaults.pitchDownDurationJitter = ReadJsonFloat(value, "pitchDownDurationJitter", defaults.pitchDownDurationJitter);
            defaults.pitchDownTargetAngle = ReadJsonFloat(value, "pitchDownTargetAngle", defaults.pitchDownTargetAngle);
            defaults.pitchUpOffsetJitter = ReadJsonFloat(value, "pitchUpOffsetJitter", defaults.pitchUpOffsetJitter);
            defaults.fireDelayMs = ReadJsonInt(value, "fireDelayMs", defaults.fireDelayMs);
            defaults.jumpKeyCode = ReadJsonInt(value, "jumpKeyCode", defaults.jumpKeyCode);
            defaults.ammoGuard = ReadJsonBool(value, "ammoGuard", defaults.ammoGuard);
            defaults.ammoGuardReserve = ReadJsonInt(value, "ammoGuardReserve", defaults.ammoGuardReserve);
            defaults.projectileSpeed = ReadJsonFloat(value, "projectileSpeed", defaults.projectileSpeed);
            defaults.projectileRadius = ReadJsonFloat(value, "projectileRadius", defaults.projectileRadius);
            defaults.projectileGravity = ReadJsonBool(value, "projectileGravity", defaults.projectileGravity);
            defaults.preFireDelayMs = ReadJsonFloat(value, "preFireDelayMs", defaults.preFireDelayMs);
            return ValidateHeroSkillSettingsValue(defaults);
        }

        std::vector<HeroSkillSequenceStep> MakeMeasuredAsheFirePatternSteps()
        {
            return {
                { 0x01,  49, 1.0f, 0 },
                { 0x02, 140, 1.0f, 0 },
                { 0x03,  70, 1.0f, 0 },
                { 0x02,  70, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },
                { 0x01,  49, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },

                { 0x01,  49, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },
                { 0x01,  49, 1.0f, 0 },
                { 0x00,  45, 1.0f, 0 },
                { 0x02, 140, 1.0f, 0 },
                { 0x03,  70, 1.0f, 0 },
                { 0x02,  70, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },

                { 0x01,  49, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },
                { 0x01,  49, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },

                { 0x01,  49, 1.0f, 0 },
                { 0x00,  47, 1.0f, 0 },
                { 0x02, 140, 1.0f, 0 },
                { 0x03,  70, 1.0f, 0 },
                { 0x02,  70, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },
                { 0x01,  49, 1.0f, 0 },
                { 0x00, 183, 1.0f, 0 },
                { 0x01,  49, 1.0f, 0 },
            };
        }

        bool MatchesAsheTenStepFirePatternDefault(const std::vector<HeroSkillSequenceStep>& steps)
        {
            constexpr std::pair<int, int> semantic[] = {
                { 0x01,  49 },
                { 0x00, 182 },
                { 0x02, 245 },
                { 0x03,  70 },
                { 0x02, 210 },
                { 0x00, 154 },
                { 0x01,  49 },
                { 0x00, 182 },
                { 0x01,  49 },
                { 0x00, 300 },
            };

            if (steps.size() != std::size(semantic))
                return false;

            for (size_t index = 0; index < std::size(semantic); ++index) {
                if (steps[index].buttonMask != semantic[index].first ||
                    steps[index].durationMs != semantic[index].second) {
                    return false;
                }
            }

            return true;
        }

        bool MatchesPreviousAsheFirePatternDefault(const std::vector<HeroSkillSequenceStep>& steps)
        {
            constexpr std::pair<int, int> previous[] = {
                { 0x01,  60 },
                { 0x03,   5 },
                { 0x02, 220 },
                { 0x03,  62 },
                { 0x02,  47 },
                { 0x00, 142 },
                { 0x01,  75 },
                { 0x00, 147 },
            };

            if (steps.size() != std::size(previous))
                return false;

            for (size_t index = 0; index < std::size(previous); ++index) {
                if (steps[index].buttonMask != previous[index].first ||
                    steps[index].durationMs != previous[index].second) {
                    return false;
                }
            }

            return true;
        }

        bool MatchesLegacyFiveStepAsheFirePatternDefault(const std::vector<HeroSkillSequenceStep>& steps)
        {
            constexpr std::pair<int, int> legacy[] = {
                { 0x01,   5 },
                { 0x02, 190 },
                { 0x03,   5 },
                { 0x00, 119 },
                { 0x00, 119 },
            };

            if (steps.size() != std::size(legacy))
                return false;

            for (size_t index = 0; index < std::size(legacy); ++index) {
                if (steps[index].buttonMask != legacy[index].first ||
                    steps[index].durationMs != legacy[index].second) {
                    return false;
                }
            }

            return true;
        }

        bool MatchesMeasuredAsheFirePattern(const std::vector<HeroSkillSequenceStep>& steps)
        {
            const std::vector<HeroSkillSequenceStep> measured = MakeMeasuredAsheFirePatternSteps();
            if (steps.size() != measured.size())
                return false;

            for (size_t index = 0; index < measured.size(); ++index) {
                if (steps[index].buttonMask != measured[index].buttonMask ||
                    steps[index].durationMs != measured[index].durationMs) {
                    return false;
                }
            }

            return true;
        }

        bool SequenceJsonMissingTimingMetadata(const rapidjson::Value& value)
        {
            if (!value.IsObject())
                return false;

            const auto sequenceSteps = value.FindMember("sequenceSteps");
            if (sequenceSteps == value.MemberEnd() || !sequenceSteps->value.IsArray())
                return false;

            for (const rapidjson::Value& step : sequenceSteps->value.GetArray()) {
                if (!step.IsObject())
                    continue;

                if (step.FindMember("speedScale") == step.MemberEnd() ||
                    step.FindMember("jitterMs") == step.MemberEnd()) {
                    return true;
                }
            }

            return false;
        }

        bool ShouldMigrateAsheFirePatternSequence(uint64_t heroId,
                                                  const std::string& skillId,
                                                  const rapidjson::Value& value,
                                                  const HeroSkillSettings& settings)
        {
            if (heroId != static_cast<uint64_t>(OW::eHero::HERO_ASHE) || skillId != "fire-pattern")
                return false;

            if (MatchesMeasuredAsheFirePattern(settings.sequenceSteps))
                return false;

            return SequenceJsonMissingTimingMetadata(value) ||
                MatchesLegacyFiveStepAsheFirePatternDefault(settings.sequenceSteps) ||
                MatchesAsheTenStepFirePatternDefault(settings.sequenceSteps) ||
                MatchesPreviousAsheFirePatternDefault(settings.sequenceSteps);
        }

        bool ShouldRestoreAsheFirePatternAimAssistDefault(uint64_t heroId,
                                                          const std::string& skillId,
                                                          const rapidjson::Value& value,
                                                          const HeroSkillSettings& settings)
        {
            if (heroId != static_cast<uint64_t>(OW::eHero::HERO_ASHE) || skillId != "fire-pattern")
                return false;

            if (!MatchesMeasuredAsheFirePattern(settings.sequenceSteps))
                return false;

            const bool hasNewAimAssistKeys =
                value.FindMember("trackingAimBehavior") != value.MemberEnd() ||
                value.FindMember("trackingSpeedScale") != value.MemberEnd();
            return !hasNewAimAssistKeys && settings.tracking.fov <= 0.001f;
        }

        bool ShouldMigrateAsheFirePatternTrackingStability(
            int heroConfigVersion,
            uint64_t heroId,
            const std::string& skillId,
            const HeroSkillSettings& settings)
        {
            if (heroConfigVersion >= kAsheFirePatternStableTrackingVersion ||
                heroId != static_cast<uint64_t>(OW::eHero::HERO_ASHE) ||
                skillId != "fire-pattern" ||
                !MatchesMeasuredAsheFirePattern(settings.sequenceSteps)) {
                return false;
            }

            // Only rewrite the exact legacy aim-assist default. Customized
            // behavior, method, FOV, bone, hitbox, or speed remains untouched.
            return settings.tracking.aimBehavior == kAimBehaviorTracking &&
                settings.tracking.aimBehaviorPresetId < 0 &&
                settings.tracking.method == 0 &&
                SameFloatForDefaultMigration(settings.tracking.smooth, 0.0f) &&
                SameFloatForDefaultMigration(settings.tracking.speedScale, 100.0f) &&
                SameFloatForDefaultMigration(settings.tracking.fov, kDefaultFovDeg) &&
                settings.tracking.bone == kAimBoneHead &&
                SameFloatForDefaultMigration(
                    settings.tracking.hitbox,
                    kDefaultHitboxScalePercent);
        }

        bool IsProjectileAimSkill(uint64_t heroId, const std::string& skillId)
        {
            return (heroId == static_cast<uint64_t>(OW::eHero::HERO_ANA) && skillId == "sleep-dart") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_ROADHOG) && skillId == "chain-hook") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_TRACER) && skillId == "pulse-bomb") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_SOLDIER76) && skillId == "helix-rockets") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_ECHO) && skillId == "sticky-bombs") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_BRIGITTE) && skillId == "whip-shot") ||
                (heroId == static_cast<uint64_t>(OW::eHero::HERO_SIGMA) && skillId == "accretion");
        }

        HeroSkillSettings ProjectileAimSkillDefaults(uint64_t heroId, const std::string& skillId)
        {
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_ANA) && skillId == "sleep-dart")
                return OW::MakeAnaSleepDartDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_ROADHOG) && skillId == "chain-hook")
                return OW::MakeRoadhogChainHookDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_TRACER) && skillId == "pulse-bomb")
                return OW::MakeTracerPulseBombDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_SOLDIER76) && skillId == "helix-rockets")
                return OW::MakeSoldierHelixRocketsDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_ECHO) && skillId == "sticky-bombs")
                return OW::MakeEchoStickyBombsDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_BRIGITTE) && skillId == "whip-shot")
                return OW::MakeBrigitteWhipShotDefaults();
            if (heroId == static_cast<uint64_t>(OW::eHero::HERO_SIGMA) && skillId == "accretion")
                return OW::MakeSigmaAccretionDefaults();
            return HeroSkillSettings{};
        }

        bool ShouldMigrateRoadhogChainHookHealthThreshold(int heroConfigVersion,
                                                          uint64_t heroId,
                                                          const std::string& skillId,
                                                          const HeroSkillSettings& settings)
        {
            if (heroConfigVersion >= kRoadhogChainHookFullHealthDefaultVersion)
                return false;
            if (heroId != static_cast<uint64_t>(OW::eHero::HERO_ROADHOG) || skillId != "chain-hook")
                return false;
            return std::fabs(settings.enemyHealthThreshold - 50.0f) <= 0.001f;
        }

        bool ShouldMigrateAnaSleepDartHealthThreshold(int heroConfigVersion,
                                                      uint64_t heroId,
                                                      const std::string& skillId,
                                                      const HeroSkillSettings& settings)
        {
            if (heroConfigVersion >= kAnaSleepDartFullHealthDefaultVersion)
                return false;
            if (heroId != static_cast<uint64_t>(OW::eHero::HERO_ANA) || skillId != "sleep-dart")
                return false;
            return std::fabs(settings.enemyHealthThreshold - 50.0f) <= 0.001f;
        }

        bool RepairBadAnaAbilityKeyLayout(int heroConfigVersion,
                                          uint64_t heroId,
                                          const std::string& skillId,
                                          HeroSkillSettings& settings)
        {
            if (heroConfigVersion != kBadAnaAbilityKeyLayoutVersion)
                return false;
            if (heroId != static_cast<uint64_t>(OW::eHero::HERO_ANA))
                return false;

            bool migrated = false;
            if (skillId == "sleep-dart") {
                if (settings.skillKey == OW::HeroSkillHotkey::EKey) {
                    settings.skillKey = OW::HeroSkillHotkey::LeftShift;
                    migrated = true;
                }
            } else if (skillId == "biotic-grenade") {
                if (settings.key == OW::HeroSkillHotkey::LeftShift) {
                    settings.key = OW::HeroSkillHotkey::EKey;
                    migrated = true;
                }
                if (settings.skillKey == OW::HeroSkillHotkey::LeftShift) {
                    settings.skillKey = OW::HeroSkillHotkey::EKey;
                    migrated = true;
                }
            }

            return migrated;
        }

        bool ShouldRestoreProjectileAimDefaults(uint64_t heroId,
                                                const std::string& skillId,
                                                const rapidjson::Value& value)
        {
            if (!IsProjectileAimSkill(heroId, skillId))
                return false;
            if (!value.IsObject())
                return true;

            return value.FindMember("trackingAimBehavior") == value.MemberEnd() ||
                value.FindMember("projectileSpeed") == value.MemberEnd() ||
                value.FindMember("preFireDelayMs") == value.MemberEnd();
        }

        TriggerPreset ReadTriggerPresetJson(const rapidjson::Value& value, TriggerPreset defaults)
        {
            if (!value.IsObject())
                return ValidateTriggerPresetValue(defaults);

            defaults.enabled = ReadJsonBool(value, "enabled", defaults.enabled);
            defaults.action = ReadJsonInt(value, "action", defaults.action);
            defaults.mode = ReadJsonInt(value, "mode", defaults.mode);
            defaults.key = ReadJsonInt(value, "key", defaults.key);
            defaults.shotInterval = ReadJsonFloat(value, "shotInterval", defaults.shotInterval);
            defaults.chargeAware = ReadJsonBool(value, "chargeAware", defaults.chargeAware);
            defaults.minCharge = ReadJsonFloat(value, "minCharge", defaults.minCharge);
            defaults.disableWhileReloading = ReadJsonBool(
                value,
                "disableWhileReloading",
                defaults.disableWhileReloading);
            defaults.ignoreInvisible = ReadJsonBool(value, "ignoreInvisible", defaults.ignoreInvisible);
            defaults.boneMask = static_cast<TriggerBoneMask>(
                ReadJsonInt(value, "boneMask", static_cast<int>(defaults.boneMask)));
            defaults.drawHitbox = ReadJsonBool(value, "drawHitbox", defaults.drawHitbox);
            return ValidateTriggerPresetValue(defaults);
        }

        HeroPreset ReadHeroPresetJson(const rapidjson::Value& value,
                                      HeroPreset defaults,
                                      bool legacyFovApertureValues,
                                      bool legacyAimBehaviorValues)
        {
            if (!value.IsObject())
                return ValidateHeroPresetValue(defaults);

            defaults.fov = ReadJsonFov(value, "fov", defaults.fov, legacyFovApertureValues);
            defaults.fovMode = ReadJsonInt(value, "fovMode", defaults.fovMode);
            defaults.dynamicFovPresetId = ReadJsonInt(value, "dynamicFovPresetId", defaults.dynamicFovPresetId);
            defaults.smooth = ReadJsonFloat(value, "smooth", defaults.smooth);
            defaults.bone = ReadJsonInt(value, "bone", defaults.bone);
            defaults.autoBone = ReadJsonBool(value, "autoBone", defaults.autoBone);
            const auto aimBoneMask = value.FindMember("aimBoneMask");
            defaults.aimBoneMask = aimBoneMask != value.MemberEnd() && aimBoneMask->value.IsInt()
                ? static_cast<SkeletonBoneMask>(aimBoneMask->value.GetInt())
                : AimBoneMaskFromLegacy(defaults.bone, defaults.autoBone);
            defaults.hitbox = ReadJsonHitboxScaleCompat(
                value,
                "hitboxScale",
                "hitbox",
                defaults.hitbox);
            defaults.aimMode = ReadJsonInt(value, "aimMode", defaults.aimMode);
            const bool hasAimBehavior = value.HasMember("aimBehavior");
            defaults.aimBehavior = NormalizeAimBehaviorForLoad(
                ReadJsonInt(value, "aimBehavior", hasAimBehavior ? defaults.aimBehavior : defaults.aimMode),
                legacyAimBehaviorValues);
            defaults.aimBehaviorPresetId = ReadJsonInt(
                value,
                "aimBehaviorPresetId",
                defaults.aimBehaviorPresetId);
            defaults.aimMethod = ReadJsonInt(value, "aimMethod", defaults.aimMethod);
            defaults.smoothType = ReadJsonInt(value, "smoothType", defaults.smoothType);
            defaults.pidP = ReadJsonFloat(value, "pidP", defaults.pidP);
            defaults.pidI = ReadJsonFloat(value, "pidI", defaults.pidI);
            defaults.pidD = ReadJsonFloat(value, "pidD", defaults.pidD);
            defaults.pidMaxIntegral = ReadJsonFloat(value, "pidMaxIntegral", defaults.pidMaxIntegral);
            defaults.pidDeadzone = ReadJsonFloat(value, "pidDeadzone", defaults.pidDeadzone);
            defaults.bezierControlPoints = ReadJsonInt(value, "bezierControlPoints", defaults.bezierControlPoints);
            defaults.bezierCurvature = ReadJsonFloat(value, "bezierCurvature", defaults.bezierCurvature);
            defaults.bezierSpeed = ReadJsonFloat(value, "bezierSpeed", defaults.bezierSpeed);
            defaults.key = ReadJsonInt(value, "key", defaults.key);
            defaults.autoshot = ReadJsonBool(value, "autoshot", defaults.autoshot);
            defaults.keepFiring = ReadJsonBool(value, "keepFiring", defaults.keepFiring);
            defaults.prediction = ReadJsonBool(value, "prediction", defaults.prediction);
            const bool hasPredictionMode = value.HasMember("predictionMode");
            defaults.predictionMode = ReadJsonInt(
                value,
                "predictionMode",
                hasPredictionMode ? defaults.predictionMode : (defaults.prediction ? 1 : 2));
            defaults.firePolicy = ReadJsonInt(value, "firePolicy", defaults.firePolicy);
            defaults.maxHeadDistance = ReadJsonFloat(value, "maxHeadDistance", defaults.maxHeadDistance);
            defaults.stickiness = ReadJsonFloat(value, "stickiness", defaults.stickiness);
            defaults.pitchScale = ReadJsonFloat(value, "pitchScale", defaults.pitchScale);
            defaults.priority = ReadJsonInt(value, "priority", defaults.priority);
            defaults.targetTeam = ReadJsonInt(value, "targetTeam", defaults.targetTeam);
            defaults.maxAimTime = ReadJsonFloat(value, "maxAimTime", defaults.maxAimTime);
            defaults.minCharge = ReadJsonFloat(value, "minCharge", defaults.minCharge);
            defaults.maxCharge = ReadJsonFloat(value, "maxCharge", defaults.maxCharge);
            defaults.ignoreInvisible = ReadJsonBool(value, "ignoreInvisible", defaults.ignoreInvisible);
            defaults.traceCondition = ReadJsonInt(value, "traceCondition", defaults.traceCondition);
            defaults.unlockCondition = ReadJsonInt(value, "unlockCondition", defaults.unlockCondition);
            defaults.lockTime = ReadJsonFloat(value, "lockTime", defaults.lockTime);
            defaults.maxDistance = ReadJsonFloat(value, "maxDistance", defaults.maxDistance);
            defaults.minDistance = ReadJsonFloat(value, "minDistance", defaults.minDistance);
            defaults.trackingDeadzone = ReadJsonFloat(value, "trackingDeadzone", defaults.trackingDeadzone);
            defaults.flickShotClampMs = ReadJsonFloat(value, "flickShotClampMs", defaults.flickShotClampMs);
            defaults.flickPostFireDelayMs = ReadJsonFloat(value, "flickPostFireDelayMs", defaults.flickPostFireDelayMs);
            defaults.flickTrajectoryWait = ReadJsonBool(value, "flickTrajectoryWait", defaults.flickTrajectoryWait);
            defaults.flickTrajectoryWaitMs = ReadJsonFloat(value, "flickTrajectoryWaitMs", defaults.flickTrajectoryWaitMs);
            defaults.flickTrajectoryApexWindowMs = ReadJsonFloat(value, "flickTrajectoryApexWindowMs", defaults.flickTrajectoryApexWindowMs);
            defaults.flick2ndTriggerGate = ReadJsonBool(value, "flick2ndTriggerGate", defaults.flick2ndTriggerGate);
            defaults.flick2ndBoxPadding = ReadJsonFloat(value, "flick2ndBoxPadding", defaults.flick2ndBoxPadding);
            defaults.flick2ndInnerRadius = ReadJsonFloat(value, "flick2ndInnerRadius", defaults.flick2ndInnerRadius);
            defaults.flick2ndInnerSmoothScale = ReadJsonFloat(value, "flick2ndInnerSmoothScale", defaults.flick2ndInnerSmoothScale);
            defaults.flick2ndInnerMethod = ReadJsonInt(value, "flick2ndInnerMethod", defaults.flick2ndInnerMethod);
            const auto trigger = value.FindMember("trigger");
            if (trigger != value.MemberEnd())
                defaults.trigger = ReadTriggerPresetJson(trigger->value, defaults.trigger);
            return ValidateHeroPresetValue(defaults);
        }

        HeroSlotPreset ReadHeroSlotJson(const rapidjson::Value& value,
                                        HeroSlotPreset defaults,
                                        int slotIndex,
                                        bool legacyFovApertureValues,
                                        bool legacyAimBehaviorValues)
        {
            if (!value.IsObject())
                return defaults;

            defaults.name = NormalizeHeroSlotName(
                ReadJsonString(value, "name", DefaultHeroSlotName(slotIndex)),
                slotIndex);
            defaults.present = ReadJsonBool(value, "present", defaults.present);
            defaults.enabled = ReadJsonBool(value, "enabled", defaults.enabled);
            defaults.scopeRequirement = NormalizeAimScopeRequirement(ReadJsonInt(
                value,
                "scopeRequirement",
                static_cast<int>(defaults.scopeRequirement)));
            const auto preset = value.FindMember("preset");
            if (preset != value.MemberEnd())
                defaults.preset = ReadHeroPresetJson(
                    preset->value,
                    defaults.preset,
                    legacyFovApertureValues,
                    legacyAimBehaviorValues);
            defaults.preset = ValidateHeroPresetValue(defaults.preset);
            return defaults;
        }

        void LoadHeroPresetStoreJson(const rapidjson::Value& storeObject,
                                     HeroPresetSlotKind kind,
                                     HeroPresetStore& store,
                                     bool legacyFovApertureValues,
                                     bool legacyAimBehaviorValues)
        {
            if (!storeObject.IsObject())
                return;

            for (auto hero = storeObject.MemberBegin(); hero != storeObject.MemberEnd(); ++hero) {
                if (!hero->name.IsString() || !hero->value.IsObject())
                    continue;

                uint64_t heroId = 0;
                if (!ParseStrictUint64(hero->name.GetString(), heroId) || heroId == 0)
                    continue;

                std::array<HeroSlotPreset, kHeroPresetSlotCount> slots{};
                InitializeHeroPresetSlots(slots, 0, false);

                const auto slotArray = hero->value.FindMember("slots");
                if (slotArray != hero->value.MemberEnd() && slotArray->value.IsArray()) {
                    const rapidjson::SizeType count = (std::min)(
                        slotArray->value.Size(),
                        static_cast<rapidjson::SizeType>(kHeroPresetSlotCount));
                    for (rapidjson::SizeType index = 0; index < count; ++index) {
                        slots[static_cast<size_t>(index)].preset = MakeHeroPresetFromCurrentUnlocked();
                        slots[static_cast<size_t>(index)] = ReadHeroSlotJson(
                            slotArray->value[index],
                            slots[static_cast<size_t>(index)],
                            static_cast<int>(index),
                            legacyFovApertureValues,
                            legacyAimBehaviorValues);
                    }
                }

                NormalizeHeroPresetSlots(slots, heroId, kind);
                store[heroId] = slots;
            }
        }

        void LoadHeroSkillPresetStoreJson(const rapidjson::Value& storeObject,
                                          int heroConfigVersion,
                                          bool legacyFovApertureValues)
        {
            if (!storeObject.IsObject())
                return;

            for (auto hero = storeObject.MemberBegin(); hero != storeObject.MemberEnd(); ++hero) {
                if (!hero->name.IsString() || !hero->value.IsObject())
                    continue;

                uint64_t heroId = 0;
                if (!ParseStrictUint64(hero->name.GetString(), heroId) || heroId == 0)
                    continue;

                auto& skills = heroSkillPresets[heroId];
                for (auto skill = hero->value.MemberBegin(); skill != hero->value.MemberEnd(); ++skill) {
                    if (!skill->name.IsString())
                        continue;

                    const std::string skillId(skill->name.GetString(), skill->name.GetStringLength());
                    if (skillId.empty())
                        continue;

                    const bool hasSkillKey = skill->value.IsObject() &&
                        skill->value.FindMember("skillKey") != skill->value.MemberEnd();
                    HeroSkillSettings settings = ReadHeroSkillSettingsJson(
                        skill->value,
                        HeroSkillSettings{},
                        legacyFovApertureValues);
                    if (!hasSkillKey &&
                        heroId == static_cast<uint64_t>(OW::eHero::HERO_ZARYA) &&
                        skillId == "propel-jump") {
                        settings.skillKey = OW::HeroSkillHotkey::RightMouse;
                        settings = ValidateHeroSkillSettingsValue(settings);
                    }
                    if (IsProjectileAimSkill(heroId, skillId)) {
                        const HeroSkillSettings defaults = ProjectileAimSkillDefaults(heroId, skillId);
                        if (!hasSkillKey ||
                            (settings.skillKey == settings.key &&
                             settings.key != defaults.skillKey)) {
                            settings.skillKey = defaults.skillKey;
                        }
                        if (ShouldRestoreProjectileAimDefaults(heroId, skillId, skill->value)) {
                            settings.tracking = defaults.tracking;
                            settings.projectileSpeed = defaults.projectileSpeed;
                            settings.projectileRadius = defaults.projectileRadius;
                            settings.projectileGravity = defaults.projectileGravity;
                            settings.preFireDelayMs = defaults.preFireDelayMs;
                        }
                        if (ShouldMigrateRoadhogChainHookHealthThreshold(
                                heroConfigVersion,
                                heroId,
                                skillId,
                                settings)) {
                            settings.enemyHealthThreshold = defaults.enemyHealthThreshold;
                        }
                        if (ShouldMigrateAnaSleepDartHealthThreshold(
                                heroConfigVersion,
                                heroId,
                                skillId,
                                settings)) {
                            settings.enemyHealthThreshold = defaults.enemyHealthThreshold;
                        }
                        settings = ValidateHeroSkillSettingsValue(settings);
                    }
                    if (RepairBadAnaAbilityKeyLayout(heroConfigVersion, heroId, skillId, settings))
                        settings = ValidateHeroSkillSettingsValue(settings);
                    if (ShouldMigrateAsheFirePatternSequence(heroId, skillId, skill->value, settings)) {
                        settings.sequenceSteps = MakeMeasuredAsheFirePatternSteps();
                        settings.cooldownGuard = true;
                        settings.ammoGuard = true;
                        settings.ammoGuardReserve = 1;
                        settings = ValidateHeroSkillSettingsValue(settings);
                        LogConfig(Diagnostics::LogLevel::Info,
                            "Migrated Ashe fire-pattern sequence to locked tuned defaults.");
                    }
                    if (ShouldRestoreAsheFirePatternAimAssistDefault(heroId, skillId, skill->value, settings)) {
                        settings.tracking.aimBehavior = kAimBehaviorTracking;
                        settings.tracking.speedScale =
                            HeroSkillDetail::kAsheFirePatternTrackingSpeedScale;
                        settings.tracking.fov = kDefaultFovDeg;
                        settings.tracking.bone = kAimBoneHead;
                        settings.tracking.hitbox = kDefaultHitboxScalePercent;
                        settings = ValidateHeroSkillSettingsValue(settings);
                        LogConfig(Diagnostics::LogLevel::Info,
                            "Restored Ashe fire-pattern aim assist defaults.");
                    }
                    if (ShouldMigrateAsheFirePatternTrackingStability(
                            heroConfigVersion,
                            heroId,
                            skillId,
                            settings)) {
                        settings.tracking.speedScale =
                            HeroSkillDetail::kAsheFirePatternTrackingSpeedScale;
                        settings = ValidateHeroSkillSettingsValue(settings);
                        LogConfig(Diagnostics::LogLevel::Info,
                            "Migrated Ashe fire-pattern tracking speed to stable default.");
                    }
                    skills[skillId] = settings;
                }
            }
        }

        bool LoadHeroConfigUnlocked(const std::string& heroConfigPath)
        {
            gLastHeroConfigHadAimOrTriggerPresets = false;
            gLastHeroConfigNeedsSave = false;

            rapidjson::Document document;
            if (!LoadJsonDocument(heroConfigPath, document))
                return false;
            const int heroConfigVersion = ReadJsonInt(document, "version", 1);
            gLoadedHeroConfigVersion = heroConfigVersion;
            const bool legacyFovApertureValues =
                heroConfigVersion < kHeroFovAnglesStoredAsHalfAnglesVersion;
            const bool legacyAimBehaviorValues =
                heroConfigVersion < kHeroAimBehaviorSemanticsVersion;
            gLastHeroConfigNeedsSave = heroConfigVersion < kCurrentHeroConfigVersion;

            const auto storeHasEntries = [&](const char* key) {
                const auto store = document.FindMember(key);
                return store != document.MemberEnd() &&
                    store->value.IsObject() &&
                    store->value.MemberCount() > 0;
            };
            gLastHeroConfigHadAimOrTriggerPresets =
                storeHasEntries("heroAimPresets") ||
                storeHasEntries("heroTriggerPresets");

            heroAimPresets.clear();
            heroTriggerPresets.clear();
            heroSkillPresets.clear();

            const auto aimStore = document.FindMember("heroAimPresets");
            if (aimStore != document.MemberEnd())
                LoadHeroPresetStoreJson(
                    aimStore->value,
                    HeroPresetSlotKind::Aim,
                    heroAimPresets,
                    legacyFovApertureValues,
                    legacyAimBehaviorValues);

            const auto triggerStore = document.FindMember("heroTriggerPresets");
            if (triggerStore != document.MemberEnd())
                LoadHeroPresetStoreJson(
                    triggerStore->value,
                    HeroPresetSlotKind::Trigger,
                    heroTriggerPresets,
                    legacyFovApertureValues,
                    legacyAimBehaviorValues);

            const auto skillStore = document.FindMember("heroSkillPresets");
            if (skillStore != document.MemberEnd())
                LoadHeroSkillPresetStoreJson(
                    skillStore->value,
                    heroConfigVersion,
                    legacyFovApertureValues);

            ValidateHeroPresetsUnlocked();
            ValidateHeroSkillPresetsUnlocked();
            LogConfig(Diagnostics::LogLevel::Info,
                "Loaded hero config JSON from %s.", heroConfigPath.c_str());
            return true;
        }

        bool LoadHeroSkillConfigUnlocked(const std::string& heroConfigPath)
        {
            rapidjson::Document document;
            if (!LoadJsonDocument(heroConfigPath, document))
                return false;
            const int heroConfigVersion = ReadJsonInt(document, "version", 1);
            const bool legacyFovApertureValues =
                heroConfigVersion < kHeroFovAnglesStoredAsHalfAnglesVersion;

            heroSkillPresets.clear();
            const auto skillStore = document.FindMember("heroSkillPresets");
            if (skillStore != document.MemberEnd())
                LoadHeroSkillPresetStoreJson(
                    skillStore->value,
                    heroConfigVersion,
                    legacyFovApertureValues);

            ValidateHeroSkillPresetsUnlocked();
            LogConfig(Diagnostics::LogLevel::Info,
                "Loaded hero skill config JSON from %s.", heroConfigPath.c_str());
            return true;
        }

        void LoadHeroPresetsUnlocked(const IniFile& ini)
        {
            heroAimPresets.clear();
            heroTriggerPresets.clear();
            heroSkillPresets.clear();
            gLoadedHeroConfigVersion = 0;
            const HeroPreset presetDefaults = MakeHeroPresetFromCurrentUnlocked();
            const int fileVersion = ReadInt(ini, kMetaSection, kVersionKey, 0);
            const bool storedBoneUsesLegacyPresetIndex =
                fileVersion < kPresetBonesStoredAsAimBonesVersion;
            const bool legacyFovApertureValues =
                fileVersion < kFovAnglesStoredAsHalfAnglesVersion;
            const bool legacyAimBehaviorValues =
                fileVersion < kAimBehaviorSemanticsVersion;

            for (const HeroPresetDefinition& def : kHeroPresetDefinitions) {
                bool legacyAutoBone = false;
                if (def.legacyName && SectionExists(ini, def.legacyName))
                    legacyAutoBone = ReadBool(ini, def.legacyName, "autobone", false);
                else if (def.legacyAlias && SectionExists(ini, def.legacyAlias))
                    legacyAutoBone = ReadBool(ini, def.legacyAlias, "autobone", false);

                auto makeDefaultSlots = [&]() {
                    std::array<HeroSlotPreset, kHeroPresetSlotCount> slots{};
                    InitializeHeroPresetSlots(slots, 0, false, legacyAutoBone);
                    for (HeroSlotPreset& slot : slots) {
                        slot.preset = presetDefaults;
                        slot.preset.autoBone = legacyAutoBone;
                    }
                    return slots;
                };

                auto loadSlots = [&](HeroPresetSlotKind kind, std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots) {
                    bool loadedAnySlot = false;
                    bool hasScopedSlots = false;
                    for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                        const std::string scopedSection = HeroPresetSlotSectionName(def.presetName, kind, slotIndex);
                        if (SectionExists(ini, scopedSection.c_str())) {
                            hasScopedSlots = true;
                            break;
                        }
                    }

                    for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                        const std::string scopedSection = HeroPresetSlotSectionName(def.presetName, kind, slotIndex);
                        const std::string legacySlotSection = HeroPresetSlotSectionName(def.presetName, slotIndex);
                        const char* sectionToRead = nullptr;
                        if (SectionExists(ini, scopedSection.c_str()))
                            sectionToRead = scopedSection.c_str();
                        else if (!hasScopedSlots && SectionExists(ini, legacySlotSection.c_str()))
                            sectionToRead = legacySlotSection.c_str();

                        if (sectionToRead) {
                            slots[static_cast<size_t>(slotIndex)] = ReadHeroPresetSectionUnlocked(
                                ini,
                                sectionToRead,
                                slots[static_cast<size_t>(slotIndex)],
                                slotIndex,
                                storedBoneUsesLegacyPresetIndex,
                                legacyFovApertureValues,
                                legacyAimBehaviorValues);
                            loadedAnySlot = true;
                        }
                    }

                    if (!loadedAnySlot) {
                        const std::string legacyPresetSection = HeroPresetSectionName(def.presetName);
                        if (SectionExists(ini, legacyPresetSection.c_str())) {
                            slots[0] = ReadHeroPresetSectionUnlocked(
                                ini, legacyPresetSection.c_str(), slots[0], 0,
                                storedBoneUsesLegacyPresetIndex,
                                legacyFovApertureValues,
                                legacyAimBehaviorValues);
                            loadedAnySlot = true;
                        } else {
                            HeroPreset legacyPreset{};
                            if (TryLoadLegacyPresetForDefinition(
                                    ini,
                                    def,
                                    presetDefaults,
                                    legacyFovApertureValues,
                                    legacyAimBehaviorValues,
                                    legacyPreset)) {
                                slots[0].present = true;
                                slots[0].enabled = true;
                                slots[0].preset = legacyPreset;
                                loadedAnySlot = true;
                            }
                        }
                    }

                    return loadedAnySlot;
                };

                auto aimSlots = makeDefaultSlots();
                auto triggerSlots = makeDefaultSlots();
                if (loadSlots(HeroPresetSlotKind::Aim, aimSlots))
                    heroAimPresets[def.heroId] = aimSlots;
                if (loadSlots(HeroPresetSlotKind::Trigger, triggerSlots))
                    heroTriggerPresets[def.heroId] = triggerSlots;
            }

            ValidateHeroPresetsUnlocked();
        }

        bool HasLoadedAimOrTriggerPresetsUnlocked()
        {
            return gLastHeroConfigHadAimOrTriggerPresets;
        }

        bool IniHasHeroPresetSections(const IniFile& ini)
        {
            for (const HeroPresetDefinition& def : kHeroPresetDefinitions) {
                const std::string legacyPresetSection = HeroPresetSectionName(def.presetName);
                if (SectionExists(ini, legacyPresetSection.c_str()))
                    return true;
                if (def.legacyName && SectionExists(ini, def.legacyName))
                    return true;
                if (def.legacyAlias && SectionExists(ini, def.legacyAlias))
                    return true;

                for (int slotIndex = 0; slotIndex < kHeroPresetSlotCount; ++slotIndex) {
                    const std::string legacySlotSection = HeroPresetSlotSectionName(def.presetName, slotIndex);
                    if (SectionExists(ini, legacySlotSection.c_str()))
                        return true;

                    const std::string aimSlotSection = HeroPresetSlotSectionName(def.presetName, HeroPresetSlotKind::Aim, slotIndex);
                    if (SectionExists(ini, aimSlotSection.c_str()))
                        return true;

                    const std::string triggerSlotSection = HeroPresetSlotSectionName(def.presetName, HeroPresetSlotKind::Trigger, slotIndex);
                    if (SectionExists(ini, triggerSlotSection.c_str()))
                        return true;
                }
            }

            return false;
        }

        bool LoadHeroConfigOrMigrateUnlocked(const std::string& heroConfigPath, const IniFile* legacyIni)
        {
            if (FileExists(heroConfigPath) && LoadHeroConfigUnlocked(heroConfigPath)) {
                const bool saveLoadedHeroConfig = gLastHeroConfigNeedsSave;
                if (HasLoadedAimOrTriggerPresetsUnlocked() ||
                    legacyIni == nullptr ||
                    !IniHasHeroPresetSections(*legacyIni)) {
                    if (saveLoadedHeroConfig) {
                        SaveHeroPresetsUnlocked(heroConfigPath);
                        gLastHeroConfigNeedsSave = false;
                        LogConfig(Diagnostics::LogLevel::Info,
                            "Migrated hero config JSON to version %d at %s.",
                            kCurrentHeroConfigVersion,
                            heroConfigPath.c_str());
                    }
                    return true;
                }

                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s contains no aim/trigger hero presets; migrating legacy INI hero presets instead.",
                    heroConfigPath.c_str());
            }

            if (legacyIni) {
                const HeroSkillPresetStore loadedSkillPresets = heroSkillPresets;
                LoadHeroPresetsUnlocked(*legacyIni);
                if (heroSkillPresets.empty() && !loadedSkillPresets.empty())
                    heroSkillPresets = loadedSkillPresets;
                SaveHeroPresetsUnlocked(heroConfigPath);
                LogConfig(Diagnostics::LogLevel::Info,
                    "Migrated legacy INI hero presets to %s.", heroConfigPath.c_str());
                return true;
            }

            heroAimPresets.clear();
            heroTriggerPresets.clear();
            heroSkillPresets.clear();
            return false;
        }

        void LoadColor(const IniFile& ini, const char* section, const char* prefix, ImVec4& color)
        {
            const std::string x = std::string(prefix) + "x";
            const std::string y = std::string(prefix) + "y";
            const std::string z = std::string(prefix) + "z";
            const std::string w = std::string(prefix) + "w";
            color.x = ReadFixedFloat(ini, section, x.c_str(), color.x);
            color.y = ReadFixedFloat(ini, section, y.c_str(), color.y);
            color.z = ReadFixedFloat(ini, section, z.c_str(), color.z);
            color.w = ReadFixedFloat(ini, section, w.c_str(), color.w);
        }

        std::string FovRingStyleKeyPrefix(FovRingSlotKind kind, int slotIndex)
        {
            char buffer[32] = {};
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%sSlot%d",
                          "aim",
                          slotIndex + 1);
            (void)kind;
            return buffer;
        }

        void LoadFovRingStyle(const IniFile& ini,
                              FovRingSlotKind kind,
                              int slotIndex,
                              FovRingSlotStyle& style)
        {
            constexpr const char* section = "Theme.FovRings";
            const std::string prefix = FovRingStyleKeyPrefix(kind, slotIndex);
            style.visible = ReadBool(ini, section, (prefix + "Visible").c_str(), style.visible);
            LoadColor(ini, section, (prefix + "Color").c_str(), style.color);
            style.thickness = ReadFixedFloat(ini, section, (prefix + "Thickness").c_str(), style.thickness);
            style.lineStyle = ReadInt(ini, section, (prefix + "LineStyle").c_str(), style.lineStyle);
            style.showLabel = ReadBool(ini, section, (prefix + "ShowLabel").c_str(), style.showLabel);
            style = ClampFovRingStyle(style, kind, slotIndex);
        }

        void LoadFovRingStyles(const IniFile& ini)
        {
            for (int slotIndex = 0; slotIndex < kMaxHeroPresetSlots; ++slotIndex) {
                LoadFovRingStyle(ini, FovRingSlotKind::Aim, slotIndex,
                                 aimFovRingStyles[static_cast<size_t>(slotIndex)]);
            }
        }

        void SaveFovRingStyle(const std::string& path,
                              FovRingSlotKind kind,
                              int slotIndex,
                              const FovRingSlotStyle& rawStyle)
        {
            constexpr const char* section = "Theme.FovRings";
            const FovRingSlotStyle style = ClampFovRingStyle(rawStyle, kind, slotIndex);
            const std::string prefix = FovRingStyleKeyPrefix(kind, slotIndex);
            WriteBoolValue(path, section, (prefix + "Visible").c_str(), style.visible);
            WriteColor(path, section, (prefix + "Color").c_str(), style.color);
            WriteFixedFloatValue(path, section, (prefix + "Thickness").c_str(), style.thickness);
            WriteIntValue(path, section, (prefix + "LineStyle").c_str(), style.lineStyle);
            WriteBoolValue(path, section, (prefix + "ShowLabel").c_str(), style.showLabel);
        }

        void SaveFovRingStyles(const std::string& path)
        {
            for (int slotIndex = 0; slotIndex < kMaxHeroPresetSlots; ++slotIndex) {
                SaveFovRingStyle(path,
                                 FovRingSlotKind::Aim,
                                 slotIndex,
                                 aimFovRingStyles[static_cast<size_t>(slotIndex)]);
            }
        }

        void LoadHeroSettingsUnlocked(const IniFile& ini,
                                      const char* section,
                                      uint64_t heroId,
                                      bool legacyFovApertureValues)
        {
            highPriority = ReadBool(ini, section, "highPriority", highPriority);
            aiaim = ReadBool(ini, section, "aiaim", aiaim);
            hanzoautospeed = ReadBool(ini, section, "hanzoautospeed", hanzoautospeed);
            autoscalefov = ReadBool(ini, section, "autoscalefov", autoscalefov);
            lockontarget = ReadBool(ini, section, "lockontarget", lockontarget);
            trackcompensate = ReadBool(ini, section, "trackc", trackcompensate);
            comarea = ReadFixedFloat(ini, section, "comarea", comarea);
            comspeed = ReadFixedFloat(ini, section, "comspeed", comspeed);
            Fov = ReadFov2Compat(ini, section, "FOV", Fov, legacyFovApertureValues);
            minFov1 = Fov;
            hitbox = ReadHitboxScaleCompat(ini, section, "hitboxScale", "hitbox", hitbox);
            missbox = ReadFixedFloat(ini, section, "missbox", missbox);
            Tracking_smooth = ReadFixedFloat(ini, section, "Tracking_smooth", Tracking_smooth);
            Flick_smooth = ReadFixedFloat(ini, section, "Flick_smooth", Flick_smooth);
            Shoottime = ReadInt(ini, section, "AutoShootTime", Shoottime);
            predit_level = static_cast<float>(ReadInt(ini, section, "predit_level", static_cast<int>(predit_level)));
            aim_key = ReadInt(ini, section, "aim_key", aim_key);
            Gravitypredit = ReadBool(ini, section, "Gravitypredit", Gravitypredit);
            SkillHealth = static_cast<float>(ReadInt(ini, section, "SkillHealth", static_cast<int>(SkillHealth)));
            AutoSkill = ReadBool(ini, section, "AutoSkill", AutoSkill);
            AntiAFK = ReadBool(ini, section, "AntiAFK", AntiAFK);
            dontshot = ReadBool(ini, section, "dontshot", dontshot);
            targetdelay = ReadBool(ini, section, "targetdelay", targetdelay);
            targetdelaytime = ReadInt(ini, section, "targetdelaytime", targetdelaytime);
            shotmanydont = ReadInt(ini, section, "dontmanyshot", shotmanydont);
            hitboxdelayshoot = ReadBool(ini, section, "hitboxdelayshoot", hitboxdelayshoot);
            hiboxdelaytime = ReadInt(ini, section, "hitboxdelaytime", hiboxdelaytime);
            accvalue = ReadFixedFloat(ini, section, "accvalue", accvalue);
            switch_team = ReadBool(ini, section, "switch_team", switch_team);
            switch_team2 = ReadBool(ini, section, "switch_team2", switch_team2);
            Bone = ReadInt(ini, section, "Bone", Bone);
            autobone = ReadBool(ini, section, "autobone", autobone);
            Bone2 = ReadInt(ini, section, "Bone2", Bone2);
            autobone2 = ReadBool(ini, section, "autobone2", autobone2);
            AutoMelee = ReadBool(ini, section, "AutoMelee", AutoMelee);
            meleedistance = ReadFixedFloat(ini, section, "meleedistance", meleedistance);
            meleehealth = ReadFixedFloat(ini, section, "meleehealth", meleehealth);
            AutoRMB = ReadBool(ini, section, "AutoRMB", AutoRMB);
            AutoRMBdistance = ReadFixedFloat(ini, section, "AutoRMBdistance", AutoRMBdistance);
            AutoRMBhealth = ReadFixedFloat(ini, section, "AutoRMBhealth", AutoRMBhealth);
            secondaim = ReadBool(ini, section, "secondaim", secondaim);
            triggerbot = ReadBool(ini, section, "triggerbot", triggerbot);
            triggerbotIgnoreInvisible = ReadBool(ini, section, "triggerbotIgnoreInvisible", triggerbotIgnoreInvisible);
            triggerbot2 = ReadBool(ini, section, "triggerbot2", triggerbot2);
            triggerbotIgnoreInvisible2 = ReadBool(ini, section, "triggerbotIgnoreInvisible2", triggerbotIgnoreInvisible2);
            Tracking2 = ReadBool(ini, section, "Tracking2", Tracking2);
            Flick2 = ReadBool(ini, section, "Flick2", Flick2);
            aim_key2 = ReadInt(ini, section, "aim_key2", aim_key2);
            togglekey = ReadInt(ini, section, "togglekey", togglekey);
            Tracking_smooth2 = ReadFixedFloat(ini, section, "Tracking_smooth2", Tracking_smooth2);
            Flick_smooth2 = ReadFixedFloat(ini, section, "Flick_smooth2", Flick_smooth2);
            accvalue2 = ReadFixedFloat(ini, section, "accvalue2", accvalue2);
            hitbox2 = ReadHitboxScaleCompat(ini, section, "hitbox2Scale", "hitbox2", hitbox2);
            Fov2 = ReadFov2Compat(ini, section, "Fov2", Fov2, legacyFovApertureValues);
            minFov2 = Fov2;

            const int legacyAimMode = ReadInt(ini, section, "Aim Mode", CurrentAimMode());
            ApplyAimMode(legacyAimMode);
            if (!KeyExists(ini, kAimbotSection, "aimBehavior") && !KeyExists(ini, section, "aimBehavior"))
                aimBehavior = CurrentAimMode();
            else
                aimBehavior = NormalizeAimBehaviorForLoad(
                    ReadInt(ini, section, "aimBehavior", aimBehavior),
                    ReadInt(ini, kMetaSection, kVersionKey, 0) < kAimBehaviorSemanticsVersion);
            AutoShoot = ReadBool(ini, section, "autoshootonoff", AutoShoot);
            Prediction = ReadBool(ini, section, "predictdec", Prediction);

            if (heroId == OW::eHero::HERO_GENJI) {
                GenjiBlade = ReadBool(ini, section, "GenjiBlade", GenjiBlade);
                AutoShiftGenji = ReadBool(ini, section, "AutoShiftGenji", AutoShiftGenji);
                bladespeed = ReadFixedFloat(ini, section, "bladespeed", bladespeed);
            } else {
                GenjiBlade = false;
                AutoShiftGenji = false;
            }

            if (heroId == OW::eHero::HERO_WIDOWMAKER) {
                widowautounscope = ReadBool(ini, section, "widowautounscope", widowautounscope);
            } else {
                widowautounscope = false;
            }
        }

        void LoadAimbotSettingsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = kAimbotSection;

            aimbotAutoshot = ReadBool(ini, section, "aimbotAutoshot", aimbotAutoshot);
            aimbotKeepFiring = ReadBool(ini, section, "aimbotKeepFiring", aimbotKeepFiring);
            aimbotPredictionMode = ReadInt(ini, section, "aimbotPredictionMode", aimbotPredictionMode);
            aimBehavior = ReadInt(ini, section, "aimBehavior", aimBehavior);
            aimbotFirePolicy = ReadInt(ini, section, "aimbotFirePolicy", aimbotFirePolicy);
            aimbotTriggerDelay = ReadFixedFloat(ini, section, "aimbotTriggerDelay", aimbotTriggerDelay);
            aimbotMaxHead = ReadFixedFloat(ini, section, "aimbotMaxHead", aimbotMaxHead);
            aimbotSmoothType = ReadInt(ini, section, "aimbotSmoothType", aimbotSmoothType);
            aimbotStickiness = ReadFixedFloat(ini, section, "aimbotStickiness", aimbotStickiness);
            aimbotSmoothY = ReadFixedFloat(ini, section, "aimbotSmoothY", aimbotSmoothY);
            aimbotPitchScale = ReadFixedFloat(ini, section, "aimbotPitchScale", aimbotPitchScale);
            aimbotMaxAim = ReadFixedFloat(ini, section, "aimbotMaxAim", aimbotMaxAim);
            aimbotMinCharge = ReadFixedFloat(ini, section, "aimbotMinCharge", aimbotMinCharge);
            aimbotMaxCharge = ReadFixedFloat(ini, section, "aimbotMaxCharge", aimbotMaxCharge);
            aimbotIgnoreInvisible = ReadBool(ini, section, "aimbotIgnoreInvisible", aimbotIgnoreInvisible);
            aimbotTrace = ReadInt(ini, section, "aimbotTrace", aimbotTrace);
            aimbotUnlock = ReadInt(ini, section, "aimbotUnlock", aimbotUnlock);
            aimbotLockTime = ReadFixedFloat(ini, section, "aimbotLockTime", aimbotLockTime);
            aimbotMaxDist = ReadFixedFloat(ini, section, "aimbotMaxDist", aimbotMaxDist);
            aimbotMinDist = ReadFixedFloat(ini, section, "aimbotMinDist", aimbotMinDist);
            aimbotAttack = ReadInt(ini, section, "aimbotAttack", aimbotAttack);
            aimbotTeam = ReadInt(ini, section, "aimbotTeam", aimbotTeam);
            aimbotPriority = ReadInt(ini, section, "aimbotPriority", aimbotPriority);
            aimbotTrackingDeadzone = ReadFixedFloat(ini, section, "aimbotTrackingDeadzone", aimbotTrackingDeadzone);
            aimbotBoneMask = static_cast<SkeletonBoneMask>(
                ReadInt(ini, section, "aimbotBoneMask", static_cast<int>(aimbotBoneMask)));
            aimbotBoneMask2 = static_cast<SkeletonBoneMask>(
                ReadInt(ini, section, "aimbotBoneMask2", static_cast<int>(aimbotBoneMask2)));
            aimbotFlickShotClampMs = ReadFixedFloat(ini, section, "aimbotFlickShotClampMs", aimbotFlickShotClampMs);
            aimbotFlickPostFireDelayMs = ReadFixedFloat(ini, section, "aimbotFlickPostFireDelayMs", aimbotFlickPostFireDelayMs);
            aimbotFlickTrajectoryWait = ReadBool(ini, section, "aimbotFlickTrajectoryWait", aimbotFlickTrajectoryWait);
            aimbotFlickTrajectoryWaitMs = ReadFixedFloat(ini, section, "aimbotFlickTrajectoryWaitMs", aimbotFlickTrajectoryWaitMs);
            aimbotFlickTrajectoryApexWindowMs = ReadFixedFloat(ini, section, "aimbotFlickTrajectoryApexWindowMs", aimbotFlickTrajectoryApexWindowMs);
            aimbotFlick2ndTriggerGate = ReadBool(ini, section, "aimbotFlick2ndTriggerGate", aimbotFlick2ndTriggerGate);
            aimbotFlick2ndBoxPadding = ReadFixedFloat(ini, section, "aimbotFlick2ndBoxPadding", aimbotFlick2ndBoxPadding);
            aimbotFlick2ndInnerRadius = ReadFixedFloat(ini, section, "aimbotFlick2ndInnerRadius", aimbotFlick2ndInnerRadius);
            aimbotFlick2ndInnerSmoothScale = ReadFixedFloat(ini, section, "aimbotFlick2ndInnerSmoothScale", aimbotFlick2ndInnerSmoothScale);
            aimbotFlick2ndInnerMethod = ReadInt(ini, section, "aimbotFlick2ndInnerMethod", aimbotFlick2ndInnerMethod);
            aimbotTwoStage = ReadBool(ini, section, "aimbotTwoStage", aimbotTwoStage);
            aimbotTwoStageTriggerGate = ReadBool(ini, section, "aimbotTwoStageTriggerGate", aimbotTwoStageTriggerGate);
            aimbotTwoStageBoxPadding = ReadFixedFloat(ini, section, "aimbotTwoStageBoxPadding", aimbotTwoStageBoxPadding);
            aimbotTwoStageInnerRadius = ReadFixedFloat(ini, section, "aimbotTwoStageInnerRadius", aimbotTwoStageInnerRadius);
            aimbotTwoStageInnerSmoothScale = ReadFixedFloat(ini, section, "aimbotTwoStageInnerSmoothScale", aimbotTwoStageInnerSmoothScale);
            if (!KeyExists(ini, section, "aimbotFlick2ndTriggerGate"))
                aimbotFlick2ndTriggerGate = aimbotTwoStageTriggerGate;
            if (!KeyExists(ini, section, "aimbotFlick2ndBoxPadding"))
                aimbotFlick2ndBoxPadding = aimbotTwoStageBoxPadding;
            if (!KeyExists(ini, section, "aimbotFlick2ndInnerRadius"))
                aimbotFlick2ndInnerRadius = aimbotTwoStageInnerRadius;
            if (!KeyExists(ini, section, "aimbotFlick2ndInnerSmoothScale"))
                aimbotFlick2ndInnerSmoothScale = aimbotTwoStageInnerSmoothScale;
            const int fileVersion = ReadInt(ini, kMetaSection, kVersionKey, 0);
            aimBehavior = NormalizeAimBehaviorForLoad(
                aimBehavior,
                fileVersion < kAimBehaviorSemanticsVersion);
            if (aimbotTwoStage && IsFlickBehavior(aimBehavior))
                aimBehavior = kAimBehaviorFlick2nd;
            aimOvershootCurve = ReadBool(ini, section, "aimOvershootCurve", aimOvershootCurve);
            aimOvershootGain = ReadFixedFloat(ini, section, "aimOvershootGain", aimOvershootGain);
            aimOvershootResetPixels = ReadFixedFloat(ini, section, "aimOvershootResetPixels", aimOvershootResetPixels);
            aimDryRun = ReadBool(ini, section, "aimDryRun", aimDryRun);
            aimVerboseLog = ReadBool(ini, section, "aimVerboseLog", aimVerboseLog);
            aimDryRunLogIntervalMs = ReadInt(ini, section, "aimDryRunLogIntervalMs", aimDryRunLogIntervalMs);

            triggerbotMode = ReadInt(ini, section, "triggerbotMode", triggerbotMode);
            triggerbotKey = ReadInt(ini, section, "triggerbotKey", triggerbotKey);
            triggerbotShotInterval = ReadFixedFloat(ini, section, "triggerbotShotInterval", triggerbotShotInterval);
            triggerbotChargeAware = ReadBool(ini, section, "triggerbotChargeAware", triggerbotChargeAware);
            triggerbotMinCharge = ReadFixedFloat(ini, section, "triggerbotMinCharge", triggerbotMinCharge);
            triggerbotDisableWhileReloading = ReadBool(
                ini,
                section,
                "triggerbotDisableWhileReloading",
                triggerbotDisableWhileReloading);
            triggerbotIgnoreInvisible = ReadBool(ini, section, "triggerbotIgnoreInvisible", triggerbotIgnoreInvisible);
            triggerbotBoneMask = static_cast<TriggerBoneMask>(
                ReadInt(ini, section, "triggerbotBoneMask", static_cast<int>(triggerbotBoneMask)));

            triggerbotMode2 = ReadInt(ini, section, "triggerbotMode2", triggerbotMode2);
            triggerbotKey2 = ReadInt(ini, section, "triggerbotKey2", triggerbotKey2);
            triggerbotShotInterval2 = ReadFixedFloat(ini, section, "triggerbotShotInterval2", triggerbotShotInterval2);
            triggerbotChargeAware2 = ReadBool(ini, section, "triggerbotChargeAware2", triggerbotChargeAware2);
            triggerbotMinCharge2 = ReadFixedFloat(ini, section, "triggerbotMinCharge2", triggerbotMinCharge2);
            triggerbotDisableWhileReloading2 = ReadBool(
                ini,
                section,
                "triggerbotDisableWhileReloading2",
                triggerbotDisableWhileReloading2);
            triggerbotIgnoreInvisible2 = ReadBool(ini, section, "triggerbotIgnoreInvisible2", triggerbotIgnoreInvisible2);
            triggerbotBoneMask2 = static_cast<TriggerBoneMask>(
                ReadInt(ini, section, "triggerbotBoneMask2", static_cast<int>(triggerbotBoneMask2)));

            triggerbotToggleActive = false;
            triggerbotToggleActive2 = false;
            triggerbotLastFireTick = 0;
            triggerbotLastFireTick2 = 0;
        }

        AimMethodPreset ReadAimMethodPresetSection(const IniFile& ini, const char* section, int fallbackIndex)
        {
            AimMethodPreset preset{};
            preset.id = ReadInt(ini, section, "id", fallbackIndex + 1);
            preset.name = ReadString(ini, section, "name", NormalizeAimPresetName("", "Method Preset", fallbackIndex).c_str());
            preset.method = ReadInt(ini, section, "method", preset.method);
            preset.angularSpeedScale = ReadFixedFloat(ini, section, "angularSpeedScale", preset.angularSpeedScale);
            preset.pidP = ReadFixedFloat(ini, section, "pidP", preset.pidP);
            preset.pidI = ReadFixedFloat(ini, section, "pidI", preset.pidI);
            preset.pidD = ReadFixedFloat(ini, section, "pidD", preset.pidD);
            preset.pidMaxIntegral = ReadFixedFloat(ini, section, "pidMaxIntegral", preset.pidMaxIntegral);
            preset.pidDeadzone = ReadFixedFloat(ini, section, "pidDeadzone", preset.pidDeadzone);
            preset.bezierControlPoints = ReadInt(ini, section, "bezierControlPoints", preset.bezierControlPoints);
            preset.bezierCurvature = ReadFixedFloat(ini, section, "bezierCurvature", preset.bezierCurvature);
            preset.bezierSpeed = ReadFixedFloat(ini, section, "bezierSpeed", preset.bezierSpeed);
            preset.piecewiseNearDegrees = ReadFixedFloat(ini, section, "piecewiseNearDegrees", preset.piecewiseNearDegrees);
            preset.piecewiseMidDegrees = ReadFixedFloat(ini, section, "piecewiseMidDegrees", preset.piecewiseMidDegrees);
            preset.piecewiseFarDegrees = ReadFixedFloat(ini, section, "piecewiseFarDegrees", preset.piecewiseFarDegrees);
            preset.piecewiseNearScale = ReadFixedFloat(ini, section, "piecewiseNearScale", preset.piecewiseNearScale);
            preset.piecewiseMidScale = ReadFixedFloat(ini, section, "piecewiseMidScale", preset.piecewiseMidScale);
            preset.piecewiseFarScale = ReadFixedFloat(ini, section, "piecewiseFarScale", preset.piecewiseFarScale);
            preset.accelLimitedAcceleration = ReadFixedFloat(ini, section, "accelLimitedAcceleration", preset.accelLimitedAcceleration);
            preset.constantAngularSpeedDeg = ReadFixedFloat(ini, section, "constantAngularSpeedDeg", preset.constantAngularSpeedDeg);
            return ValidateAimMethodPresetValue(preset, fallbackIndex);
        }

        AimBehaviorPreset ReadAimBehaviorPresetSection(const IniFile& ini, const char* section, int fallbackIndex)
        {
            AimBehaviorPreset preset{};
            preset.id = ReadInt(ini, section, "id", fallbackIndex + 1);
            preset.name = ReadString(ini, section, "name", NormalizeAimPresetName("", "Behavior Preset", fallbackIndex).c_str());
            preset.behavior = ReadInt(ini, section, "behavior", preset.behavior);
            preset.method = ReadInt(ini, section, "method", preset.method);
            preset.methodPresetId = ReadInt(ini, section, "methodPresetId", preset.methodPresetId);
            preset.baseSpeed = ReadFixedFloat(ini, section, "baseSpeed", preset.baseSpeed);
            preset.startLimiter.enabled = ReadBool(
                ini, section, "startLimiterEnabled", preset.startLimiter.enabled);
            preset.startLimiter.initialCapDegPerSec = ReadFixedFloat(
                ini, section, "startLimiterInitialCapDegPerSec", preset.startLimiter.initialCapDegPerSec);
            preset.startLimiter.capRiseDegPerSec2 = ReadFixedFloat(
                ini, section, "startLimiterCapRiseDegPerSec2", preset.startLimiter.capRiseDegPerSec2);
            preset.startLimiter.restartOnTargetChange = ReadBool(
                ini, section, "startLimiterRestartOnTargetChange", preset.startLimiter.restartOnTargetChange);
            preset.moveSplitEnabled = ReadBool(ini, section, "moveSplitEnabled", preset.moveSplitEnabled);
            preset.moveSplitMaxPixels = ReadInt(ini, section, "moveSplitMaxPixels", preset.moveSplitMaxPixels);
            preset.moveSplitDelayUs = ReadInt(ini, section, "moveSplitDelayUs", preset.moveSplitDelayUs);
            return ValidateAimBehaviorPresetValue(preset, fallbackIndex);
        }

        DynamicFovPreset ReadDynamicFovPresetSection(const IniFile& ini, const char* section, int fallbackIndex)
        {
            DynamicFovPreset preset{};
            preset.id = ReadInt(ini, section, "id", fallbackIndex + 1);
            preset.name = ReadString(ini, section, "name", NormalizeAimPresetName("", "Dynamic FOV", fallbackIndex).c_str());
            preset.smooth = ReadBool(ini, section, "smooth", preset.smooth);
            preset.pointCount = ReadInt(ini, section, "pointCount", preset.pointCount);
            for (int index = 0; index < kMaxDynamicFovPoints; ++index) {
                char distanceKey[32] = {};
                char fovKey[32] = {};
                std::snprintf(distanceKey, sizeof(distanceKey), "point%dDistanceM", index);
                std::snprintf(fovKey, sizeof(fovKey), "point%dFovDeg", index);
                preset.points[static_cast<size_t>(index)].distanceM =
                    ReadFixedFloat(ini, section, distanceKey, preset.points[static_cast<size_t>(index)].distanceM);
                preset.points[static_cast<size_t>(index)].fovDeg =
                    ReadFixedFloat(ini, section, fovKey, preset.points[static_cast<size_t>(index)].fovDeg);
            }
            return ValidateDynamicFovPresetForConfig(preset, fallbackIndex);
        }

        void LoadAimCustomPresetsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = kAimMethodSection;
            const int methodPresetCount = std::clamp(
                ReadInt(ini, section, "methodPresetCount", 0),
                0,
                kMaxAimCustomPresets);
            aimMethodPresets.clear();
            aimMethodPresets.reserve(static_cast<size_t>(methodPresetCount));
            for (int index = 0; index < methodPresetCount; ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "AimMethodPreset.%d", index);
                if (!SectionExists(ini, presetSection))
                    continue;
                aimMethodPresets.push_back(ReadAimMethodPresetSection(ini, presetSection, index));
            }
            ValidateAimCustomPresetsUnlocked();

            const int behaviorPresetCount = std::clamp(
                ReadInt(ini, section, "behaviorPresetCount", 0),
                0,
                kMaxAimCustomPresets);
            aimBehaviorPresets.clear();
            aimBehaviorPresets.reserve(static_cast<size_t>(behaviorPresetCount));
            for (int index = 0; index < behaviorPresetCount; ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "AimBehaviorPreset.%d", index);
                if (!SectionExists(ini, presetSection))
                    continue;
                aimBehaviorPresets.push_back(ReadAimBehaviorPresetSection(ini, presetSection, index));
            }
            ValidateAimCustomPresetsUnlocked();

            const int dynamicFovPresetCount = std::clamp(
                ReadInt(ini, section, "dynamicFovPresetCount", 0),
                0,
                kMaxAimCustomPresets);
            dynamicFovPresets.clear();
            dynamicFovPresets.reserve(static_cast<size_t>(dynamicFovPresetCount));
            for (int index = 0; index < dynamicFovPresetCount; ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "DynamicFovPreset.%d", index);
                if (!SectionExists(ini, presetSection))
                    continue;
                dynamicFovPresets.push_back(ReadDynamicFovPresetSection(ini, presetSection, index));
            }
            ValidateAimCustomPresetsUnlocked();
        }

        void LoadAimMethodSettingsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = kAimMethodSection;
            constexpr std::array<const char*, kAimBehaviorCount> methodKeys = {
                "trackingMethod",
                "flickMethod",
                "flick2ndMethod",
                "reacquireMethod",
                "magneticTriggerMethod"
            };
            constexpr std::array<const char*, kAimBehaviorCount> speedKeys = {
                "trackingBaseSpeed",
                "flickBaseSpeed",
                "flick2ndBaseSpeed",
                "reacquireBaseSpeed",
                "magneticTriggerBaseSpeed"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterEnabledKeys = {
                "trackingStartLimiterEnabled",
                "flickStartLimiterEnabled",
                "flick2ndStartLimiterEnabled",
                "reacquireStartLimiterEnabled",
                "magneticTriggerStartLimiterEnabled"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterInitialCapKeys = {
                "trackingStartLimiterInitialCapDegPerSec",
                "flickStartLimiterInitialCapDegPerSec",
                "flick2ndStartLimiterInitialCapDegPerSec",
                "reacquireStartLimiterInitialCapDegPerSec",
                "magneticTriggerStartLimiterInitialCapDegPerSec"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterCapRiseKeys = {
                "trackingStartLimiterCapRiseDegPerSec2",
                "flickStartLimiterCapRiseDegPerSec2",
                "flick2ndStartLimiterCapRiseDegPerSec2",
                "reacquireStartLimiterCapRiseDegPerSec2",
                "magneticTriggerStartLimiterCapRiseDegPerSec2"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterRestartKeys = {
                "trackingStartLimiterRestartOnTargetChange",
                "flickStartLimiterRestartOnTargetChange",
                "flick2ndStartLimiterRestartOnTargetChange",
                "reacquireStartLimiterRestartOnTargetChange",
                "magneticTriggerStartLimiterRestartOnTargetChange"
            };
            constexpr std::array<const char*, kAimBehaviorCount> methodPresetKeys = {
                "trackingMethodPresetId",
                "flickMethodPresetId",
                "flick2ndMethodPresetId",
                "reacquireMethodPresetId",
                "magneticTriggerMethodPresetId"
            };
            constexpr std::array<const char*, kAimBehaviorCount> accelerationKeys = {
                "trackingAcceleration",
                "flickAcceleration",
                "flick2ndAcceleration",
                "reacquireAcceleration",
                "magneticTriggerAcceleration"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitEnabledKeys = {
                "trackingMoveSplitEnabled",
                "flickMoveSplitEnabled",
                "flick2ndMoveSplitEnabled",
                "reacquireMoveSplitEnabled",
                "magneticTriggerMoveSplitEnabled"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitMaxPixelsKeys = {
                "trackingMoveSplitMaxPixels",
                "flickMoveSplitMaxPixels",
                "flick2ndMoveSplitMaxPixels",
                "reacquireMoveSplitMaxPixels",
                "magneticTriggerMoveSplitMaxPixels"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitDelayUsKeys = {
                "trackingMoveSplitDelayUs",
                "flickMoveSplitDelayUs",
                "flick2ndMoveSplitDelayUs",
                "reacquireMoveSplitDelayUs",
                "magneticTriggerMoveSplitDelayUs"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacyMethodKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickMethod"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacySpeedKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickBaseSpeed"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacyMethodPresetKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickMethodPresetId"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacyAccelerationKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickAcceleration"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacySplitEnabledKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickMoveSplitEnabled"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacySplitMaxPixelsKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickMoveSplitMaxPixels"
            };
            constexpr std::array<const char*, kAimBehaviorCount> legacySplitDelayUsKeys = {
                nullptr, nullptr, nullptr, nullptr, "assistFlickMoveSplitDelayUs"
            };
            constexpr std::array<const char*, kAimMethodCount> angularSpeedKeys = {
                "linearAngularSpeedScale",
                "pidAngularSpeedScale",
                "bezierAngularSpeedScale",
                "piecewiseAngularSpeedScale",
                "accelLimitedAngularSpeedScale",
                "constantAngularSpeedScale"
            };

            aimMethod = ReadInt(ini, section, "aimMethod", aimMethod);
            aimPidP = ReadFixedFloat(ini, section, "aimPidP", aimPidP);
            aimPidI = ReadFixedFloat(ini, section, "aimPidI", aimPidI);
            aimPidD = ReadFixedFloat(ini, section, "aimPidD", aimPidD);
            aimPidMaxIntegral = ReadFixedFloat(ini, section, "aimPidMaxIntegral", aimPidMaxIntegral);
            aimPidDeadzone = ReadFixedFloat(ini, section, "aimPidDeadzone", aimPidDeadzone);
            aimBezierControlPoints = ReadInt(ini, section, "aimBezierControlPoints", aimBezierControlPoints);
            aimBezierCurvature = ReadFixedFloat(ini, section, "aimBezierCurvature", aimBezierCurvature);
            aimBezierSpeed = ReadFixedFloat(ini, section, "aimBezierSpeed", aimBezierSpeed);
            aimPiecewiseNearDegrees = ReadFixedFloat(ini, section, "aimPiecewiseNearDegrees", aimPiecewiseNearDegrees);
            aimPiecewiseMidDegrees = ReadFixedFloat(ini, section, "aimPiecewiseMidDegrees", aimPiecewiseMidDegrees);
            aimPiecewiseFarDegrees = ReadFixedFloat(ini, section, "aimPiecewiseFarDegrees", aimPiecewiseFarDegrees);
            aimPiecewiseNearScale = ReadFixedFloat(ini, section, "aimPiecewiseNearScale", aimPiecewiseNearScale);
            aimPiecewiseMidScale = ReadFixedFloat(ini, section, "aimPiecewiseMidScale", aimPiecewiseMidScale);
            aimPiecewiseFarScale = ReadFixedFloat(ini, section, "aimPiecewiseFarScale", aimPiecewiseFarScale);
            aimAccelLimitedAcceleration = ReadFixedFloat(ini, section, "aimAccelLimitedAcceleration", aimAccelLimitedAcceleration);
            aimConstantAngularSpeedDeg = ReadFixedFloat(ini, section, "aimConstantAngularSpeedDeg", aimConstantAngularSpeedDeg);

            for (size_t index = 0; index < aimBehaviorMethod.size(); ++index) {
                const int methodFallback = legacyMethodKeys[index] && KeyExists(ini, section, legacyMethodKeys[index])
                    ? ReadInt(ini, section, legacyMethodKeys[index], aimMethod)
                    : aimMethod;
                const float speedFallback = legacySpeedKeys[index] && KeyExists(ini, section, legacySpeedKeys[index])
                    ? ReadFixedFloat(ini, section, legacySpeedKeys[index], 100.0f)
                    : 100.0f;
                const float accelerationFallback = legacyAccelerationKeys[index] && KeyExists(ini, section, legacyAccelerationKeys[index])
                    ? ReadFixedFloat(ini, section, legacyAccelerationKeys[index], accvalue)
                    : accvalue;
                const bool splitEnabledFallback = legacySplitEnabledKeys[index] && KeyExists(ini, section, legacySplitEnabledKeys[index])
                    ? ReadBool(ini, section, legacySplitEnabledKeys[index], aimBehaviorMoveSplitEnabled[index])
                    : aimBehaviorMoveSplitEnabled[index];
                const int splitMaxPixelsFallback = legacySplitMaxPixelsKeys[index] && KeyExists(ini, section, legacySplitMaxPixelsKeys[index])
                    ? ReadInt(ini, section, legacySplitMaxPixelsKeys[index], aimBehaviorMoveSplitMaxPixels[index])
                    : aimBehaviorMoveSplitMaxPixels[index];
                const int splitDelayUsFallback = legacySplitDelayUsKeys[index] && KeyExists(ini, section, legacySplitDelayUsKeys[index])
                    ? ReadInt(ini, section, legacySplitDelayUsKeys[index], aimBehaviorMoveSplitDelayUs[index])
                    : aimBehaviorMoveSplitDelayUs[index];
                aimBehaviorMethod[index] = ReadInt(ini, section, methodKeys[index], methodFallback);
                aimBehaviorBaseSpeed[index] = ReadFixedFloat(ini, section, speedKeys[index], speedFallback);
                AimStartLimiterProfile startLimiter = aimBehaviorStartLimiterProfiles[index];
                startLimiter.enabled = ReadBool(
                    ini, section, startLimiterEnabledKeys[index], startLimiter.enabled);
                startLimiter.initialCapDegPerSec = ReadFixedFloat(
                    ini, section, startLimiterInitialCapKeys[index], startLimiter.initialCapDegPerSec);
                startLimiter.capRiseDegPerSec2 = ReadFixedFloat(
                    ini, section, startLimiterCapRiseKeys[index], startLimiter.capRiseDegPerSec2);
                startLimiter.restartOnTargetChange = ReadBool(
                    ini, section, startLimiterRestartKeys[index], startLimiter.restartOnTargetChange);
                aimBehaviorStartLimiterProfiles[index] =
                    ValidateAimStartLimiterProfile(startLimiter);
                aimBehaviorAcceleration[index] = ReadFixedFloat(ini, section, accelerationKeys[index], accelerationFallback);
                aimBehaviorMoveSplitEnabled[index] = ReadBool(
                    ini,
                    section,
                    splitEnabledKeys[index],
                    splitEnabledFallback);
                aimBehaviorMoveSplitMaxPixels[index] = ReadInt(
                    ini,
                    section,
                    splitMaxPixelsKeys[index],
                    splitMaxPixelsFallback);
                aimBehaviorMoveSplitDelayUs[index] = ReadInt(
                    ini,
                    section,
                    splitDelayUsKeys[index],
                    splitDelayUsFallback);
            }
            if (!KeyExists(ini, section, "flick2ndMethod"))
                aimBehaviorMethod[static_cast<size_t>(kAimBehaviorFlick2nd)] =
                    ReadInt(ini, section, "flickClampMethod", aimBehaviorMethod[static_cast<size_t>(kAimBehaviorFlick2nd)]);
            if (!KeyExists(ini, section, "flick2ndBaseSpeed"))
                aimBehaviorBaseSpeed[static_cast<size_t>(kAimBehaviorFlick2nd)] =
                    ReadFixedFloat(ini, section, "flickClampBaseSpeed", aimBehaviorBaseSpeed[static_cast<size_t>(kAimBehaviorFlick2nd)]);
            if (!KeyExists(ini, section, "flick2ndAcceleration"))
                aimBehaviorAcceleration[static_cast<size_t>(kAimBehaviorFlick2nd)] =
                    ReadFixedFloat(ini, section, "flickClampAcceleration", aimBehaviorAcceleration[static_cast<size_t>(kAimBehaviorFlick2nd)]);
            for (size_t index = 0; index < aimMethodAngularSpeedScale.size(); ++index)
                aimMethodAngularSpeedScale[index] = ReadFixedFloat(ini, section, angularSpeedKeys[index], 100.0f);
            LoadAimCustomPresetsUnlocked(ini);
            for (size_t index = 0; index < aimBehaviorMethodPreset.size(); ++index) {
                const int methodPresetFallback = legacyMethodPresetKeys[index] && KeyExists(ini, section, legacyMethodPresetKeys[index])
                    ? ReadInt(ini, section, legacyMethodPresetKeys[index], -1)
                    : -1;
                aimBehaviorMethodPreset[index] = ReadInt(ini, section, methodPresetKeys[index], methodPresetFallback);
            }
            aimBehaviorPresetId = ReadInt(ini, section, "aimBehaviorPresetId", aimBehaviorPresetId);
            secondaryAimMethodOverride[0] = ReadInt(
                ini, section, "secondaryTrackingMethod", secondaryAimMethodOverride[0]);
            secondaryAimMethodOverride[1] = ReadInt(
                ini, section, "secondaryFlickMethod", secondaryAimMethodOverride[1]);
        }

        void SaveAimbotSettingsUnlocked(const std::string& path)
        {
            constexpr const char* section = kAimbotSection;

            WriteBoolValue(path, section, "aimbotAutoshot", aimbotAutoshot);
            WriteBoolValue(path, section, "aimbotKeepFiring", aimbotKeepFiring);
            WriteIntValue(path, section, "aimbotPredictionMode", aimbotPredictionMode);
            WriteIntValue(path, section, "aimBehavior", aimBehavior);
            WriteIntValue(path, section, "aimbotFirePolicy", aimbotFirePolicy);
            WriteFixedFloatValue(path, section, "aimbotTriggerDelay", aimbotTriggerDelay);
            WriteFixedFloatValue(path, section, "aimbotMaxHead", aimbotMaxHead);
            WriteIntValue(path, section, "aimbotSmoothType", aimbotSmoothType);
            WriteFixedFloatValue(path, section, "aimbotStickiness", aimbotStickiness);
            WriteFixedFloatValue(path, section, "aimbotSmoothY", aimbotSmoothY);
            WriteFixedFloatValue(path, section, "aimbotPitchScale", aimbotPitchScale);
            WriteFixedFloatValue(path, section, "aimbotMaxAim", aimbotMaxAim);
            WriteFixedFloatValue(path, section, "aimbotMinCharge", aimbotMinCharge);
            WriteFixedFloatValue(path, section, "aimbotMaxCharge", aimbotMaxCharge);
            WriteBoolValue(path, section, "aimbotIgnoreInvisible", aimbotIgnoreInvisible);
            WriteIntValue(path, section, "aimbotTrace", aimbotTrace);
            WriteIntValue(path, section, "aimbotUnlock", aimbotUnlock);
            WriteFixedFloatValue(path, section, "aimbotLockTime", aimbotLockTime);
            WriteFixedFloatValue(path, section, "aimbotMaxDist", aimbotMaxDist);
            WriteFixedFloatValue(path, section, "aimbotMinDist", aimbotMinDist);
            WriteIntValue(path, section, "aimbotAttack", aimbotAttack);
            WriteIntValue(path, section, "aimbotTeam", aimbotTeam);
            WriteIntValue(path, section, "aimbotPriority", aimbotPriority);
            WriteFixedFloatValue(path, section, "aimbotTrackingDeadzone", aimbotTrackingDeadzone);
            WriteIntValue(path, section, "aimbotBoneMask", static_cast<int>(aimbotBoneMask));
            WriteIntValue(path, section, "aimbotBoneMask2", static_cast<int>(aimbotBoneMask2));
            WriteFixedFloatValue(path, section, "aimbotFlickShotClampMs", aimbotFlickShotClampMs);
            WriteFixedFloatValue(path, section, "aimbotFlickPostFireDelayMs", aimbotFlickPostFireDelayMs);
            WriteBoolValue(path, section, "aimbotFlickTrajectoryWait", aimbotFlickTrajectoryWait);
            WriteFixedFloatValue(path, section, "aimbotFlickTrajectoryWaitMs", aimbotFlickTrajectoryWaitMs);
            WriteFixedFloatValue(path, section, "aimbotFlickTrajectoryApexWindowMs", aimbotFlickTrajectoryApexWindowMs);
            WriteBoolValue(path, section, "aimbotFlick2ndTriggerGate", aimbotFlick2ndTriggerGate);
            WriteFixedFloatValue(path, section, "aimbotFlick2ndBoxPadding", aimbotFlick2ndBoxPadding);
            WriteFixedFloatValue(path, section, "aimbotFlick2ndInnerRadius", aimbotFlick2ndInnerRadius);
            WriteFixedFloatValue(path, section, "aimbotFlick2ndInnerSmoothScale", aimbotFlick2ndInnerSmoothScale);
            WriteIntValue(path, section, "aimbotFlick2ndInnerMethod", ClampAimMethodIndex(aimbotFlick2ndInnerMethod));
            WriteBoolValue(path, section, "aimbotTwoStage", aimbotTwoStage);
            WriteBoolValue(path, section, "aimbotTwoStageTriggerGate", aimbotTwoStageTriggerGate);
            WriteFixedFloatValue(path, section, "aimbotTwoStageBoxPadding", aimbotTwoStageBoxPadding);
            WriteFixedFloatValue(path, section, "aimbotTwoStageInnerRadius", aimbotTwoStageInnerRadius);
            WriteFixedFloatValue(path, section, "aimbotTwoStageInnerSmoothScale", aimbotTwoStageInnerSmoothScale);
            WriteBoolValue(path, section, "aimOvershootCurve", aimOvershootCurve);
            WriteFixedFloatValue(path, section, "aimOvershootGain", aimOvershootGain);
            WriteFixedFloatValue(path, section, "aimOvershootResetPixels", aimOvershootResetPixels);
            WriteBoolValue(path, section, "aimDryRun", aimDryRun);
            WriteBoolValue(path, section, "aimVerboseLog", aimVerboseLog);
            WriteIntValue(path, section, "aimDryRunLogIntervalMs", aimDryRunLogIntervalMs);

            WriteIntValue(path, section, "triggerbotMode", triggerbotMode);
            WriteIntValue(path, section, "triggerbotKey", triggerbotKey);
            WriteFixedFloatValue(path, section, "triggerbotShotInterval", triggerbotShotInterval);
            WriteBoolValue(path, section, "triggerbotChargeAware", triggerbotChargeAware);
            WriteFixedFloatValue(path, section, "triggerbotMinCharge", triggerbotMinCharge);
            WriteBoolValue(path, section, "triggerbotDisableWhileReloading", triggerbotDisableWhileReloading);
            WriteBoolValue(path, section, "triggerbotIgnoreInvisible", triggerbotIgnoreInvisible);
            WriteIntValue(path, section, "triggerbotBoneMask", static_cast<int>(triggerbotBoneMask));

            WriteIntValue(path, section, "triggerbotMode2", triggerbotMode2);
            WriteIntValue(path, section, "triggerbotKey2", triggerbotKey2);
            WriteFixedFloatValue(path, section, "triggerbotShotInterval2", triggerbotShotInterval2);
            WriteBoolValue(path, section, "triggerbotChargeAware2", triggerbotChargeAware2);
            WriteFixedFloatValue(path, section, "triggerbotMinCharge2", triggerbotMinCharge2);
            WriteBoolValue(path, section, "triggerbotDisableWhileReloading2", triggerbotDisableWhileReloading2);
            WriteBoolValue(path, section, "triggerbotIgnoreInvisible2", triggerbotIgnoreInvisible2);
            WriteIntValue(path, section, "triggerbotBoneMask2", static_cast<int>(triggerbotBoneMask2));
        }

        void WriteAimMethodPresetSection(const std::string& path,
                                         const char* section,
                                         const AimMethodPreset& rawPreset,
                                         int fallbackIndex)
        {
            const AimMethodPreset preset = ValidateAimMethodPresetValue(rawPreset, fallbackIndex);
            WriteIntValue(path, section, "id", preset.id);
            WriteStringValue(path, section, "name", preset.name.c_str());
            WriteIntValue(path, section, "method", preset.method);
            WriteFixedFloatValue(path, section, "angularSpeedScale", preset.angularSpeedScale);
            WriteFixedFloatValue(path, section, "pidP", preset.pidP);
            WriteFixedFloatValue(path, section, "pidI", preset.pidI);
            WriteFixedFloatValue(path, section, "pidD", preset.pidD);
            WriteFixedFloatValue(path, section, "pidMaxIntegral", preset.pidMaxIntegral);
            WriteFixedFloatValue(path, section, "pidDeadzone", preset.pidDeadzone);
            WriteIntValue(path, section, "bezierControlPoints", preset.bezierControlPoints);
            WriteFixedFloatValue(path, section, "bezierCurvature", preset.bezierCurvature);
            WriteFixedFloatValue(path, section, "bezierSpeed", preset.bezierSpeed);
            WriteFixedFloatValue(path, section, "piecewiseNearDegrees", preset.piecewiseNearDegrees);
            WriteFixedFloatValue(path, section, "piecewiseMidDegrees", preset.piecewiseMidDegrees);
            WriteFixedFloatValue(path, section, "piecewiseFarDegrees", preset.piecewiseFarDegrees);
            WriteFixedFloatValue(path, section, "piecewiseNearScale", preset.piecewiseNearScale);
            WriteFixedFloatValue(path, section, "piecewiseMidScale", preset.piecewiseMidScale);
            WriteFixedFloatValue(path, section, "piecewiseFarScale", preset.piecewiseFarScale);
            WriteFixedFloatValue(path, section, "accelLimitedAcceleration", preset.accelLimitedAcceleration);
            WriteFixedFloatValue(path, section, "constantAngularSpeedDeg", preset.constantAngularSpeedDeg);
        }

        void WriteAimBehaviorPresetSection(const std::string& path,
                                           const char* section,
                                           const AimBehaviorPreset& rawPreset,
                                           int fallbackIndex)
        {
            const AimBehaviorPreset preset = ValidateAimBehaviorPresetValue(rawPreset, fallbackIndex);
            WriteIntValue(path, section, "id", preset.id);
            WriteStringValue(path, section, "name", preset.name.c_str());
            WriteIntValue(path, section, "behavior", preset.behavior);
            WriteIntValue(path, section, "method", preset.method);
            WriteIntValue(path, section, "methodPresetId", preset.methodPresetId);
            WriteFixedFloatValue(path, section, "baseSpeed", preset.baseSpeed);
            WriteBoolValue(path, section, "startLimiterEnabled", preset.startLimiter.enabled);
            WriteFixedFloatValue(path, section, "startLimiterInitialCapDegPerSec", preset.startLimiter.initialCapDegPerSec);
            WriteFixedFloatValue(path, section, "startLimiterCapRiseDegPerSec2", preset.startLimiter.capRiseDegPerSec2);
            WriteBoolValue(path, section, "startLimiterRestartOnTargetChange", preset.startLimiter.restartOnTargetChange);
            WriteBoolValue(path, section, "moveSplitEnabled", preset.moveSplitEnabled);
            WriteIntValue(path, section, "moveSplitMaxPixels", preset.moveSplitMaxPixels);
            WriteIntValue(path, section, "moveSplitDelayUs", preset.moveSplitDelayUs);
        }

        void WriteDynamicFovPresetSection(const std::string& path,
                                          const char* section,
                                          const DynamicFovPreset& rawPreset,
                                          int fallbackIndex)
        {
            const DynamicFovPreset preset = ValidateDynamicFovPresetForConfig(rawPreset, fallbackIndex);
            WriteIntValue(path, section, "id", preset.id);
            WriteStringValue(path, section, "name", preset.name.c_str());
            WriteBoolValue(path, section, "smooth", preset.smooth);
            WriteIntValue(path, section, "pointCount", preset.pointCount);
            for (int index = 0; index < kMaxDynamicFovPoints; ++index) {
                char distanceKey[32] = {};
                char fovKey[32] = {};
                std::snprintf(distanceKey, sizeof(distanceKey), "point%dDistanceM", index);
                std::snprintf(fovKey, sizeof(fovKey), "point%dFovDeg", index);
                WriteFixedFloatValue(
                    path,
                    section,
                    distanceKey,
                    preset.points[static_cast<size_t>(index)].distanceM);
                WriteFixedFloatValue(
                    path,
                    section,
                    fovKey,
                    preset.points[static_cast<size_t>(index)].fovDeg);
            }
        }

        void SaveAimCustomPresetsUnlocked(const std::string& path)
        {
            ValidateAimCustomPresetsUnlocked();
            constexpr const char* section = kAimMethodSection;
            WriteIntValue(path, section, "methodPresetCount", static_cast<int>(aimMethodPresets.size()));
            for (size_t index = 0; index < aimMethodPresets.size(); ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "AimMethodPreset.%zu", index);
                WriteAimMethodPresetSection(path, presetSection, aimMethodPresets[index], static_cast<int>(index));
            }

            WriteIntValue(path, section, "behaviorPresetCount", static_cast<int>(aimBehaviorPresets.size()));
            for (size_t index = 0; index < aimBehaviorPresets.size(); ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "AimBehaviorPreset.%zu", index);
                WriteAimBehaviorPresetSection(path, presetSection, aimBehaviorPresets[index], static_cast<int>(index));
            }

            WriteIntValue(path, section, "dynamicFovPresetCount", static_cast<int>(dynamicFovPresets.size()));
            for (size_t index = 0; index < dynamicFovPresets.size(); ++index) {
                char presetSection[64] = {};
                std::snprintf(presetSection, sizeof(presetSection), "DynamicFovPreset.%zu", index);
                WriteDynamicFovPresetSection(path, presetSection, dynamicFovPresets[index], static_cast<int>(index));
            }
        }

        void SaveAimMethodSettingsUnlocked(const std::string& path)
        {
            constexpr const char* section = kAimMethodSection;
            constexpr std::array<const char*, kAimBehaviorCount> methodKeys = {
                "trackingMethod",
                "flickMethod",
                "flick2ndMethod",
                "reacquireMethod",
                "magneticTriggerMethod"
            };
            constexpr std::array<const char*, kAimBehaviorCount> speedKeys = {
                "trackingBaseSpeed",
                "flickBaseSpeed",
                "flick2ndBaseSpeed",
                "reacquireBaseSpeed",
                "magneticTriggerBaseSpeed"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterEnabledKeys = {
                "trackingStartLimiterEnabled",
                "flickStartLimiterEnabled",
                "flick2ndStartLimiterEnabled",
                "reacquireStartLimiterEnabled",
                "magneticTriggerStartLimiterEnabled"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterInitialCapKeys = {
                "trackingStartLimiterInitialCapDegPerSec",
                "flickStartLimiterInitialCapDegPerSec",
                "flick2ndStartLimiterInitialCapDegPerSec",
                "reacquireStartLimiterInitialCapDegPerSec",
                "magneticTriggerStartLimiterInitialCapDegPerSec"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterCapRiseKeys = {
                "trackingStartLimiterCapRiseDegPerSec2",
                "flickStartLimiterCapRiseDegPerSec2",
                "flick2ndStartLimiterCapRiseDegPerSec2",
                "reacquireStartLimiterCapRiseDegPerSec2",
                "magneticTriggerStartLimiterCapRiseDegPerSec2"
            };
            constexpr std::array<const char*, kAimBehaviorCount> startLimiterRestartKeys = {
                "trackingStartLimiterRestartOnTargetChange",
                "flickStartLimiterRestartOnTargetChange",
                "flick2ndStartLimiterRestartOnTargetChange",
                "reacquireStartLimiterRestartOnTargetChange",
                "magneticTriggerStartLimiterRestartOnTargetChange"
            };
            constexpr std::array<const char*, kAimBehaviorCount> methodPresetKeys = {
                "trackingMethodPresetId",
                "flickMethodPresetId",
                "flick2ndMethodPresetId",
                "reacquireMethodPresetId",
                "magneticTriggerMethodPresetId"
            };
            constexpr std::array<const char*, kAimBehaviorCount> accelerationKeys = {
                "trackingAcceleration",
                "flickAcceleration",
                "flick2ndAcceleration",
                "reacquireAcceleration",
                "magneticTriggerAcceleration"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitEnabledKeys = {
                "trackingMoveSplitEnabled",
                "flickMoveSplitEnabled",
                "flick2ndMoveSplitEnabled",
                "reacquireMoveSplitEnabled",
                "magneticTriggerMoveSplitEnabled"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitMaxPixelsKeys = {
                "trackingMoveSplitMaxPixels",
                "flickMoveSplitMaxPixels",
                "flick2ndMoveSplitMaxPixels",
                "reacquireMoveSplitMaxPixels",
                "magneticTriggerMoveSplitMaxPixels"
            };
            constexpr std::array<const char*, kAimBehaviorCount> splitDelayUsKeys = {
                "trackingMoveSplitDelayUs",
                "flickMoveSplitDelayUs",
                "flick2ndMoveSplitDelayUs",
                "reacquireMoveSplitDelayUs",
                "magneticTriggerMoveSplitDelayUs"
            };
            constexpr std::array<const char*, kAimMethodCount> angularSpeedKeys = {
                "linearAngularSpeedScale",
                "pidAngularSpeedScale",
                "bezierAngularSpeedScale",
                "piecewiseAngularSpeedScale",
                "accelLimitedAngularSpeedScale",
                "constantAngularSpeedScale"
            };

            WriteIntValue(path, section, "aimMethod", aimMethod);
            WriteFixedFloatValue(path, section, "aimPidP", aimPidP);
            WriteFixedFloatValue(path, section, "aimPidI", aimPidI);
            WriteFixedFloatValue(path, section, "aimPidD", aimPidD);
            WriteFixedFloatValue(path, section, "aimPidMaxIntegral", aimPidMaxIntegral);
            WriteFixedFloatValue(path, section, "aimPidDeadzone", aimPidDeadzone);
            WriteIntValue(path, section, "aimBezierControlPoints", aimBezierControlPoints);
            WriteFixedFloatValue(path, section, "aimBezierCurvature", aimBezierCurvature);
            WriteFixedFloatValue(path, section, "aimBezierSpeed", aimBezierSpeed);
            WriteFixedFloatValue(path, section, "aimPiecewiseNearDegrees", AimPiecewiseNearDegrees());
            WriteFixedFloatValue(path, section, "aimPiecewiseMidDegrees", AimPiecewiseMidDegrees());
            WriteFixedFloatValue(path, section, "aimPiecewiseFarDegrees", AimPiecewiseFarDegrees());
            WriteFixedFloatValue(path, section, "aimPiecewiseNearScale", AimPiecewiseNearScale());
            WriteFixedFloatValue(path, section, "aimPiecewiseMidScale", AimPiecewiseMidScale());
            WriteFixedFloatValue(path, section, "aimPiecewiseFarScale", AimPiecewiseFarScale());
            WriteFixedFloatValue(path, section, "aimAccelLimitedAcceleration", AimMethodAcceleration(4));
            WriteFixedFloatValue(path, section, "aimConstantAngularSpeedDeg", AimConstantAngularSpeedDeg());
            WriteIntValue(path, section, "aimBehaviorPresetId", ClampAimBehaviorPresetId(aimBehaviorPresetId));

            for (size_t index = 0; index < aimBehaviorMethod.size(); ++index) {
                WriteIntValue(path, section, methodKeys[index], ClampAimMethodIndex(aimBehaviorMethod[index]));
                WriteIntValue(path, section, methodPresetKeys[index], ClampAimMethodPresetId(aimBehaviorMethodPreset[index]));
                const float baseSpeed = std::isfinite(aimBehaviorBaseSpeed[index])
                    ? std::clamp(aimBehaviorBaseSpeed[index], 0.0f, 100.0f)
                    : 100.0f;
                WriteFixedFloatValue(path, section, speedKeys[index], baseSpeed);
                const AimStartLimiterProfile startLimiter =
                    ValidateAimStartLimiterProfile(aimBehaviorStartLimiterProfiles[index]);
                WriteBoolValue(path, section, startLimiterEnabledKeys[index], startLimiter.enabled);
                WriteFixedFloatValue(path, section, startLimiterInitialCapKeys[index], startLimiter.initialCapDegPerSec);
                WriteFixedFloatValue(path, section, startLimiterCapRiseKeys[index], startLimiter.capRiseDegPerSec2);
                WriteBoolValue(path, section, startLimiterRestartKeys[index], startLimiter.restartOnTargetChange);
                const float legacyAcceleration = std::isfinite(aimBehaviorAcceleration[index])
                    ? std::clamp(aimBehaviorAcceleration[index], 0.0f, 20.0f)
                    : 0.1f;
                WriteFixedFloatValue(path, section, accelerationKeys[index], legacyAcceleration);
                WriteBoolValue(path, section, splitEnabledKeys[index], aimBehaviorMoveSplitEnabled[index]);
                WriteIntValue(path, section, splitMaxPixelsKeys[index], ClampMoveSplitMaxPixels(aimBehaviorMoveSplitMaxPixels[index]));
                WriteIntValue(path, section, splitDelayUsKeys[index], ClampMoveSplitDelayUs(aimBehaviorMoveSplitDelayUs[index]));
            }
            for (size_t index = 0; index < aimMethodAngularSpeedScale.size(); ++index)
                WriteFixedFloatValue(path, section, angularSpeedKeys[index],
                    AimMethodAngularSpeedScale(static_cast<int>(index)) * 100.0f);
            WriteIntValue(path, section, "secondaryTrackingMethod", ClampAimMethodOverride(secondaryAimMethodOverride[0]));
            WriteIntValue(path, section, "secondaryFlickMethod", ClampAimMethodOverride(secondaryAimMethodOverride[1]));
            SaveAimCustomPresetsUnlocked(path);
        }

        void LoadGlobalSettingsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = "Global";

            draw_hp_pack = ReadBool(ini, section, "draw_hp_pack", draw_hp_pack);
            crosscircle = ReadBool(ini, section, "crosscircle", crosscircle);
            eyeray = ReadBool(ini, section, "eyeray", eyeray);
            draw_info = ReadBool(ini, section, "draw_info", draw_info);
            drawbattletag = ReadBool(ini, section, "drawbattletag", drawbattletag);
            drawhealth = ReadBool(ini, section, "drawhealth", drawhealth);
            healthbar = ReadBool(ini, section, "healthbar", healthbar);
            healthbar2 = ReadBool(ini, section, "healthbar2", healthbar2);
            healthbartextsize = ReadFixedFloat(ini, section, "healthbartextsize", healthbartextsize);
            dist = ReadBool(ini, section, "dist", dist);
            visualMaxDist = ReadFixedFloat(ini, section, "visualMaxDist", visualMaxDist);
            name = ReadBool(ini, section, "name", name);
            ult = ReadBool(ini, section, "ult", ult);
            draw_skel = ReadBool(ini, section, "draw_skel", draw_skel);
            draw_avatar = ReadBool(ini, section, "draw_avatar", draw_avatar);
            draw_hitbox = ReadBool(ini, section, "draw_hitbox", draw_hitbox);
            skillinfo = ReadBool(ini, section, "skillinfo", skillinfo);
            ultimateDisplayMode = ReadInt(ini, section, "ultimateDisplayMode", ultimateDisplayMode);
            skillDisplayMode = ReadInt(ini, section, "skillDisplayMode", skillDisplayMode);
            radarCorner = ReadInt(ini, section, "radarCorner", radarCorner);
            radar = ReadBool(ini, section, "radar", radar);
            radarline = ReadBool(ini, section, "radarline", radarline);
            drawline = ReadBool(ini, section, "drawline", drawline);
            draw_fov = ReadBool(ini, section, "draw_fov", draw_fov);
            drawTrackingDeadzones = ReadBool(ini, section, "drawTrackingDeadzones", drawTrackingDeadzones);
            boxPerfMode = ReadBool(ini, section, "boxPerfMode", boxPerfMode);
            boxPerfFastRect = ReadBool(ini, section, "boxPerfFastRect", boxPerfFastRect);
            targetPriority = ReadInt(ini, section, "targetPriority", targetPriority);
            MenuToggleKey = ReadInt(ini, section, "MenuToggleKey", MenuToggleKey);
            lastConfigProfile = ReadString(ini, section, "lastConfigProfile", lastConfigProfile.c_str());
            uiLanguage = ReadInt(ini, section, "uiLanguage", uiLanguage);
            manualScreenWidth = ReadInt(ini, section, "manualScreenWidth", manualScreenWidth);
            manualScreenHeight = ReadInt(ini, section, "manualScreenHeight", manualScreenHeight);

            LoadColor(ini, section, "EnemyCol", EnemyCol);
            LoadColor(ini, section, "fovcol", fovcol);
            LoadColor(ini, section, "invisenargb", invisnenargb);
            LoadColor(ini, section, "enargb", enargb);
            LoadColor(ini, section, "targetargb", targetargb);
            LoadColor(ini, section, "allyargb", allyargb);
            LoadFovRingStyles(ini);
        }

        void LoadKmboxSettingsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = "KMBox";

            kmboxEnabled = ReadBool(ini, section, "kmboxEnabled", kmboxEnabled);
            kmboxDeviceType = ReadInt(ini, section, "kmboxDeviceType", kmboxDeviceType);
            CopyString(kmboxIp, ReadString(ini, section, "kmboxIp", kmboxIp));
            kmboxPort = ReadInt(ini, section, "kmboxPort", kmboxPort);
            kmboxMonitorPort = ReadInt(ini, section, "kmboxMonitorPort", RecommendedKmboxMonitorPort(kmboxPort));
            kmboxMonitorPortManualOverride = ReadBool(
                ini,
                section,
                "kmboxMonitorPortManualOverride",
                false);
            CopyString(kmboxMac, ReadString(ini, section, "kmboxMac", kmboxMac));
            CopyString(kmboxComPort, ReadString(ini, section, "kmboxComPort", kmboxComPort));
            kmboxCountsPerRadian = ReadFixedFloat(
                ini,
                section,
                KeyExists(ini, section, "kmboxCountsPerRadian") ? "kmboxCountsPerRadian" : "kmboxAimSensitivity",
                kmboxCountsPerRadian);
            gameMouseSensitivity = ReadFixedFloat(ini, section, "gameMouseSensitivity", gameMouseSensitivity);
            autoReadGameMouseSensitivity = ReadBool(
                ini,
                section,
                "autoReadGameMouseSensitivity",
                autoReadGameMouseSensitivity);
            detectedGameMouseSensitivity = 0.0f;
            gameMouseSensitivityAutoDetected = false;
            referenceGameSensitivity = ReadFixedFloat(
                ini,
                section,
                KeyExists(ini, section, "referenceGameSensitivity") ? "referenceGameSensitivity" : "sensReference",
                referenceGameSensitivity);
            autoScaleByGameSensitivity = ReadBool(
                ini,
                section,
                KeyExists(ini, section, "autoScaleByGameSensitivity") ? "autoScaleByGameSensitivity" : "autoSyncSensitivity",
                autoScaleByGameSensitivity);
            calibratedCountsPerRadian = ReadFixedFloat(
                ini,
                section,
                KeyExists(ini, section, "calibratedCountsPerRadian") ? "calibratedCountsPerRadian" : "calibratedPixelsPerRadian",
                calibratedCountsPerRadian);
            calibratedPitchCountsPerRadian = ReadFixedFloat(
                ini,
                section,
                KeyExists(ini, section, "calibratedPitchCountsPerRadian") ? "calibratedPitchCountsPerRadian" : "calibratedPixelsPerRadianPitch",
                calibratedPitchCountsPerRadian);
            hostMouseDpi = ReadFixedFloat(ini, section, "hostMouseDpi", hostMouseDpi);
            detectedHostMouseDpi = 0.0f;
            hostMouseDpiAutoDetected = false;
            kmboxInputDelayMs = ReadInt(ini, section, "kmboxInputDelayMs", kmboxInputDelayMs);
            kmboxDebugLog = ReadBool(ini, section, "kmboxDebugLog", kmboxDebugLog);
            kmboxSuppressOutputWhileMenuOpen = ReadBool(
                ini,
                section,
                "kmboxSuppressOutputWhileMenuOpen",
                kmboxSuppressOutputWhileMenuOpen);
        }

        void SaveKmboxSettingsUnlocked(const std::string& path)
        {
            WriteBoolValue(path, "KMBox", "kmboxEnabled", kmboxEnabled);
            WriteIntValue(path, "KMBox", "kmboxDeviceType", kmboxDeviceType);
            WriteStringValue(path, "KMBox", "kmboxIp", kmboxIp);
            WriteIntValue(path, "KMBox", "kmboxPort", kmboxPort);
            WriteIntValue(path, "KMBox", "kmboxMonitorPort", kmboxMonitorPort);
            WriteBoolValue(path, "KMBox", "kmboxMonitorPortManualOverride", kmboxMonitorPortManualOverride);
            WriteStringValue(path, "KMBox", "kmboxMac", kmboxMac);
            WriteStringValue(path, "KMBox", "kmboxComPort", kmboxComPort);
            WriteFixedFloatValue(path, "KMBox", "kmboxCountsPerRadian", kmboxCountsPerRadian);
            WriteFixedFloatValue(path, "KMBox", "kmboxAimSensitivity", kmboxCountsPerRadian);
            WriteFixedFloatValue(path, "KMBox", "gameMouseSensitivity", gameMouseSensitivity);
            WriteBoolValue(path, "KMBox", "autoReadGameMouseSensitivity", autoReadGameMouseSensitivity);
            WriteFixedFloatValue(path, "KMBox", "referenceGameSensitivity", referenceGameSensitivity);
            WriteFixedFloatValue(path, "KMBox", "sensReference", referenceGameSensitivity);
            WriteBoolValue(path, "KMBox", "autoScaleByGameSensitivity", autoScaleByGameSensitivity);
            WriteBoolValue(path, "KMBox", "autoSyncSensitivity", autoScaleByGameSensitivity);
            WriteFixedFloatValue(path, "KMBox", "calibratedCountsPerRadian", calibratedCountsPerRadian);
            WriteFixedFloatValue(path, "KMBox", "calibratedPitchCountsPerRadian", calibratedPitchCountsPerRadian);
            WriteFixedFloatValue(path, "KMBox", "hostMouseDpi", hostMouseDpi);
            WriteIntValue(path, "KMBox", "kmboxInputDelayMs", kmboxInputDelayMs);
            WriteBoolValue(path, "KMBox", "kmboxDebugLog", kmboxDebugLog);
            WriteBoolValue(
                path,
                "KMBox",
                "kmboxSuppressOutputWhileMenuOpen",
                kmboxSuppressOutputWhileMenuOpen);
        }

        template <typename T>
        void ClampSetting(const char* name, T& value, T minValue, T maxValue, T fallback)
        {
            if (value < minValue || value > maxValue) {
                const T oldValue = value;
                value = std::clamp(value, minValue, maxValue);
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s out of range (%s); clamped to %s.",
                    name, ToText(oldValue).c_str(), ToText(value).c_str());
                if (IsAimDiagnosticSetting(name)) {
                    Diagnostics::Aim("config.clamp name=%s old=%s new=%s min=%s max=%s",
                        name,
                        ToText(oldValue).c_str(),
                        ToText(value).c_str(),
                        ToText(minValue).c_str(),
                        ToText(maxValue).c_str());
                }
            }
            (void)fallback;
        }

        void NormalizeBoneSetting(const char* name, int& value)
        {
            const int normalized = NormalizeAimBone(value);
            if (normalized != value) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s invalid aim-bone choice (%d); using %d (%s).",
                    name, value, normalized, AimBoneName(normalized));
            }
            value = normalized;
        }

        void ClampFloatSetting(const char* name, float& value, float minValue, float maxValue, float fallback)
        {
            if (!std::isfinite(value)) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s is not finite; using default %s.",
                    name, ToText(fallback).c_str());
                if (IsAimDiagnosticSetting(name)) {
                    Diagnostics::Aim("config.clamp name=%s old=non_finite new=%s reason=non_finite",
                        name, ToText(fallback).c_str());
                }
                value = fallback;
            }
            if (value < minValue || value > maxValue) {
                const float oldValue = value;
                value = std::clamp(value, minValue, maxValue);
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s out of range (%s); clamped to %s.",
                    name, ToText(oldValue).c_str(), ToText(value).c_str());
                if (IsAimDiagnosticSetting(name)) {
                    Diagnostics::Aim("config.clamp name=%s old=%s new=%s min=%.4f max=%.4f",
                        name,
                        ToText(oldValue).c_str(),
                        ToText(value).c_str(),
                        minValue,
                        maxValue);
                }
            }
        }

        void ClampColorChannel(const char* name, float& value)
        {
            ClampFloatSetting(name, value, 0.0f, 1.0f, 1.0f);
        }

        void ClampColor(const char* name, ImVec4& color)
        {
            ClampColorChannel((std::string(name) + ".x").c_str(), color.x);
            ClampColorChannel((std::string(name) + ".y").c_str(), color.y);
            ClampColorChannel((std::string(name) + ".z").c_str(), color.z);
            ClampColorChannel((std::string(name) + ".w").c_str(), color.w);
        }

        void ValidateUnlocked()
        {
            config_version = kCurrentConfigVersion;

            ClampSetting("AimKey", AimKey, 1, 255, 0x01);
            ClampSetting("aim_key", aim_key, 0, MaxActivationKeyIndex(), 1);
            ClampSetting("aim_key2", aim_key2, 0, MaxActivationKeyIndex(), 1);
            ClampSetting("togglekey", togglekey, 0, 54, 0);
            ClampSetting("MenuToggleKey", MenuToggleKey, 1, 255, VK_HOME);
            ClampSetting("aimDryRunLogIntervalMs", aimDryRunLogIntervalMs, 50, 1000, 100);
            if (ultimateDisplayMode != 1) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "ultimateDisplayMode=%d is legacy-only; using left side panel.",
                    ultimateDisplayMode);
                ultimateDisplayMode = 1;
            }
            ClampSetting("skillDisplayMode", skillDisplayMode, 0, 2, 0);
            ClampSetting("radarCorner", radarCorner, 0, 3, 0);

            ClampFloatSetting("Fov", Fov, kMinFovDeg, kMaxFovDeg, kDefaultFovDeg);
            ClampFloatSetting("Fov2", Fov2, kMinFovDeg, kMaxFovDeg, kDefaultFovDeg);
            ClampFloatSetting("minFov1", minFov1, kMinFovDeg, kMaxFovDeg, kDefaultFovDeg);
            ClampFloatSetting("minFov2", minFov2, kMinFovDeg, kMaxFovDeg, kDefaultFovDeg);
            ClampFloatSetting("Smooth", Smooth, 0.0f, 100.0f, 5.0f);
            ClampFloatSetting("hitbox", hitbox, kMinHitboxScalePercent, kMaxHitboxScalePercent, kDefaultHitboxScalePercent);
            ClampFloatSetting("hitbox2", hitbox2, kMinHitboxScalePercent, kMaxHitboxScalePercent, kDefaultHitboxScalePercent);
            ClampFloatSetting("missbox", missbox, 0.0f, 5.0f, 0.6f);
            ClampFloatSetting("Tracking_smooth", Tracking_smooth, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Tracking_smooth2", Tracking_smooth2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Flick_smooth", Flick_smooth, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Flick_smooth2", Flick_smooth2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("accvalue", accvalue, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("accvalue2", accvalue2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("bladespeed", bladespeed, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("predit_level", predit_level, 0.0f, 2000.0f, 110.0f);
            ClampFloatSetting("comarea", comarea, 0.0f, 20.0f, 0.01f);
            ClampFloatSetting("comspeed", comspeed, 0.0f, 20.0f, 0.5f);
            ClampFloatSetting("aimbotTriggerDelay", aimbotTriggerDelay, 0.0f, 5000.0f, 0.0f);
            ClampFloatSetting("aimbotMaxHead", aimbotMaxHead, 0.0f, 100.0f, 100.0f);
            ClampFloatSetting("aimPidP", aimPidP, 0.0f, 2.0f, 0.5f);
            ClampFloatSetting("aimPidI", aimPidI, 0.0f, 0.5f, 0.01f);
            ClampFloatSetting("aimPidD", aimPidD, 0.0f, 1.0f, 0.1f);
            ClampFloatSetting("aimPidMaxIntegral", aimPidMaxIntegral, 1.0f, 50.0f, 10.0f);
            ClampFloatSetting("aimPidDeadzone", aimPidDeadzone, 0.0f, 10.0f, 1.0f);
            ClampFloatSetting("aimBezierCurvature", aimBezierCurvature, 0.0f, 1.0f, 0.5f);
            ClampFloatSetting("aimBezierSpeed", aimBezierSpeed, 1.0f, 200.0f, 50.0f);
            ClampFloatSetting("aimPiecewiseNearDegrees", aimPiecewiseNearDegrees, 0.0f, 30.0f, 2.0f);
            ClampFloatSetting("aimPiecewiseMidDegrees", aimPiecewiseMidDegrees, aimPiecewiseNearDegrees, 45.0f, 6.0f);
            ClampFloatSetting("aimPiecewiseFarDegrees", aimPiecewiseFarDegrees, aimPiecewiseMidDegrees, 60.0f, 12.0f);
            ClampFloatSetting("aimPiecewiseNearScale", aimPiecewiseNearScale, 0.0f, 1.0f, 0.20f);
            ClampFloatSetting("aimPiecewiseMidScale", aimPiecewiseMidScale, 0.0f, 1.0f, 0.45f);
            ClampFloatSetting("aimPiecewiseFarScale", aimPiecewiseFarScale, 0.0f, 1.0f, 0.75f);
            ClampFloatSetting("aimAccelLimitedAcceleration", aimAccelLimitedAcceleration, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("aimConstantAngularSpeedDeg", aimConstantAngularSpeedDeg, 0.0f, kAimConstantAngularSpeedMaxDeg, 30.0f);
            ClampFloatSetting("aimbotStickiness", aimbotStickiness, 0.0f, 100.0f, 100.0f);
            ClampFloatSetting("aimbotSmoothY", aimbotSmoothY, 0.0f, 100.0f, 50.0f);
            ClampFloatSetting("aimbotPitchScale", aimbotPitchScale, 0.1f, 3.0f, 1.0f);
            ClampFloatSetting("aimbotMaxAim", aimbotMaxAim, 0.0f, 100.0f, 100.0f);
            ClampFloatSetting("aimbotMinCharge", aimbotMinCharge, 0.0f, 100.0f, 5.0f);
            ClampFloatSetting("aimbotMaxCharge", aimbotMaxCharge, 0.0f, 100.0f, 100.0f);
            ClampFloatSetting("aimbotLockTime", aimbotLockTime, 0.0f, 5000.0f, 20.0f);
            ClampFloatSetting("aimbotMaxDist", aimbotMaxDist, 0.0f, 500.0f, 100.0f);
            ClampFloatSetting("aimbotMinDist", aimbotMinDist, 0.0f, 500.0f, 0.0f);
            aimbotTrackingDeadzone = ClampTrackingDeadzonePixels(aimbotTrackingDeadzone);
            aimbotFlickShotClampMs = ClampFlickShotClampMs(aimbotFlickShotClampMs);
            aimbotFlickPostFireDelayMs = ClampFlickPostFireDelayMs(aimbotFlickPostFireDelayMs);
            aimbotFlickTrajectoryWaitMs = ClampTrajectoryWaitMs(aimbotFlickTrajectoryWaitMs);
            aimbotFlickTrajectoryApexWindowMs = ClampTrajectoryApexWindowMs(aimbotFlickTrajectoryApexWindowMs);
            ClampFloatSetting("aimbotFlick2ndBoxPadding", aimbotFlick2ndBoxPadding, 0.0f, 80.0f, 8.0f);
            ClampFloatSetting("aimbotFlick2ndInnerRadius", aimbotFlick2ndInnerRadius, 0.0f, 250.0f, 34.0f);
            ClampFloatSetting("aimbotFlick2ndInnerSmoothScale", aimbotFlick2ndInnerSmoothScale, 0.1f, 1.0f, 0.55f);
            aimbotFlick2ndInnerMethod = ClampAimMethodIndex(aimbotFlick2ndInnerMethod);
            ClampFloatSetting("aimbotTwoStageBoxPadding", aimbotTwoStageBoxPadding, 0.0f, 80.0f, 8.0f);
            ClampFloatSetting("aimbotTwoStageInnerRadius", aimbotTwoStageInnerRadius, 0.0f, 250.0f, 34.0f);
            ClampFloatSetting("aimbotTwoStageInnerSmoothScale", aimbotTwoStageInnerSmoothScale, 0.1f, 1.0f, 0.55f);
            ClampFloatSetting("aimOvershootGain", aimOvershootGain, 0.0f, 1.0f, 0.25f);
            ClampFloatSetting("aimOvershootResetPixels", aimOvershootResetPixels, 1.0f, 250.0f, 56.0f);
            ClampFloatSetting("meleehealth", meleehealth, 0.0f, 1000.0f, 30.0f);
            ClampFloatSetting("meleedistance", meleedistance, 0.0f, 500.0f, 5.0f);
            ClampFloatSetting("AutoRMBhealth", AutoRMBhealth, 0.0f, 1000.0f, 100.0f);
            ClampFloatSetting("AutoRMBdistance", AutoRMBdistance, 0.0f, 500.0f, 30.0f);
            ClampFloatSetting("SkillHealth", SkillHealth, 0.0f, 1000.0f, 50.0f);
            ClampFloatSetting("healthbartextsize", healthbartextsize, 4.0f, 72.0f, 16.0f);
            ClampFloatSetting("visualMaxDist", visualMaxDist, 0.0f, 1000.0f, 100.0f);
            ClampFloatSetting("lasthealth", lasthealth, 0.0f, 1000.0f, 0.0f);
            ClampFloatSetting("health", health, 0.0f, 1000.0f, 0.0f);

            ClampSetting("triggerbotMode", triggerbotMode, 0, 2, 0);
            ClampSetting("triggerbotKey", triggerbotKey, 0, MaxActivationKeyIndex(), 1);
            ClampFloatSetting("triggerbotShotInterval", triggerbotShotInterval, 0.0f, 100.0f, 0.0f);
            ClampFloatSetting("triggerbotMinCharge", triggerbotMinCharge, 0.0f, 100.0f, 30.0f);
            ClampSetting("triggerbotMode2", triggerbotMode2, 0, 2, 0);
            ClampSetting("triggerbotKey2", triggerbotKey2, 0, MaxActivationKeyIndex(), 1);
            ClampFloatSetting("triggerbotShotInterval2", triggerbotShotInterval2, 0.0f, 100.0f, 0.0f);
            ClampFloatSetting("triggerbotMinCharge2", triggerbotMinCharge2, 0.0f, 100.0f, 30.0f);
            triggerbotBoneMask = NormalizeTriggerBoneMask(triggerbotBoneMask);
            triggerbotBoneMask2 = NormalizeTriggerBoneMask(triggerbotBoneMask2);
            aimbotBoneMask = NormalizeSkeletonBoneMask(aimbotBoneMask);
            aimbotBoneMask2 = NormalizeSkeletonBoneMask(aimbotBoneMask2);

            NormalizeBoneSetting("TargetBone", TargetBone);
            NormalizeBoneSetting("Bone", Bone);
            NormalizeBoneSetting("Bone2", Bone2);
            TargetBone = Bone;
            BoneName = AimBoneName(Bone);
            BoneName2 = AimBoneName(Bone2);
            ClampSetting("targetdelaytime", targetdelaytime, 0, 10000, 200);
            ClampSetting("hiboxdelaytime", hiboxdelaytime, 0, 10000, 200);
            ClampSetting("shotcount", shotcount, 0, 1000, 0);
            ClampSetting("shotmanydont", shotmanydont, 0, 1000, 3);
            ClampSetting("Shoottime", Shoottime, 0, 10000, 500);
            ClampSetting("lasttime", lasttime, 0, (std::numeric_limits<int>::max)(), 0);
            ClampSetting("slasttime", slasttime, 0, (std::numeric_limits<int>::max)(), 0);
            ClampSetting("Qstarttime", Qstarttime, 0, (std::numeric_limits<int>::max)(), 0);
            ClampSetting("Qtime", Qtime, 0, (std::numeric_limits<int>::max)(), 0);
            ClampSetting("lastenemy", lastenemy, -1, 100000, -1);
            ClampSetting("timebeforedelay", timebeforedelay, 0, (std::numeric_limits<int>::max)(), 0);
            ClampSetting("Targetenemyi", Targetenemyi, -1, 100000, -1);
            ClampSetting("Targetenemyifov", Targetenemyifov, -1, 100000, -1);
            ClampSetting("doingentity", doingentity, 0, 1, 1);
            ClampSetting("lastheroid", lastheroid, -2, (std::numeric_limits<int>::max)(), -2);
            ClampSetting("targetPriority", targetPriority, 0, 2, 0);
            ClampSetting("aimMethod", aimMethod, 0, kAimMethodCount - 1, 0);
            for (size_t index = 0; index < aimBehaviorMethod.size(); ++index) {
                aimBehaviorMethod[index] = ClampAimMethodIndex(aimBehaviorMethod[index]);
                aimBehaviorMethodPreset[index] = ClampAimMethodPresetId(aimBehaviorMethodPreset[index]);
                if (!std::isfinite(aimBehaviorBaseSpeed[index]))
                    aimBehaviorBaseSpeed[index] = 100.0f;
                if (!std::isfinite(aimBehaviorAcceleration[index]))
                    aimBehaviorAcceleration[index] = 0.1f;
                aimBehaviorBaseSpeed[index] = std::clamp(aimBehaviorBaseSpeed[index], 0.0f, 100.0f);
                aimBehaviorAcceleration[index] = std::clamp(aimBehaviorAcceleration[index], 0.0f, 20.0f);
                aimBehaviorStartLimiterProfiles[index] =
                    ValidateAimStartLimiterProfile(aimBehaviorStartLimiterProfiles[index]);
                aimBehaviorMoveSplitMaxPixels[index] = ClampMoveSplitMaxPixels(aimBehaviorMoveSplitMaxPixels[index]);
                aimBehaviorMoveSplitDelayUs[index] = ClampMoveSplitDelayUs(aimBehaviorMoveSplitDelayUs[index]);
            }
            for (float& speedScale : aimMethodAngularSpeedScale) {
                if (!std::isfinite(speedScale))
                    speedScale = 100.0f;
                speedScale = std::clamp(speedScale, 0.0f, 200.0f);
            }
            for (int& method : secondaryAimMethodOverride)
                method = ClampAimMethodOverride(method);
            ValidateAimCustomPresetsUnlocked();
            ClampSetting("aimbotSmoothType", aimbotSmoothType, 0, 2, 0);
            ClampSetting("aimbotPredictionMode", aimbotPredictionMode, 0, 2, 0);
            aimBehavior = ClampAimBehaviorIndex(aimBehavior);
            aimBehaviorPresetId = ClampAimBehaviorPresetId(aimBehaviorPresetId);
            if (const AimBehaviorPreset* behaviorPreset = FindAimBehaviorPreset(aimBehaviorPresetId))
                aimBehavior = ClampAimBehaviorIndex(behaviorPreset->behavior);
            ClampSetting("aimbotFirePolicy", aimbotFirePolicy, 0, 5, 1);
            aimbotTwoStage = IsFlick2ndBehavior(aimBehavior);
            aimbotTwoStageTriggerGate = aimbotFlick2ndTriggerGate;
            aimbotTwoStageBoxPadding = aimbotFlick2ndBoxPadding;
            aimbotTwoStageInnerRadius = aimbotFlick2ndInnerRadius;
            aimbotTwoStageInnerSmoothScale = aimbotFlick2ndInnerSmoothScale;
            ApplyAimMode(IsTrackingBehavior(aimBehavior) ? 0 : 1);
            ClampSetting("aimBezierControlPoints", aimBezierControlPoints, 2, 6, 2);
            ClampSetting("aimbotTrace", aimbotTrace, 0, 2, 0);
            ClampSetting("aimbotUnlock", aimbotUnlock, 0, 2, 0);
            ClampSetting("aimbotAttack", aimbotAttack, 0, 7, 0);
            ClampSetting("aimbotTeam", aimbotTeam, 0, 2, 0);
            ClampSetting("aimbotPriority", aimbotPriority, 0, 2, 0);
            ClampSetting("uiLanguage", uiLanguage, 0, kUiLanguageCount - 1, kUiLanguageEnglish);
            ClampSetting("kmboxDeviceType", kmboxDeviceType, 0, 2, 0);
            ClampSetting("kmboxPort", kmboxPort, 1, 65535, kDefaultKmboxPort);
            NormalizeKmboxPortsUnlocked("validate");
            ClampSetting("kmboxInputDelayMs", kmboxInputDelayMs, 0, 20, kDefaultKmboxInputDelayMs);
            ClampSetting("manualScreenWidth", manualScreenWidth, 0, 16384, 1920);
            ClampSetting("manualScreenHeight", manualScreenHeight, 0, 16384, 1080);
            ClampFloatSetting("kmboxCountsPerRadian", kmboxCountsPerRadian, 0.1f, 20000.0f, 100.0f);
            ClampFloatSetting("gameMouseSensitivity", gameMouseSensitivity, 0.01f, 100.0f, 15.0f);
            ClampFloatSetting("referenceGameSensitivity", referenceGameSensitivity, 0.01f, 100.0f, 15.0f);
            if (!autoReadGameMouseSensitivity) {
                detectedGameMouseSensitivity = 0.0f;
                gameMouseSensitivityAutoDetected = false;
            } else if (!std::isfinite(detectedGameMouseSensitivity) ||
                       detectedGameMouseSensitivity <= 0.0f ||
                       detectedGameMouseSensitivity >= 100.0f) {
                detectedGameMouseSensitivity = 0.0f;
                gameMouseSensitivityAutoDetected = false;
            }
            ClampFloatSetting("calibratedCountsPerRadian", calibratedCountsPerRadian, 0.0f, 20000.0f, 0.0f);
            ClampFloatSetting("calibratedPitchCountsPerRadian", calibratedPitchCountsPerRadian, 0.0f, 20000.0f, 0.0f);
            ClampFloatSetting("hostMouseDpi", hostMouseDpi, 100.0f, 64000.0f, kDefaultHostMouseDpi);
            const float baseCountsPerRadian = KmboxBaseCountsPerRadian();
            const float yawCountsPerRadian = KmboxYawCountsPerRadian();
            const float pitchCountsPerRadian = KmboxPitchCountsPerRadian();
            Diagnostics::Aim("config.validated kmboxEnabled=%d deviceType=%d ip=%s commandPort=%d monitorPortSetting=%d effectiveMonitorPort=%d manualOverride=%d countsPerRadian=%.6f calibratedCountsPerRadian=%.6f calibratedPitchCountsPerRadian=%.6f baseCountsPerRadian=%.6f gameMouseSensitivity=%.6f effectiveGameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d autoReadGameMouseSensitivity=%d autoDetected=%d hostMouseDpi=%.6f yawCountsPerRadian=%.6f pitchCountsPerRadian=%.6f inputDelayMs=%d suppressWhileMenuOpen=%d aimKey=%d aimKey2=%d trackingSmooth=%.6f flickSmooth=%.6f aimMethod=%d pidDeadzone=%.6f bezierSpeed=%.6f",
                kmboxEnabled ? 1 : 0,
                kmboxDeviceType,
                kmboxIp,
                kmboxPort,
                kmboxMonitorPort,
                EffectiveKmboxMonitorPort(),
                kmboxMonitorPortManualOverride ? 1 : 0,
                kmboxCountsPerRadian,
                calibratedCountsPerRadian,
                calibratedPitchCountsPerRadian,
                baseCountsPerRadian,
                gameMouseSensitivity,
                EffectiveGameMouseSensitivity(),
                referenceGameSensitivity,
                autoScaleByGameSensitivity ? 1 : 0,
                autoReadGameMouseSensitivity ? 1 : 0,
                gameMouseSensitivityAutoDetected ? 1 : 0,
                hostMouseDpi,
                yawCountsPerRadian,
                pitchCountsPerRadian,
                kmboxInputDelayMs,
                kmboxSuppressOutputWhileMenuOpen ? 1 : 0,
                aim_key,
                aim_key2,
                Tracking_smooth,
                Flick_smooth,
                aimMethod,
                aimPidDeadzone,
                aimBezierSpeed);
            if (yawCountsPerRadian <= 1.0f) {
                Diagnostics::Aim("config.warning yawCountsPerRadian=%.6f is very low; small angle deltas may remain sub-count for many frames",
                    yawCountsPerRadian);
            }
            ClampSetting("locx", locx, 0, 100000, 0);
            ClampSetting("locy", locy, 0, 100000, 0);
            ClampSetting("therad", therad, 0, 10000, 0);
            ClampSetting("pon", pon, 0, 10000, 0);
            ClampSetting("crss", crss, 0, 10000, 0);
            ValidateHeroPresetsUnlocked();
            ValidateHeroSkillPresetsUnlocked();

            ClampColor("enargb", enargb);
            ClampColor("invisnenargb", invisnenargb);
            ClampColor("targetargb", targetargb);
            ClampColor("allyargb", allyargb);
            ClampColor("EnemyCol", EnemyCol);
            ClampColor("fovcol", fovcol);
            for (int slotIndex = 0; slotIndex < kMaxHeroPresetSlots; ++slotIndex) {
                aimFovRingStyles[static_cast<size_t>(slotIndex)] = ClampFovRingStyle(
                    aimFovRingStyles[static_cast<size_t>(slotIndex)],
                    FovRingSlotKind::Aim,
                    slotIndex);
            }
        }

        std::string ColorText(const ImVec4& value)
        {
            char buffer[128] = {};
            std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f,%.3f",
                value.x, value.y, value.z, value.w);
            return buffer;
        }

        void DumpUnlocked(Diagnostics::LogLevel level)
        {
            LogConfig(level, "Dump: config_version=%d", config_version);
            LogConfig(level, "Dump: aim modes enableAimbot=%s triggerbot=%s triggerbot2=%s triggerDisableWhileReloading=%s triggerDisableWhileReloading2=%s triggerIgnoreInvisible=%s triggerIgnoreInvisible2=%s triggerBoneMask=0x%03X triggerBoneMask2=0x%03X Tracking=%s Tracking2=%s Flick=%s Flick2=%s",
                ToText(enableAimbot).c_str(), ToText(triggerbot).c_str(), ToText(triggerbot2).c_str(),
                ToText(triggerbotDisableWhileReloading).c_str(), ToText(triggerbotDisableWhileReloading2).c_str(),
                ToText(triggerbotIgnoreInvisible).c_str(), ToText(triggerbotIgnoreInvisible2).c_str(),
                static_cast<unsigned int>(triggerbotBoneMask),
                static_cast<unsigned int>(triggerbotBoneMask2),
                ToText(Tracking).c_str(), ToText(Tracking2).c_str(), ToText(Flick).c_str(),
                ToText(Flick2).c_str());
            LogConfig(level, "Dump: prediction projectile_arc=%s Prediction=%s Gravitypredit=%s predit_level=%.3f hanzoautospeed=%s",
                ToText(projectile_arc).c_str(), ToText(Prediction).c_str(),
                ToText(Gravitypredit).c_str(), predit_level,
                ToText(hanzoautospeed).c_str());
            LogConfig(level, "Dump: keys AimKey=%d aim_key=%d aim_key2=%d togglekey=%d MenuToggleKey=%d",
                AimKey, aim_key, aim_key2, togglekey, MenuToggleKey);
            LogConfig(level, "Dump: aim FovDeg=%.3f Fov2Deg=%.3f minFov1Deg=%.3f minFov2Deg=%.3f Smooth=%.3f autoscalefov=%s hitboxScalePct=%.3f hitbox2ScalePct=%.3f missbox=%.3f",
                Fov, Fov2, minFov1, minFov2, Smooth, ToText(autoscalefov).c_str(),
                hitbox, hitbox2, missbox);
            LogConfig(level, "Dump: smoothing Tracking_smooth=%.3f Tracking_smooth2=%.3f Flick_smooth=%.3f Flick_smooth2=%.3f accvalue=%.3f accvalue2=%.3f bladespeed=%.3f",
                Tracking_smooth, Tracking_smooth2, Flick_smooth, Flick_smooth2, accvalue, accvalue2, bladespeed);
            LogConfig(level, "Dump: bones TargetBone=%d Bone=%d Bone2=%d autobone=%s autobone2=%s aimBoneMask=0x%03X aimBoneMask2=0x%03X switch_team=%s switch_team2=%s BoneName=%s BoneName2=%s",
                TargetBone, Bone, Bone2, ToText(autobone).c_str(), ToText(autobone2).c_str(),
                static_cast<unsigned int>(aimbotBoneMask),
                static_cast<unsigned int>(aimbotBoneMask2),
                ToText(switch_team).c_str(), ToText(switch_team2).c_str(), BoneName.c_str(), BoneName2.c_str());
            LogConfig(level, "Dump: targeting lockontarget=%s trackcompensate=%s comarea=%.3f comspeed=%.3f aiaim=%s targetdelay=%s targetdelaytime=%d hitboxdelayshoot=%s hiboxdelaytime=%d dontshot=%s shotcount=%d shotmanydont=%d",
                ToText(lockontarget).c_str(), ToText(trackcompensate).c_str(), comarea, comspeed, ToText(aiaim).c_str(),
                ToText(targetdelay).c_str(), targetdelaytime, ToText(hitboxdelayshoot).c_str(), hiboxdelaytime,
                ToText(dontshot).c_str(), shotcount, shotmanydont);
            LogConfig(level, "Dump: aimbot ui autoshot=%s keepFiring=%s predictionMode=%d behavior=%d firePolicy=%d triggerDelay=%.3f maxHead=%.3f smoothType=%d stickiness=%.3f smoothY=%.3f pitchScale=%.3f maxAim=%.3f minCharge=%.3f maxCharge=%.3f ignoreInvisible=%s trace=%d unlock=%d lockTime=%.3f maxDist=%.3f minDist=%.3f attack=%d team=%d priority=%d",
                ToText(aimbotAutoshot).c_str(), ToText(aimbotKeepFiring).c_str(), aimbotPredictionMode,
                aimBehavior, aimbotFirePolicy, aimbotTriggerDelay,
                aimbotMaxHead, aimbotSmoothType, aimbotStickiness, aimbotSmoothY, aimbotPitchScale, aimbotMaxAim,
                aimbotMinCharge, aimbotMaxCharge, ToText(aimbotIgnoreInvisible).c_str(), aimbotTrace,
                aimbotUnlock, aimbotLockTime, aimbotMaxDist, aimbotMinDist, aimbotAttack, aimbotTeam,
                aimbotPriority);
            LogConfig(level, "Dump: aim two_stage=%s triggerGate=%s boxPadding=%.3f innerRadius=%.3f innerSmoothScale=%.3f overshoot=%s overshootGain=%.3f overshootResetPx=%.3f",
                ToText(aimbotTwoStage).c_str(), ToText(aimbotTwoStageTriggerGate).c_str(),
                aimbotTwoStageBoxPadding, aimbotTwoStageInnerRadius, aimbotTwoStageInnerSmoothScale,
                ToText(aimOvershootCurve).c_str(), aimOvershootGain, aimOvershootResetPixels);
            LogConfig(level, "Dump: aim method method=%d pidP=%.3f pidI=%.3f pidD=%.3f pidMaxIntegral=%.3f pidDeadzone=%.3f bezierControlPoints=%d bezierCurvature=%.3f bezierSpeed=%.3f piecewiseDeg=(%.3f,%.3f,%.3f) piecewiseScale=(%.3f,%.3f,%.3f) accelLimited=%.3f constantDegPerSec=%.3f speedScale=(%.3f,%.3f,%.3f,%.3f,%.3f,%.3f)",
                aimMethod, aimPidP, aimPidI, aimPidD, aimPidMaxIntegral, aimPidDeadzone,
                aimBezierControlPoints, aimBezierCurvature, aimBezierSpeed,
                AimPiecewiseNearDegrees(), AimPiecewiseMidDegrees(), AimPiecewiseFarDegrees(),
                AimPiecewiseNearScale(), AimPiecewiseMidScale(), AimPiecewiseFarScale(),
                AimMethodAcceleration(4),
                AimConstantAngularSpeedDeg(),
                aimMethodAngularSpeedScale[0], aimMethodAngularSpeedScale[1],
                aimMethodAngularSpeedScale[2], aimMethodAngularSpeedScale[3],
                aimMethodAngularSpeedScale[4], aimMethodAngularSpeedScale[5]);
            LogConfig(level, "Dump: aim behavior moveSplit enabled=(%s,%s,%s,%s) maxPixels=(%d,%d,%d,%d) delayUs=(%d,%d,%d,%d)",
                ToText(AimBehaviorMoveSplitEnabled(0)).c_str(),
                ToText(AimBehaviorMoveSplitEnabled(1)).c_str(),
                ToText(AimBehaviorMoveSplitEnabled(2)).c_str(),
                ToText(AimBehaviorMoveSplitEnabled(3)).c_str(),
                AimBehaviorMoveSplitMaxPixels(0),
                AimBehaviorMoveSplitMaxPixels(1),
                AimBehaviorMoveSplitMaxPixels(2),
                AimBehaviorMoveSplitMaxPixels(3),
                AimBehaviorMoveSplitDelayUs(0),
                AimBehaviorMoveSplitDelayUs(1),
                AimBehaviorMoveSplitDelayUs(2),
                AimBehaviorMoveSplitDelayUs(3));
            const AimStartLimiterProfile trackingStartLimiter =
                ResolveAimStartLimiterProfile(kAimBehaviorTracking);
            const AimBehaviorPreset* trackingBehaviorPreset =
                ActiveAimBehaviorPreset(kAimBehaviorTracking);
            LogConfig(level, "Dump: tracking startLimiter source=%s presetId=%d enabled=%s initialCapDegPerSec=%.3f capRiseDegPerSec2=%.3f restartOnTargetChange=%s",
                trackingBehaviorPreset ? "behavior_preset" : "class_default",
                trackingBehaviorPreset ? trackingBehaviorPreset->id : -1,
                ToText(trackingStartLimiter.enabled).c_str(),
                trackingStartLimiter.initialCapDegPerSec,
                trackingStartLimiter.capRiseDegPerSec2,
                ToText(trackingStartLimiter.restartOnTargetChange).c_str());
            LogConfig(level, "Dump: hero GenjiBlade=%s AutoShiftGenji=%s widowautounscope=%s",
                ToText(GenjiBlade).c_str(), ToText(AutoShiftGenji).c_str(), ToText(widowautounscope).c_str());
            LogConfig(level, "Dump: shoot AutoShoot=%s Shoottime=%d shooted=%s shooted2=%s lasttime=%d lasthealth=%.3f skilled=%s slasttime=%d sskilled=%s reloading=%s",
                ToText(AutoShoot).c_str(), Shoottime, ToText(shooted).c_str(), ToText(shooted2).c_str(), lasttime,
                lasthealth, ToText(skilled).c_str(), slasttime, ToText(sskilled).c_str(), ToText(reloading).c_str());
            LogConfig(level, "Dump: blade Qstarttime=%d Qtime=%d lastenemy=%d doingdelay=%s timebeforedelay=%d",
                Qstarttime, Qtime, lastenemy, ToText(doingdelay).c_str(), timebeforedelay);
            LogConfig(level, "Dump: auto AutoMelee=%s meleehealth=%.3f meleedistance=%.3f AutoRMB=%s AutoRMBhealth=%.3f AutoRMBdistance=%.3f AutoSkill=%s SkillHealth=%.3f AntiAFK=%s",
                ToText(AutoMelee).c_str(), meleehealth, meleedistance, ToText(AutoRMB).c_str(), AutoRMBhealth,
                AutoRMBdistance, ToText(AutoSkill).c_str(), SkillHealth, ToText(AntiAFK).c_str());
            LogConfig(level, "Dump: secondary secondaim=%s highPriority=%s targetPriority=%d",
                ToText(secondaim).c_str(), ToText(highPriority).c_str(), targetPriority);
            LogConfig(level, "Dump: kmbox enabled=%s deviceType=%d ip=%s commandPort=%d monitorPortSetting=%d effectiveMonitorPort=%d manualOverride=%s mac=%s comPort=%s countsPerRadian=%.3f calibratedCountsPerRadian=%.3f calibratedPitchCountsPerRadian=%.3f gameMouseSensitivity=%.3f effectiveGameMouseSensitivity=%.3f referenceGameSensitivity=%.3f autoScaleByGameSensitivity=%s autoReadGameMouseSensitivity=%s autoDetectedGameSensitivity=%s hostMouseDpi=%.3f detectedHostMouseDpi=%.3f hostMouseDpiAutoDetected=%s inputDelayMs=%d debugLog=%s suppressWhileMenuOpen=%s",
                ToText(kmboxEnabled).c_str(), kmboxDeviceType, kmboxIp, kmboxPort, kmboxMonitorPort,
                EffectiveKmboxMonitorPort(), ToText(kmboxMonitorPortManualOverride).c_str(), kmboxMac,
                kmboxComPort, kmboxCountsPerRadian, calibratedCountsPerRadian, calibratedPitchCountsPerRadian,
                gameMouseSensitivity, EffectiveGameMouseSensitivity(), referenceGameSensitivity,
                ToText(autoScaleByGameSensitivity).c_str(),
                ToText(autoReadGameMouseSensitivity).c_str(),
                ToText(gameMouseSensitivityAutoDetected).c_str(),
                hostMouseDpi, detectedHostMouseDpi,
                ToText(hostMouseDpiAutoDetected).c_str(), kmboxInputDelayMs,
                ToText(kmboxDebugLog).c_str(),
                ToText(kmboxSuppressOutputWhileMenuOpen).c_str());
            LogConfig(level, "Dump: manual screen width=%d height=%d",
                manualScreenWidth, manualScreenHeight);
            LogConfig(level, "Dump: visuals draw_info=%s drawbattletag=%s drawhealth=%s healthbar=%s healthbar2=%s healthbartextsize=%.3f dist=%s visualMaxDist=%.3f name=%s ult=%s draw_skel=%s draw_avatar=%s draw_hitbox=%s skillinfo=%s ultimateDisplayMode=%d",
                ToText(draw_info).c_str(), ToText(drawbattletag).c_str(), ToText(drawhealth).c_str(), ToText(healthbar).c_str(),
                ToText(healthbar2).c_str(), healthbartextsize, ToText(dist).c_str(), visualMaxDist, ToText(name).c_str(), ToText(ult).c_str(),
                ToText(draw_skel).c_str(), ToText(draw_avatar).c_str(), ToText(draw_hitbox).c_str(), ToText(skillinfo).c_str(),
                ultimateDisplayMode);
            LogConfig(level, "Dump: overlays radar=%s radarline=%s drawline=%s draw_fov=%s drawTrackingDeadzones=%s draw_hp_pack=%s crosscircle=%s eyeray=%s boxPerfMode=%s boxPerfFastRect=%s",
                ToText(radar).c_str(), ToText(radarline).c_str(),
                ToText(drawline).c_str(), ToText(draw_fov).c_str(), ToText(drawTrackingDeadzones).c_str(), ToText(draw_hp_pack).c_str(),
                ToText(crosscircle).c_str(), ToText(eyeray).c_str(),
                ToText(boxPerfMode).c_str(), ToText(boxPerfFastRect).c_str());
            LogConfig(level, "Dump: colors EnemyCol=%s fovcol=%s enargb=%s invisnenargb=%s targetargb=%s allyargb=%s",
                ColorText(EnemyCol).c_str(), ColorText(fovcol).c_str(),
                ColorText(enargb).c_str(), ColorText(invisnenargb).c_str(), ColorText(targetargb).c_str(),
                ColorText(allyargb).c_str());
            LogConfig(level, "Dump: state Targetenemyi=%d Targetenemyifov=%d health=%.3f doingentity=%d lastheroid=%d Menu=%s nowhero=%s locx=%d locy=%d therad=%d pon=%d crss=%d",
                Targetenemyi, Targetenemyifov, health, doingentity, lastheroid, ToText(Menu).c_str(),
                nowhero.c_str(), locx, locy, therad, pon, crss);
        }

    } // namespace

    void ResetToDefaults()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ResetHeroDefaultsUnlocked();
        LogConfig(Diagnostics::LogLevel::Info, "Current per-hero config reset to defaults.");
    }

    void Validate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ValidateUnlocked();
    }

    void Dump()
    {
        std::lock_guard<std::mutex> lock(mutex);
        DumpUnlocked(Diagnostics::LogLevel::Info);
    }

    HeroPreset MakeHeroPresetFromCurrent()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return MakeHeroPresetFromCurrentUnlocked();
    }

    HeroPreset MakeHeroAimPresetFromCurrent()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return MakeHeroAimPresetFromCurrentUnlocked();
    }

    HeroPreset MakeHeroTriggerPresetFromCurrent()
    {
        std::lock_guard<std::mutex> lock(mutex);
        return MakeHeroTriggerPresetFromCurrentUnlocked();
    }

    static bool TryGetHeroPresetFromStore(HeroPresetSlotKind kind, uint64_t heroId, HeroPreset& outPreset)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return false;

        for (const HeroSlotPreset& slot : item->second) {
            if (slot.present && slot.enabled) {
                outPreset = ValidateHeroPresetValueForHero(heroId, slot.preset);
                return true;
            }
        }
        return false;
    }

    bool TryGetHeroPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Aim, heroId, outPreset);
    }

    bool TryGetHeroAimPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Aim, heroId, outPreset);
    }

    bool TryGetHeroTriggerPreset(uint64_t heroId, HeroPreset& outPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return TryGetHeroPresetFromStore(HeroPresetSlotKind::Trigger, heroId, outPreset);
    }

    static bool HasHeroPresetInStore(HeroPresetSlotKind kind, uint64_t heroId)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        return item != store.end() && CountHeroPresetSlots(item->second) > 0;
    }

    bool HasHeroPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return HasHeroPresetInStore(HeroPresetSlotKind::Aim, heroId) ||
            HasHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    bool HasHeroAimPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return HasHeroPresetInStore(HeroPresetSlotKind::Aim, heroId);
    }

    bool HasHeroTriggerPreset(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return HasHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    HeroPreset GetHeroPresetOrDefault(uint64_t heroId)
    {
        return GetHeroPresetOrDefault(heroId, 0);
    }

    static HeroPreset GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        if (item != store.end() &&
            item->second[static_cast<size_t>(clampedSlotIndex)].present)
            return ValidateHeroPresetValueForHero(heroId, item->second[static_cast<size_t>(clampedSlotIndex)].preset);
        return kind == HeroPresetSlotKind::Aim
            ? MakeHeroAimPresetFromCurrentUnlocked()
            : MakeHeroTriggerPresetFromCurrentUnlocked();
    }

    HeroPreset GetHeroPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    HeroPreset GetHeroAimPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    HeroPreset GetHeroTriggerPresetOrDefault(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroPresetOrDefaultFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    void SetHeroPreset(uint64_t heroId, const HeroPreset& preset)
    {
        SetHeroPreset(heroId, 0, preset);
    }

    static std::array<HeroSlotPreset, kHeroPresetSlotCount>& EnsureHeroPresetSlotsInStore(
        HeroPresetSlotKind kind,
        uint64_t heroId)
    {
        auto& store = PresetStore(kind);
        auto [item, inserted] = store.try_emplace(heroId);
        if (inserted)
            InitializeBasicHeroPresetSlots(item->second, heroId, kind);
        else
            NormalizeHeroPresetSlots(item->second, heroId, kind);
        return item->second;
    }

    static void EnsureHeroPresetSlotRange(std::array<HeroSlotPreset, kHeroPresetSlotCount>& slots,
                                          int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const int presentCount = CountHeroPresetSlots(slots);
        for (int index = presentCount; index <= clampedSlotIndex; ++index) {
            HeroSlotPreset& slot = slots[static_cast<size_t>(index)];
            ResetHeroPresetSlot(slot, index);
            slot.present = true;
            slot.enabled = true;
        }
    }

    static void SetHeroPresetInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        if (heroId == 0)
            return;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);
        slots[static_cast<size_t>(clampedSlotIndex)].preset = ValidateHeroPresetValueForHero(heroId, preset);
        NormalizeHeroPresetSlots(slots, heroId, kind);
    }

    void SetHeroPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroPresetInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, preset);
    }

    void SetHeroAimPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroPresetInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, preset);
    }

    void SetHeroTriggerPreset(uint64_t heroId, int slotIndex, const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroPresetInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, preset);
    }

    void SetHeroAimSlotScopeRequirement(uint64_t heroId,
                                        int slotIndex,
                                        AimScopeRequirement requirement)
    {
        if (heroId == 0)
            return;

        std::lock_guard<std::mutex> lock(mutex);
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(HeroPresetSlotKind::Aim, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);

        HeroSlotPreset& slot = slots[static_cast<size_t>(clampedSlotIndex)];
        requirement = NormalizeAimScopeRequirement(static_cast<int>(requirement));
        if (HeroUsesScopedWeaponActionSplit(heroId)) {
            slot.preset.trigger.action = requirement == AimScopeRequirement::ScopedOnly
                ? 2
                : 0;
        }
        slot.scopeRequirement = requirement;
        NormalizeHeroPresetSlots(slots, heroId, HeroPresetSlotKind::Aim);
    }

    static std::string GetHeroSlotNameFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end() ||
            !item->second[static_cast<size_t>(clampedSlotIndex)].present)
            return DefaultHeroSlotName(clampedSlotIndex);

        return NormalizeHeroSlotName(item->second[static_cast<size_t>(clampedSlotIndex)].name, clampedSlotIndex);
    }

    static bool TryGetHeroSlotFromStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        if (heroId == 0)
            return false;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return false;

        const HeroSlotPreset& slot = item->second[static_cast<size_t>(clampedSlotIndex)];
        if (!slot.present)
            return false;

        outSlot = slot;
        outSlot.name = NormalizeHeroSlotName(outSlot.name, clampedSlotIndex);
        outSlot.preset = ValidateHeroPresetValueForHero(heroId, outSlot.preset);
        outSlot.scopeRequirement = kind == HeroPresetSlotKind::Aim
            ? DefaultAimScopeRequirementForHeroAction(
                heroId,
                outSlot.preset.trigger.action)
            : AimScopeRequirement::All;
        outSlot.present = true;
        return true;
    }

    std::string GetHeroSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    std::string GetHeroAimSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    std::string GetHeroTriggerSlotName(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroSlotNameFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    bool TryGetHeroAimSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return TryGetHeroSlotFromStore(HeroPresetSlotKind::Aim, heroId, slotIndex, outSlot);
    }

    bool TryGetHeroTriggerSlot(uint64_t heroId, int slotIndex, HeroSlotPreset& outSlot)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return TryGetHeroSlotFromStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, outSlot);
    }

    static bool IsHeroSlotEnabledInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        const auto& store = PresetStoreConst(kind);
        const auto item = store.find(heroId);
        if (item == store.end())
            return clampedSlotIndex == 0;

        const HeroSlotPreset& slot = item->second[static_cast<size_t>(clampedSlotIndex)];
        return slot.present && slot.enabled;
    }

    bool IsHeroSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool IsHeroAimSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool IsHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return IsHeroSlotEnabledInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    static void SetHeroSlotEnabledInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex, bool enabled)
    {
        if (heroId == 0)
            return;

        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        EnsureHeroPresetSlotRange(slots, clampedSlotIndex);
        slots[static_cast<size_t>(clampedSlotIndex)].enabled = enabled;
        NormalizeHeroPresetSlots(slots, heroId, kind);
    }

    void SetHeroSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, enabled);
    }

    void SetHeroAimSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Aim, heroId, slotIndex, enabled);
    }

    void SetHeroTriggerSlotEnabled(uint64_t heroId, int slotIndex, bool enabled)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SetHeroSlotEnabledInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex, enabled);
    }

    static int GetHeroSlotCountInStore(HeroPresetSlotKind kind, uint64_t heroId)
    {
        if (heroId == 0)
            return 1;

        auto& store = PresetStore(kind);
        auto item = store.find(heroId);
        if (item == store.end())
            return 1;

        NormalizeHeroPresetSlots(item->second, heroId, kind);
        return (std::max)(1, CountHeroPresetSlots(item->second));
    }

    int GetHeroAimSlotCount(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroSlotCountInStore(HeroPresetSlotKind::Aim, heroId);
    }

    int GetHeroTriggerSlotCount(uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return GetHeroSlotCountInStore(HeroPresetSlotKind::Trigger, heroId);
    }

    static int AddHeroSlotInStore(HeroPresetSlotKind kind, uint64_t heroId, const HeroPreset& seedPreset)
    {
        if (heroId == 0)
            return -1;

        auto& slots = EnsureHeroPresetSlotsInStore(kind, heroId);
        const int presentCount = CountHeroPresetSlots(slots);
        if (presentCount >= kHeroPresetSlotCount)
            return -1;

        HeroSlotPreset& slot = slots[static_cast<size_t>(presentCount)];
        ResetHeroPresetSlot(slot, presentCount);
        slot.present = true;
        slot.enabled = true;
        slot.name = DefaultHeroSlotName(presentCount);
        slot.preset = ValidateHeroPresetValueForHero(heroId, seedPreset);
        NormalizeHeroPresetSlots(slots, heroId, kind);
        return presentCount;
    }

    int AddHeroAimSlot(uint64_t heroId, const HeroPreset& seedPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return AddHeroSlotInStore(HeroPresetSlotKind::Aim, heroId, seedPreset);
    }

    int AddHeroTriggerSlot(uint64_t heroId, const HeroPreset& seedPreset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return AddHeroSlotInStore(HeroPresetSlotKind::Trigger, heroId, seedPreset);
    }

    static bool DeleteHeroSlotInStore(HeroPresetSlotKind kind, uint64_t heroId, int slotIndex)
    {
        if (heroId == 0)
            return false;

        auto& store = PresetStore(kind);
        auto item = store.find(heroId);
        if (item == store.end())
            return false;

        NormalizeHeroPresetSlots(item->second, heroId, kind);
        const int presentCount = CountHeroPresetSlots(item->second);
        const int clampedSlotIndex = ClampHeroPresetSlotIndex(slotIndex);
        if (presentCount <= 1 || clampedSlotIndex >= presentCount)
            return false;

        for (int index = clampedSlotIndex; index < presentCount - 1; ++index) {
            HeroSlotPreset slot = item->second[static_cast<size_t>(index + 1)];
            slot.name = NormalizeHeroSlotName(slot.name, index);
            item->second[static_cast<size_t>(index)] = slot;
        }

        ResetHeroPresetSlot(item->second[static_cast<size_t>(presentCount - 1)], presentCount - 1);
        NormalizeHeroPresetSlots(item->second, heroId, kind);
        return true;
    }

    bool DeleteHeroAimSlot(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return DeleteHeroSlotInStore(HeroPresetSlotKind::Aim, heroId, slotIndex);
    }

    bool DeleteHeroTriggerSlot(uint64_t heroId, int slotIndex)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return DeleteHeroSlotInStore(HeroPresetSlotKind::Trigger, heroId, slotIndex);
    }

    void NormalizeHeroPresets()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ValidateHeroPresetsUnlocked();
        ValidateHeroSkillPresetsUnlocked();
    }

    void ApplyHeroPresetToGlobals(const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ApplyHeroPresetUnlocked(preset);
    }

    void ApplyHeroAimPresetToGlobals(const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ApplyHeroAimPresetUnlocked(preset);
    }

    void ApplyHeroTriggerPresetToGlobals(const HeroPreset& preset)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ApplyHeroTriggerPresetUnlocked(preset);
    }

    namespace {

        std::string ResolveHeroConfigPath(const std::string& path)
        {
            std::string lowered = path;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return lowered.size() >= 5 &&
                lowered.substr(lowered.size() - 5) == ".json"
                ? path
                : HeroConfigPath(path);
        }

        void SaveSystemConfigUnlocked(const std::string& path)
        {
            WriteIntValue(path, kMetaSection, kVersionKey, kCurrentConfigVersion);
            SaveAimbotSettingsUnlocked(path);
            SaveAimMethodSettingsUnlocked(path);
            SaveKmboxSettingsUnlocked(path);

            WriteBoolValue(path, "Global", "draw_hp_pack", draw_hp_pack);
            WriteBoolValue(path, "Global", "crosscircle", crosscircle);
            WriteBoolValue(path, "Global", "eyeray", eyeray);
            WriteBoolValue(path, "Global", "draw_info", draw_info);
            WriteBoolValue(path, "Global", "drawbattletag", drawbattletag);
            WriteBoolValue(path, "Global", "drawhealth", drawhealth);
            WriteBoolValue(path, "Global", "healthbar", healthbar);
            WriteBoolValue(path, "Global", "healthbar2", healthbar2);
            WriteFixedFloatValue(path, "Global", "healthbartextsize", healthbartextsize);
            WriteBoolValue(path, "Global", "dist", dist);
            WriteFixedFloatValue(path, "Global", "visualMaxDist", visualMaxDist);
            WriteBoolValue(path, "Global", "name", name);
            WriteBoolValue(path, "Global", "ult", ult);
            WriteBoolValue(path, "Global", "draw_skel", draw_skel);
            WriteBoolValue(path, "Global", "draw_avatar", draw_avatar);
            WriteBoolValue(path, "Global", "draw_hitbox", draw_hitbox);
            WriteBoolValue(path, "Global", "skillinfo", skillinfo);
            WriteIntValue(path, "Global", "ultimateDisplayMode", 1);
            WriteIntValue(path, "Global", "skillDisplayMode", skillDisplayMode);
            WriteIntValue(path, "Global", "radarCorner", radarCorner);
            WriteBoolValue(path, "Global", "radar", radar);
            WriteBoolValue(path, "Global", "radarline", radarline);
            WriteBoolValue(path, "Global", "drawline", drawline);
            WriteBoolValue(path, "Global", "draw_fov", draw_fov);
            WriteBoolValue(path, "Global", "boxPerfMode", boxPerfMode);
            WriteBoolValue(path, "Global", "boxPerfFastRect", boxPerfFastRect);
            WriteBoolValue(path, "Global", "drawTrackingDeadzones", drawTrackingDeadzones);
            WriteIntValue(path, "Global", "targetPriority", targetPriority);
            WriteIntValue(path, "Global", "MenuToggleKey", MenuToggleKey);
            WriteStringValue(path, "Global", "lastConfigProfile", lastConfigProfile.c_str());
            WriteIntValue(path, "Global", "uiLanguage", ClampUiLanguage(uiLanguage));
            WriteIntValue(path, "Global", "manualScreenWidth", manualScreenWidth);
            WriteIntValue(path, "Global", "manualScreenHeight", manualScreenHeight);
            WriteColor(path, "Global", "EnemyCol", EnemyCol);
            WriteColor(path, "Global", "fovcol", fovcol);
            WriteColor(path, "Global", "invisenargb", invisnenargb);
            WriteColor(path, "Global", "enargb", enargb);
            WriteColor(path, "Global", "targetargb", targetargb);
            WriteColor(path, "Global", "allyargb", allyargb);
            SaveFovRingStyles(path);

            DeleteValue(path, "Aimbot", "inputSource");
            DeleteValue(path, "Global", "gafAsyncKeyStateOffset");
            DeleteValue(path, "Global", "gafAsyncKeyStateSize");
            DeleteValue(path, "Global", "gafAsyncKeyStateSessionId");
        }

        void LoadSystemConfigUnlocked(const IniFile& ini)
        {
            LoadAimbotSettingsUnlocked(ini);
            LoadAimMethodSettingsUnlocked(ini);
            LoadGlobalSettingsUnlocked(ini);
            if (!KeyExists(ini, kAimbotSection, "aimbotBoneMask"))
                aimbotBoneMask = AimBoneMaskFromLegacy(Bone, autobone);
            if (!KeyExists(ini, kAimbotSection, "aimbotBoneMask2"))
                aimbotBoneMask2 = AimBoneMaskFromLegacy(Bone2, autobone2);
            LoadKmboxSettingsUnlocked(ini);
        }

        void ApplySystemConfigMigrationDefaultsUnlocked(int fileVersion)
        {
            if (fileVersion < kVisualOverlayCleanupVersion)
                draw_skel = false;
        }

        void ApplyLoadedHeroPresetsUnlocked(uint64_t heroId)
        {
            if (heroId == 0)
                return;

            HeroPreset preset{};
            if (TryGetHeroPresetFromStore(HeroPresetSlotKind::Aim, heroId, preset))
                ApplyHeroAimPresetUnlocked(preset);
            if (TryGetHeroPresetFromStore(HeroPresetSlotKind::Trigger, heroId, preset))
                ApplyHeroTriggerPresetUnlocked(preset);
        }

    } // anonymous namespace

    void SaveHeroConfig(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SaveHeroPresetsUnlocked(ResolveHeroConfigPath(path));
    }

    void SaveHeroConfigForHero(const std::string& path, uint64_t heroId)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SaveHeroConfigForHeroUnlocked(ResolveHeroConfigPath(path), heroId);
    }

    void LoadHeroConfig(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!LoadHeroConfigOrMigrateUnlocked(ResolveHeroConfigPath(path), nullptr)) {
            LogConfig(Diagnostics::LogLevel::Warn,
                "%s is missing or unreadable; no hero presets were loaded.",
                path.c_str());
        }
    }

    void LoadHeroSkillConfig(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        heroSkillPresets.clear();
        if (!LoadHeroSkillConfigUnlocked(ResolveHeroConfigPath(path))) {
            LogConfig(Diagnostics::LogLevel::Warn,
                "%s is missing or unreadable; no hero skill presets were loaded.",
                path.c_str());
        }
    }

    void SaveHeroSkillConfig(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        SaveHeroSkillConfigUnlocked(ResolveHeroConfigPath(path));
    }

    bool TryGetHeroSkillSettings(uint64_t heroId, const std::string& skillId, HeroSkillSettings& outSettings)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto hero = heroSkillPresets.find(heroId);
        if (hero == heroSkillPresets.end())
            return false;

        const auto skill = hero->second.find(skillId);
        if (skill == hero->second.end())
            return false;

        outSettings = ValidateHeroSkillSettingsValue(skill->second);
        return true;
    }

    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId, const std::string& skillId)
    {
        return GetHeroSkillSettings(heroId, skillId, HeroSkillSettings{});
    }

    HeroSkillSettings GetHeroSkillSettings(uint64_t heroId,
                                           const std::string& skillId,
                                           const HeroSkillSettings& defaultSettings)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto hero = heroSkillPresets.find(heroId);
        if (hero == heroSkillPresets.end())
            return ValidateHeroSkillSettingsValue(defaultSettings);

        const auto skill = hero->second.find(skillId);
        if (skill == hero->second.end())
            return ValidateHeroSkillSettingsValue(defaultSettings);

        return ValidateHeroSkillSettingsValue(skill->second);
    }

    void SetHeroSkillSettings(uint64_t heroId, const std::string& skillId, const HeroSkillSettings& settings)
    {
        if (heroId == 0 || skillId.empty())
            return;

        std::lock_guard<std::mutex> lock(mutex);
        heroSkillPresets[heroId][skillId] = ValidateHeroSkillSettingsValue(settings);
    }

    void SaveHeroPresets(const std::string& path)
    {
        SaveHeroConfig(path);
    }

    void LoadHeroPresets(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);

        IniFile ini;
        const bool hasIni = LoadIniFile(path, ini);
        if (!LoadHeroConfigOrMigrateUnlocked(ResolveHeroConfigPath(path), hasIni ? &ini : nullptr)) {
            LogConfig(Diagnostics::LogLevel::Warn,
                "%s is missing or unreadable; no hero presets were loaded.",
                path.c_str());
        }
    }

    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ValidateUnlocked();
        if (heroId == 0) {
            SaveSystemConfigUnlocked(path);
            LogConfig(Diagnostics::LogLevel::Info,
                "Saved system config to %s without a current hero.", path.c_str());
            return;
        }

        const std::string heroName = HeroSectionName(heroId, linkBase);
        SaveHeroConfigForHeroUnlocked(HeroConfigPath(path), heroId);

        LogConfig(Diagnostics::LogLevel::Info,
            "Saved hero config for %s to %s.", heroName.c_str(), HeroConfigPath(path).c_str());
    }

    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase)
    {
        std::lock_guard<std::mutex> lock(mutex);

        const std::string heroName = HeroSectionName(heroId, linkBase);
        ResetHeroDefaultsUnlocked();
        ResetAimbotDefaultsUnlocked();
        ResetAimMethodDefaultsUnlocked();
        ResetGlobalDefaultsUnlocked();

        IniFile ini;
        if (!LoadIniFile(path, ini)) {
            config_version = kCurrentConfigVersion;
            LoadHeroConfigOrMigrateUnlocked(HeroConfigPath(path), nullptr);
            LogConfig(Diagnostics::LogLevel::Warn,
                "%s is missing or unreadable; using all documented defaults.",
                path.c_str());
            ApplyLoadedHeroPresetsUnlocked(heroId);
            ValidateUnlocked();
            DumpUnlocked(Diagnostics::LogLevel::Debug);
            return;
        }

        const int fileVersion = ReadInt(ini, kMetaSection, kVersionKey, 0);
        if (fileVersion > kCurrentConfigVersion) {
            config_version = kCurrentConfigVersion;
            LogConfig(Diagnostics::LogLevel::Warn,
                "Config version %d is newer than supported version %d; resetting to defaults.",
                fileVersion, kCurrentConfigVersion);
            WriteIntValue(path, kMetaSection, kVersionKey, kCurrentConfigVersion);
            LoadHeroConfigOrMigrateUnlocked(HeroConfigPath(path), nullptr);
            ApplyLoadedHeroPresetsUnlocked(heroId);
            ValidateUnlocked();
            DumpUnlocked(Diagnostics::LogLevel::Debug);
            return;
        }

        const bool needsMigration = fileVersion < kCurrentConfigVersion;
        if (needsMigration) {
            LogConfig(Diagnostics::LogLevel::Warn,
                "Migrating config version %d to %d.",
                fileVersion, kCurrentConfigVersion);
        }
        config_version = kCurrentConfigVersion;

        LoadSystemConfigUnlocked(ini);
        if (needsMigration)
            ApplySystemConfigMigrationDefaultsUnlocked(fileVersion);
        LoadHeroConfigOrMigrateUnlocked(HeroConfigPath(path), &ini);
        ApplyLoadedHeroPresetsUnlocked(heroId);
        ValidateUnlocked();
        if (needsMigration)
            SaveSystemConfigUnlocked(path);
        DumpUnlocked(Diagnostics::LogLevel::Debug);

        LogConfig(Diagnostics::LogLevel::Info,
            "Loaded config for hero %s from %s.", heroName.c_str(), path.c_str());
    }

    void SaveConfig(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ValidateUnlocked();
        SaveSystemConfigUnlocked(path);
        LogConfig(Diagnostics::LogLevel::Info,
            "Saved system config to %s.", path.c_str());
    }

    void NormalizeKmboxPorts()
    {
        std::lock_guard<std::mutex> lock(mutex);
        ClampSetting("kmboxPort", kmboxPort, 1, 65535, kDefaultKmboxPort);
        NormalizeKmboxPortsUnlocked("runtime");
    }

    void LoadConfig(const std::string& path)
    {
        const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
        LoadConfigForHero(path, localSnapshot.HeroID, localSnapshot.LinkBase);
    }

}} // namespace OW::Config
