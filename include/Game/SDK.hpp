#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>

#include "Memory/Memory.h"

namespace OW {

    // -------------------------------------------------------------------------
    // MemorySDK  --  DMA-based memory access layer
    //
    // Every read/write is forwarded to the global DMA 'mem' instance so no
    // ReadProcessMemory / WriteProcessMemory is ever used.
    // -------------------------------------------------------------------------
    class MemorySDK {
    private:
        bool            m_initialized = false;

    public:
        uint64_t        dwGameBase = 0;
        uint64_t        GlobalKey1 = 0;
        uint64_t        GlobalKey2 = 0;
        uint64_t        g_player_controller = 0;

    public:
        MemorySDK() = default;
        ~MemorySDK() = default;

        // ---- Initialisation (DMA) -------------------------------------------
        bool Initialize()
        {
            if (!mem.AttachToProcess("Overwatch.exe")) {
                printf("[SDK] Failed to attach to Overwatch.exe\n");
                return false;
            }

            dwGameBase = mem.GetBaseDaddy("Overwatch.exe");
            if (!dwGameBase) {
                printf("[SDK] Failed to get base address for Overwatch.exe\n");
                return false;
            }

            printf("[SDK] Attached to Overwatch.exe @ 0x%llX\n", dwGameBase);
            m_initialized = true;
            return true;
        }

        bool IsInitialized() const { return m_initialized; }

        // ---- DMA single-value reads -----------------------------------------
        template <typename T>
        __forceinline T RPM(uint64_t address)
        {
            return mem.Read<T>(address);
        }

        // ---- DMA single-value writes ----------------------------------------
        template <typename T>
        __forceinline bool WPM(uint64_t address, T value)
        {
            return mem.Write<T>(address, value);
        }

        // ---- DMA buffer reads -----------------------------------------------
        __forceinline void read_buf(uint64_t address, char* buffer, size_t size)
        {
            mem.Read(address, buffer, size);
        }

        // ---- DMA buffer writes ----------------------------------------------
        __forceinline void write_buf(uint64_t address, char* buffer, size_t size)
        {
            mem.Write(static_cast<uintptr_t>(address), static_cast<const void*>(buffer), size);
        }

        // ---- Relative address helper ----------------------------------------
        __forceinline uint64_t calc_relative(uint64_t current, int32_t relative)
        {
            return current + RPM<int32_t>(current) + relative;
        }

        // ---- Module base by name (DMA) --------------------------------------
        uint64_t GetModuleBaseAddress(const char* modName)
        {
            return mem.GetBaseDaddy(modName);
        }

        // ---- DMA pattern scanning -------------------------------------------
        // Converts byte-pattern + mask to IDA-style string and delegates to mem.FindSignature
        uint64_t FindPatternExReg(const uint8_t* pattern, const char* mask, uint64_t regionSize)
        {
            // Build IDA-style signature string: "AA BB ?? CC"
            std::string sigStr;
            size_t maskLen = strlen(mask);
            for (size_t i = 0; i < maskLen; i++) {
                char byteStr[8];
                if (mask[i] == '?')
                    snprintf(byteStr, sizeof(byteStr), i + 1 < maskLen ? "?? " : "??");
                else
                    snprintf(byteStr, sizeof(byteStr), i + 1 < maskLen ? "%02X " : "%02X", pattern[i]);
                sigStr += byteStr;
            }
            return mem.FindSignature(sigStr.c_str(), dwGameBase, dwGameBase + regionSize);
        }

        // ---- DMA-based memory region query (replacement for VirtualQueryEx) --
        bool UpdateMemoryQuery()
        {
            return true;
        }
    };

    inline auto SDK = std::make_unique<MemorySDK>();
} // namespace OW
