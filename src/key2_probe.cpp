// =============================================================================
// Key2 Probe — finds the second global key for May 2026 Overwatch
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

static inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    if (bits == 0) return x;
    return (x >> bits) | (x << (64 - bits));
}

bool looks_like_key(uint64_t v) {
    if (v < 0x1000000000000000ULL) return false;
    // Check it's not ASCII
    uint8_t* b = (uint8_t*)&v;
    int ascii = 0;
    for (int i = 0; i < 8; i++) {
        if (b[i] >= 0x20 && b[i] <= 0x7E) ascii++;
    }
    if (ascii >= 6) return false; // probably a string
    // Check it's not code
    if ((v >> 32) == 0) return false;
    uint8_t lo = (uint8_t)v;
    if (lo == 0x48 || lo == 0x40 || lo == 0xCC || lo == 0xC3 || lo == 0xE8 || lo == 0xE9) return false;
    return true;
}

int main() {
    printf("=== Key2 Probe for Overwatch (May 2026) ===\n\n");

    // Init DMA
    printf("[1] Initialising DMA...\n");
    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("FAIL: VMM init\n"); return 1; }

    if (!VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid) || !g_pid) {
        printf("FAIL: Overwatch.exe not found\n"); return 1;
    }
    printf("PID = %u\n", g_pid);

    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    printf("Base = 0x%llX\n\n", g_base);

    // Read GetGlobalKey function
    uint64_t gk_addr = g_base + 0x581D20;
    uint8_t code[128] = {};
    for (int i = 0; i < 128; i += 8) {
        uint64_t qw = Read<uint64_t>(gk_addr + i);
        memcpy(code + i, &qw, 8);
    }

    printf("[2] GetGlobalKey code at RVA 0x581D20:\n  ");
    for (int i = 0; i < 0x60; i++) {
        printf("%02X ", code[i]);
        if ((i + 1) % 16 == 0 && i < 0x5F) printf("\n  ");
    }
    printf("\n\n");

    // Find LEA and constants
    uint64_t lea_target = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0x8D && (code[i+2] & 0xC7) == 0x05) {
            int32_t disp = *(int32_t*)&code[i+3];
            lea_target = gk_addr + i + 7 + disp;
            printf("LEA at +%d: target RVA = 0x%llX\n", i, lea_target - g_base);
            break;
        }
    }

    uint64_t const1 = 0, const2 = 0, const3 = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0xB9 && !const1) {
            memcpy(&const1, code + i + 2, 8);
            printf("const1 (mov rcx) at +%d: 0x%llX\n", i, const1);
            i += 9;
        } else if (code[i] == 0x48 && code[i+1] == 0xB8) {
            uint64_t val;
            memcpy(&val, code + i + 2, 8);
            if (!const2) { const2 = val; printf("const2 (mov rax #1) at +%d: 0x%llX\n", i, val); }
            else if (!const3 && val != const2) { const3 = val; printf("const3 (mov rax #2) at +%d: 0x%llX\n", i, val); }
            i += 9;
        }
    }

    uint64_t decoded = const1 + ROR64(lea_target, 0x0A);
    decoded ^= const2;
    decoded -= const3;
    printf("\nDecoded pointer = 0x%llX (RVA 0x%llX)\n", decoded, decoded - g_base);

    // Find the global store RVA
    uint64_t global_rva = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
            int32_t disp = *(int32_t*)&code[i+3];
            global_rva = (gk_addr + i + 7 + disp) - g_base;
            printf("Global store at code +%d: RVA = 0x%llX\n", i, global_rva);
            break;
        }
    }

    // ================================================================
    // PROBE: Dump the global store area in detail
    // ================================================================
    printf("\n[3] Dumping global store area (RVA 0x%llX):\n", global_rva);
    uint64_t global_val = Read<uint64_t>(g_base + global_rva);
    printf("  [global+0x00] = 0x%llX", global_val);
    if (looks_like_key(global_val)) printf(" *** KEY1 ***");
    printf("\n  [global+0x00] XOR 0xF5 = 0x%llX (actual key)\n", global_val ^ 0xF5);

    // Detailed dump: -0x100 to +0x100 from global
    printf("\n[4] All key-like values near global store:\n");
    int found = 0;
    for (int64_t off = -0x800; off <= 0x800 && found < 30; off += 8) {
        uint64_t v = Read<uint64_t>(g_base + global_rva + off);
        if (looks_like_key(v) || (off >= -0x40 && off <= 0x40 && v != 0)) {
            printf("  [global%+06lld] RVA 0x%llX = 0x%llX", off, global_rva + off, v);
            if (looks_like_key(v) && v != global_val) {
                printf(" *** KEY-LIKE ***");
                found++;
            }
            printf("\n");
        }
    }

    // ================================================================
    // Try the decoded pointer directly as a DMA address
    // ================================================================
    printf("\n[5] Probing decoded pointer 0x%llX via DMA:\n", decoded);
    for (int64_t off = -0x80; off <= 0x100; off += 8) {
        uint64_t v = Read<uint64_t>(decoded + off);
        if (v != 0 && (off >= -0x10 && off <= 0xC0)) {
            printf("  [decoded%+04lld] = 0x%llX", off, v);
            if (looks_like_key(v)) printf(" ***");
            printf("\n");
        }
    }

    // ================================================================
    // New approach: read the sub-function that generates keys
    // ================================================================
    // CALL is at code offset 0x40: E8 6B D8 FD FF
    int32_t call_rel = *(int32_t*)&code[0x41];
    uint64_t sub_fn = gk_addr + 0x40 + 5 + call_rel;
    printf("\n[6] Sub-function called at 0x%llX (RVA 0x%llX):\n", sub_fn, sub_fn - g_base);

    uint8_t sub_code[256] = {};
    for (int i = 0; i < 256; i += 8) {
        uint64_t qw = Read<uint64_t>(sub_fn + i);
        memcpy(sub_code + i, &qw, 8);
    }

    printf("  First 128 bytes:\n  ");
    for (int i = 0; i < 128; i++) {
        printf("%02X ", sub_code[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n\n");

    // Look for stores to global addresses in the sub-function
    printf("  Global stores in sub-function:\n");
    for (int i = 0; i < 250; i++) {
        if (sub_code[i] == 0x48 && sub_code[i+1] == 0x89 && sub_code[i+2] == 0x05) {
            int32_t disp = *(int32_t*)&sub_code[i+3];
            uint64_t target_rva = (sub_fn + i + 7 + disp) - g_base;
            uint64_t target_val = Read<uint64_t>(g_base + target_rva);
            printf("  mov [rip+disp], rax at sub+%d: global RVA 0x%llX = 0x%llX", i, target_rva, target_val);
            if (looks_like_key(target_val)) printf(" *** KEY-LIKE ***");
            printf("\n");
        }
        // Also check mov [rip+disp], rdx/r8/r9 etc which might store Key2
        if (i + 7 <= 256 && sub_code[i] == 0x48 && sub_code[i+1] == 0x89) {
            uint8_t modrm = sub_code[i+2];
            if (modrm == 0x15 || modrm == 0x0D || modrm == 0x1D || modrm == 0x25 || modrm == 0x2D || modrm == 0x35 || modrm == 0x3D) {
                int32_t disp = *(int32_t*)&sub_code[i+3];
                uint64_t target_rva = (sub_fn + i + 7 + disp) - g_base;
                uint64_t target_val = Read<uint64_t>(g_base + target_rva);
                const char* regs[] = {"rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi"};
                int reg = modrm >> 3;
                const char* regname = (reg < 8) ? regs[reg] : "?";
                printf("  mov [rip+disp], %s at sub+%d: global RVA 0x%llX = 0x%llX", regname, i, target_rva, target_val);
                if (looks_like_key(target_val)) printf(" *** KEY-LIKE ***");
                printf("\n");
            }
        }
    }

    // ================================================================
    // Summary
    // ================================================================
    printf("\n========================================\n");
    printf("SUMMARY\n");
    printf("========================================\n");
    printf("  Key1 (from global)   = 0x%llX\n", global_val);
    printf("  Key1 XOR 0xF5 (real) = 0x%llX\n", global_val ^ 0xF5);
    printf("  Key1>>0x34           = 0x%llX\n", (global_val ^ 0xF5) >> 0x34);

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
