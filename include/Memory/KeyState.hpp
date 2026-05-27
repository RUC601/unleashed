#pragma once

#include "Memory/Memory.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace KeyState {

    inline constexpr DWORD kSystemPid = 4;

    // 256 VK codes, 1 byte each. Some builds expose a compact 64-byte bitmap;
    // keyStateByteCount selects the decoding mode.
    inline std::array<uint8_t, 256> keyStateBitmap{};
    inline std::mutex keyStateMutex;
    inline std::atomic<bool> initialized{ false };
    inline std::atomic<bool> running{ false };
    inline std::atomic<uint64_t> gafAsyncKeyStateAddr{ 0 };

    inline int pollIntervalMs = 10;
    inline std::string kernelModuleName = "win32kbase.sys";
    inline uint64_t gafAsyncKeyStateOffset = 0;
    inline std::atomic<size_t> keyStateByteCount{ keyStateBitmap.size() };
    inline std::atomic<int> keyStateReadPid{ static_cast<int>(kSystemPid) };
    inline std::atomic<DWORD> detectedBuild{ 0 };
    inline std::atomic<DWORD> resolvedSessionId{ 0 };
    inline std::atomic<uint64_t> resolvedModuleBase{ 0 };
    inline std::atomic<uint64_t> resolvedSlotsRva{ 0 };
    inline std::atomic<uint64_t> resolvedKeyStateOffset{ 0 };
    inline std::atomic<bool> resolvedViaAutoTable{ false };

    struct KeyboardProxyProcess {
        DWORD pid = 0;
        DWORD sessionId = 0;
        std::string name;
    };

    struct SessionSlotsProfile {
        DWORD minBuild = 0;
        const char* label = "";
        const char* moduleName = "";
        uint64_t slotsRva = 0;
        uint64_t keyStateOffset = 0;
    };

    inline bool EqualsNoCase(const char* lhs, const char* rhs)
    {
        if (!lhs || !rhs)
            return false;

        while (*lhs && *rhs) {
            const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(*lhs)));
            const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(*rhs)));
            if (a != b)
                return false;
            ++lhs;
            ++rhs;
        }
        return *lhs == '\0' && *rhs == '\0';
    }

    inline int KeyboardProxyPriority(const char* name)
    {
        if (EqualsNoCase(name, "explorer.exe"))
            return 0;
        if (EqualsNoCase(name, "dwm.exe"))
            return 1;
        if (EqualsNoCase(name, "winlogon.exe"))
            return 2;
        if (EqualsNoCase(name, "taskhostw.exe"))
            return 3;
        if (EqualsNoCase(name, "smartscreen.exe"))
            return 4;
        return 100;
    }

    inline DWORD QueryWindowsBuild()
    {
        if (!mem.vHandle)
            return 0;

        ULONG64 build = 0;
        if (!VMMDLL_ConfigGet(mem.vHandle, VMMDLL_OPT_WIN_VERSION_BUILD, &build))
            return 0;

        detectedBuild.store(static_cast<DWORD>(build), std::memory_order_release);
        return static_cast<DWORD>(build);
    }

    inline const SessionSlotsProfile* SelectSessionSlotsProfile(DWORD build)
    {
        static constexpr SessionSlotsProfile profiles[] = {
            // 25H2+ / build 26200+ per current memflow fallback table.
            { 26200, "Win11 25H2+", "win32k.sys", 0x86678, 0x3808 },
            // 24H2 / build 26100. memflow treats the win32k.sys path as >= 22632.
            { 22632, "Win11 24H2", "win32k.sys", 0x824F0, 0x3808 },
            // 22H2/23H2 builds keep the session slot path in win32ksgd.sys.
            { 22621, "Win11 22H2/23H2", "win32ksgd.sys", 0x3110, 0x36A8 },
        };

        for (const auto& profile : profiles) {
            if (build >= profile.minBuild)
                return &profile;
        }
        return nullptr;
    }

    inline bool FindKeyboardProxyProcess(KeyboardProxyProcess& proxy)
    {
        proxy = {};
        if (!mem.vHandle)
            return false;

        const DWORD desiredSession = OW::Config::gafAsyncKeyStateSessionId > 0
            ? static_cast<DWORD>(OW::Config::gafAsyncKeyStateSessionId)
            : 0;

        PVMMDLL_PROCESS_INFORMATION processInfo = nullptr;
        DWORD processCount = 0;
        if (!VMMDLL_ProcessGetInformationAll(mem.vHandle, &processInfo, &processCount))
            return false;

        int bestPriority = 1000;
        for (DWORD i = 0; i < processCount; ++i) {
            const auto& info = processInfo[i];
            if (info.dwPID == 0)
                continue;

            const char* name = info.szNameLong[0] ? info.szNameLong : info.szName;
            const int priority = KeyboardProxyPriority(name);
            if (priority >= 100)
                continue;

            const DWORD sessionId = info.win.dwSessionId;
            if (desiredSession != 0 && sessionId != desiredSession)
                continue;

            const bool better =
                proxy.pid == 0 ||
                priority < bestPriority ||
                (priority == bestPriority && desiredSession == 0 && sessionId > proxy.sessionId);

            if (better) {
                proxy.pid = info.dwPID;
                proxy.sessionId = sessionId;
                proxy.name = name;
                bestPriority = priority;
            }
        }

        VMMDLL_MemFree(processInfo);
        return proxy.pid != 0;
    }

    inline bool ReadU64(DWORD pid, uint64_t address, uint64_t& value)
    {
        value = 0;
        return mem.Read(static_cast<uintptr_t>(address), &value, sizeof(value), static_cast<int>(pid));
    }

    inline uint64_t ResolveKernelModuleBaseForPid(DWORD pid, const std::string& moduleName)
    {
        if (!mem.vHandle || moduleName.empty())
            return 0;

        std::string narrowName = moduleName;
        uint64_t base = VMMDLL_ProcessGetModuleBaseU(
            mem.vHandle,
            pid,
            const_cast<LPSTR>(narrowName.c_str()));
        if (base)
            return base;

        PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
        if (VMMDLL_Map_GetModuleFromNameU(
                mem.vHandle,
                pid,
                const_cast<LPSTR>(narrowName.c_str()),
                &moduleEntry,
                VMMDLL_MODULE_FLAG_NORMAL) &&
            moduleEntry) {
            base = moduleEntry->vaBase;
            VMMDLL_MemFree(moduleEntry);
            return base;
        }
        if (moduleEntry)
            VMMDLL_MemFree(moduleEntry);

        std::wstring wideName(narrowName.begin(), narrowName.end());
        moduleEntry = nullptr;
        if (VMMDLL_Map_GetModuleFromNameW(
                mem.vHandle,
                pid,
                const_cast<LPWSTR>(wideName.c_str()),
                &moduleEntry,
                VMMDLL_MODULE_FLAG_NORMAL) &&
            moduleEntry) {
            base = moduleEntry->vaBase;
            VMMDLL_MemFree(moduleEntry);
            return base;
        }
        if (moduleEntry)
            VMMDLL_MemFree(moduleEntry);

        return 0;
    }

    inline uint64_t ResolveKernelModuleBase(const std::string& moduleName)
    {
        if (const uint64_t base = ResolveKernelModuleBaseForPid(kSystemPid, moduleName))
            return base;

        return ResolveKernelModuleBaseForPid(0, moduleName);
    }

    inline size_t NormalizeKeyStateByteCount(int configuredSize)
    {
        return configuredSize == 64 ? 64u : keyStateBitmap.size();
    }

    inline uint64_t ResolveExportAddress(DWORD pid, const char* moduleName, const char* exportName)
    {
        if (!mem.vHandle || !moduleName || !exportName)
            return 0;

        PVMMDLL_MAP_EAT eatMap = nullptr;
        if (!VMMDLL_Map_GetEATU(mem.vHandle, pid, const_cast<LPSTR>(moduleName), &eatMap))
            return 0;

        uint64_t address = 0;
        if (eatMap->dwVersion == VMMDLL_MAP_EAT_VERSION) {
            for (DWORD i = 0; i < eatMap->cMap; ++i) {
                const auto& entry = eatMap->pMap[i];
                if (entry.uszFunction && std::strcmp(entry.uszFunction, exportName) == 0) {
                    address = entry.vaFunction;
                    break;
                }
            }
        }

        VMMDLL_MemFree(eatMap);
        return address;
    }

    inline uint64_t ResolveManualDirectAddress(const KeyboardProxyProcess& proxy)
    {
        kernelModuleName = "win32kbase.sys";
        keyStateByteCount.store(
            NormalizeKeyStateByteCount(OW::Config::gafAsyncKeyStateSize),
            std::memory_order_release);

        const uint64_t moduleBase = ResolveKernelModuleBaseForPid(proxy.pid, kernelModuleName);
        if (!moduleBase)
            return 0;

        const uint64_t address = moduleBase + OW::Config::gafAsyncKeyStateOffset;
        keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
        resolvedSessionId.store(proxy.sessionId, std::memory_order_release);
        resolvedModuleBase.store(moduleBase, std::memory_order_release);
        resolvedSlotsRva.store(OW::Config::gafAsyncKeyStateOffset, std::memory_order_release);
        resolvedKeyStateOffset.store(0, std::memory_order_release);
        resolvedViaAutoTable.store(false, std::memory_order_release);

        Diagnostics::Info("DMA KeyState manual direct resolver: proxy=%s pid=%lu session=%lu module=%s base=0x%llX rva=0x%llX addr=0x%llX size=%zu.",
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(proxy.sessionId),
            kernelModuleName.c_str(),
            static_cast<unsigned long long>(moduleBase),
            static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset),
            static_cast<unsigned long long>(address),
            keyStateByteCount.load(std::memory_order_acquire));
        return address;
    }

    inline uint64_t ResolveWin10ExportAddress(const KeyboardProxyProcess& proxy, DWORD build)
    {
        kernelModuleName = "win32kbase.sys";
        const uint64_t address = ResolveExportAddress(proxy.pid, kernelModuleName.c_str(), "gafAsyncKeyState");
        if (!address)
            return 0;

        keyStateByteCount.store(64u, std::memory_order_release);
        keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
        resolvedSessionId.store(proxy.sessionId, std::memory_order_release);
        resolvedModuleBase.store(ResolveKernelModuleBaseForPid(proxy.pid, kernelModuleName), std::memory_order_release);
        resolvedSlotsRva.store(0, std::memory_order_release);
        resolvedKeyStateOffset.store(0, std::memory_order_release);
        resolvedViaAutoTable.store(true, std::memory_order_release);

        Diagnostics::Info("DMA KeyState export resolver: build=%lu proxy=%s pid=%lu session=%lu module=%s export=gafAsyncKeyState addr=0x%llX size=%zu.",
            static_cast<unsigned long>(build),
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(proxy.sessionId),
            kernelModuleName.c_str(),
            static_cast<unsigned long long>(address),
            keyStateByteCount.load(std::memory_order_acquire));
        return address;
    }

    inline uint64_t ResolveSessionSlotsAddress(
        const KeyboardProxyProcess& proxy,
        DWORD build,
        const SessionSlotsProfile& profile)
    {
        kernelModuleName = profile.moduleName;
        const uint64_t moduleBase = ResolveKernelModuleBaseForPid(proxy.pid, kernelModuleName);
        if (!moduleBase)
            return 0;

        const DWORD sessionId = OW::Config::gafAsyncKeyStateSessionId > 0
            ? static_cast<DWORD>(OW::Config::gafAsyncKeyStateSessionId)
            : (proxy.sessionId > 0 ? proxy.sessionId : 1);

        uint64_t slots = 0;
        const uint64_t slotsAddress = moduleBase + profile.slotsRva;
        if (!ReadU64(proxy.pid, slotsAddress, slots) || !slots)
            return 0;

        uint64_t slot = 0;
        const uint64_t slotAddress = slots + (static_cast<uint64_t>(sessionId - 1) * sizeof(uint64_t));
        if (!ReadU64(proxy.pid, slotAddress, slot) || !slot)
            return 0;

        uint64_t sessionState = 0;
        if (!ReadU64(proxy.pid, slot, sessionState) || !sessionState)
            return 0;

        const uint64_t keyStateAddress = sessionState + profile.keyStateOffset;
        keyStateByteCount.store(64u, std::memory_order_release);
        keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
        resolvedSessionId.store(sessionId, std::memory_order_release);
        resolvedModuleBase.store(moduleBase, std::memory_order_release);
        resolvedSlotsRva.store(profile.slotsRva, std::memory_order_release);
        resolvedKeyStateOffset.store(profile.keyStateOffset, std::memory_order_release);
        resolvedViaAutoTable.store(true, std::memory_order_release);

        Diagnostics::Info("DMA KeyState auto resolver: profile=%s build=%lu proxy=%s pid=%lu session=%lu module=%s base=0x%llX slotsRva=0x%llX slots=0x%llX slot=0x%llX state=0x%llX keyOffset=0x%llX addr=0x%llX size=%zu.",
            profile.label,
            static_cast<unsigned long>(build),
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(sessionId),
            kernelModuleName.c_str(),
            static_cast<unsigned long long>(moduleBase),
            static_cast<unsigned long long>(profile.slotsRva),
            static_cast<unsigned long long>(slots),
            static_cast<unsigned long long>(slot),
            static_cast<unsigned long long>(sessionState),
            static_cast<unsigned long long>(profile.keyStateOffset),
            static_cast<unsigned long long>(keyStateAddress),
            keyStateByteCount.load(std::memory_order_acquire));
        return keyStateAddress;
    }

    inline uint64_t ResolveGafAsyncKeyStateAddress()
    {
        KeyboardProxyProcess proxy{};
        if (!FindKeyboardProxyProcess(proxy)) {
            Diagnostics::Warn("DMA KeyState resolver could not find an interactive proxy process.");
            return 0;
        }

        const DWORD build = QueryWindowsBuild();
        if (OW::Config::gafAsyncKeyStateOffset != 0) {
            gafAsyncKeyStateOffset = OW::Config::gafAsyncKeyStateOffset;
            return ResolveManualDirectAddress(proxy);
        }

        const SessionSlotsProfile* profile = SelectSessionSlotsProfile(build);
        if (profile) {
            gafAsyncKeyStateOffset = profile->slotsRva;
            if (const uint64_t address = ResolveSessionSlotsAddress(proxy, build, *profile))
                return address;
        }

        const uint64_t exportAddress = ResolveWin10ExportAddress(proxy, build);
        if (exportAddress) {
            gafAsyncKeyStateOffset = 0;
            return exportAddress;
        }

        Diagnostics::Warn("DMA KeyState resolver failed. build=%lu proxy=%s pid=%lu session=%lu manualOffset=0x%llX.",
            static_cast<unsigned long>(build),
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(proxy.sessionId),
            static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset));
        return 0;
    }

    inline bool Initialize()
    {
        if (!mem.vHandle) {
            initialized.store(false, std::memory_order_release);
            return false;
        }

        if (gafAsyncKeyStateAddr.load(std::memory_order_acquire) == 0)
            gafAsyncKeyStateAddr.store(ResolveGafAsyncKeyStateAddress(), std::memory_order_release);

        const bool ok = gafAsyncKeyStateAddr.load(std::memory_order_acquire) != 0;
        initialized.store(ok, std::memory_order_release);
        return ok;
    }

    inline void UpdateKeyStates()
    {
        const uint64_t address = gafAsyncKeyStateAddr.load(std::memory_order_acquire);
        if (!address || !initialized.load(std::memory_order_acquire))
            return;

        std::array<uint8_t, 256> latest{};
        const size_t bytesToRead = (std::min)(
            keyStateByteCount.load(std::memory_order_acquire),
            latest.size());

        if (!mem.Read(
                static_cast<uintptr_t>(address),
                latest.data(),
                bytesToRead,
                keyStateReadPid.load(std::memory_order_acquire)))
            return;

        std::lock_guard<std::mutex> lock(keyStateMutex);
        keyStateBitmap = latest;
    }

    inline bool IsKeyDownInBitmap(const std::array<uint8_t, 256>& bitmap, int vkCode)
    {
        if (vkCode < 0 || vkCode >= 256)
            return false;

        const size_t byteCount = keyStateByteCount.load(std::memory_order_acquire);
        if (byteCount == 64) {
            const size_t byteIndex = static_cast<size_t>(vkCode) / 4;
            const uint8_t downBit = static_cast<uint8_t>(1u << ((vkCode % 4) * 2));
            return byteIndex < bitmap.size() && (bitmap[byteIndex] & downBit) != 0;
        }

        return (bitmap[static_cast<size_t>(vkCode)] & 0x80) != 0;
    }

    inline bool IsKeyDown(int vkCode)
    {
        std::lock_guard<std::mutex> lock(keyStateMutex);
        return IsKeyDownInBitmap(keyStateBitmap, vkCode);
    }

    inline bool IsKeyPressed(int vkCode)
    {
        static std::array<uint8_t, 256> previousState{};

        if (vkCode < 0 || vkCode >= 256)
            return false;

        std::lock_guard<std::mutex> lock(keyStateMutex);
        const bool currentlyDown = IsKeyDownInBitmap(keyStateBitmap, vkCode);
        const bool previouslyDown = IsKeyDownInBitmap(previousState, vkCode);
        const size_t stateIndex = keyStateByteCount.load(std::memory_order_acquire) == 64
            ? static_cast<size_t>(vkCode) / 4
            : static_cast<size_t>(vkCode);
        previousState[stateIndex] = keyStateBitmap[stateIndex];
        return currentlyDown && !previouslyDown;
    }

    inline void KeyStateThread()
    {
        auto nextResolveAttempt = std::chrono::steady_clock::now();

        while (running.load(std::memory_order_acquire)) {
            if (!initialized.load(std::memory_order_acquire)) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= nextResolveAttempt) {
                    Initialize();
                    nextResolveAttempt = now + std::chrono::seconds(5);
                }
            }

            UpdateKeyStates();
            std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        }
    }

    inline void Start()
    {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;

        Initialize();
        std::thread(KeyStateThread).detach();
    }

    inline void Stop()
    {
        running.store(false, std::memory_order_release);
    }

    inline void SetAddress(uint64_t address)
    {
        gafAsyncKeyStateAddr.store(address, std::memory_order_release);
        initialized.store(address != 0, std::memory_order_release);
    }

} // namespace KeyState
