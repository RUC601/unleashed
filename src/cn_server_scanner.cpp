// =============================================================================
// cn_server_scanner.cpp  --  Chinese server (GuoFu) offset diagnostic tool
//
// Standalone executable.  Initialises the FPGA DMA card, attaches to a running
// Overwatch.exe process (any region), then scours the PE image for the correct
// RVA offsets specific to the Chinese-server (国服) binary.
//
// Key differences observed:
//   - International: ImageSize=0x4725000, base varies
//   - Chinese (国服): ImageSize=0x50BB000, base=0x7FF694460000 (observed)
//
// Scans:
//   1. PE headers (section layout)
//   2. Entity list pointer  (Address_entity_base)
//   3. Component key source (ComponentXorQword_RVA)
//   4. ViewMatrix base      (Address_viewmatrix_base)
//   5. Game admin root      (Address_game_admin_root)
//   6. DecryptComponent code dump at known international RVAs
//
// Output: stdout with PASS/FAIL markers and a summary table at the end.
// =============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <array>
#include <utility>

#include "leechcore.h"
#include "vmmdll.h"
#include "Game/Offsets.hpp"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// International known offsets for cross-reference.
static constexpr uint64_t kIntl_DecryptComponent_RVA = 0x5632B0;
static constexpr uint64_t kIntl_GetGlobalKey_RVA      = 0x581D20;
static constexpr uint64_t kIntl_DecryptVis_RVA        = 0x58C880;

// Scan range for .data equivalent section (wider for 国服 0x50BB000 image).
static constexpr uint64_t kDataScanStart = 0x2500000;  // start RVA for data scans
static constexpr uint64_t kDataScanEnd   = 0x4C00000;  // end RVA   (near ImageSize)

// Stride for entity list (observed on international: {uint64_t entity, uint64_t pad}).
static constexpr uint64_t kEntityStride = 0x10;

// Known offsets for key dereference.
static constexpr uint64_t kKeyOffsetKnown = 0x1D4;  // current intl offset
static constexpr uint64_t kKeyOffsetOld   = 0x10C;  // previous intl offset

// ViewMatrix chain offsets (international).
static constexpr uint64_t kVM_Key1 = 0x37316FB2858F0E4Aull;
static constexpr uint64_t kVM_Key2 = 0xB6326CCBCA7E34F4ull;
static constexpr uint64_t kVM_P1   = 0x20;
static constexpr uint64_t kVM_P2   = 0x48;

// GameAdmin chain offsets (international).
static constexpr uint64_t kGA_RootPtr = 0x30;
static constexpr uint64_t kGA_Add1    = 0x3A7D48F98701DF53ull;
static constexpr uint64_t kGA_Xor1    = 0xA0CC9EB06D3118CDull;
static constexpr uint64_t kGA_Ror1    = 17;
static constexpr uint64_t kGA_Add2    = 0x2AF9257775C5D0FFull;
static constexpr uint64_t kGA_Ror2    = 34;

static constexpr ULONG64 kReadFlags =
    VMMDLL_FLAG_NOCACHE |
    VMMDLL_FLAG_NOPAGING |
    VMMDLL_FLAG_ZEROPAD_ON_FAIL;

static constexpr ULONG64 kReadFlagsNoZero =
    VMMDLL_FLAG_NOCACHE |
    VMMDLL_FLAG_NOPAGING |
    VMMDLL_FLAG_NOPAGING_IO;

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static VMM_HANDLE g_vmm   = nullptr;
static DWORD      g_pid   = 0;
static uint64_t   g_base  = 0;   // base address of Overwatch.exe
static uint64_t   g_size  = 0;   // image size
static uint64_t   g_end   = 0;   // base + size

// PE header cache
static IMAGE_DOS_HEADER          g_dos{};
static IMAGE_NT_HEADERS64        g_nt{};
static std::vector<IMAGE_SECTION_HEADER> g_sections;
static bool                      g_pe_valid = false;

// ---------------------------------------------------------------------------
// Tagged result with confidence
// ---------------------------------------------------------------------------
struct FoundOffset {
    std::string name;
    uint64_t    rva            = 0;
    bool        found          = false;
    double      confidence     = 0.0;  // 0.0 - 1.0
    std::string evidence;
};

static std::vector<FoundOffset> g_found;

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
static void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

static std::string FormatString(const char* fmt, ...)
{
    char buffer[2048];
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
    Log("\n");
    Log("================================================================\n");
    Log("  %s\n", title);
    Log("================================================================\n\n");
}

static void LogTableHeader(const char* col1, const char* col2, const char* col3, const char* col4)
{
    Log("  %-30s %-18s %-22s %s\n", col1, col2, col3, col4);
    Log("  %-30s %-18s %-22s %s\n",
        "------------------------------",
        "------------------",
        "----------------------",
        "------------------------------");
}

static void LogTableRow(const char* name, bool passed, const char* value, const char* detail)
{
    Log("  %-30s %-10s %-20s %s\n",
        name,
        PassFail(passed),
        value ? value : "",
        detail ? detail : "");
}

// ---------------------------------------------------------------------------
// DMA helpers
// ---------------------------------------------------------------------------
static bool InitDMA()
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

static bool AttachOverwatch()
{
    Log("[DMA] Locating Overwatch.exe...\n");
    if (!VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid) || !g_pid) {
        Log("[FAIL] Overwatch.exe PID not found.\n");
        return false;
    }
    Log("[OK]   PID = %u\n", g_pid);

    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
    if (!g_base) {
        Log("[FAIL] Could not resolve Overwatch.exe base address.\n");
        return false;
    }
    Log("[OK]   Base = 0x%llX\n", (unsigned long long)g_base);

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
        Log("[WARN] Could not determine image size from module map; trying NT headers...\n");
        // Try to read NT headers and get SizeOfImage.
        IMAGE_DOS_HEADER dos{};
        DWORD read = 0;
        if (VMMDLL_MemReadEx(g_vmm, g_pid, g_base, (PBYTE)&dos, sizeof(dos), &read, kReadFlagsNoZero) &&
            dos.e_magic == IMAGE_DOS_SIGNATURE) {
            IMAGE_NT_HEADERS64 nt{};
            read = 0;
            if (VMMDLL_MemReadEx(g_vmm, g_pid, g_base + dos.e_lfanew, (PBYTE)&nt, sizeof(nt), &read, kReadFlagsNoZero) &&
                nt.Signature == IMAGE_NT_SIGNATURE) {
                g_size = nt.OptionalHeader.SizeOfImage;
                Log("[OK]   Image size from NT headers = 0x%llX (%llu MB)\n",
                    (unsigned long long)g_size,
                    (unsigned long long)(g_size / 1048576));
            }
        }
    }
    if (!g_size) {
        g_size = 0x5100000; // Fallback: just under 国服 size
        Log("[WARN] Could not determine image size; using 0x%llX\n",
            (unsigned long long)g_size);
    }
    g_end = g_base + g_size;
    Log("[OK]   Image range: 0x%llX - 0x%llX\n",
        (unsigned long long)g_base, (unsigned long long)g_end);
    return true;
}

// ---------------------------------------------------------------------------
// Typed read helpers
// ---------------------------------------------------------------------------
template <typename T>
static T Read(uint64_t addr)
{
    T buf{};
    DWORD read = 0;
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, (DWORD)sizeof(T), &read,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

template <typename T>
static bool ReadExact(uint64_t addr, T& value)
{
    value = {};
    DWORD read = 0;
    BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, addr,
        reinterpret_cast<PBYTE>(&value),
        static_cast<DWORD>(sizeof(T)),
        &read,
        kReadFlagsNoZero);
    return ok && read == sizeof(T);
}

static bool ReadBuf(uint64_t addr, void* buf, size_t size)
{
    if (!buf || size == 0 || size > 0x200000) return false;
    DWORD read = 0;
    return VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)buf, (DWORD)size, &read,
               VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO)
           != 0 && read == size;
}

// Read code bytes using individual qword reads (reliable for .text sections).
static void ReadCodeBytes(uint64_t addr, uint8_t* buf, size_t size)
{
    for (size_t off = 0; off < size; off += 8) {
        size_t chunk = (size - off >= 8) ? 8 : (size - off);
        uint64_t val = Read<uint64_t>(addr + off);
        memcpy(buf + off, &val, chunk);
    }
}

// ---------------------------------------------------------------------------
// Pointer validation
// ---------------------------------------------------------------------------
static bool IsCanonicalUserPointer(uint64_t value)
{
    return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull;
}

static bool LooksLikeImagePointer(uint64_t value)
{
    return g_base != 0 && g_size != 0 &&
           value >= g_base && value < g_base + g_size;
}

static bool CanReadSize(uint64_t addr, size_t size)
{
    if (!IsCanonicalUserPointer(addr) || size == 0 || size > 0x4000) return false;
    uint8_t buf[64] = {};
    DWORD read = 0;
    size_t check = (size < 64) ? size : 64;
    BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, addr, buf, (DWORD)check, &read,
                VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING | VMMDLL_FLAG_NOPAGING_IO);
    return ok && read == check;
}

// =========================================================================
// SCAN 1: PE Headers and Section Layout
// =========================================================================
static void ScanPEHeaders()
{
    LogSection("SCAN 1: PE Headers & Section Layout");

    // Read DOS header
    if (!ReadExact(g_base, g_dos) || g_dos.e_magic != IMAGE_DOS_SIGNATURE) {
        Log("[FAIL] DOS header not found at base 0x%llX.\n", (unsigned long long)g_base);
        return;
    }
    Log("[OK]   DOS header: e_magic=0x%04X e_lfanew=%ld\n",
        g_dos.e_magic, g_dos.e_lfanew);

    // Read NT headers
    if (!ReadExact(g_base + g_dos.e_lfanew, g_nt) ||
        g_nt.Signature != IMAGE_NT_SIGNATURE ||
        g_nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        Log("[FAIL] NT headers not valid.\n");
        return;
    }

    Log("[OK]   NT signature: 0x%08lX\n", g_nt.Signature);
    Log("[OK]   Machine: 0x%04X  (0x8664 = AMD64)\n", g_nt.FileHeader.Machine);
    Log("[OK]   NumberOfSections: %u\n", g_nt.FileHeader.NumberOfSections);
    Log("[OK]   SizeOfImage: 0x%llX (%llu MB)\n",
        (unsigned long long)g_nt.OptionalHeader.SizeOfImage,
        (unsigned long long)(g_nt.OptionalHeader.SizeOfImage / 1048576));
    Log("[OK]   SizeOfCode: 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.SizeOfCode);
    Log("[OK]   AddressOfEntryPoint: 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.AddressOfEntryPoint);
    Log("[OK]   ImageBase (preferred): 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.ImageBase);
    Log("[OK]   SizeOfHeaders: 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.SizeOfHeaders);
    Log("[OK]   SectionAlignment: 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.SectionAlignment);
    Log("[OK]   FileAlignment: 0x%llX\n",
        (unsigned long long)g_nt.OptionalHeader.FileAlignment);

    // Read section headers
    const uint64_t sectionOffset = g_base + g_dos.e_lfanew +
        FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) +
        g_nt.FileHeader.SizeOfOptionalHeader;
    const DWORD numSections = g_nt.FileHeader.NumberOfSections;
    g_sections.resize(numSections);

    if (!ReadBuf(sectionOffset, g_sections.data(), numSections * sizeof(IMAGE_SECTION_HEADER))) {
        Log("[FAIL] Could not read section headers at offset 0x%llX.\n",
            (unsigned long long)sectionOffset);
        return;
    }

    g_pe_valid = true;

    Log("\n  Section Table:\n");
    Log("  %-10s %12s %12s %12s %8s %s\n",
        "Name", "VirtAddr", "VirtSize", "RawSize", "Chars", "Flags");
    Log("  %s\n", std::string(78, '-').c_str());

    uint64_t data_start_rva = 0;
    uint64_t data_end_rva   = 0;
    uint64_t text_start_rva = 0;
    uint64_t text_end_rva   = 0;
    uint64_t rdata_start_rva = 0;
    uint64_t rdata_end_rva   = 0;

    for (DWORD i = 0; i < numSections; i++) {
        const auto& s = g_sections[i];
        char name[9] = {};
        memcpy(name, s.Name, 8);

        Log("  %-10s 0x%08llX 0x%08llX 0x%08llX 0x%04X ",
            name,
            (unsigned long long)s.VirtualAddress,
            (unsigned long long)s.Misc.VirtualSize,
            (unsigned long long)s.SizeOfRawData,
            s.Characteristics);

        // Decode characteristics
        std::string flags;
        if (s.Characteristics & IMAGE_SCN_CNT_CODE)   flags += "CODE ";
        if (s.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA) flags += "DATA ";
        if (s.Characteristics & IMAGE_SCN_CNT_UNINITIALIZED_DATA) flags += "UNINIT ";
        if (s.Characteristics & IMAGE_SCN_MEM_EXECUTE) flags += "EXEC ";
        if (s.Characteristics & IMAGE_SCN_MEM_READ)    flags += "READ ";
        if (s.Characteristics & IMAGE_SCN_MEM_WRITE)   flags += "WRITE ";
        Log("%s\n", flags.c_str());

        // Track data/text/rdata ranges
        std::string sn(name);
        if (sn == ".text" || (s.Characteristics & IMAGE_SCN_CNT_CODE)) {
            text_start_rva = s.VirtualAddress;
            text_end_rva   = s.VirtualAddress + s.Misc.VirtualSize;
        }
        if (sn == ".data" || (sn == ".data")) {
            data_start_rva = s.VirtualAddress;
            data_end_rva   = s.VirtualAddress + s.Misc.VirtualSize;
        }
        if (sn == ".rdata" || sn == ".idata" || sn == ".didat") {
            if (rdata_start_rva == 0) rdata_start_rva = s.VirtualAddress;
            rdata_end_rva = s.VirtualAddress + s.Misc.VirtualSize;
        }
        // Catch sections that have INITIALIZED_DATA + WRITE but not CODE (the real .data equivalent)
        if ((s.Characteristics & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_WRITE)) ==
                                 (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_WRITE) &&
            !(s.Characteristics & IMAGE_SCN_CNT_CODE)) {
            if (data_start_rva == 0 || s.VirtualAddress < data_start_rva)
                data_start_rva = s.VirtualAddress;
            if (s.VirtualAddress + s.Misc.VirtualSize > data_end_rva)
                data_end_rva = s.VirtualAddress + s.Misc.VirtualSize;
        }
    }

    Log("\n[INFO] Derived section ranges:\n");
    Log("  .text / CODE:  RVA 0x%llX - 0x%llX (size 0x%llX)\n",
        (unsigned long long)text_start_rva, (unsigned long long)text_end_rva,
        (unsigned long long)(text_end_rva - text_start_rva));
    Log("  .data / RW:    RVA 0x%llX - 0x%llX (size 0x%llX)\n",
        (unsigned long long)data_start_rva, (unsigned long long)data_end_rva,
        (unsigned long long)(data_end_rva - data_start_rva));
    Log("  .rdata / RO:   RVA 0x%llX - 0x%llX (size 0x%llX)\n",
        (unsigned long long)rdata_start_rva, (unsigned long long)rdata_end_rva,
        (unsigned long long)(rdata_end_rva - rdata_start_rva));

    // Store the data range for later scans
    if (data_start_rva == 0) {
        // Fallback: use hardcoded range
        Log("[WARN] No explicit .data section found; using hardcoded RVA range 0x3000000-0x4800000.\n");
    }
}

// =========================================================================
// SCAN 2: Entity List Pointer
// =========================================================================
static void ScanEntityBase()
{
    LogSection("SCAN 2: Entity List Pointer (Address_entity_base)");

    Log("[INFO] The entity base is a pointer stored in .data that points to an array\n");
    Log("[INFO] of {uint64_t entity, uint64_t pad} pairs with stride 0x%llX.\n",
        (unsigned long long)kEntityStride);
    Log("[INFO] Scanning all qwords in RVA range [0x3000000, 0x4A00000)\n");
    Log("[INFO] and scoring each candidate value that looks like a valid pointer.\n\n");

    struct EntityPair {
        uint64_t entity;
        uint64_t pad;
    };
    struct EntityCandidate {
        uint64_t rva;
        uint64_t ptr_value;
        int      valid_entities;     // count of non-zero entity fields
        int      valid_pointers;     // count of canonical pointers
        uint64_t sample_entities[4]; // first few for display
    };

    // Scan parameters — use bulk chunked reads for speed
    const uint64_t scan_start = g_base + 0x3000000;
    const uint64_t scan_end   = g_base + 0x4A00000;
    const uint64_t scan_size  = scan_end - scan_start;

    static constexpr size_t kChunkSize = 0x100000; // 1 MB per DMA read
    Log("[SCAN] Scanning 0x%llX (%llu MB) in %llu chunk(s)...\n",
        (unsigned long long)scan_size,
        (unsigned long long)(scan_size / 1048576),
        (unsigned long long)((scan_size + kChunkSize - 1) / kChunkSize));

    std::vector<EntityCandidate> candidates;
    std::vector<uint8_t> chunk(kChunkSize);

    for (uint64_t chunk_start = scan_start; chunk_start < scan_end; chunk_start += kChunkSize) {
        size_t this_chunk = (std::min)(kChunkSize, (size_t)(scan_end - chunk_start));
        int chunk_pct = (int)((chunk_start - scan_start) * 100 / scan_size);
        Log("[SCAN] Reading chunk at RVA 0x%llX (%zu KB) ... %d%%\n",
            (unsigned long long)(chunk_start - g_base), this_chunk / 1024, chunk_pct);

        if (!ReadBuf(chunk_start, chunk.data(), this_chunk)) {
            Log("[WARN] Failed to read chunk at 0x%llX, skipping.\n",
                (unsigned long long)chunk_start);
            continue;
        }

        // Process qwords in this chunk (in memory, fast)
        size_t num_qwords = this_chunk / 8;
        const uint64_t* qwords = reinterpret_cast<const uint64_t*>(chunk.data());

        for (size_t qi = 0; qi < num_qwords; qi++) {
            uint64_t val = qwords[qi];
            if (!val) continue;
            if (!IsCanonicalUserPointer(val)) continue;

            // Verification read (still needs DMA, but only for candidates)
            EntityPair pairs[32];
            if (!ReadBuf(val, pairs, sizeof(pairs))) continue;

            int non_zero = 0;
            int ptr_count = 0;
            for (int i = 0; i < 32 && non_zero < 32; i++) {
                if (pairs[i].entity != 0) {
                    non_zero++;
                    if (IsCanonicalUserPointer(pairs[i].entity)) ptr_count++;
                }
            }

            if (non_zero >= 3) {
                EntityCandidate c;
                c.rva = chunk_start - g_base + qi * 8;
                c.ptr_value = val;
                c.valid_entities = non_zero;
                c.valid_pointers = ptr_count;
                for (int i = 0; i < 4 && i < 32; i++)
                    c.sample_entities[i] = pairs[i].entity;
                candidates.push_back(c);
            }
        }

        if (candidates.size() > 20000) {
            Log("[WARN] Too many candidates (%zu). Stopping early.\n", candidates.size());
            break;
        }
    }

    Log("\n[RESULT] Found %zu entity-list candidates.\n", candidates.size());

    // Sort by score (valid_entities desc, then valid_pointers desc)
    std::sort(candidates.begin(), candidates.end(),
        [](const EntityCandidate& a, const EntityCandidate& b) {
            if (a.valid_entities != b.valid_entities)
                return a.valid_entities > b.valid_entities;
            return a.valid_pointers > b.valid_pointers;
        });

    // Print top candidates
    size_t display = (candidates.size() < 40) ? candidates.size() : 40;
    Log("\n  Top %zu entity-list candidates:\n", display);
    Log("  %-6s %-14s %-18s %-8s %-8s  %s\n",
        "Rank", "RVA", "Pointer", "Entities", "Ptrs", "Sample Entities");
    Log("  %s\n", std::string(95, '-').c_str());

    for (size_t i = 0; i < display; i++) {
        const auto& c = candidates[i];
        Log("  %-6zu 0x%010llX 0x%016llX %-8d %-8d  0x%llX 0x%llX 0x%llX 0x%llX\n",
            i + 1,
            (unsigned long long)c.rva,
            (unsigned long long)c.ptr_value,
            c.valid_entities,
            c.valid_pointers,
            (unsigned long long)c.sample_entities[0],
            (unsigned long long)c.sample_entities[1],
            (unsigned long long)c.sample_entities[2],
            (unsigned long long)c.sample_entities[3]);
    }

    // Determine the best candidate
    FoundOffset fo;
    fo.name = "Address_entity_base";
    if (!candidates.empty()) {
        const auto& best = candidates[0];
        fo.rva = best.rva;
        fo.found = true;
        fo.confidence = std::min(1.0, (double)best.valid_entities / 20.0);
        fo.evidence = FormatString(
            "RVA 0x%llX -> 0x%llX, entities=%d, ptrs=%d, samples=[0x%llX, 0x%llX, 0x%llX]",
            (unsigned long long)best.rva,
            (unsigned long long)best.ptr_value,
            best.valid_entities,
            best.valid_pointers,
            (unsigned long long)best.sample_entities[0],
            (unsigned long long)best.sample_entities[1],
            (unsigned long long)best.sample_entities[2]);
    }
    g_found.push_back(fo);
}

// =========================================================================
// SCAN 3: Component Key Source Pointer (ComponentXorQword_RVA)
// =========================================================================
static void ScanKeySource()
{
    LogSection("SCAN 3: Component Key Source Pointer (ComponentXorQword_RVA)");

    Log("[INFO] The key source is a pointer stored in .data;\n");
    Log("[INFO] dereferencing at offset 0x1D4 (or 0x10C) gives a qword used in\n");
    Log("[INFO] DecryptComponent transforms.\n\n");

    // Scan wide range for potential key slots.
    // A key source slot:
    //   - Contains a valid canonical pointer
    //   - [ptr + offset] yields a non-zero qword (the key material)
    //   - [ptr + offset] should NOT look like code bytes

    struct KeyCandidate {
        uint64_t rva;
        uint64_t ptr_value;
        uint64_t key_at_0x1D4;
        uint64_t key_at_0x10C;
        int      valid_offsets; // number of offsets in [0, 0x200] that yield non-zero
        uint64_t best_offset;
        uint64_t best_key;
    };

    const uint64_t scan_start = g_base + 0x3000000;
    const uint64_t scan_end   = g_base + 0x4A00000;
    const uint64_t scan_size  = scan_end - scan_start;

    static constexpr size_t kChunkSize = 0x100000; // 1 MB
    Log("[SCAN] Scanning 0x%llX (%llu MB) in chunks for key source pointers...\n",
        (unsigned long long)scan_size,
        (unsigned long long)(scan_size / 1048576));

    auto looks_like_code = [](uint64_t v) -> bool {
        uint8_t lo = (uint8_t)(v >> 0);
        if (lo >= 0x48 && lo <= 0x4F) return true;
        if (lo == 0x50 || lo == 0x53 || lo == 0x55 || lo == 0x56 || lo == 0x57) return true;
        if (lo == 0xC3 || lo == 0xCC || lo == 0xE8 || lo == 0xE9 || lo == 0xEB) return true;
        if (lo == 0xFF || lo == 0x90) return true;
        if (lo <= 0x0F) return true;
        if ((v >> 48) == 0) return true;
        return false;
    };

    std::vector<KeyCandidate> candidates;
    std::vector<uint8_t> chunk(kChunkSize);

    for (uint64_t chunk_start = scan_start; chunk_start < scan_end; chunk_start += kChunkSize) {
        size_t this_chunk = (std::min)(kChunkSize, (size_t)(scan_end - chunk_start));
        int chunk_pct = (int)((chunk_start - scan_start) * 100 / scan_size);
        Log("[SCAN] Key chunk at RVA 0x%llX ... %d%%\n",
            (unsigned long long)(chunk_start - g_base), chunk_pct);

        if (!ReadBuf(chunk_start, chunk.data(), this_chunk)) continue;

        size_t num_qwords = this_chunk / 8;
        const uint64_t* qwords = reinterpret_cast<const uint64_t*>(chunk.data());

        for (size_t qi = 0; qi < num_qwords; qi++) {
            uint64_t val = qwords[qi];
            if (!val) continue;
            if (!IsCanonicalUserPointer(val)) continue;
            if (!CanReadSize(val, 0x200)) continue;

            uint64_t k1 = Read<uint64_t>(val + 0x1D4);
            uint64_t k2 = Read<uint64_t>(val + 0x10C);
            if (k1 == 0 && k2 == 0) continue;
            if (looks_like_code(k1) && looks_like_code(k2)) continue;

            // Discover best offset: scan 0..0x200 step 8
            int valid_count = 0;
            uint64_t best_off = 0;
            uint64_t best_key_val = 0;
            for (uint64_t off = 0; off <= 0x200; off += 8) {
                uint64_t kv = Read<uint64_t>(val + off);
                if (kv != 0 && (kv >> 32) != 0) {
                    valid_count++;
                    if (best_key_val == 0 || kv > best_key_val) {
                        best_key_val = kv;
                        best_off = off;
                    }
                }
            }

            KeyCandidate c;
            c.rva = chunk_start - g_base + qi * 8;
            c.ptr_value = val;
            c.key_at_0x1D4 = k1;
            c.key_at_0x10C = k2;
            c.valid_offsets = valid_count;
            c.best_offset = best_off;
            c.best_key = best_key_val;
            candidates.push_back(c);

            if (candidates.size() >= 200) break;
        }
        if (candidates.size() >= 200) break;
    }

    Log("\n[RESULT] Found %zu key-source candidates.\n", candidates.size());

    // Sort by valid offsets desc
    std::sort(candidates.begin(), candidates.end(),
        [](const KeyCandidate& a, const KeyCandidate& b) {
            return a.valid_offsets > b.valid_offsets;
        });

    size_t display = (candidates.size() < 30) ? candidates.size() : 30;
    Log("\n  Top %zu key-source candidates:\n", display);
    Log("  %-5s %-12s %-18s %-16s %-16s %-4s %-6s %s\n",
        "Rank", "RVA", "Pointer", "Key@0x1D4", "Key@0x10C", "Cnt", "BestOff", "BestKey");
    Log("  %s\n", std::string(105, '-').c_str());

    for (size_t i = 0; i < display; i++) {
        const auto& c = candidates[i];
        Log("  %-5zu 0x%08llX 0x%016llX 0x%014llX 0x%014llX %-4d 0x%03llX 0x%014llX\n",
            i + 1,
            (unsigned long long)c.rva,
            (unsigned long long)c.ptr_value,
            (unsigned long long)c.key_at_0x1D4,
            (unsigned long long)c.key_at_0x10C,
            c.valid_offsets,
            (unsigned long long)c.best_offset,
            (unsigned long long)c.best_key);
    }

    // Also check the international slot directly
    Log("\n[CHECK] International ComponentXorQword_RVA = 0x%llX:\n",
        (unsigned long long)OW::offset::ComponentXorQword_RVA);
    uint64_t intl_slot = Read<uint64_t>(g_base + OW::offset::ComponentXorQword_RVA);
    Log("  [g_base + 0x%llX] = 0x%llX\n",
        (unsigned long long)OW::offset::ComponentXorQword_RVA,
        (unsigned long long)intl_slot);
    if (intl_slot) {
        uint64_t k_at_1d4 = Read<uint64_t>(intl_slot + 0x1D4);
        uint64_t k_at_10c = Read<uint64_t>(intl_slot + 0x10C);
        Log("  [ptr + 0x1D4] = 0x%llX\n", (unsigned long long)k_at_1d4);
        Log("  [ptr + 0x10C] = 0x%llX\n", (unsigned long long)k_at_10c);
        bool looks_good = k_at_1d4 != 0 && (k_at_10c == 0 || k_at_10c != 0);
        Log("  Status: %s\n", PassFail(looks_good));
    } else {
        Log("  Status: %s (null pointer)\n", PassFail(false));
    }

    // Determine the best candidate
    FoundOffset fo;
    fo.name = "ComponentXorQword_RVA";
    if (!candidates.empty()) {
        const auto& best = candidates[0];
        fo.rva = best.rva;
        fo.found = true;
        fo.confidence = std::min(1.0, (double)best.valid_offsets / 10.0);
        fo.evidence = FormatString(
            "RVA 0x%llX -> 0x%llX, key@0x1D4=0x%llX, key@0x10C=0x%llX, bestOff=0x%llX, bestKey=0x%llX",
            (unsigned long long)best.rva,
            (unsigned long long)best.ptr_value,
            (unsigned long long)best.key_at_0x1D4,
            (unsigned long long)best.key_at_0x10C,
            (unsigned long long)best.best_offset,
            (unsigned long long)best.best_key);
    }
    g_found.push_back(fo);
}

// =========================================================================
// SCAN 4: ComponentXorByte_RVA
// =========================================================================
static void ScanKeyByte()
{
    LogSection("SCAN 4: Component Key Byte (ComponentXorByte_RVA)");

    Log("[INFO] The key byte is stored somewhere in .data, typically near\n");
    Log("[INFO] the key source pointer area. Looking for single bytes that\n");
    Log("[INFO] could serve as the component XOR byte.\n\n");

    // Check international slot
    Log("[CHECK] International ComponentXorByte_RVA = 0x%llX:\n",
        (unsigned long long)OW::offset::ComponentXorByte_RVA);
    uint8_t intl_byte = Read<uint8_t>(g_base + OW::offset::ComponentXorByte_RVA);
    Log("  [g_base + 0x%llX] = 0x%02X (%u)\n",
        (unsigned long long)OW::offset::ComponentXorByte_RVA,
        (unsigned)intl_byte, (unsigned)intl_byte);

    // Also check visibility magic byte slot
    Log("[CHECK] International VisibilityMagicByte_RVA = 0x%llX:\n",
        (unsigned long long)OW::offset::VisibilityMagicByte_RVA);
    uint8_t vis_byte = Read<uint8_t>(g_base + OW::offset::VisibilityMagicByte_RVA);
    Log("  [g_base + 0x%llX] = 0x%02X (%u)\n",
        (unsigned long long)OW::offset::VisibilityMagicByte_RVA,
        (unsigned)vis_byte, (unsigned)vis_byte);

    // Scan for byte candidates near the key source area
    Log("\n[SCAN] Scanning RVA range around ComponentXorQword slot for byte candidates...\n");
    if (!g_found.empty() && g_found[1].name == "ComponentXorQword_RVA" && g_found[1].found) {
        // We know the key source RVA, scan nearby for byte values
        uint64_t base_rva = g_found[1].rva;
        uint64_t search_start = (base_rva > 0x10000) ? g_base + base_rva - 0x10000 : g_base;
        uint64_t search_end   = g_base + base_rva + 0x10000;

        int byte_count = 0;
        Log("  Scanning RVA 0x%llX - 0x%llX for non-zero bytes...\n",
            (unsigned long long)(search_start - g_base),
            (unsigned long long)(search_end - g_base));

        for (uint64_t addr = search_start; addr < search_end; addr += 1) {
            uint8_t b = Read<uint8_t>(addr);
            if (b != 0 && b != 0xFF) {
                byte_count++;
                if (byte_count <= 50) {
                    Log("  [RVA 0x%08llX] = 0x%02X (%u)\n",
                        (unsigned long long)(addr - g_base),
                        (unsigned)b, (unsigned)b);
                }
            }
        }
        if (byte_count > 50) {
            Log("  ... and %d more non-0xFF non-zero bytes\n", byte_count - 50);
        }
        Log("  Total non-zero bytes (non-0xFF): %d\n", byte_count);
    }

    FoundOffset fo;
    fo.name = "ComponentXorByte_RVA";
    fo.rva = OW::offset::ComponentXorByte_RVA;
    fo.found = (intl_byte != 0);
    if (fo.found) {
        fo.confidence = 0.5; // may still be valid at intl offset
        fo.evidence = FormatString("Intl RVA 0x%llX -> byte=0x%02X",
            (unsigned long long)OW::offset::ComponentXorByte_RVA, (unsigned)intl_byte);
    }
    g_found.push_back(fo);
}

// =========================================================================
// SCAN 5: ViewMatrix Base Pointer
// =========================================================================
static void ScanViewMatrix()
{
    LogSection("SCAN 5: ViewMatrix Base Pointer (Address_viewmatrix_base)");

    Log("[INFO] The viewmatrix base is a pointer stored in .data.\n");
    Log("[INFO] Its pointed-to value is encrypted: dec = (enc + key1) ^ key2\n");
    Log("[INFO] where key1=0x%llX, key2=0x%llX.\n",
        (unsigned long long)kVM_Key1, (unsigned long long)kVM_Key2);
    Log("[INFO] The decrypted value should be a valid pointer in game range.\n\n");

    // First check the international offset
    Log("[CHECK] International Address_viewmatrix_base = 0x%llX:\n",
        (unsigned long long)OW::offset::Address_viewmatrix_base);
    uint64_t intl_enc = Read<uint64_t>(g_base + OW::offset::Address_viewmatrix_base);
    Log("  [g_base + 0x%llX] = 0x%llX\n",
        (unsigned long long)OW::offset::Address_viewmatrix_base,
        (unsigned long long)intl_enc);

    uint64_t intl_dec = (intl_enc + kVM_Key1) ^ kVM_Key2;
    bool intl_dec_ok = intl_enc != 0 && LooksLikeImagePointer(intl_dec);
    Log("  Decrypted = 0x%llX, in-range=%s\n",
        (unsigned long long)intl_dec, PassFail(intl_dec_ok));

    if (intl_dec_ok) {
        // Check if we can follow the chain
        uint64_t p1 = Read<uint64_t>(intl_dec + kVM_P1);
        bool p1_ok = p1 != 0 && IsCanonicalUserPointer(p1);
        Log("  [dec + 0x%llX] (p1) = 0x%llX, valid=%s\n",
            (unsigned long long)kVM_P1, (unsigned long long)p1, PassFail(p1_ok));

        if (p1_ok) {
            uint64_t p2 = Read<uint64_t>(p1 + kVM_P2);
            bool p2_ok = p2 != 0 && IsCanonicalUserPointer(p2);
            Log("  [p1 + 0x%llX] (p2) = 0x%llX, valid=%s\n",
                (unsigned long long)kVM_P2, (unsigned long long)p2, PassFail(p2_ok));

            if (p2_ok) {
                // Try reading view matrix
                std::array<float, 16> view_mat{};
                if (ReadBuf(p2 + 0x140, view_mat.data(), sizeof(view_mat))) {
                    Log("  View matrix at p2+0x140: [%.2f, %.2f, %.2f, %.2f, ...]\n",
                        view_mat[0], view_mat[1], view_mat[2], view_mat[3]);
                }
                std::array<float, 16> proj_mat{};
                if (ReadBuf(p2 + 0xB0, proj_mat.data(), sizeof(proj_mat))) {
                    Log("  Proj matrix at p2+0xB0: [%.2f, %.2f, %.2f, %.2f, ...]\n",
                        proj_mat[0], proj_mat[1], proj_mat[2], proj_mat[3]);
                }
            }
        }
    }

    // Search for new VM base: look for slots where the encrypted-decrypted chain works
    Log("\n[SCAN] Searching for viewmatrix base pointers in .data...\n");
    Log("[SCAN] For each qword V at RVA r, test: dec = (V + k1) ^ k2\n");
    Log("[SCAN] dec must be in [base, base+size). Then check p1, p2 chain.\n");

    struct VMCandidate {
        uint64_t rva;
        uint64_t enc;
        uint64_t dec;
        uint64_t p1;
        uint64_t p2;
        bool     chain_ok;
    };

    const uint64_t scan_start = g_base + 0x3000000;
    const uint64_t scan_end   = g_base + 0x4A00000;
    const uint64_t scan_size  = scan_end - scan_start;
    static constexpr size_t kChunkSize = 0x100000;

    std::vector<VMCandidate> candidates;
    std::vector<uint8_t> chunk(kChunkSize);

    for (uint64_t chunk_start = scan_start; chunk_start < scan_end; chunk_start += kChunkSize) {
        size_t this_chunk = (std::min)(kChunkSize, (size_t)(scan_end - chunk_start));
        int pct = (int)((chunk_start - scan_start) * 100 / scan_size);
        Log("[SCAN] VM chunk at RVA 0x%llX ... %d%%\n",
            (unsigned long long)(chunk_start - g_base), pct);

        if (!ReadBuf(chunk_start, chunk.data(), this_chunk)) continue;

        size_t num_qwords = this_chunk / 8;
        const uint64_t* qwords = reinterpret_cast<const uint64_t*>(chunk.data());

        for (size_t qi = 0; qi < num_qwords; qi++) {
            uint64_t val = qwords[qi];
            if (!val) continue;

            uint64_t dec_val = (val + kVM_Key1) ^ kVM_Key2;
            if (!dec_val || !LooksLikeImagePointer(dec_val)) continue;

            uint64_t p1 = Read<uint64_t>(dec_val + kVM_P1);
            if (!IsCanonicalUserPointer(p1)) continue;
            uint64_t p2 = Read<uint64_t>(p1 + kVM_P2);
            if (!IsCanonicalUserPointer(p2)) continue;

            bool chain_ok = CanReadSize(p2, 0x200);
            VMCandidate c;
            c.rva = chunk_start - g_base + qi * 8;
            c.enc = val; c.dec = dec_val; c.p1 = p1; c.p2 = p2; c.chain_ok = chain_ok;
            candidates.push_back(c);
            if (candidates.size() >= 50) break;
        }
        if (candidates.size() >= 50) break;
    }

    Log("\n[RESULT] Found %zu viewmatrix base candidates.\n", candidates.size());
    if (!candidates.empty()) {
        Log("\n  %-4s %-12s %-18s %-18s %-18s %-18s %s\n",
            "Rank", "RVA", "Encrypted", "Decrypted", "p1", "p2", "Chain");
        Log("  %s\n", std::string(110, '-').c_str());
        for (size_t i = 0; i < candidates.size() && i < 20; i++) {
            const auto& c = candidates[i];
            Log("  %-4zu 0x%08llX 0x%016llX 0x%016llX 0x%016llX 0x%016llX %s\n",
                i + 1,
                (unsigned long long)c.rva,
                (unsigned long long)c.enc,
                (unsigned long long)c.dec,
                (unsigned long long)c.p1,
                (unsigned long long)c.p2,
                PassFail(c.chain_ok));
        }
    }

    FoundOffset fo;
    fo.name = "Address_viewmatrix_base";
    if (!candidates.empty()) {
        const auto& best = candidates[0];
        fo.rva = best.rva;
        fo.found = true;
        fo.confidence = best.chain_ok ? 0.9 : 0.5;
        fo.evidence = FormatString(
            "RVA 0x%llX enc=0x%llX dec=0x%llX p1=0x%llX p2=0x%llX chain=%s",
            (unsigned long long)best.rva,
            (unsigned long long)best.enc,
            (unsigned long long)best.dec,
            (unsigned long long)best.p1,
            (unsigned long long)best.p2,
            PassFail(best.chain_ok));
    }
    // Also check if intl offset works
    if (!fo.found && intl_dec_ok) {
        fo.rva = OW::offset::Address_viewmatrix_base;
        fo.found = true;
        fo.confidence = 0.6;
        fo.evidence = "International offset still valid";
    }
    g_found.push_back(fo);
}

// =========================================================================
// SCAN 6: GameAdmin Root Pointer
// =========================================================================
static void ScanGameAdmin()
{
    LogSection("SCAN 6: GameAdmin Root Pointer (Address_game_admin_root)");

    Log("[INFO] The game admin root is a pointer stored in .data.\n");
    Log("[INFO] decryption: slot_table = ROR34(ROR17((enc+Add1)^Xor1)+Add2)\n\n");

    // First check the international offset
    Log("[CHECK] International Address_game_admin_root = 0x%llX:\n",
        (unsigned long long)OW::offset::Address_game_admin_root);
    uint64_t intl_root_val = Read<uint64_t>(g_base + OW::offset::Address_game_admin_root);
    Log("  [g_base + 0x%llX] = 0x%llX\n",
        (unsigned long long)OW::offset::Address_game_admin_root,
        (unsigned long long)intl_root_val);

    bool intl_root_ok = intl_root_val != 0 && IsCanonicalUserPointer(intl_root_val);
    Log("  Valid pointer: %s\n", PassFail(intl_root_ok));

    if (intl_root_ok) {
        uint64_t enc = Read<uint64_t>(intl_root_val + kGA_RootPtr);
        Log("  [root + 0x%llX] (enc) = 0x%llX\n",
            (unsigned long long)kGA_RootPtr, (unsigned long long)enc);

        if (enc != 0) {
            // Apply decryption
            uint64_t st = enc + kGA_Add1;
            st ^= kGA_Xor1;
            // ROR by 17
            st = (st >> kGA_Ror1) | (st << (64 - kGA_Ror1));
            st += kGA_Add2;
            // ROR by 34
            st = (st >> kGA_Ror2) | (st << (64 - kGA_Ror2));

            bool st_ok = st != 0 && IsCanonicalUserPointer(st);
            Log("  Decrypted slot table = 0x%llX, valid=%s\n",
                (unsigned long long)st, PassFail(st_ok));

            if (st_ok) {
                // Check slot 6 (input system)
                uint64_t slot6 = Read<uint64_t>(st + 6 * 8);
                Log("  [slot_table + 6*8] = 0x%llX, valid=%s\n",
                    (unsigned long long)slot6,
                    PassFail(slot6 != 0 && IsCanonicalUserPointer(slot6)));
            }
        }
    }

    // Scan for game admin root candidates
    Log("\n[SCAN] Searching for game admin root pointers...\n");
    Log("[SCAN] Looking for pointers where [ptr + 0x30] contains a non-zero qword,\n");
    Log("[SCAN] and the decrypted slot table yields valid pointers.\n");

    struct GACandidate {
        uint64_t rva;
        uint64_t root_val;
        uint64_t enc;
        uint64_t slot_table;
        bool     chain_ok;
        uint64_t slot6;
    };

    const uint64_t scan_start = g_base + 0x3000000;
    const uint64_t scan_end   = g_base + 0x4A00000;
    const uint64_t scan_size  = scan_end - scan_start;
    static constexpr size_t kChunkSize = 0x100000;

    std::vector<GACandidate> candidates;
    std::vector<uint8_t> chunk(kChunkSize);

    auto ror64 = [](uint64_t x, int bits) -> uint64_t {
        bits &= 63;
        if (bits == 0) return x;
        return (x >> bits) | (x << (64 - bits));
    };

    for (uint64_t chunk_start = scan_start; chunk_start < scan_end; chunk_start += kChunkSize) {
        size_t this_chunk = (std::min)(kChunkSize, (size_t)(scan_end - chunk_start));
        int pct = (int)((chunk_start - scan_start) * 100 / scan_size);
        Log("[SCAN] GA chunk at RVA 0x%llX ... %d%%\n",
            (unsigned long long)(chunk_start - g_base), pct);

        if (!ReadBuf(chunk_start, chunk.data(), this_chunk)) continue;

        size_t num_qwords = this_chunk / 8;
        const uint64_t* qwords = reinterpret_cast<const uint64_t*>(chunk.data());

        for (size_t qi = 0; qi < num_qwords; qi++) {
            uint64_t val = qwords[qi];
            if (!val || !IsCanonicalUserPointer(val)) continue;

            uint64_t enc = Read<uint64_t>(val + kGA_RootPtr);
            if (!enc) continue;

            uint64_t st = enc + kGA_Add1;
            st ^= kGA_Xor1;
            st = ror64(st, kGA_Ror1);
            st += kGA_Add2;
            st = ror64(st, kGA_Ror2);
            if (!st || !IsCanonicalUserPointer(st)) continue;

            uint64_t s6 = Read<uint64_t>(st + 6 * 8);
            bool s6_ok = s6 != 0 && IsCanonicalUserPointer(s6) && CanReadSize(s6, 0x80);

            GACandidate c;
            c.rva = chunk_start - g_base + qi * 8;
            c.root_val = val; c.enc = enc; c.slot_table = st; c.chain_ok = s6_ok; c.slot6 = s6;
            candidates.push_back(c);
            if (candidates.size() >= 30) break;
        }
        if (candidates.size() >= 30) break;
    }

    Log("\n[RESULT] Found %zu game admin root candidates.\n", candidates.size());

    // Prefer candidates with working chain
    std::sort(candidates.begin(), candidates.end(),
        [](const GACandidate& a, const GACandidate& b) {
            if (a.chain_ok != b.chain_ok) return a.chain_ok;
            return a.rva < b.rva;
        });

    if (!candidates.empty()) {
        size_t display = (candidates.size() < 20) ? candidates.size() : 20;
        Log("\n  Top %zu candidates:\n", display);
        Log("  %-4s %-12s %-18s %-18s %-18s %-18s %s\n",
            "Rank", "RVA", "Root Val", "Encrypted", "Slot Table", "Slot6", "Chain");
        Log("  %s\n", std::string(110, '-').c_str());
        for (size_t i = 0; i < display; i++) {
            const auto& c = candidates[i];
            Log("  %-4zu 0x%08llX 0x%016llX 0x%016llX 0x%016llX 0x%016llX %s\n",
                i + 1,
                (unsigned long long)c.rva,
                (unsigned long long)c.root_val,
                (unsigned long long)c.enc,
                (unsigned long long)c.slot_table,
                (unsigned long long)c.slot6,
                PassFail(c.chain_ok));
        }
    }

    FoundOffset fo;
    fo.name = "Address_game_admin_root";
    if (!candidates.empty()) {
        const auto& best = candidates[0];
        fo.rva = best.rva;
        fo.found = true;
        fo.confidence = best.chain_ok ? 0.9 : 0.5;
        fo.evidence = FormatString(
            "RVA 0x%llX root=0x%llX enc=0x%llX table=0x%llX slot6=0x%llX chain=%s",
            (unsigned long long)best.rva,
            (unsigned long long)best.root_val,
            (unsigned long long)best.enc,
            (unsigned long long)best.slot_table,
            (unsigned long long)best.slot6,
            PassFail(best.chain_ok));
    }
    g_found.push_back(fo);
}

// =========================================================================
// SCAN 7: DecryptComponent Code Dump
// =========================================================================
static void ScanDecryptCode()
{
    LogSection("SCAN 7: DecryptComponent Code Dump");

    Log("[INFO] International DecryptComponent at RVA 0x%llX\n",
        (unsigned long long)kIntl_DecryptComponent_RVA);
    Log("[INFO] Reading code at this RVA on the current binary to check if matches.\n\n");

    const uint64_t known_rvas[] = {
        kIntl_DecryptComponent_RVA,
        kIntl_DecryptComponent_RVA + 0x100,
        kIntl_DecryptComponent_RVA + 0x200,
        kIntl_DecryptComponent_RVA + 0x300,
        kIntl_DecryptComponent_RVA + 0x400,
        kIntl_DecryptComponent_RVA + 0x500,
        kIntl_DecryptComponent_RVA + 0x420,  // ~tail area
        kIntl_GetGlobalKey_RVA,
        kIntl_DecryptVis_RVA,
    };

    for (uint64_t rva : known_rvas) {
        uint64_t addr = g_base + rva;
        // Check if this address is within game range
        if (addr >= g_end + 0x100000 || addr < g_base) {
            Log("[SKIP] RVA 0x%llX is outside game memory range.\n", (unsigned long long)rva);
            continue;
        }

        Log("\n[Dump at RVA 0x%08llX] (absolute 0x%llX):\n",
            (unsigned long long)rva, (unsigned long long)addr);

        uint8_t code[256];
        memset(code, 0, sizeof(code));
        ReadCodeBytes(addr, code, sizeof(code));

        // Print hex dump in 16-byte rows
        for (int row = 0; row < 16; row++) {
            int offset = row * 16;
            Log("  %04X: ", offset);
            for (int col = 0; col < 16 && (offset + col) < 256; col++) {
                Log("%02X ", code[offset + col]);
                if (col == 7) Log(" ");
            }
            Log("\n");
        }

        // Check for function prologue
        bool has_prologue = false;
        if (code[0] == 0x48 && code[1] == 0x89 && code[2] == 0x5C && code[3] == 0x24) {
            has_prologue = true; // mov [rsp+xx], rbx
        }
        if (code[0] == 0x48 && code[1] == 0x83 && code[2] == 0xEC) {
            has_prologue = true; // sub rsp, xx
        }
        if (code[0] == 0x40 && code[1] == 0x53) {
            has_prologue = true; // push rbx
        }
        if (code[0] == 0x48 && code[1] == 0x85 && code[2] == 0xC9) {
            has_prologue = true; // test rcx, rcx
        }
        if (code[0] == 0x48 && code[1] == 0x8B && code[2] == 0xC4) {
            has_prologue = true; // mov rax, rsp
        }

        if (has_prologue) {
            Log("\n  >>> Function prologue detected <<<\n");
        } else {
            Log("\n  (no obvious function prologue)\n");
        }
    }

    // Also scan the range around DecryptComponent to find where function might be
    Log("\n[SCAN] Scanning for function prologues near RVA 0x%llX...\n",
        (unsigned long long)kIntl_DecryptComponent_RVA);

    // Look for common prologue bytes: 48 89 5C 24 ?? (mov [rsp+xx], rbx)
    uint64_t search_start = g_base + kIntl_DecryptComponent_RVA - 0x2000;
    uint64_t search_end   = g_base + kIntl_DecryptComponent_RVA + 0x2000;
    if (search_start < g_base) search_start = g_base;
    if (search_end > g_end) search_end = g_end;

    int prologue_count = 0;
    for (uint64_t addr = search_start; addr < search_end; addr += 1) {
        uint8_t b = Read<uint8_t>(addr);
        // Check for common prologue patterns:
        // 48 89 5C 24 ??  = mov [rsp+??], rbx
        if (b == 0x48) {
            uint8_t b2 = Read<uint8_t>(addr + 1);
            if (b2 == 0x89) {
                uint8_t b3 = Read<uint8_t>(addr + 2);
                if (b3 == 0x5C) {
                    uint8_t b4 = Read<uint8_t>(addr + 3);
                    if (b4 == 0x24) {
                        prologue_count++;
                        if (prologue_count <= 30) {
                            Log("  [RVA 0x%08llX] mov [rsp+??], rbx\n",
                                (unsigned long long)(addr - g_base));
                        }
                        addr += 4; // skip past this pattern
                    }
                }
            }
        }
    }
    if (prologue_count > 30) {
        Log("  ... and %d more\n", prologue_count - 30);
    }
    Log("  Total prologue-like patterns found: %d\n", prologue_count);

    // Also check GetGlobalKey function area
    Log("\n[CHECK] GetGlobalKey at international RVA 0x%08llX:\n",
        (unsigned long long)kIntl_GetGlobalKey_RVA);
    uint64_t ggk_addr = g_base + kIntl_GetGlobalKey_RVA;
    if (ggk_addr < g_end && ggk_addr >= g_base) {
        uint8_t gk_code[64];
        memset(gk_code, 0, sizeof(gk_code));
        ReadCodeBytes(ggk_addr, gk_code, sizeof(gk_code));
        Log("  First 64 bytes:\n  ");
        for (int i = 0; i < 64; i++) {
            Log("%02X ", gk_code[i]);
            if ((i + 1) % 16 == 0) Log("\n  ");
        }
        Log("\n");

        // Look for LEA instruction (48 8D 05 xx xx xx xx or similar)
        bool found_lea = false;
        for (int i = 0; i < 60; i++) {
            if (gk_code[i] == 0x48 && gk_code[i+1] == 0x8D &&
                (gk_code[i+2] & 0xC7) == 0x05) {
                found_lea = true;
                int32_t disp;
                memcpy(&disp, gk_code + i + 3, 4);
                uint64_t target = ggk_addr + i + 7 + disp;
                Log("  LEA at offset +%d: target RVA = 0x%llX\n",
                    i, (unsigned long long)(target - g_base));
                break;
            }
        }
        if (!found_lea) {
            Log("  No LEA instruction found (function likely at different RVA on 国服)\n");
        }
    }
}

// =========================================================================
// SUMMARY
// =========================================================================
static void PrintSummary()
{
    LogSection("SUMMARY: Suggested 国服 Offsets");

    Log("  Found %zu offset candidates:\n\n", g_found.size());

    Log("  %-36s %-16s %-8s %-6s  %s\n",
        "Offset Name", "RVA", "Found", "Conf.", "Evidence");
    Log("  %s\n", std::string(140, '-').c_str());

    // Format the output as lines suitable for copy-paste into Offsets.hpp
    Log("\n");
    Log("  Suggested Offsets.hpp entries for 国服:\n");
    Log("  ==========================================\n\n");

    for (const auto& fo : g_found) {
        // Main table row
        Log("  %-36s 0x%010llX %-8s %5.1f%%  %s\n",
            fo.name.c_str(),
            (unsigned long long)fo.rva,
            fo.found ? "YES" : "NO",
            fo.confidence * 100.0,
            fo.evidence.c_str());

        // Suggested C++ line
        if (fo.found && fo.confidence >= 0.3) {
            Log("    ==> static constexpr auto %-27s = 0x%llX; // confidence=%.0f%% %s\n",
                (fo.name + ";").c_str(),
                (unsigned long long)fo.rva,
                fo.confidence * 100.0,
                fo.evidence.c_str());
        } else {
            Log("    ==> (could not determine with confidence)\n");
        }
        Log("\n");
    }

    // Print confidence assessment
    int high = 0, medium = 0, low = 0, none = 0;
    for (const auto& fo : g_found) {
        if (!fo.found || fo.confidence < 0.1) none++;
        else if (fo.confidence < 0.4) low++;
        else if (fo.confidence < 0.7) medium++;
        else high++;
    }

    Log("\n  Confidence Distribution:\n");
    Log("    High   (>= 70%%): %d\n", high);
    Log("    Medium (>= 40%%): %d\n", medium);
    Log("    Low    (> 0%%):   %d\n", low);
    Log("    None   (0%%):     %d\n", none);

    Log("\n  Key Observations:\n");
    if (g_pe_valid) {
        Log("    PE: Sections=%u, ImageSize=0x%llX (%s expected 0x50BB000)\n",
            g_nt.FileHeader.NumberOfSections,
            (unsigned long long)g_nt.OptionalHeader.SizeOfImage,
            g_nt.OptionalHeader.SizeOfImage == 0x50BB000 ? "MATCHES 国服" : "DIFFERENT from 国服");
    } else {
        Log("    PE headers could not be read.\n");
    }
    Log("    Raw base: 0x%llX (low32=0x%llX)\n",
        (unsigned long long)g_base,
        (unsigned long long)(g_base & 0xFFFFFFFF));

    Log("\n");
}

// =========================================================================
// Main
// =========================================================================
int main()
{
    Log("================================================================\n");
    Log("  CN Server (国服) Offset Scanner\n");
    Log("  Overwatch -- DMA-based RVA Discovery for Chinese Binary\n");
    Log("================================================================\n\n");

    // ---- Step 1: Init DMA ----
    if (!InitDMA()) {
        Log("[FATAL] DMA initialisation failed.\n");
        Log("\nPress Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // ---- Step 2: Attach to Overwatch ----
    if (!AttachOverwatch()) {
        Log("[FATAL] Could not attach to Overwatch.exe.\n");
        Log("[HINT] Make sure Overwatch.exe is running.\n");
        VMMDLL_Close(g_vmm);
        Log("\nPress Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // Determine if we might be on 国服 based on image size
    bool likely_cn = (g_size >= 0x5000000);
    Log("\n[INFO] Image size 0x%llX -> %s\n",
        (unsigned long long)g_size,
        likely_cn ? "LIKELY 国服 build" : "appears to be international build");
    if (!likely_cn) {
        Log("[WARN] This scanner is designed for the 国服 binary (ImageSize ~0x50BB000).\n");
        Log("[WARN] The current binary may be the international version (ImageSize ~0x4725000).\n");
        Log("[WARN] Results may still be useful for cross-reference.\n");
    }

    // ---- Scan 1: PE Headers ----
    ScanPEHeaders();

    // ---- Scan 2: Entity List Pointer ----
    ScanEntityBase();

    // ---- Scan 3: Component Key Source ----
    ScanKeySource();

    // ---- Scan 4: Component Key Byte ----
    ScanKeyByte();

    // ---- Scan 5: ViewMatrix Base ----
    ScanViewMatrix();

    // ---- Scan 6: GameAdmin Root ----
    ScanGameAdmin();

    // ---- Scan 7: DecryptComponent Code Dump ----
    ScanDecryptCode();

    // ---- Summary ----
    PrintSummary();

    // ---- Cleanup ----
    VMMDLL_Close(g_vmm);
    Log("Scan complete.\n");
    Log("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
