#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

static VMM_HANDLE g_vmm = nullptr;
static DWORD g_pid = 0;
static uint64_t g_base = 0;
static uint64_t g_size = 0;

#define R(a) Read<uint64_t>(a)

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
        uint64_t qw = R(cur);
        size_t cp = (sz - i >= 8) ? 8 : (sz - i);
        memcpy(buf + i, &qw, cp);
    }
}

int main() {
    printf("=== Simple Probe ===\n\n");

    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("FAIL: DMA init\n"); return 1; }
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");

    PVMMDLL_MAP_MODULE pMod = nullptr;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pMod, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pMod->cMap; i++)
            if (pMod->pMap[i].vaBase == g_base) { g_size = pMod->pMap[i].cbImageSize; break; }
        VMMDLL_MemFree(pMod);
    }
    if (!g_size) g_size = 0x5000000;

    printf("Base=0x%p Size=0x%p PID=%u\n\n", (void*)g_base, (void*)g_size, g_pid);

    // 1. Global Key
    uint64_t key_raw = R(g_base + 0x40405F8);
    uint64_t key1 = key_raw ^ 0xF5;
    printf("[KEY] Global store (RVA 0x40405F8) = 0x%p\n", (void*)key_raw);
    printf("[KEY] Key1                          = 0x%p\n", (void*)key1);
    printf("[KEY] Key1>>0x34                    = 0x%X\n\n", (unsigned)(key1 >> 52));

    // 2. Read code at the real DecryptComponent area
    printf("[CODE] RVA 0x666000 area:\n");
    for (int off = 0; off < 256; off += 16) {
        uint64_t addr = g_base + 0x666000 + off;
        uint64_t qw1 = R(addr);
        uint64_t qw2 = R(addr + 8);
        printf("  +%03X: %016llX %016llX\n", off, qw1, qw2);
        if (qw1 == 0 && qw2 == 0 && off >= 16) break;
    }
    printf("\n");

    // 3. Read the table at 0x3800000 and compute values with key1
    printf("[TABLE] Entries using Key1 index (Key1>>0x34 & 0x7FF=%u, Key1&0x7FF=%u):\n",
           (unsigned)((key1 >> 52) & 0x7FF), (unsigned)(key1 & 0x7FF));

    // Try reading table as: base + (index * 8)
    // Use key1_upper = (key1 >> 52) & 0x7FF
    unsigned idx1 = (unsigned)((key1 >> 52) & 0x7FF);
    unsigned idx2 = (unsigned)(key1 & 0x7FF);

    // Old table base + index*8
    uint64_t old_table = g_base + 0x38996E0;
    uint64_t v_old1 = R(old_table + idx1 * 8);
    uint64_t v_old2 = R(old_table + idx2 * 8);
    printf("  Old table[%u] = 0x%016llX\n", idx1, v_old1);
    printf("  Old table[%u] = 0x%016llX\n", idx2, v_old2);

    // Try with new table at 0x3800000
    // If entries are 24 bytes each (3 qwords): base + index*24
    uint64_t new_table = g_base + 0x3800000;
    uint64_t e0_v0 = R(new_table + idx1 * 24);
    uint64_t e0_v1 = R(new_table + idx1 * 24 + 8);
    uint64_t e0_v2 = R(new_table + idx1 * 24 + 16);
    printf("  New table entry[%u]: {0x%016llX, 0x%016llX, 0x%016llX}\n", idx1, e0_v0, e0_v1, e0_v2);

    // 4. Entity list candidates
    printf("\n[ENTITY] Potential entity list pointers:\n");
    for (uint64_t rva = 0x37EC580; rva <= 0x37EC9A0; rva += 8) {
        uint64_t val = R(g_base + rva);
        if (val > g_base && val < g_base + g_size && val != g_base) {
            // Read 48 bytes of the potential entity
            uint64_t ent[6];
            read_buf(val, (uint8_t*)ent, sizeof(ent));
            printf("  RVA 0x%07llX -> 0x%p: [0]=0x%p [1]=0x%p [2]=0x%p [3]=%lld\n",
                   rva, (void*)val, (void*)ent[0], (void*)ent[1], (void*)ent[2], ent[3]);
        }
    }

    // 5. Try to walk entity list with old DecryptComponent
    printf("\n[ENTITY2] Attempt old entity_base at 0x37E2AC0:\n");
    uint64_t old_ent_base = R(g_base + 0x37E2AC0);
    printf("  [0x37E2AC0] = 0x%016llX\n", old_ent_base);

    // Try the first valid entity pointer as entity_base
    uint64_t test_ent_base = R(g_base + 0x37EC5E0);
    printf("\n  Trying entity_list at RVA 0x37EC5E0 = 0x%p:\n", (void*)test_ent_base);
    if (test_ent_base > g_base && test_ent_base < g_base + g_size) {
        // Read 16 entity slots
        for (int i = 0; i < 16; i++) {
            uint64_t ent = R(test_ent_base + i * 16);
            uint64_t pad = R(test_ent_base + i * 16 + 8);
            if (ent != 0) {
                printf("    [%d] ent=0x%016llX pad=0x%016llX\n", i, ent, pad);
            }
        }
    }

    // 6. ViewMatrix scan
    printf("\n[VIEWM] Scan for viewmatrix near old offset:\n");
    for (uint64_t rva = 0x3EB5000; rva <= 0x3EB7000; rva += 8) {
        uint64_t val = R(g_base + rva);
        if (val > g_base && val < g_base + g_size) {
            float f[16];
            read_buf(val, (uint8_t*)f, sizeof(f));
            // Check for reasonable float matrix
            int outside = 0;
            for (int i = 0; i < 16; i++) if (f[i] > 1.5f || f[i] < -1.5f) outside++;
            if (outside >= 2 && outside <= 14) {
                printf("  PTR RVA 0x%07llX -> ", rva);
                for (int i = 0; i < 16; i++) printf(" %.2f", f[i]);
                printf("\n");
                break;
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
