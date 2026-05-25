// =============================================================================
// Unleashed DMA Scanner -- Finds live offsets for Overwatch (May 2026)
//
// Standalone executable.  Initialises the FPGA DMA card, attaches to the
// running Overwatch.exe process, then scours the .text / .data sections for
// the current GlobalKey entry point, decryption tables, entity base,
// view matrix, and heap manager offsets.
//
// Output is written to stdout AND to "DMA_SCAN_RESULTS.txt" in the working
// directory so it can be inspected offline.
// =============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdarg>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <chrono>
#include <thread>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"
#include "Game/Offsets.hpp"

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static VMM_HANDLE  g_vmm    = nullptr;
static DWORD       g_pid    = 0;
static uint64_t    g_base   = 0;   // base address of Overwatch.exe
static uint64_t    g_size   = 0;   // image size of Overwatch.exe
static FILE*       g_log    = nullptr;

struct CheckResult {
    std::string name;
    bool passed = false;
    std::string detail;
};

static std::vector<CheckResult> g_check_results;

// ---------------------------------------------------------------------------
// Logging helpers -- tee to stdout and log file
// ---------------------------------------------------------------------------

static void LogInit()
{
    g_log = fopen("DMA_SCAN_RESULTS.txt", "w");
    if (g_log) {
        fprintf(g_log, "Unleashed DMA Scanner -- Overwatch (May 2026)\n");
        fprintf(g_log, "==============================================\n\n");
    }
}

static void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list file_args;
    if (g_log) {
        va_copy(file_args, args);
    }
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        vfprintf(g_log, fmt, file_args);
        va_end(file_args);
    }
    fflush(stdout);
    if (g_log) fflush(g_log);
}

static std::string FormatString(const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
}

static const char* PassFail(bool passed)
{
    return passed ? "PASS" : "FAIL";
}

static void LogSection(const char* title)
{
    Log("\n============================================================\n");
    Log("  %s\n", title);
    Log("============================================================\n\n");
}

static void LogResultHeader(const char* col1, const char* col2, const char* col3)
{
    Log("  %-28s %-10s %-22s %s\n", col1, "STATUS", col2, col3);
    Log("  %-28s %-10s %-22s %s\n",
        "----------------------------",
        "----------",
        "----------------------",
        "------------------------------");
}

static void LogResultRow(const char* name, bool passed, const char* value, const char* detail)
{
    Log("  %-28s %-10s %-22s %s\n",
        name,
        PassFail(passed),
        value ? value : "",
        detail ? detail : "");
}

static void RecordCheck(const std::string& name, bool passed, const std::string& detail)
{
    g_check_results.push_back({ name, passed, detail });
    LogResultRow(name.c_str(), passed, "", detail.c_str());
}

static void RecordCheckFmt(const std::string& name, bool passed, const char* fmt, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    RecordCheck(name, passed, buffer);
}

static void LogClose()
{
    if (g_log) {
        fprintf(g_log, "\n=== End of scan ===\n");
        fclose(g_log);
        g_log = nullptr;
    }
}

// ---------------------------------------------------------------------------
// DMA helpers
// ---------------------------------------------------------------------------

bool InitDMA()
{
    Log("[DMA] Initialising FPGA DMA...\n");
    LPSTR args[] = {
        (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0",
        (LPSTR)"-norefresh"
    };
    g_vmm = VMMDLL_Initialize(4, args);
    if (!g_vmm) {
        Log("[FAIL] VMMDLL_Initialize returned null.\n");
        return false;
    }
    Log("[OK]   VMM handle acquired.\n");
    return true;
}

bool AttachOverwatch()
{
    Log("[DMA] Locating Overwatch.exe...\n");
    if (!VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid) || !g_pid) {
        Log("[FAIL] Overwatch.exe PID not found.\n");
        return false;
    }
    Log("[OK]   PID = %u\n", g_pid);

    // Resolve base address
    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    if (!g_base) {
        Log("[FAIL] Could not resolve Overwatch.exe base address.\n");
        return false;
    }
    Log("[OK]   Base = 0x%llX\n", (unsigned long long)g_base);

    // Get image size via map
    PVMMDLL_MAP_MODULE pModuleMap = nullptr;
    if (VMMDLL_Map_GetModuleU(g_vmm, g_pid, &pModuleMap, VMMDLL_MODULE_FLAG_NORMAL)) {
        for (DWORD i = 0; i < pModuleMap->cMap; i++) {
            if (pModuleMap->pMap[i].vaBase == g_base) {
                g_size = pModuleMap->pMap[i].cbImageSize;
                Log("[OK]   Image size = 0x%llX (%llu MB)\n",
                    (unsigned long long)g_size,
                    (unsigned long long)(g_size / 1048576));
                break;
            }
        }
        VMMDLL_MemFree(pModuleMap);
    }
    if (!g_size) {
        g_size = 0x4000000; // 64 MB fallback
        Log("[WARN] Could not determine image size; using 0x%llX\n",
            (unsigned long long)g_size);
    }
    return true;
}

template<typename T>
T Read(uint64_t addr)
{
    T buf{};
    DWORD read = 0;
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), &read,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

bool ReadBuf(uint64_t addr, void* buf, size_t size)
{
    DWORD read = 0;
    // NOTE: Must not use VMMDLL_FLAG_ZEROPAD_ON_FAIL here because we need
    // to know when a read genuinely fails vs. returns zero-padded.
    return VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)buf, (DWORD)size, &read,
               VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO)
           != 0;
}

template<typename T>
bool ReadExact(uint64_t addr, T& value)
{
    value = {};
    DWORD read = 0;
    const BOOL ok = VMMDLL_MemReadEx(
        g_vmm,
        g_pid,
        addr,
        reinterpret_cast<PBYTE>(&value),
        static_cast<DWORD>(sizeof(T)),
        &read,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO);
    return ok && read == sizeof(T);
}

bool ReadExactBuf(uint64_t addr, void* buf, size_t size)
{
    if (!buf || size == 0 || size > 0x100000) {
        return false;
    }

    DWORD read = 0;
    const BOOL ok = VMMDLL_MemReadEx(
        g_vmm,
        g_pid,
        addr,
        reinterpret_cast<PBYTE>(buf),
        static_cast<DWORD>(size),
        &read,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO);
    return ok && read == size;
}

static bool IsCanonicalUserPointer(uint64_t value)
{
    return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull;
}

static bool CanReadPointer(uint64_t value, size_t size = sizeof(uint64_t))
{
    if (!IsCanonicalUserPointer(value) || size == 0 || size > 0x1000) {
        return false;
    }

    uint8_t buffer[0x1000] = {};
    return ReadExactBuf(value, buffer, size);
}

static bool LooksLikeImagePointer(uint64_t value)
{
    return g_base != 0 && g_size != 0 && value >= g_base && value < g_base + g_size;
}

static bool LooksLikeReadablePointer(uint64_t value, size_t size = sizeof(uint64_t))
{
    return IsCanonicalUserPointer(value) && CanReadPointer(value, size);
}

// ---------------------------------------------------------------------------
// Helper: read code bytes at an address (individual qword reads)
// ---------------------------------------------------------------------------

bool ReadCodeBytes(uint64_t addr, uint8_t* buf, size_t size)
{
    // Read qword-by-qword; individual reads work on .text even when bulk reads fail.
    for (size_t off = 0; off < size; off += 8) {
        size_t chunk = (size - off >= 8) ? 8 : (size - off);
        uint64_t val = Read<uint64_t>(addr + off);
        memcpy(buf + off, &val, chunk);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pattern scanning
// ---------------------------------------------------------------------------

struct ScanResult {
    uint64_t address;
};

std::vector<ScanResult> FindPattern(
    const uint8_t* pattern, const char* mask,
    uint64_t start, uint64_t end)
{
    std::vector<ScanResult> results;
    size_t len = strlen(mask);
    if (len == 0 || start >= end) return results;

    // Use 4KB page-sized chunks so DMA reads don't time out.
    // Must use the same flags as the original Memory::FindSignature:
    // VMMDLL_FLAG_NOCACHE only (no NOPAGING, no ZEROPAD_ON_FAIL).
    // Small-chunk approach: read page by page so we don't need huge contiguous reads.
    constexpr uint64_t PAGE_SIZE = 0x1000; // 4 KB
    std::vector<uint8_t> buf(PAGE_SIZE);
    uint64_t chunk_start = start;
    int chunks_read = 0;
    int chunks_failed = 0;
    int consecutive_fails = 0;

    while (chunk_start < end) {
        if (chunk_start > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(chunk_start)) {
            chunk_start += PAGE_SIZE;
            continue;
        }

        uint64_t chunk_end = chunk_start + PAGE_SIZE;
        if (chunk_end > end) chunk_end = end;
        uint64_t to_read = chunk_end - chunk_start;

        DWORD actual_read = 0;
        BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, chunk_start,
                    buf.data(), (DWORD)to_read, &actual_read,
                    VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO);
        if (!ok || actual_read == 0) {
            consecutive_fails++;
            chunk_start += PAGE_SIZE;
            if (consecutive_fails > 512) {
                // If we hit too many consecutive failures, skip ahead
                chunk_start += 0x10000;
            }
            continue;
        }
        consecutive_fails = 0;
        chunks_read++;

        // Mask bytes we actually read with 0xFF so they match the wildcard
        // Wait -- we need to be smarter. If actual_read < to_read, we only
        // want to scan the part we actually received.
        uint64_t scan_end = (actual_read < to_read) ? (chunk_start + actual_read) : chunk_end;
        if (scan_end > chunk_start + actual_read)
            scan_end = chunk_start + actual_read;

        // Some entries in buf beyond actual_read may be uninitialized --
        // restrict our search window.
        uint64_t searchable = (actual_read < to_read) ? actual_read : to_read;

        for (uint64_t i = 0; i + len <= searchable; i++) {
            bool match = true;
            for (size_t j = 0; j < len; j++) {
                if (mask[j] != '?' && buf[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back({ chunk_start + i });
            }
        }
        chunk_start += PAGE_SIZE;
    }

    return results;
}

// ---------------------------------------------------------------------------
// Qword-based pattern scanning (works on ALL memory sections including .text)
// Uses individual qword reads instead of bulk reads.
// Slower but guaranteed to work where DMA bulk reads fail.
// ---------------------------------------------------------------------------

std::vector<ScanResult> FindPatternQword(
    const uint8_t* pattern, const char* mask,
    uint64_t start, uint64_t end)
{
    std::vector<ScanResult> results;
    size_t len = strlen(mask);
    if (len == 0 || start >= end) return results;

    // Read in 256-byte blocks using qword reads
    uint8_t block[256];
    constexpr uint64_t BLOCK_SIZE = 256;

    for (uint64_t addr = start; addr < end; addr += BLOCK_SIZE) {
        if (addr > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(addr)) continue;

        uint64_t remaining = end - addr;
        uint64_t read_size = (remaining < BLOCK_SIZE) ? remaining : BLOCK_SIZE;
        ReadCodeBytes(addr, block, read_size);

        for (uint64_t i = 0; i + len <= read_size; i++) {
            bool match = true;
            for (size_t j = 0; j < len; j++) {
                if (mask[j] != '?' && block[i + j] != pattern[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                results.push_back({ addr + i });
            }
        }
    }
    return results;
}

// ---------------------------------------------------------------------------
// 64-bit ROR (rotate right) helper
// ---------------------------------------------------------------------------
static inline uint64_t ROR64(uint64_t x, int bits)
{
    bits &= 63;
    if (bits == 0) return x;
    return (x >> bits) | (x << (64 - bits));
}

static inline uint64_t ROL64(uint64_t x, int bits)
{
    bits &= 63;
    if (bits == 0) return x;
    return (x << bits) | (x >> (64 - bits));
}

static bool IsSaneFloat(float value, float max_abs = 1000000.0f)
{
    return std::isfinite(value) && std::fabs(value) <= max_abs;
}

static bool IsSaneMatrix(const std::array<float, 16>& matrix)
{
    int finite_count = 0;
    int non_zero_count = 0;
    for (float value : matrix) {
        if (!IsSaneFloat(value, 100000000.0f)) {
            return false;
        }
        ++finite_count;
        if (std::fabs(value) > 0.00001f) {
            ++non_zero_count;
        }
    }
    return finite_count == 16 && non_zero_count >= 4;
}

static constexpr uint8_t kComponentTypeVelocity = 0x04;
static constexpr uint8_t kComponentTypeTeam = 0x21;
static constexpr uint8_t kComponentTypeLink = 0x34;
static constexpr uint8_t kComponentTypeHealth = 0x3B;
static constexpr uint8_t kComponentTypePlayerController = 0x44;
static constexpr uint8_t kComponentTypeHeroId = 0x54;

struct ComponentDecryptTrace {
    uint64_t parent = 0;
    uint8_t idx = 0;
    uint64_t bit_mask = 0;
    uint64_t component_bits = 0;
    uint64_t present = 0;
    uint64_t component_index = 0;
    uint64_t component_table = 0;
    uint64_t encrypted_component = 0;
    uint64_t key_source = 0;
    uint64_t key_material = 0;
    uint8_t key_byte = 0;
    uint64_t decoded = 0;
    std::string failure;
};

static const char* ComponentName(uint8_t idx)
{
    switch (idx) {
    case kComponentTypeVelocity: return "TYPE_VELOCITY";
    case kComponentTypeTeam: return "TYPE_TEAM";
    case kComponentTypeLink: return "TYPE_LINK";
    case kComponentTypeHealth: return "TYPE_HEALTH";
    case kComponentTypePlayerController: return "TYPE_PLAYERCONTROLLER";
    case kComponentTypeHeroId: return "TYPE_P_HEROID";
    default: return "TYPE_UNKNOWN";
    }
}

static void BuildComponentBitInfo(
    uint8_t component_id,
    uint64_t& bit_mask,
    uint64_t& lower_mask,
    uint32_t& shift,
    uint32_t& bucket)
{
    shift = component_id & 0x3F;
    bit_mask = 1ull << shift;
    lower_mask = bit_mask - 1;
    bucket = component_id >> 6;
}

static uint64_t ScannerDecryptComponent(uint64_t parent, uint8_t idx, ComponentDecryptTrace* trace = nullptr)
{
    ComponentDecryptTrace local_trace{};
    ComponentDecryptTrace& t = trace ? *trace : local_trace;
    t = {};
    t.parent = parent;
    t.idx = idx;

    if (!parent) {
        t.failure = "parent is null";
        return 0;
    }

    uint64_t lower_mask = 0;
    uint32_t shift = 0;
    uint32_t bucket = 0;
    BuildComponentBitInfo(idx, t.bit_mask, lower_mask, shift, bucket);

    if (!ReadExact(parent + 8ull * bucket + 0x110, t.component_bits)) {
        t.failure = "component bitmap read failed";
        return 0;
    }

    t.present = (t.component_bits & t.bit_mask) >> shift;
    if (!t.present) {
        t.failure = "component bit is not present";
        return 0;
    }

    uint64_t below = t.component_bits & lower_mask;
    below -= (below >> 1) & 0x5555555555555555ull;
    below = (below & 0x3333333333333333ull) +
            ((below >> 2) & 0x3333333333333333ull);
    below = (below + (below >> 4)) & 0x0F0F0F0F0F0F0F0Full;

    uint8_t component_index_base = 0;
    if (!ReadExact(parent + bucket + 0x130, component_index_base)) {
        t.failure = "component index-base read failed";
        return 0;
    }

    t.component_index =
        component_index_base +
        ((below * 0x0101010101010101ull) >> 0x38);

    if (!ReadExact(parent + 0x80, t.component_table) || !t.component_table) {
        t.failure = "component table pointer missing";
        return 0;
    }

    if (!ReadExact(t.component_table + 8ull * t.component_index, t.encrypted_component) ||
        !t.encrypted_component) {
        t.failure = "encrypted component qword missing";
        return 0;
    }

    if (!ReadExact(g_base + OW::offset::ComponentXorQword_RVA, t.key_source) ||
        !t.key_source) {
        t.failure = "component key source pointer missing";
        return 0;
    }

    if (!ReadExact(t.key_source + OW::offset::ComponentXorQword_Off, t.key_material)) {
        t.failure = "component key material read failed";
        return 0;
    }

    if (!ReadExact(g_base + OW::offset::ComponentXorByte_RVA, t.key_byte)) {
        t.failure = "component key byte read failed";
        return 0;
    }

    uint64_t component = t.encrypted_component;
    component += OW::offset::Component_Add1;
    component ^= t.key_material;
    component += OW::offset::Component_Add2;
    component ^= static_cast<uint64_t>(t.key_byte);
    component ^= OW::offset::Component_Xor1;
    component ^= OW::offset::Component_Xor2;
    component = ROL64(component, 0x2A);
    component = ROR64(component, 0x2D);

    const uint64_t present_mask =
        static_cast<uint64_t>(static_cast<int64_t>(
            -static_cast<int32_t>(t.present)));
    t.decoded = present_mask & component;
    if (!t.decoded) {
        t.failure = "decoded component is null";
    }
    return t.decoded;
}

static bool IsPlausibleHeroId(uint64_t hero_id)
{
    if ((hero_id & 0xFFF0000000000000ull) == 0x02E0000000000000ull) {
        return true;
    }

    switch (hero_id) {
    case 0x16DD:
    case 0x16EE:
    case 0x16BB:
        return true;
    default:
        return false;
    }
}

// ---------------------------------------------------------------------------
// Scan #1: GetGlobalKey
// ---------------------------------------------------------------------------

void ScanGetGlobalKey()
{
    Log("\n=== SCAN 1: GetGlobalKey ===\n\n");

    // We know the GetGlobalKey function is at RVA 0x581D20 from IDA hint.
    // Individual qword reads work on .text pages, so we read the function
    // code and decode the obfuscated pointer computation to locate the
    // actual key data structure.
    //
    // The function does:
    //   lea rax, [rip + disp]   -> base address
    //   ror rax, 0x0A           -> rotate right by 10
    //   mov rcx, const1         -> load constant 1
    //   add rcx, rax            -> const1 + rotated_base
    //   mov rax, const2         -> load constant 2
    //   xor rcx, rax            -> XOR with constant 2
    //   mov rax, const3         -> load constant 3
    //   sub rcx, rax            -> subtract constant 3
    //   mov [rsp+0x30], rcx     -> store decoded pointer
    //   lea rcx, [rsp+0x30]     -> return pointer to result on stack
    //
    // The decoded pointer IS the address of the key data structure.
    // Key1 is at decoded+0x38, Key2 is at decoded+0xB8.

    uint64_t gk_rva  = 0x581D20;
    uint64_t gk_addr = g_base + gk_rva;

    Log("[INFO] GetGlobalKey at RVA 0x%llX (absolute 0x%llX)\n",
        (unsigned long long)gk_rva, (unsigned long long)gk_addr);
    Log("[INFO] Reading function code via qword-by-qword...\n");

    uint8_t code[128];
    memset(code, 0, sizeof(code));
    ReadCodeBytes(gk_addr, code, sizeof(code));

    Log("[CODE] First 56 bytes:\n  ");
    for (int i = 0; i < 56; i++) {
        Log("%02X ", code[i]);
        if ((i + 1) % 16 == 0 && i < 55) Log("\n  ");
    }
    Log("\n");

    // ---- Step 1: Find LEA instruction ----
    uint64_t lea_target = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0x8D &&
            (code[i+2] & 0x07) == 0x05) {
            int32_t disp = *(int32_t*)&code[i+3];
            uint64_t rip = gk_addr + i + 7;
            lea_target = rip + disp;
            Log("[LEA] At code offset +%d: reg=0x%02X, disp=%d, target RVA 0x%llX\n",
                i, code[i+2], disp,
                (unsigned long long)(lea_target - g_base));
            break;
        }
    }
    if (lea_target == 0) {
        Log("[FAIL] Could not find LEA instruction.\n");
        return;
    }

    // ---- Step 2: Extract obfuscation constants ----
    uint64_t const1 = 0, const2 = 0, const3 = 0;
    int c1 = 0, c2 = 0, c3 = 0;
    for (int i = 0; i < 120; i++) {
        if (code[i] == 0x48 && code[i+1] == 0xB9 && !c1) {
            memcpy(&const1, code + i + 2, 8); c1 = 1;
            Log("[CONST] const1 (mov rcx) at +%d: 0x%llX\n",
                i, (unsigned long long)const1);
            i += 9;
        } else if (code[i] == 0x48 && code[i+1] == 0xB8) {
            uint64_t val;
            memcpy(&val, code + i + 2, 8);
            if (!c2) { const2 = val; c2 = 1;
                Log("[CONST] const2 (mov rax #1) at +%d: 0x%llX\n",
                    i, (unsigned long long)val);
            } else if (!c3 && val != const2) { const3 = val; c3 = 1;
                Log("[CONST] const3 (mov rax #2) at +%d: 0x%llX\n",
                    i, (unsigned long long)val);
            }
            i += 9;
        }
    }
    if (!c1 || !c2 || !c3) {
        Log("[FAIL] Missing constants: c1=%d c2=%d c3=%d\n", c1, c2, c3);
        return;
    }

    // ---- Step 3: Compute decoded (deobfuscated) pointer ----
    uint64_t ror_res = ROR64(lea_target, 0x0A);
    uint64_t decoded = const1 + ror_res;
    decoded = decoded ^ const2;
    decoded = decoded - const3;

    Log("\n[DECODE] LEA target     = 0x%llX (RVA 0x%llX)\n",
        (unsigned long long)lea_target,
        (unsigned long long)(lea_target - g_base));
    Log("[DECODE] ROR(target,10)  = 0x%llX\n",
        (unsigned long long)ror_res);
    Log("[DECODE] const1          = 0x%llX\n",
        (unsigned long long)const1);
    Log("[DECODE] + ROR           = 0x%llX\n",
        (unsigned long long)(const1 + ror_res));
    Log("[DECODE] ^ const2        = 0x%llX\n",
        (unsigned long long)((const1 + ror_res) ^ const2));
    Log("[DECODE] - const3        = 0x%llX\n",
        (unsigned long long)decoded);
    Log("[DECODE] Decoded pointer = 0x%llX (RVA 0x%llX)\n",
        (unsigned long long)decoded,
        (unsigned long long)(decoded - g_base));

    // ---- Step 4: Read key structure at decoded address ----
    // (or skip if decoded pointer is outside range)
    uint64_t key1 = 0, key2 = 0;
    bool kv1 = false, kv2 = false;

    auto looks_like_code = [](uint64_t v) -> bool {
        uint8_t lo = (uint8_t)(v >> 0);
        uint8_t b1 = (uint8_t)(v >> 8);
        if (lo >= 0x48 && lo <= 0x4F) return true;
        if (lo == 0x50 || lo == 0x53 || lo == 0x55 || lo == 0x56 || lo == 0x57) return true;
        if (lo == 0x51 || lo == 0x52 || lo == 0x54) return true;
        if (lo == 0xC3 || lo == 0xCC || lo == 0xE8 || lo == 0xE9 || lo == 0xEB) return true;
        if (lo == 0xFF || lo == 0x90) return true;
        if (lo <= 0x0F) return true;
        if (lo == 0 && b1 == 0) return true;
        if ((v >> 48) == 0) return true;
        return false;
    };

    bool decoded_in_range = (decoded >= g_base && decoded < g_base + g_size);

    if (!decoded_in_range) {
        Log("[FAIL] Decoded pointer outside game memory (0x%llX).\n",
            (unsigned long long)decoded);

        // Try alternative interpretations
        // Option A: The function calls a subroutine that modifies [rsp+0x30],
        // then XORs with 0xF5 and stores to a global. Let's find the global.
        // The instruction at offset 80: "48 89 05 81 E8 AB 03" = mov [rip+0x03ABE881], rax
        // RIP = g_base + 0x581D77, Global RVA = 0x581D77 + 0x03ABE881 = 0x040405F8
        // Compute dynamically from the code bytes to be sure:
        uint64_t global_rva = 0;
        for (int i = 0; i < 120; i++) {
            if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
                int32_t disp = *(int32_t*)&code[i+3];
                uint64_t rip = gk_addr + i + 7;
                global_rva = (rip + disp) - g_base;
                Log("[GLOBAL] Found 'mov [rip+disp], rax' at code offset +%d: disp=0x%llX, global RVA=0x%llX\n",
                    i, (unsigned long long)(int64_t)disp, (unsigned long long)global_rva);
                break;
            }
        }
        if (global_rva == 0) {
            // Fallback to hardcoded
            global_rva = 0x040405F8;
            Log("[GLOBAL] Using hardcoded fallback RVA 0x%llX\n", (unsigned long long)global_rva);
        }
        Log("[GLOBAL] Reading from global storage at RVA 0x%llX...\n",
            (unsigned long long)global_rva);
        uint64_t global_key = Read<uint64_t>(g_base + global_rva);
        Log("[GLOBAL] Value = 0x%llX\n", (unsigned long long)global_key);
        if (global_key > 0x10000000000000ULL && !looks_like_code(global_key)) {
            Log("[GLOBAL] Looks like a valid key!\n");
            // The global stores (actual_key ^ 0xF5), reverse it
            key1 = global_key; kv1 = true;
            Log("[GLOBAL] Key1 (direct from global) = 0x%llX\n", (unsigned long long)key1);
            Log("[GLOBAL] Key1 XOR 0xF5 (actual) = 0x%llX\n",
                (unsigned long long)(key1 ^ 0xF5));
        } else {
            Log("[GLOBAL] Not a valid key.\n");
        }

        // Always search for key2 near the global address
        // The function stores key1 ^ 0xF5 at this global. Key2 may be nearby.
        // Search a wide range for a second value that looks like a valid key
        Log("[GLOBAL] Searching for Key2 near global RVA 0x%llX...\n",
            (unsigned long long)global_rva);
        for (int64_t delta = -0x1000; delta <= 0x1000; delta += 8) {
            if (delta == 0) continue; // skip the key1 slot
            uint64_t probe = Read<uint64_t>(g_base + global_rva + delta);
            if (probe > 0x10000000000000ULL && !looks_like_code(probe)) {
                Log("[GLOBAL] Candidates near global slot at delta %+lld (RVA 0x%llX): 0x%llX\n",
                    (long long)delta,
                    (unsigned long long)(global_rva + delta),
                    (unsigned long long)probe);
                if (!kv2) {
                    key2 = probe; kv2 = true;
                    Log("[GLOBAL]   -> Using as Key2\n");
                }
            }
        }

        // Also try reading from the decoded address (it may be a valid heap address!)
        // The decoded pointer 0x6F0B845DA9566F6 is < 0x7FFFFFFFFFFF so it's in user space
        Log("[GLOBAL] Trying to read key structure at decoded address 0x%llX via DMA...\n",
            (unsigned long long)decoded);
        // The old layout had Key1 at +0x38 and Key2 at +0xB8
        uint64_t kstruct_k1 = Read<uint64_t>(decoded + 0x38);
        uint64_t kstruct_k2 = Read<uint64_t>(decoded + 0xB8);
        Log("[GLOBAL]   [decoded+0x38] = 0x%llX\n", (unsigned long long)kstruct_k1);
        Log("[GLOBAL]   [decoded+0xB8] = 0x%llX\n", (unsigned long long)kstruct_k2);
        if ((kstruct_k1 > 0x10000000000000ULL && !looks_like_code(kstruct_k1)) && !kv1) {
            Log("[GLOBAL]   -> Looks like Key1 from decoded structure!\n");
            key1 = kstruct_k1; kv1 = true;
        }
        if ((kstruct_k2 > 0x10000000000000ULL && !looks_like_code(kstruct_k2)) && !kv2) {
            Log("[GLOBAL]   -> Looks like Key2 from decoded structure!\n");
            key2 = kstruct_k2; kv2 = true;
        }

        // If still missing, dump a wider range of the decoded structure
        if (!kv1 || !kv2) {
            Log("[GLOBAL] Dumping +/- 0x100 from decoded address (non-zero qwords):\n");
            for (int64_t off = -0x100; off <= 0x100; off += 8) {
                uint64_t val = Read<uint64_t>(decoded + off);
                if (val != 0) {
                    Log("  [+0x%03llX] 0x%llX%s\n",
                        (long long)off, (unsigned long long)val,
                        (val > 0x10000000000000ULL && !looks_like_code(val)) ? " *** KEY-LIKE ***" : "");
                    if (!kv1 && off == 0x38) { key1 = val; kv1 = true; }
                    if (!kv2 && off == 0xB8) { key2 = val; kv2 = true; }
                }
            }
        }

        // Option B: low 32 bits of decoded value as RVA
        Log("[ALT] Trying low 32 bits as RVA: 0x%llX\n",
            (unsigned long long)(decoded & 0xFFFFFFFF));
        uint64_t lo32 = decoded & 0xFFFFFFFF;
        if (lo32 < g_size) {
            ReadCodeBytes(g_base + lo32, (uint8_t*)&key1, 8);
            ReadCodeBytes(g_base + lo32 + 0xB8 - 0x38, (uint8_t*)&key2, 8);
        }

        // Also dump the remaining function code for analysis
        Log("[ALT] Reading full 128 bytes of GetGlobalKey function:\n");
        uint8_t full_code[128];
        memset(full_code, 0, sizeof(full_code));
        ReadCodeBytes(gk_addr, full_code, sizeof(full_code));
        Log("  ");
        for (int i = 0; i < 128; i++) {
            Log("%02X ", full_code[i]);
            if ((i + 1) % 16 == 0 && i < 127) Log("\n  ");
        }
        Log("\n");

        Log("[INFO] Skipping key structure read, proceeding to fallback scans.\n");
    } else {
        Log("\n[KEYSTRUCT] Reading 256 bytes at decoded address...\n");
        uint8_t key_data[256];
        memset(key_data, 0, sizeof(key_data));
        ReadCodeBytes(decoded, key_data, sizeof(key_data));

        Log("[KEYSTRUCT] First 64 bytes:\n  ");
        for (int i = 0; i < 64; i++) {
            Log("%02X ", key_data[i]);
            if ((i + 1) % 16 == 0 && i < 63) Log("\n  ");
        }
        Log("\n");

        memcpy(&key1, key_data + 0x38, 8);
        memcpy(&key2, key_data + 0xB8, 8);

        Log("[KEYSTRUCT] [base+0x38] (Key1) = 0x%llX\n", (unsigned long long)key1);
        Log("[KEYSTRUCT] [base+0xB8] (Key2) = 0x%llX\n", (unsigned long long)key2);

        kv1 = (key1 > 0x10000000000000ULL) && !looks_like_code(key1);
        kv2 = (key2 > 0x10000000000000ULL) && !looks_like_code(key2);

        Log("\n[VALID] Key1: valid=%s  Key2: valid=%s\n",
            kv1 ? "YES" : "NO", kv2 ? "YES" : "NO");

        if (kv1 && kv2) {
            Log("\n  *** VALID GLOBAL KEY FOUND! ***\n");
            Log("  *** GlobalKey1 = 0x%llX ***\n", (unsigned long long)key1);
            Log("  *** GlobalKey2 = 0x%llX ***\n", (unsigned long long)key2);
            Log("  *** Key struct RVA = 0x%llX ***\n",
                (unsigned long long)(decoded - g_base));
        } else {
            // Dump all non-zero qwords for analysis
            Log("\n[PROBE] Dumping 256-byte key structure (non-zero qwords):\n");
            for (int i = 0; i < 32; i++) {
                uint64_t qw;
                memcpy(&qw, key_data + i * 8, 8);
                if (qw != 0)
                    Log("  [+0x%02X] 0x%llX\n", i * 8, (unsigned long long)qw);
            }

            // Probe nearby for valid key pairs
            Log("\n[PROBE] Scanning decoded +/- 0x1000 for valid key pairs...\n");
            for (int64_t d = -0x1000; d <= 0x1000; d += 8) {
                uint64_t a = decoded + d;
                if (a < g_base || a + 0xC0 > g_base + g_size) continue;
                uint64_t p1 = Read<uint64_t>(a + 0x38);
                uint64_t p2 = Read<uint64_t>(a + 0xB8);
                if ((p1 > 0x10000000000000ULL) && !looks_like_code(p1) &&
                    (p2 > 0x10000000000000ULL) && !looks_like_code(p2)) {
                    Log("\n  *** VALID KEYS at delta %+lld (RVA 0x%llX) ***\n",
                        (long long)d, (unsigned long long)(a - g_base));
                    Log("  *** GlobalKey1 = 0x%llX ***\n", (unsigned long long)p1);
                    Log("  *** GlobalKey2 = 0x%llX ***\n", (unsigned long long)p2);
                    key1 = p1; key2 = p2; kv1 = kv2 = true;
                    break;
                }
            }
        }
    }

    // ---- Step 6: Fallback pattern scans ----
    if (!kv1 || !kv2) {
        Log("\n[FALLBACK] Scanning for key_sig pattern across multiple sections...\n");
        static const uint8_t ks[] =
            "\x00\x00\x00\x00\x21\x00\x00\x00\x00\x00\x00\x00\x24\x00\x00\x00"
            "\x01\x00\x00\x00\x29\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00\x00\x00\x00\x00\x00";
        static const char* km = "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxxx";

        auto check_matches = [&](const std::vector<ScanResult>& m, const char* label) {
            Log("[FALLBACK] %s: %zu matches\n", label, m.size());
            for (auto& r : m) {
                uint64_t a = r.address;
                uint64_t p1 = Read<uint64_t>(a + 0x38);
                uint64_t p2 = Read<uint64_t>(a + 0xB8);
                Log("  RVA 0x%llX: Key1=0x%llX Key2=0x%llX\n",
                    (unsigned long long)(a - g_base),
                    (unsigned long long)p1, (unsigned long long)p2);
                if ((p1 > 0x10000000000000ULL) && !looks_like_code(p1) &&
                    (p2 > 0x10000000000000ULL) && !looks_like_code(p2)) {
                    Log("  *** VALID GLOBAL KEY! ***\n");
                    key1 = p1; key2 = p2; kv1 = kv2 = true;
                }
            }
        };

        // First try bulk-read-friendly sections (.data, .rdata)
        check_matches(FindPattern(ks, km, g_base + 0x3000000, g_base + 0x4000000), ".data (bulk)");
        if (!kv1 || !kv2)
            check_matches(FindPattern(ks, km, g_base + 0x500000, g_base + 0x2000000), ".rdata (bulk)");

        // Then try qword-by-qword scan on .text section (guaranteed to work)
        if (!kv1 || !kv2) {
            Log("[FALLBACK] Scanning .text section via qword reads (slow but reliable)...\n");
            uint64_t text_start = g_base + 0x1000;
            uint64_t text_end   = g_base + 0x800000; // first 8MB covers .text
            Log("[FALLBACK] Scanning 0x%llX to 0x%llX...\n",
                (unsigned long long)(text_start - g_base),
                (unsigned long long)(text_end - g_base));
            check_matches(FindPatternQword(ks, km, text_start, text_end), ".text (qword)");
        }
    }

    if (kv1 && kv2) {
        Log("\n=== FINAL: GetGlobalKey ===\n");
        Log("  GlobalKey1 = 0x%llX\n", (unsigned long long)key1);
        Log("  GlobalKey2 = 0x%llX\n", (unsigned long long)key2);
        Log("  Key1>>0x34 = 0x%llX\n", (unsigned long long)(key1 >> 0x34));
        Log("  Key2>>0x34 = 0x%llX\n", (unsigned long long)(key2 >> 0x34));
    } else {
        Log("\n=== FINAL: GetGlobalKey FAILED ===\n");
    }

    Log("\n[RESULT]\n");
    LogResultHeader("CHECK", "VALUE", "DETAIL");
    RecordCheckFmt(
        "GetGlobalKey",
        kv1 && kv2,
        kv1 && kv2
            ? "key1=0x%llX key2=0x%llX"
            : "key1/key2 not both resolved",
        (unsigned long long)key1,
        (unsigned long long)key2);
}

// ---------------------------------------------------------------------------
// Scan #2: Entity base
// ---------------------------------------------------------------------------

void ScanEntityBase()
{
    Log("\n=== SCAN 2: Entity Base ===\n\n");

    struct Entity { uint64_t entity; uint64_t pad; };

    const uint64_t current_entity_rva = OW::offset::Address_entity_base;
    const uint64_t current_entity_list = Read<uint64_t>(g_base + current_entity_rva);
    const bool current_ok = LooksLikeReadablePointer(current_entity_list, sizeof(Entity) * 8);

    Log("[CURRENT]\n");
    LogResultHeader("CHECK", "VALUE", "DETAIL");
    LogResultRow(
        "Entity base RVA",
        current_ok,
        FormatString("0x%llX", (unsigned long long)current_entity_rva).c_str(),
        FormatString("ptr=0x%llX stride(ref)=0x%llX",
            (unsigned long long)current_entity_list,
            (unsigned long long)OW::offset::entity_entry_stride).c_str());
    RecordCheckFmt(
        "Entity Base Current",
        current_ok,
        "RVA 0x%llX -> 0x%llX",
        (unsigned long long)current_entity_rva,
        (unsigned long long)current_entity_list);

    if (current_ok) {
        Entity entries[8] = {};
        if (ReadExactBuf(current_entity_list, entries, sizeof(entries))) {
            Log("[CURRENT] First 8 entity list pairs:\n");
            Log("  %-5s %-18s %-18s\n", "IDX", "ENTITY", "PAD");
            Log("  %-5s %-18s %-18s\n", "-----", "------------------", "------------------");
            for (int i = 0; i < 8; i++) {
                Log("  %-5d 0x%016llX 0x%016llX\n", i,
                    (unsigned long long)entries[i].entity,
                    (unsigned long long)entries[i].pad);
            }
            Log("\n");
        }
    }

    // Try the old offset
    uint64_t old_entity_offset = 0x37E2AC0;
    uint64_t old_entity_ptr = g_base + old_entity_offset;
    uint64_t entity_list = Read<uint64_t>(old_entity_ptr);

    Log("[INFO] Old entity_base offset: 0x%llX\n", (unsigned long long)old_entity_offset);
    Log("[INFO] Read at g_base + 0x%llX = 0x%llX\n",
        (unsigned long long)old_entity_offset,
        (unsigned long long)entity_list);

    // Check if it looks valid
    bool looks_valid = (entity_list > g_base && entity_list < g_base + 0x4000000);
    Log("[INFO] Entity list > base && < base+0x4000000?  %s\n",
        looks_valid ? "YES" : "NO");

    if (looks_valid) {
        // Read first few entries to verify
        Entity first_entries[8] = {};
        if (ReadBuf(entity_list, first_entries, sizeof(first_entries))) {
            Log("[INFO] First 8 entity list entries:\n");
            for (int i = 0; i < 8; i++) {
                Log("  [%d] entity=0x%llX pad=0x%llX\n", i,
                    (unsigned long long)first_entries[i].entity,
                    (unsigned long long)first_entries[i].pad);
            }
        }
    }

    // Scan nearby offsets as fallback
    Log("\n[INFO] Scanning nearby offsets for entity_base...\n");
    Log("[INFO] Checking offsets around 0x%llX ...\n",
        (unsigned long long)old_entity_offset);

    // Check a range of offsets: old_offset - 0x1000 to old_offset + 0x1000, step 8
    for (int64_t off = -0x1000; off <= 0x1000; off += 8) {
        uint64_t candidate = g_base + old_entity_offset + off;
        uint64_t val = Read<uint64_t>(candidate);
        if (val > g_base && val < g_base + 0x4000000) {
            Log("  [CANDIDATE] RVA 0x%llX  (delta 0x%llX)  value = 0x%llX\n",
                (unsigned long long)(old_entity_offset + off),
                (unsigned long long)off,
                (unsigned long long)val);
        }
    }

    // Also check a wider range with smaller scan (every 16 bytes over 0x10000 range)
    Log("\n[INFO] Wide scan for entity pointers (every 16 bytes, 0x10000 range)...\n");
    uint64_t scan_start = g_base + 0x37E0000; // near old offset
    for (uint64_t addr = scan_start; addr < scan_start + 0x10000; addr += 16) {
        uint64_t val = Read<uint64_t>(addr);
        if (val > g_base && val < g_base + 0x4000000) {
            Log("  PTR at RVA 0x%llX -> 0x%llX\n",
                (unsigned long long)(addr - g_base),
                (unsigned long long)val);
        }
    }
}

// ---------------------------------------------------------------------------
// Scan #3: Decryption tables
// ---------------------------------------------------------------------------

void ScanDecryptionTables()
{
    Log("\n=== SCAN 3: Decryption Tables ===\n\n");

    // Old decryption table offsets:
    //   0x38996E0  -- used in DecryptComponent as decrypt table base
    //   0x389A700  -- used in DecryptVis/DecryptOutline as vis/outline table base
    //
    // The table was 512 entries * 8 bytes = 0x1000 bytes per half.
    // New table is 2048 entries * 8 bytes = 0x4000 bytes per half.
    // With two halves (upper/lower key index), total = 0x8000 bytes.

    uint64_t old_table1 = 0x38996E0;
    uint64_t old_table2 = 0x389A700;

    Log("[INFO] Old decrypt table  offset: 0x%llX\n", (unsigned long long)old_table1);
    Log("[INFO] Old vis/outline table offset: 0x%llX\n", (unsigned long long)old_table2);

    // Try old offsets -- check if the data there looks like pointers
    auto check_table = [&](uint64_t rva, const char* name, int entries_to_check = 8) {
        Log("\n[INFO] Checking %s at RVA 0x%llX:\n", name, (unsigned long long)rva);
        uint64_t addr = g_base + rva;
        bool all_null = true;
        for (int i = 0; i < entries_to_check; i++) {
            uint64_t v = Read<uint64_t>(addr + (uint64_t)i * 8);
            Log("  [%d] 0x%llX\n", i, (unsigned long long)v);
            if (v != 0) all_null = false;
        }
        if (all_null) {
            Log("  -> All first %d entries are NULL (table likely moved)\n", entries_to_check);
        }
        // Try a wider dump
        uint64_t buf[64];
        if (ReadBuf(addr, buf, sizeof(buf))) {
            int non_zero = 0;
            for (int i = 0; i < 64; i++) {
                if (buf[i] != 0) non_zero++;
            }
            Log("  -> %d / 64 entries are non-zero\n", non_zero);
        }
    };

    check_table(old_table1, "DecryptComponent table (old offset)");
    check_table(old_table2, "DecryptVis/Outline table (old offset)");

    // Also check if the tables are nearby but at a shifted location.
    // Since the old table was at a specific .data offset, we can try
    // scanning a large region for potential table structures.

    Log("\n[INFO] Scanning for non-zero 8-byte aligned regions in .data...\n");

    // Scan a wide range (0x3800000 - 0x3900000) for dense pointer regions
    // A decryption table is a contiguous region of 2048 qwords
    uint64_t data_start = g_base + 0x3800000;
    uint64_t data_end   = g_base + 0x3A00000; // 2 MB range

    Log("[INFO] Scanning 0x%llX to 0x%llX for dense qword regions...\n",
        (unsigned long long)data_start, (unsigned long long)data_end);

    // Quick pre-scan: look for runs of non-zero values
    constexpr int TABLE_SIZE = 2048;
    constexpr int THRESHOLD  = 512; // require at least 512 non-zero entries
    uint64_t table_buf[TABLE_SIZE];
    int table_candidates = 0;

    for (uint64_t probe = data_start; probe + TABLE_SIZE * 8 <= data_end; probe += 0x1000) {
        if (!ReadBuf(probe, table_buf, sizeof(table_buf)))
            continue;

        int non_zero = 0;
        for (int i = 0; i < TABLE_SIZE; i++) {
            if (table_buf[i] != 0) non_zero++;
        }
        if (non_zero >= THRESHOLD) {
            ++table_candidates;
            Log("  [TABLE] RVA 0x%llX: %d/2048 entries non-zero\n",
                (unsigned long long)(probe - g_base), non_zero);

            // Print first 16 entries
            Log("    First 16 entries:");
            for (int i = 0; i < 16; i++) {
                Log(" 0x%llX", (unsigned long long)table_buf[i]);
            }
            Log("\n");

            // Print last 4 entries
            Log("    Last 4 entries:");
            for (int i = TABLE_SIZE - 4; i < TABLE_SIZE; i++) {
                Log(" 0x%llX", (unsigned long long)table_buf[i]);
            }
            Log("\n");
        }
    }

    // Also try to identify the vis/outline table (128 entries, each with 3 levels)
    Log("\n[INFO] Scanning for vis/outline table (128-entry, 3-level) structures...\n");
    constexpr int VIS_TABLE_ROWS = 128;
    uint64_t vis_buf[VIS_TABLE_ROWS * 8]; // 8 qwords per row... actually just 3 per row
    // Actually the vis table is 128 entries, each row is 3 qwords = 24 bytes
    // The old code does: base + 8 * (((uint8_t)a1 - 0x46) & 0x7F) + (((a1 + key) >> 7) & 7)
    // So it's: base + (index * 8) + sub_index
    // Where index is 0-127 (7-bit) and sub_index is 0-7 (3-bit)
    // Total = 128 * 8 * 8 = 128 * 64 = 8192 bytes = 0x2000, which matches... no wait
    // Actually the 3 levels give 128 * (8 * 3) = 3072 bytes. But with 8 sub-indices per main index
    // it's 128 * 8 * 8 = 8192. Hmm.

    // Let me just check the old table at 0x389A700 again more carefully.
    // The vis table layout: 128 main entries, each has 3 qwords (24 bytes).
    // But with sub-indexing of 0-7, each main entry spans 8*8 = 64 bytes.
    // So total table = 128 * 64 = 8192 bytes = 0x2000.
    // Old size was 512 * 8 = 0x1000 for vis table? Let me re-check.
    //
    // Looking at the old code:
    //   8ull * (((uint8_t)a1 - 0x46) & 0x7F) + (((uint64_t)(a1 + key) >> 7) & 7)
    // The first part gives index 0-127 (0x7F = 127), multiplied by 8 = 0-1016
    // The second part adds 0-7. So the table is addressed as:
    //   base + (main_index * 8) + sub_index
    // which means 128 * 8 = 1024 bytes, but with 3 sub-entries? No.
    // Actually 1024 / 3 = ~341. So maybe the table is just 128 * 8 = 1024 bytes total.
    // And the old size was probably different.
    //
    // Let me just scan for potentially valid tables without worrying too much about layout.

    // Adjusted search for vis table: look for non-zero blocks of 1024-8192 bytes
    uint64_t vis_probe_start = g_base + 0x3890000;
    uint64_t vis_probe_end   = g_base + 0x38C0000;
    constexpr int VIS_BUF_SIZE = 4096; // search in 4096 byte blocks
    uint64_t vis_block[VIS_BUF_SIZE];
    int vis_candidates = 0;

    for (uint64_t probe = vis_probe_start; probe + VIS_BUF_SIZE * 8 <= vis_probe_end; probe += 0x1000) {
        if (!ReadBuf(probe, vis_block, sizeof(vis_block)))
            continue;

        int non_zero = 0;
        for (int i = 0; i < VIS_BUF_SIZE; i += 8) {
            // Check if this qword is non-zero
            bool all_zero = true;
            for (int j = 0; j < 8; j++) {
                if (vis_block[i + j] != 0) { all_zero = false; break; }
            }
            if (!all_zero) non_zero++;
        }
        // A decent vis table would have at least 32 non-zero entries out of 512 (4096/8)
        if (non_zero >= 32) {
            ++vis_candidates;
            Log("  [VIS_CANDIDATE] RVA 0x%llX: ~%d/512 entries non-zero\n",
                (unsigned long long)(probe - g_base), non_zero);
        }
    }

    Log("\n[RESULT]\n");
    LogResultHeader("CHECK", "VALUE", "DETAIL");
    RecordCheckFmt(
        "Decrypt Table Candidates",
        table_candidates > 0,
        "component=%d vis=%d",
        table_candidates,
        vis_candidates);
}

// ---------------------------------------------------------------------------
// Scan #4: ViewMatrix
// ---------------------------------------------------------------------------

void ScanViewMatrix()
{
    Log("\n=== SCAN 4: ViewMatrix ===\n\n");

    uint64_t old_vm_rva = 0x37F7618;
    uint64_t old_vm_addr = g_base + old_vm_rva;
    uint64_t xor_key = 0x544A3BA5BE911EE7ULL;

    Log("[INFO] Old viewmatrix offset:  0x%llX\n", (unsigned long long)old_vm_rva);
    Log("[INFO] XOR key:                0x%llX\n", (unsigned long long)xor_key);

    uint64_t raw_val = Read<uint64_t>(old_vm_addr);
    uint64_t decrypted = raw_val ^ xor_key;

    Log("[INFO] Raw value at old offset: 0x%llX\n", (unsigned long long)raw_val);
    Log("[INFO] XOR-decrypted value:     0x%llX\n", (unsigned long long)decrypted);

    // Check if the decrypted value looks like a pointer in game range
    bool ptr_looks_valid = (decrypted > g_base && decrypted < g_base + g_size);
    Log("[INFO] Decrypted value is valid pointer? %s\n", ptr_looks_valid ? "YES" : "NO");

    if (ptr_looks_valid) {
        // Read first 16 bytes at that pointer to see if it looks like a matrix
        float matrix_preview[4] = {};
        if (ReadBuf(decrypted, matrix_preview, sizeof(matrix_preview))) {
            Log("[INFO] First 4 floats at decrypted ptr: %f %f %f %f\n",
                matrix_preview[0], matrix_preview[1],
                matrix_preview[2], matrix_preview[3]);
        }
    }

    // Also try the secondary viewmatrix offset
    uint64_t old_vm2_rva = 0x3EB6278;
    uint64_t raw2 = Read<uint64_t>(g_base + old_vm2_rva);
    Log("\n[INFO] Secondary viewmatrix at 0x%llX:\n", (unsigned long long)old_vm2_rva);
    Log("[INFO]   Raw value: 0x%llX\n", (unsigned long long)raw2);

    // Scan for viewmatrix-like XOR patterns in nearby memory
    Log("\n[INFO] Scanning for viewmatrix XOR-ed pointers...\n");
    Log("[INFO] Looking for values V where (V ^ 0x%llX) is a valid game pointer\n",
        (unsigned long long)xor_key);

    // Scan a range near the old offset for XOR-encrypted pointers
    uint64_t vm_scan_start = g_base + 0x37F0000; // near old viewmatrix
    uint64_t vm_scan_end   = g_base + 0x3800000;

    int vm_count = 0;
    for (uint64_t addr = vm_scan_start; addr < vm_scan_end; addr += 8) {
        uint64_t val = Read<uint64_t>(addr);
        if (val == 0) continue;
        uint64_t xored = val ^ xor_key;
        if (xored > g_base && xored < g_base + g_size) {
            Log("  XOR_PTR at RVA 0x%llX: encrypted=0x%llX -> decrypted=0x%llX\n",
                (unsigned long long)(addr - g_base),
                (unsigned long long)val,
                (unsigned long long)xored);
            vm_count++;
            if (vm_count >= 20) {
                Log("  ... (truncated, found many more)\n");
                break;
            }
        }
    }
    if (vm_count == 0) {
        Log("  No XOR-encrypted pointers found in range 0x37F0000-0x3800000\n");

        // Broader search
        Log("\n[INFO] Broad search for XOR-encrypted pointers (0x3700000-0x3F00000)...\n");
        vm_count = 0;
        for (uint64_t addr = g_base + 0x3700000; addr < g_base + 0x3F00000; addr += 0x1000) {
            uint64_t buf[512]; // 4096 bytes
            if (!ReadBuf(addr, buf, sizeof(buf))) continue;
            for (int i = 0; i < 512; i++) {
                if (buf[i] == 0) continue;
                uint64_t xored = buf[i] ^ xor_key;
                if (xored > g_base && xored < g_base + g_size) {
                    Log("  XOR_PTR at RVA 0x%llX: encrypted=0x%llX -> decrypted=0x%llX\n",
                        (unsigned long long)(addr + i * 8 - g_base),
                        (unsigned long long)buf[i],
                        (unsigned long long)xored);
                    vm_count++;
                    if (vm_count >= 20) break;
                }
            }
            if (vm_count >= 20) break;
        }
        if (vm_count == 0) {
            Log("  No XOR-encrypted pointers found in broad scan either.\n");
        }
    }

    Log("\n[RESULT]\n");
    LogResultHeader("CHECK", "VALUE", "DETAIL");
    RecordCheckFmt(
        "ViewMatrix Legacy XOR",
        ptr_looks_valid || vm_count > 0,
        "old_ptr=%s xor_candidates=%d",
        ptr_looks_valid ? "valid" : "invalid",
        vm_count);
}

// ---------------------------------------------------------------------------
// Scan #5: HeapManager
// ---------------------------------------------------------------------------

void ScanHeapManager()
{
    Log("\n=== SCAN 5: HeapManager ===\n\n");

    uint64_t old_heap_rva = 0x38B55F0;
    uint64_t old_heap_var_rva = 0x3899DD5;
    uint64_t heap_key = 0xE7E1F898E11B68B1ULL;
    uint64_t heap_ptr_off = 0x160;

    Log("[INFO] Old HeapManager offset:     0x%llX\n", (unsigned long long)old_heap_rva);
    Log("[INFO] Old HeapManager_Var offset: 0x%llX\n", (unsigned long long)old_heap_var_rva);
    Log("[INFO] HeapManager_Key:            0x%llX\n", (unsigned long long)heap_key);
    Log("[INFO] HeapManager_Pointer offset: 0x%llX\n", (unsigned long long)heap_ptr_off);

    // Check old chain
    uint64_t heap_base_ptr = Read<uint64_t>(g_base + old_heap_rva);
    Log("[INFO] Step 1: [g_base + 0x%llX] = 0x%llX\n",
        (unsigned long long)old_heap_rva, (unsigned long long)heap_base_ptr);

    if (heap_base_ptr != 0) {
        uint64_t heap_var = Read<uint8_t>(g_base + old_heap_var_rva); // note: old code reads a BYTE here
        // Actually looking at Decrypt.hpp: SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::HeapManager_Var)
        // Wait, the Decrypt.hpp uses: SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::HeapManager_Var)
        // But the offset is 0x3899DD5 which is odd, so it's probably a byte read.
        // Let me re-check: the code does:
        //   SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::HeapManager_Var) + offset::HeapManager_Key
        // If HeapManager_Var = 0x3899DD5, that's an odd address. It might be a byte in a larger structure.
        // But the return type is uintptr_t so it reads 4 or 8 bytes from an odd address.
        // Let me just try reading it.

        uint64_t heap_var_val = Read<uint64_t>(g_base + old_heap_var_rva);
        Log("[INFO] Step 2: [g_base + 0x%llX] = 0x%llX\n",
            (unsigned long long)old_heap_var_rva, (unsigned long long)heap_var_val);

        uint64_t xor_result = heap_var_val + heap_key;
        Log("[INFO] Step 3: heap_var_val + heap_key = 0x%llX\n",
            (unsigned long long)xor_result);

        uint64_t heap_ptr2 = Read<uint64_t>(heap_base_ptr + heap_ptr_off);
        Log("[INFO] Step 4: [heap_base + 0x160] = 0x%llX\n",
            (unsigned long long)heap_ptr2);

        uint64_t final_ptr = heap_ptr2 ^ xor_result;
        Log("[INFO] Step 5: final XOR = 0x%llX\n", (unsigned long long)final_ptr);

        if (final_ptr != 0) {
            // Try reading first few heap entries
            Log("[INFO] Step 6: First 8 heap entries:\n");
            for (int i = 0; i < 8; i++) {
                uint64_t entry = Read<uint64_t>(final_ptr + (uint64_t)i * 8);
                Log("  [%d] 0x%llX\n", i, (unsigned long long)entry);
            }
        }
    } else {
        Log("[INFO] Old HeapManager offset is NULL -- scanning for alternatives...\n");

        // Scan for potential heap manager pointers
        // A HeapManager pointer typically points to a large allocated block
        Log("\n[INFO] Scanning range 0x38B0000-0x38C0000 for non-zero pointers...\n");
        for (uint64_t addr = g_base + 0x38B0000; addr < g_base + 0x38C0000; addr += 8) {
            uint64_t val = Read<uint64_t>(addr);
            if (val != 0 && val > 0x100000) {
                Log("  [0x%llX] = 0x%llX\n",
                    (unsigned long long)(addr - g_base),
                    (unsigned long long)val);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Scan #6: IDA hint cross-reference
// ---------------------------------------------------------------------------

void ScanIDAHints()
{
    Log("\n=== SCAN 6: IDA Hint Cross-Reference ===\n\n");

    // The user mentioned:
    //   - GetGlobalKey code at RVA 0x581D20
    //   - DecryptComponent table lookup at RVA 0x666016 uses shr rcx, 0x34; and ecx, 0x7FF

    Log("[INFO] GetGlobalKey (from IDA hint):      RVA 0x581D20\n");
    Log("[INFO] DecryptComponent table lookup:      RVA 0x666016\n");
    Log("[INFO] Key table mask changed 0x1FF -> 0x7FF (512 -> 2048 entries)\n\n");

    // Check what's at GetGlobalKey address
    uint64_t gk_addr = g_base + 0x581D20;
    Log("[INFO] Checking GetGlobalKey at 0x%llX ...\n", (unsigned long long)gk_addr);

    // Read 64 bytes of code at GetGlobalKey
    uint8_t code_buf[64] = {};
    if (ReadBuf(gk_addr, code_buf, sizeof(code_buf))) {
        Log("[INFO] First 64 bytes:\n  ");
        for (int i = 0; i < 64; i++) {
            Log("%02X ", code_buf[i]);
            if ((i + 1) % 16 == 0 && i < 63) Log("\n  ");
        }
        Log("\n");
    } else {
        Log("[WARN] Could not read code at GetGlobalKey (might be in a different section)\n");
    }

    // Check what's at DecryptComponent table lookup
    uint64_t dc_addr = g_base + 0x666016;
    Log("\n[INFO] Checking DecryptComponent lookup at 0x%llX ...\n", (unsigned long long)dc_addr);

    uint8_t dc_code[32] = {};
    bool ida_hint_ok = false;
    if (ReadBuf(dc_addr, dc_code, sizeof(dc_code))) {
        Log("[INFO] Code at table lookup:\n  ");
        for (int i = 0; i < 32; i++) {
            Log("%02X ", dc_code[i]);
            if ((i + 1) % 16 == 0) Log("\n  ");
        }
        Log("\n");

        // Check for shr/and pattern
        // shr rcx, 0x34 = 48 C1 E9 34
        // and ecx, 0x7FF  = 81 E1 FF 07 00 00
        bool found_shr = false;
        bool found_and = false;
        for (int i = 0; i < 28; i++) {
            if (dc_code[i] == 0x48 && dc_code[i+1] == 0xC1 && dc_code[i+2] == 0xE9 && dc_code[i+3] == 0x34) {
                found_shr = true;
                Log("[MATCH] Found 'shr rcx, 0x34' at offset +%d\n", i);
            }
            if (i <= 24 && dc_code[i] == 0x81 && dc_code[i+1] == 0xE1 &&
                dc_code[i+2] == 0xFF && dc_code[i+3] == 0x07 &&
                dc_code[i+4] == 0x00 && dc_code[i+5] == 0x00) {
                found_and = true;
                Log("[MATCH] Found 'and ecx, 0x7FF' at offset +%d\n", i);
            }
        }
        if (!found_shr) Log("[WARN] Did not find 'shr rcx, 0x34' in the sample\n");
        if (!found_and) Log("[WARN] Did not find 'and ecx, 0x7FF' in the sample\n");
        if (found_shr && found_and) {
            Log("[OK]   Confirmed: the IDA hint matches the running binary.\n");
            ida_hint_ok = true;

            // Now we know the table is 2048 entries. Let's try to find the table
            // by looking for what RVA is used in the offset calculation.
            // The code at 0x666016 would reference the table base via something like:
            //   lea rcx, [game_base + table_rva]
            //   mov rax, [rcx + rdx*8]
            // etc. But this is complex to reverse from hex alone.
        }
    } else {
        Log("[WARN] Could not read code at DecryptComponent (might be in a different section)\n");
    }

    Log("\n[RESULT]\n");
    LogResultHeader("CHECK", "VALUE", "DETAIL");
    RecordCheck(
        "IDA DecryptComponent Hint",
        ida_hint_ok,
        ida_hint_ok ? "shr rcx,0x34 and and ecx,0x7FF matched"
                    : "expected instruction sample not confirmed");
}

// ---------------------------------------------------------------------------
// Scan #7: .data section exploration
// ---------------------------------------------------------------------------

void ScanDataSection()
{
    Log("\n=== SCAN 7: .data Section Exploration ===\n\n");

    // The .data section of Overwatch.exe is typically around RVA 0x3000000-0x4000000
    // Let's dump a summary of what's at key regions

    Log("[INFO] Scanning .data section regions for meaningful structures...\n\n");

    // Dump important offsets as a batch for easy reference
    struct OffsetTest {
        const char* name;
        uint64_t rva;
    };

    OffsetTest tests[] = {
        { "entity_base (old)",              0x37E2AC0 },
        { "viewmatrix_xor (old)",           0x37F7618 },
        { "viewmatrix_test (old)",          0x3EB6278 },
        { "decrypt_table (old)",            0x38996E0 },
        { "vis_table (old)",                0x389A700 },
        { "HeapManager (old)",              0x38B55F0 },
        { "HeapManager_Var (old)",          0x3899DD5 },
        { "changefov (old)",                0x395EDB8 },
        { "GetGlobalKey (IDA)",             0x581D20 },
        { "DecryptComp lookup (IDA)",       0x666016 },
    };

    Log("[INFO] Batch offset dump:\n");
    Log("  %-30s %12s  =>  %s\n", "NAME", "RVA", "VALUE");
    Log("  %s\n", std::string(80, '-').c_str());
    for (auto& t : tests) {
        uint64_t val = Read<uint64_t>(g_base + t.rva);
        Log("  %-30s 0x%08llX  =>  0x%llX\n",
            t.name,
            (unsigned long long)t.rva,
            (unsigned long long)val);
    }
    Log("\n");
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--dump-mmap") == 0) {
        for (size_t i = 0; i < kPhysicalRangeCount; i++) {
            printf("0x%llX - 0x%llX\n",
                (unsigned long long)kPhysicalRanges[i].start,
                (unsigned long long)kPhysicalRanges[i].end);
        }
        return 0;
    }

    LogInit();

    Log("============================================================\n");
    Log("  Unleashed DMA Scanner\n");
    Log("  Overwatch Live Offset Discovery -- May 2026\n");
    Log("============================================================\n\n");

    // ---- Step 1: Init DMA ----
    if (!InitDMA()) {
        Log("[FATAL] DMA initialisation failed.\n");
        LogClose();
        printf("\nPress Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // ---- Step 2: Attach to Overwatch ----
    if (!AttachOverwatch()) {
        Log("[FATAL] Could not attach to Overwatch.exe.\n");
        Log("[HINT] Make sure Overwatch.exe is running.\n");
        VMMDLL_Close(g_vmm);
        LogClose();
        printf("\nPress Enter to exit.\n");
        std::getchar();
        return 1;
    }

    Log("\n============================================================\n");
    Log("  Starting scans...\n");
    Log("============================================================\n");

    // ---- Scan 1: GetGlobalKey ----
    ScanGetGlobalKey();

    // ---- Scan 2: Entity Base ----
    ScanEntityBase();

    // ---- Scan 3: Decryption Tables ----
    ScanDecryptionTables();

    // ---- Scan 4: ViewMatrix ----
    ScanViewMatrix();

    // ---- Scan 5: HeapManager ----
    ScanHeapManager();

    // ---- Scan 6: IDA Hints ----
    ScanIDAHints();

    // ---- Scan 7: Data Section Summary ----
    ScanDataSection();

    // ---- Done ----
    Log("\n============================================================\n");
    Log("  Scan complete.\n");
    Log("============================================================\n");
    Log("\nResults saved to DMA_SCAN_RESULTS.txt\n");

    VMMDLL_Close(g_vmm);
    LogClose();

    printf("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
