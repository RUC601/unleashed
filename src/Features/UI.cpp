#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>

#include "Features/UI.hpp"
#include "Kmbox/KmBoxConfig.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Kmbox/KmboxMoveTest.h"
#include "Kmbox/KmboxTimerResolution.h"
#include "Utils/Config.hpp"
#include "Game/HeroSkills.hpp"
#include "Game/Offsets.hpp"
#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Game/Structs.hpp"
#include "Game/WeaponSpec.hpp"
#include "Renderer/Overlay.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"
#include "Utils/InputLabels.hpp"
#include "Utils/ProcessConnection.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static void ApplySelectedTypePreset();

// =====================================================================
// LOCAL UI STATE
// =====================================================================
namespace {

    constexpr size_t kConfigProfileNameBufferSize = 64;

    struct KmboxConnectionTestResult {
        bool ok = false;
        std::string message;
    };

    struct FirePatternRecorderEvent {
        long long timeMs = 0;
        int previousMask = 0;
        int mask = 0;
    };

    constexpr int kFirePatternRecorderMaxMs = 8000;
    std::atomic<bool> g_firePatternRecorderRunning{ false };
    std::jthread g_firePatternRecorderThread;
    std::mutex g_firePatternRecorderMutex;
    std::string g_firePatternRecorderStatus = "Idle";

    int MouseButtonMask(bool left, bool right)
    {
        return (left ? 0x01 : 0) | (right ? 0x02 : 0);
    }

    bool KmboxMonitorAvailable()
    {
        return OW::Config::kmboxEnabled &&
            OW::Config::kmboxDeviceType == 0 &&
            kmbox::KmBoxMgr.KeyBoard.ListenerRuned.load() &&
            kmbox::KmBoxMgr.KeyBoard.InputPacketCount() > 0;
    }

    bool DmaKeyStateAvailable()
    {
        return KeyState::initialized.load() && KeyState::gafAsyncKeyStateAddr.load() != 0;
    }

    bool ReadLocalMouseButton(int vk)
    {
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    bool ReadDmaMouseButton(int vk)
    {
        return DmaKeyStateAvailable() && KeyState::IsKeyDown(static_cast<uint32_t>(vk));
    }

    bool ReadKmboxMouseButton(int vk)
    {
        return KmboxMonitorAvailable() && kmbox::KmBoxMgr.KeyBoard.IsMouseButtonPressed(vk);
    }

    bool ReadRecorderMouseButton(int vk)
    {
        switch (OW::Config::inputSource) {
        case 1:
            if (KmboxMonitorAvailable())
                return ReadKmboxMouseButton(vk);
            return ReadDmaMouseButton(vk) || ReadLocalMouseButton(vk);
        case 2:
            return ReadLocalMouseButton(vk);
        case 3:
            return ReadDmaMouseButton(vk);
        default:
            if (KmboxMonitorAvailable())
                return ReadKmboxMouseButton(vk);
            return ReadDmaMouseButton(vk) || ReadLocalMouseButton(vk);
        }
    }

    const char* RecorderInputSourceName()
    {
        if (OW::Config::inputSource == 1)
            return KmboxMonitorAvailable() ? "kmbox_monitor" : "kmbox_fallback_dma_or_local";
        if (OW::Config::inputSource == 2)
            return "local_GetAsyncKeyState";
        if (OW::Config::inputSource == 3)
            return "dma_keystate";
        return KmboxMonitorAvailable() ? "auto_kmbox_monitor" : "auto_dma_or_local";
    }

    std::string MaskChangeText(int previousMask, int mask)
    {
        const int changed = previousMask ^ mask;
        std::string text;
        auto append = [&](const char* item) {
            if (!text.empty())
                text += "+";
            text += item;
        };

        if (changed & 0x01)
            append((mask & 0x01) ? "L_down" : "L_up");
        if (changed & 0x02)
            append((mask & 0x02) ? "R_down" : "R_up");
        if (text.empty())
            text = "none";
        return text;
    }

    void SetFirePatternRecorderStatus(const std::string& status)
    {
        std::lock_guard<std::mutex> lock(g_firePatternRecorderMutex);
        g_firePatternRecorderStatus = status;
    }

    std::string GetFirePatternRecorderStatus()
    {
        std::lock_guard<std::mutex> lock(g_firePatternRecorderMutex);
        return g_firePatternRecorderStatus;
    }

    void LogFirePatternRecorderSummary(const std::vector<FirePatternRecorderEvent>& events,
                                       long long totalMs,
                                       const char* sourceName)
    {
        Diagnostics::Aim("fire_pattern.record stop source=%s events=%zu totalMs=%lld",
            sourceName,
            events.size(),
            totalMs);

        if (events.empty()) {
            Diagnostics::Aim("fire_pattern.record result=no_mouse_edges");
            SetFirePatternRecorderStatus("No events captured");
            return;
        }

        const long long firstEventMs = events.front().timeMs;
        std::string candidate = "fire_pattern.steps_candidate ";
        for (size_t index = 0; index < events.size(); ++index) {
            const FirePatternRecorderEvent& event = events[index];
            const long long nextMs = index + 1 < events.size()
                ? events[index + 1].timeMs
                : totalMs;
            const long long durationMs = (std::max)(0ll, nextMs - event.timeMs);
            const long long cumulativeMs = (std::max)(0ll, nextMs - firstEventMs);
            const std::string action = MaskChangeText(event.previousMask, event.mask);

            Diagnostics::Aim("fire_pattern.raw event=%zu action=%s mask=0x%02X nextDeltaMs=%lld cumulativeMs=%lld",
                index + 1,
                action.c_str(),
                event.mask,
                durationMs,
                cumulativeMs);

            Diagnostics::Aim("fire_pattern.step step=%zu mask=0x%02X durationMs=%lld boundary=%s",
                index + 1,
                event.mask,
                durationMs,
                index + 1 < events.size() ? "next_event" : "stop");

            char stepText[48] = {};
            std::snprintf(stepText, sizeof(stepText), "{0x%02X,%lld}%s",
                event.mask,
                durationMs,
                index + 1 < events.size() ? "," : "");
            candidate += stepText;
        }

        Diagnostics::Aim("%s", candidate.c_str());

        char status[96] = {};
        std::snprintf(status, sizeof(status), "Captured %zu events", events.size());
        SetFirePatternRecorderStatus(status);
    }

    void FirePatternRecorderLoop(std::stop_token stopToken)
    {
        g_firePatternRecorderRunning.store(true, std::memory_order_release);
        SetFirePatternRecorderStatus("Recording...");
        timeBeginPeriod(1);

        std::vector<FirePatternRecorderEvent> events;
        events.reserve(64);

        const char* sourceName = RecorderInputSourceName();
        const auto started = std::chrono::steady_clock::now();
        bool left = ReadRecorderMouseButton(VK_LBUTTON);
        bool right = ReadRecorderMouseButton(VK_RBUTTON);
        int previousMask = MouseButtonMask(left, right);

        Diagnostics::Aim("fire_pattern.record start source=%s initialMask=0x%02X maxMs=%d",
            sourceName,
            previousMask,
            kFirePatternRecorderMaxMs);

        long long elapsedMs = 0;
        while (!stopToken.stop_requested()) {
            const auto now = std::chrono::steady_clock::now();
            elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
            if (elapsedMs >= kFirePatternRecorderMaxMs)
                break;

            left = ReadRecorderMouseButton(VK_LBUTTON);
            right = ReadRecorderMouseButton(VK_RBUTTON);
            const int mask = MouseButtonMask(left, right);
            if (mask != previousMask) {
                events.push_back({ elapsedMs, previousMask, mask });
                Diagnostics::Aim("fire_pattern.edge tMs=%lld action=%s previousMask=0x%02X mask=0x%02X",
                    elapsedMs,
                    MaskChangeText(previousMask, mask).c_str(),
                    previousMask,
                    mask);
                previousMask = mask;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        const auto finished = std::chrono::steady_clock::now();
        elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();
        LogFirePatternRecorderSummary(events, elapsedMs, sourceName);
        timeEndPeriod(1);
        g_firePatternRecorderRunning.store(false, std::memory_order_release);
    }

    bool IsFirePatternRecorderRunning()
    {
        return g_firePatternRecorderRunning.load(std::memory_order_acquire);
    }

    void StartFirePatternRecorder()
    {
        if (IsFirePatternRecorderRunning())
            return;

        if (g_firePatternRecorderThread.joinable())
            g_firePatternRecorderThread.join();

        g_firePatternRecorderThread = std::jthread(FirePatternRecorderLoop);
    }

    void StopFirePatternRecorder()
    {
        if (!g_firePatternRecorderThread.joinable())
            return;

        g_firePatternRecorderThread.request_stop();
        g_firePatternRecorderThread.join();
    }

    std::string CurrentDirectoryPath()
    {
        char buffer[MAX_PATH]{};
        DWORD length = GetCurrentDirectoryA(MAX_PATH, buffer);
        if (length == 0)
            return ".";

        if (length < MAX_PATH)
            return std::string(buffer, length);

        std::vector<char> dynamicBuffer(static_cast<size_t>(length) + 1);
        length = GetCurrentDirectoryA(static_cast<DWORD>(dynamicBuffer.size()), dynamicBuffer.data());
        if (length == 0 || static_cast<size_t>(length) >= dynamicBuffer.size())
            return ".";

        return std::string(dynamicBuffer.data(), length);
    }

    std::string JoinPath(const std::string& directory, const char* child)
    {
        if (directory.empty())
            return child;

        const char tail = directory.back();
        if (tail == '\\' || tail == '/')
            return directory + child;

        return directory + "\\" + child;
    }

    bool RegularFileExists(const std::string& path)
    {
        const DWORD attributes = GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }

    std::string SocketErrorMessage(const char* prefix, int error)
    {
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "%s (WSA %d)", prefix, error);
        return buffer;
    }

    std::string Win32ErrorMessage(const char* prefix, DWORD error)
    {
        char buffer[96]{};
        std::snprintf(buffer, sizeof(buffer), "%s (error %lu)", prefix, static_cast<unsigned long>(error));
        return buffer;
    }

    bool TryParseHexU32(const char* text, unsigned int& value)
    {
        if (text == nullptr || *text == '\0')
            return false;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 16);
        if (end == text || (end != nullptr && *end != '\0'))
            return false;

        value = static_cast<unsigned int>(parsed);
        return true;
    }

    std::wstring PowerShellSingleQuoted(const char* text)
    {
        std::wstring quoted = L"'";
        if (text != nullptr) {
            for (const char* current = text; *current != '\0'; ++current) {
                if (*current == '\'')
                    quoted += L"''";
                else
                    quoted += static_cast<unsigned char>(*current);
            }
        }
        quoted += L"'";
        return quoted;
    }

    std::wstring PowerShellSingleQuoted(const std::wstring& text)
    {
        std::wstring quoted = L"'";
        for (const wchar_t ch : text) {
            if (ch == L'\'')
                quoted += L"''";
            else
                quoted += ch;
        }
        quoted += L"'";
        return quoted;
    }

    std::wstring CurrentExecutablePath()
    {
        std::array<wchar_t, MAX_PATH> stackBuffer{};
        DWORD length = GetModuleFileNameW(nullptr, stackBuffer.data(), static_cast<DWORD>(stackBuffer.size()));
        if (length == 0)
            return {};

        if (length < static_cast<DWORD>(stackBuffer.size()))
            return std::wstring(stackBuffer.data(), length);

        std::vector<wchar_t> dynamicBuffer(32768);
        length = GetModuleFileNameW(nullptr, dynamicBuffer.data(), static_cast<DWORD>(dynamicBuffer.size()));
        if (length == 0 || length >= static_cast<DWORD>(dynamicBuffer.size()))
            return {};

        return std::wstring(dynamicBuffer.data(), length);
    }

    KmboxConnectionTestResult AllowKmboxMonitorFirewall()
    {
        if (OW::Config::kmboxDeviceType != 0)
            return { false, "Network mode only" };

        const int monitorPort = OW::Config::kmboxMonitorPort;
        if (monitorPort <= 0 || monitorPort > 65535)
            return { false, "Invalid monitor port" };

        const std::wstring executablePath = CurrentExecutablePath();
        if (executablePath.empty())
            return { false, "EXE path unavailable" };

        const std::wstring exe = PowerShellSingleQuoted(executablePath);
        const std::wstring displayName =
            PowerShellSingleQuoted(L"Unleashed KMBox Monitor UDP " + std::to_wstring(monitorPort));
        const std::wstring command =
            L"-NoProfile -ExecutionPolicy Bypass -Command "
            L"\"$exe = " + exe + L"; "
            L"$port = " + std::to_wstring(monitorPort) + L"; "
            L"$portText = [string]$port; "
            L"$name = " + displayName + L"; "
            L"$blocks = Get-NetFirewallRule -Direction Inbound -Action Block -ErrorAction SilentlyContinue | "
            L"Where-Object { "
            L"try { "
            L"$app = Get-NetFirewallApplicationFilter -AssociatedNetFirewallRule $_ -ErrorAction Stop; "
            L"$pf = Get-NetFirewallPortFilter -AssociatedNetFirewallRule $_ -ErrorAction Stop; "
            L"($app.Program -eq $exe) -and "
            L"(($pf.Protocol -eq 'UDP') -or ($pf.Protocol -eq 'Any')) -and "
            L"((@($pf.LocalPort) -contains $portText) -or (@($pf.LocalPort) -contains 'Any')) "
            L"} catch { $false } "
            L"}; "
            L"$blocks | Disable-NetFirewallRule -ErrorAction SilentlyContinue; "
            L"Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | "
            L"Remove-NetFirewallRule -ErrorAction SilentlyContinue; "
            L"New-NetFirewallRule -DisplayName $name -Direction Inbound -Action Allow "
            L"-Program $exe -Protocol UDP -LocalPort $port -Profile Any -ErrorAction Stop | Out-Null\"";

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"runas",
            L"powershell.exe",
            command.c_str(),
            nullptr,
            SW_HIDE);

        const auto resultCode = reinterpret_cast<intptr_t>(result);
        if (resultCode <= 32)
            return { false, Win32ErrorMessage("Firewall launch failed", static_cast<DWORD>(resultCode)) };

        Diagnostics::Info("KMBox monitor firewall allow requested. monitorPort=%d", monitorPort);
        Diagnostics::Aim("kmbox.firewall allow_requested monitorPort=%d", monitorPort);
        return { true, "Firewall rule requested" };
    }

    KmboxConnectionTestResult RestartKmboxNetworkAdapter()
    {
        const std::wstring targetIp = PowerShellSingleQuoted(OW::Config::kmboxIp);
        const std::wstring command =
            L"-NoProfile -ExecutionPolicy Bypass -Command "
            L"\"$target = " + targetIp + L"; "
            L"$adapter = $null; "
            L"try { "
            L"$route = Find-NetRoute -RemoteIPAddress $target -ErrorAction Stop | "
            L"Sort-Object RouteMetric,InterfaceMetric | Select-Object -First 1; "
            L"if ($route) { $adapter = Get-NetAdapter -InterfaceIndex $route.InterfaceIndex -ErrorAction Stop; } "
            L"} catch {} "
            L"if (-not $adapter) { "
            L"$adapter = Get-NetAdapter -InterfaceDescription 'USB 2.0 Ethernet Adapter' -ErrorAction Stop | "
            L"Select-Object -First 1; "
            L"} "
            L"Restart-NetAdapter -Name $adapter.Name -Confirm:$false\"";

        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"runas",
            L"powershell.exe",
            command.c_str(),
            nullptr,
            SW_HIDE);

        const auto resultCode = reinterpret_cast<intptr_t>(result);
        if (resultCode <= 32)
            return { false, Win32ErrorMessage("Restart launch failed", static_cast<DWORD>(resultCode)) };

        return { true, "NIC restart requested" };
    }

    KmboxConnectionTestResult TestNetworkKmboxConnection()
    {
        if (OW::Config::kmboxPort <= 0 || OW::Config::kmboxPort > 65535)
            return { false, "Invalid port" };

        unsigned int deviceMac = 0;
        if (!TryParseHexU32(OW::Config::kmboxMac, deviceMac))
            return { false, "Invalid MAC" };

        WSADATA wsaData{};
        const int startupStatus = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (startupStatus != 0)
            return { false, SocketErrorMessage("WSAStartup failed", startupStatus) };

        SOCKET socketHandle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (socketHandle == INVALID_SOCKET) {
            const int error = WSAGetLastError();
            WSACleanup();
            return { false, SocketErrorMessage("Socket failed", error) };
        }

        const int timeoutMs = 1000;
        setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
        setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(OW::Config::kmboxPort));
        if (InetPtonA(AF_INET, OW::Config::kmboxIp, &address.sin_addr) != 1) {
            closesocket(socketHandle);
            WSACleanup();
            return { false, "Invalid IP" };
        }

        cmd_head_t testPacket{};
        testPacket.mac = deviceMac;
        testPacket.rand = GetTickCount();
        testPacket.indexpts = 0;
        testPacket.cmd = cmd_connect;

        const int sent = sendto(socketHandle, reinterpret_cast<const char*>(&testPacket),
                                sizeof(testPacket), 0,
                                reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        if (sent == SOCKET_ERROR || sent != static_cast<int>(sizeof(testPacket))) {
            const int error = WSAGetLastError();
            closesocket(socketHandle);
            WSACleanup();
            return { false, SocketErrorMessage("Send failed", error) };
        }

        client_data response{};
        sockaddr_in from{};
        int fromLength = sizeof(from);
        const int received = recvfrom(socketHandle, reinterpret_cast<char*>(&response), sizeof(response), 0,
                                      reinterpret_cast<sockaddr*>(&from), &fromLength);
        if (received == SOCKET_ERROR) {
            const int error = WSAGetLastError();
            closesocket(socketHandle);
            WSACleanup();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK)
                return { false, "Timeout" };
            return { false, SocketErrorMessage("Receive failed", error) };
        }

        if (received < static_cast<int>(sizeof(cmd_head_t))) {
            closesocket(socketHandle);
            WSACleanup();
            return { false, "Short response" };
        }

        if (response.head.cmd != testPacket.cmd) {
            closesocket(socketHandle);
            WSACleanup();
            return { false, "Command mismatch" };
        }

        if (response.head.indexpts != testPacket.indexpts) {
            closesocket(socketHandle);
            WSACleanup();
            return { false, "Sequence mismatch" };
        }

        closesocket(socketHandle);
        WSACleanup();
        return { true, "OK: connect" };
    }

    std::string NormalizeComPortPath(const char* comPort)
    {
        const std::string port = (comPort && *comPort) ? comPort : "COM1";
        if (port.rfind("\\\\.\\", 0) == 0)
            return port;
        return "\\\\.\\" + port;
    }

    KmboxConnectionTestResult TestSerialKmboxConnection()
    {
        const std::string portPath = NormalizeComPortPath(OW::Config::kmboxComPort);
        HANDLE portHandle = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (portHandle == INVALID_HANDLE_VALUE)
            return { false, Win32ErrorMessage("Open failed", GetLastError()) };

        CloseHandle(portHandle);
        return { true, "OK: opened" };
    }

    KmboxConnectionTestResult TestKmboxConnection()
    {
        return OW::Config::kmboxDeviceType == 0
            ? TestNetworkKmboxConnection()
            : TestSerialKmboxConnection();
    }

    KmboxConnectionTestResult InitializeKmboxFromCurrentConfig()
    {
        if (!OW::Config::kmboxEnabled)
            return { false, "Disabled" };

        kmbox::EnsureTimerResolution();

        if (OW::Config::kmboxDeviceType == 0) {
            if (OW::Config::kmboxPort <= 0 || OW::Config::kmboxPort > 65535)
                return { false, "Invalid port" };

            const int status = kmbox::KmBoxMgr.InitDevice(
                OW::Config::kmboxIp,
                static_cast<WORD>(OW::Config::kmboxPort),
                OW::Config::kmboxMac);
            if (status != success || !kmbox::KmBoxMgr.IsConnected()) {
                char message[96] = {};
                std::snprintf(message, sizeof(message), "Init failed: %d", status);
                Diagnostics::Error("KMBox UI enable network init failed. status=%d state=%s",
                    status, ToString(kmbox::KmBoxMgr.GetConnectionState()));
                return { false, message };
            }

            const WORD monitorPort = static_cast<WORD>(OW::Config::kmboxMonitorPort);
            const int monitorStatus = kmbox::KmBoxMgr.KeyBoard.StartMonitor(monitorPort);
            if (monitorStatus != success) {
                char message[96] = {};
                std::snprintf(message, sizeof(message), "Init OK, monitor failed: %d", monitorStatus);
                Diagnostics::Warn("KMBox UI enable network init succeeded, monitor failed. port=%u status=%d",
                    monitorPort, monitorStatus);
                return { true, message };
            }

            Diagnostics::Info("KMBox UI enable network init succeeded. ip=%s port=%d monitor=%u",
                OW::Config::kmboxIp, OW::Config::kmboxPort, monitorPort);
            Diagnostics::Aim("kmbox.ui_enable network success ip=%s port=%d monitor=%u",
                OW::Config::kmboxIp, OW::Config::kmboxPort, monitorPort);
            return { true, "Init OK" };
        }

        const int status = kmbox::kmBoxBMgr.init(OW::Config::kmboxComPort);
        if (status != success || !kmbox::kmBoxBMgr.IsConnected()) {
            char message[96] = {};
            std::snprintf(message, sizeof(message), "Init failed: %d", status);
            Diagnostics::Error("KMBox UI enable serial init failed. status=%d state=%s",
                status, ToString(kmbox::kmBoxBMgr.GetConnectionState()));
            return { false, message };
        }

        Diagnostics::Info("KMBox UI enable serial init succeeded. port=%s", OW::Config::kmboxComPort);
        Diagnostics::Aim("kmbox.ui_enable serial success port=%s", OW::Config::kmboxComPort);
        return { true, "Init OK" };
    }

    void CopyConfigProfileName(char (&destination)[kConfigProfileNameBufferSize], const std::string& name)
    {
        std::snprintf(destination, kConfigProfileNameBufferSize, "%s", name.c_str());
    }

    std::string NormalizeProfileFileName(const char* rawName)
    {
        std::string name = rawName ? rawName : "";

        const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
        name.erase(name.begin(), std::find_if(name.begin(), name.end(),
            [&](unsigned char ch) { return !isSpace(ch); }));
        name.erase(std::find_if(name.rbegin(), name.rend(),
            [&](unsigned char ch) { return !isSpace(ch); }).base(), name.end());

        const size_t slash = name.find_last_of("\\/");
        if (slash != std::string::npos)
            name = name.substr(slash + 1);

        for (char& ch : name) {
            const unsigned char value = static_cast<unsigned char>(ch);
            if (value < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' ||
                ch == '/' || ch == '\\' || ch == '|' || ch == '?' || ch == '*') {
                ch = '_';
            }
        }

        if (name.empty())
            name = "config";

        std::string lowered = name;
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (lowered.size() < 4 || lowered.substr(lowered.size() - 4) != ".ini")
            name += ".ini";

        return name;
    }

    void ReloadConfigProfile()
    {
        const std::string savedName = OW::Config::configFileName;
        const std::string path = OW::Config::ConfigPath();
        OW::Config::LoadConfig(path);
        OW::Config::configFileName = savedName;
        OW::RefreshScreenSizeFromConfig();
    }

    void SelectConfigProfile(const char* name, char (&profileName)[kConfigProfileNameBufferSize])
    {
        CopyConfigProfileName(profileName, name);
        OW::Config::configFileName = profileName;
        ReloadConfigProfile();
        OW::Config::lastConfigProfile = profileName;
        ApplySelectedTypePreset();
    }

    std::vector<std::string> EnumerateConfigProfiles()
    {
        std::vector<std::string> profiles;
        WIN32_FIND_DATAA findData{};
        const std::string searchPath = JoinPath(OW::Config::ConfigDirectoryPath(), "*.ini");
        HANDLE findHandle = FindFirstFileA(searchPath.c_str(), &findData);
        if (findHandle != INVALID_HANDLE_VALUE) {
            do {
                if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                    profiles.emplace_back(findData.cFileName);
            } while (FindNextFileA(findHandle, &findData));
            FindClose(findHandle);
        }

        if (std::find(profiles.begin(), profiles.end(), "config.ini") == profiles.end())
            profiles.emplace_back("config.ini");
        if (!OW::Config::configFileName.empty() &&
            std::find(profiles.begin(), profiles.end(), OW::Config::configFileName) == profiles.end())
            profiles.emplace_back(OW::Config::configFileName);

        std::sort(profiles.begin(), profiles.end());
        profiles.erase(std::unique(profiles.begin(), profiles.end()), profiles.end());
        return profiles;
    }

    bool CreateConfigProfileFromCurrent(const std::string& profileName)
    {
        const std::string targetPath = JoinPath(OW::Config::ConfigDirectoryPath(), profileName.c_str());
        if (RegularFileExists(targetPath))
            return false;

        const std::string sourcePath = OW::Config::ConfigPath();
        if (RegularFileExists(sourcePath)) {
            CopyFileA(sourcePath.c_str(), targetPath.c_str(), TRUE);

            const std::string sourceHeroPath = OW::Config::HeroConfigPath(sourcePath);
            const std::string targetHeroPath = OW::Config::HeroConfigPath(targetPath);
            if (RegularFileExists(sourceHeroPath))
                CopyFileA(sourceHeroPath.c_str(), targetHeroPath.c_str(), TRUE);
        } else {
            OW::Config::SaveConfig(targetPath);
            OW::Config::SaveHeroConfig(targetPath);
        }

        return true;
    }

    int FindConfigProfileIndex(const std::vector<std::string>& profiles, const std::string& name)
    {
        for (int i = 0; i < static_cast<int>(profiles.size()); ++i) {
            if (profiles[static_cast<size_t>(i)] == name)
                return i;
        }
        return 0;
    }

    void PersistLastConfigProfile()
    {
        OW::Config::SaveConfig(OW::Config::ConfigPath());
        WritePrivateProfileStringA(
            "Global",
            "lastConfigProfile",
            OW::Config::lastConfigProfile.c_str(),
            OW::Config::ConfigPath().c_str());
    }

} // anonymous namespace

// =====================================================================
// SELECT OPTION STRINGS (matching React selectOptions)
// =====================================================================
struct HeroOption {
    const char* label;
    uint64_t heroId;
    const char* category;
};

static const HeroOption kHeroOptions[] = {
    { "All", 0, "Global" },
    { "Reinhardt", OW::eHero::HERO_REINHARDT, "Tank" },
    { "Winston", OW::eHero::HERO_WINSTON, "Tank" },
    { "Zarya", OW::eHero::HERO_ZARYA, "Tank" },
    { "DVa", OW::eHero::HERO_DVA, "Tank" },
    { "Roadhog", OW::eHero::HERO_ROADHOG, "Tank" },
    { "Orisa", OW::eHero::HERO_ORISA, "Tank" },
    { "WreckingBall", OW::eHero::HERO_WRECKINGBALL, "Tank" },
    { "Sigma", OW::eHero::HERO_SIGMA, "Tank" },
    { "Doomfist", OW::eHero::HERO_DOOMFIST, "Tank" },
    { "Ramattra", OW::eHero::HERO_RAMATTRA, "Tank" },
    { "JunkerQueen", OW::eHero::HERO_JUNKERQUEEN, "Tank" },
    { "Mauga", OW::eHero::HERO_MAUGA, "Tank" },
    { "Hazard", OW::eHero::HERO_HAZARD, "Tank" },
    { "Tracer", OW::eHero::HERO_TRACER, "Damage" },
    { "Widowmaker", OW::eHero::HERO_WIDOWMAKER, "Damage" },
    { "Soldier76", OW::eHero::HERO_SOLDIER76, "Damage" },
    { "Genji", OW::eHero::HERO_GENJI, "Damage" },
    { "Hanzo", OW::eHero::HERO_HANJO, "Damage" },
    { "Cassidy", OW::eHero::HERO_MCCREE, "Damage" },
    { "Pharah", OW::eHero::HERO_PHARAH, "Damage" },
    { "Reaper", OW::eHero::HERO_REAPER, "Damage" },
    { "Sombra", OW::eHero::HERO_SOMBRA, "Damage" },
    { "Symmetra", OW::eHero::HERO_SYMMETRA, "Damage" },
    { "Torbjorn", OW::eHero::HERO_TORBJORN, "Damage" },
    { "Bastion", OW::eHero::HERO_BASTION, "Damage" },
    { "Junkrat", OW::eHero::HERO_JUNKRAT, "Damage" },
    { "Mei", OW::eHero::HERO_MEI, "Damage" },
    { "Ashe", OW::eHero::HERO_ASHE, "Damage" },
    { "Sojourn", OW::eHero::HERO_SOJOURN, "Damage" },
    { "Venture", OW::eHero::HERO_VENTURE, "Damage" },
    { "Echo", OW::eHero::HERO_ECHO, "Damage" },
    { "Freja", OW::eHero::HERO_FREJA, "Damage" },
    { "Vendetta", OW::eHero::HERO_VENDETTA, "Damage" },
    { "Anran", OW::eHero::HERO_ANRAN, "Damage" },
    { "Mercy", OW::eHero::HERO_MERCY, "Support" },
    { "Lucio", OW::eHero::HERO_LUCIO, "Support" },
    { "Zenyatta", OW::eHero::HERO_ZENYATTA, "Support" },
    { "Ana", OW::eHero::HERO_ANA, "Support" },
    { "Brigitte", OW::eHero::HERO_BRIGITTE, "Support" },
    { "Moira", OW::eHero::HERO_MOIRA, "Support" },
    { "Baptiste", OW::eHero::HERO_BAPTISTE, "Support" },
    { "Kiriko", OW::eHero::HERO_KIRIKO, "Support" },
    { "Lifeweaver", OW::eHero::HERO_LIFEWEAVER, "Support" },
    { "Illari", OW::eHero::HERO_ILLARI, "Support" },
    { "Juno", OW::eHero::HERO_JUNO, "Support" },
    { "Wuyang", OW::eHero::HERO_WUYANG, "Support" },
    { "JetpackCat", OW::eHero::HERO_JETPACKCAT, "Support" },
};
static const char* kInputSource[] = {
    "KMBox Monitor (Primary)", "Auto (KMBox > DMA > Local)",
    "DMA KeyState (Diagnostic)", "Local GetAsyncKeyState (Diagnostic)"
};
static const char* kHeroSkillModes[] = {
    "Auto", "Assist", "Manual"
};
static const char* kHeroSkillTrackingBones[] = {
    "Chest", "Head", "Neck"
};
static const char* kHeroSkillTargetBones[] = {
    "Chest", "Head", "Neck", "Closest"
};
static constexpr int kInputSourceConfigOrder[] = { 1, 0, 3, 2 };
static const char* kBonePreference[] = { "Head", "Neck", "Chest", "Closest" };
static constexpr int kBonePreferenceAimBones[] = {
    OW::Config::kAimBoneHead,
    OW::Config::kAimBoneNeck,
    OW::Config::kAimBoneChest
};
static constexpr int kBonePreferenceClosestIndex = 3;
static const char* kAimBehavior[]  = { "Tracking", "Flick", "Flick2nd", "Reacquire" };
static const char* kAimMethod[]    = { "Linear", "PID", "Bezier", "Piecewise", "Accel Limited", "Constant" };
static const char* kAimSmoothType[] = { "Constant Speed", "Linear", "Bezier" };
static const char* kPredictionMode[] = { "Auto", "Force On", "Force Off" };
static const char* kFirePolicy[] = {
    "Manual", "Hold Tracking", "Tap Hit Window", "Release Delay", "Timed Burst", "Charge Release"
};
static const char* kPriority[]     = { "Lowest FOV", "Lowest HP", "Distance" };
static const char* kTeam[]         = { "Enemies", "Allies", "All" };
static const char* kTrace[]        = { "Strict", "Relaxed", "Off" };
static const char* kUnlock[]       = { "Anytime", "On Release", "Never" };
static const char* kKmBoxDeviceTypes[] = { "Network", "Serial" };
static const char* kMenuToggleKeys[] = {
    "Home", "Insert", "End", "Delete",
    "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12"
};
static constexpr int kMenuToggleVk[] = {
    VK_HOME, VK_INSERT, VK_END, VK_DELETE,
    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6,
    VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12
};

static int InputSourceConfigToUiIndex(int configValue) {
    for (int i = 0; i < IM_ARRAYSIZE(kInputSourceConfigOrder); ++i) {
        if (kInputSourceConfigOrder[i] == configValue)
            return i;
    }
    return 0;
}

static int InputSourceUiIndexToConfig(int uiIndex) {
    if (uiIndex < 0 || uiIndex >= IM_ARRAYSIZE(kInputSourceConfigOrder))
        return 0;
    return kInputSourceConfigOrder[uiIndex];
}

static void DrawProbeState(const char* label, bool available, bool down) {
    const ImVec4 color = !available
        ? ImVec4(0.55f, 0.58f, 0.62f, 1.0f)
        : (down ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f) : ImVec4(0.86f, 0.88f, 0.92f, 1.0f));
    ImGui::TextColored(color, "%s: %s", label, !available ? "n/a" : (down ? "DOWN" : "up"));
}

static bool IsAimMouseActivationVk(int vk) {
    switch (vk) {
    case VK_LBUTTON:
    case VK_RBUTTON:
    case VK_MBUTTON:
    case VK_XBUTTON1:
    case VK_XBUTTON2:
        return true;
    default:
        return false;
    }
}

static void DrawAimHotkeyProbe() {
    const int keySetting = OW::Config::aim_key;
    const int vk = OW::get_bind_id(keySetting);
    const char* keyLabel = OW::Labels::AimActivationKeyName(keySetting);

    ImGui::Text("Hotkey Probe: %s  vk=0x%02X", keyLabel, vk > 0 ? vk : 0);
    if (vk <= 0) {
        DrawProbeState("KMBox Monitor", false, false);
        ImGui::SameLine();
        DrawProbeState("DMA KeyState", false, false);
        ImGui::SameLine();
        DrawProbeState("Local", false, false);
        return;
    }

    const bool localDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
    const bool kmboxAvailable =
        OW::Config::kmboxEnabled &&
        OW::Config::kmboxDeviceType == 0 &&
        kmbox::KmBoxMgr.KeyBoard.ListenerRuned.load();
    bool kmboxDown = false;
    if (kmboxAvailable) {
        kmboxDown = IsAimMouseActivationVk(vk)
            ? kmbox::KmBoxMgr.KeyBoard.IsMouseButtonPressed(vk)
            : kmbox::KmBoxMgr.KeyBoard.GetKeyState(static_cast<WORD>(vk));
    }

    const bool dmaAvailable =
        KeyState::initialized.load() &&
        KeyState::gafAsyncKeyStateAddr.load() != 0;
    const bool dmaDown = dmaAvailable && KeyState::IsKeyDown(vk);

    DrawProbeState("KMBox Monitor", kmboxAvailable, kmboxDown);
    ImGui::SameLine();
    DrawProbeState("DMA KeyState", dmaAvailable, dmaDown);
    ImGui::SameLine();
    DrawProbeState("Local", true, localDown);
}

static int ClampHeroPresetSlotIndex(int slotIndex) {
    return ImClamp(slotIndex, 0, OW::Config::kMaxHeroPresetSlots - 1);
}

enum class ActionSlotKind {
    Aim,
    Trigger
};

static int& ActiveHeroPresetSlotRef(ActionSlotKind kind) {
    return kind == ActionSlotKind::Aim
        ? UI::state.aimHeroSegActive
        : UI::state.triggerHeroSegActive;
}

static int ActiveHeroPresetSlotIndex(ActionSlotKind kind) {
    int& activeSlot = ActiveHeroPresetSlotRef(kind);
    activeSlot = ClampHeroPresetSlotIndex(activeSlot);
    return activeSlot;
}

static bool IsHeroActionSlotEnabled(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::IsHeroAimSlotEnabled(heroId, slotIndex)
        : OW::Config::IsHeroTriggerSlotEnabled(heroId, slotIndex);
}

static void SetHeroActionSlotEnabled(ActionSlotKind kind, uint64_t heroId, int slotIndex, bool enabled) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::SetHeroAimSlotEnabled(heroId, slotIndex, enabled);
    else
        OW::Config::SetHeroTriggerSlotEnabled(heroId, slotIndex, enabled);
}

static int GetHeroActionSlotCount(ActionSlotKind kind, uint64_t heroId) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::GetHeroAimSlotCount(heroId)
        : OW::Config::GetHeroTriggerSlotCount(heroId);
}

static OW::Config::HeroPreset MakeCurrentHeroActionPreset(ActionSlotKind kind) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::MakeHeroAimPresetFromCurrent()
        : OW::Config::MakeHeroTriggerPresetFromCurrent();
}

static bool HasHeroActionPreset(ActionSlotKind kind, uint64_t heroId) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::HasHeroAimPreset(heroId)
        : OW::Config::HasHeroTriggerPreset(heroId);
}

static OW::Config::HeroPreset GetHeroActionPresetOrDefault(ActionSlotKind kind,
                                                           uint64_t heroId,
                                                           int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::GetHeroAimPresetOrDefault(heroId, slotIndex)
        : OW::Config::GetHeroTriggerPresetOrDefault(heroId, slotIndex);
}

static void SetHeroActionPreset(ActionSlotKind kind,
                                uint64_t heroId,
                                int slotIndex,
                                const OW::Config::HeroPreset& preset) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::SetHeroAimPreset(heroId, slotIndex, preset);
    else
        OW::Config::SetHeroTriggerPreset(heroId, slotIndex, preset);
}

static int AddHeroActionSlot(ActionSlotKind kind,
                             uint64_t heroId,
                             const OW::Config::HeroPreset& seedPreset) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::AddHeroAimSlot(heroId, seedPreset)
        : OW::Config::AddHeroTriggerSlot(heroId, seedPreset);
}

static bool DeleteHeroActionSlot(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    return kind == ActionSlotKind::Aim
        ? OW::Config::DeleteHeroAimSlot(heroId, slotIndex)
        : OW::Config::DeleteHeroTriggerSlot(heroId, slotIndex);
}

static void ApplyHeroActionPresetToGlobals(ActionSlotKind kind, const OW::Config::HeroPreset& preset) {
    if (kind == ActionSlotKind::Aim)
        OW::Config::ApplyHeroAimPresetToGlobals(preset);
    else
        OW::Config::ApplyHeroTriggerPresetToGlobals(preset);
}

static void PushHeroActionSlotControlId(ActionSlotKind kind, uint64_t heroId, int slotIndex) {
    char id[64] = {};
    std::snprintf(id, sizeof(id), "%s:%llX:%d",
                  kind == ActionSlotKind::Aim ? "AimSlot" : "TriggerSlot",
                  static_cast<unsigned long long>(heroId),
                  ClampHeroPresetSlotIndex(slotIndex));
    ImGui::PushID(id);
}

static bool IsConcreteHeroSelection(const HeroOption& hero) {
    return hero.heroId != 0;
}

static void ShowAimSlotSummaryTooltip(bool hasSpecificHero) {
    ImGui::SetItemTooltip("%s",
        hasSpecificHero
            ? "Aim and Trigger have separate slot lists for the selected hero. Save Config writes that hero's JSON config."
            : "All uses the current local hero or global config fallback. Aim and Trigger slot lists are separate.");
}

static void ShowSaveConfigTooltip(bool savesSelectedHero) {
    ImGui::SetItemTooltip("%s",
        savesSelectedHero
            ? "Save the selected hero's presets to config.heroes.json."
            : "Save config.ini using the current local hero or global fallback.");
}

// =====================================================================
// GROUP BOX STATE (lazy border drawing)
// =====================================================================
static bool     g_gbOpen      = false;
static char     g_gbTitle[64] = "";
static ImVec2   g_gbStartPos(0, 0);
static float    g_gbWidth     = 0.0f;
static float    g_gbMinHeight = 0.0f;
static ImDrawListSplitter g_gbDrawSplitter;
static uint64_t s_lastSyncedDetectedHeroId = 0;
static std::string s_configSaveStatus;
static double s_configSaveStatusUntil = 0.0;

static constexpr float kShellWidth = 720.0f;
static constexpr float kShellBorder = 2.0f;
static constexpr float kHeaderHeight = 84.0f;
static constexpr float kMinShellHeight = 140.0f;
static constexpr float kMaxShellHeight = 1200.0f;
static constexpr float kBodyBottomPadding = 10.0f;
static constexpr float kDefaultLabelWidth = 120.0f;
static constexpr float kAimbotHeroLabelWidth = 98.0f;
static constexpr float kAimbotLeftLabelWidth = 104.0f;
static constexpr float kAimbotRightLabelWidth = 138.0f;
static constexpr float kControlHeight = 22.0f;
static constexpr float kControlRounding = 0.0f;
static constexpr float kGroupRounding = 0.0f;
static constexpr float kGroupContentIndent = 14.0f;
static constexpr float kGroupBorderClipInset = 2.0f;
static constexpr float kControlRightPadding = 10.0f;
static constexpr int kAimingSubTabCount = 4;
static constexpr int kThemeSubTabCount = 2;
static constexpr int kMiscSubTabCount = 6;
static constexpr int kVisualsPageKey = kAimingSubTabCount;
static constexpr int kThemePageKeyBase = kVisualsPageKey + 1;
static constexpr int kMiscPageKeyBase = kThemePageKeyBase + kThemeSubTabCount;
static constexpr int kMeasuredPageCount = kMiscPageKeyBase + kMiscSubTabCount;

static const ImU32 kColShell0       = IM_COL32(0x05, 0x06, 0x09, 0xFF);
static const ImU32 kColShell1       = IM_COL32(0x12, 0x13, 0x17, 0xFF);
static const ImU32 kColShell2       = IM_COL32(0x0b, 0x0c, 0x10, 0xFF);
static const ImU32 kColPanel        = IM_COL32(0x16, 0x17, 0x1c, 0xF8);
static const ImU32 kColPanelSoft    = IM_COL32(0x13, 0x14, 0x19, 0xF0);
static const ImU32 kColControl      = IM_COL32(0x1b, 0x20, 0x24, 0xFF);
static const ImU32 kColControlHover = IM_COL32(0x23, 0x28, 0x2e, 0xFF);
static const ImU32 kColControlHot   = IM_COL32(0x2a, 0x30, 0x37, 0xFF);
static const ImU32 kColStroke       = IM_COL32(0x34, 0x37, 0x40, 0xD0);
static const ImU32 kColStrokeDark   = IM_COL32(0x07, 0x08, 0x0a, 0xF0);
static const ImU32 kColText         = IM_COL32(0xff, 0xff, 0xff, 0xFF);
static const ImU32 kColTextMuted    = IM_COL32(0xe1, 0xe4, 0xea, 0xFF);
static const ImU32 kColTextDim      = IM_COL32(0xa8, 0xae, 0xb8, 0xFF);
static const ImU32 kColAccent       = IM_COL32(0xe4, 0x11, 0x43, 0xFF);
static const ImU32 kColAccentDark   = IM_COL32(0xa9, 0x0a, 0x2e, 0xFF);
static const ImU32 kColAccentSoft   = IM_COL32(0xe4, 0x11, 0x43, 0x58);
static const ImU32 kColAccentGlow   = IM_COL32(0xe4, 0x11, 0x43, 0x28);
static constexpr const char* kNotOpenedText = "\xE6\x9C\xAA\xE6\x89\x93\xE5\xBC\x80";
static constexpr ImWchar kNotOpenedGlyphRanges[] = {
    0x5F00, 0x5F00,
    0x6253, 0x6253,
    0x672A, 0x672A,
    0
};

static ImFont* s_regularFont = nullptr;
static ImFont* s_boldFont = nullptr;
static ImFont* s_titleFont = nullptr;
static ImGuiID s_preNewFrameInitHook = 0;
static ID3D11ShaderResourceView* s_logoTexture = nullptr;
static constexpr int kLogoTextureSize = 32;
static constexpr float kBrandLogoDrawSize = 30.0f;
static constexpr float kBrandTitleGap = 12.0f;
static float s_measuredBodyHeightByPage[kMeasuredPageCount] = {};
static UI::MenuClientSize s_desiredMenuClientSize{ kShellWidth, 550.0f };

// Forward declarations
static void CloseGroupBox();
static void SettingRow(const char* label, float labelWidthPx = kDefaultLabelWidth);

static void InitStyleBeforeNewFrame(ImGuiContext*, ImGuiContextHook*) {
    if (!UI::state.initialized)
        UI::InitStyle();
}

static void EnsurePreNewFrameInitHook() {
    if (s_preNewFrameInitHook != 0)
        return;

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return;

    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = InitStyleBeforeNewFrame;
    s_preNewFrameInitHook = ImGui::AddContextHook(context, &hook);
}

static float MaxFloat(float a, float b) {
    return (a > b) ? a : b;
}

static float MinFloat(float a, float b) {
    return (a < b) ? a : b;
}

static ImU32 MixColor(ImU32 from, ImU32 to, float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    ));
}

static float VisualTransition(ImGuiID id, bool enabled, float speed = 16.0f) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float current = storage->GetFloat(id, enabled ? 1.0f : 0.0f);
    float target = enabled ? 1.0f : 0.0f;
    float step = ImClamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f);
    current += (target - current) * step;
    storage->SetFloat(id, current);
    return current;
}

static int FindMenuToggleKeyIndex(int vk) {
    for (int i = 0; i < IM_ARRAYSIZE(kMenuToggleVk); ++i) {
        if (kMenuToggleVk[i] == vk)
            return i;
    }
    return 0;
}

static int ClampHeroSelectionIndex(int index) {
    return ImClamp(index, 0, IM_ARRAYSIZE(kHeroOptions) - 1);
}

static const HeroOption& CurrentHeroOption() {
    UI::state.selectedTypeIndex = ClampHeroSelectionIndex(UI::state.selectedTypeIndex);
    return kHeroOptions[UI::state.selectedTypeIndex];
}

static int FindHeroOptionIndexById(uint64_t heroId) {
    if (heroId == 0)
        return 0;

    for (int i = 1; i < IM_ARRAYSIZE(kHeroOptions); ++i) {
        if (kHeroOptions[i].heroId == heroId)
            return i;
    }
    return -1;
}

static const HeroOption* FindHeroOptionById(uint64_t heroId) {
    const int index = FindHeroOptionIndexById(heroId);
    return index >= 0 ? &kHeroOptions[index] : nullptr;
}

static ID3D11ShaderResourceView* HeroAvatarForName(const char* label) {
    if (!label || label[0] == '\0')
        return nullptr;

    IconManager* icons = Render::GetIconManager();
    if (!icons)
        return nullptr;

    const std::string slug = OW::HeroDisplayNameToSlug(label);
    if (slug.empty() || slug == "all")
        return nullptr;

    if (ID3D11ShaderResourceView* avatar = icons->GetIcon(slug))
        return avatar;
    return icons->LoadHeroAvatar(slug);
}

static ID3D11ShaderResourceView* HeroAvatarForOption(const HeroOption& hero) {
    if (hero.heroId == 0)
        return nullptr;
    return HeroAvatarForName(hero.label);
}

static ID3D11ShaderResourceView* HeroSkillIconForDefinition(const OW::HeroSkillDefinition& definition) {
    IconManager* icons = Render::GetIconManager();
    if (!icons || !definition.heroSlug || !definition.iconSlug)
        return nullptr;

    const std::string key = std::string(definition.heroSlug) + "/" + definition.iconSlug;
    if (ID3D11ShaderResourceView* icon = icons->GetIcon(key))
        return icon;

    icons->LoadAbilityIcons(definition.heroSlug, { definition.iconSlug });
    return icons->GetIcon(key);
}

static std::string HeroDisplayNameForId(uint64_t heroId) {
    if (heroId == 0)
        return "Not detected";

    if (const HeroOption* option = FindHeroOptionById(heroId))
        return option->label;

    const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
    std::string name = OW::GetHeroEngNames(heroId, localSnapshot.LinkBase);
    if (name.empty() || name == "Unknown") {
        char unknown[64] = {};
        std::snprintf(unknown, sizeof(unknown), "Unknown (0x%llX)",
                      static_cast<unsigned long long>(heroId));
        return unknown;
    }
    return name;
}

static std::string SaveSelectedConfig() {
    const HeroOption& selectedHero = CurrentHeroOption();
    const std::string path = OW::Config::ConfigPath();
    const OW::c_entity localSnapshot = OW::SnapshotLocalEntity();
    OW::Config::NormalizeHeroPresets();

    if (IsConcreteHeroSelection(selectedHero)) {
        const std::string heroPath = OW::Config::HeroConfigPath(path);
        OW::Config::SaveConfig(path);
        OW::Config::SaveConfigForHero(path, selectedHero.heroId, localSnapshot.LinkBase);
        std::string status = "Saved ";
        status += selectedHero.label;
        status += " config";
        Diagnostics::Info("%s to %s.", status.c_str(), heroPath.c_str());
        return status;
    }

    OW::Config::SaveConfig(path);
    OW::Config::SaveHeroConfig(path);
    const uint64_t savedHeroId = OW::Config::lastheroid > 0
        ? static_cast<uint64_t>(OW::Config::lastheroid)
        : localSnapshot.HeroID;
    const std::string savedName = savedHeroId != 0
        ? HeroDisplayNameForId(savedHeroId)
        : std::string("global");
    std::string status = "Saved ";
    status += savedName;
    status += " config";
    Diagnostics::Info("%s to %s.", status.c_str(), path.c_str());
    return status;
}

static uint64_t DetectedLocalHeroId() {
    const Diagnostics::StatusSnapshot snapshot = Diagnostics::Snapshot();
    return snapshot.localEntity.selectedHeroId;
}

static void ApplySelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return;

    const int aimSlotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    if (IsHeroActionSlotEnabled(ActionSlotKind::Aim, hero.heroId, aimSlotIndex)) {
        const OW::Config::HeroPreset preset =
            GetHeroActionPresetOrDefault(ActionSlotKind::Aim, hero.heroId, aimSlotIndex);
        ApplyHeroActionPresetToGlobals(ActionSlotKind::Aim, preset);
    }

    const int triggerSlotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Trigger);
    if (IsHeroActionSlotEnabled(ActionSlotKind::Trigger, hero.heroId, triggerSlotIndex)) {
        const OW::Config::HeroPreset preset =
            GetHeroActionPresetOrDefault(ActionSlotKind::Trigger, hero.heroId, triggerSlotIndex);
        ApplyHeroActionPresetToGlobals(ActionSlotKind::Trigger, preset);
    }
}

static bool SelectTypeIndex(int index) {
    index = ClampHeroSelectionIndex(index);
    if (UI::state.selectedTypeIndex == index)
        return false;

    UI::state.selectedTypeIndex = index;
    ApplySelectedTypePreset();
    return true;
}

static void SyncSelectedTypeWithDetectedHero() {
    const uint64_t detectedHeroId = DetectedLocalHeroId();
    if (detectedHeroId == 0) {
        s_lastSyncedDetectedHeroId = 0;
        return;
    }

    if (detectedHeroId == s_lastSyncedDetectedHeroId)
        return;

    s_lastSyncedDetectedHeroId = detectedHeroId;
    const int detectedIndex = FindHeroOptionIndexById(detectedHeroId);
    if (detectedIndex > 0)
        SelectTypeIndex(detectedIndex);
}

static bool SaveSelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return false;

    const int slotIndex = ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    const OW::Config::HeroPreset preset = MakeCurrentHeroActionPreset(ActionSlotKind::Aim);
    SetHeroActionPreset(ActionSlotKind::Aim, hero.heroId, slotIndex, preset);
    OW::Config::SaveHeroConfigForHero(OW::Config::ConfigPath(), hero.heroId);
    return true;
}

static bool LoadSelectedTypePreset() {
    const HeroOption& hero = CurrentHeroOption();
    if (hero.heroId == 0)
        return false;

    ActiveHeroPresetSlotIndex(ActionSlotKind::Aim);
    ActiveHeroPresetSlotIndex(ActionSlotKind::Trigger);
    OW::Config::LoadHeroConfig(OW::Config::ConfigPath());
    ApplySelectedTypePreset();
    return true;
}

static int BonePreferenceIndexFromPreset(const OW::Config::HeroPreset& preset) {
    if (preset.autoBone)
        return kBonePreferenceClosestIndex;

    const int normalizedAimBone = OW::Config::NormalizeAimBone(preset.bone);
    for (int i = 0; i < IM_ARRAYSIZE(kBonePreferenceAimBones); ++i) {
        if (kBonePreferenceAimBones[i] == normalizedAimBone)
            return i;
    }
    return 0;
}

static void ApplyBonePreferenceToPreset(int preferenceIndex, OW::Config::HeroPreset& preset) {
    if (preferenceIndex == kBonePreferenceClosestIndex) {
        preset.autoBone = true;
        preset.bone = OW::Config::NormalizeAimBone(preset.bone);
        return;
    }

    preferenceIndex = ImClamp(preferenceIndex, 0, IM_ARRAYSIZE(kBonePreferenceAimBones) - 1);
    preset.autoBone = false;
    preset.bone = kBonePreferenceAimBones[preferenceIndex];
}

static const char* PresetBoneName(const OW::Config::HeroPreset& preset) {
    return preset.autoBone ? "Closest" : OW::Config::AimBoneName(preset.bone);
}

static const char* PresetAimBehaviorName(int behavior) {
    behavior = ImClamp(behavior, 0, IM_ARRAYSIZE(kAimBehavior) - 1);
    return kAimBehavior[behavior];
}

static void DrawPresetSummary(const HeroOption& hero,
                              const OW::Config::HeroPreset& preset,
                              bool hasStoredPreset,
                              ActionSlotKind kind) {
    char summary[320] = {};
    const char* scope = hero.heroId == 0
        ? "Global defaults"
        : (hasStoredPreset ? "Stored preset" : "Using global defaults");
    if (kind == ActionSlotKind::Aim) {
        std::snprintf(summary, sizeof(summary),
                      "%s - %s | %s | Speed %.1f%% | FOV %.0f deg | %s | Hitbox %.0f%%",
                      hero.label, scope, PresetAimBehaviorName(preset.aimBehavior),
                      preset.smooth, preset.fov,
                      PresetBoneName(preset), preset.hitbox);
    } else {
        std::snprintf(summary, sizeof(summary),
                      "%s - %s | Trigger %s | %s | %s | Hitbox %.0f%% | %s",
                      hero.label, scope,
                      preset.trigger.enabled ? "On" : "Off",
                      OW::Labels::AttackActionName(preset.trigger.action),
                      OW::Labels::TriggerbotModeName(preset.trigger.mode),
                      preset.hitbox,
                      OW::Labels::AimModeName(preset.aimMode));
    }
    ImGui::TextUnformatted(summary);
}

static bool TypeSelectorButton(const HeroOption& hero, const ImVec2& size) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
    const ImGuiID id = window->GetID("##globalTypeSelector");

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float hoverT = VisualTransition(id ^ 0x61ad, hovered || held, 18.0f);

    const ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, frameCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);

    const float iconSize = 20.0f;
    const ImVec2 iconMin(bb.Min.x + 5.0f, bb.Min.y + (size.y - iconSize) * 0.5f);
    const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
    if (ID3D11ShaderResourceView* avatar = HeroAvatarForOption(hero)) {
        window->DrawList->AddImage(reinterpret_cast<ImTextureID>(avatar), iconMin, iconMax);
    } else {
        window->DrawList->AddRectFilled(iconMin, iconMax, kColPanelSoft, 0.0f);
        window->DrawList->AddRect(iconMin, iconMax, kColStroke, 0.0f);
        const char* fallback = hero.heroId == 0 ? "All" : "?";
        const ImVec2 fallbackSize = ImGui::CalcTextSize(fallback);
        window->DrawList->AddText(ImVec2(iconMin.x + (iconSize - fallbackSize.x) * 0.5f,
                                         iconMin.y + (iconSize - fallbackSize.y) * 0.5f),
                                  kColTextMuted, fallback);
    }

    const ImVec2 textPos(iconMax.x + 7.0f,
                         bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
    window->DrawList->AddText(textPos, kColText, hero.label);

    const ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + size.y * 0.5f);
    const ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);

    return pressed;
}

static void DrawDetectedTypeReadout() {
    const uint64_t heroId = DetectedLocalHeroId();
    const bool detected = heroId != 0;
    const std::string displayName = HeroDisplayNameForId(heroId);
    const HeroOption* option = detected ? FindHeroOptionById(heroId) : nullptr;
    ID3D11ShaderResourceView* avatar = option ? HeroAvatarForOption(*option) : HeroAvatarForName(displayName.c_str());

    if (avatar) {
        constexpr float kDetectedAvatarSize = 28.0f;
        ImGui::Image(reinterpret_cast<ImTextureID>(avatar),
                     ImVec2(kDetectedAvatarSize, kDetectedAvatarSize));
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    }

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(detected ? kColText : kColTextDim),
                       "%s", displayName.c_str());
}

static bool TypePickerPanel() {
    ImGui::SetNextWindowSize(ImVec2(520.0f, 430.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopup("TypePickerPopup"))
        return false;

    bool changed = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kColPanelSoft);

    const char* categories[] = { "Global", "Tank", "Damage", "Support" };
    constexpr float itemW = 76.0f;
    constexpr float itemH = 76.0f;
    constexpr float iconSize = 42.0f;

    if (ImGui::BeginChild("##typePickerScroll", ImVec2(0.0f, 0.0f), true)) {
        const float availW = ImGui::GetContentRegionAvail().x;
        const int columns = ImClamp(static_cast<int>(availW / (itemW + 8.0f)), 1, 8);

        for (const char* category : categories) {
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s", category);

            int column = 0;
            for (int i = 0; i < IM_ARRAYSIZE(kHeroOptions); ++i) {
                const HeroOption& hero = kHeroOptions[i];
                if (std::strcmp(hero.category, category) != 0)
                    continue;

                if (column > 0)
                    ImGui::SameLine();

                ImGui::PushID(i);
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const bool selected = UI::state.selectedTypeIndex == i;
                ImGui::InvisibleButton("##typeTile", ImVec2(itemW, itemH));
                const bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) {
                    changed |= SelectTypeIndex(i);
                    ImGui::CloseCurrentPopup();
                }

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const ImU32 fill = selected
                    ? IM_COL32(0x25, 0x18, 0x20, 0xFF)
                    : (hovered ? kColControlHover : kColControl);
                const ImU32 border = selected ? kColAccent : (hovered ? kColStroke : kColStrokeDark);
                drawList->AddRectFilled(pos, ImVec2(pos.x + itemW, pos.y + itemH), fill, 0.0f);
                drawList->AddRect(pos, ImVec2(pos.x + itemW, pos.y + itemH), border, 0.0f, 0, selected ? 2.0f : 1.0f);

                const ImVec2 iconMin(pos.x + (itemW - iconSize) * 0.5f, pos.y + 7.0f);
                const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                if (ID3D11ShaderResourceView* avatar = HeroAvatarForOption(hero)) {
                    drawList->AddImage(reinterpret_cast<ImTextureID>(avatar), iconMin, iconMax);
                } else {
                    drawList->AddRectFilled(iconMin, iconMax, kColPanelSoft, 0.0f);
                    drawList->AddRect(iconMin, iconMax, kColStroke, 0.0f);
                    const char* fallback = hero.heroId == 0 ? "All" : "?";
                    const ImVec2 fallbackSize = ImGui::CalcTextSize(fallback);
                    drawList->AddText(ImVec2(iconMin.x + (iconSize - fallbackSize.x) * 0.5f,
                                             iconMin.y + (iconSize - fallbackSize.y) * 0.5f),
                                      kColTextMuted, fallback);
                }

                const ImVec2 textSize = ImGui::CalcTextSize(hero.label);
                const float textX = pos.x + (itemW - textSize.x) * 0.5f;
                drawList->PushClipRect(ImVec2(pos.x + 4.0f, pos.y + 54.0f),
                                       ImVec2(pos.x + itemW - 4.0f, pos.y + itemH - 4.0f),
                                       true);
                drawList->AddText(ImVec2(MaxFloat(pos.x + 4.0f, textX), pos.y + 55.0f),
                                  selected ? kColText : kColTextMuted,
                                  hero.label);
                drawList->PopClipRect();
                ImGui::PopID();

                ++column;
                if (column >= columns) {
                    column = 0;
                }
            }

            ImGui::Dummy(ImVec2(0.0f, 4.0f));
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::EndPopup();
    return changed;
}

static bool CreateTextureFromIconResource(ID3D11Device* device, int resourceId, int size,
                                          ID3D11ShaderResourceView** outTexture) {
    if (!device || !outTexture || size <= 0)
        return false;

    *outTexture = nullptr;

    HICON icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resourceId),
                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR)
    );
    if (!icon)
        return false;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    void* bgraBits = nullptr;
    HBITMAP bitmap = screenDc
        ? CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bgraBits, nullptr, 0)
        : nullptr;
    if (screenDc)
        ReleaseDC(nullptr, screenDc);

    if (!memoryDc || !bitmap || !bgraBits) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memoryDc)
            DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(size) * static_cast<size_t>(size) * 4;
    std::memset(bgraBits, 0, pixelBytes);

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    const BOOL drewIcon = DrawIconEx(memoryDc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    if (oldBitmap)
        SelectObject(memoryDc, oldBitmap);

    if (!drewIcon) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    std::vector<std::uint8_t> rgba(pixelBytes);
    const auto* bgra = static_cast<const std::uint8_t*>(bgraBits);
    bool hasAlpha = false;
    for (int i = 0; i < size * size; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = bgra[i * 4 + 3];
        hasAlpha = hasAlpha || rgba[i * 4 + 3] != 0;
    }

    if (!hasAlpha) {
        for (int i = 0; i < size * size; ++i) {
            if (rgba[i * 4 + 0] || rgba[i * 4 + 1] || rgba[i * 4 + 2])
                rgba[i * 4 + 3] = 0xFF;
        }
    }

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    DestroyIcon(icon);

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(size);
    textureDesc.Height = static_cast<UINT>(size);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = static_cast<UINT>(size * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&textureDesc, &initData, &texture);
    if (FAILED(hr) || !texture)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();

    if (FAILED(hr) || !srv)
        return false;

    *outTexture = srv;
    return true;
}

static void DrawText(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
                     ImU32 color, const char* text) {
    if (font)
        ImGui::PushFont(font);
    drawList->AddText(pos, color, text);
    if (font)
        ImGui::PopFont();
}

// =====================================================================
// FORMAT SLIDER VALUE  (mirrors React formatSliderText)
// =====================================================================
static const char* FormatSliderValue(char* buf, size_t size, float value, const char* text) {
    if (!text || text[0] == '\0') {
        std::snprintf(buf, size, "%.0f %%", value);
        return buf;
    }

    // Keyword-based formats
    if (std::strcmp(text, "Max") == 0) {
        if (value >= 99.5f) return "Max";
        std::snprintf(buf, size, "%.0f %%", value);
        return buf;
    }
    if (std::strcmp(text, "Endless") == 0) {
        if (value >= 99.5f) return "Endless";
        std::snprintf(buf, size, "%.0f ms", value * 20.0f);
        return buf;
    }
    if (std::strcmp(text, "Instant") == 0) {
        if (value <= 0.5f) return "Instant";
        std::snprintf(buf, size, "%.0f ms", value * 3.0f);
        return buf;
    }
    if (std::strcmp(text, "Unlimited") == 0) {
        if (value >= 99.5f) return "Unlimited";
        std::snprintf(buf, size, "%.0f m", value);
        return buf;
    }
    // Pattern-based formats (encoded as "base-value unit")
    if (std::strcmp(text, "50.00 %") == 0 || std::strcmp(text, "50.00 %%") == 0) {
        std::snprintf(buf, size, "%.2f %%", value);
        return buf;
    }
    if (std::strcmp(text, "10.00 deg") == 0) {
        std::snprintf(buf, size, "%.2f deg", value);
        return buf;
    }
    if (std::strcmp(text, "200 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 10.0f);
        return buf;
    }
    if (std::strcmp(text, "200 cm") == 0) {
        std::snprintf(buf, size, "%.0f cm", value * 6.25f);
        return buf;
    }
    if (std::strcmp(text, "1000 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 41.6667f);
        return buf;
    }
    if (std::strcmp(text, "180 deg") == 0) {
        std::snprintf(buf, size, "%.0f deg", value * 1.8f);
        return buf;
    }
    if (std::strcmp(text, "90 deg/s") == 0) {
        std::snprintf(buf, size, "%.0f deg/s", value * 45.0f);
        return buf;
    }
    if (std::strcmp(text, "7.5 m/s") == 0) {
        std::snprintf(buf, size, "%.1f m/s", value * 3.75f);
        return buf;
    }
    if (std::strcmp(text, "1.0 m/s^2") == 0) {
        std::snprintf(buf, size, "%.1f m/s^2", value * 0.125f);
        return buf;
    }
    if (std::strcmp(text, "35 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 17.5f);
        return buf;
    }
    if (std::strcmp(text, "135 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 15.0f);
        return buf;
    }
    if (std::strcmp(text, "500 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 5.0f);
        return buf;
    }

    // Catch-all mirrors React's /^(-?\d+(?:\.\d+)?)(.*)$/ fallback.
    const char* p = text;
    if (*p == '-')
        ++p;

    bool hasDigit = false;
    int decimals = 0;
    while (*p >= '0' && *p <= '9') {
        hasDigit = true;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            hasDigit = true;
            ++decimals;
            ++p;
        }
    }

    if (hasDigit) {
        std::snprintf(buf, size, "%.*f%s", decimals, value, p);
        return buf;
    }

    return text;
}

static int SliderDecimalPlacesFromText(const char* text) {
    if (!text)
        return -1;

    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9')
            continue;

        while (*p >= '0' && *p <= '9')
            ++p;

        if (*p != '.')
            return 0;

        int decimals = 0;
        ++p;
        while (*p >= '0' && *p <= '9') {
            ++decimals;
            ++p;
        }
        return decimals;
    }

    return -1;
}

static float StepFromDecimalPlaces(int decimals) {
    decimals = ImClamp(decimals, 0, 4);

    float step = 1.0f;
    for (int i = 0; i < decimals; ++i)
        step *= 0.1f;
    return step;
}

static float SliderKeyboardSlowStep(float v_min, float v_max, const char* formatText,
                                    bool integralValue) {
    if (integralValue)
        return 1.0f;

    const int decimals = SliderDecimalPlacesFromText(formatText);
    if (decimals >= 0)
        return StepFromDecimalPlaces(decimals);

    const float range = std::fabs(v_max - v_min);
    if (range <= 1.0f)
        return 0.01f;
    if (range <= 10.0f)
        return 0.1f;
    return 1.0f;
}

static float RoundToSliderStep(float value, float step) {
    if (step <= 0.0f)
        return value;
    return std::round(value / step) * step;
}

// =====================================================================
// UIGroupBox  --  Opens a group box.  Closes any previously open group
//                 box (drawing its border lazily via CloseGroupBox).
// =====================================================================
static bool UIGroupBox(const char* title, float minHeightPx = 0.0f) {
    // Close previous group box first (draws its border)
    if (g_gbOpen)
        CloseGroupBox();

    g_gbOpen = true;
    g_gbMinHeight = minHeightPx;
    std::strncpy(g_gbTitle, title, sizeof(g_gbTitle) - 1);
    g_gbTitle[sizeof(g_gbTitle) - 1] = '\0';

    if (s_boldFont)
        ImGui::PushFont(s_boldFont);
    ImVec2 textSize = ImGui::CalcTextSize(title);
    if (s_boldFont)
        ImGui::PopFont();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    g_gbDrawSplitter.Split(dl, 2);
    g_gbDrawSplitter.SetCurrentChannel(dl, 1);

    ImGui::Dummy(ImVec2(0.0f, textSize.y + 10.0f));

    // Save state for lazy border drawing
    g_gbStartPos = ImGui::GetCursorScreenPos();
    g_gbWidth    = ImGui::GetContentRegionAvail().x;

    ImGui::Indent(kGroupContentIndent);
    ImGui::BeginGroup();
    return true;
}

// =====================================================================
// CloseGroupBox  --  Draws the border and legend of the current group
//                    box and restores indentation.
// =====================================================================
static void CloseGroupBox() {
    if (!g_gbOpen) return;

    ImVec2 textSize = s_boldFont ? ImGui::CalcTextSize(g_gbTitle) : ImGui::CalcTextSize(g_gbTitle);
    float  titleH   = textSize.y + 10.0f;
    float  borderTopY = g_gbStartPos.y - titleH;

    if (g_gbMinHeight > 0.0f) {
        float desiredContentEndY = borderTopY + g_gbMinHeight - 6.0f;
        float currentY = ImGui::GetCursorScreenPos().y;
        if (currentY < desiredContentEndY)
            ImGui::Dummy(ImVec2(0.0f, desiredContentEndY - currentY));
    }

    ImGui::EndGroup();   // tracks the content bounds
    ImGui::Unindent(kGroupContentIndent);

    ImVec2 contentEnd = ImGui::GetItemRectMax();
    auto*  dl = ImGui::GetWindowDrawList();

    ImVec2 borderMin = g_gbStartPos;
    borderMin.y       = borderTopY;
    ImVec2 borderMax  = ImVec2(
        MaxFloat(borderMin.x + 1.0f, g_gbStartPos.x + g_gbWidth - kGroupBorderClipInset),
        contentEnd.y + 9.0f);
    if (g_gbMinHeight > 0.0f)
        borderMax.y = MaxFloat(borderMax.y, borderMin.y + g_gbMinHeight);

    g_gbDrawSplitter.SetCurrentChannel(dl, 0);
    dl->AddRectFilled(ImVec2(borderMin.x + 1.0f, borderMin.y + 2.0f),
                      ImVec2(borderMax.x + 1.0f, borderMax.y + 2.0f),
                      IM_COL32(0x00, 0x00, 0x00, 0x38), kGroupRounding);
    dl->AddRectFilled(borderMin, borderMax, kColPanelSoft, kGroupRounding);
    dl->AddRectFilledMultiColor(borderMin, ImVec2(borderMax.x, borderMin.y + 26.0f),
                                IM_COL32(0x1a, 0x1b, 0x20, 0x78),
                                IM_COL32(0x18, 0x19, 0x1e, 0x42),
                                IM_COL32(0x12, 0x13, 0x17, 0x00),
                                IM_COL32(0x12, 0x13, 0x17, 0x00));
    dl->AddRect(borderMin, borderMax, kColStroke, kGroupRounding, 0, 1.0f);
    dl->AddRect(ImVec2(borderMin.x + 1.0f, borderMin.y + 1.0f),
                ImVec2(borderMax.x - 1.0f, borderMax.y - 1.0f),
                kColStrokeDark, kGroupRounding, 0, 1.0f);
    dl->AddLine(ImVec2(borderMin.x + 10.0f, borderMin.y + 1.0f),
                ImVec2(borderMax.x - 10.0f, borderMin.y + 1.0f),
                IM_COL32(0xe4, 0x11, 0x43, 0x42), 1.0f);

    dl->AddRectFilled(
        ImVec2(borderMin.x + 13.0f, borderMin.y - 1.0f),
        ImVec2(borderMin.x + 18.0f + textSize.x + 12.0f, borderMin.y + textSize.y + 4.0f),
        kColPanel
    );

    g_gbDrawSplitter.SetCurrentChannel(dl, 1);
    DrawText(dl, s_boldFont, ImVec2(borderMin.x + 17.0f, borderMin.y + 2.0f),
             kColText, g_gbTitle);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    if (cursor.y < borderMax.y + 7.0f)
        ImGui::Dummy(ImVec2(0.0f, borderMax.y + 7.0f - cursor.y));

    g_gbDrawSplitter.Merge(dl);
    g_gbOpen = false;
    g_gbMinHeight = 0.0f;
}

// =====================================================================
// UICheckbox  --  compact custom checkbox with premium dark-state styling.
// =====================================================================
static bool UICheckbox(const char* label, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g    = *GImGui;
    const ImGuiID id   = window->GetID(label);
    const float sz     = 12.0f;
    const float height = kControlHeight;
    const float spacing = g.Style.ItemInnerSpacing.x + 2.0f;

    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const bool hasVisibleLabel = labelEnd > label;
    const ImVec2 labelSize = hasVisibleLabel
        ? ImGui::CalcTextSize(label, labelEnd, false)
        : ImVec2(0.0f, 0.0f);
    ImVec2 pos = window->DC.CursorPos;

    ImRect bb(pos, ImVec2(pos.x + sz + (hasVisibleLabel ? spacing + labelSize.x : 0.0f), pos.y + height));

    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *value = !*value;

    float hoverT = VisualTransition(id ^ 0x23a7, hovered || held, 18.0f);
    float checkedT = VisualTransition(id ^ 0x51d3, *value, 18.0f);
    ImVec2 boxMin(bb.Min.x, bb.Min.y + (height - sz) * 0.5f);
    ImVec2 boxMax(boxMin.x + sz, boxMin.y + sz);
    ImU32 baseCol = MixColor(kColControl, kColControlHover, hoverT);
    ImU32 squareCol = MixColor(baseCol, kColAccent, checkedT);
    float rounding = 0.0f;

    if (hovered || held) {
        window->DrawList->AddRectFilled(ImVec2(boxMin.x - 2.0f, boxMin.y - 2.0f),
                                        ImVec2(boxMax.x + 2.0f, boxMax.y + 2.0f),
                                        MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00), kColAccentGlow, hoverT),
                                        rounding + 2.0f);
    }
    window->DrawList->AddRectFilled(boxMin, boxMax, squareCol, rounding);
    window->DrawList->AddRect(boxMin, boxMax,
                              MixColor(kColStroke, IM_COL32(0xf0, 0x34, 0x5d, 0xD0), checkedT),
                              rounding, 0, 1.0f);

    if (checkedT > 0.18f) {
        ImU32 tickCol = MixColor(IM_COL32(0xff, 0xff, 0xff, 0x00),
                                 IM_COL32(0xff, 0xff, 0xff, 0xF4), checkedT);
        window->DrawList->AddLine(ImVec2(boxMin.x + 3.0f, boxMin.y + 6.0f),
                                  ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  tickCol, 1.4f);
        window->DrawList->AddLine(ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  ImVec2(boxMin.x + 9.1f, boxMin.y + 3.8f),
                                  tickCol, 1.4f);
    }

    if (hasVisibleLabel) {
        ImVec2 labelPos(bb.Min.x + sz + spacing, bb.Min.y + (height - labelSize.y) * 0.5f);
        ImU32 labelCol = MixColor(kColTextMuted, kColText, MaxFloat(hoverT, checkedT * 0.5f));
        window->DrawList->AddText(labelPos, labelCol, label, labelEnd);
    }

    return pressed;
}

// =====================================================================
// UISlider  --  Custom dark track with accent fill and value text.
// =====================================================================
static void PushControlWidth(float rightPaddingPx = kControlRightPadding) {
    const float width = MaxFloat(1.0f, ImGui::GetContentRegionAvail().x - rightPaddingPx);
    ImGui::PushItemWidth(width);
}

static bool UISlider(const char* label, float* value, float v_min, float v_max,
                     const char* formatText, bool integralValue = false) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id   = window->GetID(label);
    const float height = kControlHeight;

    ImVec2 pos  = window->DC.CursorPos;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_Inputable)) return false;

    // Normalise value to [0,1]
    float range  = (v_max - v_min);
    float v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    // Interaction
    ImGuiContext& g = *GImGui;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held,
                                         ImGuiButtonFlags_MouseButtonLeft |
                                         ImGuiButtonFlags_PressedOnClick);
    bool valueChanged = false;

    auto assignValue = [&](float next) {
        next = ImClamp(next, v_min, v_max);
        if (integralValue)
            next = static_cast<float>(ImClamp(static_cast<int>(std::lround(next)),
                                              static_cast<int>(std::lround(v_min)),
                                              static_cast<int>(std::lround(v_max))));
        if (std::fabs(*value - next) <= 0.000001f)
            return;

        *value = next;
        valueChanged = true;
    };

    auto setFromNorm = [&](float normalized) {
        normalized = ImClamp(normalized, 0.0f, 1.0f);
        assignValue(v_min + normalized * range);
    };

    const bool mousePressed = pressed && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool mouseDragging = held && g.ActiveId == id && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (mousePressed) {
        ImGui::SetActiveID(id, window);
        ImGui::SetFocusID(id, window);
        ImGui::FocusWindow(window);
    }

    if (mousePressed || mouseDragging) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        setFromNorm((mousePos.x - bb.Min.x) / (bb.Max.x - bb.Min.x));
    }

    const bool keyboardTarget = (ImGui::IsItemFocused() || hovered || g.ActiveId == id) &&
                                ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (keyboardTarget && !ImGui::GetIO().WantTextInput) {
        ImGui::SetKeyOwner(ImGuiKey_LeftArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_RightArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_UpArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_DownArrow, id);
        ImGui::SetKeyOwner(ImGuiKey_Home, id);
        ImGui::SetKeyOwner(ImGuiKey_End, id);

        const float slowStep = SliderKeyboardSlowStep(v_min, v_max, formatText, integralValue);
        const float fastStep = slowStep * 10.0f;
        auto nudgeValue = [&](float delta) {
            const float next = ImClamp(*value + delta, v_min, v_max);
            assignValue(RoundToSliderStep(next, slowStep));
        };

        const ImGuiInputFlags repeat = ImGuiInputFlags_Repeat;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, repeat, id)) {
            nudgeValue(slowStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, repeat, id)) {
            nudgeValue(-slowStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, repeat, id)) {
            nudgeValue(fastStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, repeat, id)) {
            nudgeValue(-fastStep);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Home, repeat, id)) {
            assignValue(v_min);
        } else if (ImGui::IsKeyPressed(ImGuiKey_End, repeat, id)) {
            assignValue(v_max);
        }
    }

    v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    float hoverT = VisualTransition(id ^ 0x6f47, hovered || held || ImGui::IsItemFocused(), 18.0f);
    ImU32 trackCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x28), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f),
                                    bb.Max, trackCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    window->DrawList->AddLine(ImVec2(bb.Min.x + 5.0f, bb.Min.y + 1.0f),
                              ImVec2(bb.Max.x - 5.0f, bb.Min.y + 1.0f),
                              IM_COL32(0xff, 0xff, 0xff, 0x12), 1.0f);

    float fillW = v_norm * (bb.Max.x - bb.Min.x);
    if (fillW > 0.0f) {
        ImVec2 fillMax(bb.Min.x + fillW, bb.Max.y);
        window->DrawList->AddRectFilledMultiColor(
            bb.Min, fillMax,
            kColAccent, IM_COL32(0xf0, 0x1a, 0x49, 0xFF),
            kColAccentDark, kColAccent);
        window->DrawList->AddRectFilled(bb.Min, fillMax,
                                        IM_COL32(0xff, 0xff, 0xff, 0x08),
                                        kControlRounding);
    }

    // Value text
    char tmp[64];
    const char* displayText = FormatSliderValue(tmp, sizeof(tmp), *value, formatText);
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(bb.Max.x - textSize.x - 8.0f, bb.Min.y + (height - textSize.y) * 0.5f);
    window->DrawList->AddText(
        ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
        IM_COL32(0x00, 0x00, 0x00, 0x80), displayText);
    window->DrawList->AddText(textPos, kColText, displayText);

    if (valueChanged)
        ImGui::MarkItemEdited(id);

    return valueChanged;
}

static bool UISlider(const char* label, int* value, float v_min, float v_max,
                     const char* formatText) {
    const int minValue = static_cast<int>(std::lround(v_min));
    const int maxValue = static_cast<int>(std::lround(v_max));
    const int clampedValue = ImClamp(*value, minValue, maxValue);
    float sliderValue = static_cast<float>(clampedValue);

    const bool sliderChanged = UISlider(label, &sliderValue, v_min, v_max, formatText, true);
    const int roundedValue = ImClamp(static_cast<int>(std::lround(sliderValue)), minValue, maxValue);
    if (roundedValue != *value) {
        *value = roundedValue;
        return true;
    }
    return sliderChanged;
}

// =====================================================================
// UISelect  --  Custom-styled dropdown using ImGui BeginCombo.
//               Dropdown items get a red #e41143 background on hover/active.
// =====================================================================
static bool UISelect(const char* label, int* current, const char* const items[], int itemCount) {
    if (*current < 0 || *current >= itemCount)
        *current = 0;

    const char* preview = items[*current];
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    const float height = kControlHeight;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    bool preHovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    float hoverT = VisualTransition(window->GetID(label) ^ 0x3911, preHovered, 18.0f);
    ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);

    ImGui::PushStyleColor(ImGuiCol_Header,        kColAccentDark);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kColAccent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  kColAccent);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       kColPanel);
    ImGui::PushStyleColor(ImGuiCol_Border,        kColStroke);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       frameCol);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MixColor(kColControlHover, kColControlHot, hoverT));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, MaxFloat(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kControlRounding);

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview,
                          ImGuiComboFlags_HeightLargest | ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < itemCount; i++) {
            const bool isSelected = (*current == i);
            if (ImGui::Selectable(items[i], isSelected)) {
                *current = i;
                changed = true;
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);

    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + height * 0.5f);
    ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);
    return changed;
}

static void UIDisabledSelect(const char* label, const char* previewText = kNotOpenedText) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImGuiID id = window->GetID(label);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    float width = ImGui::CalcItemWidth();
    if (width <= 0.0f || width > availableWidth)
        width = availableWidth;
    width = MaxFloat(1.0f, width);
    const float height = kControlHeight;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id))
        return;

    const ImU32 frameCol = MixColor(kColPanelSoft, kColControl, 0.45f);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, 0.25f),
                              kControlRounding, 0, 1.0f);

    const ImVec2 textSize = ImGui::CalcTextSize(previewText);
    const ImVec2 textPos(bb.Min.x + 8.0f, bb.Min.y + (height - textSize.y) * 0.5f);
    ImGui::PushClipRect(bb.Min, bb.Max, true);
    window->DrawList->AddText(textPos, kColTextDim, previewText);
    ImGui::PopClipRect();

    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", previewText);
}

static std::vector<int> AttackActionsForHero(uint64_t heroId) {
    std::vector<int> actions;

    auto addAction = [&](int action) {
        if (action < 0 || action >= OW::Labels::AttackActionCount())
            return;
        if (std::find(actions.begin(), actions.end(), action) == actions.end())
            actions.push_back(action);
    };

    if (heroId != 0) {
        for (const OW::WeaponSpec* spec = OW::WeaponSpecsBegin(); spec != OW::WeaponSpecsEnd(); ++spec) {
            if (spec->heroId == heroId)
                addAction(spec->action);
        }
    }

    if (actions.empty()) {
        const int actionCount = OW::Labels::AttackActionCount();
        for (int action = 0; action < actionCount; ++action)
            actions.push_back(action);
    }

    return actions;
}

static bool UIAttackActionSelect(const char* label, int* action, uint64_t heroId) {
    std::vector<int> actionValues = AttackActionsForHero(heroId);
    if (actionValues.empty())
        return false;

    bool changed = false;
    int currentIndex = 0;
    const auto current = std::find(actionValues.begin(), actionValues.end(), *action);
    if (current != actionValues.end()) {
        currentIndex = static_cast<int>(current - actionValues.begin());
    } else {
        *action = actionValues.front();
        changed = true;
    }

    std::vector<const char*> actionLabels;
    actionLabels.reserve(actionValues.size());
    for (int value : actionValues)
        actionLabels.push_back(OW::Labels::AttackActionName(value));

    if (UISelect(label, &currentIndex, actionLabels.data(), static_cast<int>(actionLabels.size()))) {
        *action = actionValues[static_cast<size_t>(currentIndex)];
        changed = true;
    }

    return changed;
}

// =====================================================================
// UISegmented  --  Row of buttons, active one gets accent background.
//                  Returns the new active index (or old if unchanged).
// =====================================================================
static int UISegmented(const char* items[], int itemCount, int active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float width  = ImGui::GetContentRegionAvail().x;
    float height = (itemCount <= 5) ? 21.0f : 17.0f;

    ImVec2 pos = window->DC.CursorPos;

    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                    kColControl, kControlRounding);
    window->DrawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                              kColStrokeDark, kControlRounding, 0, 1.0f);

    float segW = width / itemCount;
    int   result = active;

    for (int i = 0; i < itemCount; i++) {
        ImVec2 segMin(pos.x + i * segW, pos.y);
        ImVec2 segMax(pos.x + (i + 1) * segW, pos.y + height);

        ImGui::SetCursorScreenPos(segMin);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(segW, height));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            result = i;
        ImGui::PopID();

        float hoverT = VisualTransition(window->GetID(items[i]) ^ 0x7791, hovered, 16.0f);
        if (hovered && i != active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                                     kColControlHover, hoverT),
                                            kControlRounding);
        }
        if (i == active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            kColAccent, kControlRounding);
            window->DrawList->AddLine(ImVec2(segMin.x + 5.0f, segMin.y + 1.0f),
                                      ImVec2(segMax.x - 5.0f, segMin.y + 1.0f),
                                      IM_COL32(0xff, 0xff, 0xff, 0x28), 1.0f);
        }

        const char* txt = items[i];
        ImVec2 tsz = ImGui::CalcTextSize(txt);
        ImVec2 txtPos(segMin.x + (segW - tsz.x) * 0.5f,
                      segMin.y + (height - tsz.y) * 0.5f);
        ImU32 txtCol = (i == active)
            ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
            : MixColor(kColTextMuted, kColText, hoverT);
        window->DrawList->AddText(txtPos, txtCol, txt);
    }

    ImGui::Dummy(ImVec2(0.0f, height + 8.0f));

    return result;
}

static std::string ActionSlotTabLabel(int slotIndex) {
    return std::to_string(ClampHeroPresetSlotIndex(slotIndex) + 1);
}

static std::string ActionSlotReadoutLabel(int slotIndex) {
    return std::string("Slot ") + ActionSlotTabLabel(slotIndex);
}

static bool UIActionSlotSelector(ActionSlotKind kind, uint64_t heroId) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    int& activeSlot = ActiveHeroPresetSlotRef(kind);
    const int slotCount = heroId == 0
        ? 1
        : ImClamp(GetHeroActionSlotCount(kind, heroId), 1, OW::Config::kMaxHeroPresetSlots);
    activeSlot = ImClamp(activeSlot, 0, slotCount - 1);

    std::vector<int> slotIndices;
    std::vector<std::string> slotLabels;
    slotIndices.reserve(slotCount);
    slotLabels.reserve(slotCount);

    for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
        slotIndices.push_back(slotIndex);
        slotLabels.emplace_back(ActionSlotTabLabel(slotIndex));
    }

    const int visibleActive = activeSlot;
    const bool canAdd = heroId != 0 && slotCount < OW::Config::kMaxHeroPresetSlots;
    const bool canDelete = heroId != 0 && slotCount > 1;
    const float addW = canAdd ? 24.0f : 0.0f;
    const float deleteW = canDelete ? 24.0f : 0.0f;
    const float addGap = canAdd ? 6.0f : 0.0f;
    const float deleteGap = canDelete ? 6.0f : 0.0f;
    const float height = (slotIndices.size() <= 5) ? 21.0f : 17.0f;
    const float availW = ImGui::GetContentRegionAvail().x;
    const float width = MaxFloat(1.0f, availW - addW - addGap - deleteW - deleteGap);
    const ImVec2 pos = window->DC.CursorPos;

    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                    kColControl, kControlRounding);
    window->DrawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                              kColStrokeDark, kControlRounding, 0, 1.0f);

    bool changed = false;
    const float segW = width / static_cast<float>(slotIndices.size());
    for (int i = 0; i < static_cast<int>(slotIndices.size()); ++i) {
        const int slotIndex = slotIndices[static_cast<size_t>(i)];
        const bool slotEnabled = heroId == 0 || IsHeroActionSlotEnabled(kind, heroId, slotIndex);
        const bool isActive = i == visibleActive;
        const ImVec2 segMin(pos.x + i * segW, pos.y);
        const ImVec2 segMax(pos.x + (i + 1) * segW, pos.y + height);

        ImGui::SetCursorScreenPos(segMin);
        ImGui::PushID(slotIndex);
        ImGui::InvisibleButton("##actionSlot", ImVec2(segW, height));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) {
            activeSlot = slotIndex;
            changed = true;
        }
        ImGui::PopID();

        const float hoverT = VisualTransition(window->GetID(slotLabels[static_cast<size_t>(i)].c_str()) ^ 0x327a, hovered, 16.0f);
        if (hovered && !isActive) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                                     kColControlHover, hoverT),
                                            kControlRounding);
        }
        if (isActive) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            slotEnabled ? kColAccent : kColControlHot,
                                            kControlRounding);
            window->DrawList->AddLine(ImVec2(segMin.x + 5.0f, segMin.y + 1.0f),
                                      ImVec2(segMax.x - 5.0f, segMin.y + 1.0f),
                                      IM_COL32(0xff, 0xff, 0xff, 0x28), 1.0f);
        }

        const char* txt = slotLabels[static_cast<size_t>(i)].c_str();
        const ImVec2 tsz = ImGui::CalcTextSize(txt);
        const ImVec2 txtPos(segMin.x + (segW - tsz.x) * 0.5f,
                            segMin.y + (height - tsz.y) * 0.5f);
        const ImU32 txtCol = isActive
            ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
            : (slotEnabled ? MixColor(kColTextMuted, kColText, hoverT) : kColTextDim);
        window->DrawList->AddText(txtPos, txtCol, txt);
    }

    float actionX = pos.x + width;
    if (canAdd) {
        actionX += addGap;
        const ImVec2 addPos(actionX, pos.y);
        ImGui::SetCursorScreenPos(addPos);
        ImGui::PushID("AddActionSlot");
        if (ImGui::Button("+", ImVec2(addW, height))) {
            const OW::Config::HeroPreset seedPreset =
                GetHeroActionPresetOrDefault(kind, heroId, activeSlot);
            const int addedSlot = AddHeroActionSlot(kind, heroId, seedPreset);
            if (addedSlot >= 0) {
                activeSlot = addedSlot;
                changed = true;
            }
        }
        ImGui::SetItemTooltip("Add a slot page at the end.");
        ImGui::PopID();
        actionX += addW;
    }

    if (canDelete) {
        actionX += deleteGap;
        const ImVec2 deletePos(actionX, pos.y);
        ImGui::SetCursorScreenPos(deletePos);
        ImGui::PushID("DeleteActionSlot");
        if (ImGui::Button("-", ImVec2(deleteW, height))) {
            const int deletedSlot = activeSlot;
            if (DeleteHeroActionSlot(kind, heroId, deletedSlot)) {
                const int nextCount = ImClamp(GetHeroActionSlotCount(kind, heroId), 1, OW::Config::kMaxHeroPresetSlots);
                activeSlot = ImClamp(deletedSlot, 0, nextCount - 1);
                changed = true;
            }
        }
        ImGui::SetItemTooltip("Delete this slot page and shift later slots forward.");
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy(ImVec2(availW, height + 8.0f));
    return changed;
}

// =====================================================================
// UITwoColumns  --  Two equal-width columns.
// =====================================================================
static void UITwoColumns(std::function<void()> left, std::function<void()> right) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
    ImGui::Columns(2, nullptr, false);
    left();
    ImGui::NextColumn();
    right();
    ImGui::Columns(1);
    ImGui::PopStyleVar();
}

// =====================================================================
// SettingRow  --  Renders a fixed-width label column + the next widget.
// =====================================================================
static void SettingRow(const char* label, float labelWidthPx) {
    float startX = ImGui::GetCursorPosX();
    float startY = ImGui::GetCursorPosY();
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(screenPos.x, screenPos.y + (kControlHeight - labelSize.y) * 0.5f),
        kColText, label);
    ImGui::SetCursorPosX(startX + labelWidthPx);
    ImGui::SetCursorPosY(startY);
}

static bool UIActionSlotEnabledCheckbox(ActionSlotKind kind,
                                        const HeroOption& hero,
                                        const char* checkboxId,
                                        float labelWidthPx) {
    SettingRow("Slot Enabled", labelWidthPx);
    if (hero.heroId == 0) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Always");
        return false;
    }

    const int slotIndex = ActiveHeroPresetSlotIndex(kind);
    bool slotEnabled = IsHeroActionSlotEnabled(kind, hero.heroId, slotIndex);
    if (!UICheckbox(checkboxId, &slotEnabled))
        return false;

    SetHeroActionSlotEnabled(kind, hero.heroId, slotIndex, slotEnabled);
    return true;
}

static bool UIColorEdit(const char* label, ImVec4* value,
                        float labelWidthPx = kDefaultLabelWidth) {
    ImGui::PushID(label);
    SettingRow(label, labelWidthPx);
    PushControlWidth();
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kColControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kColControlHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleColor(ImGuiCol_Border, kColStroke);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);

    const bool changed = ImGui::ColorEdit4(
        "##color",
        &value->x,
        ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_InputRGB
    );

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    ImGui::PopItemWidth();
    ImGui::PopID();
    return changed;
}

// =====================================================================
// DrawDivider  --  Thin separator line matching React .divider
// =====================================================================
static void DrawDivider() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        pos, ImVec2(pos.x + width, pos.y), IM_COL32(0x08, 0x09, 0x0b, 0xB8));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

static void DrawCheckboxGrid3(const char* labels[], bool* values[], int rowCount,
                              float gapX, const float ratios[3]) {
    float avail = ImGui::GetContentRegionAvail().x;
    float ratioTotal = ratios[0] + ratios[1] + ratios[2];
    float usable = MaxFloat(0.0f, avail - gapX * 2.0f);
    float colW[3] = {
        usable * ratios[0] / ratioTotal,
        usable * ratios[1] / ratioTotal,
        usable * ratios[2] / ratioTotal
    };

    ImVec2 start = ImGui::GetCursorScreenPos();
    float colX[3] = {
        start.x,
        start.x + colW[0] + gapX,
        start.x + colW[0] + gapX + colW[1] + gapX
    };
    const float rowH = kControlHeight;
    const float rowGap = 10.0f;

    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (!labels[idx] || !values[idx])
                continue;

            ImGui::SetCursorScreenPos(ImVec2(colX[col], start.y + row * (rowH + rowGap)));
            UICheckbox(labels[idx], values[idx]);
        }
    }

    float gridH = (rowCount > 0) ? (rowCount * rowH + (rowCount - 1) * rowGap) : 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + gridH));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

static void DrawTopTabIcon(ImDrawList* drawList, int tabIndex, const ImVec2& min, ImU32 color) {
    ImVec2 c(min.x + 9.0f, min.y + 9.0f);

    if (tabIndex == 0) {
        drawList->AddCircle(c, 6.0f, color, 24, 1.5f);
        drawList->AddCircle(c, 2.0f, color, 16, 1.5f);
        drawList->AddLine(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.5f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x + 4.5f, c.y), ImVec2(c.x + 8.0f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y - 4.5f), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y + 4.5f), ImVec2(c.x, c.y + 8.0f), color, 1.5f);
    } else if (tabIndex == 1) {
        drawList->AddEllipse(c, ImVec2(8.0f, 4.8f), color, 0.0f, 24, 1.5f);
        drawList->AddCircle(c, 2.4f, color, 16, 1.5f);
    } else if (tabIndex == 2) {
        drawList->AddCircle(c, 7.0f, color, 24, 1.5f);
        drawList->AddCircleFilled(ImVec2(c.x - 2.5f, c.y - 2.2f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x + 2.0f, c.y - 2.5f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x - 0.5f, c.y + 2.4f), 1.1f, color, 8);
        drawList->AddCircle(ImVec2(c.x + 4.2f, c.y + 3.4f), 2.1f, color, 10, 1.5f);
    } else {
        drawList->AddCircle(c, 3.3f, color, 16, 1.5f);
        for (int i = 0; i < 8; ++i) {
            float a = (3.14159265f * 2.0f * i) / 8.0f;
            ImVec2 inner(c.x + std::cos(a) * 5.1f, c.y + std::sin(a) * 5.1f);
            ImVec2 outer(c.x + std::cos(a) * 8.0f, c.y + std::sin(a) * 8.0f);
            drawList->AddLine(inner, outer, color, 1.5f);
        }
    }
}

static int CurrentPageKey() {
    switch (UI::state.activeTab) {
        case UI::TAB_AIMING:
            return ImClamp(UI::state.aimingSubTab, 0, kAimingSubTabCount - 1);
        case UI::TAB_VISUALS:
            return kVisualsPageKey;
        case UI::TAB_THEME:
            return kThemePageKeyBase + ImClamp(UI::state.themeSubTab, 0, kThemeSubTabCount - 1);
        case UI::TAB_MISC:
            return kMiscPageKeyBase + ImClamp(UI::state.miscSubTab, 0, kMiscSubTabCount - 1);
        default:
            return 0;
    }
}

static float CurrentSubBarHeight() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 32.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 42.0f;
    if (UI::state.activeTab == UI::TAB_THEME)
        return 32.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 32.0f;
    return 0.0f;
}

static float CurrentBodyTopPad() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 8.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 10.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 8.0f;
    return 10.0f;
}

static float CurrentShellFrameHeight() {
    return kShellBorder * 2.0f + kHeaderHeight + CurrentSubBarHeight() + CurrentBodyTopPad();
}

static float FallbackShellHeight() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 548.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 476.0f;
    if (UI::state.activeTab == UI::TAB_THEME)
        return 548.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 548.0f;
    return 140.0f;
}

static float CurrentShellHeight() {
    const float measuredBodyHeight = s_measuredBodyHeightByPage[CurrentPageKey()];
    const float shellHeight = measuredBodyHeight > 0.0f
        ? CurrentShellFrameHeight() + measuredBodyHeight
        : FallbackShellHeight();
    return ImClamp(shellHeight, kMinShellHeight, kMaxShellHeight);
}

static void UpdateDesiredMenuClientSize(float measuredBodyHeight) {
    s_measuredBodyHeightByPage[CurrentPageKey()] = MaxFloat(0.0f, measuredBodyHeight);
    s_desiredMenuClientSize.width = kShellWidth;
    s_desiredMenuClientSize.height = CurrentShellHeight();
}

void UI::InitializeResources(ID3D11Device* device) {
    if (s_logoTexture || !device)
        return;

    CreateTextureFromIconResource(device, IDI_UNLEASHED, kLogoTextureSize, &s_logoTexture);
}

void UI::ShutdownResources() {
    if (s_logoTexture) {
        s_logoTexture->Release();
        s_logoTexture = nullptr;
    }
}

UI::MenuClientSize UI::DesiredMenuClientSize() {
    return s_desiredMenuClientSize;
}

// =====================================================================
// UI::InitStyle  --  Refined dark overlay styling.
// =====================================================================
void UI::InitStyle() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableTooltip = false;

    if (!s_regularFont && !io.Fonts->Locked) {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;
        fontConfig.RasterizerMultiply = 1.12f;
        auto addUiFont = [&](const char* primaryPath, const char* fallbackPath, float size) {
            ImFont* font = io.Fonts->AddFontFromFileTTF(primaryPath, size, &fontConfig);
            if (font) {
                ImFontConfig fallbackConfig;
                fallbackConfig.MergeMode = true;
                fallbackConfig.OversampleH = fontConfig.OversampleH;
                fallbackConfig.OversampleV = fontConfig.OversampleV;
                fallbackConfig.PixelSnapH = fontConfig.PixelSnapH;
                fallbackConfig.RasterizerMultiply = fontConfig.RasterizerMultiply;
                io.Fonts->AddFontFromFileTTF(fallbackPath, size, &fallbackConfig, kNotOpenedGlyphRanges);
            }
            return font;
        };
        s_regularFont = addUiFont("C:\\Windows\\Fonts\\segoeui.ttf",
                                  "C:\\Windows\\Fonts\\msyh.ttc", 13.25f);
        s_boldFont = addUiFont("C:\\Windows\\Fonts\\segoeuib.ttf",
                               "C:\\Windows\\Fonts\\msyhbd.ttc", 13.25f);
        s_titleFont = addUiFont("C:\\Windows\\Fonts\\segoeuib.ttf",
                                "C:\\Windows\\Fonts\\msyhbd.ttc", 15.25f);
        if (!s_titleFont)
            s_titleFont = s_boldFont;
        if (s_regularFont)
            io.FontDefault = s_regularFont;
    } else if (s_regularFont) {
        io.FontDefault = s_regularFont;
    }

    ImGuiStyle& style   = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = kControlRounding;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = kControlRounding;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = kControlRounding;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.ScrollbarSize     = 7.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_FrameBg]           = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_FrameBgHovered]    = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_FrameBgActive]     = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Button]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_ButtonHovered]     = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_ButtonActive]      = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Header]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_HeaderHovered]     = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_HeaderActive]      = ImGui::ColorConvertU32ToFloat4(kColAccentDark);
    colors[ImGuiCol_CheckMark]         = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrab]        = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrabActive]  = ImGui::ColorConvertU32ToFloat4(IM_COL32(0xf0, 0x1a, 0x49, 0xFF));
    colors[ImGuiCol_Text]              = ImGui::ColorConvertU32ToFloat4(kColText);
    colors[ImGuiCol_TextDisabled]      = ImGui::ColorConvertU32ToFloat4(kColTextDim);
    colors[ImGuiCol_Border]            = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_TitleBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_TitleBgActive]     = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_Separator]         = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_ScrollbarBg]       = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x0a, 0x0b, 0x0e, 0xE0));
    colors[ImGuiCol_ScrollbarGrab]     = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x34, 0x37, 0x40, 0xD8));
    colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(0xe4, 0x11, 0x43, 0xC8));
    colors[ImGuiCol_PopupBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_TextSelectedBg]    = ImGui::ColorConvertU32ToFloat4(kColAccentSoft);
    state.initialized = s_regularFont != nullptr || !io.Fonts->Locked;
}

// =====================================================================
// UI::AimbotPage
// =====================================================================
void UI::AimbotPage() {
    state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
    const HeroOption* selectedHero = &kHeroOptions[state.selectedTypeIndex];
    constexpr ActionSlotKind slotKind = ActionSlotKind::Aim;

    // ---- Enabled action-slot selector for the current hero ----
    const int previousSlot = state.aimHeroSegActive;
    const bool slotSelectorChanged = UIActionSlotSelector(slotKind, selectedHero->heroId);
    state.aimHeroSegActive = ActiveHeroPresetSlotIndex(slotKind);
    if (slotSelectorChanged || previousSlot != state.aimHeroSegActive)
        ApplySelectedTypePreset();

    bool presetChanged = false;
    bool slotEnabledChanged = false;
    OW::Config::HeroPreset activePreset{};

    auto refreshActivePreset = [&]() {
        state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
        selectedHero = &kHeroOptions[state.selectedTypeIndex];
        const bool isGlobal = selectedHero->heroId == 0;
        activePreset = isGlobal
            ? MakeCurrentHeroActionPreset(slotKind)
            : GetHeroActionPresetOrDefault(slotKind, selectedHero->heroId, state.aimHeroSegActive);
    };
    refreshActivePreset();

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.aimHeroSegActive);

    UIGroupBox("Action Slot");
    {
        slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##aimSlotEnabled", kAimbotHeroLabelWidth);
    }
    CloseGroupBox();

    // ---- Two columns ----
    UITwoColumns([&]() {
        // LEFT: Aimbot Hero Basic Options
        UIGroupBox("Aim Hero Basic Options");
        {
            // Aim Behavior
            SettingRow("Aim Behavior", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimBehavior", &activePreset.aimBehavior,
                         kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
                activePreset.aimBehavior = OW::Config::ClampAimBehaviorIndex(activePreset.aimBehavior);
                activePreset.aimMode = OW::Config::IsTrackingBehavior(activePreset.aimBehavior) ? 0 : 1;
                if (activePreset.firePolicy == 0 || activePreset.firePolicy == 1 || activePreset.firePolicy == 2) {
                    activePreset.firePolicy = OW::Config::IsTrackingBehavior(activePreset.aimBehavior) ? 1 : 2;
                    activePreset.keepFiring = activePreset.firePolicy == 1;
                    activePreset.autoshot = activePreset.firePolicy >= 2;
                }
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            // Aim activation key is stored per aim slot.
            SettingRow("Aim Activation Key", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimActivationKey", &activePreset.key,
                                      OW::Labels::kAimActivationKeys, OW::Labels::AimActivationKeyCount());
            ImGui::PopItemWidth();

            // Attack
            SettingRow("Attack", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UIAttackActionSelect("##aimAttack", &activePreset.trigger.action, selectedHero->heroId);
            ImGui::PopItemWidth();

            // Fire Policy
            SettingRow("Fire Policy", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimFirePolicy", &activePreset.firePolicy,
                         kFirePolicy, IM_ARRAYSIZE(kFirePolicy))) {
                activePreset.keepFiring = activePreset.firePolicy == 1;
                activePreset.autoshot = activePreset.firePolicy >= 2;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            // Bone Preference combines fixed aim bones and the dynamic closest-bone mode.
            SettingRow("Bone Preference", kAimbotLeftLabelWidth);
            PushControlWidth();
            activePreset.bone = OW::Config::NormalizeAimBone(activePreset.bone);
            int bonePreferenceIndex = BonePreferenceIndexFromPreset(activePreset);
            if (UISelect("##aimBone", &bonePreferenceIndex, kBonePreference, IM_ARRAYSIZE(kBonePreference))) {
                ApplyBonePreferenceToPreset(bonePreferenceIndex, activePreset);
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            // Prediction
            SettingRow("Prediction", kAimbotLeftLabelWidth);
            PushControlWidth();
            if (UISelect("##aimPredictionMode", &activePreset.predictionMode,
                         kPredictionMode, IM_ARRAYSIZE(kPredictionMode))) {
                activePreset.prediction = activePreset.predictionMode == 1;
                presetChanged = true;
            }
            ImGui::PopItemWidth();

            // Max Head Distance
            SettingRow("Max Head Distance", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxHead", &activePreset.maxHeadDistance, 0.0f, 500.0f, "100 m");
            ImGui::PopItemWidth();

            // Retarget hysteresis
            SettingRow("Retarget Hysteresis", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimStick", &activePreset.stickiness, 0.0f, 100.0f, "Max");
            ImGui::PopItemWidth();

            // Speed scale
            SettingRow("Speed Scale", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimSmooth", &activePreset.smooth, 0.0f, 100.0f, "50.00 %");
            ImGui::PopItemWidth();

            if (OW::Config::IsTrackingBehavior(activePreset.aimBehavior)) {
                SettingRow("Deadzone", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimTrackingDeadzone", &activePreset.trackingDeadzone, 0.0f, 250.0f, "0 px");
                ImGui::PopItemWidth();
            }

            if (OW::Config::IsFlickBehavior(activePreset.aimBehavior)) {
                SettingRow("Shot Clamp", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlickShotClamp", &activePreset.flickShotClampMs, 0.0f, 1000.0f, "0 ms");
                ImGui::PopItemWidth();

                SettingRow("Post-fire Delay", kAimbotLeftLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlickPostFireDelay", &activePreset.flickPostFireDelayMs, 0.0f, 500.0f, "0 ms");
                ImGui::PopItemWidth();

                SettingRow("Trajectory Wait", kAimbotLeftLabelWidth);
                presetChanged |= UICheckbox("##aimFlickTrajectoryWait", &activePreset.flickTrajectoryWait);

                if (activePreset.flickTrajectoryWait) {
                    SettingRow("Wait Limit", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimFlickTrajectoryWaitMs", &activePreset.flickTrajectoryWaitMs, 0.0f, 1000.0f, "120 ms");
                    ImGui::PopItemWidth();

                    SettingRow("Apex Window", kAimbotLeftLabelWidth);
                    PushControlWidth();
                    presetChanged |= UISlider("##aimFlickTrajectoryApexWindow", &activePreset.flickTrajectoryApexWindowMs, 0.0f, 300.0f, "60 ms");
                    ImGui::PopItemWidth();
                }
            }

            // FOV
            SettingRow("FOV (deg)", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimFov", &activePreset.fov,
                                      OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg,
                                      "100 deg");
            ImGui::PopItemWidth();

            // Target Priority
            SettingRow("Target Priority", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimPriority", &activePreset.priority, kPriority, IM_ARRAYSIZE(kPriority));
            ImGui::PopItemWidth();

            // Target Team
            SettingRow("Target Team", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimTeam", &activePreset.targetTeam, kTeam, IM_ARRAYSIZE(kTeam));
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    }, [&]() {
        // RIGHT: Aimbot Hero Expert Options
        UIGroupBox("Aim Hero Expert Options");
        {
            // Max Aim Time
            SettingRow("Max Aim Time", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxAim", &activePreset.maxAimTime, 0.0f, 100.0f, "Endless");
            ImGui::PopItemWidth();

            // Hitbox Scale
            SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent,
                                      "100 %");
            ImGui::PopItemWidth();

            if (OW::Config::IsFlick2ndBehavior(activePreset.aimBehavior)) {
                SettingRow("Second Method", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISelect("##aimFlick2ndInnerMethod", &activePreset.flick2ndInnerMethod,
                                          kAimMethod, IM_ARRAYSIZE(kAimMethod));
                ImGui::PopItemWidth();

                SettingRow("Trigger Gate", kAimbotRightLabelWidth);
                presetChanged |= UICheckbox("##aimFlick2ndTriggerGate", &activePreset.flick2ndTriggerGate);

                SettingRow("Box Padding", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndBoxPadding", &activePreset.flick2ndBoxPadding, 0.0f, 80.0f, "8 px");
                ImGui::PopItemWidth();

                SettingRow("Inner Radius", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndInnerRadius", &activePreset.flick2ndInnerRadius, 0.0f, 250.0f, "34 px");
                ImGui::PopItemWidth();

                SettingRow("Inner Smooth", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimFlick2ndInnerSmooth", &activePreset.flick2ndInnerSmoothScale, 0.1f, 1.0f, "0.55x");
                ImGui::PopItemWidth();
            }

            const bool showAimChargeSettings =
                activePreset.firePolicy == static_cast<int>(OW::FirePolicyType::ChargeRelease);
            if (showAimChargeSettings) {
                // Aim Min Charge
                SettingRow("Aim Min Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimMinChg", &activePreset.minCharge, 0.0f, 100.0f, "5 %");
                ImGui::PopItemWidth();

                // Autoshot Max Charge
                SettingRow("Autoshot Max Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##aimMaxChg", &activePreset.maxCharge, 0.0f, 100.0f, "100 %");
                ImGui::PopItemWidth();
            }

            // Ignore Invisible Targets
            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##aimIgnoreInvis", &activePreset.ignoreInvisible);

            // Trace Condition
            SettingRow("Trace Condition", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimTrace", &activePreset.traceCondition, kTrace, IM_ARRAYSIZE(kTrace));
            ImGui::PopItemWidth();

            // Unlock Condition
            SettingRow("Unlock Condition", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##aimUnlock", &activePreset.unlockCondition, kUnlock, IM_ARRAYSIZE(kUnlock));
            ImGui::PopItemWidth();

            // Lock Time
            SettingRow("Lock Time", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimLockTime", &activePreset.lockTime, 0.0f, 100.0f, "200 ms");
            ImGui::PopItemWidth();

            // Max Distance
            SettingRow("Max Distance", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMaxDist", &activePreset.maxDistance, 0.0f, 500.0f, "100 m");
            ImGui::PopItemWidth();

            // Min Distance
            SettingRow("Min Distance", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##aimMinDist", &activePreset.minDistance, 0.0f, 500.0f, "0 m");
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    });

    if (selectedHero->heroId == 0) {
        if (presetChanged)
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
    } else {
        if (presetChanged) {
            SetHeroActionPreset(slotKind, selectedHero->heroId, state.aimHeroSegActive, activePreset);
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        } else if (slotEnabledChanged &&
                   IsHeroActionSlotEnabled(slotKind, selectedHero->heroId, state.aimHeroSegActive)) {
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        }
    }

    ImGui::PopID();
}

// =====================================================================
// UI::TriggerPage
// =====================================================================
void UI::TriggerPage() {
    state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
    const HeroOption* selectedHero = &kHeroOptions[state.selectedTypeIndex];
    constexpr ActionSlotKind slotKind = ActionSlotKind::Trigger;

    const int previousSlot = state.triggerHeroSegActive;
    const bool slotSelectorChanged = UIActionSlotSelector(slotKind, selectedHero->heroId);
    state.triggerHeroSegActive = ActiveHeroPresetSlotIndex(slotKind);
    if (slotSelectorChanged || previousSlot != state.triggerHeroSegActive)
        ApplySelectedTypePreset();

    bool presetChanged = false;
    bool slotEnabledChanged = false;
    OW::Config::HeroPreset activePreset{};

    auto refreshActivePreset = [&]() {
        state.selectedTypeIndex = ClampHeroSelectionIndex(state.selectedTypeIndex);
        selectedHero = &kHeroOptions[state.selectedTypeIndex];
        const bool isGlobal = selectedHero->heroId == 0;
        activePreset = isGlobal
            ? MakeCurrentHeroActionPreset(slotKind)
            : GetHeroActionPresetOrDefault(slotKind, selectedHero->heroId, state.triggerHeroSegActive);
    };
    refreshActivePreset();

    PushHeroActionSlotControlId(slotKind, selectedHero->heroId, state.triggerHeroSegActive);

    UIGroupBox("Action Slot");
    {
        slotEnabledChanged |= UIActionSlotEnabledCheckbox(slotKind, *selectedHero, "##triggerSlotEnabled", kAimbotHeroLabelWidth);
    }
    CloseGroupBox();

    UITwoColumns([&]() {
        UIGroupBox("Trigger Slot");
        {
            SettingRow("Enabled", kAimbotLeftLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotEnable", &activePreset.trigger.enabled);

            SettingRow("Action", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UIAttackActionSelect("##triggerSlotAction", &activePreset.trigger.action, selectedHero->heroId);
            ImGui::PopItemWidth();

            SettingRow("Mode", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##triggerSlotMode", &activePreset.trigger.mode,
                                      OW::Labels::kTriggerbotModes, OW::Labels::TriggerbotModeCount());
            ImGui::PopItemWidth();

            SettingRow("Activation Key", kAimbotLeftLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##triggerSlotKey", &activePreset.trigger.key,
                                      OW::Labels::kAimActivationKeys, OW::Labels::AimActivationKeyCount());
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    }, [&]() {
        UIGroupBox("Trigger Conditions");
        {
            SettingRow("Shot Interval", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##triggerSlotInterval", &activePreset.trigger.shotInterval,
                                      0.0f, 100.0f, "500 ms");
            ImGui::PopItemWidth();

            SettingRow("Charge Aware", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotChargeAware", &activePreset.trigger.chargeAware);

            if (activePreset.trigger.chargeAware) {
                SettingRow("Min Charge", kAimbotRightLabelWidth);
                PushControlWidth();
                presetChanged |= UISlider("##triggerSlotMinCharge", &activePreset.trigger.minCharge,
                                          0.0f, 100.0f, "50.00 %");
                ImGui::PopItemWidth();
            }

            SettingRow("Detection Scale", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISlider("##triggerSlotHitbox", &activePreset.hitbox,
                                      OW::Config::kMinHitboxScalePercent,
                                      OW::Config::kMaxHitboxScalePercent,
                                      "100 %");
            ImGui::PopItemWidth();

            SettingRow("Target Filter", kAimbotRightLabelWidth);
            PushControlWidth();
            presetChanged |= UISelect("##triggerSlotTeam", &activePreset.targetTeam,
                                      kTeam, IM_ARRAYSIZE(kTeam));
            ImGui::PopItemWidth();

            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotIgnoreInvis", &activePreset.trigger.ignoreInvisible);

            SettingRow("Draw Hitbox", kAimbotRightLabelWidth);
            presetChanged |= UICheckbox("##triggerSlotDrawHitbox", &activePreset.trigger.drawHitbox);
        }
        CloseGroupBox();
    });

    if (selectedHero->heroId == 0) {
        if (presetChanged)
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
    } else {
        if (presetChanged) {
            SetHeroActionPreset(slotKind, selectedHero->heroId, state.triggerHeroSegActive, activePreset);
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        } else if (slotEnabledChanged &&
                   IsHeroActionSlotEnabled(slotKind, selectedHero->heroId, state.triggerHeroSegActive)) {
            ApplyHeroActionPresetToGlobals(slotKind, activePreset);
        }
    }

    ImGui::PopID();
}

static bool DrawHeroSkillSequenceSteps(const OW::HeroSkillDefinition& definition,
                                       OW::Config::HeroSkillSettings& settings) {
    bool changed = false;

    ImGui::Spacing();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "Sequence Steps  (L=Left  R=Right)");

    if (!settings.sequenceSteps.empty()) {
        float speedScale = settings.sequenceSteps.front().speedScale;
        SettingRow("Speed Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        if (UISlider("##sequenceSpeedScale", &speedScale, 0.5f, 2.0f, "1.00x")) {
            for (OW::Config::HeroSkillSequenceStep& step : settings.sequenceSteps)
                step.speedScale = speedScale;
            changed = true;
        }
        ImGui::PopItemWidth();
    }

    for (int index = 0; index < static_cast<int>(settings.sequenceSteps.size()); ++index) {
        OW::Config::HeroSkillSequenceStep& step = settings.sequenceSteps[static_cast<size_t>(index)];
        ImGui::PushID(index);

        ImGui::AlignTextToFramePadding();
        ImGui::Text("#%d", index + 1);
        ImGui::SameLine(0.0f, 8.0f);

        bool left = (step.buttonMask & 1) != 0;
        bool right = (step.buttonMask & 2) != 0;
        if (UICheckbox("L", &left)) { step.buttonMask ^= 1; changed = true; }
        ImGui::SameLine(0.0f, 4.0f);
        if (UICheckbox("R", &right)) { step.buttonMask ^= 2; changed = true; }
        ImGui::SameLine(0.0f, 8.0f);

        ImGui::PushItemWidth(150.0f);
        changed |= UISlider("##stepDur", &step.durationMs, 0.0f, 1000.0f, "117 ms");
        ImGui::PopItemWidth();
        ImGui::SameLine(0.0f, 8.0f);

        ImGui::TextUnformatted("+/-");
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushItemWidth(96.0f);
        changed |= UISlider("##stepJitter", &step.jitterMs, 0.0f, 50.0f, "0 ms");
        ImGui::PopItemWidth();
        ImGui::SameLine(0.0f, 8.0f);

        if (ImGui::Button("Remove")) {
            settings.sequenceSteps.erase(settings.sequenceSteps.begin() + index);
            changed = true;
            ImGui::PopID();
            break;
        }

        ImGui::PopID();
    }

    if (static_cast<int>(settings.sequenceSteps.size()) < OW::Config::kMaxHeroSkillSequenceSteps) {
        if (ImGui::Button("Add Step")) {
            settings.sequenceSteps.push_back({ 1, 117, 1.0f, 0 });
            changed = true;
        }
    }

    if (!definition.defaultSettings.sequenceSteps.empty()) {
        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Reset Steps")) {
            settings.sequenceSteps = definition.defaultSettings.sequenceSteps;
            changed = true;
        }
    }

    return changed;
}

static bool DrawHeroSkillDefinition(const OW::HeroSkillDefinition& definition, uint64_t heroId) {
    OW::Config::HeroSkillSettings settings = OW::Config::GetHeroSkillSettings(
        heroId,
        definition.skillId ? definition.skillId : "",
        definition.defaultSettings);

    bool changed = false;
    auto hasControl = [&](OW::HeroSkillControlFlags control) {
        return OW::HasHeroSkillControl(definition, control);
    };
    ImGui::PushID(definition.skillId);

    if (ID3D11ShaderResourceView* icon = HeroSkillIconForDefinition(definition)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(24.0f, 24.0f));
        ImGui::SameLine(0.0f, 8.0f);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s / %s",
                       OW::HeroSkillCategoryName(definition.category),
                       OW::HeroSkillInputActionName(definition.inputAction));

    if (hasControl(OW::HeroSkillControls::Enabled)) {
        SettingRow("Enabled", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillEnabled", &settings.enabled);
    }

    const bool hasSequenceControls = hasControl(OW::HeroSkillControls::SequenceSteps);
    const bool hasTrackingOverlay = hasControl(OW::HeroSkillControls::TrackingOverlay);
    const bool hasPitchControls = hasControl(OW::HeroSkillControls::PitchControl);
    const bool hasPhaseTiming = hasControl(OW::HeroSkillControls::PhaseTiming);
    const bool hasAbsoluteEnemyHealth = hasControl(OW::HeroSkillControls::EnemyHealthAbsolute);
    const bool hasSkillOutputKey = !hasSequenceControls &&
        (hasControl(OW::HeroSkillControls::Key) || hasPitchControls || hasPhaseTiming);

    if (hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillKey", &settings.key,
                            OW::Labels::kAimActivationKeys,
                            OW::Labels::AimActivationKeyCount());
        ImGui::PopItemWidth();
    }

    if (hasSkillOutputKey) {
        SettingRow("Skill Key", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillOutputKey", &settings.skillKey,
                            OW::Labels::kAimActivationKeys,
                            OW::Labels::AimActivationKeyCount());
        ImGui::PopItemWidth();
    }

    if ((hasSequenceControls || hasPitchControls || hasPhaseTiming) &&
        !hasControl(OW::HeroSkillControls::Key)) {
        SettingRow("Activation Key", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillActivationKey", &settings.key,
                            OW::Labels::kAimActivationKeys,
                            OW::Labels::AimActivationKeyCount());
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Mode)) {
        SettingRow("Mode", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillMode", &settings.mode, kHeroSkillModes, IM_ARRAYSIZE(kHeroSkillModes));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::HealthThreshold)) {
        SettingRow("Health Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillHealth", &settings.healthThreshold, 0.0f, 100.0f, "50%");
        ImGui::PopItemWidth();
    }

    if (hasAbsoluteEnemyHealth) {
        SettingRow("Max Health", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillEnemyMaxHealth", &settings.enemyHealthThreshold, 0.0f, 300.0f, "40 HP");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::EnemyHealthThreshold)) {
        SettingRow("Enemy HP Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillEnemyHealth", &settings.enemyHealthThreshold, 0.0f, 100.0f, "50%");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AllyHealthThreshold)) {
        SettingRow("Ally HP Threshold", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillAllyHealth", &settings.allyHealthThreshold, 0.0f, 100.0f, "50%");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Distance)) {
        SettingRow(hasAbsoluteEnemyHealth ? "Max Distance" : "Distance", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillDistance", &settings.distance, 0.0f, 100.0f,
                            hasAbsoluteEnemyHealth ? "3.0 m" : "5.0 m");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Radius)) {
        SettingRow("Radius", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillRadius", &settings.radius, 0.0f, 30.0f, "1.5 m");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Prediction)) {
        SettingRow("Prediction", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillPrediction", &settings.prediction);
    }

    if (hasControl(OW::HeroSkillControls::CooldownGuard)) {
        SettingRow("Cooldown Guard", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillCooldownGuard", &settings.cooldownGuard);
    }

    if (hasControl(OW::HeroSkillControls::Cooldown)) {
        SettingRow("Cooldown", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillCooldown", &settings.cooldown, 0.0f, 60.0f, "10.0 s");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::AmmoGuard)) {
        SettingRow("Ammo Guard", kAimbotRightLabelWidth);
        changed |= UICheckbox("##skillAmmoGuard", &settings.ammoGuard);

        if (settings.ammoGuard) {
            SettingRow("Reserve Ammo", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillAmmoReserve", &settings.ammoGuardReserve, 0.0f, 12.0f, "1");
            ImGui::PopItemWidth();
        }
    }

    if (hasControl(OW::HeroSkillControls::MinTargets)) {
        SettingRow("Min Targets", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillMinTargets", &settings.minTargets, 1.0f, 8.0f, "2");
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Bone)) {
        SettingRow("Bone", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillTargetBone", &settings.tracking.bone,
                            kHeroSkillTargetBones, IM_ARRAYSIZE(kHeroSkillTargetBones));
        ImGui::PopItemWidth();
    }

    if (hasControl(OW::HeroSkillControls::Hitbox)) {
        SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTargetHitbox", &settings.tracking.hitbox,
                            OW::Config::kMinHitboxScalePercent,
                            OW::Config::kMaxHitboxScalePercent,
                            "150 %");
        ImGui::PopItemWidth();
    }

    if (hasTrackingOverlay && settings.enabled) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "Skill Aim");

        SettingRow("Aim Behavior", kAimbotRightLabelWidth);
        PushControlWidth();
        if (UISelect("##skillTrackingBehavior", &settings.tracking.aimBehavior,
                     kAimBehavior, IM_ARRAYSIZE(kAimBehavior))) {
            settings.tracking.aimBehavior =
                OW::Config::ClampAimBehaviorIndex(settings.tracking.aimBehavior);
            changed = true;
        }
        ImGui::PopItemWidth();

        SettingRow("Speed Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingSpeedScale", &settings.tracking.speedScale, 0.0f, 100.0f, "100 %");
        ImGui::PopItemWidth();

        SettingRow("FOV (deg)", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingFov", &settings.tracking.fov,
                            OW::Config::kMinFovDeg, OW::Config::kMaxFovDeg,
                            "100 deg");
        ImGui::PopItemWidth();

        SettingRow("Bone", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISelect("##skillTrackingBone", &settings.tracking.bone,
                            kHeroSkillTrackingBones, IM_ARRAYSIZE(kHeroSkillTrackingBones));
        ImGui::PopItemWidth();

        SettingRow("Hitbox Scale", kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider("##skillTrackingHitbox", &settings.tracking.hitbox,
                            OW::Config::kMinHitboxScalePercent,
                            OW::Config::kMaxHitboxScalePercent,
                            "100 %");
        ImGui::PopItemWidth();

        if (hasControl(OW::HeroSkillControls::Prediction)) {
            SettingRow("Projectile Speed", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillProjectileSpeed", &settings.projectileSpeed, 0.0f, 300.0f, "60 m/s");
            ImGui::PopItemWidth();

            SettingRow("Projectile Radius", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillProjectileRadius", &settings.projectileRadius, 0.0f, 2.0f, "0.2 m");
            ImGui::PopItemWidth();

            SettingRow("Projectile Gravity", kAimbotRightLabelWidth);
            changed |= UICheckbox("##skillProjectileGravity", &settings.projectileGravity);

            SettingRow("Pre-fire Delay", kAimbotRightLabelWidth);
            PushControlWidth();
            changed |= UISlider("##skillPreFireDelay", &settings.preFireDelayMs, 0.0f, 1000.0f, "320 ms");
            ImGui::PopItemWidth();
        }
    }

    auto drawFloatSlider = [&](const char* rowLabel,
                               const char* id,
                               float& value,
                               float minValue,
                               float maxValue,
                               const char* formatText) {
        SettingRow(rowLabel, kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider(id, &value, minValue, maxValue, formatText);
        ImGui::PopItemWidth();
    };

    auto drawIntSlider = [&](const char* rowLabel,
                             const char* id,
                             int& value,
                             float minValue,
                             float maxValue,
                             const char* formatText) {
        SettingRow(rowLabel, kAimbotRightLabelWidth);
        PushControlWidth();
        changed |= UISlider(id, &value, minValue, maxValue, formatText);
        ImGui::PopItemWidth();
    };

    if (hasPitchControls) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "Pitch Down");

        drawIntSlider("Duration", "##pitchDownDuration", settings.pitchDownDurationMs, 20.0f, 100.0f, "45 ms");
        drawFloatSlider("Duration Jitter", "##pitchDownDurationJitter", settings.pitchDownDurationJitter, 0.0f, 50.0f, "+/- 10 ms");
        drawFloatSlider("Target Angle", "##pitchDownTarget", settings.pitchDownTargetAngle, 0.0f, 180.0f, "90 deg");
    }

    if (hasPitchControls || hasPhaseTiming) {
        ImGui::Spacing();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "Pitch Up");

        if (hasPitchControls) {
            drawFloatSlider("Return Jitter", "##pitchUpOffsetJitter", settings.pitchUpOffsetJitter, 0.0f, 20.0f, "+/- 1.5 deg");
        }

        if (hasPhaseTiming) {
            drawIntSlider("Fire Delay", "##skillFireDelay", settings.fireDelayMs, 0.0f, 100.0f, "50 ms");
            drawIntSlider("Jump Key (VK)", "##skillJumpKeyCode", settings.jumpKeyCode, 0.0f, 255.0f, "32");
        }
    }

    if (changed)
        OW::Config::SetHeroSkillSettings(heroId, definition.skillId ? definition.skillId : "", settings);

    ImGui::PopID();
    return changed;
}

static bool DrawHeroSkillSequenceDefinition(const OW::HeroSkillDefinition& definition, uint64_t heroId) {
    OW::Config::HeroSkillSettings settings = OW::Config::GetHeroSkillSettings(
        heroId,
        definition.skillId ? definition.skillId : "",
        definition.defaultSettings);

    ImGui::PushID(definition.skillId);
    if (ID3D11ShaderResourceView* icon = HeroSkillIconForDefinition(definition)) {
        ImGui::Image(reinterpret_cast<ImTextureID>(icon), ImVec2(24.0f, 24.0f));
        ImGui::SameLine(0.0f, 8.0f);
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s / %s",
                       OW::HeroSkillCategoryName(definition.category),
                       OW::HeroSkillInputActionName(definition.inputAction));

    const bool changed = DrawHeroSkillSequenceSteps(definition, settings);
    if (changed)
        OW::Config::SetHeroSkillSettings(heroId, definition.skillId ? definition.skillId : "", settings);

    ImGui::PopID();
    return changed;
}

// =====================================================================
// UI::SkillsPage
// =====================================================================
void UI::SkillsPage() {
    ImGui::PushID("SkillsPage");

    UIGroupBox("Auto Alt-Fire");
    {
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoRmbEnable", &OW::Config::AutoRMB);

        SettingRow("Health Threshold", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##autoRmbHealth", &OW::Config::AutoRMBhealth, 0.0f, 500.0f, "50 HP");
        ImGui::PopItemWidth();

        SettingRow("Distance", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##autoRmbDistance", &OW::Config::AutoRMBdistance, 0.0f, 60.0f, "5.0 m");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox("Auto Skill");
    {
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoSkillEnable", &OW::Config::AutoSkill);

        SettingRow("Health Threshold", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##skillHealth", &OW::Config::SkillHealth, 0.0f, 500.0f, "50 HP");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox("Auto Shoot");
    {
        SettingRow("Enabled", kDefaultLabelWidth);
        UICheckbox("##autoShootEnable", &OW::Config::AutoShoot);

        SettingRow("Cooldown", kDefaultLabelWidth);
        PushControlWidth();
        UISlider("##shootCooldown", &OW::Config::Shoottime, 100, 2000, "500 ms");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    const HeroOption& selectedHero = CurrentHeroOption();
    int renderedSkillCount = 0;
    if (selectedHero.heroId != 0) {
        for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
            if (definition.heroId != selectedHero.heroId)
                continue;

            std::string title = selectedHero.label;
            title += " / ";
            title += definition.displayName;
            UIGroupBox(title.c_str());
            DrawHeroSkillDefinition(definition, selectedHero.heroId);
            CloseGroupBox();
            ++renderedSkillCount;
        }
    }

    if (renderedSkillCount == 0) {
        UIGroupBox("Hero Skills");
        {
            SettingRow("Selected Hero", kDefaultLabelWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(selectedHero.label);

            SettingRow("Definitions", kDefaultLabelWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextDim), "None");
        }
        CloseGroupBox();
    }

    ImGui::PopID();
}

// =====================================================================
// UI::Combo page
// =====================================================================
void UI::SequencesPage() {
    ImGui::PushID("SequencesPage");

    const HeroOption& selectedHero = CurrentHeroOption();
    int renderedSequenceCount = 0;

    if (selectedHero.heroId != 0) {
        for (const OW::HeroSkillDefinition& definition : OW::AllHeroSkillDefinitions()) {
            if (definition.heroId != selectedHero.heroId ||
                !OW::HasHeroSkillControl(definition, OW::HeroSkillControls::SequenceSteps)) {
                continue;
            }

            std::string title = selectedHero.label;
            title += " / ";
            title += definition.displayName;
            UIGroupBox(title.c_str());
            DrawHeroSkillSequenceDefinition(definition, selectedHero.heroId);
            CloseGroupBox();
            ++renderedSequenceCount;
        }
    }

    if (renderedSequenceCount == 0) {
        UIGroupBox("Combo");
        {
            SettingRow("Selected Hero", kDefaultLabelWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(selectedHero.label);

            SettingRow("Definitions", kDefaultLabelWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextDim), "None");
        }
        CloseGroupBox();
    }

    ImGui::PopID();
}

// =====================================================================
// UI::VisualsPage
// =====================================================================
void UI::VisualsPage() {
    // Player Visual Features -- renderer-backed OW::Config toggles.
    UIGroupBox("Player Visual Features");
    {
        const char* labels[] = {
            "Box", "Hero Name", "BattleTag",
            "Health Color", "Health Bar", "Health Bar 2",
            "Distance", "Ultimate", "Skill Info",
            "Skeleton", "Snapline", "Eye Ray",
            "Radar", "Radar Lines", "FOV Circle",
            "Tracking Deadzone", "Crosshair", "Health Packs", nullptr
        };
        bool* values[] = {
            &OW::Config::draw_info, &OW::Config::name, &OW::Config::drawbattletag,
            &OW::Config::drawhealth, &OW::Config::healthbar, &OW::Config::healthbar2,
            &OW::Config::dist, &OW::Config::ult, &OW::Config::skillinfo,
            &OW::Config::draw_skel, &OW::Config::drawline, &OW::Config::eyeray,
            &OW::Config::radar, &OW::Config::radarline, &OW::Config::draw_fov,
            &OW::Config::drawTrackingDeadzones, &OW::Config::crosscircle, &OW::Config::draw_hp_pack, nullptr
        };
        const float ratios[] = { 1.0f, 1.0f, 1.2f };
        DrawCheckboxGrid3(labels, values, 6, 26.0f, ratios);
    }
    CloseGroupBox();

    // Filters
    UIGroupBox("Filters");
    {
        SettingRow("Max Distance");
        PushControlWidth();
        UISlider("##visMaxDist", &OW::Config::visualMaxDist, 0.0f, 1000.0f, "100 m");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

// =====================================================================
// UI::ThemePage
// =====================================================================
static void DrawThemeGeneralPage() {
    UIGroupBox("ESP Display Position");
    {
        static const char* kDisplayPosition[] = { "Above Head", "Left Side", "Right Side" };
        static const char* kRadarCorner[] = { "Bottom Right", "Bottom Left", "Top Right", "Top Left" };

        auto drawPositionSelect = [](const char* rowLabel, const char* controlId,
                                     bool visualEnabled, int* value,
                                     const char* const items[], int itemCount) {
            SettingRow(rowLabel);
            PushControlWidth();
            if (visualEnabled) {
                if (UISelect(controlId, value, items, itemCount))
                    OW::Config::SaveConfig(OW::Config::ConfigPath());
            } else {
                UIDisabledSelect(controlId);
            }
            ImGui::PopItemWidth();
        };

        drawPositionSelect("Radar Corner", "##radarCorner", OW::Config::radar,
                           &OW::Config::radarCorner, kRadarCorner, IM_ARRAYSIZE(kRadarCorner));
        drawPositionSelect("Ultimate Status", "##ultimateDisplayMode", OW::Config::ult,
                           &OW::Config::ultimateDisplayMode, kDisplayPosition, IM_ARRAYSIZE(kDisplayPosition));
        drawPositionSelect("Skill Cooldowns", "##skillDisplayMode", OW::Config::skillinfo,
                           &OW::Config::skillDisplayMode, kDisplayPosition, IM_ARRAYSIZE(kDisplayPosition));
    }
    CloseGroupBox();

    UIGroupBox("Overlay Colors");
    {
        UIColorEdit("Box Outline", &OW::Config::EnemyCol);
        UIColorEdit("FOV Circle", &OW::Config::fovcol);
        UIColorEdit("Visible Fill", &OW::Config::enargb);
        UIColorEdit("Hidden Fill", &OW::Config::invisnenargb);
        UIColorEdit("Target Fill", &OW::Config::targetargb);
        UIColorEdit("Ally Fill", &OW::Config::allyargb);
    }
    CloseGroupBox();
}

static bool UIFovRingCompactColorEdit(const char* label, ImVec4* value) {
    ImGui::SetNextItemWidth(92.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kColControl);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kColControlHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleColor(ImGuiCol_Border, kColStroke);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    const bool changed = ImGui::ColorEdit4(
        label,
        &value->x,
        ImGuiColorEditFlags_NoLabel |
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_InputRGB);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return changed;
}

static bool UIFovRingStyleRow(OW::Config::FovRingSlotKind kind,
                              int slotIndex,
                              bool slotEnabled,
                              float slotFovDeg) {
    OW::Config::FovRingSlotStyle& style = OW::Config::FovRingStyleFor(kind, slotIndex);
    style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    bool changed = false;
    ImGui::PushID(static_cast<int>(kind) * 100 + slotIndex);

    char slotLabel[32] = {};
    std::snprintf(slotLabel, sizeof(slotLabel), "Slot %d", slotIndex + 1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(slotLabel);

    ImGui::SameLine(74.0f);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(slotEnabled ? kColTextMuted : kColTextDim),
                       "%s %.0f deg",
                       slotEnabled ? "On" : "Off",
                       OW::Config::ClampFovDeg(slotFovDeg));

    ImGui::SameLine(160.0f);
    changed |= UICheckbox("##visible", &style.visible);
    ImGui::SameLine(181.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Show");

    ImGui::SameLine(232.0f);
    changed |= UIFovRingCompactColorEdit("##color", &style.color);

    ImGui::SameLine(334.0f);
    ImGui::SetNextItemWidth(102.0f);
    changed |= UISlider("##thickness", &style.thickness, 0.5f, 6.0f, "1.5 px");

    ImGui::SameLine(448.0f);
    static const char* kLineStyles[] = { "Solid", "Dashed" };
    ImGui::SetNextItemWidth(88.0f);
    changed |= UISelect("##lineStyle", &style.lineStyle, kLineStyles, IM_ARRAYSIZE(kLineStyles));

    ImGui::SameLine(548.0f);
    changed |= UICheckbox("##label", &style.showLabel);
    ImGui::SameLine(569.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Label");

    if (changed)
        style = OW::Config::ClampFovRingStyle(style, kind, slotIndex);

    ImGui::PopID();
    return changed;
}

static void DrawFovRingStyleGroup(OW::Config::FovRingSlotKind kind,
                                  const HeroOption& selectedHero) {
    UIGroupBox("Aim Slot Rings");
    {
        const int slotCount = selectedHero.heroId == 0
            ? OW::Config::kMaxHeroPresetSlots
            : ImClamp(OW::Config::GetHeroAimSlotCount(selectedHero.heroId),
                1,
                OW::Config::kMaxHeroPresetSlots);

        for (int slotIndex = 0; slotIndex < slotCount; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool hasSlot = selectedHero.heroId != 0 &&
                OW::Config::TryGetHeroAimSlot(selectedHero.heroId, slotIndex, slot);
            const bool slotEnabled = hasSlot && slot.enabled;
            const float fovDeg = hasSlot ? slot.preset.fov : OW::Config::kDefaultFovDeg;

            UIFovRingStyleRow(kind, slotIndex, selectedHero.heroId == 0 || slotEnabled, fovDeg);
        }
    }
    CloseGroupBox();
}

static void DrawThemeFovRingsPage() {
    const HeroOption& selectedHero = CurrentHeroOption();
    DrawFovRingStyleGroup(OW::Config::FovRingSlotKind::Aim, selectedHero);
}

void UI::ThemePage() {
    ImGui::PushID("ThemePage");

    state.themeSubTab = ImClamp(state.themeSubTab, 0, kThemeSubTabCount - 1);
    if (state.themeSubTab == 0)
        DrawThemeGeneralPage();
    else
        DrawThemeFovRingsPage();

    ImGui::PopID();
}

// =====================================================================
// UI::MiscPage
// =====================================================================
static void DrawMiscGeneralPage();
static void DrawMiscDiagnosticsPage();
static void DrawMiscBehaviorPage();
static void DrawMiscMethodPage();
static void DrawMiscKmboxPage();
static void DrawMiscScreenPage();

void UI::MiscPage() {
    ImGui::PushID("MiscPage");

    state.miscSubTab = ImClamp(state.miscSubTab, 0, kMiscSubTabCount - 1);
    switch (state.miscSubTab) {
        case 0:
            DrawMiscGeneralPage();
            break;
        case 1:
            DrawMiscDiagnosticsPage();
            break;
        case 2:
            DrawMiscBehaviorPage();
            break;
        case 3:
            DrawMiscMethodPage();
            break;
        case 4:
            DrawMiscKmboxPage();
            break;
        case 5:
            DrawMiscScreenPage();
            break;
        default:
            DrawMiscGeneralPage();
            break;
    }

    ImGui::PopID();
}

// =====================================================================
// Misc page sections
// =====================================================================
static void DrawMiscGeneralPage() {
    UIGroupBox("Config Profile");
    {
        static char profileName[kConfigProfileNameBufferSize] = "";
        static char newProfileName[kConfigProfileNameBufferSize] = "";
        static bool profileNameInitialized = false;

        std::vector<std::string> profiles = EnumerateConfigProfiles();
        std::vector<const char*> profileItems;
        profileItems.reserve(profiles.size());
        for (const std::string& profile : profiles)
            profileItems.push_back(profile.c_str());

        if (!profileNameInitialized) {
            const std::string initialProfile = OW::Config::lastConfigProfile.empty()
                ? OW::Config::configFileName
                : OW::Config::lastConfigProfile;
            const int initialIndex = FindConfigProfileIndex(profiles, initialProfile);
            const std::string selectedInitialProfile = profiles[static_cast<size_t>(initialIndex)];
            CopyConfigProfileName(profileName, selectedInitialProfile);
            if (OW::Config::configFileName != selectedInitialProfile)
                SelectConfigProfile(selectedInitialProfile.c_str(), profileName);
            OW::Config::lastConfigProfile = selectedInitialProfile;
            profileNameInitialized = true;
        }

        SettingRow("Active Profile");
        PushControlWidth();
        int selectedProfileIndex = FindConfigProfileIndex(profiles, OW::Config::configFileName);
        if (UISelect("##configProfile", &selectedProfileIndex,
                     profileItems.data(), static_cast<int>(profileItems.size()))) {
            const std::string selectedProfile = profiles[static_cast<size_t>(selectedProfileIndex)];
            SelectConfigProfile(selectedProfile.c_str(), profileName);
            OW::Config::lastConfigProfile = selectedProfile;
            PersistLastConfigProfile();
        }
        ImGui::PopItemWidth();

        SettingRow("Folder");
        {
            char folderBuffer[MAX_PATH] = {};
            const std::string configDirectory = OW::Config::ConfigDirectoryPath();
            std::snprintf(folderBuffer, sizeof(folderBuffer), "%s", configDirectory.c_str());
            const float openButtonWidth = 54.0f;
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float folderWidth = MaxFloat(
                120.0f,
                ImGui::GetContentRegionAvail().x - openButtonWidth - spacing);
            ImGui::PushItemWidth(folderWidth);
            ImGui::InputText("##configFolder", folderBuffer, IM_ARRAYSIZE(folderBuffer),
                             ImGuiInputTextFlags_ReadOnly);
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Open", ImVec2(openButtonWidth, kControlHeight)))
                ShellExecuteA(nullptr, "open", configDirectory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }

        SettingRow("New Profile");
        {
            const float createButtonWidth = 64.0f;
            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float inputWidth = MaxFloat(
                90.0f,
                ImGui::GetContentRegionAvail().x - createButtonWidth - spacing);
            ImGui::PushItemWidth(inputWidth);
            ImGui::InputText("##newConfigProfile", newProfileName, IM_ARRAYSIZE(newProfileName));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Create", ImVec2(createButtonWidth, kControlHeight))) {
                const std::string normalizedProfile = NormalizeProfileFileName(newProfileName);
                if (CreateConfigProfileFromCurrent(normalizedProfile)) {
                    SelectConfigProfile(normalizedProfile.c_str(), profileName);
                    OW::Config::lastConfigProfile = normalizedProfile;
                    PersistLastConfigProfile();
                    newProfileName[0] = '\0';
                    s_configSaveStatus = "Created config profile";
                } else {
                    s_configSaveStatus = "Config profile already exists";
                }
                s_configSaveStatusUntil = ImGui::GetTime() + 3.0;
            }
        }
    }
    CloseGroupBox();

    UIGroupBox("Menu");
    {
        SettingRow("Toggle Key");
        PushControlWidth();
        int toggleKeyIndex = FindMenuToggleKeyIndex(OW::Config::MenuToggleKey);
        OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        if (UISelect("##menuToggleKey", &toggleKeyIndex, kMenuToggleKeys, IM_ARRAYSIZE(kMenuToggleKeys)))
            OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    UIGroupBox("Application");
    {
        SettingRow("Unleashed");
        if (ImGui::Button("Close UN", ImVec2(96.0f, kControlHeight)))
            g_Overlay.RequestExit();
        ImGui::SameLine();
        const bool reconnectBusy =
            OW::ProcessConnection::IsConnecting() ||
            OW::ProcessConnection::IsReconnectRequested();
        if (reconnectBusy)
            ImGui::BeginDisabled();
        if (ImGui::Button("Reconnect UN", ImVec2(112.0f, kControlHeight)))
            OW::ProcessConnection::RequestReconnect();
        if (reconnectBusy)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Force a fresh target-process PID scan and SDK attach.");

        SettingRow("Runtime Version");
        const bool cnNeProfile = OW::offset::IsCnNeProfile();
        ImGui::TextColored(
            cnNeProfile ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f)
                        : ImVec4(0.45f, 0.75f, 1.0f, 1.0f),
            "%s",
            cnNeProfile ? "NE" : "BZ");
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", OW::offset::ActiveProfileName());

        SettingRow("Process");
        const bool connected = OW::ProcessConnection::IsConnected();
        const bool connecting = OW::ProcessConnection::IsConnecting();
        const ImVec4 statusColor = connected
            ? ImVec4(0.25f, 1.0f, 0.45f, 1.0f)
            : connecting
                ? ImVec4(1.0f, 0.75f, 0.25f, 1.0f)
                : ImVec4(1.0f, 0.45f, 0.35f, 1.0f);
        ImGui::TextColored(statusColor, "%s", OW::ProcessConnection::StatusText().c_str());
        if (connected) {
            ImGui::SameLine();
            ImGui::TextDisabled("PID %d", OW::ProcessConnection::ConnectedPid());
        }
    }
    CloseGroupBox();
}

static void DrawMiscDiagnosticsPage() {
    UIGroupBox("Diagnostics");
    {
        SettingRow("Input Source");
        PushControlWidth();
        int inputSourceUiIndex = InputSourceConfigToUiIndex(OW::Config::inputSource);
        if (UISelect("##inputSource", &inputSourceUiIndex,
                     kInputSource, IM_ARRAYSIZE(kInputSource))) {
            OW::Config::inputSource = InputSourceUiIndexToConfig(inputSourceUiIndex);
        }
        ImGui::PopItemWidth();

        if (OW::Config::inputSource == 3) {
            ImGui::TextColored(ImVec4(0.45f, 0.75f, 1.0f, 1),
                "DMA KeyState reads host Windows VK state (keyboard and mouse)");
            const uint64_t keyStateAddress = KeyState::gafAsyncKeyStateAddr.load();
            if (KeyState::initialized.load() && keyStateAddress != 0) {
                ImGui::TextColored(ImVec4(0.25f, 1.0f, 0.45f, 1),
                    "DMA KeyState ready: build=%lu session=%lu pid=%d addr=0x%llX size=%zu",
                    static_cast<unsigned long>(KeyState::detectedBuild.load()),
                    static_cast<unsigned long>(KeyState::resolvedSessionId.load()),
                    KeyState::keyStateReadPid.load(),
                    static_cast<unsigned long long>(keyStateAddress),
                    KeyState::keyStateByteCount.load());
            } else if (OW::Config::gafAsyncKeyStateOffset == 0) {
                ImGui::TextColored(ImVec4(1, 0.35f, 0.2f, 1),
                    "DMA KeyState auto resolver pending/failed; check Diagnostic Log");
            } else {
                ImGui::TextColored(ImVec4(1, 0.35f, 0.2f, 1),
                    "DMA KeyState manual offset unresolved: 0x%llX",
                    static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset));
            }
        }

        DrawAimHotkeyProbe();
        ImGui::Spacing();

        SettingRow("Fire Recorder");
        if (IsFirePatternRecorderRunning()) {
            if (ImGui::Button("Stop", ImVec2(72.0f, kControlHeight)))
                StopFirePatternRecorder();
        } else {
            if (ImGui::Button("Start", ImVec2(72.0f, kControlHeight)))
                StartFirePatternRecorder();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Records left/right mouse button down/up edges from the current input source for up to 8 seconds.\nPerform one full fire-pattern cycle plus the first press of the next cycle, then press Stop.\nResults are written to unleashed_aim_diag.log and the log overlay.");
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "%s", GetFirePatternRecorderStatus().c_str());

        ImGui::Spacing();
        ImGui::Checkbox("Dry Run (Log Only)", &OW::Config::aimDryRun);
        if (OW::Config::aimDryRun) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "(NO cursor movement)");
        }
        bool showLogOverlay = Diagnostics::IsLogOverlayVisible();
        if (ImGui::Checkbox("Show Log Overlay", &showLogOverlay))
            Diagnostics::SetLogOverlayVisible(showLogOverlay);
        ImGui::SameLine();
        if (ImGui::Button("Clear Log"))
            Diagnostics::ClearLogLines();
        ImGui::Checkbox("Verbose Aim Logs", &OW::Config::aimVerboseLog);
        if (OW::Config::aimVerboseLog) {
            ImGui::TextWrapped("Warning: verbose logging may impact performance");
        }
        SettingRow("Log Interval (ms)");
        PushControlWidth();
        UISlider("##LogInterval", &OW::Config::aimDryRunLogIntervalMs, 50.0f, 1000.0f, "50 ms");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

static void DrawMiscBehaviorPage() {
    UIGroupBox("Behavior");
    {
        static int selectedBehavior = 0;
        selectedBehavior = OW::Config::ClampAimBehaviorIndex(selectedBehavior);

        SettingRow("Behavior");
        PushControlWidth();
        UISelect("##miscBehaviorSelector", &selectedBehavior,
                 kAimBehavior, IM_ARRAYSIZE(kAimBehavior));
        ImGui::PopItemWidth();

        selectedBehavior = OW::Config::ClampAimBehaviorIndex(selectedBehavior);
        const int behaviorIndex = selectedBehavior;
        int& behaviorMethod = OW::Config::aimBehaviorMethod[static_cast<size_t>(behaviorIndex)];
        float& behaviorBaseSpeed = OW::Config::aimBehaviorBaseSpeed[static_cast<size_t>(behaviorIndex)];
        bool& behaviorMoveSplitEnabled = OW::Config::aimBehaviorMoveSplitEnabled[static_cast<size_t>(behaviorIndex)];
        int& behaviorMoveSplitMaxPixels = OW::Config::aimBehaviorMoveSplitMaxPixels[static_cast<size_t>(behaviorIndex)];
        int& behaviorMoveSplitDelayUs = OW::Config::aimBehaviorMoveSplitDelayUs[static_cast<size_t>(behaviorIndex)];
        behaviorMethod = OW::Config::ClampAimMethodIndex(behaviorMethod);
        behaviorMoveSplitMaxPixels = OW::Config::ClampMoveSplitMaxPixels(behaviorMoveSplitMaxPixels);
        behaviorMoveSplitDelayUs = OW::Config::ClampMoveSplitDelayUs(behaviorMoveSplitDelayUs);
        OW::Config::aimMethod = behaviorMethod;

        SettingRow("Method");
        PushControlWidth();
        if (UISelect("##miscBehaviorMethod", &behaviorMethod,
                     kAimMethod, IM_ARRAYSIZE(kAimMethod))) {
            behaviorMethod = OW::Config::ClampAimMethodIndex(behaviorMethod);
            OW::Config::aimMethod = behaviorMethod;
        }
        ImGui::PopItemWidth();

        SettingRow("Base Angular Speed");
        PushControlWidth();
        UISlider("##miscBaseAngularSpeed", &behaviorBaseSpeed, 0.0f, 100.0f, "100");
        ImGui::PopItemWidth();

        SettingRow("Move Split");
        UICheckbox("##miscBehaviorMoveSplit", &behaviorMoveSplitEnabled);

        if (behaviorMoveSplitEnabled) {
            SettingRow("Split Chunk");
            PushControlWidth();
            UISlider("##miscBehaviorMoveSplitMax", &behaviorMoveSplitMaxPixels, 1.0f, 50.0f, "4 px");
            ImGui::PopItemWidth();

            SettingRow("Split Delay");
            PushControlWidth();
            UISlider("##miscBehaviorMoveSplitDelay", &behaviorMoveSplitDelayUs, 0.0f, 10000.0f, "800 us");
            ImGui::PopItemWidth();
        }

        SettingRow("Pitch Scale");
        PushControlWidth();
        UISlider("##miscPitchScale", &OW::Config::aimbotPitchScale, 0.1f, 3.0f, "1.00");
        ImGui::PopItemWidth();

        SettingRow("Overshoot Curve");
        UICheckbox("##miscOvershootCurve", &OW::Config::aimOvershootCurve);

        if (OW::Config::aimOvershootCurve) {
            SettingRow("Overshoot Gain");
            PushControlWidth();
            UISlider("##miscOvershootGain", &OW::Config::aimOvershootGain, 0.0f, 1.0f, "0.25");
            ImGui::PopItemWidth();

            SettingRow("Overshoot Reset");
            PushControlWidth();
            UISlider("##miscOvershootReset", &OW::Config::aimOvershootResetPixels, 1.0f, 250.0f, "56 px");
            ImGui::PopItemWidth();
        }
    }
    CloseGroupBox();
}

static void DrawMiscMethodPage() {
    UIGroupBox("Method");
    {
        static int selectedMethod = 0;
        selectedMethod = OW::Config::ClampAimMethodIndex(selectedMethod);

        SettingRow("Method");
        PushControlWidth();
        UISelect("##miscMethodSelector", &selectedMethod, kAimMethod, IM_ARRAYSIZE(kAimMethod));
        ImGui::PopItemWidth();

        selectedMethod = OW::Config::ClampAimMethodIndex(selectedMethod);
        float& angularSpeedScale = OW::Config::aimMethodAngularSpeedScale[static_cast<size_t>(selectedMethod)];

        auto drawAngularSpeed = [&](const char* id) {
            SettingRow("Angular Speed");
            PushControlWidth();
            UISlider(id, &angularSpeedScale, 0.0f, 200.0f, "100 %");
            ImGui::PopItemWidth();
        };

        switch (selectedMethod) {
        case 0:
            drawAngularSpeed("##methodLinearAngularSpeed");
            break;
        case 1:
            SettingRow("P Gain");
            PushControlWidth();
            UISlider("##methodPidP", &OW::Config::aimPidP, 0.0f, 2.0f, "0.50");
            ImGui::PopItemWidth();

            SettingRow("I Gain");
            PushControlWidth();
            UISlider("##methodPidI", &OW::Config::aimPidI, 0.0f, 0.5f, "0.050");
            ImGui::PopItemWidth();

            SettingRow("D Gain");
            PushControlWidth();
            UISlider("##methodPidD", &OW::Config::aimPidD, 0.0f, 1.0f, "0.10");
            ImGui::PopItemWidth();

            SettingRow("Max Integral");
            PushControlWidth();
            UISlider("##methodPidMaxI", &OW::Config::aimPidMaxIntegral, 1.0f, 50.0f, "10.0");
            ImGui::PopItemWidth();

            SettingRow("Deadzone");
            PushControlWidth();
            UISlider("##methodPidDeadzone", &OW::Config::aimPidDeadzone, 0.0f, 10.0f, "1.0 deg");
            ImGui::PopItemWidth();
            break;
        case 2:
            SettingRow("Control Points");
            PushControlWidth();
            UISlider("##methodBezierControlPoints", &OW::Config::aimBezierControlPoints, 2.0f, 6.0f, "2");
            ImGui::PopItemWidth();

            SettingRow("Curvature");
            PushControlWidth();
            UISlider("##methodBezierCurvature", &OW::Config::aimBezierCurvature, 0.0f, 1.0f, "0.50");
            ImGui::PopItemWidth();

            SettingRow("Curve Speed");
            PushControlWidth();
            UISlider("##methodBezierSpeed", &OW::Config::aimBezierSpeed, 1.0f, 200.0f, "50.0");
            ImGui::PopItemWidth();
            break;
        case 3:
            drawAngularSpeed("##methodPiecewiseAngularSpeed");

            SettingRow("Near Degrees");
            PushControlWidth();
            UISlider("##methodPiecewiseNearDegrees", &OW::Config::aimPiecewiseNearDegrees, 0.0f, 30.0f, "2.0 deg");
            ImGui::PopItemWidth();

            OW::Config::aimPiecewiseMidDegrees = (std::max)(
                OW::Config::aimPiecewiseMidDegrees,
                OW::Config::AimPiecewiseNearDegrees());
            SettingRow("Mid Degrees");
            PushControlWidth();
            UISlider("##methodPiecewiseMidDegrees", &OW::Config::aimPiecewiseMidDegrees,
                     OW::Config::AimPiecewiseNearDegrees(), 45.0f, "6.0 deg");
            ImGui::PopItemWidth();

            OW::Config::aimPiecewiseFarDegrees = (std::max)(
                OW::Config::aimPiecewiseFarDegrees,
                OW::Config::AimPiecewiseMidDegrees());
            SettingRow("Far Degrees");
            PushControlWidth();
            UISlider("##methodPiecewiseFarDegrees", &OW::Config::aimPiecewiseFarDegrees,
                     OW::Config::AimPiecewiseMidDegrees(), 60.0f, "12.0 deg");
            ImGui::PopItemWidth();

            SettingRow("Near Scale");
            PushControlWidth();
            UISlider("##methodPiecewiseNearScale", &OW::Config::aimPiecewiseNearScale, 0.0f, 1.0f, "0.20");
            ImGui::PopItemWidth();

            SettingRow("Mid Scale");
            PushControlWidth();
            UISlider("##methodPiecewiseMidScale", &OW::Config::aimPiecewiseMidScale, 0.0f, 1.0f, "0.45");
            ImGui::PopItemWidth();

            SettingRow("Far Scale");
            PushControlWidth();
            UISlider("##methodPiecewiseFarScale", &OW::Config::aimPiecewiseFarScale, 0.0f, 1.0f, "0.75");
            ImGui::PopItemWidth();
            break;
        case 4:
            drawAngularSpeed("##methodAccelAngularSpeed");

            SettingRow("Acceleration");
            PushControlWidth();
            UISlider("##methodAccelAcceleration", &OW::Config::aimAccelLimitedAcceleration, 0.0f, 20.0f, "0.10");
            ImGui::PopItemWidth();
            break;
        case 5:
            SettingRow("Angular Speed");
            PushControlWidth();
            UISlider("##methodConstantAngularSpeed", &OW::Config::aimConstantAngularSpeedDeg,
                     0.0f, 720.0f, "30 deg/s");
            ImGui::PopItemWidth();
            break;
        default:
            break;
        }
    }
    CloseGroupBox();
}

static void DrawMiscKmboxPage() {
    UIGroupBox("KMBox Settings");
    {
        bool kmboxSaveRequested = false;
        static bool kmboxConnectionTestOk = false;
        static std::string kmboxConnectionTestMessage;
        static bool kmboxNetworkRestartOk = false;
        static std::string kmboxNetworkRestartMessage;
        static bool kmboxFirewallOk = false;
        static std::string kmboxFirewallMessage;
        ImGui::PushID("KMBoxSettings");

        SettingRow("Enable KMBox");
        const bool wasKmboxEnabled = OW::Config::kmboxEnabled;
        if (ImGui::Checkbox("##Enable", &OW::Config::kmboxEnabled)) {
            kmboxSaveRequested = true;
            kmboxNetworkRestartMessage.clear();
            kmboxFirewallMessage.clear();
            if (OW::Config::kmboxEnabled && !wasKmboxEnabled) {
                const KmboxConnectionTestResult initResult = InitializeKmboxFromCurrentConfig();
                kmboxConnectionTestOk = initResult.ok;
                kmboxConnectionTestMessage = initResult.message;
            } else if (!OW::Config::kmboxEnabled && wasKmboxEnabled) {
                kmboxConnectionTestOk = true;
                kmboxConnectionTestMessage = "Disabled";
                kmbox::ReleaseTimerResolution();
            }
        }
        if (!kmboxConnectionTestMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                kmboxConnectionTestOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                kmboxConnectionTestMessage.c_str());
        }

        SettingRow("Device Type");
        PushControlWidth();
        kmboxSaveRequested |= ImGui::Combo("##DeviceType", &OW::Config::kmboxDeviceType,
                                           kKmBoxDeviceTypes, IM_ARRAYSIZE(kKmBoxDeviceTypes));
        ImGui::PopItemWidth();

        if (OW::Config::kmboxDeviceType == 0) {
            SettingRow("IP");
            PushControlWidth();
            ImGui::InputText("##Ip", OW::Config::kmboxIp, IM_ARRAYSIZE(OW::Config::kmboxIp));
            kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopItemWidth();

            SettingRow("Port");
            PushControlWidth();
            ImGui::InputInt("##Port", &OW::Config::kmboxPort, 0, 0);
            kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopItemWidth();

            SettingRow("Monitor Port");
            PushControlWidth();
            ImGui::InputInt("##MonitorPort", &OW::Config::kmboxMonitorPort, 0, 0);
            kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopItemWidth();

            SettingRow("MAC");
            PushControlWidth();
            ImGui::InputText("##Mac", OW::Config::kmboxMac, IM_ARRAYSIZE(OW::Config::kmboxMac));
            kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopItemWidth();
        } else {
            SettingRow("COM Port");
            PushControlWidth();
            ImGui::InputText("##ComPort", OW::Config::kmboxComPort, IM_ARRAYSIZE(OW::Config::kmboxComPort));
            kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::PopItemWidth();
        }

        SettingRow("Base Counts/Rad");
        PushControlWidth();
        ImGui::InputFloat("##CountsPerRadian", &OW::Config::kmboxCountsPerRadian,
                          1.0f, 10.0f, "%.1f");
        kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Manual KMBox relative mouse counts per radian. Calibration overrides this value until calibration is cleared.");
        ImGui::PopItemWidth();

        SettingRow("Current Game Sens");
        PushControlWidth();
        ImGui::InputFloat("##GameSens", &OW::Config::gameMouseSensitivity,
                          0.0f, 0.0f, "%.2f");
        kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Manual current in-game sensitivity. DMA sensitivity reading is not used for this value.");
        ImGui::PopItemWidth();

        SettingRow("Host DPI (DMA)");
        PushControlWidth();
        float detectedHostDpi = OW::Config::hostMouseDpiAutoDetected
            ? OW::Config::detectedHostMouseDpi
            : 0.0f;
        ImGui::InputFloat("##HostDpiDisplay", &detectedHostDpi, 0.0f, 0.0f, "%.0f",
                          ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow("Mouse DPI");
        PushControlWidth();
        ImGui::InputFloat("##MouseDpi", &OW::Config::hostMouseDpi,
                          0.0f, 0.0f, "%.0f");
        kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopItemWidth();

        SettingRow("Reference Game Sens");
        const float useCurrentButtonWidth = 88.0f;
        const float useCurrentSpacing = ImGui::GetStyle().ItemInnerSpacing.x;
        const float referenceWidth = MaxFloat(
            80.0f,
            ImGui::GetContentRegionAvail().x - useCurrentButtonWidth - useCurrentSpacing);
        ImGui::PushItemWidth(referenceWidth);
        ImGui::InputFloat("##ReferenceGameSens", &OW::Config::referenceGameSensitivity,
                          0.0f, 0.0f, "%.2f");
        kmboxSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The in-game sensitivity used when Base Counts/Rad or calibration was measured.");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        if (ImGui::Button("Use Current", ImVec2(useCurrentButtonWidth, kControlHeight))) {
            OW::Config::referenceGameSensitivity = OW::Config::gameMouseSensitivity;
            kmboxSaveRequested = true;
        }

        SettingRow("Auto Sens Scale");
        kmboxSaveRequested |= UICheckbox("##AutoScaleGameSens", &OW::Config::autoScaleByGameSensitivity);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Scales KMBox counts by reference game sensitivity divided by current game sensitivity.");

        SettingRow("Input Delay (ms)");
        PushControlWidth();
        kmboxSaveRequested |= UISlider("##InputDelay", &OW::Config::kmboxInputDelayMs,
                                       0.0f, 20.0f, "0 ms");
        ImGui::PopItemWidth();

        SettingRow("Debug Logging");
        kmboxSaveRequested |= ImGui::Checkbox("##Debug", &OW::Config::kmboxDebugLog);

        // ---- Counts-per-radian auto-calibration button ----
        SettingRow("Counts Calibration");
        {
            const bool wasCalibrated = OW::Config::calibratedCountsPerRadian > 0.0f;
            if (ImGui::Button(OW::Config::calibrationInProgress ? "Calibrating..." : "Calibrate", ImVec2(72.0f, kControlHeight))) {
                if (!OW::Config::calibrationInProgress) {
                    OW::CalibrateSensitivity();
                    kmboxSaveRequested = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Sends a known KMBox movement and measures the view angle change to compute actual counts per radian.\nRequires the game to be running and in a state where mouse movement changes view angles.\nWarning: This will move the remote machine's mouse cursor!");
            ImGui::SameLine();
            if (wasCalibrated) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "Yaw: %.1f c/rad @ %.2f",
                    OW::Config::calibratedCountsPerRadian,
                    OW::Config::referenceGameSensitivity);
                if (OW::Config::calibratedPitchCountsPerRadian > 0.0f) {
                    std::snprintf(buf, sizeof(buf), "Yaw: %.1f / Pitch: %.1f c/rad @ %.2f",
                        OW::Config::calibratedCountsPerRadian,
                        OW::Config::calibratedPitchCountsPerRadian,
                        OW::Config::referenceGameSensitivity);
                }
                ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.45f, 1.0f), "%s", buf);
            } else {
                ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Not calibrated");
            }
        }

        SettingRow("KMBox Move Stress");
        if (ImGui::Button("Run", ImVec2(72.0f, kControlHeight))) {
            RunKmboxMoveTest();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Sends a rapid square mouse-movement sequence through the current KMBox device.\nNo sleep is added between move commands; results are printed to the console and aim diagnostics log.");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Console + aim log");

        SettingRow("Connection Test");
        if (ImGui::Button("Test", ImVec2(72.0f, kControlHeight))) {
            const KmboxConnectionTestResult testResult = TestKmboxConnection();
            kmboxConnectionTestOk = testResult.ok;
            kmboxConnectionTestMessage = testResult.message;
        }
        if (OW::Config::kmboxDeviceType == 0) {
            ImGui::SameLine();
            if (ImGui::Button("Restart NIC", ImVec2(104.0f, kControlHeight))) {
                const KmboxConnectionTestResult restartResult = RestartKmboxNetworkAdapter();
                kmboxNetworkRestartOk = restartResult.ok;
                kmboxNetworkRestartMessage = restartResult.message;
            }
        }
        if (!kmboxConnectionTestMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(
                kmboxConnectionTestOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                kmboxConnectionTestMessage.c_str());
        }
        if (!kmboxNetworkRestartMessage.empty()) {
            ImGui::TextColored(
                kmboxNetworkRestartOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                      : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                "%s",
                kmboxNetworkRestartMessage.c_str());
        }
        if (OW::Config::kmboxDeviceType == 0) {
            SettingRow("Monitor Firewall");
            if (ImGui::Button("Fix Firewall", ImVec2(112.0f, kControlHeight))) {
                const KmboxConnectionTestResult firewallResult = AllowKmboxMonitorFirewall();
                kmboxFirewallOk = firewallResult.ok;
                kmboxFirewallMessage = firewallResult.message;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Adds an elevated inbound UDP allow rule for this executable and disables matching block rules on the KMBox monitor port.");
            if (!kmboxFirewallMessage.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(
                    kmboxFirewallOk ? ImVec4(0.30f, 0.90f, 0.45f, 1.0f)
                                    : ImVec4(1.00f, 0.28f, 0.28f, 1.0f),
                    "%s",
                    kmboxFirewallMessage.c_str());
            }
        }

        if (kmboxSaveRequested)
            OW::Config::SaveConfig(OW::Config::ConfigPath());

        ImGui::PopID();
    }
    CloseGroupBox();
}

static void DrawMiscScreenPage() {
    UIGroupBox("Target Screen");
    {
        bool screenChanged = false;
        bool screenSaveRequested = false;
        int localWidth = GetSystemMetrics(SM_CXSCREEN);
        int localHeight = GetSystemMetrics(SM_CYSCREEN);
        RECT canvasRect = {};
        if (HWND canvasWindow = g_Overlay.GetOverlayWindow();
            canvasWindow && GetWindowRect(canvasWindow, &canvasRect)) {
            const int canvasWidth = canvasRect.right - canvasRect.left;
            const int canvasHeight = canvasRect.bottom - canvasRect.top;
            if (canvasWidth > 0 && canvasHeight > 0) {
                localWidth = canvasWidth;
                localHeight = canvasHeight;
            }
        }
        int detectedWidth = OW::detectedScreenWidth;
        int detectedHeight = OW::detectedScreenHeight;

        SettingRow("Local Width");
        PushControlWidth();
        ImGui::InputInt("##localScreenWidth", &localWidth, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow("Local Height");
        PushControlWidth();
        ImGui::InputInt("##localScreenHeight", &localHeight, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow("DMA Width");
        PushControlWidth();
        ImGui::InputInt("##detectedScreenWidth", &detectedWidth, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow("DMA Height");
        PushControlWidth();
        ImGui::InputInt("##detectedScreenHeight", &detectedHeight, 0, 0,
                        ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        SettingRow("Fallback Width");
        PushControlWidth();
        screenChanged |= ImGui::InputInt("##manualScreenWidth", &OW::Config::manualScreenWidth, 0, 0);
        screenSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        if (OW::Config::manualScreenWidth < 0)
            OW::Config::manualScreenWidth = 0;
        ImGui::PopItemWidth();

        SettingRow("Fallback Height");
        PushControlWidth();
        screenChanged |= ImGui::InputInt("##manualScreenHeight", &OW::Config::manualScreenHeight, 0, 0);
        screenSaveRequested |= ImGui::IsItemDeactivatedAfterEdit();
        if (OW::Config::manualScreenHeight < 0)
            OW::Config::manualScreenHeight = 0;
        ImGui::PopItemWidth();

        if (screenChanged)
            OW::RefreshScreenSizeFromConfig();
        if (screenSaveRequested)
            OW::Config::SaveConfig(OW::Config::ConfigPath());
    }
    CloseGroupBox();
}

// =====================================================================
// UI::Render  --  Main entry point, called each frame.
//                 Draws the top bar, sub-tabs, and page body.
// =====================================================================
void UI::Render() {
    EnsurePreNewFrameInitHook();
    if (!state.initialized)
        InitStyle();

    SyncSelectedTypeWithDetectedHero();

    if (!OW::Config::Menu)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    if (!ImGui::Begin("Unleashed##panel", nullptr,
                      ImGuiWindowFlags_NoDecoration |
                      ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    auto* dl = ImGui::GetWindowDrawList();

    ImVec2 shellMin = viewport->Pos;
    float shellWidth = kShellWidth;
    const float desiredShellHeight = CurrentShellHeight();
    float shellHeight = desiredShellHeight;
    if (viewport->Size.y > 0.0f)
        shellHeight = MinFloat(shellHeight, viewport->Size.y);
    s_desiredMenuClientSize = { shellWidth, desiredShellHeight };

    ImVec2 shellMax(shellMin.x + shellWidth, shellMin.y + shellHeight);
    dl->AddRectFilled(shellMin, shellMax, kColShell0);
    dl->AddRectFilledMultiColor(shellMin, shellMax,
                                kColShell1,
                                kColShell1,
                                kColShell2,
                                kColShell2);
    dl->AddRectFilled(ImVec2(shellMin.x + kShellBorder, shellMin.y + kShellBorder),
                      ImVec2(shellMax.x - kShellBorder, shellMax.y - kShellBorder),
                      IM_COL32(0x12, 0x13, 0x17, 0xF4));

    ImVec2 winPos(shellMin.x + kShellBorder, shellMin.y + kShellBorder);
    ImVec2 contentMax(shellMax.x - kShellBorder, shellMax.y - kShellBorder);
    float  winW = shellWidth - kShellBorder * 2.0f;

    // ==================================================================
    // TOP BAR
    // ==================================================================
    {
        ImRect headerRect(winPos, ImVec2(winPos.x + winW, winPos.y + kHeaderHeight));
        dl->AddRectFilled(headerRect.Min, headerRect.Max, kColPanel);
        dl->AddRectFilledMultiColor(headerRect.Min, headerRect.Max,
                                    IM_COL32(0x18, 0x19, 0x1e, 0xFF),
                                    IM_COL32(0x15, 0x16, 0x1a, 0xFF),
                                    IM_COL32(0x11, 0x12, 0x16, 0xFF),
                                    IM_COL32(0x14, 0x15, 0x19, 0xFF));
        dl->AddRectFilled(ImVec2(headerRect.Min.x, headerRect.Max.y - 1.0f),
                          headerRect.Max, kColAccent);
        dl->AddLine(ImVec2(headerRect.Min.x + 10.0f, headerRect.Min.y + 4.0f),
                    ImVec2(headerRect.Max.x - 10.0f, headerRect.Min.y + 4.0f),
                    IM_COL32(0xe4, 0x11, 0x43, 0x2C), 1.0f);

        ImVec2 brandPos(winPos.x + 14.0f, winPos.y + 10.0f);
        if (s_logoTexture) {
            dl->AddImage(reinterpret_cast<ImTextureID>(s_logoTexture),
                         brandPos,
                         ImVec2(brandPos.x + kBrandLogoDrawSize, brandPos.y + kBrandLogoDrawSize));
        }

        const float selectorW = 172.0f;
        const float configButtonW = 96.0f;
        const float actionGap = 6.0f;

        const char* title = "UNLEASHED";
        ImFont* titleFont = s_titleFont ? s_titleFont : s_boldFont;
        if (titleFont)
            ImGui::PushFont(titleFont);
        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        if (titleFont)
            ImGui::PopFont();
        const float titleX = brandPos.x + kBrandLogoDrawSize + kBrandTitleGap;
        const float titleY = brandPos.y + (kBrandLogoDrawSize - titleSize.y) * 0.5f;
        DrawText(dl, titleFont,
                 ImVec2(titleX, titleY),
                 kColText, title);

        const float selectorStartX = titleX + titleSize.x + 24.0f;
        const float configButtonX = headerRect.Max.x - configButtonW - 12.0f;
        const float selectorX = MinFloat(selectorStartX, configButtonX - selectorW - actionGap);
        const HeroOption& selectedHero = CurrentHeroOption();
        const bool savesSelectedHero = IsConcreteHeroSelection(selectedHero);
        ImGui::SetCursorScreenPos(ImVec2(selectorX, headerRect.Min.y + 11.0f));
        if (TypeSelectorButton(selectedHero, ImVec2(selectorW, 26.0f)))
            ImGui::OpenPopup("TypePickerPopup");
        ShowAimSlotSummaryTooltip(selectedHero.heroId != 0);

        ImGui::SetCursorScreenPos(ImVec2(configButtonX, headerRect.Min.y + 11.0f));
        if (ImGui::Button("Save Config", ImVec2(configButtonW, 26.0f))) {
            s_configSaveStatus = SaveSelectedConfig();
            s_configSaveStatusUntil = ImGui::GetTime() + 3.0;
        }
        ShowSaveConfigTooltip(savesSelectedHero);

        const double now = ImGui::GetTime();
        if (!s_configSaveStatus.empty() && now >= s_configSaveStatusUntil)
            s_configSaveStatus.clear();
        if (!s_configSaveStatus.empty()) {
            const ImVec2 statusSize = ImGui::CalcTextSize(s_configSaveStatus.c_str());
            const float statusMinX = selectorX + selectorW + 14.0f;
            const float statusMaxX = configButtonX - 10.0f;
            if (statusMinX < statusMaxX) {
                const float statusX = MaxFloat(statusMinX, statusMaxX - statusSize.x);
                const float statusY = titleY + (titleSize.y - statusSize.y) * 0.5f;
                DrawText(dl, nullptr, ImVec2(statusX, statusY),
                         kColTextMuted, s_configSaveStatus.c_str());
            }
        }
        TypePickerPanel();

        // Top tab bar at the bottom of the header
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 20.0f, winPos.y + kHeaderHeight - 43.0f));

        const char* topTabNames[] = { "Aiming", "Visuals", "Theme", "Misc" };
        for (int i = 0; i < IM_ARRAYSIZE(topTabNames); i++) {
            bool isActive = (state.activeTab == i);

            ImVec2 tabPos = ImGui::GetCursorScreenPos();
            float tabW = 109.0f;

            ImGui::PushID(i);
            ImGui::InvisibleButton("##topTab", ImVec2(tabW, 43.0f));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                state.activeTab = (Tab)i;
            ImGui::PopID();

            float tabT = VisualTransition(ImGui::GetID(topTabNames[i]) ^ 0x2261,
                                           isActive || hovered, 14.0f);
            if (isActive) {
                dl->AddRectFilled(ImVec2(tabPos.x + 1.0f, tabPos.y + 4.0f),
                                  ImVec2(tabPos.x + tabW - 3.0f, tabPos.y + 42.0f),
                                  IM_COL32(0x1a, 0x1d, 0x22, 0xFF), 0.0f);
                dl->AddRectFilled(ImVec2(tabPos.x + 14.0f, tabPos.y + 40.0f),
                                  ImVec2(tabPos.x + tabW - 14.0f, tabPos.y + 42.0f),
                                  kColAccent, 0.0f);
            } else if (hovered) {
                dl->AddRectFilled(ImVec2(tabPos.x + 1.0f, tabPos.y + 6.0f),
                                  ImVec2(tabPos.x + tabW - 3.0f, tabPos.y + 40.0f),
                                  MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                           IM_COL32(0x1f, 0x22, 0x27, 0xB0), tabT),
                                  0.0f);
            }

            ImU32 txtCol = isActive
                ? kColAccent
                : MixColor(kColTextMuted, kColText, tabT);

            ImVec2 txtSize = ImGui::CalcTextSize(topTabNames[i]);
            DrawText(dl, isActive ? s_boldFont : nullptr,
                     ImVec2(tabPos.x + 36.0f, tabPos.y + (43.0f - txtSize.y) * 0.5f),
                     txtCol, topTabNames[i]);

            DrawTopTabIcon(dl, i, ImVec2(tabPos.x + 14.0f, tabPos.y + 12.5f), txtCol);

            ImGui::SetCursorScreenPos(ImVec2(tabPos.x + tabW, tabPos.y));
        }
    }

    // ==================================================================
    // CONTENT BAND (sub-tab bar + body area)
    // ==================================================================
    float contentBandY = winPos.y + kHeaderHeight;

    // Sub-tabs & page body content (drawn inside a child region)
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, contentBandY));

    // Determine which sub-tabs to show
    const char* subTabNames[6] = { nullptr };
    int subTabCount = 0;
    int* activeSub  = nullptr;

    switch (state.activeTab) {
        case TAB_AIMING:
            subTabNames[0] = "Aim";
            subTabNames[1] = "Trigger";
            subTabNames[2] = "Skills";
            subTabNames[3] = "Combo";
            subTabCount = kAimingSubTabCount;
            state.aimingSubTab = ImClamp(state.aimingSubTab, 0, subTabCount - 1);
            activeSub   = &state.aimingSubTab;
            break;
        case TAB_VISUALS:
            subTabNames[0] = "Players";
            subTabCount = 1;
            activeSub   = &state.visualsSubTab;
            break;
        case TAB_THEME:
            subTabNames[0] = "General";
            subTabNames[1] = "Aim FOV";
            subTabCount = kThemeSubTabCount;
            state.themeSubTab = ImClamp(state.themeSubTab, 0, subTabCount - 1);
            activeSub   = &state.themeSubTab;
            break;
        case TAB_MISC:
            subTabNames[0] = "General";
            subTabNames[1] = "Diagnostics";
            subTabNames[2] = "Behavior";
            subTabNames[3] = "Method";
            subTabNames[4] = "KMBox";
            subTabNames[5] = "Screen";
            subTabCount = kMiscSubTabCount;
            state.miscSubTab = ImClamp(state.miscSubTab, 0, subTabCount - 1);
            activeSub   = &state.miscSubTab;
            break;
    }

    float subBarHeight = subTabCount > 0 ? CurrentSubBarHeight() : 0.0f;

    ImRect subBarRect(ImVec2(winPos.x, contentBandY),
                      ImVec2(winPos.x + winW, contentBandY + subBarHeight));
    if (subBarHeight > 0.0f) {
        dl->AddRectFilled(subBarRect.Min, subBarRect.Max, IM_COL32(0x17, 0x1a, 0x1d, 0xF6));
        dl->AddLine(ImVec2(subBarRect.Min.x, subBarRect.Max.y - 1.0f),
                    ImVec2(subBarRect.Max.x, subBarRect.Max.y - 1.0f),
                    IM_COL32(0x07, 0x08, 0x0a, 0xC8), 1.0f);
    }

    // Draw sub-tab buttons
    if (subTabCount > 0 && activeSub) {
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 22.0f, contentBandY));
        for (int i = 0; i < subTabCount; i++) {
            bool isActive = (*activeSub == i);
            ImVec2 pos = ImGui::GetCursorScreenPos();
            ImVec2 subTextSize = ImGui::CalcTextSize(subTabNames[i]);
            const float subTabW = MaxFloat(60.0f, subTextSize.x + 16.0f);

            ImGui::PushID(i + 10);
            ImGui::InvisibleButton("##subTab", ImVec2(subTabW, subBarHeight));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                *activeSub = i;
            ImGui::PopID();

            float subT = VisualTransition(ImGui::GetID(subTabNames[i]) ^ 0x2c91,
                                           isActive || hovered, 16.0f);
            ImVec2 subTextPos(pos.x + 6.0f, pos.y + (subBarHeight - subTextSize.y) * 0.5f);
            ImU32 col = isActive
                ? kColText
                : MixColor(kColTextDim, kColTextMuted, subT);
            dl->AddText(subTextPos, col, subTabNames[i]);
            if (isActive) {
                dl->AddRectFilled(ImVec2(subTextPos.x, subBarRect.Max.y - 3.0f),
                                  ImVec2(subTextPos.x + subTextSize.x, subBarRect.Max.y - 1.0f),
                                  kColAccent, 1.0f);
            }

            ImGui::SetCursorScreenPos(ImVec2(pos.x + subTabW + 20.0f, pos.y));
        }
    }

    // ==================================================================
    // PAGE BODY
    // ==================================================================
    float bodyTopPad = CurrentBodyTopPad();
    float bodyY = contentBandY + subBarHeight + bodyTopPad;
    float bodyH = MaxFloat(0.0f, contentMax.y - bodyY);
    const bool pageBodyNeedsVerticalScroll =
        s_measuredBodyHeightByPage[CurrentPageKey()] > bodyH + 1.0f;
    ImGuiWindowFlags pageBodyFlags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration;
    if (pageBodyNeedsVerticalScroll)
        pageBodyFlags |= ImGuiWindowFlags_AlwaysVerticalScrollbar;

    // Begin a child region for scrollable content.
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, bodyY));
    ImGui::BeginChild("PageBody", ImVec2(winW, bodyH), false, pageBodyFlags);
    const float bodyCursorStartY = ImGui::GetCursorPosY();

    // Apply page-body padding.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::Indent(11.0f);

    // Render the active page
    if (state.activeTab == TAB_AIMING) {
        if (state.aimingSubTab == 0)
            AimbotPage();
        else if (state.aimingSubTab == 1)
            TriggerPage();
        else if (state.aimingSubTab == 2)
            SkillsPage();
        else
            SequencesPage();
    } else if (state.activeTab == TAB_VISUALS) {
        VisualsPage();
    } else if (state.activeTab == TAB_THEME) {
        ThemePage();
    } else if (state.activeTab == TAB_MISC) {
        MiscPage();
    }

    // Close any remaining open group box
    CloseGroupBox();

    ImGui::Unindent(11.0f);
    const float measuredBodyHeight =
        ImGui::GetCursorPosY() - bodyCursorStartY + kBodyBottomPadding;
    UpdateDesiredMenuClientSize(measuredBodyHeight);
    ImGui::EndChild();

    dl->AddRect(shellMin, shellMax, IM_COL32(0x02, 0x03, 0x06, 0xFF), 0.0f, 0, 2.0f);
    dl->AddRect(ImVec2(shellMin.x + 2.0f, shellMin.y + 2.0f),
                ImVec2(shellMax.x - 2.0f, shellMax.y - 2.0f),
                IM_COL32(0x30, 0x32, 0x3a, 0xD0));
    dl->AddRect(ImVec2(shellMin.x + 3.0f, shellMin.y + 3.0f),
                ImVec2(shellMax.x - 3.0f, shellMax.y - 3.0f),
                IM_COL32(0xff, 0xff, 0xff, 0x08));

    ImGui::SetCursorScreenPos(shellMin);
    ImGui::Dummy(ImVec2(shellWidth, shellHeight));

    ImGui::End();
}
