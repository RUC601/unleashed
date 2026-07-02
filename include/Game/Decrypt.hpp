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
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <emmintrin.h>

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

        static const uint8_t key_sig[] =
            "\x00\x00\x00\x00\x21\x00\x00\x00\x00\x00\x00\x00\x24\x00\x00\x00"
            "\x01\x00\x00\x00\x29\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
            "\x00\x00\x00\x00\x00\x00";
        static const char* key_mask = "xxxxxxxxxxxxx?xxxxxxxxxxxxxxxxxxxxxxxx";

        int pattern_attempts = 0;
        while (true) {
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

            {
                uint64_t gk_addr = SDK->dwGameBase + offset::GetGlobalKey_RVA;
                uint8_t code[128] = {};
                SDK->read_buf(gk_addr, (char*)code, sizeof(code));

                uint64_t lea_target = 0;
                for (int i = 0; i < 120; i++) {
                    if (code[i] == 0x48 && code[i+1] == 0x8D &&
                        (code[i+2] & 0xC7) == 0x05) {
                        int32_t disp = *(int32_t*)&code[i+3];
                        lea_target = gk_addr + i + 7 + disp;
                        break;
                    }
                }

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

                    bool decoded_in_range = (decoded >= SDK->dwGameBase &&
                                             decoded < SDK->dwGameBase + 0x4000000);

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
                        SDK->GlobalKey1 = SDK->GlobalKey2 = 0;
                    }

                    for (int i = 0; i < 120; i++) {
                        if (code[i] == 0x48 && code[i+1] == 0x89 && code[i+2] == 0x05) {
                            int32_t disp = *(int32_t*)&code[i+3];
                            uint64_t global_rva = (gk_addr + i + 7 + disp) - SDK->dwGameBase;
                            uint64_t global_val = SDK->RPM<uint64_t>(SDK->dwGameBase + global_rva);
                            printf("[Decrypt] Global store at RVA 0x%llX = 0x%llX\n",
                                   global_rva, global_val);

                            if (global_val > 0x1000000000000000) {
                                SDK->GlobalKey1 = global_val;
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

    inline uint64_t GetParent(uint64_t encrypted) {
        __try {
            auto result = encrypted;
            result = (result >> 0x20) | (result << 0x20);
            result ^= 0x4B920A7072A077C5;
            result -= 0x107816B001CA79C8;
            result = (result >> 0x23) | (result << 0x1D);
            result += 0xFD2150D0AEF24514;
            return result;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    inline void sub_E8D1A0(uint64_t* bit_mask, uint64_t* lower_mask,
                           uint32_t* shift, uint32_t* bucket,
                           uint32_t componentid) {
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

    inline uintptr_t DecryptComponent(uintptr_t parent, uint32_t idx,
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

        const uint64_t present_mask =
            static_cast<uint64_t>(static_cast<int64_t>(
                -static_cast<int32_t>(present)));
        const auto& activeOffsets = offset::Active();

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::Identity) {
            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
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

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World151177) {
            component = ROR64(component ^ component_key_material_1,
                offset::Component151177_Ror1);
            component = ROR64(component + offset::Component151177_Add1,
                offset::Component151177_Ror2);
            component ^= static_cast<uint64_t>(component_key_byte);
            component ^= offset::Component151177_Xor1;
            component ^= offset::Component151177_Xor2;

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        if (activeOffsets.componentTransform == offset::ComponentTransformMode::World150818) {
            component = static_cast<uint64_t>(component_key_byte) ^
                ((component_key_material_1 ^
                    (component + offset::Component150818_Add1)) +
                    offset::Component150818_Add2) ^
                offset::Component150818_Xor1;
            component ^= offset::Component150818_Xor2;
            component = ROR64(component, offset::Component150818_Ror);

            const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
            if (!decoded)
                Diagnostics::RecordDecryptFailure();
            return decoded;
        }

        component ^= component_key_material_1;
        component ^= offset::Component_Xor1;
        component = ROR64(component, offset::Component_Ror1);
        component += offset::Component_Add1;
        component ^= static_cast<uint64_t>(component_key_byte);
        component -= offset::Component_Sub1;
        component = ROR64(component, offset::Component_Ror2);
        component = ROR64(component, offset::Component_Ror3);

        const uintptr_t decoded = static_cast<uintptr_t>(present_mask & component);
        if (!decoded)
            Diagnostics::RecordDecryptFailure();
        return decoded;
    }

    inline uintptr_t DecryptComponent(uintptr_t parent, uint32_t idx) {
        EntityHeaderSnapshot snapshot{};
        const EntityHeaderSnapshot* active_snapshot =
            snapshot.Read(parent) ? &snapshot : nullptr;
        return DecryptComponent(parent, idx, active_snapshot);
    }

    inline uint64_t DecryptVis(uint64_t visBase) {
        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.VisibilityValueOffset)
            return 0;

        const bool cnNeProfile = offset::ActiveProfile() == offset::RuntimeProfile::CnNe;
        if (cnNeProfile) {
            static bool loggedCnVisibility = false;
            if (!loggedCnVisibility) {
                Diagnostics::Info("[VISIBILITY] CN/NE +0x%llX uses live-verified raw bool state; visible when raw == 1.",
                    static_cast<unsigned long long>(activeOffsets.VisibilityValueOffset));
                loggedCnVisibility = true;
            }
        }

        const uint64_t enc = SDK->RPM<uint64_t>(visBase + activeOffsets.VisibilityValueOffset);
        bool visible = false;
        if (cnNeProfile) {
            visible = enc == 1;
        } else {
            const uint64_t keySource = SDK->RPM<uint64_t>(
                SDK->dwGameBase + offset::VisibilityGlobalKeyPtr_RVA);
            const uint64_t xorQword = keySource
                ? SDK->RPM<uint64_t>(keySource + offset::VisibilityQwordOffset)
                : 0;
            const uint8_t xorByte = SDK->RPM<uint8_t>(
                SDK->dwGameBase + offset::VisibilityMagicByte_RVA);
            uint64_t value =
                (static_cast<uint64_t>(xorByte) ^
                    ((ROR64(enc, offset::Visibility_Ror1) ^
                        offset::Visibility_Xor1) -
                        offset::Visibility_Sub1)) +
                offset::Visibility_Add1;
            value = ROR64(value + offset::Visibility_Add2, offset::Visibility_Ror2);
            visible = (xorQword ^ ROL64(value, 1)) == 1;
        }
        const uint64_t result = visible ? 1 : 0;
        {
            static uint32_t sampleCount = 0;
            if (sampleCount < 50) {
                Diagnostics::Aim("visibility.decrypt sample=%u raw=0x%llX visible=%d visbase=0x%llX",
                    sampleCount,
                    static_cast<unsigned long long>(enc),
                    visible ? 1 : 0,
                    static_cast<unsigned long long>(visBase));
                ++sampleCount;
            }
        }
        return result;
    }

    // =========================================================================
    // Input sensitivity helpers
    // =========================================================================

    inline bool IsPlausibleSensitivity(float value) {
        return value > 0.0f && value < 100.0f;
    }

    inline bool IsPlausibleUserPointer(uint64_t value) {
        return value >= 0x10000ull &&
            value < 0x0000800000000000ull &&
            (value % alignof(void*) == 0);
    }

    inline bool IsAddressInRange(uint64_t value, uint64_t start, uint64_t end) {
        return start != 0 && end > start && value >= start && value < end;
    }

    inline bool IsLikelyEntityMatchId(uint32_t value) {
        return (value & 0x80000000u) != 0;
    }

    inline bool IsLikelyCnNeEntityParent(
        uint64_t value,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize) {
        if (!IsPlausibleUserPointer(value))
            return false;

        const uint64_t moduleEnd =
            moduleSize != 0 ? moduleBase + moduleSize : moduleBase + 0x8000000ull;
        if (IsAddressInRange(value, moduleBase, moduleEnd))
            return false;
        if (IsAddressInRange(value, entityList, entityList + entityListReadSize))
            return false;
        return true;
    }

    inline uint64_t ResolveCnNeWrapperMapBase(uint64_t parent) {
        if (!IsPlausibleUserPointer(parent))
            return 0;

        const uint32_t id = SDK->RPM<uint32_t>(parent + 0x2F0);
        if (id == 0)
            return 0;

        const uint64_t bucketArray = SDK->RPM<uint64_t>(parent + 0x18);
        if (!IsPlausibleUserPointer(bucketArray))
            return 0;

        const uint64_t bucket = id & 0xFFFu;
        const uint64_t entries = SDK->RPM<uint64_t>(bucketArray + 16ull * bucket);
        const uint32_t count = SDK->RPM<uint32_t>(bucketArray + 16ull * bucket + 8);
        if (!IsPlausibleUserPointer(entries) || count == 0 || count > 4096)
            return 0;

        const uint32_t cappedCount = (std::min)(count, 512u);
        for (uint32_t index = 0; index < cappedCount; ++index) {
            const uint64_t entry = entries + 16ull * index;
            if (SDK->RPM<uint32_t>(entry) != id)
                continue;
            const uint64_t mapBase = SDK->RPM<uint64_t>(entry + 8);
            if (IsPlausibleUserPointer(mapBase))
                return mapBase;
        }
        return 0;
    }

    inline uint64_t ResolveCnNeLinkTargetMapBase(
        uint64_t linkBase,
        uint64_t idOffset,
        uint32_t* resolvedId = nullptr) {
        if (resolvedId)
            *resolvedId = 0;
        if (!IsPlausibleUserPointer(linkBase))
            return 0;

        const uint32_t id = SDK->RPM<uint32_t>(linkBase + idOffset);
        if (id == 0)
            return 0;

        const uint64_t context = SDK->RPM<uint64_t>(linkBase + 0x8);
        if (!IsPlausibleUserPointer(context))
            return 0;

        const uint64_t bucketOwner = SDK->RPM<uint64_t>(context + 0x28);
        if (!IsPlausibleUserPointer(bucketOwner))
            return 0;

        const uint64_t bucketArray = SDK->RPM<uint64_t>(bucketOwner + 0x18);
        if (!IsPlausibleUserPointer(bucketArray))
            return 0;

        const uint64_t bucket = id & 0xFFFu;
        const uint64_t entries = SDK->RPM<uint64_t>(bucketArray + 16ull * bucket);
        const uint32_t count = SDK->RPM<uint32_t>(bucketArray + 16ull * bucket + 8);
        if (!IsPlausibleUserPointer(entries) || count == 0 || count > 4096)
            return 0;

        const uint32_t cappedCount = (std::min)(count, 512u);
        for (uint32_t index = 0; index < cappedCount; ++index) {
            const uint64_t entry = entries + 16ull * index;
            if (SDK->RPM<uint32_t>(entry) != id)
                continue;
            const uint64_t mapBase = SDK->RPM<uint64_t>(entry + 8);
            if (IsPlausibleUserPointer(mapBase)) {
                if (resolvedId)
                    *resolvedId = id;
                return mapBase;
            }
        }

        return 0;
    }

    inline bool AddCnNeMapBaseCandidate(
        std::vector<uint64_t>& candidates,
        uint64_t value,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize) {
        if (!IsLikelyCnNeEntityParent(
                value,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize)) {
            return false;
        }
        if (std::find(candidates.begin(), candidates.end(), value) != candidates.end())
            return false;
        candidates.push_back(value);
        return true;
    }

    inline std::vector<uint64_t> CollectCnNeMapBaseCandidates(
        uint64_t parent,
        uint64_t entityList,
        size_t entityListReadSize,
        uint64_t moduleBase,
        uint64_t moduleSize,
        Diagnostics::EntityScanDetailStats* scanDetail = nullptr,
        bool mapDiagEnabled = false) {
        std::vector<uint64_t> candidates;
        candidates.reserve(3);

        if (AddCnNeMapBaseCandidate(
                candidates,
                parent,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnNeMapCandidateDirectSourceCount;
        }

        const uint64_t plus8 = SDK->RPM<uint64_t>(parent + 0x8);
        if (AddCnNeMapBaseCandidate(
                candidates,
                plus8,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnNeMapCandidatePlus8SourceCount;
        }

        const uint64_t wrapperMapBase = ResolveCnNeWrapperMapBase(parent);
        if (AddCnNeMapBaseCandidate(
                candidates,
                wrapperMapBase,
                entityList,
                entityListReadSize,
                moduleBase,
                moduleSize) &&
            mapDiagEnabled &&
            scanDetail) {
            ++scanDetail->cnNeMapCandidateWrapperSourceCount;
        }

        return candidates;
    }

    inline uint64_t DecryptSingletonList(uint64_t raw) {
        return ROR64(
            (raw ^ offset::Singleton_K1_xor) - offset::Singleton_K2_sub,
            offset::Singleton_Ror) + offset::Singleton_K3_add;
    }

    inline uint64_t DecryptLiveGameAdmin(uint64_t raw) {
        return ROR64(
            ((raw + offset::LiveGameAdmin_Add1) ^ offset::LiveGameAdmin_Xor1) +
                offset::LiveGameAdmin_Add2,
            offset::LiveGameAdmin_Ror);
    }

    inline bool TryGetLiveGameAdmin(uint64_t& gameAdmin) {
        gameAdmin = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.GlobalAdmin_RVA)
            return false;

        const uint64_t clientGame = SDK->RPM<uint64_t>(
            SDK->dwGameBase + activeOffsets.GlobalAdmin_RVA);
        if (!IsPlausibleUserPointer(clientGame))
            return false;

        const uint64_t raw = SDK->RPM<uint64_t>(
            clientGame + offset::LiveGameAdmin_InputOffset);
        if (!raw)
            return false;

        gameAdmin = DecryptLiveGameAdmin(raw);
        return IsPlausibleUserPointer(gameAdmin);
    }

    inline uint64_t ResolveSingletonList(uint64_t raw) {
        switch (offset::Active().singletonListMode) {
        case offset::SingletonListMode::Plain:
            return raw;
        case offset::SingletonListMode::LiveGameAdminWorldBz:
            return raw;
        case offset::SingletonListMode::EncryptedWorldBz:
        default:
            return DecryptSingletonList(raw);
        }
    }

    inline bool TryGetLiveGameAdminSingleton(uint64_t index, uint64_t& object) {
        object = 0;

        uint64_t gameAdmin = 0;
        if (!TryGetLiveGameAdmin(gameAdmin))
            return false;

        const uint64_t listOffsets[] = {
            offset::Singleton_InputOffset,
            0x168,
            0x2E0,
            0x300,
            0x310,
        };

        for (const uint64_t listOffset : listOffsets) {
            const uint64_t raw = SDK->RPM<uint64_t>(gameAdmin + listOffset);
            const uint64_t candidates[] = {
                raw,
                DecryptSingletonList(raw),
                DecryptLiveGameAdmin(raw),
            };
            for (const uint64_t list : candidates) {
                if (!IsPlausibleUserPointer(list))
                    continue;
                const uint64_t candidate = SDK->RPM<uint64_t>(list + 8ull * index);
                if (IsPlausibleUserPointer(candidate)) {
                    object = candidate;
                    return true;
                }
            }
        }

        return false;
    }

    inline bool TryGetGlobalAdminSingleton(uint64_t index, uint64_t& object) {
        object = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.GlobalAdmin_RVA)
            return false;

        if (activeOffsets.singletonListMode == offset::SingletonListMode::LiveGameAdminWorldBz)
            return TryGetLiveGameAdminSingleton(index, object);

        const uint64_t admin = SDK->RPM<uint64_t>(
            SDK->dwGameBase + activeOffsets.GlobalAdmin_RVA);
        if (!IsPlausibleUserPointer(admin))
            return false;

        const uint64_t listRaw = SDK->RPM<uint64_t>(
            admin + offset::Singleton_InputOffset);
        const uint64_t list = ResolveSingletonList(listRaw);
        if (!IsPlausibleUserPointer(list))
            return false;

        object = SDK->RPM<uint64_t>(list + 8ull * index);
        return IsPlausibleUserPointer(object);
    }

    inline bool TryReadSensitivityFromObject(uint64_t object, float& value) {
        if (!IsPlausibleUserPointer(object))
            return false;

        const float sensitivity = SDK->RPM<float>(object + offset::Sensitivity);
        if (!IsPlausibleSensitivity(sensitivity))
            return false;

        const float sensX = SDK->RPM<float>(object + offset::SensX_Scale);
        const float sensY = SDK->RPM<float>(object + offset::SensY_Scale);
        if (!std::isfinite(sensX) || !std::isfinite(sensY))
            return false;

        value = sensitivity;
        return true;
    }

    inline bool TryReadInputMouseScaleSensitivity(float& value, uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;
        if (!SDK->dwGameBase)
            return false;

        const auto& activeOffsets = offset::Active();
        if (!activeOffsets.InputMouseScaleX_RVA || !activeOffsets.InputMouseScaleY_RVA)
            return false;

        const uint64_t xAddress = SDK->dwGameBase + activeOffsets.InputMouseScaleX_RVA;
        const uint64_t yAddress = SDK->dwGameBase + activeOffsets.InputMouseScaleY_RVA;
        const float sensX = SDK->RPM<float>(xAddress);
        const float sensY = SDK->RPM<float>(yAddress);
        if (!IsPlausibleSensitivity(sensX) || !IsPlausibleSensitivity(sensY))
            return false;

        const float tolerance = (std::max)(0.05f, (std::max)(sensX, sensY) * 0.02f);
        if (std::fabs(sensX - sensY) > tolerance)
            return false;

        value = (sensX + sensY) * 0.5f;
        if (sourceObject)
            *sourceObject = xAddress;
        return true;
    }

    inline bool TryReadNormalizedSensitivityObject(
        uint64_t object,
        float& value,
        uint64_t* sourceObject) {
        value = 0.0f;
        if (!IsPlausibleUserPointer(object))
            return false;

        const float normalized = SDK->RPM<float>(object + offset::Sensitivity);
        if (!std::isfinite(normalized) || normalized < 0.0001f || normalized > 1.0f)
            return false;

        value = normalized * offset::Sensitivity_NormalizedToUserScale;
        if (!IsPlausibleSensitivity(value))
            return false;

        if (sourceObject)
            *sourceObject = object;
        return true;
    }

    inline bool TryReadWorldBzGlobalAdminSensitivity(
        float& value,
        uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;
        if (!SDK->dwGameBase || !offset::SensitivityAdmin_WorldBz_RVA)
            return false;

        const uint64_t admin = SDK->RPM<uint64_t>(
            SDK->dwGameBase + offset::SensitivityAdmin_WorldBz_RVA);
        if (!IsPlausibleUserPointer(admin))
            return false;

        const uint64_t raw = SDK->RPM<uint64_t>(admin + offset::Singleton_InputOffset);
        if (!raw)
            return false;

        const uint64_t lists[] = {
            raw,
            DecryptLiveGameAdmin(raw),
            DecryptSingletonList(raw),
        };
        for (uint64_t list : lists) {
            if (!IsPlausibleUserPointer(list))
                continue;

            const uint64_t object = SDK->RPM<uint64_t>(
                list + 8ull * offset::SensitivitySingletonIndex);
            if (TryReadNormalizedSensitivityObject(object, value, sourceObject))
                return true;
        }

        return false;
    }

    inline bool TryReadLiveGameAdminSensitivity(float& value, uint64_t* sourceObject) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;

        if (TryReadWorldBzGlobalAdminSensitivity(value, sourceObject))
            return true;

        uint64_t gameAdmin = 0;
        if (!TryGetLiveGameAdmin(gameAdmin))
            return false;

        uint64_t sensitivityObject = 0;
        if (TryGetLiveGameAdminSingleton(offset::SensitivitySingletonIndex, sensitivityObject) &&
            TryReadSensitivityFromObject(sensitivityObject, value)) {
            if (sourceObject)
                *sourceObject = sensitivityObject;
            return true;
        }

        return false;
    }

    inline bool TryReadGameMouseSensitivity(float& value, uint64_t* sourceObject = nullptr) {
        value = 0.0f;
        if (sourceObject)
            *sourceObject = 0;

        if (offset::Active().singletonListMode == offset::SingletonListMode::LiveGameAdminWorldBz)
            return TryReadLiveGameAdminSensitivity(value, sourceObject) ||
                TryReadInputMouseScaleSensitivity(value, sourceObject);

        uint64_t sensitivityObject = 0;
        if (!TryGetGlobalAdminSingleton(offset::SensitivitySingletonIndex, sensitivityObject))
            return false;

        if (!TryReadSensitivityFromObject(sensitivityObject, value))
            return false;

        if (sourceObject)
            *sourceObject = sensitivityObject;
        return true;
    }

    inline uintptr_t GetSenstivePTR() {
        if (!SDK->dwGameBase)
            return 0;

        static uintptr_t cached = 0;
        static uint64_t cachedBase = 0;
        static offset::RuntimeProfile cachedProfile = offset::RuntimeProfile::WorldBz;
        const offset::RuntimeProfile currentProfile = offset::ActiveProfile();
        if (cached && cachedBase == SDK->dwGameBase && cachedProfile == currentProfile)
            return cached;

        cached = 0;
        cachedBase = SDK->dwGameBase;
        cachedProfile = currentProfile;

        const auto& activeOffsets = offset::Active();
        const uintptr_t mouse_scale_x =
            SDK->dwGameBase + activeOffsets.InputMouseScaleX_RVA;
        if (IsPlausibleSensitivity(SDK->RPM<float>(mouse_scale_x))) {
            cached = mouse_scale_x;
            return cached;
        }

        return 0;
    }

    inline int ReadEntityScannerEnvFlagState(const char* name)
    {
        char buffer[16] = {};
        const DWORD length = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return -1;
        return buffer[0] != '0' &&
            buffer[0] != 'n' &&
            buffer[0] != 'N' &&
            buffer[0] != 'f' &&
            buffer[0] != 'F'
            ? 1
            : 0;
    }

    inline bool EntityScannerEnvFlagEnabled(const char* name)
    {
        return ReadEntityScannerEnvFlagState(name) == 1;
    }

    inline bool EntityLightScanRequested()
    {
        static const int lightScanFlag =
            ReadEntityScannerEnvFlagState("UN_DMA_LIGHT_SCAN");
        if (lightScanFlag >= 0)
            return lightScanFlag == 1;
        return offset::IsCnNeProfile() ||
            EntityScannerEnvFlagEnabled("UN_DMA_ENTITY_PIPELINE_V2");
    }

    inline size_t EntityLightScanMaxCandidates()
    {
        char buffer[32] = {};
        const DWORD length = GetEnvironmentVariableA(
            "UN_DMA_LIGHT_SCAN_MAX_CANDIDATES",
            buffer,
            static_cast<DWORD>(sizeof(buffer)));
        if (length == 0 || length >= sizeof(buffer))
            return 128;

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(buffer, &end, 10);
        if (end == buffer || (end && *end != '\0'))
            return 128;
        return (std::min<size_t>)((std::max<size_t>)(parsed, 16), 512);
    }

    inline DWORD EntityScannerPersistentMapCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_STAGE4B_MAP_CACHE_PERSIST_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline size_t EntityScannerPersistentMapCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_STAGE4B_MAP_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline DWORD EntityScannerComponentNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerLinkDecryptNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_LINK_DECRYPT_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerRecordSnapshotCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListRootCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_ENTITY_LIST_ROOT_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 10000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListReadNegativeCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_ENTITY_LIST_READ_NEGATIVE_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 2000);
        }();
        return ttlMs;
    }

    inline DWORD EntityScannerEntityListReadCacheTtlMs()
    {
        static const DWORD ttlMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_ENTITY_LIST_READ_CACHE_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 16), 2000);
        }();
        return ttlMs;
    }

    inline size_t EntityScannerCnNeEntityListChunkSize()
    {
        static const size_t chunkSize = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_ENTITY_LIST_CHUNK_SIZE",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0x10000);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 0);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0x10000);
            size_t value = (std::min<size_t>)(
                (std::max<size_t>)(parsed, 0x1000),
                0x10000);
            value -= value % 0x1000;
            return value == 0 ? static_cast<size_t>(0x1000) : value;
        }();
        return chunkSize;
    }

    inline size_t EntityScannerRecordSnapshotCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_RECORD_SNAPSHOT_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline size_t EntityScannerComponentNegativeCacheRefreshBudget()
    {
        static const size_t refreshBudget = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_COMPONENT_NEGATIVE_CACHE_REFRESH_BUDGET",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<size_t>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<size_t>(0);
            if (parsed == 0)
                return static_cast<size_t>(0);
            return (std::min<size_t>)((std::max<size_t>)(parsed, 1), 64);
        }();
        return refreshBudget;
    }

    inline DWORD EntityScannerStaleMetadataMs()
    {
        static const DWORD staleMs = []() {
            char buffer[32] = {};
            const DWORD length = GetEnvironmentVariableA(
                "UN_DMA_CN_NE_SCANNER_STALE_METADATA_MS",
                buffer,
                static_cast<DWORD>(sizeof(buffer)));
            if (length == 0 || length >= sizeof(buffer))
                return static_cast<DWORD>(0);

            char* end = nullptr;
            const unsigned long parsed = std::strtoul(buffer, &end, 10);
            if (end == buffer || (end && *end != '\0'))
                return static_cast<DWORD>(0);
            if (parsed == 0)
                return static_cast<DWORD>(0);
            return (std::min<DWORD>)((std::max<DWORD>)(parsed, 50), 30000);
        }();
        return staleMs;
    }

    // =========================================================================
    // Entity list scanning
    // =========================================================================

    inline size_t ProbeEntityTopologyCandidateCount() {
        struct Entity {
            uint64_t entity;
            uint64_t pad;
        };

        const uint64_t entity_list = SDK->RPM<uint64_t>(
            SDK->dwGameBase + offset::Active().Address_entity_base);
        if (!entity_list)
            return 0;

        const bool cnNeProfile = offset::IsCnNeProfile();
        const size_t listReadSize = cnNeProfile ? 0x40000 : 0x1E000;
        const size_t chunkSize = cnNeProfile ? 0x10000 : 0x1000;
        uint64_t moduleSize = 0;
        if (cnNeProfile)
            moduleSize = static_cast<uint64_t>(mem.GetBaseSize("Overwatch.exe"));

        std::unordered_set<uint64_t> seen{};
        seen.reserve(256);
        std::vector<uint8_t> chunk(chunkSize);
        for (size_t offset = 0; offset < listReadSize; offset += chunkSize) {
            const size_t remaining = listReadSize - offset;
            const size_t bytesToRead = remaining < chunkSize ? remaining : chunkSize;
            if (!mem.Read(entity_list + offset, chunk.data(), bytesToRead))
                continue;

            for (size_t slotOffset = 0; slotOffset + sizeof(Entity) <= bytesToRead;
                 slotOffset += sizeof(Entity)) {
                uint64_t possibleParent = 0;
                std::memcpy(&possibleParent, chunk.data() + slotOffset, sizeof(possibleParent));
                if (!IsPlausibleUserPointer(possibleParent))
                    continue;
                if (cnNeProfile &&
                    !IsLikelyCnNeEntityParent(
                        possibleParent,
                        entity_list,
                        listReadSize,
                        SDK->dwGameBase,
                        moduleSize)) {
                    continue;
                }
                seen.insert(possibleParent);
            }
        }

        return seen.size();
    }

    inline std::vector<std::pair<uint64_t, uint64_t>> get_ow_entities() {
        SDK->BeginFrame(Diagnostics::SdkFrameSource::Scan);
        const Diagnostics::ScopedDmaCallsite scannerCallsite(Diagnostics::DmaCallsite::EntityScan);
        (void)scannerCallsite;

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

        const Diagnostics::DmaReadStats scanDmaStart = Diagnostics::SnapshotDmaReadStats();
        const bool scanDmaRangeDiagEnabled =
            EntityScannerEnvFlagEnabled("UN_DMA_SCAN_DMA_RANGE_DIAG");
        const size_t scanDmaRangeStart =
            scanDmaRangeDiagEnabled ? Diagnostics::DmaSampleCursor() : 0;
        Diagnostics::EntityScanDetailStats scanDetail{};
        scanDetail.scanDmaRangeDiagEnabled = scanDmaRangeDiagEnabled;
        auto elapsedMs = [](std::chrono::steady_clock::time_point startedAt) {
            return static_cast<double>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startedAt).count()) / 1000.0;
        };
        auto finishScanDetail = [&]() {
            const auto finalizeStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::DmaReadStats scanDmaEnd = Diagnostics::SnapshotDmaReadStats();
            scanDetail.scanDmaReadsDelta = scanDmaEnd.total >= scanDmaStart.total
                ? scanDmaEnd.total - scanDmaStart.total
                : 0;
            scanDetail.scanDmaFailDelta = scanDmaEnd.failed >= scanDmaStart.failed
                ? scanDmaEnd.failed - scanDmaStart.failed
                : 0;
            if (scanDmaRangeDiagEnabled) {
                const Diagnostics::DmaWindowStats scanDmaRange =
                    Diagnostics::GetDmaRangeStats(
                        scanDmaRangeStart,
                        Diagnostics::DmaSampleCursor());
                scanDetail.scanDmaRangeReads = scanDmaRange.totalReads;
                scanDetail.scanDmaRangeFailed = scanDmaRange.failedReads;
                scanDetail.scanDmaRangeMaxLatencyUs = scanDmaRange.maxLatencyUs;
                uint64_t maxCallsiteLatencyUs = 0;
                int maxCallsite = 0;
                for (int i = 0; i < static_cast<int>(Diagnostics::DmaCallsite::Count); ++i) {
                    if (scanDmaRange.perCallsiteMaxUs[i] > maxCallsiteLatencyUs) {
                        maxCallsiteLatencyUs = scanDmaRange.perCallsiteMaxUs[i];
                        maxCallsite = i;
                    }
                }
                scanDetail.scanDmaRangeMaxCallsite =
                    static_cast<uint8_t>(maxCallsite);
                auto isScannerCallsite = [](Diagnostics::DmaCallsite callsite) {
                    switch (callsite) {
                    case Diagnostics::DmaCallsite::EntityScan:
                    case Diagnostics::DmaCallsite::EntityScanRoot:
                    case Diagnostics::DmaCallsite::EntityScanListRead:
                    case Diagnostics::DmaCallsite::EntityScanRecordBuild:
                    case Diagnostics::DmaCallsite::EntityScanRecordMatchId:
                    case Diagnostics::DmaCallsite::EntityScanRecordHeader:
                    case Diagnostics::DmaCallsite::EntityScanRecordPoolPtr:
                    case Diagnostics::DmaCallsite::EntityScanRecordPoolId:
                    case Diagnostics::DmaCallsite::EntityScanMatchLink:
                    case Diagnostics::DmaCallsite::EntityScanTargetMap:
                    case Diagnostics::DmaCallsite::EntityScanMapCandidate:
                    case Diagnostics::DmaCallsite::EntityScanLinkTargetResolve:
                    case Diagnostics::DmaCallsite::EntityScanSelfValidation:
                    case Diagnostics::DmaCallsite::EntityScanComponentValidation:
                        return true;
                    default:
                        return false;
                    }
                };
                for (int i = 0; i < static_cast<int>(Diagnostics::DmaCallsite::Count); ++i) {
                    const uint64_t reads = scanDmaRange.perCallsiteReads[i];
                    if (reads == 0)
                        continue;
                    const uint64_t maxUs = scanDmaRange.perCallsiteMaxUs[i];
                    const auto callsite = static_cast<Diagnostics::DmaCallsite>(i);
                    if (isScannerCallsite(callsite)) {
                        scanDetail.scanDmaRangeScannerReads += reads;
                        if (maxUs > scanDetail.scanDmaRangeScannerMaxLatencyUs) {
                            scanDetail.scanDmaRangeScannerMaxLatencyUs = maxUs;
                            scanDetail.scanDmaRangeScannerMaxCallsite =
                                static_cast<uint8_t>(i);
                        }
                    } else {
                        scanDetail.scanDmaRangeForeignReads += reads;
                        if (maxUs > scanDetail.scanDmaRangeForeignMaxLatencyUs) {
                            scanDetail.scanDmaRangeForeignMaxLatencyUs = maxUs;
                            scanDetail.scanDmaRangeForeignMaxCallsite =
                                static_cast<uint8_t>(i);
                        }
                    }
                }
                auto callsiteMax = [&](Diagnostics::DmaCallsite callsite) {
                    return scanDmaRange.perCallsiteMaxUs[static_cast<int>(callsite)];
                };
                scanDetail.scanDmaRangeRootMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRoot);
                scanDetail.scanDmaRangeListReadMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanListRead);
                scanDetail.scanDmaRangeRecordHeaderMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRecordHeader);
                scanDetail.scanDmaRangeRecordPoolIdMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanRecordPoolId);
                scanDetail.scanDmaRangeTargetMapMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanTargetMap);
                scanDetail.scanDmaRangeComponentValidationMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::EntityScanComponentValidation);
                scanDetail.scanDmaRangeViewMatrixMaxUs =
                    callsiteMax(Diagnostics::DmaCallsite::ViewMatrix);
            }
            scanDetail.scanFinalizeMs += elapsedMs(finalizeStartedAt);
            Diagnostics::SetEntityScanDetailStats(scanDetail);
        };

        auto isDynamicEntityId = [](uint64_t entity_id) {
            return entity_id == 0x400000000000060 ||
                   entity_id == 0x40000000000480A ||
                   entity_id == 0x40000000000005F ||
                   entity_id == 0x400000000002533;
        };

        const bool cnNeProfile = offset::IsCnNeProfile();
        const DWORD rootCacheTtlMs =
            cnNeProfile ? EntityScannerEntityListRootCacheTtlMs() : 0;
        const bool rootCacheEnabled = cnNeProfile && rootCacheTtlMs > 0;
        scanDetail.cnNeEntityListRootCacheEnabled = rootCacheEnabled;
        scanDetail.cnNeEntityListRootCacheTtlMs = rootCacheTtlMs;
        const DWORD staleMetadataMs =
            cnNeProfile ? EntityScannerStaleMetadataMs() : 0;
        scanDetail.cnNeScannerStaleMetadataMs = staleMetadataMs;
        const bool staleMetadataOnly =
            cnNeProfile &&
            staleMetadataMs > 0 &&
            EntityScannerEnvFlagEnabled("UN_DMA_CN_NE_SCANNER_STALE_METADATA_ONLY");
        scanDetail.cnNeScannerStaleMetadataOnlyEnabled = staleMetadataOnly;

        static uint64_t cachedCnNeEntityListRootModuleBase = 0;
        static uint64_t cachedCnNeEntityListRootPointer = 0;
        static DWORD cachedCnNeEntityListRootTick = 0;
        if (rootCacheEnabled &&
            cachedCnNeEntityListRootModuleBase != 0 &&
            cachedCnNeEntityListRootModuleBase != SDK->dwGameBase) {
            cachedCnNeEntityListRootModuleBase = 0;
            cachedCnNeEntityListRootPointer = 0;
            cachedCnNeEntityListRootTick = 0;
        }

        const auto rootPhaseStartedAt = std::chrono::steady_clock::now();
        uint64_t entity_list = 0;
        const DWORD rootCacheNowTick = GetTickCount();
        const bool rootCacheContextMatch =
            rootCacheEnabled &&
            cachedCnNeEntityListRootModuleBase == SDK->dwGameBase &&
            cachedCnNeEntityListRootPointer != 0;
        const DWORD rootCacheAgeMs =
            rootCacheContextMatch ?
                (rootCacheNowTick - cachedCnNeEntityListRootTick) : 0;
        if (rootCacheContextMatch &&
            rootCacheAgeMs <= rootCacheTtlMs) {
            entity_list = cachedCnNeEntityListRootPointer;
            scanDetail.cnNeEntityListRootCacheHitCount = 1;
        } else if (
            rootCacheContextMatch &&
            staleMetadataMs > 0 &&
            (staleMetadataOnly ||
             static_cast<uint64_t>(rootCacheAgeMs) <=
                static_cast<uint64_t>(rootCacheTtlMs) + staleMetadataMs)) {
            entity_list = cachedCnNeEntityListRootPointer;
            scanDetail.cnNeEntityListRootCacheExpiredCount = 1;
            scanDetail.cnNeEntityListRootCacheStaleHitCount = 1;
        } else {
            if (rootCacheContextMatch) {
                scanDetail.cnNeEntityListRootCacheExpiredCount = 1;
            }
            scanDetail.cnNeEntityListRootCacheReadCount = 1;
            const Diagnostics::ScopedDmaCallsite rootCallsite(
                Diagnostics::DmaCallsite::EntityScanRoot);
            entity_list = SDK->RPM<uint64_t>(
                SDK->dwGameBase + offset::Active().Address_entity_base);
            if (rootCacheEnabled && entity_list != 0) {
                cachedCnNeEntityListRootModuleBase = SDK->dwGameBase;
                cachedCnNeEntityListRootPointer = entity_list;
                cachedCnNeEntityListRootTick = rootCacheNowTick;
                scanDetail.cnNeEntityListRootCacheStoreCount = 1;
            }
        }
        scanDetail.entityList = entity_list;
        if (!entity_list) {
            scanDetail.scanRootMs += elapsedMs(rootPhaseStartedAt);
            finishScanDetail();
            Diagnostics::Trace("Entity scan skipped: entity list pointer is null.");
            return result;
        }

        std::vector<EntityScanRecord> records{};
        std::unordered_map<uint32_t, std::vector<uint64_t>> match_index{};
        std::unordered_set<uint64_t> seen_entities{};
        records.reserve(4096);
        match_index.reserve(4096);
        seen_entities.reserve(4096);

        const size_t kEntityListReadSize =
            cnNeProfile ? 0x40000 : 0x1E000;
        const size_t kEntityListChunkSize =
            cnNeProfile ? EntityScannerCnNeEntityListChunkSize() : 0x1000;
        scanDetail.cnNeEntityListChunkSize = cnNeProfile ? kEntityListChunkSize : 0;
        constexpr size_t kEntityListFallbackChunkSize = 0x1000;
        const bool lightScanRequested = EntityLightScanRequested();
        const bool lightScan = lightScanRequested;
        const size_t lightScanMaxCandidates =
            lightScan ? EntityLightScanMaxCandidates() : 0;
        const bool cnNeMapCandidateCacheEnabled =
            cnNeProfile &&
            EntityScannerEnvFlagEnabled("UN_DMA_CN_NE_STAGE4B_MAP_CACHE");
        scanDetail.cnNeMapCandidateCacheEnabled = cnNeMapCandidateCacheEnabled;
        const bool cnNeMapDiagEnabled =
            cnNeProfile && EntityScannerEnvFlagEnabled("UN_DMA_CN_NE_MAP_DIAG");
        scanDetail.cnNeMapDiagEnabled = cnNeMapDiagEnabled;
        const bool cnNeRecordMatchIdFromHeaderEnabled =
            cnNeProfile &&
            EntityScannerEnvFlagEnabled("UN_DMA_CN_NE_RECORD_MATCH_ID_FROM_HEADER");
        scanDetail.cnNeRecordMatchIdFromHeaderEnabled = cnNeRecordMatchIdFromHeaderEnabled;
        static uint64_t cachedModuleBase = 0;
        static uint64_t cachedModuleSize = 0;
        if (cnNeProfile && cachedModuleBase != SDK->dwGameBase) {
            cachedModuleBase = SDK->dwGameBase;
            cachedModuleSize = static_cast<uint64_t>(mem.GetBaseSize("Overwatch.exe"));
        }
        const uint64_t moduleSize = cnNeProfile ? cachedModuleSize : 0;
        scanDetail.scanRootMs += elapsedMs(rootPhaseStartedAt);

        struct PersistentCnNeRecordSnapshotCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
            EntityScanRecord record{};
        };
        static std::unordered_map<uint64_t, PersistentCnNeRecordSnapshotCacheEntry>
            persistentCnNeRecordSnapshotCache{};
        static uint64_t persistentCnNeRecordSnapshotEntityList = 0;
        static uint64_t persistentCnNeRecordSnapshotModuleBase = 0;
        static uint64_t persistentCnNeRecordSnapshotModuleSize = 0;
        const DWORD recordSnapshotCacheTtlMs =
            cnNeProfile ? EntityScannerRecordSnapshotCacheTtlMs() : 0;
        const bool recordSnapshotCacheEnabled =
            cnNeProfile && recordSnapshotCacheTtlMs > 0;
        const size_t recordSnapshotCacheRefreshBudget =
            recordSnapshotCacheEnabled ? EntityScannerRecordSnapshotCacheRefreshBudget() : 0;
        const bool recordSnapshotCacheRefreshBudgetEnabled =
            recordSnapshotCacheRefreshBudget > 0;
        size_t recordSnapshotCacheRefreshCount = 0;
        scanDetail.cnNeRecordSnapshotCacheEnabled = recordSnapshotCacheEnabled;
        scanDetail.cnNeRecordSnapshotCacheTtlMs = recordSnapshotCacheTtlMs;
        scanDetail.cnNeRecordSnapshotCacheRefreshBudget =
            recordSnapshotCacheRefreshBudget;
        if (recordSnapshotCacheEnabled &&
            (persistentCnNeRecordSnapshotEntityList != entity_list ||
             persistentCnNeRecordSnapshotModuleBase != SDK->dwGameBase ||
             persistentCnNeRecordSnapshotModuleSize != moduleSize)) {
            persistentCnNeRecordSnapshotCache.clear();
            persistentCnNeRecordSnapshotEntityList = entity_list;
            persistentCnNeRecordSnapshotModuleBase = SDK->dwGameBase;
            persistentCnNeRecordSnapshotModuleSize = moduleSize;
        }
        if (recordSnapshotCacheEnabled &&
            persistentCnNeRecordSnapshotCache.size() > 8192) {
            persistentCnNeRecordSnapshotCache.clear();
        }

        static std::unordered_map<uint64_t, DWORD>
            persistentCnNeEntityListReadNegativeCache{};
        static uint64_t persistentCnNeEntityListReadNegativeCacheEntityList = 0;
        static uint64_t persistentCnNeEntityListReadNegativeCacheModuleBase = 0;
        const DWORD listReadNegativeCacheTtlMs =
            cnNeProfile ? EntityScannerEntityListReadNegativeCacheTtlMs() : 0;
        const bool listReadNegativeCacheEnabled =
            cnNeProfile && listReadNegativeCacheTtlMs > 0;
        scanDetail.cnNeEntityListReadNegativeCacheEnabled =
            listReadNegativeCacheEnabled;
        scanDetail.cnNeEntityListReadNegativeCacheTtlMs =
            listReadNegativeCacheTtlMs;
        if (listReadNegativeCacheEnabled &&
            (persistentCnNeEntityListReadNegativeCacheEntityList != entity_list ||
             persistentCnNeEntityListReadNegativeCacheModuleBase != SDK->dwGameBase)) {
            persistentCnNeEntityListReadNegativeCache.clear();
            persistentCnNeEntityListReadNegativeCacheEntityList = entity_list;
            persistentCnNeEntityListReadNegativeCacheModuleBase = SDK->dwGameBase;
        }
        if (listReadNegativeCacheEnabled &&
            persistentCnNeEntityListReadNegativeCache.size() > 512) {
            persistentCnNeEntityListReadNegativeCache.clear();
        }
        auto entityListReadNegativeCacheKey = [](size_t offset, size_t size) {
            return static_cast<uint64_t>(offset) ^
                (static_cast<uint64_t>(size) << 32);
        };
        auto shouldSkipEntityListRead = [&](size_t offset, size_t size) {
            if (!listReadNegativeCacheEnabled)
                return false;
            ++scanDetail.cnNeEntityListReadNegativeCacheLookupCount;
            const uint64_t key = entityListReadNegativeCacheKey(offset, size);
            auto it = persistentCnNeEntityListReadNegativeCache.find(key);
            if (it == persistentCnNeEntityListReadNegativeCache.end())
                return false;
            const DWORD now = GetTickCount();
            const DWORD ageMs = now - it->second;
            const bool fresh = ageMs <= listReadNegativeCacheTtlMs;
            const bool staleAllowed =
                !fresh &&
                staleMetadataMs > 0 &&
                (staleMetadataOnly ||
                 static_cast<uint64_t>(ageMs) <=
                    static_cast<uint64_t>(listReadNegativeCacheTtlMs) +
                        staleMetadataMs);
            if (fresh || staleAllowed) {
                ++scanDetail.cnNeEntityListReadNegativeCacheHitCount;
                if (staleAllowed)
                    ++scanDetail.cnNeEntityListReadNegativeCacheStaleHitCount;
                ++scanDetail.listReadSkippedCount;
                return true;
            }
            ++scanDetail.cnNeEntityListReadNegativeCacheExpiredCount;
            persistentCnNeEntityListReadNegativeCache.erase(it);
            return false;
        };
        auto rememberEntityListReadFailure = [&](size_t offset, size_t size) {
            if (!listReadNegativeCacheEnabled)
                return;
            persistentCnNeEntityListReadNegativeCache.insert_or_assign(
                entityListReadNegativeCacheKey(offset, size),
                GetTickCount());
            ++scanDetail.cnNeEntityListReadNegativeCacheStoreCount;
        };

        struct PersistentCnNeEntityListReadCacheEntry {
            DWORD updateTick = 0;
            std::vector<uint8_t> bytes{};
        };
        static std::unordered_map<uint64_t, PersistentCnNeEntityListReadCacheEntry>
            persistentCnNeEntityListReadCache{};
        static uint64_t persistentCnNeEntityListReadCacheEntityList = 0;
        static uint64_t persistentCnNeEntityListReadCacheModuleBase = 0;
        const DWORD listReadCacheTtlMs =
            cnNeProfile ? EntityScannerEntityListReadCacheTtlMs() : 0;
        const bool listReadCacheEnabled =
            cnNeProfile && listReadCacheTtlMs > 0;
        scanDetail.cnNeEntityListReadCacheEnabled = listReadCacheEnabled;
        scanDetail.cnNeEntityListReadCacheTtlMs = listReadCacheTtlMs;
        if (listReadCacheEnabled &&
            (persistentCnNeEntityListReadCacheEntityList != entity_list ||
             persistentCnNeEntityListReadCacheModuleBase != SDK->dwGameBase)) {
            persistentCnNeEntityListReadCache.clear();
            persistentCnNeEntityListReadCacheEntityList = entity_list;
            persistentCnNeEntityListReadCacheModuleBase = SDK->dwGameBase;
        }
        if (listReadCacheEnabled &&
            persistentCnNeEntityListReadCache.size() > 512) {
            persistentCnNeEntityListReadCache.clear();
        }
        auto loadEntityListReadCache =
            [&](size_t offset, size_t size, std::vector<uint8_t>& destination) {
                if (!listReadCacheEnabled)
                    return false;
                ++scanDetail.cnNeEntityListReadCacheLookupCount;
                const uint64_t key = entityListReadNegativeCacheKey(offset, size);
                auto it = persistentCnNeEntityListReadCache.find(key);
                if (it == persistentCnNeEntityListReadCache.end())
                    return false;

                const DWORD now = GetTickCount();
                PersistentCnNeEntityListReadCacheEntry& entry = it->second;
                const DWORD ageMs = now - entry.updateTick;
                if (entry.bytes.size() != size) {
                    ++scanDetail.cnNeEntityListReadCacheExpiredCount;
                    persistentCnNeEntityListReadCache.erase(it);
                    return false;
                }

                if (ageMs > listReadCacheTtlMs) {
                    ++scanDetail.cnNeEntityListReadCacheExpiredCount;
                    if (staleMetadataMs == 0 ||
                        (!staleMetadataOnly &&
                         static_cast<uint64_t>(ageMs) >
                            static_cast<uint64_t>(listReadCacheTtlMs) +
                                staleMetadataMs)) {
                        persistentCnNeEntityListReadCache.erase(it);
                        return false;
                    }

                    std::memcpy(destination.data(), entry.bytes.data(), size);
                    ++scanDetail.cnNeEntityListReadCacheHitCount;
                    ++scanDetail.cnNeEntityListReadCacheStaleHitCount;
                    return true;
                }

                std::memcpy(destination.data(), entry.bytes.data(), size);
                ++scanDetail.cnNeEntityListReadCacheHitCount;
                return true;
            };
        auto rememberEntityListReadSuccess =
            [&](size_t offset, size_t size, const std::vector<uint8_t>& source) {
                if (!listReadCacheEnabled)
                    return;
                PersistentCnNeEntityListReadCacheEntry entry{};
                entry.updateTick = GetTickCount();
                entry.bytes.assign(source.begin(), source.begin() + size);
                persistentCnNeEntityListReadCache.insert_or_assign(
                    entityListReadNegativeCacheKey(offset, size),
                    std::move(entry));
                ++scanDetail.cnNeEntityListReadCacheStoreCount;
            };

        auto addRecord = [&](uint64_t possible_common) {
            const auto recordBuildStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite recordBuildCallsite(
                Diagnostics::DmaCallsite::EntityScanRecordBuild);
            ++scanDetail.recordAddAttemptCount;
            auto finishRecordBuild = [&]() {
                scanDetail.scanRecordBuildMs += elapsedMs(recordBuildStartedAt);
            };
            if (!possible_common)
            {
                finishRecordBuild();
                return;
            }
            if (cnNeProfile &&
                !IsLikelyCnNeEntityParent(
                    possible_common,
                    entity_list,
                    kEntityListReadSize,
                    SDK->dwGameBase,
                    moduleSize)) {
                finishRecordBuild();
                return;
            }

            if (recordSnapshotCacheEnabled) {
                ++scanDetail.cnNeRecordSnapshotCacheLookupCount;
                const DWORD now = GetTickCount();
                auto snapshotIt = persistentCnNeRecordSnapshotCache.find(possible_common);
                if (snapshotIt != persistentCnNeRecordSnapshotCache.end()) {
                    const PersistentCnNeRecordSnapshotCacheEntry& entry =
                        snapshotIt->second;
                    const bool sameContext =
                        entry.entityList == entity_list &&
                        entry.moduleBase == SDK->dwGameBase &&
                        entry.moduleSize == moduleSize;
                    const DWORD ageMs = now - entry.updateTick;
                    const bool fresh = sameContext && ageMs <= recordSnapshotCacheTtlMs;
                    const bool staleMetadataAllowed =
                        sameContext &&
                        !fresh &&
                        staleMetadataMs > 0 &&
                        (staleMetadataOnly || ageMs <= staleMetadataMs);
                    const bool staleAllowed =
                        staleMetadataAllowed ||
                        (sameContext &&
                         !fresh &&
                         recordSnapshotCacheRefreshBudgetEnabled &&
                         recordSnapshotCacheRefreshCount >= recordSnapshotCacheRefreshBudget);
                    if (fresh || staleAllowed) {
                        EntityScanRecord cached = entry.record;
                        if (cached.entity == possible_common && cached.unique_id != 0) {
                            if (!seen_entities.insert(possible_common).second) {
                                ++scanDetail.recordDuplicateCount;
                                finishRecordBuild();
                                return;
                            }
                            ++scanDetail.cnNeRecordSnapshotCacheHitCount;
                            if (staleAllowed)
                                ++scanDetail.cnNeRecordSnapshotCacheStaleHitCount;
                            match_index[cached.unique_id].push_back(cached.entity);
                            records.push_back(std::move(cached));
                            finishRecordBuild();
                            return;
                        }
                    }
                    if (sameContext)
                        ++scanDetail.cnNeRecordSnapshotCacheExpiredCount;
                    persistentCnNeRecordSnapshotCache.erase(snapshotIt);
                }
            }
            if (recordSnapshotCacheEnabled) {
                ++recordSnapshotCacheRefreshCount;
                ++scanDetail.cnNeRecordSnapshotCacheRefreshCount;
            }

            EntityScanRecord record{};
            record.entity = possible_common;
            bool recordHeaderLoaded = false;
            auto readRecordHeader = [&]() {
                if (recordHeaderLoaded)
                    return;
                ++scanDetail.recordHeaderReadCount;
                {
                    const Diagnostics::ScopedDmaCallsite headerCallsite(
                        Diagnostics::DmaCallsite::EntityScanRecordHeader);
                    record.header.Read(possible_common);
                }
                if (!record.header.valid)
                    ++scanDetail.recordHeaderFailCount;
                recordHeaderLoaded = true;
            };

            bool headerMatchIdObserved = false;
            bool headerHasUniqueId = false;
            uint32_t headerUniqueId = 0;
            auto observeHeaderMatchId = [&](uint32_t compareValue, bool haveCompareValue) {
                if (headerMatchIdObserved)
                    return;
                headerHasUniqueId =
                    record.header.ReadParentOffset(offset::Entity_MatchId, headerUniqueId);
                if (headerHasUniqueId) {
                    ++scanDetail.recordMatchIdHeaderHitCount;
                    if (haveCompareValue) {
                        if (headerUniqueId == compareValue)
                            ++scanDetail.recordMatchIdHeaderMatchCount;
                        else
                            ++scanDetail.recordMatchIdHeaderMismatchCount;
                    }
                } else {
                    ++scanDetail.recordMatchIdHeaderMissCount;
                }
                headerMatchIdObserved = true;
            };

            uint32_t cnNeUniqueId = 0;
            if (cnNeProfile && cnNeRecordMatchIdFromHeaderEnabled) {
                readRecordHeader();
                observeHeaderMatchId(0, false);
                if (headerHasUniqueId && headerUniqueId != 0) {
                    cnNeUniqueId = headerUniqueId;
                    ++scanDetail.recordMatchIdHeaderUseCount;
                } else {
                    ++scanDetail.recordRemoteFallbackReadCount;
                    ++scanDetail.recordMatchIdDirectReadCount;
                    {
                        const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                            Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                        cnNeUniqueId = SDK->RPM<uint32_t>(
                            possible_common + offset::Entity_MatchId);
                    }
                    if (cnNeUniqueId == 0) {
                        ++scanDetail.recordMatchIdDirectZeroCount;
                        finishRecordBuild();
                        return;
                    }
                }
            } else if (cnNeProfile) {
                ++scanDetail.recordRemoteFallbackReadCount;
                ++scanDetail.recordMatchIdDirectReadCount;
                {
                    const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                        Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                    cnNeUniqueId = SDK->RPM<uint32_t>(
                        possible_common + offset::Entity_MatchId);
                }
                if (cnNeUniqueId == 0) {
                    ++scanDetail.recordMatchIdDirectZeroCount;
                    finishRecordBuild();
                    return;
                }
            }

            if (!seen_entities.insert(possible_common).second) {
                ++scanDetail.recordDuplicateCount;
                finishRecordBuild();
                return;
            }

            readRecordHeader();
            observeHeaderMatchId(cnNeUniqueId, cnNeProfile && cnNeUniqueId != 0);

            if (cnNeProfile) {
                record.unique_id = cnNeUniqueId;
            } else if (headerHasUniqueId) {
                record.unique_id = headerUniqueId;
            } else {
                ++scanDetail.recordRemoteFallbackReadCount;
                ++scanDetail.recordMatchIdDirectReadCount;
                const Diagnostics::ScopedDmaCallsite matchIdCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordMatchId);
                record.unique_id = SDK->RPM<uint32_t>(possible_common + offset::Entity_MatchId);
                if (record.unique_id == 0)
                    ++scanDetail.recordMatchIdDirectZeroCount;
            }

            uint64_t ptr_value = 0;
            if (!record.header.ReadParentOffset(offset::Entity_PoolPtr, ptr_value)) {
                ++scanDetail.recordRemoteFallbackReadCount;
                const Diagnostics::ScopedDmaCallsite poolPtrCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordPoolPtr);
                ptr_value = SDK->RPM<uint64_t>(possible_common + offset::Entity_PoolPtr);
            }

            record.ptr = ptr_value & 0xFFFFFFFFFFFFFFC0;
            if (record.ptr && record.ptr < 0xFFFFFFFFFFFFFFEF) {
                ++scanDetail.poolIdReadCount;
                const Diagnostics::ScopedDmaCallsite poolIdCallsite(
                    Diagnostics::DmaCallsite::EntityScanRecordPoolId);
                record.entity_id = SDK->RPM<uint64_t>(record.ptr + offset::Pool_PoolId);
            }

            if (record.unique_id != 0)
                match_index[record.unique_id].push_back(record.entity);
            if (recordSnapshotCacheEnabled && record.unique_id != 0) {
                PersistentCnNeRecordSnapshotCacheEntry entry{};
                entry.entityList = entity_list;
                entry.moduleBase = SDK->dwGameBase;
                entry.moduleSize = moduleSize;
                entry.updateTick = GetTickCount();
                entry.record = record;
                persistentCnNeRecordSnapshotCache.insert_or_assign(
                    record.entity,
                    entry);
                ++scanDetail.cnNeRecordSnapshotCacheStoreCount;
            }
            records.push_back(std::move(record));
            finishRecordBuild();
        };

        std::vector<uint8_t> entity_chunk(kEntityListChunkSize);
        size_t readable_bytes = 0;
        size_t readable_chunks = 0;
        size_t slots_scanned = 0;
        size_t nonzero_slots = 0;
        size_t plausible_slots = 0;
        auto processEntityChunk = [&](const std::vector<uint8_t>& chunk,
                                      size_t chunk_size) {
            for (size_t slot_offset = 0; slot_offset + sizeof(Entity) <= chunk_size;
                 slot_offset += sizeof(Entity)) {
                const auto slotWalkStartedAt = std::chrono::steady_clock::now();
                uint64_t possible_common = 0;
                std::memcpy(&possible_common, chunk.data() + slot_offset, sizeof(possible_common));
                ++slots_scanned;
                if (possible_common)
                    ++nonzero_slots;
                if (IsPlausibleUserPointer(possible_common))
                    ++plausible_slots;
                scanDetail.scanSlotWalkMs += elapsedMs(slotWalkStartedAt);
                addRecord(possible_common);
            }
        };

        for (size_t offset = 0; offset < kEntityListReadSize; offset += kEntityListChunkSize) {
            const size_t remaining = kEntityListReadSize - offset;
            const size_t chunk_size =
                remaining < kEntityListChunkSize ? remaining : kEntityListChunkSize;
            bool chunkRead = loadEntityListReadCache(offset, chunk_size, entity_chunk);
            const bool chunkSkipped = chunkRead ? false : shouldSkipEntityListRead(offset, chunk_size);
            if (!chunkSkipped) {
                if (!chunkRead) {
                    ++scanDetail.listReadCount;
                    const auto listReadStartedAt = std::chrono::steady_clock::now();
                    {
                        const Diagnostics::ScopedDmaCallsite listReadCallsite(
                            Diagnostics::DmaCallsite::EntityScanListRead);
                        chunkRead = mem.Read(entity_list + offset, entity_chunk.data(), chunk_size);
                    }
                    scanDetail.scanListReadMs += elapsedMs(listReadStartedAt);
                    if (chunkRead)
                        rememberEntityListReadSuccess(offset, chunk_size, entity_chunk);
                }
            }
            if (chunkRead) {
                readable_bytes += chunk_size;
                ++readable_chunks;
                processEntityChunk(entity_chunk, chunk_size);
                continue;
            }
            if (!chunkSkipped) {
                ++scanDetail.listReadFailCount;
                rememberEntityListReadFailure(offset, chunk_size);
            }

            if (!cnNeProfile || kEntityListFallbackChunkSize >= chunk_size)
                continue;

            for (size_t sub_offset = 0; sub_offset < chunk_size;
                 sub_offset += kEntityListFallbackChunkSize) {
                const size_t sub_remaining = chunk_size - sub_offset;
                const size_t sub_size =
                    sub_remaining < kEntityListFallbackChunkSize
                        ? sub_remaining
                        : kEntityListFallbackChunkSize;
                const size_t absoluteSubOffset = offset + sub_offset;
                bool subChunkRead =
                    loadEntityListReadCache(absoluteSubOffset, sub_size, entity_chunk);
                const bool subChunkSkipped = subChunkRead
                    ? false
                    : shouldSkipEntityListRead(absoluteSubOffset, sub_size);
                if (!subChunkSkipped) {
                    if (!subChunkRead) {
                        ++scanDetail.listReadCount;
                        ++scanDetail.listFallbackReadCount;
                        const auto subListReadStartedAt = std::chrono::steady_clock::now();
                        {
                            const Diagnostics::ScopedDmaCallsite listReadCallsite(
                                Diagnostics::DmaCallsite::EntityScanListRead);
                            subChunkRead = mem.Read(entity_list + absoluteSubOffset,
                                    entity_chunk.data(),
                                    sub_size);
                        }
                        scanDetail.scanListReadMs += elapsedMs(subListReadStartedAt);
                        if (subChunkRead)
                            rememberEntityListReadSuccess(
                                absoluteSubOffset,
                                sub_size,
                                entity_chunk);
                    }
                }
                if (!subChunkRead) {
                    if (!subChunkSkipped) {
                        ++scanDetail.listReadFailCount;
                        rememberEntityListReadFailure(absoluteSubOffset, sub_size);
                    }
                    continue;
                }
                readable_bytes += sub_size;
                ++readable_chunks;
                processEntityChunk(entity_chunk, sub_size);
            }
        }

        scanDetail.readableBytes = readable_bytes;
        scanDetail.readableChunks = readable_chunks;
        scanDetail.slotsScanned = slots_scanned;
        scanDetail.nonZeroSlots = nonzero_slots;
        scanDetail.plausibleSlots = plausible_slots;
        scanDetail.records = records.size();
        scanDetail.matchIds = match_index.size();
        scanDetail.lightScanRequested = lightScanRequested;
        scanDetail.lightScanEnabled = lightScan;
        scanDetail.lightScanPairCap = lightScanMaxCandidates;

        if (records.empty()) {
            Diagnostics::Trace("Entity scan skipped: no records from list=0x%llX chunks=%zu bytes=%zu.",
                static_cast<unsigned long long>(entity_list),
                readable_chunks,
                readable_bytes);
            finishScanDetail();
            return result;
        }

        auto addPair = [&](uint64_t component_parent,
                           uint64_t link_parent,
                           bool lightCandidate = false) {
            ++scanDetail.addPairAttemptCount;
            if (!component_parent || !link_parent)
                return false;
            if (lightCandidate &&
                lightScanMaxCandidates != 0 &&
                result.size() >= lightScanMaxCandidates) {
                ++scanDetail.lightScanCapHits;
                return false;
            }
            const auto pair = std::make_pair(component_parent, link_parent);
            if (std::find(result.begin(), result.end(), pair) != result.end()) {
                ++scanDetail.addPairDuplicateCount;
                return false;
            }
            result.push_back(pair);
            if (lightCandidate)
                ++scanDetail.lightScanUnvalidatedPairs;
            return true;
        };

        auto hasPlayableComponents = [&](uint64_t component_parent) {
            ++scanDetail.playableValidationAttemptCount;
            if (!component_parent)
                return false;
            const uint64_t health =
                DecryptComponent(component_parent, TYPE_HEALTH);
            if (!health)
                return false;
            const uint64_t velocity =
                DecryptComponent(component_parent, TYPE_VELOCITY);
            if (!velocity)
                return false;
            ++scanDetail.playableValidationSuccessCount;
            return true;
        };

        auto hasCnNePlayableComponents = [&](const EntityScanRecord& current) {
            const auto validationStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite validationCallsite(
                Diagnostics::DmaCallsite::EntityScanSelfValidation);
            ++scanDetail.playableValidationAttemptCount;
            auto finishValidation = [&]() {
                scanDetail.scanCnNeSelfValidationMs += elapsedMs(validationStartedAt);
            };
            if (!current.entity)
            {
                finishValidation();
                return false;
            }
            const EntityHeaderSnapshot* snapshot =
                current.header.valid ? &current.header : nullptr;
            auto rememberReject = [&](int reason,
                                      uint64_t healthBase,
                                      uint64_t heroBase,
                                      uint64_t heroId,
                                      uint64_t velocityBase,
                                      uint64_t boneBase,
                                      float healthValue,
                                      float healthMaxValue) {
                if (scanDetail.sampleRejectParent != 0)
                    return;
                scanDetail.sampleRejectReason = reason;
                scanDetail.sampleRejectParent = current.entity;
                scanDetail.sampleRejectMatchId = current.unique_id;
                scanDetail.sampleRejectHealthBase = healthBase;
                scanDetail.sampleRejectHeroBase = heroBase;
                scanDetail.sampleRejectHeroId = heroId;
                scanDetail.sampleRejectVelocityBase = velocityBase;
                scanDetail.sampleRejectBoneBase = boneBase;
                scanDetail.sampleRejectHealthCm = std::isfinite(healthValue)
                    ? static_cast<int>(healthValue * 100.0f)
                    : 0;
                scanDetail.sampleRejectHealthMaxCm = std::isfinite(healthMaxValue)
                    ? static_cast<int>(healthMaxValue * 100.0f)
                    : 0;
            };

            const uint64_t health =
                DecryptComponent(current.entity, TYPE_HEALTH, snapshot);
            if (IsPlausibleUserPointer(health))
                ++scanDetail.selfHealthBase;
            if (!IsPlausibleUserPointer(health)) {
                rememberReject(1, health, 0, 0, 0, 0, 0.0f, 0.0f);
                finishValidation();
                return false;
            }

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo))) {
                rememberReject(2, health, 0, 0, 0, 0, 0.0f, 0.0f);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHealthRead;

            const Vector2 health_ext = health_compo.health_ext;
            const float totalHealth =
                health_compo.health + health_compo.armor + health_compo.barrier + health_ext.Y;
            const float totalHealthMax =
                health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + health_ext.X;
            if (!std::isfinite(totalHealth) ||
                !std::isfinite(totalHealthMax) ||
                totalHealth <= 0.0f ||
                totalHealthMax <= 0.0f ||
                totalHealthMax > 10000.0f ||
                totalHealth > totalHealthMax + 2000.0f) {
                rememberReject(3, health, 0, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHealthPlausible;

            const uint64_t hero =
                DecryptComponent(current.entity, TYPE_P_HEROID, snapshot);
            if (IsPlausibleUserPointer(hero))
                ++scanDetail.selfHeroBase;
            if (!IsPlausibleUserPointer(hero)) {
                rememberReject(4, health, hero, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            hero_compo_t hero_compo{};
            if (!SDK->read_range(hero, &hero_compo, sizeof(hero_compo))) {
                rememberReject(5, health, hero, 0, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHeroRead;
            if (!GameData::IsKnownHeroId(hero_compo.heroid)) {
                rememberReject(6, health, hero, hero_compo.heroid, 0, 0, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfHeroKnown;

            const uint64_t velocity =
                DecryptComponent(current.entity, TYPE_VELOCITY, snapshot);
            const uint64_t bone =
                DecryptComponent(current.entity, TYPE_BONE, snapshot);
            if (IsPlausibleUserPointer(velocity))
                ++scanDetail.selfVelocityBase;
            if (IsPlausibleUserPointer(bone))
                ++scanDetail.selfBoneBase;
            if (!IsPlausibleUserPointer(velocity) && !IsPlausibleUserPointer(bone)) {
                rememberReject(7, health, hero, hero_compo.heroid, velocity, bone, totalHealth, totalHealthMax);
                finishValidation();
                return false;
            }
            ++scanDetail.selfPlayable;
            ++scanDetail.playableValidationSuccessCount;
            finishValidation();
            return true;
        };

        struct PersistentCnNeComponentNegativeCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
        };
        static std::unordered_map<uint64_t, PersistentCnNeComponentNegativeCacheEntry>
            persistentCnNeComponentNegativeCache{};
        static uint64_t persistentCnNeComponentNegativeEntityList = 0;
        static uint64_t persistentCnNeComponentNegativeModuleBase = 0;
        static uint64_t persistentCnNeComponentNegativeModuleSize = 0;
        const DWORD componentNegativeCacheTtlMs =
            cnNeProfile ? EntityScannerComponentNegativeCacheTtlMs() : 0;
        const bool componentNegativeCacheEnabled =
            cnNeProfile && componentNegativeCacheTtlMs > 0;
        const size_t componentNegativeCacheRefreshBudget =
            componentNegativeCacheEnabled ? EntityScannerComponentNegativeCacheRefreshBudget() : 0;
        const bool componentNegativeCacheRefreshBudgetEnabled =
            componentNegativeCacheRefreshBudget > 0;
        size_t componentNegativeCacheRefreshCount = 0;
        scanDetail.cnNeComponentNegativeCacheEnabled = componentNegativeCacheEnabled;
        scanDetail.cnNeComponentNegativeCacheTtlMs = componentNegativeCacheTtlMs;
        scanDetail.cnNeComponentNegativeCacheRefreshBudget =
            componentNegativeCacheRefreshBudget;
        if (componentNegativeCacheEnabled &&
            (persistentCnNeComponentNegativeEntityList != entity_list ||
             persistentCnNeComponentNegativeModuleBase != SDK->dwGameBase ||
             persistentCnNeComponentNegativeModuleSize != moduleSize)) {
            persistentCnNeComponentNegativeCache.clear();
            persistentCnNeComponentNegativeEntityList = entity_list;
            persistentCnNeComponentNegativeModuleBase = SDK->dwGameBase;
            persistentCnNeComponentNegativeModuleSize = moduleSize;
        }
        if (componentNegativeCacheEnabled &&
            persistentCnNeComponentNegativeCache.size() > 8192) {
            persistentCnNeComponentNegativeCache.clear();
        }

        auto hasCnNeComponentOnlyPlayable = [&](const EntityScanRecord& source,
                                                uint64_t component_parent) {
            const auto validationStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite validationCallsite(
                Diagnostics::DmaCallsite::EntityScanComponentValidation);
            ++scanDetail.componentOnlyValidationAttemptCount;
            auto finishValidation = [&]() {
                scanDetail.scanComponentOnlyValidationMs += elapsedMs(validationStartedAt);
            };
            if (!component_parent)
            {
                finishValidation();
                return false;
            }

            if (componentNegativeCacheEnabled) {
                ++scanDetail.cnNeComponentNegativeCacheLookupCount;
                const DWORD now = GetTickCount();
                auto negativeIt = persistentCnNeComponentNegativeCache.find(component_parent);
                if (negativeIt != persistentCnNeComponentNegativeCache.end()) {
                    const PersistentCnNeComponentNegativeCacheEntry& entry =
                        negativeIt->second;
                    const bool sameContext =
                        entry.entityList == entity_list &&
                        entry.moduleBase == SDK->dwGameBase &&
                        entry.moduleSize == moduleSize;
                    const DWORD ageMs = now - entry.updateTick;
                    const bool fresh = sameContext && ageMs <= componentNegativeCacheTtlMs;
                    const bool staleMetadataAllowed =
                        sameContext &&
                        !fresh &&
                        staleMetadataMs > 0 &&
                        (staleMetadataOnly || ageMs <= staleMetadataMs);
                    const bool staleAllowed =
                        staleMetadataAllowed ||
                        (sameContext &&
                         !fresh &&
                         componentNegativeCacheRefreshBudgetEnabled &&
                         componentNegativeCacheRefreshCount >= componentNegativeCacheRefreshBudget);
                    if (fresh || staleAllowed) {
                        ++scanDetail.cnNeComponentNegativeCacheHitCount;
                        if (staleAllowed)
                            ++scanDetail.cnNeComponentNegativeCacheStaleHitCount;
                        finishValidation();
                        return false;
                    }
                    if (sameContext)
                        ++scanDetail.cnNeComponentNegativeCacheExpiredCount;
                    persistentCnNeComponentNegativeCache.erase(negativeIt);
                }
            }
            if (componentNegativeCacheEnabled) {
                ++componentNegativeCacheRefreshCount;
                ++scanDetail.cnNeComponentNegativeCacheRefreshCount;
            }

            EntityHeaderSnapshot componentHeader{};
            const EntityHeaderSnapshot* snapshot =
                componentHeader.Read(component_parent) ? &componentHeader : nullptr;
            auto rememberNegative = [&]() {
                if (!componentNegativeCacheEnabled)
                    return;
                PersistentCnNeComponentNegativeCacheEntry entry{};
                entry.entityList = entity_list;
                entry.moduleBase = SDK->dwGameBase;
                entry.moduleSize = moduleSize;
                entry.updateTick = GetTickCount();
                persistentCnNeComponentNegativeCache.insert_or_assign(
                    component_parent,
                    entry);
                ++scanDetail.cnNeComponentNegativeCacheStoreCount;
            };
            auto failValidation = [&]() {
                rememberNegative();
                finishValidation();
                return false;
            };
            auto rememberReject = [&](int reason,
                                      uint64_t healthBase,
                                      uint64_t velocityBase,
                                      uint64_t boneBase,
                                      float healthValue,
                                      float healthMaxValue) {
                if (scanDetail.sampleRejectParent != 0)
                    return;
                scanDetail.sampleRejectReason = reason;
                scanDetail.sampleRejectParent = component_parent;
                scanDetail.sampleRejectMatchId = source.unique_id;
                scanDetail.sampleRejectHealthBase = healthBase;
                scanDetail.sampleRejectVelocityBase = velocityBase;
                scanDetail.sampleRejectBoneBase = boneBase;
                scanDetail.sampleRejectHealthCm = std::isfinite(healthValue)
                    ? static_cast<int>(healthValue * 100.0f)
                    : 0;
                scanDetail.sampleRejectHealthMaxCm = std::isfinite(healthMaxValue)
                    ? static_cast<int>(healthMaxValue * 100.0f)
                    : 0;
            };

            const uint64_t health =
                DecryptComponent(component_parent, TYPE_HEALTH, snapshot);
            if (IsPlausibleUserPointer(health))
                ++scanDetail.selfHealthBase;
            if (!IsPlausibleUserPointer(health)) {
                rememberReject(11, health, 0, 0, 0.0f, 0.0f);
                return failValidation();
            }

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo))) {
                rememberReject(12, health, 0, 0, 0.0f, 0.0f);
                return failValidation();
            }
            ++scanDetail.selfHealthRead;

            const Vector2 health_ext = health_compo.health_ext;
            const float totalHealth =
                health_compo.health + health_compo.armor + health_compo.barrier + health_ext.Y;
            const float totalHealthMax =
                health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + health_ext.X;
            const bool plausibleHealthLayout =
                std::isfinite(totalHealth) &&
                std::isfinite(totalHealthMax) &&
                totalHealth > 0.0f &&
                totalHealthMax > 0.0f &&
                totalHealthMax <= 10000.0f &&
                totalHealth <= totalHealthMax + 2000.0f;
            if (!plausibleHealthLayout) {
                rememberReject(13, health, 0, 0, totalHealth, totalHealthMax);
                return failValidation();
            }
            ++scanDetail.selfHealthPlausible;

            const bool trainingBotSized =
                std::fabs(totalHealthMax - 200.0f) <= 5.0f ||
                std::fabs(totalHealthMax - 500.0f) <= 10.0f;
            if (!trainingBotSized) {
                rememberReject(14, health, 0, 0, totalHealth, totalHealthMax);
                return failValidation();
            }

            const uint64_t velocity =
                DecryptComponent(component_parent, TYPE_VELOCITY, snapshot);
            const uint64_t team =
                DecryptComponent(component_parent, TYPE_TEAM, snapshot);
            const uint64_t bone =
                DecryptComponent(component_parent, TYPE_BONE, snapshot);
            if (IsPlausibleUserPointer(velocity))
                ++scanDetail.selfVelocityBase;
            if (IsPlausibleUserPointer(bone))
                ++scanDetail.selfBoneBase;
            if (!IsPlausibleUserPointer(velocity)) {
                rememberReject(15, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            velocity_compo_t velocity_compo{};
            if (!SDK->read_range(velocity, &velocity_compo, sizeof(velocity_compo))) {
                rememberReject(16, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }
            const XMFLOAT3 location = velocity_compo.location;
            const bool finiteLocation =
                std::isfinite(location.x) &&
                std::isfinite(location.y) &&
                std::isfinite(location.z) &&
                std::fabs(location.x) < 100000.0f &&
                std::fabs(location.y) < 100000.0f &&
                std::fabs(location.z) < 100000.0f &&
                (std::fabs(location.x) > 0.001f ||
                 std::fabs(location.y) > 0.001f ||
                 std::fabs(location.z) > 0.001f);
            if (!finiteLocation) {
                rememberReject(17, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            bool teamOk = false;
            if (IsPlausibleUserPointer(team)) {
                const uint32_t teamBits =
                    SDK->RPM<uint32_t>(team + 0x58) & 0x0F800000u;
                teamOk = teamBits != 0;
            }
            if (!teamOk && !IsPlausibleUserPointer(bone)) {
                rememberReject(18, health, velocity, bone, totalHealth, totalHealthMax);
                return failValidation();
            }

            if (componentNegativeCacheEnabled)
                persistentCnNeComponentNegativeCache.erase(component_parent);
            ++scanDetail.selfPlayable;
            ++scanDetail.componentOnlyValidationSuccessCount;
            finishValidation();
            return true;
        };

        auto hasCnNePlausibleHealth = [&](uint64_t component_parent) {
            if (!component_parent)
                return false;

            EntityHeaderSnapshot componentHeader{};
            const EntityHeaderSnapshot* snapshot =
                componentHeader.Read(component_parent) ? &componentHeader : nullptr;
            const uint64_t health =
                DecryptComponent(component_parent, TYPE_HEALTH, snapshot);
            if (!IsPlausibleUserPointer(health))
                return false;

            health_compo_t health_compo{};
            if (!SDK->read_range(health, &health_compo, sizeof(health_compo)))
                return false;

            const Vector2 health_ext = health_compo.health_ext;
            const float totalHealth =
                health_compo.health + health_compo.armor + health_compo.barrier + health_ext.Y;
            const float totalHealthMax =
                health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + health_ext.X;
            return std::isfinite(totalHealth) &&
                std::isfinite(totalHealthMax) &&
                totalHealth > 0.0f &&
                totalHealthMax > 0.0f &&
                totalHealthMax <= 10000.0f &&
                totalHealth <= totalHealthMax + 2000.0f;
        };

        auto hasCnNeKnownHero = [&](uint64_t link_parent) {
            if (!link_parent)
                return false;

            EntityHeaderSnapshot linkHeader{};
            const EntityHeaderSnapshot* snapshot =
                linkHeader.Read(link_parent) ? &linkHeader : nullptr;
            const uint64_t hero =
                DecryptComponent(link_parent, TYPE_P_HEROID, snapshot);
            if (!IsPlausibleUserPointer(hero))
                return false;

            hero_compo_t hero_compo{};
            return SDK->read_range(hero, &hero_compo, sizeof(hero_compo)) &&
                GameData::IsKnownHeroId(hero_compo.heroid);
        };

        auto addPlayableMatches = [&](uint32_t match_id,
                                      uint64_t link_parent,
                                      bool trustMatchOnly = false) {
            if (match_id == 0)
                return false;
            ++scanDetail.matchLookupCount;
            const auto match = match_index.find(match_id);
            if (match == match_index.end())
                return false;
            ++scanDetail.matchLookupHitCount;

            bool added = false;
            for (uint64_t component_parent : match->second) {
                if (component_parent == link_parent)
                    continue;
                if (!trustMatchOnly && !hasPlayableComponents(component_parent))
                    continue;
                added = addPair(component_parent, link_parent, trustMatchOnly) || added;
            }
            return added;
        };

        std::unordered_map<uint64_t, std::vector<uint64_t>> cnNeMapCandidateCache{};
        if (cnNeMapCandidateCacheEnabled)
            cnNeMapCandidateCache.reserve(records.size());
        struct PersistentCnNeMapCandidateCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
            std::vector<uint64_t> candidates{};
        };
        static std::unordered_map<uint64_t, PersistentCnNeMapCandidateCacheEntry>
            persistentCnNeMapCandidateCache{};
        static uint64_t persistentCnNeMapCandidateEntityList = 0;
        static uint64_t persistentCnNeMapCandidateModuleBase = 0;
        static uint64_t persistentCnNeMapCandidateModuleSize = 0;
        const DWORD persistentMapCacheTtlMs =
            cnNeMapCandidateCacheEnabled ? EntityScannerPersistentMapCacheTtlMs() : 0;
        const bool persistentMapCacheEnabled =
            cnNeProfile && cnNeMapCandidateCacheEnabled && persistentMapCacheTtlMs > 0;
        const size_t persistentMapCacheRefreshBudget =
            persistentMapCacheEnabled ? EntityScannerPersistentMapCacheRefreshBudget() : 0;
        const bool persistentMapCacheRefreshBudgetEnabled =
            persistentMapCacheRefreshBudget > 0;
        size_t persistentMapCacheRefreshCount = 0;
        scanDetail.cnNeMapCandidatePersistentCacheEnabled = persistentMapCacheEnabled;
        scanDetail.cnNeMapCandidatePersistentCacheTtlMs = persistentMapCacheTtlMs;
        scanDetail.cnNeMapCandidatePersistentCacheRefreshBudget =
            persistentMapCacheRefreshBudget;
        if (persistentMapCacheEnabled &&
            (persistentCnNeMapCandidateEntityList != entity_list ||
             persistentCnNeMapCandidateModuleBase != SDK->dwGameBase ||
             persistentCnNeMapCandidateModuleSize != moduleSize)) {
            persistentCnNeMapCandidateCache.clear();
            persistentCnNeMapCandidateEntityList = entity_list;
            persistentCnNeMapCandidateModuleBase = SDK->dwGameBase;
            persistentCnNeMapCandidateModuleSize = moduleSize;
        }
        if (persistentMapCacheEnabled &&
            persistentCnNeMapCandidateCache.size() > 4096) {
            persistentCnNeMapCandidateCache.clear();
        }
        std::unordered_set<uint64_t> cnNeMapDiagParents{};
        if (cnNeMapDiagEnabled)
            cnNeMapDiagParents.reserve(records.size());

        auto collectCnNeMapBaseCandidatesForParent =
            [&](uint64_t parent) -> std::vector<uint64_t> {
                if (cnNeMapDiagEnabled) {
                    ++scanDetail.cnNeMapCandidateParentLookupCount;
                    if (cnNeMapDiagParents.insert(parent).second)
                        ++scanDetail.cnNeMapCandidateUniqueParentCount;
                    else
                        ++scanDetail.cnNeMapCandidateDuplicateParentCount;
                }

                if (!cnNeMapCandidateCacheEnabled) {
                    return CollectCnNeMapBaseCandidates(
                        parent,
                        entity_list,
                        kEntityListReadSize,
                        SDK->dwGameBase,
                        moduleSize,
                        &scanDetail,
                        cnNeMapDiagEnabled);
                }

                ++scanDetail.cnNeMapCandidateCacheLookupCount;
                const auto cachedIt = cnNeMapCandidateCache.find(parent);
                if (cachedIt != cnNeMapCandidateCache.end()) {
                    ++scanDetail.cnNeMapCandidateCacheHitCount;
                    return cachedIt->second;
                }

                ++scanDetail.cnNeMapCandidateCacheMissCount;
                if (persistentMapCacheEnabled) {
                    ++scanDetail.cnNeMapCandidatePersistentCacheLookupCount;
                    const DWORD now = GetTickCount();
                    auto persistentIt = persistentCnNeMapCandidateCache.find(parent);
                    if (persistentIt != persistentCnNeMapCandidateCache.end()) {
                        const PersistentCnNeMapCandidateCacheEntry& entry =
                            persistentIt->second;
                        const bool sameContext =
                            entry.entityList == entity_list &&
                            entry.moduleBase == SDK->dwGameBase &&
                            entry.moduleSize == moduleSize;
                        const DWORD ageMs = now - entry.updateTick;
                        if (sameContext && ageMs <= persistentMapCacheTtlMs) {
                            ++scanDetail.cnNeMapCandidatePersistentCacheHitCount;
                            cnNeMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        if (sameContext &&
                            staleMetadataMs > 0 &&
                            (staleMetadataOnly || ageMs <= staleMetadataMs) &&
                            !entry.candidates.empty()) {
                            ++scanDetail.cnNeMapCandidatePersistentCacheStaleHitCount;
                            cnNeMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        if (sameContext)
                            ++scanDetail.cnNeMapCandidatePersistentCacheExpiredCount;
                        if (sameContext &&
                            persistentMapCacheRefreshBudgetEnabled &&
                            persistentMapCacheRefreshCount >= persistentMapCacheRefreshBudget &&
                            !entry.candidates.empty()) {
                            ++scanDetail.cnNeMapCandidatePersistentCacheStaleHitCount;
                            cnNeMapCandidateCache.emplace(parent, entry.candidates);
                            return entry.candidates;
                        }
                        persistentCnNeMapCandidateCache.erase(persistentIt);
                    }
                    ++scanDetail.cnNeMapCandidatePersistentCacheMissCount;
                }
                if (persistentMapCacheEnabled) {
                    ++persistentMapCacheRefreshCount;
                    ++scanDetail.cnNeMapCandidatePersistentCacheRefreshCount;
                }
                std::vector<uint64_t> candidates = CollectCnNeMapBaseCandidates(
                    parent,
                    entity_list,
                    kEntityListReadSize,
                    SDK->dwGameBase,
                    moduleSize,
                    &scanDetail,
                    cnNeMapDiagEnabled);
                cnNeMapCandidateCache.emplace(parent, candidates);
                if (persistentMapCacheEnabled) {
                    PersistentCnNeMapCandidateCacheEntry entry{};
                    entry.entityList = entity_list;
                    entry.moduleBase = SDK->dwGameBase;
                    entry.moduleSize = moduleSize;
                    entry.updateTick = GetTickCount();
                    entry.candidates = candidates;
                    persistentCnNeMapCandidateCache.insert_or_assign(parent, std::move(entry));
                    ++scanDetail.cnNeMapCandidatePersistentCacheStoreCount;
                }
                return candidates;
            };

        const bool worldBzProfile = !offset::IsCnNeProfile();
        std::unordered_set<uint64_t> cnNeLinkTargetPairs{};

        struct PersistentCnNeLinkDecryptNegativeCacheEntry {
            uint64_t entityList = 0;
            uint64_t moduleBase = 0;
            uint64_t moduleSize = 0;
            DWORD updateTick = 0;
        };
        static std::unordered_map<uint64_t, PersistentCnNeLinkDecryptNegativeCacheEntry>
            persistentCnNeLinkDecryptNegativeCache{};
        static uint64_t persistentCnNeLinkDecryptNegativeEntityList = 0;
        static uint64_t persistentCnNeLinkDecryptNegativeModuleBase = 0;
        static uint64_t persistentCnNeLinkDecryptNegativeModuleSize = 0;
        const DWORD linkDecryptNegativeCacheTtlMs =
            cnNeProfile ? EntityScannerLinkDecryptNegativeCacheTtlMs() : 0;
        const bool linkDecryptNegativeCacheEnabled =
            cnNeProfile && linkDecryptNegativeCacheTtlMs > 0;
        scanDetail.cnNeLinkDecryptNegativeCacheEnabled =
            linkDecryptNegativeCacheEnabled;
        scanDetail.cnNeLinkDecryptNegativeCacheTtlMs =
            linkDecryptNegativeCacheTtlMs;
        if (linkDecryptNegativeCacheEnabled &&
            (persistentCnNeLinkDecryptNegativeEntityList != entity_list ||
             persistentCnNeLinkDecryptNegativeModuleBase != SDK->dwGameBase ||
             persistentCnNeLinkDecryptNegativeModuleSize != moduleSize)) {
            persistentCnNeLinkDecryptNegativeCache.clear();
            persistentCnNeLinkDecryptNegativeEntityList = entity_list;
            persistentCnNeLinkDecryptNegativeModuleBase = SDK->dwGameBase;
            persistentCnNeLinkDecryptNegativeModuleSize = moduleSize;
        }
        if (linkDecryptNegativeCacheEnabled &&
            persistentCnNeLinkDecryptNegativeCache.size() > 8192) {
            persistentCnNeLinkDecryptNegativeCache.clear();
        }
        auto linkDecryptNegativeCacheHit = [&](uint64_t parent) {
            if (!linkDecryptNegativeCacheEnabled || !parent)
                return false;
            ++scanDetail.cnNeLinkDecryptNegativeCacheLookupCount;
            const DWORD now = GetTickCount();
            auto negativeIt = persistentCnNeLinkDecryptNegativeCache.find(parent);
            if (negativeIt == persistentCnNeLinkDecryptNegativeCache.end())
                return false;

            const PersistentCnNeLinkDecryptNegativeCacheEntry& entry =
                negativeIt->second;
            const bool sameContext =
                entry.entityList == entity_list &&
                entry.moduleBase == SDK->dwGameBase &&
                entry.moduleSize == moduleSize;
            const DWORD ageMs = now - entry.updateTick;
            const bool fresh = sameContext && ageMs <= linkDecryptNegativeCacheTtlMs;
            const bool staleAllowed =
                sameContext &&
                !fresh &&
                staleMetadataMs > 0 &&
                (staleMetadataOnly || ageMs <= staleMetadataMs);
            if (fresh || staleAllowed) {
                ++scanDetail.cnNeLinkDecryptNegativeCacheHitCount;
                if (staleAllowed)
                    ++scanDetail.cnNeLinkDecryptNegativeCacheStaleHitCount;
                return true;
            }

            if (sameContext)
                ++scanDetail.cnNeLinkDecryptNegativeCacheExpiredCount;
            persistentCnNeLinkDecryptNegativeCache.erase(negativeIt);
            return false;
        };
        auto storeLinkDecryptNegativeCache = [&](uint64_t parent) {
            if (!linkDecryptNegativeCacheEnabled || !parent)
                return;
            PersistentCnNeLinkDecryptNegativeCacheEntry entry{};
            entry.entityList = entity_list;
            entry.moduleBase = SDK->dwGameBase;
            entry.moduleSize = moduleSize;
            entry.updateTick = GetTickCount();
            persistentCnNeLinkDecryptNegativeCache.insert_or_assign(parent, entry);
            ++scanDetail.cnNeLinkDecryptNegativeCacheStoreCount;
        };
        auto decryptCnNeLink = [&](uint64_t parent,
                                   const EntityHeaderSnapshot* snapshot) {
            if (linkDecryptNegativeCacheHit(parent))
                return static_cast<uint64_t>(0);
            ++scanDetail.linkDecryptAttemptCount;
            const uint64_t link = DecryptComponent(parent, TYPE_LINK, snapshot);
            if (!link)
                storeLinkDecryptNegativeCache(parent);
            return link;
        };

        for (const EntityScanRecord& current : records) {
            const auto matchLinkStartedAt = std::chrono::steady_clock::now();
            const Diagnostics::ScopedDmaCallsite matchLinkCallsite(
                Diagnostics::DmaCallsite::EntityScanMatchLink);
            uint64_t cur_entity = current.entity;
            if (!cur_entity) {
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            if (worldBzProfile && lightScan) {
                bool added = false;
                if (current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id + 1, cur_entity, true);
                if (!added && current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id, cur_entity, true);
                if (!added &&
                    current.unique_id != 0 &&
                    (current.unique_id & 0x80000000u) == 0) {
                    addPlayableMatches(current.unique_id | 0x80000000u, cur_entity, true);
                }
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            uint64_t common_linker = decryptCnNeLink(
                cur_entity,
                current.header.valid ? &current.header : nullptr);
            if (!common_linker) {
                if (worldBzProfile) {
                    Diagnostics::RecordInvalidEntity();
                    scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                    continue;
                }
            } else {
                ++scanDetail.linkDecryptSuccessCount;
                ++scanDetail.linkPresent;
            }

            if (worldBzProfile) {
                uint32_t unique_id = SDK->RPM<uint32_t>(common_linker + offset::Link_UniqueId);
                if (unique_id != 0)
                    ++scanDetail.linkUidNonZero;
                bool added = false;
                if (current.unique_id != 0)
                    added = addPlayableMatches(current.unique_id + 1, cur_entity);
                if (!added && unique_id != 0)
                    added = addPlayableMatches(unique_id, cur_entity);
                if (!added && unique_id != 0 && (unique_id & 0x80000000u) == 0)
                    addPlayableMatches(unique_id | 0x80000000u, cur_entity);
                scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
                continue;
            }

            bool processedCnNeLink = false;
            auto processCnNeLinkParent = [&](uint64_t link_parent,
                                             const EntityHeaderSnapshot* snapshot) {
                if (!link_parent)
                    return;

                const uint64_t linkBase =
                    link_parent == cur_entity && common_linker
                        ? common_linker
                        : decryptCnNeLink(link_parent, snapshot);
                if (!linkBase)
                    return;
                if (!(link_parent == cur_entity && common_linker))
                    ++scanDetail.linkDecryptSuccessCount;

                processedCnNeLink = true;
                ++scanDetail.linkPresent;

                const uint32_t unique_id =
                    SDK->RPM<uint32_t>(linkBase + offset::Link_UniqueId);
                if (unique_id != 0) {
                    ++scanDetail.linkUidNonZero;
                    ++scanDetail.matchLookupCount;
                    const auto match = match_index.find(unique_id);
                    if (match != match_index.end()) {
                        ++scanDetail.matchLookupHitCount;
                        ++scanDetail.linkMatched;
                        scanDetail.cnNeBucketEntryScanCount += match->second.size();
                        for (uint64_t component_parent : match->second) {
                            if (addPair(component_parent, link_parent, lightScan))
                                ++scanDetail.linkPairs;
                        }
                    }
                }

                const bool linkHeroKnown = !lightScan && hasCnNeKnownHero(link_parent);
                for (uint64_t idOffset : { offset::Link_TargetId, offset::Link_UniqueId }) {
                    uint32_t resolvedId = 0;
                    uint64_t targetMapBase = 0;
                    {
                        const auto targetMapStartedAt = std::chrono::steady_clock::now();
                        const Diagnostics::ScopedDmaCallsite targetResolveCallsite(
                            Diagnostics::DmaCallsite::EntityScanLinkTargetResolve);
                        ++scanDetail.cnNeTargetMapAttemptCount;
                        targetMapBase =
                            ResolveCnNeLinkTargetMapBase(linkBase, idOffset, &resolvedId);
                        scanDetail.scanCnNeTargetMapMs += elapsedMs(targetMapStartedAt);
                    }
                    if (!IsLikelyCnNeEntityParent(
                            targetMapBase,
                            entity_list,
                            kEntityListReadSize,
                            SDK->dwGameBase,
                            moduleSize)) {
                        continue;
                    }
                    ++scanDetail.cnNeTargetMapSuccessCount;

                    const uint64_t pairKey =
                        (link_parent >> 4) ^ (targetMapBase << 17) ^ resolvedId;
                    if (!cnNeLinkTargetPairs.insert(pairKey).second)
                        continue;

                    if (!lightScan && !linkHeroKnown && !hasCnNePlausibleHealth(targetMapBase))
                        continue;

                    if (addPair(targetMapBase, link_parent, lightScan)) {
                        ++scanDetail.linkMatched;
                        ++scanDetail.linkPairs;
                    }
                }
            };

            if (common_linker)
                processCnNeLinkParent(
                    cur_entity,
                    current.header.valid ? &current.header : nullptr);

            {
                std::vector<uint64_t> linkParentCandidates{};
                {
                    const auto targetMapStartedAt = std::chrono::steady_clock::now();
                    const Diagnostics::ScopedDmaCallsite mapCandidateCallsite(
                        Diagnostics::DmaCallsite::EntityScanMapCandidate);
                    linkParentCandidates =
                        collectCnNeMapBaseCandidatesForParent(cur_entity);
                    scanDetail.scanCnNeTargetMapMs += elapsedMs(targetMapStartedAt);
                }
                scanDetail.cnNeMapCandidateCount += linkParentCandidates.size();
                for (uint64_t link_parent : linkParentCandidates) {
                    if (link_parent == cur_entity)
                        continue;
                    if (linkDecryptNegativeCacheHit(link_parent))
                        continue;

                    EntityHeaderSnapshot linkHeader{};
                    const EntityHeaderSnapshot* snapshot =
                        linkHeader.Read(link_parent) ? &linkHeader : nullptr;
                    processCnNeLinkParent(link_parent, snapshot);
                }
            }

            if (!processedCnNeLink)
                Diagnostics::RecordInvalidEntity();
            scanDetail.scanMatchLinkMs += elapsedMs(matchLinkStartedAt);
        }

        if (offset::IsCnNeProfile() && !lightScan) {
            for (const EntityScanRecord& current : records) {
                if (hasCnNePlayableComponents(current))
                    addPair(current.entity, current.entity);
                std::vector<uint64_t> componentCandidates{};
                {
                    const auto targetMapStartedAt = std::chrono::steady_clock::now();
                    const Diagnostics::ScopedDmaCallsite mapCandidateCallsite(
                        Diagnostics::DmaCallsite::EntityScanMapCandidate);
                    componentCandidates =
                        collectCnNeMapBaseCandidatesForParent(current.entity);
                    scanDetail.scanCnNeTargetMapMs += elapsedMs(targetMapStartedAt);
                }
                scanDetail.cnNeMapCandidateCount += componentCandidates.size();
                for (uint64_t component_parent : componentCandidates) {
                    if (hasCnNeComponentOnlyPlayable(current, component_parent))
                        addPair(component_parent, component_parent);
                }
            }
        }

        const auto dynamicPairStartedAt = std::chrono::steady_clock::now();
        for (const EntityScanRecord& current : records) {
            if (isDynamicEntityId(current.entity_id)) {
                addPair(current.entity, current.entity);
                ++scanDetail.dynamicPairs;
            }
        }
        scanDetail.scanDynamicPairMs += elapsedMs(dynamicPairStartedAt);

        scanDetail.totalPairs = result.size();
        finishScanDetail();

        Diagnostics::Trace("Entity scan list=0x%llX bytes=%zu records=%zu pairs=%zu.",
            static_cast<unsigned long long>(entity_list),
            readable_bytes,
            records.size(),
            result.size());
        return result;
    }

    inline std::string GetHeroEngNames(uint64_t HeroID, uint64_t LinkBase) {
        if (HeroID == eHero::HERO_DVA) {
            return SDK->RPM<uint16_t>(LinkBase + 0xD4) != SDK->RPM<uint16_t>(LinkBase + 0xD8)
                ? "D.Va"
                : "Hana";
        }

        if (const char* name = GameData::HeroName(HeroID); name[0] != '\0')
            return name;

        switch (HeroID) {
            case eHero::TOBTERT: return "Tob";
            case eHero::SYMTERT: return "Sym";
            case eHero::Bob:     return "Bob";
            default:
                if (GameData::HasHeroIdPrefix(HeroID)) {
                    char fallback[16] = {};
                    std::snprintf(fallback, sizeof(fallback), "Hero_%04llX",
                        static_cast<unsigned long long>(HeroID & 0xFFFFull));
                    return fallback;
                }
                if (HeroID != 0) {
                    return GameData::UnknownHeroFallbackName(HeroID);
                }
                return "Unknown";
        }
    }

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

    inline int readammo(uint64_t base, uint16_t skillIdx, uint16_t skillIdx2) {
        __m128 skillStruct{};
        uint16_t skillId[15] = { skillIdx, skillIdx2 };
        skillStruct.m128_u64[1] = base + 0xD0;
        const uint64_t skill = FnSkillStruct(&skillStruct, skillId);
        if (!skill) return -1;

        auto roundedAmmo = [](float value) -> int {
            if (!std::isfinite(value) || value < 0.0f || value > 300.0f)
                return -1;
            if (value > 0.0f && value < 0.01f)
                return -1;
            return static_cast<int>(std::lround(value));
        };

        int zeroCandidate = -1;
        auto chooseAmmo = [&zeroCandidate](int value) -> int {
            if (value > 0)
                return value;
            if (value == 0)
                zeroCandidate = 0;
            return -1;
        };

        int ammo = chooseAmmo(roundedAmmo(SDK->RPM<float>(skill + 0x60)));
        if (ammo > 0)
            return ammo;

        ammo = chooseAmmo(roundedAmmo(SDK->RPM<float>(skill + 0x50)));
        if (ammo > 0)
            return ammo;

        const int rawAmmo50 = SDK->RPM<int>(skill + 0x50);
        if (rawAmmo50 >= 0 && rawAmmo50 <= 300) {
            ammo = chooseAmmo(rawAmmo50);
            if (ammo > 0)
                return ammo;
        }

        const int rawAmmo60 = SDK->RPM<int>(skill + 0x60);
        if (rawAmmo60 >= 0 && rawAmmo60 <= 300) {
            ammo = chooseAmmo(rawAmmo60);
            if (ammo > 0)
                return ammo;
        }

        return zeroCandidate;
    }

} // namespace OW
