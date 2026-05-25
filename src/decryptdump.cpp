// =============================================================================
// DecryptDump — find correct Key2 + dump actual decrypt code + test decryption
// =============================================================================

#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <intrin.h>

#include "leechcore.h"
#include "vmmdll.h"

#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

static VMM_HANDLE  g_vmm  = nullptr;
static DWORD       g_pid  = 0;
static uint64_t    g_base = 0;
static uint64_t    g_size = 0;

#define R(a) Read<uint64_t>(a)

template<typename T>
T Read(uint64_t addr) {
    T buf{};
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), 0,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

void read_buf(uint64_t addr, uint8_t* buf, size_t sz) {
    for (size_t i = 0; i < sz; i += 8) {
        uint64_t qw = R(addr + i);
        memcpy(buf + i, &qw, (sz - i >= 8) ? 8 : (sz - i));
    }
}

inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    return (x >> bits) | (x << (64 - bits));
}

// =============================================================================
// DecryptComponent (from Decrypt.hpp, accepting external Key1/Key2)
// =============================================================================

static uint64_t DecryptComponent(uint64_t parent, uint8_t idx,
                                  uint64_t Key1, uint64_t Key2,
                                  uint64_t table_base, uint32_t table_mask) {
    if (!parent || !Key1 || !Key2) return 0;

    uint64_t v1 = parent;
    uint64_t v2 = (uintptr_t)1 << (uintptr_t)(idx & 0x3F);
    uint64_t v3 = v2 - 1;
    uint64_t v4 = idx & 0x3F;
    uint64_t v5 = idx / 0x3F;

    uint64_t v6 = R(v1 + 8 * (uint32_t)v5 + 0x110);
    if (!v6) return 0;

    uint64_t v7 = (v2 & v6) >> v4;
    uint64_t v8 = (v3 & v6) - (((v3 & v6) >> 1) & 0x5555555555555555);

    uint64_t inner_ptr = R(v1 + 0x80);
    if (!inner_ptr) return 0;

    uint64_t v9 = R(inner_ptr +
        8 * (Read<uint8_t>((uint32_t)v5 + v1 + 0x130) +
            ((0x101010101010101 * (
                ((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333) +
                (((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333)) >> 4))
            ) & 0xF0F0F0F0F0F0F0F0) >> 0x38))
    );
    if (!v9) return 0;

    auto dummy  = R(table_base + ((Key1 >> 0x34) & table_mask));
    auto dummy2 = R(table_base + (Key1 & table_mask));

    uint64_t v10 = (unsigned int)v9 | v9 & 0xFFFFFFFF00000000ui64 ^
                   ((uint64_t)((unsigned int)v9 + 0x71747EF8) << 0x20);
    uint64_t v11 = Key2 ^ ((unsigned int)v9 | v10 & 0xFFFFFFFF00000000ui64 ^
                   ((uint64_t)(unsigned int)(v10 + __ROL4__(HIDWORD(dummy), 1)) << 0x20));
    uint64_t v12 = -(int)v7 & ((unsigned int)v11 |
                   ((unsigned int)v11 | v11 & 0xFFFFFFFF00000000ui64 ^
                    ((uint64_t)((unsigned int)v11 ^ ~(unsigned int)dummy2) << 0x20)) &
                    0xFFFFFFFF00000000ui64 ^
                    ((uint64_t)((unsigned int)v11 ^ 0xDFBFA250) << 0x20));
    return v12;
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== DecryptDump ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("[FAIL] DMA init\n"); return 1; }

    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");

    PVMMDLL_MAP_MODULE pMod = nullptr;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pMod, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pMod->cMap; i++)
            if (pMod->pMap[i].vaBase == g_base) { g_size = pMod->pMap[i].cbImageSize; break; }
        VMMDLL_MemFree(pMod);
    }
    if (!g_size) g_size = 0x5000000;

    printf("Base=0x%llX Size=0x%llX PID=%u\n\n", g_base, g_size, g_pid);

    // =========================================================================
    // Step 1: Verified Key1 value
    // =========================================================================

    uint64_t key1_raw_global = R(g_base + 0x40405F8);
    uint64_t Key1 = key1_raw_global ^ 0xF5;
    printf("[1] Key1 = 0x%llX ^ 0xF5 = 0x%llX\n", key1_raw_global, Key1);
    printf("    Key1>>0x34 & 0x7FF = %u\n", (unsigned)((Key1 >> 0x34) & 0x7FF));
    printf("    Key1     & 0x7FF = %u\n\n", (unsigned)(Key1 & 0x7FF));

    // =========================================================================
    // Step 2: Collect Key2 candidates from global store area
    // =========================================================================

    printf("[2] Collecting Key2 candidates near RVA 0x40405F8:\n");
    std::vector<std::pair<uint64_t, int64_t>> key2_candidates;

    for (int64_t off = -0x1000; off <= 0x1000; off += 8) {
        uint64_t v = R(g_base + 0x40405F8 + off);
        if (v > 0x1000000000000000 && v != key1_raw_global) {
            key2_candidates.push_back({v, off});
        }
    }

    // Sort by proximity to global store, prefer ones closer
    std::sort(key2_candidates.begin(), key2_candidates.end(),
        [](auto& a, auto& b) { return std::abs(a.second) < std::abs(b.second); });

    printf("    Found %zu candidates\n", key2_candidates.size());
    // Show best 10
    for (size_t i = 0; i < std::min(key2_candidates.size(), (size_t)10); i++) {
        printf("    [+%lld] RVA 0x%07llX = 0x%016llX\n",
               key2_candidates[i].second,
               0x40405F8 + key2_candidates[i].second,
               key2_candidates[i].first);
    }
    printf("\n");

    // =========================================================================
    // Step 3: Scan for "shr rcx, 0x34" pattern to find DecryptComponent
    // =========================================================================

    printf("[3] Scanning for DecryptComponent (shr rcx, 0x34 = 48 C1 E9 34):\n");

    uint8_t shr_pattern[] = { 0x48, 0xC1, 0xE9, 0x34 }; // shr rcx, 0x34
    std::vector<uint8_t> buf(0x10000); // 64KB scan buffer
    int shr_matches = 0;

    // Scan .text section range (RVA 0x1000 to ~0x4300000)
    for (uint64_t rva = 0x1000; rva < g_size && shr_matches < 20; rva += buf.size()) {
        size_t chunk = (rva + buf.size() > g_size) ? (g_size - rva) : buf.size();
        read_buf(g_base + rva, buf.data(), chunk);

        for (size_t i = 0; i < chunk - 4; i++) {
            if (buf[i] == 0x48 && buf[i+1] == 0xC1 && buf[i+2] == 0xE9 && buf[i+3] == 0x34) {
                uint64_t match_rva = rva + i;
                printf("    Found shr rcx,0x34 at RVA 0x%llX\n", match_rva);

                // Show surrounding 48 bytes
                uint8_t ctx[48] = {};
                read_buf(g_base + match_rva - 8, ctx, 48);
                printf("      -8: ");
                for (int j = 0; j < 48; j++) { printf("%02X ", ctx[j]); if ((j+1)%16==0 && j<47) printf("\n          "); }
                printf("\n");

                // Check if followed by "and ecx, 0x7FF" (81 E1 FF 07 00 00)
                if (ctx[8+0] == 0x81 && ctx[8+1] == 0xE1 &&
                    ctx[8+2] == 0xFF && ctx[8+3] == 0x07 && ctx[8+4] == 0x00 && ctx[8+5] == 0x00) {
                    printf("      >>> CONFIRMED: followed by and ecx, 0x7FF (May 2026 format)\n");
                    printf("      >>> DecryptComponent at RVA 0x%llX\n", match_rva);
                }

                shr_matches++;
                if (shr_matches >= 10) break;
            }
        }
    }
    printf("\n");

    // =========================================================================
    // Step 4: Test entity decryption with Key1 + various Key2 candidates
    // =========================================================================

    printf("[4] Testing entity decryption...\n");

    uint64_t entity_list_ptr = R(g_base + 0x37EC5E0);
    printf("    Entity list ptr = 0x%llX (RVA 0x%llX)\n\n", entity_list_ptr, entity_list_ptr - g_base);

    struct EntitySlot { uint64_t entity; uint64_t pad; };
    EntitySlot raw_list[4096] = {};
    read_buf(entity_list_ptr, (uint8_t*)raw_list, sizeof(raw_list));

    // Count valid entity slots
    int valid_slots = 0;
    for (int i = 0; i < 4096; i++) if (raw_list[i].entity != 0) valid_slots++;
    printf("    Non-zero entity slots: %d\n\n", valid_slots);

    // Test with top 20 Key2 candidates
    printf("    Testing decryption with different Key2 + table combinations:\n");
    printf("    %-4s %-20s %-20s %s\n", "Try", "Key2", "TableBase", "Result");

    struct Combo {
        const char* label;
        uint64_t table_base;
        uint32_t table_mask;
    };

    Combo combos[] = {
        {"OldTable+0x7FF", g_base + 0x38996E0, 0x7FF},
        {"OldTable+0x7FF*8", g_base + 0x38996E0, 0x7FF},
        {"NewTable+0x7FF", g_base + 0x3800000, 0x7FF},
    };

    uint64_t test_entity = raw_list[0].entity;
    if (!test_entity) {
        for (int i = 0; i < 100; i++)
            if (raw_list[i].entity) { test_entity = raw_list[i].entity; break; }
    }

    for (size_t ci = 0; ci < 3; ci++) {
        auto& combo = combos[ci];
        for (size_t ki = 0; ki < std::min(key2_candidates.size(), (size_t)5); ki++) {
            uint64_t Key2 = key2_candidates[ki].first;
            int64_t k2_off = key2_candidates[ki].second;

            uint64_t table_base = combo.table_base;
            uint32_t table_mask = combo.table_mask;

            int successes = 0;
            for (int e = 0; e < 16; e++) {
                uint64_t ent = raw_list[e].entity;
                if (!ent) continue;
                uint64_t link = DecryptComponent(ent, 0x34, Key1, Key2, table_base, table_mask);
                if (link && link > 0x10000) successes++;
            }

            printf("    %-4zu %016llX (+%lld)  0x%llX  %d/16 links\n",
                   ki, Key2, k2_off, table_base, successes);
        }
    }

    // =========================================================================
    // Step 5: Brute force — test ALL key pairs in region
    // =========================================================================

    printf("\n[5] Brute-force testing ALL pairs in region...\n");

    int best_pair = -1;
    int best_links = 0;
    uint64_t best_k2 = 0;

    // Test first 50 Key2 candidates against first 8 entities
    for (size_t ki = 0; ki < std::min(key2_candidates.size(), (size_t)50); ki++) {
        uint64_t k2 = key2_candidates[ki].first;

        int links = 0;
        for (int e = 0; e < 8; e++) {
            uint64_t ent = raw_list[e].entity;
            if (!ent) continue;
            uint64_t link = DecryptComponent(ent, 0x34, Key1, k2, g_base + 0x38996E0, 0x7FF);
            if (link && link > 0x10000) links++;
        }

        if (links > best_links) {
            best_links = links;
            best_pair = (int)ki;
            best_k2 = k2;
        }
    }

    printf("    Best: Key2=0x%llX (+%lld) -> %d/8 links\n",
           best_k2, key2_candidates[best_pair].second, best_links);

    // =========================================================================
    // Step 6: Dump DecryptTable (old format) contents at calculated indices
    // =========================================================================

    printf("\n[6] DecryptTable (0x38996E0) at Key1-derived indices:\n");

    uint64_t table = g_base + 0x38996E0;
    unsigned idx_hi = (unsigned)((Key1 >> 0x34) & 0x7FF);
    unsigned idx_lo = (unsigned)(Key1 & 0x7FF);

    printf("    Table[%u] @ +0x%X = ", idx_hi, idx_hi * 8);
    uint64_t t_hi = R(table + idx_hi * 8);
    printf("0x%016llX\n", t_hi);

    printf("    Table[%u] @ +0x%X = ", idx_lo, idx_lo * 8);
    uint64_t t_lo = R(table + idx_lo * 8);
    printf("0x%016llX\n", t_lo);

    // Also read raw byte at the byte-offset index (if table is byte array)
    printf("\n    Byte-offset read (if 1-byte entries):\n");
    printf("    [table + 0x%X] u64    = 0x%016llX\n", idx_hi, R(table + idx_hi));
    printf("    [table + 0x%X] u64    = 0x%016llX\n", idx_lo, R(table + idx_lo));

    VMMDLL_Close(g_vmm);
    printf("\nDone.\n");
    return 0;
}
