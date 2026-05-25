// =============================================================================
// KeyScan — complete key resolution for May 2026 GetGlobalKey
// =============================================================================

#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>

#include "leechcore.h"
#include "vmmdll.h"

static VMM_HANDLE  g_vmm    = nullptr;
static DWORD       g_pid    = 0;
static uint64_t    g_base   = 0;
static uint64_t    g_size   = 0;

template<typename T>
T Read(uint64_t addr) {
    T buf{};
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), 0,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

void read_buf(uint64_t addr, uint8_t* buf, size_t sz) {
    for (size_t i = 0; i < sz; i += 8) {
        uint64_t qw = Read<uint64_t>(addr + i);
        memcpy(buf + i, &qw, (sz - i >= 8) ? 8 : (sz - i));
    }
}

inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    if (bits == 0) return x;
    return (x >> bits) | (x << (64 - bits));
}

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== KeyScan — May 2026 GetGlobalKey Resolution ===\n\n");

    // --- DMA init ---
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

    printf("[DMA] Base=0x%llX Size=0x%llX PID=%u\n\n", g_base, g_size, g_pid);

    // =========================================================================
    // Step 1: Read and emulate GetGlobalKey
    // =========================================================================

    uint64_t gk_rva = 0x581D20;
    uint8_t code[128] = {};
    read_buf(g_base + gk_rva, code, sizeof(code));

    printf("[1] GetGlobalKey code at RVA 0x%llX:\n  ", gk_rva);
    for (int i = 0; i < 0x60; i++) {
        printf("%02X ", code[i]);
        if ((i + 1) % 16 == 0 && i < 0x5F) printf("\n  ");
    }
    printf("\n\n");

    // Extract constants
    uint64_t lea_target = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0x8D && (code[i+2] & 0xC7) == 0x05) {
            int32_t disp = *(int32_t*)&code[i+3];
            lea_target = g_base + gk_rva + i + 7 + disp;
            printf("  LEA at +%d -> 0x%llX (RVA 0x%llX)\n", i, lea_target, lea_target - g_base);
            break;
        }
    }

    uint64_t const1 = 0, const2 = 0, const3 = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0xB9 && !const1) {
            memcpy(&const1, code + i + 2, 8);
            printf("  mov rcx, 0x%llX at +%d\n", const1, i);
            i += 9;
        } else if (code[i] == 0x48 && code[i+1] == 0xB8) {
            uint64_t val;
            memcpy(&val, code + i + 2, 8);
            if (!const2) { const2 = val; printf("  mov rax, 0x%llX at +%d (const2)\n", val, i); }
            else if (!const3 && val != const2) { const3 = val; printf("  mov rax, 0x%llX at +%d (const3)\n", val, i); }
            i += 9;
        }
    }

    printf("\n");

    // Compute decoded value (before sub-function call)
    // decoded = (const1 + ROR64(lea_target, 0xA)) ^ const2 - const3
    uint64_t step1 = const1 + ROR64(lea_target, 0xA);
    uint64_t step2 = step1 ^ const2;
    uint64_t decoded = step2 - const3;
    printf("[2] Decoded from IDA algo:\n");
    printf("  step1 = 0x%llX + ROR(lea,0xA) = 0x%llX\n", const1, step1);
    printf("  step2 = step1 ^ const2    = 0x%llX\n", step2);
    printf("  decoded = step2 - const3  = 0x%llX\n", decoded);
    printf("  decoded RVA             = 0x%llX\n\n", decoded - g_base);

    // Now emulate the sub-function call: *rcx += stashed_const
    // Sub-fn at code + 0x45 (CALL E8): disp = *(int32*)&code[0x41]
    // Actually from key2_probe output: sub_fn adds 0x87CD0626CAAD43BC to *rcx
    int32_t call_rel = *(int32_t*)&code[0x41];
    uint64_t sub_fn = g_base + gk_rva + 0x40 + 5 + call_rel;
    uint8_t sub_code[48] = {};
    read_buf(sub_fn, sub_code, 48);
    printf("[3] Sub-function at RVA 0x%llX:\n  ", sub_fn - g_base);
    for (int i = 0; i < 48; i++) {
        printf("%02X ", sub_code[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n");

    // Parse sub-function: first instruction should be mov rax, imm64
    uint64_t sub_add_const = 0;
    if (sub_code[0] == 0x48 && sub_code[1] == 0xB8) {
        memcpy(&sub_add_const, sub_code + 2, 8);
        printf("  sub_fn ADD constant = 0x%llX\n\n", sub_add_const);
    } else if (sub_code[0] == 0x48 && sub_code[1] == 0xB9) {
        memcpy(&sub_add_const, sub_code + 2, 8);
        printf("  sub_fn ADD constant = 0x%llX (via rcx)\n\n", sub_add_const);
    }

    // After sub function: decoded += sub_add_const
    uint64_t decoded_final = decoded + sub_add_const;
    printf("[4] After sub-function (decoded + 0x%llX):\n", sub_add_const);
    printf("  decoded_final          = 0x%llX\n", decoded_final);
    printf("  decoded_final RVA      = 0x%llX\n\n", decoded_final - g_base);

    // After: xor rax, 0xF5 → global store
    uint64_t global_stored = decoded_final ^ 0xF5;
    printf("[5] Global store value (XOR 0xF5):\n");
    printf("  expected at 0x40405F8 = 0x%llX\n", global_stored);

    // Verify: read actual global store
    uint64_t actual_global = Read<uint64_t>(g_base + 0x40405F8);
    printf("  actual  at 0x40405F8 = 0x%llX", actual_global);
    if (actual_global == global_stored) printf("  *** MATCH ***\n\n");
    else printf("  *** MISMATCH ***\n\n");

    // =========================================================================
    // Step 2: The decoded_final value — is it a pointer or a key?
    // =========================================================================

    printf("[6] Probing decoded_final as pointer:\n");
    bool in_game = (decoded_final >= g_base && decoded_final < g_base + g_size);
    printf("  In game module range: %s\n", in_game ? "YES" : "NO");

    if (decoded_final > 0x10000 && decoded_final < 0x8000000000000000) {
        // Try reading as a structure: +0x38 = Key1, +0xB8 = Key2
        uint64_t k1 = Read<uint64_t>(decoded_final + 0x38);
        uint64_t k2 = Read<uint64_t>(decoded_final + 0xB8);
        printf("  [ptr+0x38] = 0x%llX", k1);
        if (k1 > 0x1000000000000000) printf(" *** VALID KEY1 ***");
        printf("\n  [ptr+0xB8] = 0x%llX", k2);
        if (k2 > 0x1000000000000000) printf(" *** VALID KEY2 ***");
        printf("\n");

        if (k1 > 0x1000000000000000 && k2 > 0x1000000000000000) {
            printf("\n*** FOUND VALID KEY PAIR FROM STRUCT POINTER ***\n");
            printf("Key1 = 0x%llX\n", k1);
            printf("Key2 = 0x%llX\n", k2);
            printf("Key1>>0x34 = 0x%X\n\n", (unsigned)(k1 >> 0x34));
        }

        // Also scan nearby for key-like values
        printf("\n  Scanning [ptr-0x200 .. ptr+0x200]:\n");
        int cnt = 0;
        for (int64_t off = -0x200; off <= 0x200 && cnt < 20; off += 8) {
            uint64_t v = Read<uint64_t>(decoded_final + off);
            if (v > 0x1000000000000000) {
                printf("    [ptr%+lld] = 0x%llX", off, v);
                if (off == 0x38) printf(" <-- Key1 candidate");
                if (off == 0xB8) printf(" <-- Key2 candidate");
                printf("\n");
                cnt++;
            }
        }
    }
    printf("\n");

    // =========================================================================
    // Step 3: Also check if decoded_final IS the key itself (or key^0xF5)
    // =========================================================================

    printf("[7] Treating decoded_final (XOR 0xF5) as actual Key1:\n");
    uint64_t alt_key1 = decoded_final ^ 0xF5;
    printf("  Key1_candidate = 0x%llX\n", alt_key1);
    printf("  (Key1>>0x34)&0x7FF = %u\n", (unsigned)((alt_key1 >> 0x34) & 0x7FF));
    printf("  Key1&0x7FF = %u\n", (unsigned)(alt_key1 & 0x7FF));

    // =========================================================================
    // Step 4: Scan heap-like regions for key structure pattern
    // =========================================================================

    printf("\n[8] Scanning for key structure pattern (2 large qwords at +0x38, +0xB8):\n");

    // Read the physical memory map to find valid RAM regions
    PVMMDLL_MAP_PHYSMEM pPhysMemMap = NULL;
    if (VMMDLL_Map_GetPhysMem(g_vmm, &pPhysMemMap) && pPhysMemMap->cMap > 0) {
        printf("  Physical memory map: %u regions\n", pPhysMemMap->cMap);

        // Search in the game's heap area — look for allocations in the 0x000001XX...0x7FF... space
        // We'll scan by reading chunks of memory and looking for key patterns
        // For now, do a targeted scan at common heap ranges

        // First, dump the global store area in detail
        uint64_t global_raw = Read<uint64_t>(g_base + 0x40405F8);
        printf("\n  Global store area (RVA 0x40405F8) detailed:\n");
        printf("    raw value    = 0x%016llX\n", global_raw);
        printf("    ^ 0xF5       = 0x%016llX\n", global_raw ^ 0xF5);

        // The global store value ^ 0xF5 might be the decoded key value, NOT a ptr
        // Let's check: is (global_raw ^ 0xF5) a valid Key1?
        uint64_t gk = global_raw ^ 0xF5;
        printf("    gk >> 0x34   = 0x%X\n", (unsigned)(gk >> 0x34));
        printf("    gk & 0x7FF   = %u\n", (unsigned)(gk & 0x7FF));

        // Also read from the key struct — the value should be accessed somehow
        printf("\n  Probing value at 0x40405F8 as heap pointer:\n");
        printf("    global_raw (no XOR) = 0x%016llX\n", global_raw);
        uint64_t k1_raw = Read<uint64_t>(global_raw + 0x38);
        uint64_t k2_raw = Read<uint64_t>(global_raw + 0xB8);
        printf("    [raw+0x38] = 0x%016llX %s\n", k1_raw,
               k1_raw > 0x1000000000000000 ? "***" : "");
        printf("    [raw+0xB8] = 0x%016llX %s\n", k2_raw,
               k2_raw > 0x1000000000000000 ? "***" : "");

        // And with XOR'd
        uint64_t xor_ptr = global_raw ^ 0xF5;
        printf("    global_raw ^ 0xF5 = 0x%016llX\n", xor_ptr);
        uint64_t k1_xor = Read<uint64_t>(xor_ptr + 0x38);
        uint64_t k2_xor = Read<uint64_t>(xor_ptr + 0xB8);
        printf("    [xor_ptr+0x38] = 0x%016llX %s\n", k1_xor,
               k1_xor > 0x1000000000000000 ? "***" : "");
        printf("    [xor_ptr+0xB8] = 0x%016llX %s\n", k2_xor,
               k2_xor > 0x1000000000000000 ? "***" : "");

        VMMDLL_MemFree(pPhysMemMap);
    }

    // =========================================================================
    // Step 5: Read the entire 0x4040000-0x4050000 region for patterns
    // =========================================================================

    printf("\n[9] Dump 0x4040000 - 0x4040900 (key storage region):\n");
    for (uint64_t rva = 0x4040000; rva <= 0x4040900; rva += 0x80) {
        uint64_t vals[16];
        read_buf(g_base + rva, (uint8_t*)vals, 0x80);
        bool has_interesting = false;
        for (int i = 0; i < 16; i++) {
            if (vals[i] > 0x1000000000000000 && vals[i] < 0x9000000000000000) {
                has_interesting = true;
                break;
            }
        }
        if (has_interesting) {
            printf("  RVA 0x%07llX:\n", rva);
            for (int i = 0; i < 16; i++) {
                printf("    [+%03X] = 0x%016llX", i*8, vals[i]);
                if (vals[i] > 0x1000000000000000 && vals[i] < 0x9000000000000000)
                    printf(" ***");
                printf("\n");
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nDone.\n");
    return 0;
}
