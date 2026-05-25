// =============================================================================
// Key2 Hunt v2 — trace the sub-function to find where Key2 is stored
// =============================================================================
#include <Windows.h>
#include <cstdio>
#include <cstdint>

#include "leechcore.h"
#include "vmmdll.h"

static VMM_HANDLE g_vmm = nullptr;
static DWORD g_pid = 0;
static uint64_t g_base = 0;

#define R(a) Read<uint64_t>(a)
template<typename T>
T Read(uint64_t addr) {
    T buf{};
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), 0,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

int main() {
    printf("=== Key2 Hunt v2 ===\n\n");
    LPSTR args[] = { (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0", (LPSTR)"-norefresh" };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) { printf("FAIL\n"); return 1; }
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    printf("Base=0x%p PID=%u\n\n", (void*)g_base, g_pid);

    // GetGlobalKey function: call is at offset +0x40 to sub-function
    // Sub-function at: gk_addr + 0x40 + 5 + rel32 = g_base + 0x581D20 + 0x45 + (-0x22795)
    // = g_base + 0x55F5D0
    uint64_t sub_fn = g_base + 0x55F5D0;
    printf("[1] Sub-function at RVA 0x55F5D0 (absolute 0x%p):\n", (void*)sub_fn);

    // The sub-function code:
    // 48 B8 BC 43 AD CA 26 06 CD 87    mov rax, 0x87CD0626CAAD43BC
    // 48 01 01                          add [rcx], rax
    // C3                                ret
    // This JUST computes Key1 = decoded_ptr + const
    printf("    Simple adder: Key1 = decoded + 0x87CD0626CAAD43BC\n\n");

    // But there's a SECOND function right after (at offset +0x10 = RVA 0x55F5E0):
    uint64_t sub_fn2 = g_base + 0x55F5E0;
    printf("[2] Second function at RVA 0x55F5E0:\n");
    uint64_t code_qw[16];
    for (int i = 0; i < 16; i++) code_qw[i] = R(sub_fn2 + i * 8);

    printf("    Raw code:\n    ");
    for (int i = 0; i < 16; i++) printf(" %016llX", code_qw[i]);
    printf("\n\n");

    // Let me decode this second function's instructions:
    // 40 53                push rbx
    // 48 83 EC 20          sub rsp, 20h
    // 48 8B D9             mov rbx, rcx
    // E8 B2 4A CF 01       call sub_XXXXXX_1   (target = sub_fn2 + 8 + 0x01CF4AB2)
    // 8B C8                mov ecx, eax
    // 48 8D 15 31 B8 96 02 lea rdx, [rip+0x0296B831]  ; rdx = sub_fn2 + 0x11 + 0x0296B831 = data ref
    // E8 84 4B CF 01       call sub_XXXXXX_2   (target = sub_fn2 + 0x18 + 0x01CF4B84)
    // 48 89 03             mov [rbx], rax
    // 48 83 C4 20          add rsp, 20h
    // 5B                   pop rbx
    // C3                   ret

    // The LEA rdx references data at RVA: sub_fn2_RVA(0x55F5E0) + 0x11 + 0x0296B831
    uint64_t lea_target = 0x55F5E0 + 0x11 + 0x0296B831;
    printf("[3] LEA target RVA = 0x%llX\n", lea_target);

    // Read the data structure at this RVA
    printf("    Data at RVA 0x%llX:\n", lea_target);
    for (int i = 0; i < 32; i++) {
        uint64_t v = R(g_base + lea_target + i * 8);
        printf("    [+%02X] 0x%016llX\n", i*8, v);
    }

    // The real question: does this second function write to a GLOBAL?
    // It does: mov rbx, rcx (save rcx = pointer to output)
    // Then: mov [rbx], rax (write result to *rcx)
    // This OVERWRITES the same memory location [rsp+0x30] that Key1 was stored to!

    // But wait, this second function is NOT called by GetGlobalKey.
    // Let me look for who calls this second function.

    // Actually, let me try a different approach. Let me look at the code
    // that CONSUMES GlobalKey1 and GlobalKey2. In the old code, both were
    // used in DecryptComponent. In the new code, maybe only one is needed.

    // Let me check if there's a second global store NEARBY.
    printf("\n[4] Scanning for Key2 candidates near global stores:\n");

    // Check if there are other 'mov [rip+disp], rax' patterns in nearby functions
    // The global store is at RVA 0x40405F8
    // Key2 might be at a completely different RVA

    // Look for data values that look like Key2:
    // Key1 = 0x8EBDBE6CA542AAB2, upper 16 bits = 0x8EBD
    // Key2 should have similar entropy but different value

    // Scan the entire .data section for similar-looking keys
    printf("    Searching for Key2-like values (> 0x1000000000000000, not code-like):\n");
    int found = 0;
    for (uint64_t rva = 0x3700000; rva < 0x4100000 && found < 10; rva += 0x1000) {
        uint64_t v = R(g_base + rva);
        // Key criteria: high bits set, not ASCII, not all same pattern
        if (v > 0x1000000000000000ULL && v < 0xF000000000000000ULL) {
            // Check it's not ASCII-heavy
            uint8_t* b = (uint8_t*)&v;
            int ascii = 0;
            for (int j = 0; j < 8; j++) if (b[j] >= 0x20 && b[j] <= 0x7E) ascii++;
            if (ascii < 5) {
                printf("    RVA 0x%07llX: 0x%016llX\n", rva, v);
                found++;
            }
        }
    }

    // Also check: maybe Key2 = Key1 with some transformation
    printf("\n[5] Testing Key2 theories:\n");
    uint64_t key1 = R(g_base + 0x40405F8) ^ 0xF5;
    printf("    Key1                = 0x%016llX\n", key1);
    printf("    Key1 ^ 0xFFFFFFFFFFFFFFFF = 0x%016llX\n", key1 ^ 0xFFFFFFFFFFFFFFFFULL);
    printf("    ~Key1               = 0x%016llX\n", ~key1);
    printf("    ROR(Key1, 32)       = 0x%016llX\n", ((key1 >> 32) | (key1 << 32)));
    printf("    Key1 * 0x9E3779B97F4A7C15 = 0x%016llX (golden ratio)\n", key1 * 0x9E3779B97F4A7C15ULL);

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter...\n");
    std::getchar();
    return 0;
}
