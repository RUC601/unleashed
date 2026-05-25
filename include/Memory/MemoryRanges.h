#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>

// Physical memory ranges from the VMM/EPT memory map. These are used to
// validate physical-looking DMA access targets before issuing reads.
struct MemoryRange {
    uint64_t start;
    uint64_t end;
};

constexpr MemoryRange kPhysicalRanges[] = {
    { 0x1000ULL, 0x5DFFFULL },
    { 0x5F000ULL, 0x9FFFFULL },
    { 0x100000ULL, 0x63E99FFFULL },
    { 0x63E9B000ULL, 0x63E9BFFFULL },
    { 0x63E9D000ULL, 0x741F0FFFULL },
    { 0x79FFF000ULL, 0x79FFFFFFULL },
    { 0x100000000ULL, 0x87FFFFFFFULL },
};

constexpr size_t kPhysicalRangeCount = 7;

inline bool IsValidPhysicalAddress(uint64_t addr)
{
    for (size_t i = 0; i < kPhysicalRangeCount; i++) {
        if (addr >= kPhysicalRanges[i].start && addr <= kPhysicalRanges[i].end) {
            return true;
        }
    }
    return false;
}

inline uint64_t GetNextValidRangeStart(uint64_t addr)
{
    for (size_t i = 0; i < kPhysicalRangeCount; i++) {
        if (addr < kPhysicalRanges[i].start) {
            return kPhysicalRanges[i].start;
        }
    }
    return 0;
}
