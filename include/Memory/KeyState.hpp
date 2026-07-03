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
#include <initializer_list>
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
    inline std::atomic<uint64_t> resolvedSlotsPointer{ 0 };
    inline std::atomic<uint64_t> resolvedKeyStateOffset{ 0 };
    inline std::atomic<DWORD> resolvedProxyPid{ 0 };
    inline std::atomic<DWORD> resolvedProxySessionId{ 0 };
    inline std::atomic<bool> resolvedViaAutoTable{ false };
    inline std::mutex resolverDiagnosticsMutex;
    inline std::string resolvedProfileLabel = "none";
    inline std::string resolvedModuleName = "win32kbase.sys";
    inline std::string resolvedMethod = "unresolved";
    inline std::string resolvedSlotsMethod = "none";
    inline std::string resolvedKeyOffsetMethod = "none";

    struct ResolverDiagnostics {
        DWORD build = 0;
        std::string profile;
        std::string module;
        std::string method;
        std::string slotsMethod;
        std::string keyOffsetMethod;
        uint64_t moduleBase = 0;
        uint64_t slotsRva = 0;
        uint64_t slotsPointer = 0;
        uint64_t keyStateOffset = 0;
        DWORD proxyPid = 0;
        DWORD proxySessionId = 0;
        DWORD resolvedSessionId = 0;
    };

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

    struct KernelModuleInfo {
        uint64_t base = 0;
        uint64_t size = 0;
        std::string name;
    };

    inline void SetResolverDiagnosticText(
        const char* profile,
        const char* module,
        const char* method,
        const char* slotsMethod,
        const char* keyOffsetMethod)
    {
        std::lock_guard<std::mutex> lock(resolverDiagnosticsMutex);
        resolvedProfileLabel = profile ? profile : "none";
        resolvedModuleName = module ? module : "";
        resolvedMethod = method ? method : "unresolved";
        resolvedSlotsMethod = slotsMethod ? slotsMethod : "none";
        resolvedKeyOffsetMethod = keyOffsetMethod ? keyOffsetMethod : "none";
    }

    inline ResolverDiagnostics SnapshotResolverDiagnostics()
    {
        ResolverDiagnostics snapshot{};
        snapshot.build = detectedBuild.load(std::memory_order_acquire);
        snapshot.moduleBase = resolvedModuleBase.load(std::memory_order_acquire);
        snapshot.slotsRva = resolvedSlotsRva.load(std::memory_order_acquire);
        snapshot.slotsPointer = resolvedSlotsPointer.load(std::memory_order_acquire);
        snapshot.keyStateOffset = resolvedKeyStateOffset.load(std::memory_order_acquire);
        snapshot.proxyPid = resolvedProxyPid.load(std::memory_order_acquire);
        snapshot.proxySessionId = resolvedProxySessionId.load(std::memory_order_acquire);
        snapshot.resolvedSessionId = resolvedSessionId.load(std::memory_order_acquire);

        std::lock_guard<std::mutex> lock(resolverDiagnosticsMutex);
        snapshot.profile = resolvedProfileLabel;
        snapshot.module = resolvedModuleName;
        snapshot.method = resolvedMethod;
        snapshot.slotsMethod = resolvedSlotsMethod;
        snapshot.keyOffsetMethod = resolvedKeyOffsetMethod;
        return snapshot;
    }

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
            // 24H2 / build 26100 keeps the session slot path in win32k.sys.
            { 26100, "Win11 24H2", "win32k.sys", 0x824F0, 0x3808 },
            // 22H2/23H2 builds keep the session slot path in win32ksgd.sys.
            { 22621, "Win11 22H2/23H2", "win32ksgd.sys", 0x3110, 0x36A8 },
        };

        for (const auto& profile : profiles) {
            if (build >= profile.minBuild)
                return &profile;
        }
        return nullptr;
    }

    inline bool FindKeyboardProxyProcesses(std::vector<KeyboardProxyProcess>& proxies)
    {
        proxies.clear();
        if (!mem.vHandle)
            return false;

        const DWORD desiredSession = OW::Config::gafAsyncKeyStateSessionId > 0
            ? static_cast<DWORD>(OW::Config::gafAsyncKeyStateSessionId)
            : 0;

        PVMMDLL_PROCESS_INFORMATION processInfo = nullptr;
        DWORD processCount = 0;
        if (!VMMDLL_ProcessGetInformationAll(mem.vHandle, &processInfo, &processCount))
            return false;

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

            KeyboardProxyProcess proxy{};
            proxy.pid = info.dwPID;
            proxy.sessionId = sessionId;
            proxy.name = name;
            proxies.push_back(proxy);
        }

        VMMDLL_MemFree(processInfo);

        std::sort(proxies.begin(), proxies.end(),
            [desiredSession](const KeyboardProxyProcess& lhs, const KeyboardProxyProcess& rhs) {
                if (desiredSession == 0) {
                    const bool lhsHasSession = lhs.sessionId != 0;
                    const bool rhsHasSession = rhs.sessionId != 0;
                    if (lhsHasSession != rhsHasSession)
                        return lhsHasSession;
                }

                const int lhsPriority = KeyboardProxyPriority(lhs.name.c_str());
                const int rhsPriority = KeyboardProxyPriority(rhs.name.c_str());
                if (lhsPriority != rhsPriority)
                    return lhsPriority < rhsPriority;

                if (desiredSession == 0 && lhs.sessionId != rhs.sessionId)
                    return lhs.sessionId > rhs.sessionId;

                return lhs.pid < rhs.pid;
            });

        return !proxies.empty();
    }

    inline bool FindKeyboardProxyProcess(KeyboardProxyProcess& proxy)
    {
        std::vector<KeyboardProxyProcess> proxies;
        if (!FindKeyboardProxyProcesses(proxies))
            return false;

        proxy = proxies.front();
        return true;
    }

    inline constexpr DWORD kKeyStateReadFlags =
        VMMDLL_FLAG_NOCACHE |
        VMMDLL_FLAG_NOPAGING |
        VMMDLL_FLAG_ZEROPAD_ON_FAIL;

    inline bool ReadRaw(DWORD pid, uint64_t address, void* buffer, size_t size)
    {
        const auto start = std::chrono::steady_clock::now();
        if (!mem.vHandle || !buffer || size == 0) {
            Diagnostics::RecordDmaRead(false, std::chrono::steady_clock::duration::zero());
            return false;
        }

        DWORD bytesRead = 0;
        const bool ok = VMMDLL_MemReadEx(
            mem.vHandle,
            pid,
            address,
            static_cast<PBYTE>(buffer),
            static_cast<DWORD>(size),
            &bytesRead,
            kKeyStateReadFlags) != FALSE;
        const bool success = ok && bytesRead == size;
        Diagnostics::RecordDmaRead(success, std::chrono::steady_clock::now() - start);
        return success;
    }

    inline bool ReadU64(DWORD pid, uint64_t address, uint64_t& value)
    {
        value = 0;
        return ReadRaw(pid, address, &value, sizeof(value));
    }

    inline std::vector<DWORD> BuildSessionProbeList(const KeyboardProxyProcess& proxy)
    {
        std::vector<DWORD> sessions;
        const DWORD configuredSession = OW::Config::gafAsyncKeyStateSessionId > 0
            ? static_cast<DWORD>(OW::Config::gafAsyncKeyStateSessionId)
            : 0;

        if (configuredSession != 0) {
            sessions.push_back(configuredSession);
            return sessions;
        }

        if (proxy.sessionId != 0)
            sessions.push_back(proxy.sessionId);

        // Most desktop targets use Session 1; keep Session 2 as a cheap fallback
        // for machines with a second interactive session.
        sessions.push_back(1);
        sessions.push_back(2);

        std::sort(sessions.begin(), sessions.end());
        sessions.erase(std::unique(sessions.begin(), sessions.end()), sessions.end());
        sessions.erase(std::remove(sessions.begin(), sessions.end(), 0), sessions.end());
        return sessions;
    }

    inline void AppendResolverFailure(
        std::string* failures,
        const KeyboardProxyProcess& proxy,
        DWORD sessionId,
        const char* step,
        uint64_t address,
        uint64_t value = 0)
    {
        if (!failures || failures->size() > 900)
            return;

        char line[256] = {};
        std::snprintf(
            line,
            sizeof(line),
            "%s pid=%lu session=%lu step=%s addr=0x%llX value=0x%llX; ",
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(sessionId),
            step ? step : "unknown",
            static_cast<unsigned long long>(address),
            static_cast<unsigned long long>(value));
        failures->append(line);
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

    inline bool ResolveKernelModuleInfoForPid(DWORD pid, const std::string& moduleName, KernelModuleInfo& info)
    {
        info = {};
        if (!mem.vHandle || moduleName.empty())
            return false;

        std::string narrowName = moduleName;
        PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
        if (VMMDLL_Map_GetModuleFromNameU(
                mem.vHandle,
                pid,
                const_cast<LPSTR>(narrowName.c_str()),
                &moduleEntry,
                VMMDLL_MODULE_FLAG_NORMAL) &&
            moduleEntry) {
            info.base = moduleEntry->vaBase;
            info.size = moduleEntry->cbImageSize;
            info.name = narrowName;
            VMMDLL_MemFree(moduleEntry);
            return info.base != 0;
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
            info.base = moduleEntry->vaBase;
            info.size = moduleEntry->cbImageSize;
            info.name = narrowName;
            VMMDLL_MemFree(moduleEntry);
            return info.base != 0;
        }
        if (moduleEntry)
            VMMDLL_MemFree(moduleEntry);

        info.base = ResolveKernelModuleBaseForPid(pid, moduleName);
        info.name = narrowName;
        return info.base != 0;
    }

    inline bool ResolveKernelModuleInfo(const std::string& moduleName, KernelModuleInfo& info)
    {
        if (ResolveKernelModuleInfoForPid(kSystemPid, moduleName, info))
            return true;

        return ResolveKernelModuleInfoForPid(0, moduleName, info);
    }

    inline uint64_t ResolveKernelModuleBase(const std::string& moduleName)
    {
        if (const uint64_t base = ResolveKernelModuleBaseForPid(kSystemPid, moduleName))
            return base;

        return ResolveKernelModuleBaseForPid(0, moduleName);
    }

    inline bool ReadModuleImage(DWORD pid, const KernelModuleInfo& module, std::vector<uint8_t>& image)
    {
        image.clear();
        if (!mem.vHandle || module.base == 0 || module.size == 0 || module.size > 64ull * 1024ull * 1024ull)
            return false;

        image.resize(static_cast<size_t>(module.size));
        const auto start = std::chrono::steady_clock::now();
        DWORD bytesRead = 0;
        const bool ok = VMMDLL_MemReadEx(
            mem.vHandle,
            pid,
            module.base,
            image.data(),
            static_cast<DWORD>(image.size()),
            &bytesRead,
            kKeyStateReadFlags) != FALSE;
        Diagnostics::RecordDmaRead(ok, std::chrono::steady_clock::now() - start);
        if (!ok) {
            image.clear();
            return false;
        }
        return true;
    }

    inline bool FindPatternOffset(const std::vector<uint8_t>& image, std::initializer_list<int> pattern, size_t& offset)
    {
        offset = 0;
        if (image.empty() || pattern.size() == 0 || pattern.size() > image.size())
            return false;

        std::vector<int> bytes(pattern);
        for (size_t i = 0; i + bytes.size() <= image.size(); ++i) {
            bool matched = true;
            for (size_t j = 0; j < bytes.size(); ++j) {
                if (bytes[j] >= 0 && image[i + j] != static_cast<uint8_t>(bytes[j])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                offset = i;
                return true;
            }
        }
        return false;
    }

    inline bool ReadLe32At(const std::vector<uint8_t>& image, size_t offset, int32_t& value)
    {
        value = 0;
        if (offset + sizeof(value) > image.size())
            return false;

        std::memcpy(&value, image.data() + offset, sizeof(value));
        return true;
    }

    inline bool ComputeRipRelativeTargetRva(size_t imageSize, size_t rel32Offset, int32_t rel32, uint64_t& rva)
    {
        rva = 0;
        const int64_t targetRva = static_cast<int64_t>(rel32Offset) + sizeof(int32_t) + rel32;
        if (targetRva <= 0 || static_cast<uint64_t>(targetRva) >= imageSize)
            return false;

        rva = static_cast<uint64_t>(targetRva);
        return true;
    }

    inline bool TryResolveRipRelativeRvaInImage(
        const std::vector<uint8_t>& image,
        std::initializer_list<int> pattern,
        size_t rel32OffsetInPattern,
        uint64_t& rva)
    {
        rva = 0;
        size_t matchOffset = 0;
        if (!FindPatternOffset(image, pattern, matchOffset))
            return false;

        int32_t rel32 = 0;
        const size_t rel32Offset = matchOffset + rel32OffsetInPattern;
        if (!ReadLe32At(image, rel32Offset, rel32))
            return false;

        return ComputeRipRelativeTargetRva(image.size(), rel32Offset, rel32, rva);
    }

    inline bool TryScanRipRelativeRva(
        DWORD pid,
        const KernelModuleInfo& module,
        std::initializer_list<int> pattern,
        size_t rel32OffsetInPattern,
        uint64_t& rva)
    {
        rva = 0;
        std::vector<uint8_t> image;
        if (!ReadModuleImage(pid, module, image))
            return false;

        return TryResolveRipRelativeRvaInImage(image, pattern, rel32OffsetInPattern, rva);
    }

    inline bool TryScanU32Value(
        DWORD pid,
        const KernelModuleInfo& module,
        std::initializer_list<int> pattern,
        size_t valueOffsetInPattern,
        uint64_t& value)
    {
        value = 0;
        std::vector<uint8_t> image;
        if (!ReadModuleImage(pid, module, image))
            return false;

        size_t matchOffset = 0;
        if (!FindPatternOffset(image, pattern, matchOffset))
            return false;

        int32_t rawValue = 0;
        if (!ReadLe32At(image, matchOffset + valueOffsetInPattern, rawValue))
            return false;

        if (rawValue <= 0)
            return false;

        value = static_cast<uint32_t>(rawValue);
        return true;
    }

    inline bool TryLoadPdbModuleName(DWORD pid, const KernelModuleInfo& module, std::string& pdbModuleName)
    {
        pdbModuleName.clear();
        if (!mem.vHandle || module.base == 0)
            return false;

        char moduleName[MAX_PATH] = {};
        if (!VMMDLL_PdbLoad(mem.vHandle, pid, module.base, moduleName))
            return false;

        pdbModuleName = moduleName;
        return !pdbModuleName.empty();
    }

    inline bool NormalizePdbSymbolAddressToRva(
        const KernelModuleInfo& module,
        uint64_t symbolAddress,
        uint64_t& rva)
    {
        rva = 0;
        if (symbolAddress >= module.base && symbolAddress < module.base + module.size) {
            rva = symbolAddress - module.base;
            return true;
        }

        if (module.size != 0 && symbolAddress < module.size) {
            rva = symbolAddress;
            return true;
        }

        return false;
    }

    inline bool TryPdbSymbolRva(
        DWORD pid,
        const KernelModuleInfo& module,
        std::initializer_list<const char*> symbolNames,
        uint64_t& symbolRva)
    {
        symbolRva = 0;
        std::string pdbModuleName;
        if (!TryLoadPdbModuleName(pid, module, pdbModuleName))
            return false;

        for (const char* symbolName : symbolNames) {
            if (!symbolName || !*symbolName)
                continue;

            std::string symbol = symbolName;
            ULONG64 symbolAddress = 0;
            if (!VMMDLL_PdbSymbolAddress(
                    mem.vHandle,
                    pdbModuleName.data(),
                    symbol.data(),
                    &symbolAddress) ||
                symbolAddress == 0) {
                continue;
            }

            uint64_t rva = 0;
            if (NormalizePdbSymbolAddressToRva(module, symbolAddress, rva)) {
                symbolRva = rva;
                return true;
            }
        }

        return false;
    }

    inline bool TryPdbTypeChildOffset(
        DWORD pid,
        const KernelModuleInfo& module,
        std::initializer_list<const char*> typeNames,
        std::initializer_list<const char*> childNames,
        uint64_t& childOffset)
    {
        childOffset = 0;
        std::string pdbModuleName;
        if (!TryLoadPdbModuleName(pid, module, pdbModuleName))
            return false;

        for (const char* typeName : typeNames) {
            if (!typeName || !*typeName)
                continue;

            for (const char* childName : childNames) {
                if (!childName || !*childName)
                    continue;

                std::string type = typeName;
                std::string child = childName;
                DWORD offset = 0;
                if (VMMDLL_PdbTypeChildOffset(
                        mem.vHandle,
                        pdbModuleName.data(),
                        type.data(),
                        child.data(),
                        &offset) &&
                    offset > 0 &&
                    offset < 0x100000) {
                    childOffset = offset;
                    return true;
                }
            }
        }

        return false;
    }

    inline bool TryPdbSessionSlotsRva(DWORD pid, const KernelModuleInfo& module, uint64_t& slotsRva)
    {
        return TryPdbSymbolRva(
            pid,
            module,
            { "gSessionGlobalSlots" },
            slotsRva);
    }

    inline bool TryScanSessionSlotsRva(
        DWORD pid,
        const KernelModuleInfo& module,
        const SessionSlotsProfile& profile,
        uint64_t& slotsRva)
    {
        if (EqualsNoCase(profile.moduleName, "win32k.sys")) {
            return TryScanRipRelativeRva(
                pid,
                module,
                { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0xFF, 0xC9 },
                3,
                slotsRva);
        }

        return TryScanRipRelativeRva(
            pid,
            module,
            { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x8B, 0x04, 0xC8 },
            3,
            slotsRva);
    }

    inline bool TryScanWin10GafAsyncKeyStateRva(
        DWORD pid,
        const KernelModuleInfo& win32kbase,
        uint64_t& gafAsyncKeyStateRva)
    {
        // memflow-win32's Win10 fallback scans NtUserGetAsyncKeyState for
        // a RIP-relative reference to the compact gafAsyncKeyState bitmap.
        return TryScanRipRelativeRva(
            pid,
            win32kbase,
            { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x89, 0x81, -1, -1, 0x00, 0x00, 0x48, 0x8B, 0x8F },
            3,
            gafAsyncKeyStateRva);
    }

    inline bool TryScanKeyStateOffset(DWORD pid, uint64_t& keyStateOffset)
    {
        KernelModuleInfo win32kbase{};
        if (!ResolveKernelModuleInfoForPid(pid, "win32kbase.sys", win32kbase) &&
            !ResolveKernelModuleInfo("win32kbase.sys", win32kbase))
            return false;

        return TryScanU32Value(
            pid,
            win32kbase,
            { 0xB9, 0x00, 0x80, 0xFF, 0xFF, -1, 0x22, 0xB4, -1, -1, -1, -1, -1, 0x41 },
            9,
            keyStateOffset);
    }

    inline bool TryPdbKeyStateOffset(DWORD pid, uint64_t& keyStateOffset)
    {
        KernelModuleInfo win32kbase{};
        if (!ResolveKernelModuleInfoForPid(pid, "win32kbase.sys", win32kbase) &&
            !ResolveKernelModuleInfo("win32kbase.sys", win32kbase))
            return false;

        return TryPdbTypeChildOffset(
            pid,
            win32kbase,
            { "tagWINDOWSTATION", "_WINDOWSTATION", "WINDOWSTATION" },
            { "gafAsyncKeyState" },
            keyStateOffset);
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

        uint64_t moduleBase = ResolveKernelModuleBase(kernelModuleName);
        if (!moduleBase)
            moduleBase = ResolveKernelModuleBaseForPid(proxy.pid, kernelModuleName);
        if (!moduleBase)
            return 0;

        const uint64_t address = moduleBase + OW::Config::gafAsyncKeyStateOffset;
        keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
        resolvedSessionId.store(proxy.sessionId, std::memory_order_release);
        resolvedModuleBase.store(moduleBase, std::memory_order_release);
        resolvedSlotsRva.store(OW::Config::gafAsyncKeyStateOffset, std::memory_order_release);
        resolvedSlotsPointer.store(0, std::memory_order_release);
        resolvedKeyStateOffset.store(0, std::memory_order_release);
        resolvedProxyPid.store(proxy.pid, std::memory_order_release);
        resolvedProxySessionId.store(proxy.sessionId, std::memory_order_release);
        resolvedViaAutoTable.store(false, std::memory_order_release);
        SetResolverDiagnosticText(
            "manual",
            kernelModuleName.c_str(),
            "manual",
            "manual-offset",
            "none");

        Diagnostics::Info("DMA KeyState manual direct resolver: build=%lu profile=manual method=manual proxy=%s pid=%lu session=%lu module=%s base=0x%llX slotsRva=0x%llX keyOffset=0x0 addr=0x%llX size=%zu.",
            static_cast<unsigned long>(detectedBuild.load(std::memory_order_acquire)),
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

    inline uint64_t ResolveWin10ExportAddress(
        const KeyboardProxyProcess& proxy,
        DWORD build,
        std::string* failureDetails = nullptr)
    {
        kernelModuleName = "win32kbase.sys";
        KernelModuleInfo moduleInfo{};
        if (!ResolveKernelModuleInfoForPid(proxy.pid, kernelModuleName, moduleInfo) &&
            !ResolveKernelModuleInfo(kernelModuleName, moduleInfo)) {
            AppendResolverFailure(failureDetails, proxy, proxy.sessionId, "win10_module", 0);
            return 0;
        }

        uint64_t address = ResolveExportAddress(proxy.pid, kernelModuleName.c_str(), "gafAsyncKeyState");
        const bool resolvedByExport = address != 0;
        uint64_t scannedRva = 0;
        if (!address && TryScanWin10GafAsyncKeyStateRva(proxy.pid, moduleInfo, scannedRva))
            address = moduleInfo.base + scannedRva;
        if (!address) {
            AppendResolverFailure(failureDetails, proxy, proxy.sessionId, "win10_gaf_export_sig", moduleInfo.base);
            return 0;
        }

        keyStateByteCount.store(64u, std::memory_order_release);
        keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
        resolvedSessionId.store(proxy.sessionId, std::memory_order_release);
        uint64_t moduleBase = moduleInfo.base;
        resolvedModuleBase.store(moduleBase, std::memory_order_release);
        resolvedSlotsRva.store(resolvedByExport || moduleBase == 0 ? 0 : scannedRva, std::memory_order_release);
        resolvedSlotsPointer.store(0, std::memory_order_release);
        resolvedKeyStateOffset.store(0, std::memory_order_release);
        resolvedProxyPid.store(proxy.pid, std::memory_order_release);
        resolvedProxySessionId.store(proxy.sessionId, std::memory_order_release);
        resolvedViaAutoTable.store(true, std::memory_order_release);
        SetResolverDiagnosticText(
            "legacy_win32kbase",
            kernelModuleName.c_str(),
            resolvedByExport ? "export" : "signature",
            resolvedByExport ? "export" : "signature",
            "none");

        Diagnostics::Info("DMA KeyState legacy resolver: build=%lu profile=legacy_win32kbase method=%s proxy=%s pid=%lu session=%lu module=%s base=0x%llX slotsRva=0x%llX keyOffset=0x0 addr=0x%llX size=%zu.",
            static_cast<unsigned long>(build),
            resolvedByExport ? "export" : "signature",
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(proxy.sessionId),
            kernelModuleName.c_str(),
            static_cast<unsigned long long>(moduleBase),
            static_cast<unsigned long long>(resolvedByExport || moduleBase == 0 ? 0 : scannedRva),
            static_cast<unsigned long long>(address),
            keyStateByteCount.load(std::memory_order_acquire));
        return address;
    }

    inline uint64_t ResolveSessionSlotsAddress(
        const KeyboardProxyProcess& proxy,
        DWORD build,
        const SessionSlotsProfile& profile,
        std::string* failureDetails = nullptr)
    {
        kernelModuleName = profile.moduleName;
        KernelModuleInfo moduleInfo{};
        uint64_t moduleBase = 0;
        if (ResolveKernelModuleInfoForPid(proxy.pid, kernelModuleName, moduleInfo) ||
            ResolveKernelModuleInfo(kernelModuleName, moduleInfo))
            moduleBase = moduleInfo.base;
        if (!moduleBase)
            moduleBase = ResolveKernelModuleBase(kernelModuleName);
        if (!moduleBase)
            moduleBase = ResolveKernelModuleBaseForPid(proxy.pid, kernelModuleName);
        if (!moduleBase) {
            AppendResolverFailure(failureDetails, proxy, 0, "module_base", 0);
            return 0;
        }

        uint64_t slotsRva = profile.slotsRva;
        const char* slotsMethod = "fallback";
        if (moduleInfo.base != 0) {
            if (TryScanSessionSlotsRva(proxy.pid, moduleInfo, profile, slotsRva))
                slotsMethod = "signature";
            else if (TryPdbSessionSlotsRva(proxy.pid, moduleInfo, slotsRva))
                slotsMethod = "pdb";
        }

        uint64_t keyStateOffset = profile.keyStateOffset;
        const char* keyOffsetMethod = "fallback";
        if (TryScanKeyStateOffset(proxy.pid, keyStateOffset))
            keyOffsetMethod = "signature";
        else if (TryPdbKeyStateOffset(proxy.pid, keyStateOffset))
            keyOffsetMethod = "pdb";

        uint64_t slots = 0;
        const uint64_t slotsAddress = moduleBase + slotsRva;
        if (!ReadU64(proxy.pid, slotsAddress, slots) || !slots) {
            AppendResolverFailure(failureDetails, proxy, 0, "slots", slotsAddress, slots);
            return 0;
        }

        for (const DWORD sessionId : BuildSessionProbeList(proxy)) {
            uint64_t slot = 0;
            const uint64_t slotAddress = slots + (static_cast<uint64_t>(sessionId - 1) * sizeof(uint64_t));
            if (!ReadU64(proxy.pid, slotAddress, slot) || !slot) {
                AppendResolverFailure(failureDetails, proxy, sessionId, "slot", slotAddress, slot);
                continue;
            }

            uint64_t sessionState = 0;
            if (!ReadU64(proxy.pid, slot, sessionState) || !sessionState) {
                AppendResolverFailure(failureDetails, proxy, sessionId, "state", slot, sessionState);
                continue;
            }

            const uint64_t keyStateAddress = sessionState + keyStateOffset;
            std::array<uint8_t, 64> probe{};
            if (!ReadRaw(proxy.pid, keyStateAddress, probe.data(), probe.size())) {
                AppendResolverFailure(failureDetails, proxy, sessionId, "keystate_probe", keyStateAddress);
                continue;
            }

            keyStateByteCount.store(64u, std::memory_order_release);
            keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
            resolvedSessionId.store(sessionId, std::memory_order_release);
            resolvedModuleBase.store(moduleBase, std::memory_order_release);
            resolvedSlotsRva.store(slotsRva, std::memory_order_release);
            resolvedSlotsPointer.store(slots, std::memory_order_release);
            resolvedKeyStateOffset.store(keyStateOffset, std::memory_order_release);
            resolvedProxyPid.store(proxy.pid, std::memory_order_release);
            resolvedProxySessionId.store(proxy.sessionId, std::memory_order_release);
            resolvedViaAutoTable.store(true, std::memory_order_release);
            gafAsyncKeyStateOffset = slotsRva;
            SetResolverDiagnosticText(
                profile.label,
                kernelModuleName.c_str(),
                "session_slots",
                slotsMethod,
                keyOffsetMethod);

            Diagnostics::Info("DMA KeyState auto resolver: build=%lu profile=%s method=session_slots proxy=%s pid=%lu proxySession=%lu session=%lu module=%s base=0x%llX slotsRva=0x%llX slotsMethod=%s slots=0x%llX slot=0x%llX state=0x%llX keyOffset=0x%llX keyOffsetMethod=%s addr=0x%llX size=%zu.",
                static_cast<unsigned long>(build),
                profile.label,
                proxy.name.c_str(),
                static_cast<unsigned long>(proxy.pid),
                static_cast<unsigned long>(proxy.sessionId),
                static_cast<unsigned long>(sessionId),
                kernelModuleName.c_str(),
                static_cast<unsigned long long>(moduleBase),
                static_cast<unsigned long long>(slotsRva),
                slotsMethod,
                static_cast<unsigned long long>(slots),
                static_cast<unsigned long long>(slot),
                static_cast<unsigned long long>(sessionState),
                static_cast<unsigned long long>(keyStateOffset),
                keyOffsetMethod,
                static_cast<unsigned long long>(keyStateAddress),
                keyStateByteCount.load(std::memory_order_acquire));
            return keyStateAddress;
        }

        return 0;
    }

    inline uint64_t ResolveGafAsyncKeyStateAddress()
    {
        std::vector<KeyboardProxyProcess> proxies;
        if (!FindKeyboardProxyProcesses(proxies)) {
            SetResolverDiagnosticText("none", "", "no_proxy", "none", "none");
            resolvedProxyPid.store(0, std::memory_order_release);
            resolvedProxySessionId.store(0, std::memory_order_release);
            Diagnostics::Warn("DMA KeyState resolver could not find an interactive proxy process.");
            return 0;
        }

        const DWORD build = QueryWindowsBuild();
        if (OW::Config::gafAsyncKeyStateOffset != 0) {
            gafAsyncKeyStateOffset = OW::Config::gafAsyncKeyStateOffset;
            for (const KeyboardProxyProcess& proxy : proxies) {
                if (const uint64_t address = ResolveManualDirectAddress(proxy))
                    return address;
            }

            Diagnostics::Warn("DMA KeyState manual resolver failed. build=%lu candidates=%zu manualOffset=0x%llX.",
                static_cast<unsigned long>(build),
                proxies.size(),
                static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset));
            return 0;
        }

        const SessionSlotsProfile* profile = SelectSessionSlotsProfile(build);
        std::string failureDetails;
        if (profile) {
            gafAsyncKeyStateOffset = profile->slotsRva;
            SetResolverDiagnosticText(
                profile->label,
                profile->moduleName,
                "session_slots",
                "pending",
                "pending");
            for (const KeyboardProxyProcess& proxy : proxies) {
                if (const uint64_t address = ResolveSessionSlotsAddress(proxy, build, *profile, &failureDetails))
                    return address;
            }

            Diagnostics::Warn("DMA KeyState resolver failed. build=%lu candidates=%zu profile=%s module=%s method=session_slots manualOffset=0x%llX sessionConfig=%d details=%s",
                static_cast<unsigned long>(build),
                proxies.size(),
                profile->label,
                profile->moduleName,
                static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset),
                OW::Config::gafAsyncKeyStateSessionId,
                failureDetails.empty() ? "none" : failureDetails.c_str());
            return 0;
        }

        SetResolverDiagnosticText(
            "legacy_win32kbase",
            "win32kbase.sys",
            "legacy_export_signature",
            "pending",
            "none");
        for (const KeyboardProxyProcess& proxy : proxies) {
            const uint64_t exportAddress = ResolveWin10ExportAddress(proxy, build, &failureDetails);
            if (exportAddress) {
                gafAsyncKeyStateOffset = 0;
                return exportAddress;
            }
        }

        Diagnostics::Warn("DMA KeyState resolver failed. build=%lu candidates=%zu profile=legacy_win32kbase module=win32kbase.sys method=export_signature manualOffset=0x%llX sessionConfig=%d details=%s",
            static_cast<unsigned long>(build),
            proxies.size(),
            static_cast<unsigned long long>(OW::Config::gafAsyncKeyStateOffset),
            OW::Config::gafAsyncKeyStateSessionId,
            failureDetails.empty() ? "none" : failureDetails.c_str());
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

        if (!ReadRaw(
                static_cast<DWORD>(keyStateReadPid.load(std::memory_order_acquire)),
                address,
                latest.data(),
                bytesToRead))
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
        Diagnostics::ScopedDmaCallsite tag(Diagnostics::DmaCallsite::KeyState);
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
