// =============================================================================
// Final Probe — verify entity base, find viewmatrix, confirm new offsets
// =============================================================================

#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

static VMM_HANDLE  g_vmm    = nullptr;
static DWORD       g_pid    = 0;
static uint64_t    g_base   = 0;
static uint64_t    g_size   = 0;

template<typename T>
T Read(uint64_t addr) {
    T buf{};
    if (addr > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(addr)) {
        printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",
            (unsigned long long)addr);
        return buf;
    }
    DWORD read = 0;
    BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), &read,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    if ((!ok || read == 0) && addr > 0x7FFFFFFFFFFFULL) {
        printf("[MMAP] DMA read failed at physical-looking address 0x%llX (%s valid physical ranges)\n",
            (unsigned long long)addr,
            IsValidPhysicalAddress(addr) ? "inside" : "outside");
    }
    return buf;
}

void read_buf(uint64_t addr, uint8_t* buf, size_t sz) {
    for (size_t i = 0; i < sz; i += 8) {
        uint64_t cur = addr + i;
        if (cur > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(cur)) {
            memset(buf + i, 0, sz - i);
            printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges; zero-filled %llu bytes\n",
                (unsigned long long)cur,
                (unsigned long long)(sz - i));
            break;
        }
        uint64_t qw = Read<uint64_t>(cur);
        memcpy(buf + i, &qw, (sz - i >= 8) ? 8 : (sz - i));
    }
}

bool looks_valid_ptr(uint64_t v) {
    return v >= g_base && v < g_base + g_size;
}

// Read 32 bytes from a potential entity pointer and interpret
struct EntitySlot {
    uint64_t entity;   // [0x00] pointer to entity or encrypted
    uint64_t pad;      // [0x08] padding
};

int main() {
    printf("=== Final Probe ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("FAIL\n"); return 1; }
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");

    // Get image size
    PVMMDLL_MAP_MODULE pModMap = nullptr;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pModMap, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pModMap->cMap; i++) {
            if (pModMap->pMap[i].vaBase == g_base) {
                g_size = pModMap->pMap[i].cbImageSize;
                break;
            }
        }
        VMMDLL_MemFree(pModMap);
    }
    if (!g_size) g_size = 0x5000000;
    printf("Base: %I64X  Size: %I64X  PID: %u\n\n", g_base, g_size, g_pid);

    // =====================================================================
    // 1. Confirm Global Key
    // =====================================================================
    uint64_t key_global = Read<uint64_t>(g_base + 0x40405F8);
    uint64_t key1 = key_global ^ 0xF5;
    printf("[KEY] Global store at 0x40405F8 = 0x%I64X\n", key_global);
    printf("[KEY] Key1 (XOR 0xF5)           = 0x%I64X\n", key1);
    printf("[KEY] Key1 >> 0x34              = %I64X\n", key1 >> 0x34);

    // =====================================================================
    // 2. Entity list hunt — brute force scan
    // =====================================================================
    printf("\n[ENTITY] Scanning for entity list pointer...\n");

    // Entity list is typically a pointer that points to an array of EntitySlot structs
    // We're looking for: [some RVA] = pointer to array where first few entries are non-zero
    struct candidate_t {
        uint64_t rva;
        uint64_t value;
        int non_zero_entries;
        int valid_ptrs;
    };
    std::vector<candidate_t> candidates;

    // Scan .data section (RVA 0x3700000 to 0x4000000)
    for (uint64_t probe_rva = 0x37B0000; probe_rva < 0x3A00000; probe_rva += 8) {
        uint64_t val = Read<uint64_t>(g_base + probe_rva);
        if (!looks_valid_ptr(val)) continue;
        if (val == g_base) continue;

        // Read 16 EntitySlot entries from this candidate
        EntitySlot slots[16] = {};
        read_buf(val, (uint8_t*)slots, sizeof(slots));

        int non_zero = 0, valid = 0;
        for (int i = 0; i < 16; i++) {
            if (slots[i].entity != 0) non_zero++;
            if (looks_valid_ptr(slots[i].entity)) valid++;
        }

        // Score: non-zero entries suggest a list
        if (non_zero >= 1 && non_zero <= 16) {
            candidates.push_back({probe_rva, val, non_zero, valid});
        }
    }

    printf("  Found %zu candidate entity list pointers\n", candidates.size());

    // Show top candidates (sorted by score)
    std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) {
        return a.non_zero_entries > b.non_zero_entries;
    });

    for (int i = 0; i < std::min(20, (int)candidates.size()); i++) {
        auto& c = candidates[i];
        printf("  RVA 0x%016llX -> 0x%016llX  [non-zero:%d  valid:%d]\n",
               (unsigned long long)c.rva, (unsigned long long)c.value,
               c.non_zero_entries, c.valid_ptrs);

        // For top 5, dump first 8 entries
        if (i < 5) {
            EntitySlot slots[8] = {};
            read_buf(c.value, (uint8_t*)slots, sizeof(slots));
            for (int j = 0; j < 8; j++) {
                printf("    [%d] entity=0x%I64X  pad=0x%I64X\n",
                       j, slots[j].entity, slots[j].pad);
            }
        }
    }

    // =====================================================================
    // 3. Old entity_base area: check the vtable-pointers we found
    // =====================================================================
    printf("\n[ENTITY2] Checking vtable pointers at 0x37EC5E0 area...\n");
    uint64_t rva = 0x37EC5D0;
    while (rva <= 0x37EC990) {
        uint64_t val = Read<uint64_t>(g_base + rva);
        if (looks_valid_ptr(val)) {
            // Read the first 8 bytes of the pointed-to structure
            uint64_t vtable = Read<uint64_t>(val);
            printf("  RVA 0x%I64X -> 0x%I64X (RVA %I64X) -> vtable? 0x%I64X\n",
                   rva, val, val - g_base, vtable);
        }
        rva += 0x10;
    }

    // =====================================================================
    // 4. DecryptComponent code at correct address
    // =====================================================================
    printf("\n[DECRYPT] Reading code at RVA 0x666000:\n");
    uint8_t code[192] = {};
    read_buf(g_base + 0x666000, code, sizeof(code));

    // Check if it's all zeros (wrong address)
    bool all_zero = true;
    for (int i = 0; i < 192; i++) if (code[i] != 0) { all_zero = false; break; }
    if (all_zero) { printf("  ALL ZEROS at 0x666000 — code page not mapped or wrong RVA\n"); }
    else {
        printf("  ");
        for (int i = 0; i < 192; i++) {
            printf("%02X ", code[i]);
            if ((i + 1) % 16 == 0) printf("\n  ");
        }
        printf("\n");

        // Find shr/and pattern
        for (int i = 0; i < 180; i++) {
            if (code[i] == 0x48 && code[i+1] == 0xC1 && code[i+2] == 0xE9 && code[i+3] == 0x34) {
                printf("  [MATCH] shr rcx, 0x34 at +0x%X\n", i);
            }
            if (code[i] == 0x81 && code[i+1] == 0xE1 && code[i+2] == 0xFF) {
                printf("  [MATCH] and ecx, 0x%02X%02X at +0x%X\n", code[i+3], code[i+4], i);
            }
        }
    }

    // =====================================================================
    // 5. ViewMatrix hunt
    // =====================================================================
    printf("\n[VIEWMATRIX] Searching for 4x4 float matrix pointers:\n");
    // A 4x4 view matrix pointer typically points to 64 bytes of float data
    // where values are in range [-10.0, 10.0] and the last column is [0,0,0,1] or similar

    for (uint64_t probe_rva = 0x3EB0000; probe_rva < 0x3F00000; probe_rva += 8) {
        uint64_t val = Read<uint64_t>(g_base + probe_rva);
        if (!looks_valid_ptr(val)) continue;

        // Read 64 bytes (16 floats) from the potential matrix
        float floats[16] = {};
        read_buf(val, (uint8_t*)floats, sizeof(floats));

        // Check for view/projection matrix characteristics:
        // - Values should be finite floats
        // - Should have some values outside [-1, 1] (camera position/projection)
        // - Should not be all zero or all identity
        bool check_ok = true;
        int outside_one = 0;
        for (int i = 0; i < 16; i++) {
            if (floats[i] != floats[i]) { check_ok = false; break; } // NaN check
            if (fabs(floats[i]) > 1.0f) outside_one++;
        }
        if (!check_ok || outside_one < 2) continue;

        printf("  VM_CANDIDATE RVA 0x%I64X ->", probe_rva);
        for (int i = 0; i < 16; i++) printf(" %.2f", floats[i]);
        printf("\n");
        break; // Just report first good one
    }

    // Also check old viewmatrix offset with new XOR scanning
    printf("\n[VIEWMATRIX] Brute-force XOR scan for viewmatrix pointer:\n");
    // A viewmatrix pointer (decrypted) usually points to somewhere in .data
    // Try common XOR patterns in the old viewmatrix area
    uint64_t vm_scan_start = g_base + 0x37F0000;
    uint64_t vm_scan_end   = g_base + 0x37FFFFF;

    for (uint64_t addr = vm_scan_start; addr < vm_scan_end; addr += 8) {
        uint64_t enc = Read<uint64_t>(addr);
        if (enc == 0 || enc == 0x6E776F || enc == 0x6E6B6E5500000007) continue;

        // Try: the viewmatrix pointer IS the encrypted value (just a regular pointer)
        if (looks_valid_ptr(enc)) {
            float f[4];
            read_buf(enc, (uint8_t*)f, sizeof(f));
            if (f[0] >= -200.0f && f[0] <= 200.0f && f[1] >= -200.0f && f[1] <= 200.0f) {
                printf("  PLAIN PTR at RVA %I64X: %I64X -> floats: %.2f %.2f %.2f %.2f\n",
                       addr - g_base, enc, f[0], f[1], f[2], f[3]);
            }
        }
    }

    // =====================================================================
    // 6. Summary
    // =====================================================================
    printf("\n========================================\n");
    printf("FINAL SUMMARY\n");
    printf("========================================\n");
    printf("  Base:           0x%I64X\n", g_base);
    printf("  Size:           %I64X\n", g_size);
    printf("  GlobalKey1 RVA: 0x40405F8\n");
    printf("  Key1 (actual):  0x%I64X\n", key1);

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
