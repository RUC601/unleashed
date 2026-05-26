#pragma once

#include "Memory/Memory.h"
#include "Utils/Config.hpp"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

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

    inline uint64_t ResolveGafAsyncKeyStateAddress()
    {
        gafAsyncKeyStateOffset = OW::Config::gafAsyncKeyStateOffset;
        keyStateByteCount.store(
            NormalizeKeyStateByteCount(OW::Config::gafAsyncKeyStateSize),
            std::memory_order_release);

        if (gafAsyncKeyStateOffset == 0)
            return 0;

        const uint64_t moduleBase = ResolveKernelModuleBase(kernelModuleName);
        if (!moduleBase)
            return 0;

        return moduleBase + gafAsyncKeyStateOffset;
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

        if (!mem.Read(static_cast<uintptr_t>(address), latest.data(), bytesToRead, static_cast<int>(kSystemPid)))
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
