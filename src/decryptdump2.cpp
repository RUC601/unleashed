// DecryptDump — scan for real DecryptComponent + test Key2 candidates
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
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

static VMM_HANDLE  g_vmm  = 0;
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
        size_t cp = (sz - i >= 8) ? 8 : (sz - i);
        memcpy(buf + i, &qw, cp);
    }
}

inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    return (x >> bits) | (x << (64 - bits));
}

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

int main() {
    printf("=== DecryptDump ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("[FAIL] DMA init\n"); return 1; }

    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");

    PVMMDLL_MAP_MODULE pMod = 0;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pMod, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pMod->cMap; i++)
            if (pMod->pMap[i].vaBase == g_base) { g_size = pMod->pMap[i].cbImageSize; break; }
        VMMDLL_MemFree(pMod);
    }
    if (!g_size) g_size = 0x5000000;

    printf("Base=0x%llX Size=0x%llX PID=%u\n\n", g_base, g_size, g_pid);

    // Step 1: Verified Key1
    uint64_t key1_raw = R(g_base + 0x40405F8);
    uint64_t Key1 = key1_raw ^ 0xF5;
    printf("[1] Key1 = 0x%llX ^ 0xF5 = 0x%llX\n", key1_raw, Key1);
    printf("    (Key1>>0x34)&0x7FF = %u\n", (unsigned)((Key1 >> 0x34) & 0x7FF));
    printf("    Key1&0x7FF = %u\n\n", (unsigned)(Key1 & 0x7FF));

    // Step 2: Scan for shr rcx,0x34 pattern
    printf("[2] Scanning for shr rcx,0x34 (48 C1 E9 34):\n");
    uint64_t rva = 0x1000;
    int found = 0;
    while (rva < g_size - 0x100 && found < 10) {
        uint8_t code[0x10000] = {};
        size_t chunk = (rva + 0x10000 > g_size) ? (g_size - rva) : 0x10000;
        read_buf(g_base + rva, code, chunk);
        for (size_t i = 0; i < chunk - 10 && found < 10; i++) {
            if (code[i]==0x48 && code[i+1]==0xC1 && code[i+2]==0xE9 && code[i+3]==0x34) {
                uint64_t match_rva = rva + i;
                printf("  Found at RVA 0x%llX: ", match_rva);
                for (int j = -4; j < 20; j++) printf("%02X ", code[i+j]);
                printf("\n");
                found++;
            }
        }
        rva += chunk;
    }
    printf("\n");

    // Step 3: Collect Key2 candidates and brute force
    printf("[3] Brute-force Key2 candidates:\n");

    uint64_t entity_list_ptr = R(g_base + 0x37EC5E0);
    printf("    Entity list ptr = 0x%llX\n", entity_list_ptr);

    struct { uint64_t entity; uint64_t pad; } raw_list[4096];
    read_buf(entity_list_ptr, (uint8_t*)raw_list, sizeof(raw_list));

    int valid = 0;
    for (int i = 0; i < 4096; i++) if (raw_list[i].entity) valid++;
    printf("    Non-zero slots: %d\n\n", valid);

    // Get candidates
    struct K2Cand { uint64_t val; int64_t off; };
    K2Cand candidates[200] = {};
    int ncand = 0;
    for (int64_t off = -0x1000; off <= 0x1000 && ncand < 200; off += 8) {
        uint64_t v = R(g_base + 0x40405F8 + off);
        if (v > 0x1000000000000000 && v != key1_raw) {
            candidates[ncand].val = v;
            candidates[ncand].off = off;
            ncand++;
        }
    }

    // Sort by abs(off)
    for (int i = 0; i < ncand; i++) {
        for (int j = i + 1; j < ncand; j++) {
            int64_t ai = candidates[i].off; if (ai < 0) ai = -ai;
            int64_t aj = candidates[j].off; if (aj < 0) aj = -aj;
            if (aj < ai) {
                K2Cand t = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = t;
            }
        }
    }

    printf("    Testing top 30 Key2 with 3 table combos...\n");
    printf("    %-4s %-22s %s\n", "Try", "Key2", "Links");

    uint64_t tables[3] = {
        g_base + 0x38996E0,
        g_base + 0x3800000,
        g_base + 0x389A700
    };

    for (int ki = 0; ki < 30 && ki < ncand; ki++) {
        uint64_t k2 = candidates[ki].val;
        int best_links = 0;
        uint64_t best_table = 0;

        for (int ti = 0; ti < 3; ti++) {
            int links = 0;
            for (int e = 0; e < 16; e++) {
                if (!raw_list[e].entity) continue;
                uint64_t r = DecryptComponent(raw_list[e].entity, 0x34, Key1, k2, tables[ti], 0x7FF);
                if (r && r > 0x10000) links++;
            }
            if (links > best_links) {
                best_links = links;
                best_table = tables[ti] - g_base;
            }
        }
        printf("    %-4d 0x%016llX  %d/16 (best table RVA=0x%llX)\n",
               ki, k2, best_links, best_table);
        if (best_links > 0) break; // stop on first hit
    }

    // Step 4: Dump tables at Key1 indices
    printf("\n[4] DecryptTable at Key1 indices:\n");
    unsigned idx_hi = (unsigned)((Key1 >> 0x34) & 0x7FF);
    unsigned idx_lo = (unsigned)(Key1 & 0x7FF);

    for (int ti = 0; ti < 4; ti++) {
        uint64_t t;
        const char* name;
        switch (ti) {
            case 0: t = g_base + 0x38996E0; name = "OldTable1"; break;
            case 1: t = g_base + 0x389A700; name = "OldTable2"; break;
            case 2: t = g_base + 0x3800000; name = "NewTable "; break;
            case 3: t = g_base + 0x37EC5E0; name = "EntityPtr"; break;
        }
        printf("  %s[%u] = 0x%016llX\n", name, idx_hi, R(t + idx_hi * 8));
        printf("  %s[%u] = 0x%016llX\n", name, idx_lo, R(t + idx_lo * 8));
        printf("  %s[byte+0x%X] = 0x%016llX\n", name, idx_hi, R(t + idx_hi));
        printf("  %s[byte+0x%X] = 0x%016llX\n\n", name, idx_lo, R(t + idx_lo));
    }

    VMMDLL_Close(g_vmm);
    printf("Done.\n");
    return 0;
}
