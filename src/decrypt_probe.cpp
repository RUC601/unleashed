// =============================================================================
// Decrypt Probe — reads DecryptComponent function code for analysis
// =============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <thread>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

static VMM_HANDLE  g_vmm    = nullptr;
static DWORD       g_pid    = 0;
static uint64_t    g_base   = 0;

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

void dump_hex(const uint8_t* data, size_t len, const char* label) {
    printf("%s:\n  ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) printf("\n  ");
    }
    printf("\n\n");
}

static inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    return (x >> bits) | (x << (64 - bits));
}

int main() {
    printf("=== DecryptComponent Probe ===\n\n");

    // Init DMA
    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("FAIL: VMM init\n"); return 1; }

    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    printf("Base = 0x%llX, PID = %u\n\n", g_base, g_pid);

    // =====================================================================
    // 1. Read the DecryptComponent function code (around RVA 0x666016)
    // =====================================================================
    // The 'shr rcx,0x34; and ecx,0x7FF' was found at RVA 0x666016
    // Let's read from 0x665F00 to 0x666200 to get the full function
    uint64_t dc_start = g_base + 0x665F00;
    uint8_t dc_code[0x300] = {};
    read_buf(dc_start, dc_code, sizeof(dc_code));
    dump_hex(dc_code, 256, "[1] Code around DecryptComponent (0x665F00)");

    // =====================================================================
    // 2. Trace references: look for LEA instructions that reference data
    // =====================================================================
    printf("[2] LEA instructions referencing data (RIP-relative):\n");
    for (int i = 0; i < 0x300 - 7; i++) {
        if (dc_code[i] == 0x48 && dc_code[i+1] == 0x8D && (dc_code[i+2] & 0xC7) == 0x05) {
            int32_t disp = *(int32_t*)&dc_code[i+3];
            uint64_t target = (dc_start + i + 7 + disp) & 0xFFFFFFFF;
            printf("  LEA at +0x%X: reg=0x%02X -> RVA 0x%llX\n", i, dc_code[i+2], target);
        }
        // Also look for mov reg, [rip+disp] (loading from data)
        if (dc_code[i] == 0x48 && dc_code[i+1] == 0x8B && (dc_code[i+2] & 0xC7) == 0x05) {
            int32_t disp = *(int32_t*)&dc_code[i+3];
            uint64_t target = (dc_start + i + 7 + disp) & 0xFFFFFFFF;
            printf("  MOV reg,[rip] at +0x%X: reg=0x%02X -> RVA 0x%llX\n", i, dc_code[i+2], target);
        }
    }

    // =====================================================================
    // 3. Find the function start and end
    // =====================================================================
    printf("\n[3] Searching for function boundaries...\n");

    // Walk backwards from 0x666000 to find function prologue
    uint64_t search_addr = g_base + 0x665F00;
    for (int64_t off = 0x100; off >= 0; off -= 0x10) {
        uint8_t buf[16] = {};
        read_buf(search_addr + off, buf, 16);

        // Check for common prologue patterns
        bool is_prologue = false;
        const char* type = "";

        // mov [rsp+8], rcx or mov [rsp+0x10], rdx
        if (buf[0] == 0x48 && buf[1] == 0x89 && buf[2] == 0x4C && buf[3] == 0x24) {
            type = "mov [rsp+X], rcx"; is_prologue = true;
        }
        // push rbx/rbp/rsi/rdi
        if (buf[0] == 0x40 && buf[1] == 0x53) { type = "push rbx"; is_prologue = true; }
        if (buf[0] == 0x40 && buf[1] == 0x55) { type = "push rbp"; is_prologue = true; }
        if (buf[0] == 0x40 && buf[1] == 0x56) { type = "push rsi"; is_prologue = true; }
        if (buf[0] == 0x40 && buf[1] == 0x57) { type = "push rdi"; is_prologue = true; }
        // sub rsp, XX
        if (buf[0] == 0x48 && buf[1] == 0x83 && buf[2] == 0xEC) {
            type = "sub rsp, imm"; is_prologue = true;
        }
        if (buf[0] == 0x48 && buf[1] == 0x81 && buf[2] == 0xEC) {
            type = "sub rsp, imm32"; is_prologue = true;
        }
        // push r14/r15 (41 56 / 41 57)
        if (buf[0] == 0x41 && buf[1] == 0x56) { type = "push r14"; is_prologue = true; }
        if (buf[0] == 0x41 && buf[1] == 0x57) { type = "push r15"; is_prologue = true; }
        // push r12/r13 (41 54 / 41 55)
        if (buf[0] == 0x41 && buf[1] == 0x54) { type = "push r12"; is_prologue = true; }
        if (buf[0] == 0x41 && buf[1] == 0x55) { type = "push r13"; is_prologue = true; }

        if (is_prologue) {
            printf("  Found %s at RVA 0x%llX (search offset +0x%lld)\n",
                   type, (search_addr + off - g_base), off);
            // Dump 64 bytes from here
            uint8_t fn[64] = {};
            read_buf(search_addr + off, fn, 64);
            dump_hex(fn, 64, "  Function start");
            break;
        }
    }

    // =====================================================================
    // 4. Read the table at RVA 0x3800000 and try different interpretations
    // =====================================================================
    printf("[4] Table at RVA 0x3800000 - 3-qword structure analysis:\n");

    // Read 48 bytes (5 entries of 3 qwords = 15 qwords)
    uint8_t table_data[240] = {};
    read_buf(g_base + 0x3800000, table_data, sizeof(table_data));

    for (int entry = 0; entry < 10; entry++) {
        uint64_t val0 = *(uint64_t*)&table_data[entry * 24];
        uint64_t val1 = *(uint64_t*)&table_data[entry * 24 + 8];
        uint64_t val2 = *(uint64_t*)&table_data[entry * 24 + 16];
        printf("  [%d] 0x%016llX  0x%016llX  0x%016llX\n", entry, val0, val1, val2);
    }

    // Check: are val1 and val2 constant across entries?
    uint64_t first_val1 = *(uint64_t*)&table_data[8];
    uint64_t first_val2 = *(uint64_t*)&table_data[16];
    int same_v1 = 0, same_v2 = 0;
    for (int entry = 1; entry < 10; entry++) {
        if (*(uint64_t*)&table_data[entry * 24 + 8] == first_val1) same_v1++;
        if (*(uint64_t*)&table_data[entry * 24 + 16] == first_val2) same_v2++;
    }
    printf("  val1 (0x%016llX) is constant across entries? %d/9\n", first_val1, same_v1);
    printf("  val2 (0x%016llX) is constant across entries? %d/9\n", first_val2, same_v2);

    // =====================================================================
    // 5. Entity probe: check potential entity pointers at 0x37EC5E0
    // =====================================================================
    printf("\n[5] Entity probe at RVA 0x37EC5E0 area:\n");

    // Try reading what these pointers point to
    // The first few are: 0x7FF794B6F0E8, 0x7FF794B6F118, 0x7FF794B6F148
    // These are in range RVA ~0x3C3F0E8 etc (module-relative)
    uint64_t entity_ptrs[12] = {};
    read_buf(g_base + 0x37EC5E0, (uint8_t*)entity_ptrs, sizeof(entity_ptrs));

    for (int i = 0; i < 12; i++) {
        uint64_t ptr = entity_ptrs[i];
        if (ptr >= g_base && ptr < g_base + 0x4718000) {
            uint64_t entity_rva = ptr - g_base;
            printf("  [%d] PTR -> RVA 0x%llX :", i, entity_rva);

            // Read first 32 bytes of what this points to
            uint8_t ent_data[32] = {};
            read_buf(ptr, ent_data, 32);
            for (int j = 0; j < 32; j++) printf(" %02X", ent_data[j]);

            // Check if first qword looks like a vtable pointer
            uint64_t first_qw = *(uint64_t*)ent_data;
            if (first_qw >= g_base && first_qw < g_base + 0x4718000)
                printf(" [VTABLE? -> RVA 0x%llX]", first_qw - g_base);
            else if (first_qw == 0)
                printf(" [NULL]");
            else
                printf(" [val=0x%llX]", first_qw);

            printf("\n");
        }
    }

    // =====================================================================
    // 6. Try to find EntityBase by scanning for linked-list patterns
    // =====================================================================
    printf("\n[6] Entity list scan (searching for linked-list patterns):\n");

    // In old OW, entity list was at [g_base + entity_base_offset]
    // Let's scan 0x37E0000-0x37F0000 for values that look like pointers
    // to entity-like structures (within 0x3C3F000-0x3C40000 range)
    for (uint64_t probe = g_base + 0x37E0000; probe < g_base + 0x37F0000; probe += 8) {
        uint64_t val = Read<uint64_t>(probe);
        // Check if it points to the entity vtable region we discovered
        if (val >= g_base + 0x3C3F000 && val < g_base + 0x3C40000) {
            printf("  ENTITY PTR at RVA 0x%llX -> 0x%llX\n", probe - g_base, val - g_base);
            break; // just report first one
        }
    }

    // Also check the old entity_base area more broadly
    printf("\n[7] Full pointer scan in entity region (0x37E0000-0x37F0000, every 0x80 bytes):\n");
    for (uint64_t probe = g_base + 0x37E0000; probe < g_base + 0x37F0000; probe += 0x80) {
        uint64_t val = Read<uint64_t>(probe);
        if (val > g_base && val < g_base + 0x4718000) {
            uint64_t target_rva = val - g_base;
            // Check if the target looks like it has a vtable pointer
            uint64_t vtable = Read<uint64_t>(val);
            if (vtable > g_base && vtable < g_base + 0x4718000) {
                printf("  PTR at RVA 0x%llX -> RVA 0x%llX -> [vtable RVA 0x%llX]\n",
                       probe - g_base, target_rva, vtable - g_base);
            }
        }
    }

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
