#include "Utils/Config.hpp"

#include "Game/Target.hpp"
#include "Utils/Diagnostics.hpp"

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

    int config_version = 2;

    namespace {

        constexpr int kCurrentConfigVersion = 2;
        constexpr const char* kMetaSection = "Meta";
        constexpr const char* kVersionKey = "config_version";

        using SectionValues = std::unordered_map<std::string, std::string>;

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

            if (parsed < std::numeric_limits<int>::min() || parsed > std::numeric_limits<int>::max())
                return false;

            out = static_cast<int>(parsed);
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

        std::string ToText(float value)
        {
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%.4f", value);
            return buffer;
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

        float ReadFov2Compat(const IniFile& ini, const char* section, const char* key, float def)
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

            int intValue = 0;
            if (!ParseStrictInt(raw, intValue)) {
                LogInvalid(section, key, raw, ToText(def));
                return def;
            }

            const float value = std::abs(intValue) > 10000
                ? static_cast<float>(intValue) / 10000.0f
                : static_cast<float>(intValue);
            LogLoaded(section, key, ToText(value));
            return value;
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

        void WriteIntValue(const std::string& path, const char* section, const char* key, int value)
        {
            char buffer[64] = {};
            std::snprintf(buffer, sizeof(buffer), "%d", value);
            WriteValue(path, section, key, buffer);
        }

        void WriteBoolValue(const std::string& path, const char* section, const char* key, bool value)
        {
            WriteIntValue(path, section, key, value ? 1 : 0);
        }

        void WriteFixedFloatValue(const std::string& path, const char* section, const char* key, float value)
        {
            const int fixedValue = static_cast<int>(std::lround(value * 10000.0f));
            WriteIntValue(path, section, key, fixedValue);
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
            hanzo_flick = false;          // default: false
            silent = false;               // default: false
            Rage = false;                 // default: false
            fakesilent = false;           // default: false

            Prediction = false;           // default: false
            Prediction2 = false;          // default: false
            Gravitypredit = false;        // default: false
            Gravitypredit2 = false;       // default: false
            predit_level = 110.0f;        // default: 110
            predit_level2 = 110.0f;       // default: 110
            hanzoautospeed = false;       // default: false

            AimKey = 0x01;                // default: VK_LBUTTON
            aim_key = 6;                  // default: key index 6
            aim_key2 = 6;                 // default: key index 6
            togglekey = 0;                // default: disabled

            Fov = 200.0f;                 // default: 200
            Fov2 = 200.0f;                // default: 200
            minFov1 = 200.0f;             // default: 200
            minFov2 = 200.0f;             // default: 200
            Smooth = 5.0f;                // default: 5
            fov360 = false;               // default: false
            autoscalefov = false;         // default: false
            hitbox = 0.13f;               // default: 0.13
            hitbox2 = 0.13f;              // default: 0.13
            missbox = 0.6f;               // default: 0.6
            Tracking_smooth = 0.1f;       // default: 0.1
            Tracking_smooth2 = 0.1f;      // default: 0.1
            Flick_smooth = 0.1f;          // default: 0.1
            Flick_smooth2 = 0.1f;         // default: 0.1
            accvalue = 0.1f;              // default: 0.1
            accvalue2 = 0.1f;             // default: 0.1
            bladespeed = 0.1f;            // default: 0.1

            TargetBone = 0;               // default: head
            Bone = 1;                     // default: 1
            Bone2 = 1;                    // default: 1
            autobone = false;             // default: false
            autobone2 = false;            // default: false
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

            norecoil = false;             // default: false
            recoilnum = 0.5f;             // default: 0.5
            horizonreco = false;          // default: false

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

            enablechangefov = false;      // default: false
            CHANGEFOV = 103.0f;           // default: 103
            trackback = false;            // default: false

            secondaim = false;            // default: false
            highPriority = false;         // default: false

            Targetenemyi = -1;            // default: -1
            Targetenemyifov = -1;         // default: -1
            health = 0.0f;                // default: 0
        }

        void ResetGlobalDefaultsUnlocked()
        {
            // Defaults match Config.hpp inline initializers for global visual/UI settings.
            draw_info = true;             // default: true
            drawbattletag = false;        // default: false
            drawhealth = true;            // default: true
            healthbar = true;             // default: true
            healthbar2 = false;           // default: false
            healthbartextsize = 16.0f;    // default: 16
            dist = true;                  // default: true
            name = true;                  // default: true
            ult = true;                   // default: true
            draw_skel = true;             // default: true
            skillinfo = false;            // default: false
            outline = false;              // default: false
            externaloutline = false;      // default: false
            teamoutline = false;          // default: false
            healthoutline = false;        // default: false
            rainbowoutline = false;       // default: false
            draw_edge = false;            // default: false
            drawbox3d = false;            // default: false
            radar = false;                // default: false
            radarline = false;            // default: false
            drawline = false;             // default: false
            draw_fov = false;             // default: false
            draw_hp_pack = false;         // default: false
            crosscircle = false;          // default: false
            eyeray = false;               // default: false
            testvalue = false;            // default: false
            MenuToggleKey = VK_HOME;      // default: VK_HOME

            enargb = ImVec4(1.0f, 0.0f, 0.0f, 0.4f);       // default: 1,0,0,0.4
            invisnenargb = ImVec4(1.0f, 0.0f, 0.0f, 0.4f); // default: 1,0,0,0.4
            targetargb = ImVec4(0.0f, 1.0f, 0.0f, 0.8f);   // default: 0,1,0,0.8
            targetargb2 = ImVec4(0.0f, 1.0f, 0.0f, 0.8f);  // default: 0,1,0,0.8
            allyargb = ImVec4(0.0f, 0.0f, 1.0f, 0.4f);     // default: 0,0,1,0.4
            EnemyCol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);     // default: 1,1,1,1
            fovcol = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);       // default: 1,1,1,1
            fovcol2 = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);      // default: 1,1,1,1

            visenemy = 0;                 // default: 0
            invisenemy = 0;               // default: 0
            targetenemy = 0;              // default: 0
            targetenemy2 = 0;             // default: 0
            Allycolor = 0;                // default: 0
            cps1 = 0;                     // default: 0
            cps2 = 0;                     // default: 0
            cps3 = 0;                     // default: 0
            rainbowargb = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);  // default: 0,0,0,1

            namespoofer = false;          // default: false
            fakename[0] = '\0';           // default: empty string
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
            hanzo_flick = false;
            silent = false;
            triggerbot = false;

            switch (mode) {
            case 0: Tracking = true; break;
            case 1: Flick = true; break;
            case 2: hanzo_flick = true; break;
            case 3: silent = true; break;
            case 4: triggerbot = true; break;
            default: break;
            }
        }

        int CurrentAimMode()
        {
            if (Tracking) return 0;
            if (Flick) return 1;
            if (hanzo_flick) return 2;
            if (silent) return 3;
            if (triggerbot) return 4;
            return -1;
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

        void LoadHeroSettingsUnlocked(const IniFile& ini, const char* section, uint64_t heroId)
        {
            highPriority = ReadBool(ini, section, "highPriority", highPriority);
            aiaim = ReadBool(ini, section, "aiaim", aiaim);
            hanzoautospeed = ReadBool(ini, section, "hanzoautospeed", hanzoautospeed);
            autoscalefov = ReadBool(ini, section, "autoscalefov", autoscalefov);
            lockontarget = ReadBool(ini, section, "lockontarget", lockontarget);
            trackcompensate = ReadBool(ini, section, "trackc", trackcompensate);
            comarea = ReadFixedFloat(ini, section, "comarea", comarea);
            comspeed = ReadFixedFloat(ini, section, "comspeed", comspeed);
            Fov = static_cast<float>(ReadInt(ini, section, "FOV", static_cast<int>(Fov)));
            minFov1 = Fov;
            hitbox = ReadFixedFloat(ini, section, "hitbox", hitbox);
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
            recoilnum = ReadFixedFloat(ini, section, "recoilnum", recoilnum);
            accvalue = ReadFixedFloat(ini, section, "accvalue", accvalue);
            norecoil = ReadBool(ini, section, "norecoil", norecoil);
            horizonreco = ReadBool(ini, section, "horizonreco", horizonreco);
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
            triggerbot2 = ReadBool(ini, section, "triggerbot2", triggerbot2);
            Tracking2 = ReadBool(ini, section, "Tracking2", Tracking2);
            Flick2 = ReadBool(ini, section, "Flick2", Flick2);
            Prediction2 = ReadBool(ini, section, "Prediction2", Prediction2);
            Gravitypredit2 = ReadBool(ini, section, "Gravitypredit2", Gravitypredit2);
            aim_key2 = ReadInt(ini, section, "aim_key2", aim_key2);
            togglekey = ReadInt(ini, section, "togglekey", togglekey);
            predit_level2 = ReadFixedFloat(ini, section, "predit_level2", predit_level2);
            Tracking_smooth2 = ReadFixedFloat(ini, section, "Tracking_smooth2", Tracking_smooth2);
            Flick_smooth2 = ReadFixedFloat(ini, section, "Flick_smooth2", Flick_smooth2);
            accvalue2 = ReadFixedFloat(ini, section, "accvalue2", accvalue2);
            hitbox2 = ReadFixedFloat(ini, section, "hitbox2", hitbox2);
            Fov2 = ReadFov2Compat(ini, section, "Fov2", Fov2);
            minFov2 = Fov2;
            enablechangefov = ReadBool(ini, section, "enablechangefov", enablechangefov);
            CHANGEFOV = ReadFixedFloat(ini, section, "CHANGEFOV", CHANGEFOV);

            ApplyAimMode(ReadInt(ini, section, "Aim Mode", CurrentAimMode()));
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

        void LoadGlobalSettingsUnlocked(const IniFile& ini)
        {
            constexpr const char* section = "Global";

            draw_hp_pack = ReadBool(ini, section, "draw_hp_pack", draw_hp_pack);
            crosscircle = ReadBool(ini, section, "crosscircle", crosscircle);
            eyeray = ReadBool(ini, section, "eyeray", eyeray);
            trackback = ReadBool(ini, section, "trackback", trackback);
            draw_info = ReadBool(ini, section, "draw_info", draw_info);
            drawbattletag = ReadBool(ini, section, "drawbattletag", drawbattletag);
            drawhealth = ReadBool(ini, section, "drawhealth", drawhealth);
            healthbar = ReadBool(ini, section, "healthbar", healthbar);
            healthbar2 = ReadBool(ini, section, "healthbar2", healthbar2);
            healthbartextsize = ReadFixedFloat(ini, section, "healthbartextsize", healthbartextsize);
            dist = ReadBool(ini, section, "dist", dist);
            name = ReadBool(ini, section, "name", name);
            ult = ReadBool(ini, section, "ult", ult);
            draw_skel = ReadBool(ini, section, "draw_skel", draw_skel);
            skillinfo = ReadBool(ini, section, "skillinfo", skillinfo);
            outline = ReadBool(ini, section, "outline", outline);
            externaloutline = ReadBool(ini, section, "externaloutline", externaloutline);
            teamoutline = ReadBool(ini, section, "teamoutline", teamoutline);
            healthoutline = ReadBool(ini, section, "healthoutline", healthoutline);
            rainbowoutline = ReadBool(ini, section, "rainbowoutline", rainbowoutline);
            draw_edge = ReadBool(ini, section, "draw_edge", draw_edge);
            drawbox3d = ReadBool(ini, section, "drawbox3d", drawbox3d);
            radar = ReadBool(ini, section, "radar", radar);
            radarline = ReadBool(ini, section, "radarline", radarline);
            drawline = ReadBool(ini, section, "drawline", drawline);
            draw_fov = ReadBool(ini, section, "draw_fov", draw_fov);
            MenuToggleKey = ReadInt(ini, section, "MenuToggleKey", MenuToggleKey);

            LoadColor(ini, section, "EnemyCol", EnemyCol);
            LoadColor(ini, section, "fovcol", fovcol);
            LoadColor(ini, section, "fovcol2", fovcol2);
            LoadColor(ini, section, "invisenargb", invisnenargb);
            LoadColor(ini, section, "enargb", enargb);
            LoadColor(ini, section, "targetargb", targetargb);
            LoadColor(ini, section, "targetargb2", targetargb2);
            LoadColor(ini, section, "allyargb", allyargb);
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
            }
            (void)fallback;
        }

        void ClampFloatSetting(const char* name, float& value, float minValue, float maxValue, float fallback)
        {
            if (!std::isfinite(value)) {
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s is not finite; using default %s.",
                    name, ToText(fallback).c_str());
                value = fallback;
            }
            if (value < minValue || value > maxValue) {
                const float oldValue = value;
                value = std::clamp(value, minValue, maxValue);
                LogConfig(Diagnostics::LogLevel::Warn,
                    "%s out of range (%s); clamped to %s.",
                    name, ToText(oldValue).c_str(), ToText(value).c_str());
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
            ClampSetting("aim_key", aim_key, 0, 54, 6);
            ClampSetting("aim_key2", aim_key2, 0, 54, 6);
            ClampSetting("togglekey", togglekey, 0, 54, 0);
            ClampSetting("MenuToggleKey", MenuToggleKey, 1, 255, VK_HOME);

            ClampFloatSetting("Fov", Fov, 0.0f, 500.0f, 200.0f);
            ClampFloatSetting("Fov2", Fov2, 0.0f, 500.0f, 200.0f);
            ClampFloatSetting("minFov1", minFov1, 0.0f, 500.0f, 200.0f);
            ClampFloatSetting("minFov2", minFov2, 0.0f, 500.0f, 200.0f);
            ClampFloatSetting("Smooth", Smooth, 0.0f, 100.0f, 5.0f);
            ClampFloatSetting("hitbox", hitbox, 0.0f, 5.0f, 0.13f);
            ClampFloatSetting("hitbox2", hitbox2, 0.0f, 5.0f, 0.13f);
            ClampFloatSetting("missbox", missbox, 0.0f, 5.0f, 0.6f);
            ClampFloatSetting("Tracking_smooth", Tracking_smooth, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Tracking_smooth2", Tracking_smooth2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Flick_smooth", Flick_smooth, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("Flick_smooth2", Flick_smooth2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("accvalue", accvalue, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("accvalue2", accvalue2, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("bladespeed", bladespeed, 0.0f, 20.0f, 0.1f);
            ClampFloatSetting("predit_level", predit_level, 0.0f, 2000.0f, 110.0f);
            ClampFloatSetting("predit_level2", predit_level2, 0.0f, 2000.0f, 110.0f);
            ClampFloatSetting("comarea", comarea, 0.0f, 20.0f, 0.01f);
            ClampFloatSetting("comspeed", comspeed, 0.0f, 20.0f, 0.5f);
            ClampFloatSetting("recoilnum", recoilnum, 0.0f, 20.0f, 0.5f);
            ClampFloatSetting("meleehealth", meleehealth, 0.0f, 1000.0f, 30.0f);
            ClampFloatSetting("meleedistance", meleedistance, 0.0f, 500.0f, 5.0f);
            ClampFloatSetting("AutoRMBhealth", AutoRMBhealth, 0.0f, 1000.0f, 100.0f);
            ClampFloatSetting("AutoRMBdistance", AutoRMBdistance, 0.0f, 500.0f, 30.0f);
            ClampFloatSetting("SkillHealth", SkillHealth, 0.0f, 1000.0f, 50.0f);
            ClampFloatSetting("CHANGEFOV", CHANGEFOV, 1.0f, 179.0f, 103.0f);
            ClampFloatSetting("healthbartextsize", healthbartextsize, 4.0f, 72.0f, 16.0f);
            ClampFloatSetting("lasthealth", lasthealth, 0.0f, 1000.0f, 0.0f);
            ClampFloatSetting("health", health, 0.0f, 1000.0f, 0.0f);

            ClampSetting("TargetBone", TargetBone, 0, 2, 0);
            ClampSetting("Bone", Bone, 0, 2, 1);
            ClampSetting("Bone2", Bone2, 0, 2, 1);
            ClampSetting("targetdelaytime", targetdelaytime, 0, 10000, 200);
            ClampSetting("hiboxdelaytime", hiboxdelaytime, 0, 10000, 200);
            ClampSetting("shotcount", shotcount, 0, 1000, 0);
            ClampSetting("shotmanydont", shotmanydont, 0, 1000, 3);
            ClampSetting("Shoottime", Shoottime, 0, 10000, 500);
            ClampSetting("lasttime", lasttime, 0, std::numeric_limits<int>::max(), 0);
            ClampSetting("slasttime", slasttime, 0, std::numeric_limits<int>::max(), 0);
            ClampSetting("Qstarttime", Qstarttime, 0, std::numeric_limits<int>::max(), 0);
            ClampSetting("Qtime", Qtime, 0, std::numeric_limits<int>::max(), 0);
            ClampSetting("lastenemy", lastenemy, -1, 100000, -1);
            ClampSetting("timebeforedelay", timebeforedelay, 0, std::numeric_limits<int>::max(), 0);
            ClampSetting("Targetenemyi", Targetenemyi, -1, 100000, -1);
            ClampSetting("Targetenemyifov", Targetenemyifov, -1, 100000, -1);
            ClampSetting("doingentity", doingentity, 0, 1, 1);
            ClampSetting("lastheroid", lastheroid, -2, std::numeric_limits<int>::max(), -2);
            ClampSetting("locx", locx, 0, 100000, 0);
            ClampSetting("locy", locy, 0, 100000, 0);
            ClampSetting("therad", therad, 0, 10000, 0);
            ClampSetting("pon", pon, 0, 10000, 0);
            ClampSetting("crss", crss, 0, 10000, 0);

            ClampColor("enargb", enargb);
            ClampColor("invisnenargb", invisnenargb);
            ClampColor("targetargb", targetargb);
            ClampColor("targetargb2", targetargb2);
            ClampColor("allyargb", allyargb);
            ClampColor("EnemyCol", EnemyCol);
            ClampColor("fovcol", fovcol);
            ClampColor("fovcol2", fovcol2);
            ClampColor("rainbowargb", rainbowargb);
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
            LogConfig(level, "Dump: aim modes enableAimbot=%s triggerbot=%s triggerbot2=%s Tracking=%s Tracking2=%s Flick=%s Flick2=%s hanzo_flick=%s silent=%s Rage=%s fakesilent=%s",
                ToText(enableAimbot).c_str(), ToText(triggerbot).c_str(), ToText(triggerbot2).c_str(),
                ToText(Tracking).c_str(), ToText(Tracking2).c_str(), ToText(Flick).c_str(),
                ToText(Flick2).c_str(), ToText(hanzo_flick).c_str(), ToText(silent).c_str(),
                ToText(Rage).c_str(), ToText(fakesilent).c_str());
            LogConfig(level, "Dump: prediction Prediction=%s Prediction2=%s Gravitypredit=%s Gravitypredit2=%s predit_level=%.3f predit_level2=%.3f hanzoautospeed=%s",
                ToText(Prediction).c_str(), ToText(Prediction2).c_str(), ToText(Gravitypredit).c_str(),
                ToText(Gravitypredit2).c_str(), predit_level, predit_level2, ToText(hanzoautospeed).c_str());
            LogConfig(level, "Dump: keys AimKey=%d aim_key=%d aim_key2=%d togglekey=%d MenuToggleKey=%d",
                AimKey, aim_key, aim_key2, togglekey, MenuToggleKey);
            LogConfig(level, "Dump: aim Fov=%.3f Fov2=%.3f minFov1=%.3f minFov2=%.3f Smooth=%.3f fov360=%s autoscalefov=%s hitbox=%.3f hitbox2=%.3f missbox=%.3f",
                Fov, Fov2, minFov1, minFov2, Smooth, ToText(fov360).c_str(), ToText(autoscalefov).c_str(),
                hitbox, hitbox2, missbox);
            LogConfig(level, "Dump: smoothing Tracking_smooth=%.3f Tracking_smooth2=%.3f Flick_smooth=%.3f Flick_smooth2=%.3f accvalue=%.3f accvalue2=%.3f bladespeed=%.3f",
                Tracking_smooth, Tracking_smooth2, Flick_smooth, Flick_smooth2, accvalue, accvalue2, bladespeed);
            LogConfig(level, "Dump: bones TargetBone=%d Bone=%d Bone2=%d autobone=%s autobone2=%s switch_team=%s switch_team2=%s BoneName=%s BoneName2=%s",
                TargetBone, Bone, Bone2, ToText(autobone).c_str(), ToText(autobone2).c_str(),
                ToText(switch_team).c_str(), ToText(switch_team2).c_str(), BoneName.c_str(), BoneName2.c_str());
            LogConfig(level, "Dump: targeting lockontarget=%s trackcompensate=%s comarea=%.3f comspeed=%.3f aiaim=%s targetdelay=%s targetdelaytime=%d hitboxdelayshoot=%s hiboxdelaytime=%d dontshot=%s shotcount=%d shotmanydont=%d",
                ToText(lockontarget).c_str(), ToText(trackcompensate).c_str(), comarea, comspeed, ToText(aiaim).c_str(),
                ToText(targetdelay).c_str(), targetdelaytime, ToText(hitboxdelayshoot).c_str(), hiboxdelaytime,
                ToText(dontshot).c_str(), shotcount, shotmanydont);
            LogConfig(level, "Dump: recoil norecoil=%s recoilnum=%.3f horizonreco=%s",
                ToText(norecoil).c_str(), recoilnum, ToText(horizonreco).c_str());
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
            LogConfig(level, "Dump: fov-change enablechangefov=%s CHANGEFOV=%.3f trackback=%s secondaim=%s highPriority=%s",
                ToText(enablechangefov).c_str(), CHANGEFOV, ToText(trackback).c_str(),
                ToText(secondaim).c_str(), ToText(highPriority).c_str());
            LogConfig(level, "Dump: visuals draw_info=%s drawbattletag=%s drawhealth=%s healthbar=%s healthbar2=%s healthbartextsize=%.3f dist=%s name=%s ult=%s draw_skel=%s skillinfo=%s outline=%s externaloutline=%s teamoutline=%s healthoutline=%s rainbowoutline=%s",
                ToText(draw_info).c_str(), ToText(drawbattletag).c_str(), ToText(drawhealth).c_str(), ToText(healthbar).c_str(),
                ToText(healthbar2).c_str(), healthbartextsize, ToText(dist).c_str(), ToText(name).c_str(), ToText(ult).c_str(),
                ToText(draw_skel).c_str(), ToText(skillinfo).c_str(), ToText(outline).c_str(), ToText(externaloutline).c_str(),
                ToText(teamoutline).c_str(), ToText(healthoutline).c_str(), ToText(rainbowoutline).c_str());
            LogConfig(level, "Dump: overlays draw_edge=%s drawbox3d=%s radar=%s radarline=%s drawline=%s draw_fov=%s draw_hp_pack=%s crosscircle=%s eyeray=%s testvalue=%s",
                ToText(draw_edge).c_str(), ToText(drawbox3d).c_str(), ToText(radar).c_str(), ToText(radarline).c_str(),
                ToText(drawline).c_str(), ToText(draw_fov).c_str(), ToText(draw_hp_pack).c_str(),
                ToText(crosscircle).c_str(), ToText(eyeray).c_str(), ToText(testvalue).c_str());
            LogConfig(level, "Dump: colors EnemyCol=%s fovcol=%s fovcol2=%s enargb=%s invisnenargb=%s targetargb=%s targetargb2=%s allyargb=%s rainbowargb=%s",
                ColorText(EnemyCol).c_str(), ColorText(fovcol).c_str(), ColorText(fovcol2).c_str(),
                ColorText(enargb).c_str(), ColorText(invisnenargb).c_str(), ColorText(targetargb).c_str(),
                ColorText(targetargb2).c_str(), ColorText(allyargb).c_str(), ColorText(rainbowargb).c_str());
            LogConfig(level, "Dump: state Targetenemyi=%d Targetenemyifov=%d health=%.3f doingentity=%d lastheroid=%d Menu=%s manualsave=%s loginornot=%s nowhero=%s namespoofer=%s fakename=%s locx=%d locy=%d therad=%d pon=%d crss=%d",
                Targetenemyi, Targetenemyifov, health, doingentity, lastheroid, ToText(Menu).c_str(), ToText(manualsave).c_str(),
                ToText(loginornot).c_str(), nowhero.c_str(), ToText(namespoofer).c_str(), fakename, locx, locy, therad, pon, crss);
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

    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ValidateUnlocked();

        if (heroId == 0) {
            LogConfig(Diagnostics::LogLevel::Warn, "Skipping config save because hero id is 0.");
            return;
        }

        const std::string heroName = HeroSectionName(heroId, linkBase);
        const char* section = heroName.c_str();

        WriteIntValue(path, kMetaSection, kVersionKey, kCurrentConfigVersion);

        WriteBoolValue(path, section, "highPriority", highPriority);
        WriteBoolValue(path, section, "aiaim", aiaim);
        WriteBoolValue(path, section, "hanzoautospeed", hanzoautospeed);
        WriteBoolValue(path, section, "autoscalefov", autoscalefov);
        WriteBoolValue(path, section, "lockontarget", lockontarget);
        WriteBoolValue(path, section, "trackc", trackcompensate);
        WriteFixedFloatValue(path, section, "comarea", comarea);
        WriteFixedFloatValue(path, section, "comspeed", comspeed);
        WriteIntValue(path, section, "FOV", static_cast<int>(Fov));
        WriteFixedFloatValue(path, section, "hitbox", hitbox);
        WriteFixedFloatValue(path, section, "missbox", missbox);
        WriteFixedFloatValue(path, section, "Tracking_smooth", Tracking_smooth);
        WriteFixedFloatValue(path, section, "Flick_smooth", Flick_smooth);
        WriteIntValue(path, section, "AutoShootTime", Shoottime);
        WriteIntValue(path, section, "predit_level", static_cast<int>(predit_level));
        WriteIntValue(path, section, "aim_key", aim_key);
        WriteBoolValue(path, section, "Gravitypredit", Gravitypredit);
        WriteIntValue(path, section, "SkillHealth", static_cast<int>(SkillHealth));
        WriteBoolValue(path, section, "AutoSkill", AutoSkill);
        WriteBoolValue(path, section, "AntiAFK", AntiAFK);
        WriteIntValue(path, section, "Aim Mode", CurrentAimMode());
        WriteBoolValue(path, section, "autoshootonoff", AutoShoot);
        WriteBoolValue(path, section, "predictdec", Prediction);
        WriteBoolValue(path, section, "dontshot", dontshot);
        WriteBoolValue(path, section, "targetdelay", targetdelay);
        WriteIntValue(path, section, "targetdelaytime", targetdelaytime);
        WriteIntValue(path, section, "dontmanyshot", shotmanydont);
        WriteBoolValue(path, section, "hitboxdelayshoot", hitboxdelayshoot);
        WriteIntValue(path, section, "hitboxdelaytime", hiboxdelaytime);
        WriteFixedFloatValue(path, section, "recoilnum", recoilnum);
        WriteFixedFloatValue(path, section, "accvalue", accvalue);
        WriteBoolValue(path, section, "norecoil", norecoil);
        WriteBoolValue(path, section, "horizonreco", horizonreco);
        WriteBoolValue(path, section, "switch_team", switch_team);
        WriteBoolValue(path, section, "switch_team2", switch_team2);
        WriteIntValue(path, section, "Bone", Bone);
        WriteBoolValue(path, section, "autobone", autobone);
        WriteIntValue(path, section, "Bone2", Bone2);
        WriteBoolValue(path, section, "autobone2", autobone2);
        WriteBoolValue(path, section, "AutoMelee", AutoMelee);
        WriteFixedFloatValue(path, section, "meleedistance", meleedistance);
        WriteFixedFloatValue(path, section, "meleehealth", meleehealth);
        WriteBoolValue(path, section, "AutoRMB", AutoRMB);
        WriteFixedFloatValue(path, section, "AutoRMBdistance", AutoRMBdistance);
        WriteFixedFloatValue(path, section, "AutoRMBhealth", AutoRMBhealth);
        WriteBoolValue(path, section, "secondaim", secondaim);
        WriteBoolValue(path, section, "triggerbot2", triggerbot2);
        WriteBoolValue(path, section, "Tracking2", Tracking2);
        WriteBoolValue(path, section, "Flick2", Flick2);
        WriteBoolValue(path, section, "Prediction2", Prediction2);
        WriteBoolValue(path, section, "Gravitypredit2", Gravitypredit2);
        WriteIntValue(path, section, "aim_key2", aim_key2);
        WriteIntValue(path, section, "togglekey", togglekey);
        WriteFixedFloatValue(path, section, "predit_level2", predit_level2);
        WriteFixedFloatValue(path, section, "Tracking_smooth2", Tracking_smooth2);
        WriteFixedFloatValue(path, section, "Flick_smooth2", Flick_smooth2);
        WriteFixedFloatValue(path, section, "accvalue2", accvalue2);
        WriteFixedFloatValue(path, section, "hitbox2", hitbox2);
        WriteFixedFloatValue(path, section, "Fov2", Fov2);
        WriteBoolValue(path, section, "enablechangefov", enablechangefov);
        WriteFixedFloatValue(path, section, "CHANGEFOV", CHANGEFOV);

        if (heroId == OW::eHero::HERO_GENJI) {
            WriteBoolValue(path, section, "GenjiBlade", GenjiBlade);
            WriteBoolValue(path, section, "AutoShiftGenji", AutoShiftGenji);
            WriteFixedFloatValue(path, section, "bladespeed", bladespeed);
        }
        if (heroId == OW::eHero::HERO_WIDOWMAKER) {
            WriteBoolValue(path, section, "widowautounscope", widowautounscope);
        }

        WriteBoolValue(path, "Global", "draw_hp_pack", draw_hp_pack);
        WriteBoolValue(path, "Global", "crosscircle", crosscircle);
        WriteBoolValue(path, "Global", "eyeray", eyeray);
        WriteBoolValue(path, "Global", "trackback", trackback);
        WriteBoolValue(path, "Global", "draw_info", draw_info);
        WriteBoolValue(path, "Global", "drawbattletag", drawbattletag);
        WriteBoolValue(path, "Global", "drawhealth", drawhealth);
        WriteBoolValue(path, "Global", "healthbar", healthbar);
        WriteBoolValue(path, "Global", "healthbar2", healthbar2);
        WriteFixedFloatValue(path, "Global", "healthbartextsize", healthbartextsize);
        WriteBoolValue(path, "Global", "dist", dist);
        WriteBoolValue(path, "Global", "name", name);
        WriteBoolValue(path, "Global", "ult", ult);
        WriteBoolValue(path, "Global", "draw_skel", draw_skel);
        WriteBoolValue(path, "Global", "skillinfo", skillinfo);
        WriteBoolValue(path, "Global", "outline", outline);
        WriteBoolValue(path, "Global", "externaloutline", externaloutline);
        WriteBoolValue(path, "Global", "teamoutline", teamoutline);
        WriteBoolValue(path, "Global", "healthoutline", healthoutline);
        WriteBoolValue(path, "Global", "rainbowoutline", rainbowoutline);
        WriteBoolValue(path, "Global", "draw_edge", draw_edge);
        WriteBoolValue(path, "Global", "drawbox3d", drawbox3d);
        WriteBoolValue(path, "Global", "radar", radar);
        WriteBoolValue(path, "Global", "radarline", radarline);
        WriteBoolValue(path, "Global", "drawline", drawline);
        WriteBoolValue(path, "Global", "draw_fov", draw_fov);
        WriteIntValue(path, "Global", "MenuToggleKey", MenuToggleKey);
        WriteColor(path, "Global", "EnemyCol", EnemyCol);
        WriteColor(path, "Global", "fovcol", fovcol);
        WriteColor(path, "Global", "fovcol2", fovcol2);
        WriteColor(path, "Global", "invisenargb", invisnenargb);
        WriteColor(path, "Global", "enargb", enargb);
        WriteColor(path, "Global", "targetargb", targetargb);
        WriteColor(path, "Global", "targetargb2", targetargb2);
        WriteColor(path, "Global", "allyargb", allyargb);

        LogConfig(Diagnostics::LogLevel::Info,
            "Saved config for hero %s to %s.", heroName.c_str(), path.c_str());
    }

    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase)
    {
        std::lock_guard<std::mutex> lock(mutex);

        const std::string heroName = HeroSectionName(heroId, linkBase);
        ResetHeroDefaultsUnlocked();
        ResetGlobalDefaultsUnlocked();

        IniFile ini;
        if (!LoadIniFile(path, ini)) {
            config_version = kCurrentConfigVersion;
            LogConfig(Diagnostics::LogLevel::Warn,
                "%s is missing or unreadable; using all documented defaults.",
                path.c_str());
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
            ValidateUnlocked();
            DumpUnlocked(Diagnostics::LogLevel::Debug);
            return;
        }

        if (fileVersion < kCurrentConfigVersion) {
            LogConfig(Diagnostics::LogLevel::Warn,
                "Migrating config version %d to %d.",
                fileVersion, kCurrentConfigVersion);
            WriteIntValue(path, kMetaSection, kVersionKey, kCurrentConfigVersion);
        }
        config_version = kCurrentConfigVersion;

        LoadHeroSettingsUnlocked(ini, heroName.c_str(), heroId);
        LoadGlobalSettingsUnlocked(ini);
        ValidateUnlocked();
        DumpUnlocked(Diagnostics::LogLevel::Debug);

        LogConfig(Diagnostics::LogLevel::Info,
            "Loaded config for hero %s from %s.", heroName.c_str(), path.c_str());
    }

    void SaveConfig(const std::string& path)
    {
        const uint64_t heroId = lastheroid > 0
            ? static_cast<uint64_t>(lastheroid)
            : OW::local_entity.HeroID;
        SaveConfigForHero(path, heroId, OW::local_entity.LinkBase);
    }

    void LoadConfig(const std::string& path)
    {
        LoadConfigForHero(path, OW::local_entity.HeroID, OW::local_entity.LinkBase);
    }

}} // namespace OW::Config
