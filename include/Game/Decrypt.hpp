#pragma once
#include <Windows.h>
#include <intrin.h>
#include <intsafe.h>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <emmintrin.h>

// Fallback for modern Windows SDK where HIDWORD/LODWORD are removed
#ifndef HIDWORD
#define HIDWORD(_ui64) ((DWORD)(((DWORDLONG)(_ui64) >> 32) & 0xFFFFFFFF))
#endif
#ifndef LODWORD
#define LODWORD(_ui64) ((DWORD)((_ui64) & 0xFFFFFFFF))
#endif
#ifndef __ROL4__
#define __ROL4__(x, n) _rotl(x, n)
#endif

#include "Game/Structs.hpp"
#include "Game/Offsets.hpp"
#include "Game/SDK.hpp"

namespace OW {

    // =========================================================================
    // Global key management
    // =========================================================================

    static inline uint64_t ROR64(uint64_t x, int bits) {
        bits &= 63;
        if (bits == 0) return x;
        return (x >> bits) | (x << (64 - bits));
    }

    inline bool GetGlobalKey() {
        // ---- Method 1: Pattern scan (RIGEL-2411 key_sig, may not exist in May2026) ----
        static const uint8_t key_sig[] =
            "\x00\x00\x00\x00\x21\x00\x00\x00\x00\x00\x00\x00\x24\x00\x00\x00"
            "\x01\x00\x00\x00\x29\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00\x00\x00\x00\x00\x00";
        static const char* key_mask = "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxxx";

        int pattern_attempts = 0;
        while (true) {
            // --- Try pattern scan first (3 attempts, then fall back to IDA method) ---
            if (pattern_attempts < 3) {
                uint64_t Key = SDK->FindPatternExReg(
                    reinterpret_cast<const uint8_t*>(key_sig),
                    key_mask,
                    0x100000
                );
                if (Key && Key < 0xF000000000000000 &&
                    SDK->RPM<uint64_t>(Key + 0x38) > 0x1000000000000000 &&
                    SDK->RPM<uint64_t>(Key + 0xB8) > 0x1000000000000000) {
                    SDK->GlobalKey1 = SDK->RPM<uint64_t>(Key + 0x38);
                    SDK->GlobalKey2 = SDK->RPM<uint64_t>(Key + 0xB8);
                    printf("[Decrypt] GlobalKey1: 0x%llX\n", SDK->GlobalKey1);
                    printf("[Decrypt] GlobalKey2: 0x%llX\n", SDK->GlobalKey2);
                    return true;
                }
                pattern_attempts++;
            }

            // ---- Method 2: IDA-derived direct decode (May 2026 GetGlobalKey at RVA 0x581D20) ----
            // The function computes an obfuscated pointer via:
            //   lea rax, [rip+disp]          -> base address
            //   ror rax, 0x0A                -> rotate right by 10
            //   mov rcx, const1; add rcx, rax
            //   mov rax, const2; xor rcx, rax
            //   mov rax, const3; sub rcx, rax
            //   mov [rsp+0x30], rcx          -> decoded pointer stored on stack
            // Then reads the key structure at decoded+0x38 (Key1) and decoded+0xB8 (Key2)
            // OR stores to a global: mov [rip+disp], rax (computed key value)
            {
                uint64_t gk_addr = SDK->dwGameBase + offset::GetGlobalKey_RVA;
                uint8_t code[128] = {};
                SDK->read_buf(gk_addr, (char*)code, sizeof(code));

                // Find LEA instruction: 48 8D 05/0D/15/1D/25/2D/35/3D disp32
                uint64_t lea_target = 0;
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0x8D &&
                        (code[i+2] & 0xC7) == 0x05) {
                        int32_t disp = *(int32_t*)&code[i+3];
                        lea_target = gk_addr + i + 7 + disp;
                        break;
                    }
                }

                // Extract obfuscation constants: mov rcx,imm64 (48 B9) and mov rax,imm64 (48 B8)
                uint64_t const1 = 0, const2 = 0, const3 = 0;
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0xB9 && !const1) {
                        memcpy(&const1, code + i + 2, 8);
                        i += 9;
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

                    printf("[Decrypt] IDA method: decoded struct ptr = 0x%llX (RVA 0x%llX)\n",
                           decoded, decoded - SDK->dwGameBase);

                    // Check if decoded points within game memory
                    bool decoded_in_range = (decoded >= SDK->dwGameBase &&
                                             decoded < SDK->dwGameBase + 0x4000000);

                    // Try reading keys from decoded structure at +0x38 and +0xB8
                    if (decoded_in_range || decoded > 0x10000) {
                        uint64_t k1 = SDK->RPM<uint64_t>(decoded + 0x38);
                        uint64_t k2 = SDK->RPM<uint64_t>(decoded + 0xB8);

                        if (k1 > 0x1000000000000000 && k2 > 0x1000000000000000) {
                            SDK->GlobalKey1 = k1;
                            SDK->GlobalKey2 = k2;
                            printf("[Decrypt] GlobalKey1: 0x%llX (from decoded struct)\n", k1);
                            printf("[Decrypt] GlobalKey2: 0x%llX (from decoded struct)\n", k2);
                            return true;
                        }

                        // If not at +0x38/+0xB8, scan the decoded structure for key-like values
                        int found = 0;
                        for (int64_t off = -0x200; off <= 0x200 && found < 2; off += 8) {
                            uint64_t v = SDK->RPM<uint64_t>(decoded + off);
                            if (v > 0x1000000000000000) {
                                if (!SDK->GlobalKey1) { SDK->GlobalKey1 = v; found++; }
                                else if (!SDK->GlobalKey2 && v != SDK->GlobalKey1) { SDK->GlobalKey2 = v; found++; }
                            }
                        }
                        if (found >= 2) {
                            printf("[Decrypt] GlobalKey1: 0x%llX (scanned struct)\n", SDK->GlobalKey1);
                            printf("[Decrypt] GlobalKey2: 0x%llX (scanned struct)\n", SDK->GlobalKey2);
                            return true;
                        }
                        SDK->GlobalKey1 = SDK->GlobalKey2 = 0; // reset
                    }

                    // Try: the global storage approach (GetGlobalKey stores to a global var)
                    // The function code contains: mov [rip+disp], rax  (48 89 05 disp32)
                    for (int i = 0; i < 120; i++) {
                        if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
                            int32_t disp = *(int32_t*)&code[i+3];
                            uint64_t global_rva = (gk_addr + i + 7 + disp) - SDK->dwGameBase;
                            uint64_t global_val = SDK->RPM<uint64_t>(SDK->dwGameBase + global_rva);
                            printf("[Decrypt] Global store at RVA 0x%llX = 0x%llX\n",
                                   global_rva, global_val);

                            if (global_val > 0x1000000000000000) {
                                // May be XOR'd with 0xF5 or similar small constant
                                SDK->GlobalKey1 = global_val;
                                // Search nearby for Key2
                                for (int64_t d = -0x1000; d <= 0x1000; d += 8) {
                                    if (d == 0) continue;
                                    uint64_t v = SDK->RPM<uint64_t>(SDK->dwGameBase + global_rva + d);
                                    if (v > 0x1000000000000000 && v != global_val) {
                                        SDK->GlobalKey2 = v;
                                        printf("[Decrypt] GlobalKey1: 0x%llX (global store)\n", SDK->GlobalKey1);
                                        printf("[Decrypt] GlobalKey2: 0x%llX (near global)\n", SDK->GlobalKey2);
                                        return true;
                                    }
                                }
                            }
                        }
                    }
                }
                SDK->GlobalKey1 = SDK->GlobalKey2 = 0;
            }

            printf("[Decrypt] Key resolution failed, retrying in 2s...\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

    // =========================================================================
    // Parent decryption helper
    // =========================================================================

    // May 2026: NEW GetParent constants (verified from IDA dump RVA 0x56B4C0)
    // Formula: result = ROR([parent+0x30], 32) ^ 0x4B920A7072A077C5
    //                  - 0x107816B001CA79C8
    //                  = ROR(result, 35) + 0xFD2150D0AEF24514
    // Old (RIGEL-2411): result -= 0x401C60913E3B91CE; ROR32

    inline uint64_t GetParent(uint64_t encrypted) {
        __try {
            auto result = encrypted;
            // New decryption chain (verified May 2026)
            result = (result >> 0x20) | (result << 0x20);  // ROR64 by 32
            result ^= 0x4B920A7072A077C5;
            result -= 0x107816B001CA79C8;
            result = (result >> 0x23) | (result << 0x1D);  // ROR64 by 35
            result += 0xFD2150D0AEF24514;
            return result;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // =========================================================================
    // Component decryption
    // =========================================================================

    inline void sub_E8D1A0(uint64_t* bit_mask, uint64_t* lower_mask,
                           uint32_t* shift, uint32_t* bucket,
                           uint8_t componentid) {
        *shift = componentid & 0x3F;
        *bit_mask = 1ull << *shift;
        *lower_mask = *bit_mask - 1;
        *bucket = componentid / 0x3F;
    }

    /*
        May 2026 real component decode sequence.

        Evidence:
          - Inlined at RVA 0x563700 inside sub_7FF6A80B3200.
          - Duplicate sequence at RVA 0x1F93DAD.
          - The old 0x666016 site is not component decrypt; it is an IEEE-754
            exponent extraction path.

        Key material used by the real sequence:
          - qword ptr [base + 0x3A86E30] + 0x10C
          - byte  ptr [base + 0x3772769]

        Constants and final transform:
          + 0x4C8675CDE55BA1B2
          + 0x7BE57670994040F6
          ^ 0x3864150DB528414C
          ^ 0xA4764E53CD34159B
          ROL64(value, 0x2A), then ROR64(..., 0x2D)
    */
    inline uintptr_t DecryptComponent(uintptr_t parent, uint8_t idx) {
        uint64_t bit_mask = 0;
        uint64_t lower_mask = 0;
        uint32_t shift = 0;
        uint32_t bucket = 0;
        sub_E8D1A0(&bit_mask, &lower_mask, &shift, &bucket, idx);

        const uint64_t component_bits =
            SDK->RPM<uint64_t>(parent + 8ull * bucket + 0x110);
        const uint64_t present = (component_bits & bit_mask) >> shift;

        uint64_t below = component_bits & lower_mask;
        below -= (below >> 1) & 0x5555555555555555ull;
        below = (below & 0x3333333333333333ull) +
                ((below >> 2) & 0x3333333333333333ull);
        below = (below + (below >> 4)) & 0x0F0F0F0F0F0F0F0Full;

        const uint64_t component_index =
            SDK->RPM<uint8_t>(parent + bucket + 0x130) +
            ((below * 0x0101010101010101ull) >> 0x38);

        const uint64_t component_table = SDK->RPM<uint64_t>(parent + 0x80);
        uint64_t component =
            SDK->RPM<uint64_t>(component_table + 8ull * component_index);

        component += 0x4C8675CDE55BA1B2ull;

        const uint64_t component_key_source =
            SDK->RPM<uint64_t>(SDK->dwGameBase + 0x3A86E30);
        const uint64_t component_key_material_1 =
            SDK->RPM<uint64_t>(component_key_source + 0x10C);
        component ^= component_key_material_1;

        component += 0x7BE57670994040F6ull;

        const uint8_t component_key_byte =
            SDK->RPM<uint8_t>(SDK->dwGameBase + 0x3772769);
        component ^= static_cast<uint64_t>(component_key_byte);
        component ^= 0x3864150DB528414Cull;
        component ^= 0xA4764E53CD34159Bull;

        component = (component << 0x2A) | (component >> 0x16);
        component = ROR64(component, 0x2D);

        const uint64_t present_mask =
            static_cast<uint64_t>(static_cast<int64_t>(
                -static_cast<int32_t>(present)));
        return static_cast<uintptr_t>(present_mask & component);
    }

    // =========================================================================
    // Visibility decryption — May 2026 (UC p330, snowancestor 2026-05-25)
    //
    // Replaces the old table-walk approach that used DEAD VisFN/Vis_Key.
    // New chain reads from VisBase+0x98 and uses the same ComponentXorQword
    // and ComponentXorByte sources as DecryptComponent.
    // =========================================================================

    inline uint64_t DecryptVis(uint64_t visBase) {
        // Step 1: read encrypted qword from VisBase+0x98, ROR3, XOR constant
        uint64_t enc = SDK->RPM<uint64_t>(visBase + 0x98);
        enc = ROR64(enc, 3) ^ 0x53DB07B6B873760Cull;

        // Step 2: load ComponentXorByte (u8 at base+RVA)
        uint8_t var_byte = SDK->RPM<uint8_t>(SDK->dwGameBase + offset::ComponentXorByte_RVA);

        // Step 3: load ComponentXorQword ptr (the pointer, not the +0x10C value)
        uint64_t var_qword = SDK->RPM<uint64_t>(SDK->dwGameBase + offset::ComponentXorQword_RVA);

        // Step 4: main transform
        uint64_t dec = (var_byte ^ (enc - 0x7A7DB4DE6CD03BBCull)) + 0x5CE60F50EA1D337Full;

        // Step 5: mix with qword data at var_qword+0x6A
        dec = SDK->RPM<uint64_t>(var_qword + 0x6A) ^ ((2 * dec) | (dec >> 0x3F));

        // Step 6: final rotate
        dec = ROR64(dec + 0x78D75198F1D34D38ull, 0xC);
        return dec;
    }

    // =========================================================================
    // Heap manager helpers
    // =========================================================================

    inline uintptr_t GetHeapManager(uint8_t index) {
        uintptr_t v0 = SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::HeapManager);
        if (v0 != 0) {
            auto v1 = SDK->RPM<uintptr_t>(v0 + offset::HeapManager_Pointer) ^
                     (SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::HeapManager_Var) + offset::HeapManager_Key);
            if (v1 != 0) {
                return SDK->RPM<uintptr_t>(v1 + 0x8 * index);
            }
        }
        return 0;
    }

    inline uintptr_t GetSenstivePTR() {
        uintptr_t heap = GetHeapManager(6);
        if (heap) {
            return heap + offset::SensitivePtr;
        }
        return 0;
    }

    // =========================================================================
    // Outline — REMOVED 2026-05-25
    //
    // DMA external cheats CANNOT render outlines on the host machine, and
    // outlines must NEVER be drawn on the host.  This is a fundamental
    // limitation of the DMA architecture — the FPGA reads memory but has no
    // path to inject D3D11 draw calls into the game's rendering pipeline.
    // Outline decryption (GetOutlineStruct, DecryptOutline, SetBorderLine)
    // has been deleted.  If needed later for read-only analysis, recover
    // from git history or the UC audit at uc/0525/viewmatrix_audit.md.
    // =========================================================================

    // =========================================================================
    // Entity list scanning
    // =========================================================================

    inline std::vector<std::pair<uint64_t, uint64_t>> get_ow_entities() {
        std::vector<std::pair<uint64_t, uint64_t>> result;

        struct Entity {
            uint64_t entity;
            uint64_t pad;
        };

        uint64_t entity_list = SDK->RPM<uint64_t>(SDK->dwGameBase + offset::Address_entity_base);
        if (!entity_list) return result;

        // DMA-based: read entity list (64 KB max)
        Entity raw_list[4096] = {};

        if (!mem.Read(entity_list, raw_list, sizeof(raw_list)))
            return result;

        size_t entity_list_size = sizeof(raw_list) / sizeof(Entity);

        for (size_t i = 0; i < entity_list_size; ++i) {
            uint64_t cur_entity = raw_list[i].entity;
            if (!cur_entity) continue;

            uint64_t common_linker = DecryptComponent(cur_entity, TYPE_LINK);
            if (!common_linker) continue;

            uint32_t unique_id = SDK->RPM<uint32_t>(common_linker + 0xD4);

            for (size_t x = 0; x < entity_list_size; ++x) {
                uint64_t possible_common = raw_list[x].entity;
                if (!possible_common) continue;

                if (SDK->RPM<uint32_t>(possible_common + 0x138) == unique_id) {
                    result.emplace_back(possible_common, cur_entity);
                    break;
                } else {
                    uint64_t Ptr = SDK->RPM<uint64_t>(possible_common + 0x30) & 0xFFFFFFFFFFFFFFC0;
                    if (Ptr < 0xFFFFFFFFFFFFFFEF) {
                        uint64_t EntityID = SDK->RPM<uint64_t>(Ptr + 0x10);
                        if (EntityID == 0x400000000000060 ||
                            EntityID == 0x40000000000480A ||
                            EntityID == 0x40000000000005F ||
                            EntityID == 0x400000000002533) {
                            result.emplace_back(possible_common, cur_entity);
                        }
                    }
                }
            }
        }
        return result;
    }

    // =========================================================================
    // Hero name helpers
    // =========================================================================

    inline std::string GetHeroEngNames(uint64_t HeroID, uint64_t LinkBase) {
        switch (HeroID) {
            case eHero::HERO_REAPER:       return "Reaper";
            case eHero::HERO_TRACER:       return "Tracer";
            case eHero::HERO_MERCY:        return "Mercy";
            case eHero::HERO_HANJO:        return "Hanzo";
            case eHero::HERO_TORBJORN:     return "Torbjorn";
            case eHero::HERO_REINHARDT:    return "Reinhardt";
            case eHero::HERO_PHARAH:       return "Pharah";
            case eHero::HERO_WINSTON:      return "Winston";
            case eHero::HERO_WIDOWMAKER:   return "Widowmaker";
            case eHero::HERO_BASTION:      return "Bastion";
            case eHero::HERO_SYMMETRA:     return "Symmetra";
            case eHero::HERO_ZENYATTA:     return "Zenyatta";
            case eHero::HERO_GENJI:        return "Genji";
            case eHero::HERO_ROADHOG:      return "Roadhog";
            case eHero::HERO_MCCREE:       return "McCree";
            case eHero::HERO_JUNKRAT:      return "Junkrat";
            case eHero::HERO_ZARYA:        return "Zarya";
            case eHero::HERO_SOLDIER76:    return "Soldier 76";
            case eHero::HERO_LUCIO:        return "Lucio";
            case eHero::HERO_DVA:
                if (SDK->RPM<uint16_t>(LinkBase + 0xD4) != SDK->RPM<uint16_t>(LinkBase + 0xD8))
                    return "D.Va";
                else
                    return "Hana";
            case eHero::HERO_MEI:          return "Mei";
            case eHero::HERO_ANA:          return "Ana";
            case eHero::HERO_SOMBRA:       return "Sombra";
            case eHero::HERO_ORISA:        return "Orisa";
            case eHero::HERO_DOOMFIST:     return "Doomfist";
            case eHero::HERO_MOIRA:        return "Moira";
            case eHero::HERO_BRIGITTE:     return "Brigitte";
            case eHero::HERO_WRECKINGBALL: return "Wrecking Ball";
            case eHero::HERO_SOJOURN:      return "Sojourn";
            case eHero::HERO_ASHE:         return "Ashe";
            case eHero::HERO_BAPTISTE:     return "Baptiste";
            case eHero::HERO_KIRIKO:       return "Kiriko";
            case eHero::HERO_JUNKERQUEEN:  return "Junker Queen";
            case eHero::HERO_SIGMA:        return "Sigma";
            case eHero::HERO_ECHO:         return "Echo";
            case eHero::HERO_RAMATTRA:     return "Ramattra";
            case eHero::HERO_TRAININGBOT1: return "Standard Bot";
            case eHero::HERO_TRAININGBOT2: return "Tank Bot";
            case eHero::HERO_TRAININGBOT3: return "Sniper Bot";
            case eHero::HERO_TRAININGBOT4: return "Friend Bot";
            case eHero::HERO_TRAININGBOT5: return "Friend Tank Bot";
            case eHero::HERO_TRAININGBOT6: return "Rocket Bot";
            case eHero::HERO_TRAININGBOT7: return "Training Bot";
            case eHero::HERO_LIFEWEAVER:   return "LifeWeaver";
            case eHero::HERO_ILLARI:       return "Illari";
            case eHero::HERO_MAUGA:        return "Mauga";
            case eHero::HERO_VENTURE:      return "Venture";
            case eHero::TOBTERT:           return "Tob";
            case eHero::SYMTERT:           return "Sym";
            case eHero::Bob:               return "Bob";
            default:                       return "Unknown";
        }
    }

    // =========================================================================
    // Skill system helpers
    // =========================================================================

    inline bool IsSkillActive(uint64_t base, uint16_t index, uint16_t id) {
        if (id == 0) return false;
        uint64_t skillList = 0;

        if (index == 0) {
            skillList = base + 0x570;
        } else {
            uint32_t count = SDK->RPM<uint32_t>(base + 0x378);
            if (count <= 0 || count >= 0xFF) return false;
            uint64_t entry = SDK->RPM<uint64_t>(base + 0x370);
            if (!entry) return false;
            for (uint32_t i = 0; i < count; i++, entry += 0x10) {
                if (SDK->RPM<uint16_t>(entry + 0x8) == index) {
                    uint64_t listStruct = SDK->RPM<uint64_t>(entry);
                    if (!listStruct) return false;
                    skillList = SDK->RPM<uint64_t>(listStruct + 0xA8);
                    break;
                }
            }
        }
        if (!skillList) return false;

        uint64_t listEntry = skillList + 0x20 * ((id & 0xF) + 1);
        uint64_t structList = SDK->RPM<uint64_t>(listEntry);
        if (!structList) return false;

        int32_t listIndex = (index == 0) ? 0 : SDK->RPM<int32_t>(listEntry + 0x8) - 1;
        if (listIndex < 0 || listIndex >= 0xFF) return false;

        uint64_t skillEntry = structList + 0x10 * listIndex;
        if (SDK->RPM<uint16_t>(skillEntry) == id) {
            uint64_t skill = SDK->RPM<uint64_t>(skillEntry + 0x8);
            if (!skill) return false;
            return SDK->RPM<uint8_t>(skill + 0x48) == 1;
        }
        return false;
    }

    inline uintptr_t SkillStructCheck(uint64_t a1, uint16_t a2) {
        if (SDK->RPM<uint32_t>(a1 + 0x2A8) <= 0) return 0;
        uint64_t v2 = 0;
        uint64_t i = SDK->RPM<uint64_t>(a1 + 0x2A0);
        for (; SDK->RPM<uint16_t>(i + 8) != a2; i += 16) {
            if (++v2 >= SDK->RPM<uint32_t>(a1 + 0x2A8)) return 0;
        }
        return SDK->RPM<uint64_t>(i);
    }

    inline uint64_t FnSkillStruct(__m128* a1, uint16_t* a2) {
        uint64_t v2 = 0;
        uint16_t* v3 = a2;
        if (!a2[1]) return 0;
        uint16_t v4 = *a2;
        uint64_t v5 = a1->m128_i64[1];
        uint64_t v6 = 0;
        if (!v4) {
            v6 = v5 + 0x4A0;
            goto LABEL_6;
        }
        {
            uint64_t v7 = SkillStructCheck(v5, v4);
            if (!v7) return 0;
            v6 = SDK->RPM<uint64_t>(v7 + 0xB8);
        }
    LABEL_6:
        uint16_t v8 = v3[1];
        uint64_t v9 = 32 * ((v3[1] & 0xF) + 1);
        int32_t v10 = SDK->RPM<uint32_t>(v9 + v6 + 8) - 1;
        if (v10 < 0) return 0;
        uint64_t v11 = v10;
        uint64_t v12 = SDK->RPM<uint64_t>(v9 + v6) + 16 * v10;
        while (SDK->RPM<uint16_t>(v12) != v8) {
            v12 -= 16;
            if (--v11 < 0) return 0;
        }
        uint64_t v13 = SDK->RPM<uint64_t>(v12 + 8);
        if (!v13) return 0;
        if (*((uint32_t*)v3 + 4) <= 0) return v13;
        return v13;
    }

    inline bool IsSkillActivate1(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return false;
        return SDK->RPM<uint8_t>(skill + 0x48) == 1;
    }

    inline float GetSkill(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2, uint16_t offset) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return 0.f;
        return SDK->RPM<float>(skill + offset);
    }

    inline float readskillcd(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return false;
        float ret = SDK->RPM<float>(skill + 0x48);
        if (!ret) return ret;
        ret = SDK->RPM<float>(skill + 0x60);
        return (ret != 0) ? ret : 1.f;
    }

    inline float readult(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return false;
        return SDK->RPM<float>(skill + 0x60);
    }

} // namespace OW
