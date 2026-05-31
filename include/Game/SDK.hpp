#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>
#include <thread>
#include <chrono>
#include <memory>
#include <mutex>
#include <atomic>

#include "Memory/Memory.h"

namespace OW {

    // -------------------------------------------------------------------------
    // MemorySDK  --  DMA-based memory access layer
    //
    // Every read/write is forwarded to the global DMA 'mem' instance so no
    // ReadProcessMemory / WriteProcessMemory is ever used.
    // -------------------------------------------------------------------------
    class MemorySDK {
    public:
        class ReadRange {
        private:
            uint64_t             m_base = 0;
            std::vector<uint8_t> m_buffer{};
            bool                 m_valid = false;

        public:
            ReadRange() = default;

            bool Read(uint64_t address, size_t size)
            {
                m_base = address;
                m_buffer.assign(size, 0);
                m_valid = size > 0 &&
                    mem.Read(static_cast<uintptr_t>(address), m_buffer.data(), size);
                if (!m_valid)
                    m_buffer.clear();
                return m_valid;
            }

            bool IsValid() const { return m_valid; }
            uint64_t Base() const { return m_base; }
            size_t Size() const { return m_buffer.size(); }

            bool Contains(uint64_t address, size_t size) const
            {
                if (!m_valid || address < m_base || size > m_buffer.size())
                    return false;

                const uint64_t offset = address - m_base;
                return offset <= m_buffer.size() &&
                    size <= m_buffer.size() - static_cast<size_t>(offset);
            }

            template <typename T>
            bool ReadOffset(size_t offset, T& value) const
            {
                if (!m_valid || offset > m_buffer.size() ||
                    sizeof(T) > m_buffer.size() - offset) {
                    value = {};
                    return false;
                }
                memcpy(&value, m_buffer.data() + offset, sizeof(T));
                return true;
            }

            template <typename T>
            T ReadOffset(size_t offset) const
            {
                T value{};
                ReadOffset(offset, value);
                return value;
            }

            template <typename T>
            bool ReadAddress(uint64_t address, T& value) const
            {
                if (!Contains(address, sizeof(T))) {
                    value = {};
                    return false;
                }
                return ReadOffset(static_cast<size_t>(address - m_base), value);
            }

            template <typename T>
            T ReadAddress(uint64_t address) const
            {
                T value{};
                ReadAddress(address, value);
                return value;
            }
        };

    private:
        std::atomic<bool> m_initialized{ false };

        struct ComponentKeyCache {
            uint64_t generation = 0;
            uint64_t sourcePointerAddress = 0;
            uint64_t sourceOffset = 0;
            uint64_t byteAddress = 0;
            uint64_t source = 0;
            uint64_t material = 0;
            uint8_t  byte = 0;
            bool     valid = false;
        };

        std::mutex       m_frameCacheMutex{};
        uint64_t         m_frameGeneration = 1;
        ComponentKeyCache m_componentKeyCache{};

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
                Reset();
                return false;
            }

            dwGameBase = mem.GetBaseDaddy("Overwatch.exe");
            if (!dwGameBase) {
                printf("[SDK] Failed to get base address for Overwatch.exe\n");
                Reset();
                return false;
            }

            printf("[SDK] Attached to Overwatch.exe @ 0x%llX\n", dwGameBase);
            m_initialized.store(true, std::memory_order_release);
            return true;
        }

        bool IsInitialized() const { return m_initialized.load(std::memory_order_acquire); }

        void Reset()
        {
            m_initialized.store(false, std::memory_order_release);
            dwGameBase = 0;
            GlobalKey1 = 0;
            GlobalKey2 = 0;
            g_player_controller = 0;
            std::lock_guard<std::mutex> lock(m_frameCacheMutex);
            ++m_frameGeneration;
            if (m_frameGeneration == 0)
                m_frameGeneration = 1;
            m_componentKeyCache = {};
        }

        void BeginFrame()
        {
            std::lock_guard<std::mutex> lock(m_frameCacheMutex);
            ++m_frameGeneration;
            if (m_frameGeneration == 0)
                m_frameGeneration = 1;
            m_componentKeyCache.valid = false;
        }

        // ---- DMA single-value reads -----------------------------------------
        template <typename T>
        __forceinline T RPM(uint64_t address)
        {
            return mem.Read<T>(address);
        }

        // ---- DMA buffer reads -----------------------------------------------
        __forceinline void read_buf(uint64_t address, char* buffer, size_t size)
        {
            mem.Read(address, buffer, size);
        }

        __forceinline bool read_range(uint64_t address, void* buffer, size_t size)
        {
            return mem.Read(static_cast<uintptr_t>(address), buffer, size);
        }

        __forceinline bool read_range(uint64_t address, ReadRange& range, size_t size)
        {
            return range.Read(address, size);
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

        bool GetCachedComponentKeyMaterial(uint64_t sourcePointerAddress,
                                           uint64_t sourceOffset,
                                           uint64_t byteAddress,
                                           uint64_t& source,
                                           uint64_t& material,
                                           uint8_t& byte)
        {
            std::lock_guard<std::mutex> lock(m_frameCacheMutex);

            if (m_componentKeyCache.valid &&
                m_componentKeyCache.generation == m_frameGeneration &&
                m_componentKeyCache.sourcePointerAddress == sourcePointerAddress &&
                m_componentKeyCache.sourceOffset == sourceOffset &&
                m_componentKeyCache.byteAddress == byteAddress) {
                source = m_componentKeyCache.source;
                material = m_componentKeyCache.material;
                byte = m_componentKeyCache.byte;
                return true;
            }

            const uint64_t loadedSource = RPM<uint64_t>(sourcePointerAddress);
            if (!loadedSource)
                return false;

            const uint64_t loadedMaterial =
                RPM<uint64_t>(loadedSource + sourceOffset);
            const uint8_t loadedByte = RPM<uint8_t>(byteAddress);

            m_componentKeyCache.generation = m_frameGeneration;
            m_componentKeyCache.sourcePointerAddress = sourcePointerAddress;
            m_componentKeyCache.sourceOffset = sourceOffset;
            m_componentKeyCache.byteAddress = byteAddress;
            m_componentKeyCache.source = loadedSource;
            m_componentKeyCache.material = loadedMaterial;
            m_componentKeyCache.byte = loadedByte;
            m_componentKeyCache.valid = true;

            source = loadedSource;
            material = loadedMaterial;
            byte = loadedByte;
            return true;
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
