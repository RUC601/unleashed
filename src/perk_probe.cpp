// =============================================================================
// Perk Probe — scan Hero component + unknown component IDs for perk data
//
// Perk system (威能): minor perk (level 2) + major perk (level 3)
// Each is one of: 0=not picked, 1=left option, 2=right option
// Hypothesis: 2x int32 or 4x bool in Hero component, or a dedicated component
//
// Uses CURRENT 2026-05-27 decryption from Decrypt.hpp / Offsets.hpp
// =============================================================================

#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <intrin.h>

#include "leechcore.h"
#include "vmmdll.h"
#include "Memory/MemoryRanges.h"

// Current offsets from Offsets.hpp 2026-05-27
static constexpr uint64_t Address_entity_base     = 0x3935908;
static constexpr uint64_t ComponentXorQword_RVA   = 0x3A92E70;
static constexpr uint64_t ComponentXorQword_Off   = 0x1D4;
static constexpr uint64_t ComponentXorByte_RVA    = 0x377E243;
static constexpr uint64_t Component_Xor1          = 0xDC01B58B9BDFFB4B;
static constexpr uint64_t Component_Add1          = 0x024620C984E36588;
static constexpr uint64_t Component_Sub1          = 0x7D957CD64821F39B;
static constexpr int      Component_Ror1          = 32;
static constexpr int      Component_Ror2          = 60;
static constexpr int      Component_Ror3          = 57;

// DMA globals
static VMM_HANDLE  g_vmm  = nullptr;
static DWORD       g_pid  = 0;
static uint64_t    g_base = 0;
static uint64_t    g_size = 0;

// Key material (resolved from memory)
static uint64_t g_key_ptr        = 0;
static uint64_t g_key_material   = 0;
static uint8_t  g_key_byte       = 0;

// Known component types (from plexies p332)
enum CompType : uint8_t {
    TYPE_LINK             = 0x34,
    TYPE_VISIBILITY       = 0x35,
    TYPE_SKILL            = 0x37,
    TYPE_HEALTH           = 0x3B,
    TYPE_PLAYERCONTROLLER = 0x43,
    TYPE_P_HEROID         = 0x54,
    TYPE_OUTLINE           = 0x5A,
};

// Candidate unknown component IDs to try for perks
static const uint8_t candidate_comp_ids[] = {
    0x02, 0x03, 0x05, 0x06, 0x07, 0x08,
    0x36, 0x38, 0x3A, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E,
    0x4F, 0x50, 0x51, 0x52, 0x53,
    0x55, 0x56, 0x57, 0x58, 0x59,
    0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
    0x61, 0x62, 0x63, 0x64, 0x65,
};

// =============================================================================
// DMA helpers
// =============================================================================

template<typename T>
T Read(uint64_t addr) {
    T buf{};
    if (addr > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(addr))
        return buf;
    DWORD dwRead = 0;
    VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), &dwRead,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    return buf;
}

void read_buf(uint64_t addr, uint8_t* buf, size_t sz) {
    memset(buf, 0, sz);
    for (size_t i = 0; i < sz; i += 8) {
        uint64_t cur = addr + i;
        if (cur > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(cur))
            break;
        uint64_t qw = Read<uint64_t>(cur);
        size_t cp = (sz - i >= 8) ? 8 : (sz - i);
        memcpy(buf + i, &qw, cp);
    }
}

static bool looks_like_ptr(uint64_t v) {
    // Accept Windows user-mode pointers: any 64-bit address in valid user range
    return v > 0x10000 && v < 0x00007FFFFFFFFFFF;
}

// =============================================================================
// Crypto — current transform from Decrypt.hpp (2026-05-27)
// =============================================================================

static inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    if (bits == 0) return x;
    return (x >> bits) | (x << (64 - bits));
}

static bool InitCrypto() {
    g_key_ptr = Read<uint64_t>(g_base + ComponentXorQword_RVA);
    printf("[CRYPTO] KeyPtr = 0x%016llX (RVA 0x%llX)\n",
           g_key_ptr, g_key_ptr - g_base);

    if (!looks_like_ptr(g_key_ptr)) {
        printf("[FAIL] KeyPtr out of range\n");
        return false;
    }

    g_key_material = Read<uint64_t>(g_key_ptr + ComponentXorQword_Off);
    g_key_byte     = Read<uint8_t>(g_base + ComponentXorByte_RVA);

    printf("[CRYPTO] KeyMaterial = 0x%016llX (@ +0x%llX)\n",
           g_key_material, (unsigned long long)ComponentXorQword_Off);
    printf("[CRYPTO] ByteKey     = 0x%02X\n", g_key_byte);
    return true;
}

// Current DecryptComponent transform (from Decrypt.hpp, forum p331, IDA 0527)
static uint64_t DecryptComponent(uint64_t parent, uint8_t idx) {
    if (!parent || !g_key_ptr) return 0;

    uint64_t v1 = parent;
    uint64_t bit_mask  = 1ULL << (idx & 0x3F);
    uint64_t lower_mask = bit_mask - 1;
    uint32_t shift      = idx & 0x3F;
    uint32_t bucket     = idx >> 6;

    uint64_t comp_bits = Read<uint64_t>(v1 + 8 * bucket + 0x110);
    uint64_t present   = (comp_bits & bit_mask) >> shift;
    if (!present) return 0;

    uint64_t below = comp_bits & lower_mask;
    below = below - ((below >> 1) & 0x5555555555555555);
    below = (below & 0x3333333333333333) + ((below >> 2) & 0x3333333333333333);
    below = (below + (below >> 4)) & 0x0F0F0F0F0F0F0F0F;

    uint8_t idx_byte = Read<uint8_t>(v1 + bucket + 0x130);
    uint64_t comp_idx = idx_byte + ((below * 0x0101010101010101) >> 0x38);

    uint64_t comp_table = Read<uint64_t>(v1 + 0x80);
    if (!comp_table) return 0;

    uint64_t component = Read<uint64_t>(comp_table + 8 * comp_idx);
    if (!component) return 0;

    // Current transform (0527)
    component ^= g_key_material;
    component ^= Component_Xor1;
    component  = ROR64(component, Component_Ror1);
    component += Component_Add1;
    component ^= (uint64_t)g_key_byte;
    component -= Component_Sub1;
    component  = ROR64(component, Component_Ror2);
    component  = ROR64(component, Component_Ror3);

    uint64_t present_mask = (uint64_t)((int64_t)(-(int32_t)present));
    return present_mask & component;
}

// =============================================================================
// Hex dump helper
// =============================================================================

static void hexdump(const uint8_t* data, size_t len, uint64_t base_addr) {
    for (size_t off = 0; off < len; off += 16) {
        printf("    +0x%03llX: ", (unsigned long long)off);
        for (size_t j = 0; j < 16 && (off + j) < len; j++)
            printf("%02X ", data[off + j]);
        printf(" ");
        for (size_t j = 0; j < 16 && (off + j) < len; j++) {
            uint8_t b = data[off + j];
            printf("%c", (b >= 32 && b < 127) ? (char)b : '.');
        }
        printf("\n");
    }
}

// =============================================================================
// Main
// =============================================================================

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Perk Probe (2026-05-27 decrypt) ===\n\n");

    // --- DMA init ---
    printf("[DMA] Initializing...\n");
    {
        LPSTR args[] = {
            (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0",
            (LPSTR)"-waitinitialize", (LPSTR)"-norefresh"
        };
        g_vmm = VMMDLL_Initialize(5, args);
    }
    if (!g_vmm) {
        printf("[FAIL] DMA init failed\n");
        std::getchar();
        return 1;
    }
    printf("[DMA] Connected\n");

    // --- Find Overwatch.exe ---
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    if (!g_pid) {
        printf("[FAIL] Overwatch.exe not found\n");
        VMMDLL_Close(g_vmm);
        std::getchar();
        return 1;
    }
    printf("[DMA] Overwatch.exe PID = %u\n", g_pid);

    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, (LPSTR)"Overwatch.exe");
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
    printf("[DMA] GameBase = 0x%llX  Size = 0x%llX\n\n", g_base, g_size);

    // --- Init crypto ---
    if (!InitCrypto()) {
        printf("[FAIL] Could not init crypto\n");
        VMMDLL_Close(g_vmm);
        std::getchar();
        return 1;
    }

    // --- Read entity list ---
    uint64_t entity_list_ptr = Read<uint64_t>(g_base + Address_entity_base);
    printf("\n[ENTITY] List @ 0x%016llX (RVA 0x%llX)\n",
           entity_list_ptr, entity_list_ptr - g_base);

    if (!looks_like_ptr(entity_list_ptr)) {
        printf("[FAIL] Invalid entity list pointer\n");
        VMMDLL_Close(g_vmm);
        std::getchar();
        return 1;
    }

    // Read 4096 entity slots (stride 0x10: {entity, pad})
    struct { uint64_t entity; uint64_t pad; } raw_list[4096] = {};
    read_buf(entity_list_ptr, (uint8_t*)raw_list, sizeof(raw_list));

    // Collect hero entities
    struct HeroEntity {
        int slot;
        uint64_t entity;
        uint64_t link_parent;
        uint64_t hero_base;
        uint64_t hero_id;
        bool is_local;
    };
    std::vector<HeroEntity> heroes;

    int total_slots = 0;
    for (int i = 0; i < 4096; i++) {
        uint64_t cur = raw_list[i].entity;
        if (!cur) continue;
        total_slots++;

        uint64_t link = DecryptComponent(cur, TYPE_LINK);
        if (!looks_like_ptr(link)) continue;

        uint64_t hbase = DecryptComponent(link, TYPE_P_HEROID);
        if (!looks_like_ptr(hbase)) continue;

        uint64_t hid = Read<uint64_t>(hbase + 0xD0);
        if (hid < 0x100) continue;

        uint64_t pc = DecryptComponent(link, TYPE_PLAYERCONTROLLER);

        HeroEntity h;
        h.slot     = i;
        h.entity   = cur;
        h.link_parent = link;
        h.hero_base   = hbase;
        h.hero_id     = hid;
        h.is_local    = (pc != 0 && looks_like_ptr(pc));
        heroes.push_back(h);
    }

    printf("[SCAN] %d entity slots\n", total_slots);

    // DIAGNOSTIC: dump first few valid entities' component headers
    int diag_count = 0;
    for (int i = 0; i < 4096 && diag_count < 3; i++) {
        uint64_t cur = raw_list[i].entity;
        if (!cur) continue;
        diag_count++;

        printf("\n  [DIAG] Entity slot %d @ 0x%016llX:\n", i, cur);
        uint64_t bm0 = Read<uint64_t>(cur + 0x110);
        uint64_t bm1 = Read<uint64_t>(cur + 0x118);
        printf("    +0x110 bitmap[0] = 0x%016llX\n", bm0);
        printf("    +0x118 bitmap[1] = 0x%016llX\n", bm1);

        uint8_t ib0 = Read<uint8_t>(cur + 0x130);
        uint8_t ib1 = Read<uint8_t>(cur + 0x131);
        printf("    +0x130 idxbytes  = %u %u\n", ib0, ib1);

        uint64_t ct = Read<uint64_t>(cur + 0x80);
        printf("    +0x080 comptable = 0x%016llX\n", ct);

        // Test components that ARE in the bitmap (0x01 TRANSFORM, 0x03, 0x0B)
        uint64_t t01 = DecryptComponent(cur, 0x01);
        printf("    DecryptComponent(+0x01 in bitmap) = 0x%016llX\n", t01);
        uint64_t t03 = DecryptComponent(cur, 0x03);
        printf("    DecryptComponent(+0x03 in bitmap) = 0x%016llX\n", t03);
        uint64_t t0b = DecryptComponent(cur, 0x0B);
        printf("    DecryptComponent(+0x0B in bitmap) = 0x%016llX\n", t0b);

        // Try components not in bitmap — should return 0
        uint64_t link = DecryptComponent(cur, TYPE_LINK);
        printf("    DecryptComponent(+0x34 LINK)    = 0x%016llX\n", link);
        uint64_t hero = DecryptComponent(cur, TYPE_P_HEROID);
        printf("    DecryptComponent(+0x54 HERO)    = 0x%016llX\n", hero);
    }
    printf("\n");

    if (heroes.empty()) {
        printf("[FAIL] No heroes — check DecryptComponent\n");
        // still continue to show phase 2 brute force on a raw entity
    }

    // Pick local player for detailed analysis
    int detail_idx = 0;
    for (size_t i = 0; i < heroes.size(); i++) {
        if (heroes[i].is_local) { detail_idx = (int)i; break; }
    }
    printf("[INFO] Using hero slot %d (heroID=0x%llX) as primary probe target\n\n",
           heroes[detail_idx].slot, heroes[detail_idx].hero_id);

    // =========================================================================
    // PHASE 1: Scan Hero component (0x54) for perk-like data
    // =========================================================================

    printf("========================================\n");
    printf("  PHASE 1: Hero Component (0x54) scan\n");
    printf("========================================\n\n");

    for (size_t hi = 0; hi < heroes.size(); hi++) {
        auto& h = heroes[hi];
        bool detailed = (hi == (size_t)detail_idx);

        uint8_t buf[0x200] = {};
        read_buf(h.hero_base, buf, sizeof(buf));

        // Scan for int32 in [0,2]
        struct PerkCandidate { int off; int32_t val; };
        std::vector<PerkCandidate> candidates;
        for (int off = 0; off < (int)sizeof(buf) - 4; off += 4) {
            int32_t v = *(int32_t*)(buf + off);
            if (v >= 0 && v <= 2)
                candidates.push_back({off, v});
        }

        printf("  [Slot %d] hero_base=0x%llX heroID=0x%llX %s\n",
               h.slot, h.hero_base, h.hero_id,
               h.is_local ? "**LOCAL**" : "");

        for (auto& c : candidates) {
            printf("    +0x%03X: %d  [ ", c.off, c.val);
            for (int j = -4; j <= 8; j += 4) {
                int o = c.off + j;
                if (o >= 0 && o < (int)sizeof(buf) - 4)
                    printf("+0x%X=%d ", o, *(int32_t*)(buf + o));
            }
            printf("]\n");
        }
        if (candidates.empty())
            printf("    (no int32 values in [0,2] found)\n");

        if (detailed) {
            printf("\n    Hex dump (+0x00..+0xFF):\n");
            hexdump(buf, 0x100, h.hero_base);

            // Also show specific important offsets
            printf("\n    Key offsets:\n");
            for (int off = 0xD0; off <= 0x110; off += 0x10) {
                printf("    +0x%03X: ", off);
                for (int b = 0; b < 16 && (off+b) < (int)sizeof(buf); b += 4) {
                    int32_t v = *(int32_t*)(buf + off + b);
                    printf("%d(%d) ", b, v);
                }
                printf("\n");
            }
        }
    }

    // =========================================================================
    // PHASE 2: Brute-force unknown component IDs
    // =========================================================================

    printf("\n========================================\n");
    printf("  PHASE 2: Unknown Component ID brute-force\n");
    printf("========================================\n\n");

    auto& probe_hero = heroes[detail_idx];
    int num_ids = sizeof(candidate_comp_ids) / sizeof(candidate_comp_ids[0]);
    int found_count = 0;

    printf("  Probing on entity slot %d (heroID=0x%llX)\n",
           probe_hero.slot, probe_hero.hero_id);

    for (int ci = 0; ci < num_ids; ci++) {
        uint8_t cid = candidate_comp_ids[ci];
        uint64_t comp = DecryptComponent(probe_hero.entity, cid);
        if (!looks_like_ptr(comp))
            comp = DecryptComponent(probe_hero.link_parent, cid);

        if (looks_like_ptr(comp)) {
            found_count++;
            uint8_t buf[0x40] = {};
            read_buf(comp, buf, sizeof(buf));

            printf("\n  [ID 0x%02X] @ 0x%016llX:\n", cid, comp);
            hexdump(buf, 0x40, comp);

            // Perk-like scan
            printf("    int32 in [0,2]:");
            bool any = false;
            for (int o = 0; o < (int)sizeof(buf) - 4; o += 4) {
                int32_t v = *(int32_t*)(buf + o);
                if (v >= 0 && v <= 2) { printf(" +0x%02X=%d", o, v); any = true; }
            }
            if (!any) printf(" (none)");
            printf("\n    int8 in [0,2]: ");
            any = false;
            for (int o = 0; o < (int)sizeof(buf); o++) {
                if (buf[o] <= 2) { printf("+0x%02X=%u ", o, buf[o]); any = true; }
            }
            if (!any) printf("(none)");
            printf("\n");
        }
    }

    printf("\n  Tested %d unknown IDs, found %d valid\n", num_ids, found_count);

    // =========================================================================
    // PHASE 3: Cross-hero comparison for valid IDs from Phase 2
    // =========================================================================

    printf("\n========================================\n");
    printf("  PHASE 3: Cross-hero comparison\n");
    printf("========================================\n\n");

    const uint8_t quick_ids[] = {0x55, 0x56, 0x57, 0x58, 0x59, 0x5B, 0x5C, 0x5D};
    int quick_count = sizeof(quick_ids) / sizeof(quick_ids[0]);

    for (int qi = 0; qi < quick_count; qi++) {
        uint8_t cid = quick_ids[qi];
        printf("  CompID 0x%02X:", cid);

        bool any_valid = false;
        for (auto& h : heroes) {
            uint64_t comp = DecryptComponent(h.entity, cid);
            if (!looks_like_ptr(comp))
                comp = DecryptComponent(h.link_parent, cid);

            if (looks_like_ptr(comp)) {
                any_valid = true;
                uint8_t buf[0x20] = {};
                read_buf(comp, buf, sizeof(buf));

                printf("\n    Slot %d (heroID=0x%llX): ", h.slot, h.hero_id);
                for (int o = 0; o < 16; o += 4)
                    printf("%s%d", o ? " / " : "", *(int32_t*)(buf + o));
            }
        }
        if (!any_valid) printf(" (no valid)");
        printf("\n");
    }

    // =========================================================================
    // Done
    // =========================================================================
    printf("\n========================================\n");
    printf("  Probe complete.\n");
    printf("========================================\n");

    VMMDLL_Close(g_vmm);
    printf("\nPress Enter to exit.\n");
    std::getchar();
    return 0;
}
