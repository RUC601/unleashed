// =============================================================================
// Hero Probe — read currently selected hero from Overwatch.exe via DMA
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

// HIDWORD/LODWORD macros (may be missing in newer SDKs)
#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

// DMA globals
static VMM_HANDLE  g_vmm  = nullptr;
static DWORD       g_pid  = 0;
static uint64_t    g_base = 0;
static uint64_t    g_size = 0;

// =============================================================================
// Offset / constant definitions (from Offsets.hpp)
// =============================================================================

static constexpr uint64_t KeyGlobalStore_RVA  = 0x40405F8;
static constexpr uint64_t KeyGlobal_XOR       = 0xF5;
static constexpr uint64_t Address_entity_base = 0x37EC5E0;
static constexpr uint64_t DecryptTable_1      = 0x38996E0;
static constexpr uint64_t DecryptTable_2      = 0x389A700;
static constexpr uint64_t DecryptTable_Mask   = 0x7FF;

// Global keys
static uint64_t Key1 = 0;
static uint64_t Key2 = 0;

// Component types
enum CompType : uint8_t {
    TYPE_LINK             = 0x34,
    TYPE_P_HEROID         = 0x54,
    TYPE_PLAYERCONTROLLER = 0x43,
    TYPE_HEALTH           = 0x3B,
    TYPE_TEAM             = 0x21,
    TYPE_VELOCITY         = 0x4,
    TYPE_P_VISIBILITY     = 0x35,
};

// =============================================================================
// DMA read helpers
// =============================================================================

template<typename T>
T Read(uint64_t addr) {
    T buf{};
    if (addr > 0x7FFFFFFFFFFFULL && !IsValidPhysicalAddress(addr)) {
        printf("[MMAP] Skipping physical-looking address 0x%llX outside valid physical ranges\n",
            (unsigned long long)addr);
        return buf;
    }
    DWORD dwRead = 0;
    BOOL ok = VMMDLL_MemReadEx(g_vmm, g_pid, addr, (PBYTE)&buf, sizeof(T), &dwRead,
        VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_NOPAGING_IO | VMMDLL_FLAG_ZEROPAD_ON_FAIL);
    if ((!ok || dwRead == 0) && addr > 0x7FFFFFFFFFFFULL) {
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
        size_t cp = (sz - i >= 8) ? 8 : (sz - i);
        memcpy(buf + i, &qw, cp);
    }
}

// =============================================================================
// Crypto helpers (from Decrypt.hpp)
// =============================================================================

static inline uint64_t ROR64(uint64_t x, int bits) {
    bits &= 63;
    if (bits == 0) return x;
    return (x >> bits) | (x << (64 - bits));
}

static inline uint64_t GetParent(uint64_t encrypted) {
    auto result = encrypted;
    result -= 0x401C60913E3B91CE;
    result = (result >> 0x20) | (result << 0x20);
    return result;
}

// =============================================================================
// DecryptComponent — exact logic from Decrypt.hpp (ported to direct VMMDLL)
// =============================================================================

static uint64_t DecryptComponent(uint64_t parent, uint8_t idx) {
    if (!parent || !Key1 || !Key2) return 0;

    uint64_t v1 = parent;
    uint64_t v2 = (uintptr_t)1 << (uintptr_t)(idx & 0x3F);
    uint64_t v3 = v2 - 1;
    uint64_t v4 = idx & 0x3F;
    uint64_t v5 = idx / 0x3F;

    uint64_t v6 = Read<uint64_t>(v1 + 8 * (uint32_t)v5 + 0x110);
    if (!v6) return 0;

    uint64_t v7 = (v2 & v6) >> v4;
    uint64_t v8 = (v3 & v6) - (((v3 & v6) >> 1) & 0x5555555555555555);

    uint64_t inner_ptr = Read<uint64_t>(v1 + 0x80);
    if (!inner_ptr) return 0;

    uint64_t v9 = Read<uint64_t>(
        inner_ptr +
        8 * (Read<uint8_t>((uint32_t)v5 + v1 + 0x130) +
            ((0x101010101010101 * (
                ((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333) +
                (((v8 & 0x3333333333333333) + ((v8 >> 2) & 0x3333333333333333)) >> 4))
            ) & 0xF0F0F0F0F0F0F0F0) >> 0x38))
    );
    if (!v9) return 0;

    auto dummy  = Read<uint64_t>(g_base + DecryptTable_1 + ((Key1 >> 0x34) & DecryptTable_Mask));
    auto dummy2 = Read<uint64_t>(g_base + DecryptTable_1 + (Key1 & DecryptTable_Mask));

    uint64_t v10 = (unsigned int)v9 | v9 & 0xFFFFFFFF00000000ui64 ^
                   ((uint64_t)((unsigned int)v9 + 0x71747EF8) << 0x20);
    uint64_t v11 = Key2 ^ ((unsigned int)v9 | v10 & 0xFFFFFFFF00000000ui64 ^
                   ((uint64_t)(unsigned int)(v10 + __ROL4__(HIDWORD(dummy), 1)) << 0x20));
    uint64_t v12 = -(int)v7 & ((unsigned int)v11 |
                   ((unsigned int)v11 | v11 & 0xFFFFFFFF00000000ui64 ^
                    ((uint64_t)((unsigned int)v11 ^ ~(unsigned int)dummy2) << 0x20)) &
                    0xFFFFFFFF00000000ui64 ^
                    ((uint64_t)((unsigned int)v11 ^ 0xDFBFA250) << 0x20));
    return v12;
}

// Heuristic: returns true if a value looks like a valid pointer into game memory
static inline bool looks_like_ptr(uint64_t v) {
    return v >= g_base && v < g_base + g_size;
}

// =============================================================================
// Hero name lookup (from Structs.hpp)
// =============================================================================

static const char* GetHeroName(uint64_t heroID) {
    switch (heroID) {
        case 0x2E0000000000002:  return "Reaper";
        case 0x2E0000000000003:  return "Tracer";
        case 0x2E0000000000004:  return "Mercy";
        case 0x2E0000000000005:  return "Hanzo";
        case 0x2E0000000000006:  return "Torbjorn";
        case 0x2E0000000000007:  return "Reinhardt";
        case 0x2E0000000000008:  return "Pharah";
        case 0x2E0000000000009:  return "Winston";
        case 0x2E000000000000A:  return "Widowmaker";
        case 0x2E0000000000015:  return "Bastion";
        case 0x2E0000000000016:  return "Symmetra";
        case 0x2E0000000000020:  return "Zenyatta";
        case 0x2E0000000000029:  return "Genji";
        case 0x2E0000000000040:  return "Roadhog";
        case 0x2E0000000000042:  return "McCree";
        case 0x2E0000000000065:  return "Junkrat";
        case 0x2E0000000000068:  return "Zarya";
        case 0x2E000000000006E:  return "Soldier 76";
        case 0x2E0000000000079:  return "Lucio";
        case 0x2E000000000007A:  return "D.Va";
        case 0x2E00000000000DD:  return "Mei";
        case 0x2E000000000013B:  return "Ana";
        case 0x2E000000000012E:  return "Sombra";
        case 0x2E000000000013E:  return "Orisa";
        case 0x2E000000000012F:  return "Doomfist";
        case 0x2E00000000001A2:  return "Moira";
        case 0x2E0000000000195:  return "Brigitte";
        case 0x2E00000000001CA:  return "Wrecking Ball";
        case 0x2E00000000001EC:  return "Sojourn";
        case 0x2E0000000000200:  return "Ashe";
        case 0x2E0000000000221:  return "Baptiste";
        case 0x2E0000000000231:  return "Kiriko";
        case 0x2E0000000000236:  return "Junker Queen";
        case 0x2E000000000023B:  return "Sigma";
        case 0x2E0000000000206:  return "Echo";
        case 0x2E000000000028D:  return "Ramattra";
        case 0x02E0000000000291: return "LifeWeaver";
        case 0x02E000000000031C: return "Illari";
        case 0x02E000000000030A: return "Mauga";
        case 0x2E000000000032B:  return "Venture";
        case 0x2E000000000033C:  return "Training Bot (Standard)";
        case 0x2E0000000000337:  return "Training Bot (Tank)";
        case 0x2E000000000035A:  return "Training Bot (Sniper)";
        case 0x2E000000000016C:  return "Training Bot (Friend)";
        case 0x2E0000000000363:  return "Training Bot (Friend Tank)";
        case 0x2E0000000000349:  return "Training Bot (Rocket)";
        case 0x2E0000000000339:  return "Training Bot";
        default:                 return nullptr;
    }
}

// =============================================================================
// Entity slot (16 bytes each: entity ptr + padding)
// =============================================================================
struct EntitySlot {
    uint64_t entity;
    uint64_t pad;
};

// =============================================================================
// Main
// =============================================================================

int main() {
    printf("=== Hero Probe ===\n\n");

    // -------------------------------------------------------------------------
    // Step 1 — DMA init (wait for FTDI FT601 USB FPGA)
    // -------------------------------------------------------------------------
    printf("[DMA] Initializing (waiting for FTDI FT601 USB FPGA)...\n");

    {
        LPSTR args[] = {
            (LPSTR)"", (LPSTR)"-device", (LPSTR)"fpga://algo=0",
            (LPSTR)"-waitinitialize", (LPSTR)"-norefresh"
        };
        g_vmm = VMMDLL_Initialize(5, args);
    }

    if (!g_vmm) {
        printf("[FAIL] DMA init — FTDI FT601 USB FPGA not detected\n");
        printf("[INFO] Make sure:\n");
        printf("       1. FTDI FT601 USB board is plugged in and powered\n");
        printf("       2. FTD3XX.dll is in the same directory as this .exe\n");
        printf("       3. WinUSB / FTDI driver is installed\n");
        printf("Press Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // Show FPGA details
    ULONG64 fpga_id = 0, ver_major = 0, ver_minor = 0;
    VMMDLL_ConfigGet(g_vmm, LC_OPT_FPGA_FPGA_ID, &fpga_id);
    VMMDLL_ConfigGet(g_vmm, LC_OPT_FPGA_VERSION_MAJOR, &ver_major);
    VMMDLL_ConfigGet(g_vmm, LC_OPT_FPGA_VERSION_MINOR, &ver_minor);
    printf("[DMA] Connected — FPGA ID=%llu  Version=%llu.%llu\n", fpga_id, ver_major, ver_minor);

    // -------------------------------------------------------------------------
    // Step 2 — Find Overwatch.exe
    // -------------------------------------------------------------------------
    VMMDLL_PidGetFromName(g_vmm, (LPSTR)"Overwatch.exe", &g_pid);
    if (!g_pid) {
        printf("[FAIL] Overwatch.exe not running\n");
        VMMDLL_Close(g_vmm);
        printf("Press Enter to exit.\n");
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

    // -------------------------------------------------------------------------
    // Step 3 — Read global encryption keys
    // -------------------------------------------------------------------------
    uint64_t key_raw = Read<uint64_t>(g_base + KeyGlobalStore_RVA);
    Key1 = key_raw ^ KeyGlobal_XOR;

    printf("[KEY] Global store (RVA 0x%X) raw  = 0x%016llX\n", (unsigned)KeyGlobalStore_RVA, key_raw);
    printf("[KEY] Key1 = raw ^ 0x%02llX           = 0x%016llX\n", KeyGlobal_XOR, Key1);

    if (!Key1 || Key1 < 0x1000000000000000) {
        printf("[WARN] Key1 looks invalid (< 0x1000...)\n");
        printf("[INFO] Trying IDA-based GetGlobalKey method...\n");

        // IDA method: read GetGlobalKey function code and decode
        uint64_t gk_addr = g_base + 0x581D20;
        uint8_t code[128] = {};
        read_buf(gk_addr, code, sizeof(code));

        // Find LEA instruction
        uint64_t lea_target = 0;
        for (int i = 0; i < 120; i++) {
            if (code[i] == 0x48 && code[i+1] == 0x8D &&
                (code[i+2] & 0xC7) == 0x05) {
                int32_t disp = *(int32_t*)&code[i+3];
                lea_target = gk_addr + i + 7 + disp;
                break;
            }
        }

        // Extract constants
        uint64_t const1 = 0, const2 = 0, const3 = 0;
        for (int i = 0; i < 120; i++) {
            if (code[i] == 0x48 && code[i+1] == 0xB9 && !const1) {
                memcpy(&const1, code + i + 2, 8); i += 9;
            } else if (code[i] == 0x48 && code[i+1] == 0xB8) {
                uint64_t val;
                memcpy(&val, code + i + 2, 8);
                if (!const2) { const2 = val; }
                else if (!const3 && val != const2) { const3 = val; }
                i += 9;
            }
        }

        if (lea_target && const1 && const2 && const3) {
            uint64_t decoded = const1 + ROR64(lea_target, 0x0A);
            decoded ^= const2;
            decoded -= const3;

            printf("[KEY] IDA decoded struct ptr = 0x%llX (RVA 0x%llX)\n",
                   decoded, decoded - g_base);

            if (looks_like_ptr(decoded) || decoded > 0x10000) {
                Key1 = Read<uint64_t>(decoded + 0x38);
                Key2 = Read<uint64_t>(decoded + 0xB8);

                if (Key1 > 0x1000000000000000 && Key2 > 0x1000000000000000) {
                    printf("[KEY] Key1 (from struct +0x38) = 0x%016llX\n", Key1);
                    printf("[KEY] Key2 (from struct +0xB8) = 0x%016llX\n", Key2);
                } else {
                    // Scan for keys near decoded
                    Key1 = Key2 = 0;
                    for (int64_t off = -0x200; off <= 0x200 && (!Key1 || !Key2); off += 8) {
                        uint64_t v = Read<uint64_t>(decoded + off);
                        if (v > 0x1000000000000000) {
                            if (!Key1) Key1 = v;
                            else if (!Key2 && v != Key1) Key2 = v;
                        }
                    }
                    if (Key1 && Key2) {
                        printf("[KEY] Key1 (scanned) = 0x%016llX\n", Key1);
                        printf("[KEY] Key2 (scanned) = 0x%016llX\n", Key2);
                    }
                }
            }

            // Also try: mov [rip+disp], rax pattern (global store)
            if (!Key2) {
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
                        int32_t disp = *(int32_t*)&code[i+3];
                        uint64_t global_rva = (gk_addr + i + 7 + disp) - g_base;
                        uint64_t global_val = Read<uint64_t>(g_base + global_rva);
                        printf("[KEY] Global store at RVA 0x%llX = 0x%016llX\n", global_rva, global_val);
                        if (global_val > 0x1000000000000000) {
                            if (!Key1 || Key1 < 0x1000000000000000) Key1 = global_val;
                            else if (!Key2 && global_val != Key1) Key2 = global_val;
                        }
                    }
                }
            }
        }
    }

    // If Key2 is still missing, scan around Key1's storage location
    if (!Key2) {
        printf("[KEY] Searching for Key2 near global store...\n");
        for (int64_t off = -0x200; off <= 0x200; off += 8) {
            if (off == 0) continue;
            uint64_t val = Read<uint64_t>(g_base + KeyGlobalStore_RVA + off);
            if (val > 0x1000000000000000 && val != key_raw && val != Key1) {
                Key2 = val;
                printf("[KEY] Key2 found at RVA 0x%llX = 0x%016llX\n",
                       KeyGlobalStore_RVA + off, Key2);
                break;
            }
        }
    }

    printf("[KEY] Final: Key1=0x%016llX  Key2=0x%016llX\n", Key1, Key2);
    printf("[KEY] Key1>>0x34=0x%X (table idx %u), Key1&0x7FF=%u\n\n",
           (unsigned)(Key1 >> 0x34), (unsigned)((Key1 >> 0x34) & 0x7FF), (unsigned)(Key1 & 0x7FF));

    if (!Key1) {
        printf("[FAIL] Could not resolve GlobalKey1\n");
        VMMDLL_Close(g_vmm);
        printf("Press Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // -------------------------------------------------------------------------
    // Step 4 — Read entity list
    // -------------------------------------------------------------------------
    printf("[ENTITY] Reading entity list from RVA 0x%X...\n", (unsigned)Address_entity_base);
    uint64_t entity_list_ptr = Read<uint64_t>(g_base + Address_entity_base);
    printf("[ENTITY] Entity list pointer = 0x%016llX (RVA 0x%llX)\n",
           entity_list_ptr, entity_list_ptr - g_base);

    if (!looks_like_ptr(entity_list_ptr)) {
        printf("[FAIL] Entity list pointer invalid\n");
        VMMDLL_Close(g_vmm);
        printf("Press Enter to exit.\n");
        std::getchar();
        return 1;
    }

    // Read up to 4096 entity slots
    EntitySlot raw_list[4096] = {};
    read_buf(entity_list_ptr, (uint8_t*)raw_list, sizeof(raw_list));

    // -------------------------------------------------------------------------
    // Step 5 — Walk entities, decrypt components, identify heroes
    // -------------------------------------------------------------------------
    printf("\n");
    printf("========================================\n");
    printf("  ENTITY SCAN RESULTS\n");
    printf("========================================\n");

    int total_slots = 0;
    int valid_heroes = 0;

    struct HeroInfo {
        int slot;
        uint64_t entity_addr;
        uint64_t hero_id;
        const char* name;
        float health;
        float health_max;
        bool is_local;
    };
    std::vector<HeroInfo> heroes;

    // For local player detection: track entity with PlayerController
    int local_slot = -1;

    for (int i = 0; i < 4096; i++) {
        uint64_t cur_entity = raw_list[i].entity;
        if (!cur_entity) continue;
        total_slots++;

        // GetParent to get the "true" entity address (used in linking)
        uint64_t link_parent = DecryptComponent(cur_entity, TYPE_LINK);
        if (!link_parent) continue;

        // Get HeroID component
        uint64_t hero_base = DecryptComponent(link_parent, TYPE_P_HEROID);
        if (!hero_base) continue;

        uint64_t hero_id = Read<uint64_t>(hero_base + 0xD0);
        const char* name = GetHeroName(hero_id);
        if (!name) continue;

        valid_heroes++;

        // Read health
        float hp = 0, hp_max = 0;
        uint64_t health_base = DecryptComponent(cur_entity, TYPE_HEALTH);
        if (health_base) {
            hp     = Read<float>(health_base + 0xE0);
            hp_max = Read<float>(health_base + 0xDC);
        }

        // Check for PlayerController (local player indicator)
        uint64_t pc_base = DecryptComponent(link_parent, TYPE_PLAYERCONTROLLER);
        bool is_local = (pc_base != 0 && looks_like_ptr(pc_base));

        HeroInfo info;
        info.slot        = i;
        info.entity_addr = cur_entity;
        info.hero_id     = hero_id;
        info.name        = name;
        info.health      = hp;
        info.health_max  = hp_max;
        info.is_local    = is_local;
        heroes.push_back(info);
    }

    // -------------------------------------------------------------------------
    // Step 6 — Print results
    // -------------------------------------------------------------------------
    if (heroes.empty()) {
        printf("\n  No heroes found!\n");
        printf("  Total non-zero entity slots scanned: %d\n", total_slots);
        printf("\n  Possible issues:\n");
        printf("  - Decryption keys are still wrong for current game version\n");
        printf("  - DecryptTable format changed (old: qword array, new: 24-byte structs)\n");
        printf("  - Entity list structure changed\n");
    } else {
        printf("  %-4s %-22s %-8s %-8s %-7s %s\n",
               "Slot", "Hero", "HP", "MaxHP", "Local?", "HeroID");
        printf("  ---- ---------------------- -------- -------- ------- ------------------\n");

        for (auto& h : heroes) {
            printf("  %-4d %-22s %-8.0f %-8.0f %-7s 0x%016llX\n",
                   h.slot, h.name, h.health, h.health_max,
                   h.is_local ? "**YES**" : "no",
                   h.hero_id);
        }

        printf("\n  Total non-zero entity slots: %d\n", total_slots);
        printf("  Valid heroes identified:     %d\n", valid_heroes);

        // Identify local player
        int local_count = 0;
        for (auto& h : heroes) {
            if (h.is_local) {
                if (local_count == 0)
                    printf("\n  >>> LOCAL PLAYER: %s (slot %d, HP %.0f/%.0f)\n",
                           h.name, h.slot, h.health, h.health_max);
                local_count++;
            }
        }
        if (local_count == 0) {
            printf("\n  >>> Could not identify local player (no entity had PlayerController)\n");
        } else if (local_count > 1) {
            printf("  >>> WARNING: %d entities have PlayerController — ambiguous\n", local_count);
        }
    }

    printf("\n========================================\n");

    // -------------------------------------------------------------------------
    // Cleanup
    // -------------------------------------------------------------------------
    VMMDLL_Close(g_vmm);
    printf("\nDone. Press Enter to exit.\n");
    std::getchar();
    return 0;
}
