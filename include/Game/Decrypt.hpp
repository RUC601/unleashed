#pragma once
#include <Windows.h>
#include <intrin.h>
#include <intsafe.h>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>
#include <type_traits>
#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
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
#include "Utils/Diagnostics.hpp"

namespace OW {

    // =========================================================================
    // Global key management
    // =========================================================================

    static inline uint64_t ROR64(uint64_t x, int bits) {
        bits &= 63;
        if (bits == 0) return x;
        return (x >> bits) | (x << (64 - bits));
    }

    static inline uint64_t ROL64(uint64_t x, int bits) {
        bits &= 63;
        if (bits == 0) return x;
        return (x << bits) | (x >> (64 - bits));
    }

    /**
     * Resolve the older global key pair used by legacy probes.
     *
     * Current May 2026 component and visibility decrypt paths read their key
     * material directly from ComponentXorQword/ComponentXorByte, so startup no
     * longer depends on this function. It is retained as an optional diagnostic
     * helper: first try the old RIGEL pattern, then decode the IDA-derived
     * GetGlobalKey function at offset::GetGlobalKey_RVA by extracting the LEA,
     * MOV-immediate constants, and RIP-relative global store from live code.
     */
    inline bool GetGlobalKey() {
        Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Resolving);
        Diagnostics::Info("GetGlobalKey resolution started.");

        auto markResolved = [](const char* method) {
            Diagnostics::SetKeyStatus(
                Diagnostics::KeyStatus::Resolved,
                SDK->GlobalKey1,
                SDK->GlobalKey2);
            Diagnostics::Info("GetGlobalKey succeeded via %s: key1=0x%llX key2=0x%llX",
                method,
                static_cast<unsigned long long>(SDK->GlobalKey1),
                static_cast<unsigned long long>(SDK->GlobalKey2));
            return true;
        };

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
                    return markResolved("pattern scan");
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
                            return markResolved("decoded struct");
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
                            return markResolved("struct scan");
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
                                        return markResolved("global store");
                                    }
                                }
                            }
                        }
                    }
                }
                SDK->GlobalKey1 = SDK->GlobalKey2 = 0;
            }

            Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Failed);
            Diagnostics::Warn("GetGlobalKey resolution attempt failed; retrying in 2s.");
            printf("[Decrypt] Key resolution failed, retrying in 2s...\n");
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        }
    }

    // =========================================================================
    // Parent decryption helper
    // =========================================================================

    /**
     * Decode an encrypted parent/entity pointer.
     *
     * The caller passes the qword already read from the parent field (commonly
     * parent+0x30 in the entity relation chain). The May 2026 chain rotates the
     * value right by 32, XORs/subtracts the two current constants, rotates right
     * by 35, then adds the final bias. The result is the resolved parent pointer.
     */
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
        *bucket = componentid >> 6;
    }

    static constexpr uint64_t kEntityHeaderSnapshotOffset = 0x30;
    static constexpr size_t kEntityHeaderSnapshotSize = 0x120;

    struct EntityHeaderSnapshot {
        uintptr_t parent = 0;
        MemorySDK::ReadRange range{};
        bool valid = false;

        bool Read(uintptr_t value) {
            parent = value;
            valid = parent != 0 &&
                range.Read(parent + kEntityHeaderSnapshotOffset,
                           kEntityHeaderSnapshotSize);
            return valid;
        }

        template <typename T>
        bool ReadParentOffset(uint64_t offset, T& value) const {
            if (!valid) {
                value = {};
                return false;
            }
            return range.ReadAddress(parent + offset, value);
        }
    };

    /**
     * Resolve a component pointer from a component parent and component id.
     *
     * Flow:
     *  1. Split the component id into a 64-bit bitmap bucket and bit position.
     *  2. Read the presence bitmap at parent+0x110+(bucket*8). If the bit is
     *     clear, the component is absent and the function returns 0.
     *  3. Popcount bits below the requested bit and add the per-bucket base byte
     *     at parent+0x130+bucket to get the component-table index.
     *  4. Read parent+0x80 as the component table, then read the encrypted qword.
     *  5. Apply the UC p331 / IDA 0527 component transform:
     *     XOR key material, XOR fixed constant, ROR32, ADD, XOR byte key,
     *     SUB fixed constant, ROR60, then ROR57.
     *  6. Mask the decoded pointer with the presence bit and return it.
     */
    inline uintptr_t DecryptComponent(uintptr_t parent, uint8_t idx,
                                      const EntityHeaderSnapshot* parent_snapshot) {
        if (!parent)
            return 0;

        uint64_t bit_mask = 0;
        uint64_t lower_mask = 0;
        uint32_t shift = 0;
        uint32_t bucket = 0;
        sub_E8D1A0(&bit_mask, &lower_mask, &shift, &bucket, idx);

        auto readParent = [&](uint64_t offset, auto& value) {
            if (parent_snapshot &&
                parent_snapshot->parent == parent &&
                parent_snapshot->ReadParentOffset(offset, value)) {
                return;
            }
            value = SDK->RPM<std::remove_reference_t<decltype(value)>>(parent + offset);
        };

        uint64_t component_bits = 0;
        readParent(8ull * bucket + 0x110, component_bits);
        const uint64_t present = (component_bits & bit_mask) >> shift;
        if (!present)
            return 0;

        uint64_t below = component_bits & lower_mask;
        below -= (below >> 1) & 0x5555555555555555ull;
        below = (below & 0x3333333333333333ull) +
                ((below >> 2) & 0x3333333333333333ull);
        below = (below + (below >> 4)) & 0x0F0F0F0F0F0F0F0Full;

        uint8_t component_index_base = 0;
        readParent(bucket + 0x130, component_index_base);

        const uint64_t component_index =
            component_index_base +
            ((below * 0x0101010101010101ull) >> 0x38);

        uint64_t component_table = 0;
        readParent(0x80, component_table);
        if (!component_table) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        uint64_t component =
            SDK->RPM<uint64_t>(component_table + 8ull * component_index);
        if (!component) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        uint64_t component_key_source = 0;
        uint64_t component_key_material_1 = 0;
        uint8_t component_key_byte = 0;
        if (!SDK->GetCachedComponentKeyMaterial(
                SDK->dwGameBase + offset::ComponentXorQword_RVA,
                offset::ComponentXorQword_Off,
                SDK->dwGameBase + offset::ComponentXorByte_RVA,
                component_key_source,
                component_key_material_1,
                component_key_byte)) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

#if 0
        // 0521: old add/xor/add/xor/xor/xor/net-ROR3 tail retained for audit.
        component += 0x4C8675CDE55BA1B2;
        component ^= component_key_material_1;
        component += 0x7BE57670994040F6;
        component ^= static_cast<uint64_t>(component_key_byte);
        component ^= 0x3864150DB528414C;
        component ^= 0xA4764E53CD34159B;
        component = (component << 0x2A) | (component >> 0x16);
        component = ROR64(component, 0x2D);
#endif

        component ^= component_key_material_1;
        component ^= offset::Component_Xor1;
        component = ROR64(component, offset::Component_Ror1);
        component += offset::Component_Add1;
        component ^= static_cast<uint64_t>(component_key_byte);
        component -= offset::Component_Sub1;
        component = ROR64(component, offset::Component_Ror2);
        component = ROR64(component, offset::Component_Ror3);

        const uint64_t present_mask =
            static_cast<uint64_t>(static_cast<int64_t>(
                -static_cast<int32_t>(present)));
        const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
        if (!decoded)
            Diagnostics::RecordDecryptFailure();
        return decoded;
    }

    inline uintptr_t DecryptComponent(uintptr_t parent, uint8_t idx) {
        EntityHeaderSnapshot snapshot{};
        const EntityHeaderSnapshot* active_snapshot =
            snapshot.Read(parent) ? &snapshot : nullptr;
        return DecryptComponent(parent, idx, active_snapshot);
    }

    // =========================================================================
    // Visibility decryption - 2026-05-27 (UC p331 + IDA sub_7FF7BD68C880)
    //
    // Replaces the old table-walk approach that used DEAD VisFN/Vis_Key.
    // New chain reads from VisBase+0x2D8, uses key material at
    // RPM(base+0x3A92E70)+0x16A, and finishes with a magic byte from .data.
    // =========================================================================

    /**
     * Decode the visibility flag from a visibility component.
     *
     * This replaces the old VisFN/Vis_Key table walk and the 0521 p330
     * VisBase+0x98 helper. The decoded value is expected to be non-zero for
     * visible and zero for occluded.
     */
    inline uint64_t DecryptVis(uint64_t visBase) {
        uint64_t value = SDK->RPM<uint64_t>(visBase + offset::VisibilityValueOffset);

#if 0
        // 0521: old p330 VisBase+0x98 chain retained for audit.
        uint64_t enc = SDK->RPM<uint64_t>(visBase + 0x98);
        enc = ROR64(enc, 3) ^ 0x53DB07B6B873760C;
        uint64_t var_qword = 0;
        uint64_t unused_material = 0;
        uint8_t var_byte = 0;
        SDK->GetCachedComponentKeyMaterial(
            SDK->dwGameBase + 0x3A86E30,
            0x10C,
            SDK->dwGameBase + 0x3772769,
            var_qword,
            unused_material,
            var_byte);
        uint64_t dec = (var_byte ^ (enc - 0x7A7DB4DE6CD03BBC)) + 0x5CE60F50EA1D337F;
        dec = ROR64(dec + 0x78D75198F1D34D38, 0xC);
        dec = SDK->RPM<uint64_t>(var_qword + 0x6A) ^ ((2 * dec) | (dec >> 0x3F));
        return dec;
#endif

        value += offset::Visibility_Add1;
        value = ROR64(value, offset::Visibility_Ror1);
        value += offset::Visibility_Add2;
        value = ROR64(value, offset::Visibility_Ror2);

        const uint64_t key_ptr =
            SDK->RPM<uint64_t>(SDK->dwGameBase + offset::VisibilityGlobalKeyPtr_RVA);
        if (!key_ptr) {
            Diagnostics::RecordDecryptFailure();
            return 0;
        }

        value ^= SDK->RPM<uint64_t>(key_ptr + offset::VisibilityQwordOffset);
        value = ROL64(value, offset::Visibility_Rol1);
        value ^= offset::Visibility_Xor1;
        value -= offset::Visibility_Sub1;

        const uint64_t magic =
            static_cast<uint64_t>(
                SDK->RPM<uint8_t>(SDK->dwGameBase + offset::VisibilityMagicByte_RVA));
        return magic ^ ROR64(value, offset::Visibility_FinalRor);
    }

    // =========================================================================
    // GameAdmin / input helpers
    // =========================================================================

    inline uintptr_t GetHeapManager(uint8_t index) {
        if (!SDK->dwGameBase)
            return 0;

        const uintptr_t root =
            SDK->RPM<uintptr_t>(SDK->dwGameBase + offset::Address_game_admin_root);
        if (!root)
            return 0;

        const uint64_t enc = SDK->RPM<uint64_t>(root + offset::GameAdmin_RootPtr);
        if (!enc)
            return 0;

        uint64_t slot_table = enc + offset::GameAdmin_Add1;
        slot_table ^= offset::GameAdmin_Xor1;
        slot_table = ROR64(slot_table, offset::GameAdmin_Ror1);
        slot_table += offset::GameAdmin_Add2;
        slot_table = ROR64(slot_table, offset::GameAdmin_Ror2);
        if (!slot_table)
            return 0;

        return SDK->RPM<uintptr_t>(slot_table + 8ull * index);
    }

    inline bool IsPlausibleSensitivity(float value) {
        return value > 0.0f && value < 100.0f;
    }

    inline uintptr_t GetSenstivePTR() {
        if (!SDK->dwGameBase)
            return 0;

        static uintptr_t cached = 0;
        if (cached)
            return cached;

        const uintptr_t mouse_scale_x =
            SDK->dwGameBase + offset::InputMouseScaleX_RVA;
        if (IsPlausibleSensitivity(SDK->RPM<float>(mouse_scale_x))) {
            cached = mouse_scale_x;
            return cached;
        }

        // Last-resort slot-relative probe for older dumps. Current IDA evidence
        // prefers the input.MouseScaleX/Y globals above.
        const uintptr_t input_system =
            GetHeapManager(offset::HeapSlotIndex_InputSystem);
        if (input_system) {
            const uintptr_t legacy_ptr = input_system + offset::SensitivePtr;
            if (IsPlausibleSensitivity(SDK->RPM<float>(legacy_ptr))) {
                cached = legacy_ptr;
                return cached;
            }
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
        SDK->BeginFrame();

        std::vector<std::pair<uint64_t, uint64_t>> result;

        struct Entity {
            uint64_t entity;
            uint64_t pad;
        };

        struct EntityScanRecord {
            uint64_t entity = 0;
            uint32_t unique_id = 0;
            uint64_t ptr = 0;
            uint64_t entity_id = 0;
            EntityHeaderSnapshot header{};
        };

        auto isDynamicEntityId = [](uint64_t entity_id) {
            return entity_id == 0x400000000000060 ||
                   entity_id == 0x40000000000480A ||
                   entity_id == 0x40000000000005F ||
                   entity_id == 0x400000000002533;
        };

        uint64_t entity_list = SDK->RPM<uint64_t>(SDK->dwGameBase + offset::Address_entity_base);
        if (!entity_list) {
            Diagnostics::Trace("Entity scan skipped: entity list pointer is null.");
            return result;
        }

        std::vector<EntityScanRecord> records{};
        std::unordered_map<uint32_t, uint64_t> match_index{};
        std::unordered_set<uint64_t> seen_entities{};
        records.reserve(4096);
        match_index.reserve(4096);
        seen_entities.reserve(4096);

        auto addRecord = [&](uint64_t possible_common) {
            if (!possible_common)
                return;
            if (!seen_entities.insert(possible_common).second)
                return;

            EntityScanRecord record{};
            record.entity = possible_common;
            record.header.Read(possible_common);

            if (!record.header.ReadParentOffset(offset::Entity_MatchId, record.unique_id))
                record.unique_id = SDK->RPM<uint32_t>(possible_common + offset::Entity_MatchId);

            uint64_t ptr_value = 0;
            if (!record.header.ReadParentOffset(offset::Entity_PoolPtr, ptr_value))
                ptr_value = SDK->RPM<uint64_t>(possible_common + offset::Entity_PoolPtr);

            record.ptr = ptr_value & 0xFFFFFFFFFFFFFFC0;
            if (record.ptr && record.ptr < 0xFFFFFFFFFFFFFFEF)
                record.entity_id = SDK->RPM<uint64_t>(record.ptr + offset::Pool_PoolId);

            if (record.unique_id != 0)
                match_index.emplace(record.unique_id, record.entity);
            records.push_back(std::move(record));
        };

        constexpr size_t kEntityListReadSize = 0x10000;
        constexpr size_t kEntityListChunkSize = 0x1000;
        std::vector<uint8_t> entity_chunk(kEntityListChunkSize);
        size_t readable_bytes = 0;
        size_t readable_chunks = 0;

        for (size_t offset = 0; offset < kEntityListReadSize; offset += kEntityListChunkSize) {
            const size_t remaining = kEntityListReadSize - offset;
            const size_t chunk_size =
                remaining < kEntityListChunkSize ? remaining : kEntityListChunkSize;
            if (!mem.Read(entity_list + offset, entity_chunk.data(), chunk_size))
                continue;

            readable_bytes += chunk_size;
            ++readable_chunks;
            for (size_t slot_offset = 0; slot_offset + sizeof(Entity) <= chunk_size;
                 slot_offset += sizeof(Entity)) {
                uint64_t possible_common = 0;
                std::memcpy(&possible_common, entity_chunk.data() + slot_offset, sizeof(possible_common));
                addRecord(possible_common);
            }
        }

        if (records.empty()) {
            Diagnostics::Trace("Entity scan skipped: no records from list=0x%llX chunks=%zu bytes=%zu.",
                static_cast<unsigned long long>(entity_list),
                readable_chunks,
                readable_bytes);
            return result;
        }

        auto addPair = [&](uint64_t component_parent, uint64_t link_parent) {
            if (!component_parent || !link_parent)
                return;
            const auto pair = std::make_pair(component_parent, link_parent);
            if (std::find(result.begin(), result.end(), pair) == result.end())
                result.push_back(pair);
        };

        for (const EntityScanRecord& current : records) {
            uint64_t cur_entity = current.entity;
            if (!cur_entity) continue;

            uint64_t common_linker = DecryptComponent(
                cur_entity,
                TYPE_LINK,
                current.header.valid ? &current.header : nullptr);
            if (!common_linker) {
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            uint32_t unique_id = SDK->RPM<uint32_t>(common_linker + offset::Link_UniqueId);
            if (unique_id == 0)
                continue;

            const auto match = match_index.find(unique_id);
            if (match != match_index.end())
                addPair(match->second, cur_entity);
        }

        for (const EntityScanRecord& current : records) {
            if (isDynamicEntityId(current.entity_id))
                addPair(current.entity, current.entity);
        }

        Diagnostics::Trace("Entity scan list=0x%llX bytes=%zu records=%zu pairs=%zu.",
            static_cast<unsigned long long>(entity_list),
            readable_bytes,
            records.size(),
            result.size());
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
            case eHero::HERO_TRAININGBOT8: return "Training Bot";
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
            if (count == 0 || count >= 0xFF) return false;
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
        const uint32_t count = SDK->RPM<uint32_t>(a1 + 0x2A8);
        if (count == 0 || count >= 0xFF) return 0;

        uint64_t entry = SDK->RPM<uint64_t>(a1 + 0x2A0);
        if (!entry) return 0;

        for (uint32_t scanned = 0; scanned < count; ++scanned, entry += 16) {
            if (SDK->RPM<uint16_t>(entry + 8) == a2)
                return SDK->RPM<uint64_t>(entry);
        }
        return 0;
    }

    inline uint64_t FnSkillStruct(__m128* a1, uint16_t* a2) {
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
        const uint32_t entryCount = SDK->RPM<uint32_t>(v9 + v6 + 8);
        if (entryCount == 0 || entryCount >= 0xFF) return 0;

        const uint64_t listBase = SDK->RPM<uint64_t>(v9 + v6);
        if (!listBase) return 0;

        for (uint32_t remaining = entryCount; remaining > 0; --remaining) {
            const uint64_t entry = listBase + 16ull * (remaining - 1);
            if (SDK->RPM<uint16_t>(entry) != v8)
                continue;

            uint64_t v13 = SDK->RPM<uint64_t>(entry + 8);
            if (!v13) return 0;
            return v13;
        }
        return 0;
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
