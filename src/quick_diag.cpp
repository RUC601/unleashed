// Quick diagnostic: dump entity structure + decryption table
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <intrin.h>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

static VMM_HANDLE g_vmm = nullptr;
static DWORD g_pid = 0;
static uint64_t g_base = 0, g_size = 0;

template<typename T>
T R(uint64_t addr) {
    T buf{};
    if (addr > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(addr)) {
        printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",
            (unsigned long long)addr);
        return buf;
    }
    DWORD dw = 0;
    BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), &dw,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    if ((!ok || dw == 0) && addr > 0x7FFFFFFFFFFFULL) {
        printf("[MMAP] DMA read failed at physical-looking address 0x%llX (%s valid physical ranges)\n",
            (unsigned long long)addr,
            IsValidPhysicalAddress(addr) ? "inside" : "outside");
    }
    return buf;
}

void rb(uint64_t addr, uint8_t* buf, size_t sz) {
    for (size_t i = 0; i < sz; i += 8) {
        uint64_t cur = addr + i;
        if (cur > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(cur)) {
            memset(buf + i, 0, sz - i);
            printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges; zero-filled %llu bytes\n",
                (unsigned long long)cur,
                (unsigned long long)(sz - i));
            break;
        }
        uint64_t qw = R<uint64_t>(cur);
        size_t cp = (sz - i >= 8) ? 8 : (sz - i);
        memcpy(buf + i, &qw, cp);
    }
}

bool looks(uint64_t v) { return v >= g_base && v < g_base + g_size; }

int main() {
    printf("=== Quick Diag ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("DMA fail\n"); return 1; }

    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");

    PVMMDLL_MAP_MODULE pMod = nullptr;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pMod, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pMod->cMap; i++)
            if (pMod->pMap[i].vaBase == g_base) { g_size = pMod->pMap[i].cbImageSize; break; }
        VMMDLL_MemFree(pMod);
    }
    if (!g_size) g_size = 0x5000000;
    printf("Base=0x%llX Size=0x%X PID=%u\n\n", g_base, (unsigned)g_size, g_pid);

    // ---- Keys ----
    uint64_t key_raw = R<uint64_t>(g_base + 0x40405F8);
    uint64_t K1 = key_raw ^ 0xF5;
    uint64_t K2 = 0;
    for (int64_t off = -0x200; off <= 0x200; off += 8) {
        if (off == 0) continue;
        uint64_t v = R<uint64_t>(g_base + 0x40405F8 + off);
        if (v > 0x1000000000000000 && v != key_raw && v != K1) { K2 = v; break; }
    }
    printf("[KEY] K1=0x%016llX  K2=0x%016llX\n", K1, K2);
    printf("[KEY] K1>>0x34=0x%X (idx=%u)  K1&0x7FF=%u\n\n",
           (unsigned)(K1>>52), (unsigned)((K1>>52)&0x7FF), (unsigned)(K1&0x7FF));

    // ---- 1. Dump raw entity slots ----
    printf("=== [1] RAW ENTITY SLOTS (first 16) ===\n");
    uint64_t ent_list = R<uint64_t>(g_base + 0x37EC5E0);
    printf("Entity list ptr: 0x%llX (RVA 0x%llX)\n\n", ent_list, ent_list - g_base);

    struct { uint64_t e, pad; } slots[16];
    rb(ent_list, (uint8_t*)slots, sizeof(slots));
    for (int i = 0; i < 16; i++) {
        printf("  [%2d] ent=0x%016llX  pad=0x%016llX", i, slots[i].e, slots[i].pad);
        if (looks(slots[i].e)) printf("  [in range, RVA=0x%llX]", slots[i].e - g_base);
        printf("\n");
    }

    // ---- 2. Dump first valid entity structure ----
    printf("\n=== [2] FIRST VALID ENTITY STRUCTURE DUMP ===\n");
    uint64_t ent = 0;
    for (int i = 0; i < 16; i++) {
        if (looks(slots[i].e)) { ent = slots[i].e; break; }
    }
    if (!ent) { printf("No valid entity found!\n"); }
    else {
    printf("Entity addr: 0x%llX (RVA 0x%llX)\n", ent, ent - g_base);

    // Dump entity bytes: 0x00-0x140
    uint8_t ent_data[0x180];
    memset(ent_data, 0, sizeof(ent_data));
    rb(ent, ent_data, sizeof(ent_data));
    printf("  [0x000-0x040]: "); for (int j=0;j<64;j++) printf("%02X ",ent_data[j]); printf("\n");
    printf("  [0x040-0x080]: "); for (int j=0;j<64;j++) printf("%02X ",ent_data[0x40+j]); printf("\n");
    printf("  [0x080-0x0C0]: "); for (int j=0;j<64;j++) printf("%02X ",ent_data[0x80+j]); printf("\n");
    printf("  [0x0C0-0x100]: "); for (int j=0;j<64;j++) printf("%02X ",ent_data[0xC0+j]); printf("\n");
    printf("  [0x100-0x140]: "); for (int j=0;j<64;j++) printf("%02X ",ent_data[0x100+j]); printf("\n");

    // Key offsets for DecryptComponent
    uint64_t at_80 = *(uint64_t*)&ent_data[0x80];
    uint64_t at_110 = *(uint64_t*)&ent_data[0x110];
    uint8_t  at_130 = ent_data[0x130];
    printf("\n  [0x80]  = 0x%016llX  %s\n", at_80, looks(at_80) ? "(in range)" : "");
    printf("  [0x110] = 0x%016llX  %s\n", at_110, looks(at_110) ? "(in range)" : "");
    printf("  [0x130] = 0x%02X\n", at_130);

    // ---- 3. Try DecryptComponent step by step ----
    printf("\n=== [3] DECRYPT STEP-BY-STEP (idx=0x34 LINK) ===\n");
    {
        uint64_t parent = ent;
        uint8_t idx = 0x34;
        uint64_t v5 = idx / 0x3F;

        uint64_t v6 = R<uint64_t>(parent + 8*(uint32_t)v5 + 0x110);
        printf("  v6 (parent+0x110) = 0x%016llX\n", v6);
        if (!v6) { printf("  ** FAIL at v6 **\n"); }
        else {
            uint64_t v7 = ((1ull << (idx & 0x3F)) & v6) >> (idx & 0x3F);
            uint64_t v3 = (1ull << (idx & 0x3F)) - 1;
            uint64_t v8 = (v3 & v6) - (((v3 & v6) >> 1) & 0x5555555555555555);
            printf("  v7 = 0x%016llX  v8 = 0x%016llX\n", v7, v8);

            uint64_t inner = R<uint64_t>(parent + 0x80);
            printf("  inner_ptr (parent+0x80) = 0x%016llX\n", inner);
            if (!inner) { printf("  ** FAIL at inner_ptr **\n"); }
            else if (!looks(inner)) { printf("  ** WARNING: inner_ptr not in game range! **\n"); }
            else {
                uint8_t byte_at_130 = R<uint8_t>((uint32_t)v5 + parent + 0x130);
                printf("  byte_at_130 = 0x%02X (%u)\n", byte_at_130, byte_at_130);

                uint64_t popcnt = ((0x101010101010101 * (
                    ((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333) +
                    (((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333)) >> 4))
                )) & 0xF0F0F0F0F0F0F0F0) >> 0x38;
                printf("  popcnt = %llu\n", popcnt);

                uint64_t v9_addr = inner + 8 * (byte_at_130 + popcnt);
                printf("  v9_addr = 0x%016llX (inner + 8*%u)\n", v9_addr, (unsigned)(byte_at_130 + popcnt));
                uint64_t v9 = R<uint64_t>(v9_addr);
                printf("  v9 = 0x%016llX\n", v9);
                if (!v9) { printf("  ** FAIL at v9 **\n"); }
                else {
                    // ---- 4. Table values ----
                    printf("\n=== [4] DECRYPT TABLE VALUES ===\n");
                    unsigned idx1 = (unsigned)((K1 >> 52) & 0x7FF);
                    unsigned idx2 = (unsigned)(K1 & 0x7FF);

                    uint64_t old_t1 = R<uint64_t>(g_base + 0x38996E0 + idx1);
                    uint64_t old_t2 = R<uint64_t>(g_base + 0x38996E0 + idx2);
                    printf("  OLD table [0x38996E0 + %u] = 0x%016llX\n", idx1, old_t1);
                    printf("  OLD table [0x38996E0 + %u] = 0x%016llX\n", idx2, old_t2);

                    uint64_t new_t1_v0 = R<uint64_t>(g_base + 0x3800000 + idx1 * 24);
                    uint64_t new_t1_v1 = R<uint64_t>(g_base + 0x3800000 + idx1 * 24 + 8);
                    uint64_t new_t1_v2 = R<uint64_t>(g_base + 0x3800000 + idx1 * 24 + 16);
                    printf("  NEW table [0x3800000 + %u*24] = {0x%016llX, 0x%016llX, 0x%016llX}\n",
                           idx1, new_t1_v0, new_t1_v1, new_t1_v2);

                    uint64_t new_t2_v0 = R<uint64_t>(g_base + 0x3800000 + idx2 * 24);
                    uint64_t new_t2_v1 = R<uint64_t>(g_base + 0x3800000 + idx2 * 24 + 8);
                    uint64_t new_t2_v2 = R<uint64_t>(g_base + 0x3800000 + idx2 * 24 + 16);
                    printf("  NEW table [0x3800000 + %u*24] = {0x%016llX, 0x%016llX, 0x%016llX}\n",
                           idx2, new_t2_v0, new_t2_v1, new_t2_v2);

                    uint64_t new8_t1 = R<uint64_t>(g_base + 0x3800000 + idx1 * 8);
                    uint64_t new8_t2 = R<uint64_t>(g_base + 0x3800000 + idx2 * 8);
                    printf("  NEW table [0x3800000 + %u*8]  = 0x%016llX\n", idx1, new8_t1);
                    printf("  NEW table [0x3800000 + %u*8]  = 0x%016llX\n", idx2, new8_t2);

                    // ---- 5. Complete DecryptComponent with ALL table variants ----
                    printf("\n=== [5] COMPLETE DECRYPT ATTEMPTS ===\n");
                    auto try_decrypt = [&](const char* label, uint64_t dummy, uint64_t dummy2) -> void {
                        uint64_t v10 = (unsigned int)v9 | v9 & 0xFFFFFFFF00000000ui64 ^
                                       ((uint64_t)((unsigned int)v9 + 0x71747EF8) << 0x20);
                        uint64_t v11 = K2 ^ ((unsigned int)v9 | v10 & 0xFFFFFFFF00000000ui64 ^
                                       ((uint64_t)(unsigned int)(v10 + __ROL4__(HIDWORD(dummy), 1)) << 0x20));
                        uint64_t v12 = -(int)v7 & ((unsigned int)v11 |
                                       ((unsigned int)v11 | v11 & 0xFFFFFFFF00000000ui64 ^
                                        ((uint64_t)((unsigned int)v11 ^ ~(unsigned int)dummy2) << 0x20)) &
                                        0xFFFFFFFF00000000ui64 ^
                                        ((uint64_t)((unsigned int)v11 ^ 0xDFBFA250) << 0x20));
                        printf("  %-30s -> 0x%016llX  %s\n", label, v12, looks(v12) ? "(in range)" : "");
                    };

                    try_decrypt("OLD table byte-offset", old_t1, old_t2);
                    try_decrypt("NEW table [idx*24].v0", new_t1_v0, new_t2_v0);
                    try_decrypt("NEW table [idx*24].v1", new_t1_v1, new_t2_v1);
                    try_decrypt("NEW table [idx*24].v2", new_t1_v2, new_t2_v2);
                    try_decrypt("NEW table [idx*8]", new8_t1, new8_t2);

                    uint64_t old8_t1 = R<uint64_t>(g_base + 0x38996E0 + idx1 * 8);
                    uint64_t old8_t2 = R<uint64_t>(g_base + 0x38996E0 + idx2 * 8);
                    try_decrypt("OLD table [idx*8] qword", old8_t1, old8_t2);
                }
            }
        }
    }

    // ---- 6. Dump first few entries of various tables ----
    printf("\n=== [6] TABLE DUMP (first 5 entries) ===\n");
    printf("  OLD table 0x38996E0 (byte offset):\n");
    for (int i = 0; i < 5; i++) {
        uint64_t v = R<uint64_t>(g_base + 0x38996E0 + i);
        printf("    [+%d] = 0x%016llX\n", i, v);
    }
    printf("  NEW table 0x3800000 (24-byte entries):\n");
    for (int i = 0; i < 5; i++) {
        uint64_t v0 = R<uint64_t>(g_base + 0x3800000 + i*24);
        uint64_t v1 = R<uint64_t>(g_base + 0x3800000 + i*24 + 8);
        uint64_t v2 = R<uint64_t>(g_base + 0x3800000 + i*24 + 16);
        printf("    [%d] v0=0x%016llX v1=0x%016llX v2=0x%016llX\n", i, v0, v1, v2);
    }

    // ---- 7. Try to find which entity is the local player by scanning for HeroID directly ----
    printf("\n=== [7] DIRECT HEROID SCAN (known HeroID values) ===\n");
    // Known hero IDs all start with 0x2E00000000000xx pattern
    // Try reading at offset 0xD0 from each valid entity
    for (int i = 0; i < 16; i++) {
        if (!looks(slots[i].e)) continue;
        uint64_t e = slots[i].e;
        // The entity might have heroID at different offsets
        // Try reading from the entity itself and common component offsets
        uint64_t d0_val = R<uint64_t>(e + 0xD0);
        if ((d0_val >> 48) == 0x2E00 || (d0_val & 0xFFF0000000000000) == 0x2E0000000000000) {
            printf("  [%d] entity+0xD0 = 0x%016llX (HeroID!)\n", i, d0_val);
        }
    }

    // Scan entity memory for HeroID-like values
    printf("\n  Scanning entity [0] memory for HeroID-like values:\n");
    if (looks(slots[0].e)) {
        for (int off = 0; off < 0x200; off += 8) {
            uint64_t v = R<uint64_t>(slots[0].e + off);
            if ((v >> 48) == 0x2E00 && v != 0) {
                printf("    [+0x%03X] = 0x%016llX  (possible HeroID)\n", off, v);
            }
        }
    }

    } // end if(ent) block

// cleanup tag kept for reference but no goto used
cleanup:
    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
