#pragma once

#include "Memory/Memory.h"
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

    inline constexpr size_t kKeyStateByteCount = 256 * 2 / 8;
    using KeyStateBitmap = std::array<uint8_t, kKeyStateByteCount>;

    struct KeyStateVkSample {
        int vk = 0;
        bool valid = false;
        bool available = false;
        size_t byteIndex = 0;
        uint8_t rawByte = 0;
        uint8_t downMask = 0;
        bool down = false;
    };

    // Windows stores two bits per VK in the compact gafAsyncKeyState bitmap.
    inline KeyStateBitmap keyStateBitmap{};
    inline std::mutex keyStateMutex;
    inline std::atomic<bool> initialized{ false };
    inline std::atomic<bool> running{ false };
    inline std::atomic<uint64_t> gafAsyncKeyStateAddr{ 0 };

    inline int pollIntervalMs = 10;
    inline std::string kernelModuleName = "win32kbase.sys";
    inline std::atomic<size_t> keyStateByteCount{ kKeyStateByteCount };
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
    inline std::string lastResolverFailureDetails;

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
        std::string lastFailureDetails;
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

    struct ResolverValueCandidate {
        uint64_t value = 0;
        const char* method = "";
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

    inline void SetResolverFailureDetails(const std::string& details)
    {
        std::lock_guard<std::mutex> lock(resolverDiagnosticsMutex);
        lastResolverFailureDetails = details;
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
        snapshot.lastFailureDetails = lastResolverFailureDetails;
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
            KeyboardProxyProcess proxy{};
            proxy.pid = info.dwPID;
            proxy.sessionId = sessionId;
            proxy.name = name;
            proxies.push_back(proxy);
        }

        VMMDLL_MemFree(processInfo);

        std::sort(proxies.begin(), proxies.end(),
            [](const KeyboardProxyProcess& lhs, const KeyboardProxyProcess& rhs) {
                const bool lhsHasSession = lhs.sessionId != 0;
                const bool rhsHasSession = rhs.sessionId != 0;
                if (lhsHasSession != rhsHasSession)
                    return lhsHasSession;

                const int lhsPriority = KeyboardProxyPriority(lhs.name.c_str());
                const int rhsPriority = KeyboardProxyPriority(rhs.name.c_str());
                if (lhsPriority != rhsPriority)
                    return lhsPriority < rhsPriority;

                if (lhs.sessionId != rhs.sessionId)
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

    inline DWORD KernelMemoryPid(DWORD pid)
    {
        return pid | VMMDLL_PID_PROCESS_WITH_KERNELMEMORY;
    }

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
            KernelMemoryPid(pid),
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
        const auto addSession = [&sessions](DWORD sessionId) {
            if (sessionId == 0)
                return;
            if (std::find(sessions.begin(), sessions.end(), sessionId) == sessions.end())
                sessions.push_back(sessionId);
        };

        addSession(proxy.sessionId);
        // Most desktop targets use Session 1; keep Session 2 as a cheap fallback
        // for machines with a second interactive session.
        addSession(1);
        addSession(2);
        return sessions;
    }

    inline void AppendResolverFailure(
        std::string* failures,
        const KeyboardProxyProcess& proxy,
        DWORD sessionId,
        const char* step,
        uint64_t address,
        uint64_t value = 0,
        uint64_t slotsRva = 0,
        const char* slotsMethod = nullptr,
        uint64_t keyStateOffset = 0,
        const char* keyOffsetMethod = nullptr)
    {
        if (!failures || failures->size() > 12000)
            return;

        char line[384] = {};
        std::snprintf(
            line,
            sizeof(line),
            "%s pid=%lu session=%lu step=%s slotsRva=0x%llX slotsMethod=%s keyOffset=0x%llX keyMethod=%s addr=0x%llX value=0x%llX; ",
            proxy.name.c_str(),
            static_cast<unsigned long>(proxy.pid),
            static_cast<unsigned long>(sessionId),
            step ? step : "unknown",
            static_cast<unsigned long long>(slotsRva),
            slotsMethod ? slotsMethod : "none",
            static_cast<unsigned long long>(keyStateOffset),
            keyOffsetMethod ? keyOffsetMethod : "none",
            static_cast<unsigned long long>(address),
            static_cast<unsigned long long>(value));
        failures->append(line);
    }

    inline void AddResolverCandidate(
        std::vector<ResolverValueCandidate>& candidates,
        uint64_t value,
        const char* method)
    {
        if (value == 0)
            return;

        const auto existing = std::find_if(
            candidates.begin(),
            candidates.end(),
            [value](const ResolverValueCandidate& candidate) {
                return candidate.value == value;
            });
        if (existing != candidates.end())
            return;

        ResolverValueCandidate candidate{};
        candidate.value = value;
        candidate.method = method ? method : "unknown";
        candidates.push_back(candidate);
    }

    inline uint64_t ResolveKernelModuleBaseForPid(DWORD pid, const std::string& moduleName)
    {
        if (!mem.vHandle || moduleName.empty())
            return 0;

        std::string narrowName = moduleName;
        const std::array<DWORD, 2> pidCandidates{ pid, KernelMemoryPid(pid) };
        for (const DWORD candidatePid : pidCandidates) {
            uint64_t base = VMMDLL_ProcessGetModuleBaseU(
                mem.vHandle,
                candidatePid,
                const_cast<LPSTR>(narrowName.c_str()));
            if (base)
                return base;
        }

        for (const DWORD candidatePid : pidCandidates) {
            PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
            if (VMMDLL_Map_GetModuleFromNameU(
                    mem.vHandle,
                    candidatePid,
                    const_cast<LPSTR>(narrowName.c_str()),
                    &moduleEntry,
                    VMMDLL_MODULE_FLAG_NORMAL) &&
                moduleEntry) {
                const uint64_t base = moduleEntry->vaBase;
                VMMDLL_MemFree(moduleEntry);
                return base;
            }
            if (moduleEntry)
                VMMDLL_MemFree(moduleEntry);
        }

        std::wstring wideName(narrowName.begin(), narrowName.end());
        for (const DWORD candidatePid : pidCandidates) {
            PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
            if (VMMDLL_Map_GetModuleFromNameW(
                mem.vHandle,
                candidatePid,
                const_cast<LPWSTR>(wideName.c_str()),
                &moduleEntry,
                VMMDLL_MODULE_FLAG_NORMAL) &&
                moduleEntry) {
                const uint64_t base = moduleEntry->vaBase;
                VMMDLL_MemFree(moduleEntry);
                return base;
            }
            if (moduleEntry)
                VMMDLL_MemFree(moduleEntry);
        }

        return 0;
    }

    inline bool ResolveKernelModuleInfoForPid(DWORD pid, const std::string& moduleName, KernelModuleInfo& info)
    {
        info = {};
        if (!mem.vHandle || moduleName.empty())
            return false;

        std::string narrowName = moduleName;
        const std::array<DWORD, 2> pidCandidates{ pid, KernelMemoryPid(pid) };
        for (const DWORD candidatePid : pidCandidates) {
            PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
            if (VMMDLL_Map_GetModuleFromNameU(
                    mem.vHandle,
                    candidatePid,
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
        }

        std::wstring wideName(narrowName.begin(), narrowName.end());
        for (const DWORD candidatePid : pidCandidates) {
            PVMMDLL_MAP_MODULEENTRY moduleEntry = nullptr;
            if (VMMDLL_Map_GetModuleFromNameW(
                    mem.vHandle,
                    candidatePid,
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
        }

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
            KernelMemoryPid(pid),
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

    inline bool TryResolveU32ValueInImage(
        const std::vector<uint8_t>& image,
        std::initializer_list<int> pattern,
        size_t valueOffsetInPattern,
        uint64_t& value)
    {
        value = 0;
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

        return TryResolveU32ValueInImage(image, pattern, valueOffsetInPattern, value);
    }

    inline bool TryLoadPdbModuleName(DWORD pid, const KernelModuleInfo& module, std::string& pdbModuleName)
    {
        pdbModuleName.clear();
        if (!mem.vHandle || module.base == 0)
            return false;

        char moduleName[MAX_PATH] = {};
        if (!VMMDLL_PdbLoad(mem.vHandle, KernelMemoryPid(pid), module.base, moduleName))
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
            if (TryScanRipRelativeRva(
                pid,
                module,
                { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0xFF, 0xC9 },
                3,
                slotsRva)) {
                return true;
            }

            return TryScanRipRelativeRva(
                pid,
                module,
                { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x8B, 0x04, 0xC8 },
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

    inline bool TryScanKeyStateOffsetLea(DWORD pid, uint64_t& keyStateOffset)
    {
        KernelModuleInfo win32kbase{};
        if (!ResolveKernelModuleInfoForPid(pid, "win32kbase.sys", win32kbase) &&
            !ResolveKernelModuleInfo("win32kbase.sys", win32kbase))
            return false;

        return TryScanU32Value(
            pid,
            win32kbase,
            { 0x48, 0x8D, 0x90, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x0F, 0x57, 0xC0 },
            3,
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

    inline void AddProfileSlotsRvaCandidates(
        std::vector<ResolverValueCandidate>& candidates,
        DWORD build,
        const SessionSlotsProfile& profile)
    {
        if (build >= 26100 && build < 26200 && EqualsNoCase(profile.moduleName, "win32k.sys")) {
            AddResolverCandidate(candidates, 0x824F0, "fallback_26100_ubr3323");
            AddResolverCandidate(candidates, 0x82530, "fallback_26100_ubr3037");
            AddResolverCandidate(candidates, 0x82538, "fallback_26100_pre3037");
            return;
        }

        AddResolverCandidate(candidates, profile.slotsRva, "fallback_profile");
    }

    inline void AddProfileKeyOffsetCandidates(
        std::vector<ResolverValueCandidate>& candidates,
        DWORD build,
        const SessionSlotsProfile& profile)
    {
        if (build >= 26100 && build < 26200 && EqualsNoCase(profile.moduleName, "win32k.sys")) {
            AddResolverCandidate(candidates, 0x3808, "fallback_26100_ubr3323");
            AddResolverCandidate(candidates, 0x3830, "fallback_26100_pre3323");
            return;
        }

        AddResolverCandidate(candidates, profile.keyStateOffset, "fallback_profile");
    }

    inline bool IsProfileKeyOffsetCandidate(
        DWORD build,
        const SessionSlotsProfile& profile,
        uint64_t keyStateOffset)
    {
        std::vector<ResolverValueCandidate> candidates;
        AddProfileKeyOffsetCandidates(candidates, build, profile);
        return std::any_of(
            candidates.begin(),
            candidates.end(),
            [keyStateOffset](const ResolverValueCandidate& candidate) {
                return candidate.value == keyStateOffset;
            });
    }

    inline void AddTrustedScannedKeyOffsetCandidate(
        std::vector<ResolverValueCandidate>& candidates,
        DWORD build,
        const SessionSlotsProfile& profile,
        uint64_t keyStateOffset,
        const char* method)
    {
        if (build >= 22621 && !IsProfileKeyOffsetCandidate(build, profile, keyStateOffset))
            return;

        AddResolverCandidate(candidates, keyStateOffset, method);
    }

    inline std::vector<ResolverValueCandidate> BuildSessionSlotsRvaCandidates(
        DWORD pid,
        DWORD build,
        const KernelModuleInfo& module,
        const SessionSlotsProfile& profile)
    {
        std::vector<ResolverValueCandidate> candidates;
        uint64_t value = 0;
        if (module.base != 0) {
            if (TryScanSessionSlotsRva(pid, module, profile, value))
                AddResolverCandidate(candidates, value, "signature");
            if (TryPdbSessionSlotsRva(pid, module, value))
                AddResolverCandidate(candidates, value, "pdb");
        }

        AddProfileSlotsRvaCandidates(candidates, build, profile);
        return candidates;
    }

    inline std::vector<ResolverValueCandidate> BuildKeyStateOffsetCandidates(
        DWORD pid,
        DWORD build,
        const SessionSlotsProfile& profile)
    {
        std::vector<ResolverValueCandidate> candidates;
        uint64_t value = 0;
        if (TryPdbKeyStateOffset(pid, value))
            AddTrustedScannedKeyOffsetCandidate(candidates, build, profile, value, "pdb");
        if (TryScanKeyStateOffsetLea(pid, value))
            AddTrustedScannedKeyOffsetCandidate(candidates, build, profile, value, "signature_lea");

        AddProfileKeyOffsetCandidates(candidates, build, profile);
        if (TryScanKeyStateOffset(pid, value))
            AddTrustedScannedKeyOffsetCandidate(candidates, build, profile, value, "signature_u32");
        return candidates;
    }

    inline uint64_t ResolveExportAddress(DWORD pid, const char* moduleName, const char* exportName)
    {
        if (!mem.vHandle || !moduleName || !exportName)
            return 0;

        PVMMDLL_MAP_EAT eatMap = nullptr;
        if (!VMMDLL_Map_GetEATU(mem.vHandle, KernelMemoryPid(pid), const_cast<LPSTR>(moduleName), &eatMap))
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

        std::vector<ResolverValueCandidate> addressCandidates;
        uint64_t address = ResolveExportAddress(proxy.pid, kernelModuleName.c_str(), "gafAsyncKeyState");
        AddResolverCandidate(addressCandidates, address, "export");

        uint64_t scannedRva = 0;
        if (TryScanWin10GafAsyncKeyStateRva(proxy.pid, moduleInfo, scannedRva))
            AddResolverCandidate(addressCandidates, moduleInfo.base + scannedRva, "signature");

        uint64_t pdbRva = 0;
        if (TryPdbSymbolRva(proxy.pid, moduleInfo, { "gafAsyncKeyState" }, pdbRva))
            AddResolverCandidate(addressCandidates, moduleInfo.base + pdbRva, "pdb");

        if (addressCandidates.empty()) {
            AppendResolverFailure(failureDetails, proxy, proxy.sessionId, "win10_gaf_export_sig_pdb", moduleInfo.base);
            return 0;
        }

        for (const ResolverValueCandidate& candidate : addressCandidates) {
            KeyStateBitmap probe{};
            if (!ReadRaw(proxy.pid, candidate.value, probe.data(), probe.size())) {
                AppendResolverFailure(
                    failureDetails,
                    proxy,
                    proxy.sessionId,
                    "win10_keystate_probe",
                    candidate.value,
                    0,
                    moduleInfo.base != 0 && candidate.value >= moduleInfo.base ? candidate.value - moduleInfo.base : 0,
                    candidate.method);
                continue;
            }

            keyStateByteCount.store(kKeyStateByteCount, std::memory_order_release);
            keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
            resolvedSessionId.store(proxy.sessionId, std::memory_order_release);
            uint64_t moduleBase = moduleInfo.base;
            resolvedModuleBase.store(moduleBase, std::memory_order_release);
            resolvedSlotsRva.store(moduleBase != 0 && candidate.value >= moduleBase ? candidate.value - moduleBase : 0, std::memory_order_release);
            resolvedSlotsPointer.store(0, std::memory_order_release);
            resolvedKeyStateOffset.store(0, std::memory_order_release);
            resolvedProxyPid.store(proxy.pid, std::memory_order_release);
            resolvedProxySessionId.store(proxy.sessionId, std::memory_order_release);
            resolvedViaAutoTable.store(true, std::memory_order_release);
            SetResolverDiagnosticText(
                "legacy_win32kbase",
                kernelModuleName.c_str(),
                candidate.method,
                candidate.method,
                "none");
            SetResolverFailureDetails(std::string());

            Diagnostics::Info("DMA KeyState legacy resolver: build=%lu profile=legacy_win32kbase method=%s proxy=%s pid=%lu readPid=0x%lX session=%lu module=%s base=0x%llX slotsRva=0x%llX keyOffset=0x0 addr=0x%llX size=%zu.",
                static_cast<unsigned long>(build),
                candidate.method,
                proxy.name.c_str(),
                static_cast<unsigned long>(proxy.pid),
                static_cast<unsigned long>(KernelMemoryPid(proxy.pid)),
                static_cast<unsigned long>(proxy.sessionId),
                kernelModuleName.c_str(),
                static_cast<unsigned long long>(moduleBase),
                static_cast<unsigned long long>(moduleBase != 0 && candidate.value >= moduleBase ? candidate.value - moduleBase : 0),
                static_cast<unsigned long long>(candidate.value),
                keyStateByteCount.load(std::memory_order_acquire));
            return candidate.value;
        }

        return 0;
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

        const std::vector<ResolverValueCandidate> slotsCandidates =
            BuildSessionSlotsRvaCandidates(proxy.pid, build, moduleInfo, profile);
        const std::vector<ResolverValueCandidate> keyOffsetCandidates =
            BuildKeyStateOffsetCandidates(proxy.pid, build, profile);
        if (slotsCandidates.empty()) {
            AppendResolverFailure(failureDetails, proxy, 0, "slots_candidates", moduleBase);
            return 0;
        }
        if (keyOffsetCandidates.empty()) {
            AppendResolverFailure(failureDetails, proxy, 0, "key_offset_candidates", moduleBase);
            return 0;
        }

        for (const ResolverValueCandidate& slotsCandidate : slotsCandidates) {
            uint64_t slots = 0;
            const uint64_t slotsAddress = moduleBase + slotsCandidate.value;
            if (!ReadU64(proxy.pid, slotsAddress, slots) || !slots) {
                AppendResolverFailure(
                    failureDetails,
                    proxy,
                    0,
                    "slots",
                    slotsAddress,
                    slots,
                    slotsCandidate.value,
                    slotsCandidate.method);
                continue;
            }

            for (const DWORD sessionId : BuildSessionProbeList(proxy)) {
                uint64_t slot = 0;
                const uint64_t slotAddress = slots + (static_cast<uint64_t>(sessionId - 1) * sizeof(uint64_t));
                if (!ReadU64(proxy.pid, slotAddress, slot) || !slot) {
                    AppendResolverFailure(
                        failureDetails,
                        proxy,
                        sessionId,
                        "slot",
                        slotAddress,
                        slot,
                        slotsCandidate.value,
                        slotsCandidate.method);
                    continue;
                }

                uint64_t sessionState = 0;
                if (!ReadU64(proxy.pid, slot, sessionState) || !sessionState) {
                    AppendResolverFailure(
                        failureDetails,
                        proxy,
                        sessionId,
                        "state",
                        slot,
                        sessionState,
                        slotsCandidate.value,
                        slotsCandidate.method);
                    continue;
                }

                for (const ResolverValueCandidate& keyOffsetCandidate : keyOffsetCandidates) {
                    const uint64_t keyStateAddress = sessionState + keyOffsetCandidate.value;
                    KeyStateBitmap probe{};
                    if (!ReadRaw(proxy.pid, keyStateAddress, probe.data(), probe.size())) {
                        AppendResolverFailure(
                            failureDetails,
                            proxy,
                            sessionId,
                            "keystate_probe",
                            keyStateAddress,
                            0,
                            slotsCandidate.value,
                            slotsCandidate.method,
                            keyOffsetCandidate.value,
                            keyOffsetCandidate.method);
                        continue;
                    }

                    keyStateByteCount.store(kKeyStateByteCount, std::memory_order_release);
                    keyStateReadPid.store(static_cast<int>(proxy.pid), std::memory_order_release);
                    resolvedSessionId.store(sessionId, std::memory_order_release);
                    resolvedModuleBase.store(moduleBase, std::memory_order_release);
                    resolvedSlotsRva.store(slotsCandidate.value, std::memory_order_release);
                    resolvedSlotsPointer.store(slots, std::memory_order_release);
                    resolvedKeyStateOffset.store(keyOffsetCandidate.value, std::memory_order_release);
                    resolvedProxyPid.store(proxy.pid, std::memory_order_release);
                    resolvedProxySessionId.store(proxy.sessionId, std::memory_order_release);
                    resolvedViaAutoTable.store(true, std::memory_order_release);
                    SetResolverDiagnosticText(
                        profile.label,
                        kernelModuleName.c_str(),
                        "session_slots",
                        slotsCandidate.method,
                        keyOffsetCandidate.method);
                    SetResolverFailureDetails(std::string());

                    Diagnostics::Info("DMA KeyState auto resolver: build=%lu profile=%s method=session_slots proxy=%s pid=%lu readPid=0x%lX proxySession=%lu session=%lu module=%s base=0x%llX slotsRva=0x%llX slotsMethod=%s slots=0x%llX slot=0x%llX state=0x%llX keyOffset=0x%llX keyOffsetMethod=%s addr=0x%llX size=%zu.",
                        static_cast<unsigned long>(build),
                        profile.label,
                        proxy.name.c_str(),
                        static_cast<unsigned long>(proxy.pid),
                        static_cast<unsigned long>(KernelMemoryPid(proxy.pid)),
                        static_cast<unsigned long>(proxy.sessionId),
                        static_cast<unsigned long>(sessionId),
                        kernelModuleName.c_str(),
                        static_cast<unsigned long long>(moduleBase),
                        static_cast<unsigned long long>(slotsCandidate.value),
                        slotsCandidate.method,
                        static_cast<unsigned long long>(slots),
                        static_cast<unsigned long long>(slot),
                        static_cast<unsigned long long>(sessionState),
                        static_cast<unsigned long long>(keyOffsetCandidate.value),
                        keyOffsetCandidate.method,
                        static_cast<unsigned long long>(keyStateAddress),
                        keyStateByteCount.load(std::memory_order_acquire));
                    return keyStateAddress;
                }
            }
        }

        return 0;
    }

    inline uint64_t ResolveGafAsyncKeyStateAddress()
    {
        std::vector<KeyboardProxyProcess> proxies;
        if (!FindKeyboardProxyProcesses(proxies)) {
            SetResolverDiagnosticText("none", "", "no_proxy", "none", "none");
            SetResolverFailureDetails("no interactive proxy process");
            resolvedProxyPid.store(0, std::memory_order_release);
            resolvedProxySessionId.store(0, std::memory_order_release);
            Diagnostics::Warn("DMA KeyState resolver could not find an interactive proxy process.");
            return 0;
        }

        const DWORD build = QueryWindowsBuild();
        const SessionSlotsProfile* profile = SelectSessionSlotsProfile(build);
        std::string failureDetails;
        if (profile) {
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

            SetResolverFailureDetails(failureDetails);
            Diagnostics::Warn("DMA KeyState resolver failed. build=%lu candidates=%zu profile=%s module=%s method=session_slots details=%s",
                static_cast<unsigned long>(build),
                proxies.size(),
                profile->label,
                profile->moduleName,
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
            if (exportAddress)
                return exportAddress;
        }

        SetResolverFailureDetails(failureDetails);
        Diagnostics::Warn("DMA KeyState resolver failed. build=%lu candidates=%zu profile=legacy_win32kbase module=win32kbase.sys method=export_signature details=%s",
            static_cast<unsigned long>(build),
            proxies.size(),
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

        KeyStateBitmap latest{};
        if (!ReadRaw(
                static_cast<DWORD>(keyStateReadPid.load(std::memory_order_acquire)),
                address,
                latest.data(),
                latest.size()))
            return;

        std::lock_guard<std::mutex> lock(keyStateMutex);
        keyStateBitmap = latest;
    }

    inline KeyStateVkSample MakeVkSampleLocation(int vkCode)
    {
        KeyStateVkSample sample{};
        sample.vk = vkCode;

        if (vkCode < 0 || vkCode >= 256)
            return sample;

        sample.valid = true;
        sample.byteIndex = static_cast<size_t>(vkCode) / 4;
        sample.downMask = static_cast<uint8_t>(1u << ((vkCode % 4) * 2));
        return sample;
    }

    inline KeyStateVkSample SampleVkInBitmap(const KeyStateBitmap& bitmap, int vkCode)
    {
        KeyStateVkSample sample = MakeVkSampleLocation(vkCode);
        if (!sample.valid || sample.byteIndex >= bitmap.size())
            return sample;

        sample.available = true;
        sample.rawByte = bitmap[sample.byteIndex];
        sample.down = (sample.rawByte & sample.downMask) != 0;
        return sample;
    }

    inline bool IsKeyDownInBitmap(const KeyStateBitmap& bitmap, int vkCode)
    {
        return SampleVkInBitmap(bitmap, vkCode).down;
    }

    inline KeyStateVkSample SampleVk(int vkCode)
    {
        KeyStateVkSample sample = MakeVkSampleLocation(vkCode);
        if (!sample.valid)
            return sample;

        if (!initialized.load(std::memory_order_acquire) ||
            gafAsyncKeyStateAddr.load(std::memory_order_acquire) == 0) {
            return sample;
        }

        std::lock_guard<std::mutex> lock(keyStateMutex);
        return SampleVkInBitmap(keyStateBitmap, vkCode);
    }

    inline bool IsKeyDown(int vkCode)
    {
        std::lock_guard<std::mutex> lock(keyStateMutex);
        return IsKeyDownInBitmap(keyStateBitmap, vkCode);
    }

    inline bool IsKeyPressed(int vkCode)
    {
        static KeyStateBitmap previousState{};

        if (vkCode < 0 || vkCode >= 256)
            return false;

        std::lock_guard<std::mutex> lock(keyStateMutex);
        const bool currentlyDown = IsKeyDownInBitmap(keyStateBitmap, vkCode);
        const bool previouslyDown = IsKeyDownInBitmap(previousState, vkCode);
        const size_t stateIndex = static_cast<size_t>(vkCode) / 4;
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
