#pragma once

#include <atomic>
#include <cstdint>

// =============================================================================
// Overwatch Offsets - static dump verified on 2026-05-27
//
// Dump: D:\Desktop\downp\0527\dump_SCY.exe
// ImageBase=0x7FF7BD100000  SizeOfImage=0x4725000  Timestamp=2026-05-14T18:32:55Z
//
// Changes from 0521/0525:
//   - Component key source RVAs changed; key qword offset moved 0x10C -> 0x1D4.
//   - Component tail changed from add/xor/add/xor/xor/xor/ROR3 to xor/ROR/add/sub/ROR60/ROR57.
//   - Visibility uses profile-specific runtime polarity; CN/NE uses +0x98 as
//     a live-verified raw bool state.
//   - ViewMatrix root moved and lost the third key: new formula is (enc + key1) ^ key2.
//   - GameAdmin root moved and now reads +0x30 with a two-rotate formula.
// =============================================================================

namespace OW {
    namespace offset {

        enum class RuntimeProfile {
            WorldBz,
            CnNe,
        };

        enum class ComponentTransformMode {
            World0527,
            Identity,
        };

        struct RuntimeOffsetProfile {
            const char* name = "world/bz";
            ComponentTransformMode componentTransform = ComponentTransformMode::World0527;

            uint64_t Address_entity_base = 0;
            uint64_t InputMouseScaleX_RVA = 0;
            uint64_t InputMouseScaleY_RVA = 0;
            uint64_t ViewportWidth_RVA = 0;
            uint64_t ViewportHeight_RVA = 0;
            uint64_t VisibilityValueOffset = 0;

            uint64_t Address_viewmatrix_base = 0;
            uint64_t offset_viewmatrix_xor_key = 0;
            uint64_t offset_viewmatrix_xor_key2 = 0;
            uint64_t VM_P1 = 0;
            uint64_t VM_P2 = 0;
            uint64_t VM_ViewProjectionParent = 0;
            uint64_t VM_ViewProjectionPtr = 0;
            uint64_t VM_ViewProjectionMatrix = 0;
            uint64_t VM_ViewMatrix = 0;
            uint64_t VM_ProjMatrix = 0;

            uint64_t Address_game_admin_root = 0;
            uint64_t GameAdmin_RootPtr = 0;
            uint64_t GameAdmin_Add1 = 0;
            uint64_t GameAdmin_Xor1 = 0;
            uint64_t GameAdmin_Ror1 = 0;
            uint64_t GameAdmin_Add2 = 0;
            uint64_t GameAdmin_Ror2 = 0;
        };

        // =========================================================================
        // STATIC-VERIFIED (2026-05-27): IDA xrefs + forum p330/p331
        // =========================================================================

        // Key system
        static constexpr auto KeyGlobalStore_RVA = 0x40405F8; // diagnostic only
        static constexpr auto KeyGlobal_XOR    = 0xF5;        // diagnostic only
        static constexpr auto GetGlobalKey_RVA = 0x581D20;    // diagnostic legacy helper

        // DecryptComponent: function start at RVA 0x5632B0; transform tail near 0x5637D2.
        // OLD (0521): static constexpr auto DecryptComponent_RVA = 0x563700;
        static constexpr auto DecryptComponent_RVA = 0x5632B0; // verified 0527: IDA sub_7FF7BD6632B0 + forum p331
        static constexpr auto DecryptTable_Mask    = 0x7FF;    // 2048 entries

        // Component pointer resolver (forum p331 NormPlayer, IDA tail at RVA 0x5637D2).
        // OLD (0521): static constexpr auto ComponentXorQword_RVA = 0x3A86E30;
        static constexpr auto ComponentXorQword_RVA = 0x3A92E70; // verified 0527: RIP xrefs + forum p331
        // OLD (0521): static constexpr auto ComponentXorQword_Off = 0x10C;
        static constexpr auto ComponentXorQword_Off = 0x1D4;     // verified 0527: [keySource+0x1D4]
        // OLD (0521): static constexpr auto ComponentXorByte_RVA = 0x3772769;
        static constexpr auto ComponentXorByte_RVA  = 0x377E243; // verified 0527: RIP xrefs + forum p331
        // OLD (0521): Component_Add1=0x4C8675CDE55BA1B2, Component_Add2=0x7BE57670994040F6
        // OLD (0521): Component_Xor1=0x3864150DB528414C, Component_Xor2=0xA4764E53CD34159B, Component_Ror=3
        static constexpr auto Component_Xor1        = 0xDC01B58B9BDFFB4B; // verified 0527: fixed xor
        static constexpr auto Component_Add1        = 0x024620C984E36588; // verified 0527: add after ROR32
        static constexpr auto Component_Sub1        = 0x7D957CD64821F39B; // verified 0527: subtract before final rotates
        static constexpr auto Component_Ror1        = 32;                 // verified 0527
        static constexpr auto Component_Ror2        = 60;                 // verified 0527
        static constexpr auto Component_Ror3        = 57;                 // verified 0527 helper call

        // Wrapper/helper path from plexies p332. This is not the main
        // DecryptComponent tail above: it uses a helper XOR/ROR stage plus a
        // different byte/key-field pair. IDA at RVA 0x58EF91 references the same
        // qword slot as ComponentXorQword_RVA; plexies' 0x4092E70 entry is not a
        // referenced slot in the 0527 IDA database.
        static constexpr auto ComponentWrapperHelper_XorKey = 0x3450DD4E165D1B80;
        static constexpr auto ComponentWrapperHelper_Ror    = 29;
        static constexpr auto ComponentWrapperByte_RVA      = 0x377E422;
        static constexpr auto ComponentWrapperQword_RVA     = ComponentXorQword_RVA;
        static constexpr auto ComponentWrapperQword_Off     = 0x45;
        static constexpr auto ComponentWrapper_SubK         = 0x5E1EE52880C2AB5C;
        static constexpr auto ComponentWrapper_XorK         = 0x0C42F747C128EF56;
        static constexpr auto ComponentWrapper_AddK         = 0x476DC63CD7A9AA1C;
        static constexpr auto ComponentWrapper_Ror1         = 12;
        static constexpr auto ComponentWrapper_Ror2         = 19;

        // Visibility/data flag resolver (forum p331 full disassembly, IDA RVA 0x58C880).
        // OLD (0521): static constexpr auto VisibilityGlobalKeyPtr_RVA = 0x3B76970;
        static constexpr auto VisibilityGlobalKeyPtr_RVA = 0x3B76970; // forum p330 verified; IDA 0x3A92E70 produced garbage
        // OLD (0521): static constexpr auto VisibilityQwordOffset = 0x1B1;
        static constexpr auto VisibilityQwordOffset      = 0x1B1;     // forum p330 verified; IDA 0x16A produced garbage
        // OLD (0521): static constexpr auto VisibilityValueOffset = 0x98;
        static constexpr auto VisibilityValueOffset      = 0x98;     // reverted from 0x2D8 — IDA 0527 chain produced garbage
        static constexpr auto VisibilityMagicByte_RVA    = 0x377DFE9; // verified 0527: magic byte RIP ref
        static constexpr auto Visibility_Add1            = 0x1B69BE8B4ADCEA02; // verified 0527
        static constexpr auto Visibility_Ror1            = 15;        // verified 0527
        static constexpr auto Visibility_Add2            = 0x05714F5E0F71FB82; // verified 0527
        static constexpr auto Visibility_Ror2            = 40;        // verified 0527
        static constexpr auto Visibility_Rol1            = 28;        // verified 0527
        static constexpr auto Visibility_Xor1            = 0x84A19C7434B9971A; // verified 0527
        static constexpr auto Visibility_Sub1            = 0x375EC32E5219B09C; // verified 0527
        static constexpr auto Visibility_FinalRor        = 26;        // verified 0527

        // New decryption table base (24-byte entry format, not simple qword array).
        static constexpr auto DecryptTable_New = 0x3800000;

        // Entity system.
        // OLD (0521): static constexpr auto Address_entity_base = 0x39298C8;
        static constexpr auto Address_entity_base = 0x3935908; // verified 0527: forum p330 + IDA xref, stride 0x10
        static constexpr auto entity_entry_stride = 0x10;      // entity list slot: { entity, pad }
        static constexpr auto Entity_MatchId      = 0x138;     // u32 on component parent; retained from 0521
        static constexpr auto Entity_PoolPtr      = 0x30;      // masked with 0xFFFFFFFFFFFFFFC0
        static constexpr auto Pool_PoolId         = 0x10;
        static constexpr auto Link_UniqueId       = 0xD4;      // u32 on decrypted link component
        static constexpr auto Link_UniqueIdAlt    = 0xD8;

        // ViewMatrix - VM12 (2026-05-27, UC p330/p331 betsit)
        //   Chain: enc = RPM(base+Addr); dec = (enc + k1) ^ k2
        //          p1 = RPM(dec + VM_P1); p2 = RPM(p1 + VM_P2)
        //          primary render VP = RPM(RPM(p2 + 0x6C8) + 0x8) + 0xC0
        //          camera view candidate = p2 + VM_ViewMatrix; projection candidate = p2 + VM_ProjMatrix
        // OLD (0521): static constexpr auto Address_viewmatrix_base = 0x38D0220;
        static constexpr auto Address_viewmatrix_base = 0x38DC230; // verified 0527: IDA xref + forum p330/p331
        // OLD (0521): key1=0x59D406B75C2A4377, key2=0xD54D81BA4EED36CE, key3=0x1C840F09D6923D76
        static constexpr auto offset_viewmatrix_xor_key  = 0x37316FB2858F0E4A; // verified 0527: add key
        static constexpr auto offset_viewmatrix_xor_key2 = 0xB6326CCBCA7E34F4; // verified 0527: xor key
        static constexpr auto offset_viewmatrix_xor_key3 = 0x0;                // retired 0527: two-key formula
        static constexpr auto VM_P1          = 0x20;
        static constexpr auto VM_P2          = 0x48;
        static constexpr auto VM_ViewProjectionParent = 0x6C8;
        static constexpr auto VM_ViewProjectionPtr    = 0x8;
        static constexpr auto VM_ViewProjectionMatrix = 0xC0;
        static constexpr auto VM_ViewMatrix  = 0x140;
        static constexpr auto VM_ProjMatrix  = 0xB0;

        // Viewport dimensions (2026-05-27, dump_SCY.exe).
        // IDA: FullScreenWidth/FullScreenHeight option objects at
        // 0x7FF7C1137C00 / 0x7FF7C1137C70; current uint32 value is at +0x38.
        // sub_7FF7BEBA3330 returns these as the resolved output width/height.
        static constexpr auto ViewportWidth_RVA  = 0x4037C38;
        static constexpr auto ViewportHeight_RVA = 0x4037CA8;

        // GameAdmin / input globals (forum p330 correction, IDA RVA 0x56B120).
        // OLD (0521): static constexpr auto Address_game_admin_root = 0x3A8CCB0;
        static constexpr auto Address_game_admin_root = 0x3A92F80; // verified 0527: root xrefs + corrected forum post
        // OLD (0521): static constexpr auto GameAdmin_RootPtr = 0x160;
        static constexpr auto GameAdmin_RootPtr       = 0x30;      // verified 0527: mov rax,[rcx+30h]
        // OLD (0521): Add1=0x78B568A5D3C8EF76, Xor1=0x8B846BECDFD77B79, Add2=0x73978469CB862683, Ror=48
        static constexpr auto GameAdmin_Add1          = 0x3A7D48F98701DF53; // verified 0527
        static constexpr auto GameAdmin_Xor1          = 0xA0CC9EB06D3118CD; // verified 0527
        static constexpr auto GameAdmin_Ror1          = 17;                 // verified 0527
        static constexpr auto GameAdmin_Add2          = 0x2AF9257775C5D0FF; // verified 0527
        static constexpr auto GameAdmin_Ror2          = 34;                 // verified 0527
        static constexpr auto GameAdmin_Ror           = GameAdmin_Ror2;     // compatibility alias for older diagnostics

        static constexpr auto HeapSlotIndex_InputSystem = 6; // slot-6 input system helper
        static constexpr auto InputMouseScaleX_RVA      = 0x3778BCC; // input.MouseScaleX
        static constexpr auto InputMouseScaleY_RVA      = 0x3778BE4; // input.MouseScaleY

        static constexpr auto changefov = 0x402B658; // current candidate; not revalidated in 0527 pass

        inline constexpr RuntimeOffsetProfile kWorldBzRuntimeProfile{
            "world/bz",
            ComponentTransformMode::World0527,
            Address_entity_base,
            InputMouseScaleX_RVA,
            InputMouseScaleY_RVA,
            ViewportWidth_RVA,
            ViewportHeight_RVA,
            VisibilityValueOffset,
            Address_viewmatrix_base,
            offset_viewmatrix_xor_key,
            offset_viewmatrix_xor_key2,
            VM_P1,
            VM_P2,
            VM_ViewProjectionParent,
            VM_ViewProjectionPtr,
            VM_ViewProjectionMatrix,
            VM_ViewMatrix,
            VM_ProjMatrix,
            Address_game_admin_root,
            GameAdmin_RootPtr,
            GameAdmin_Add1,
            GameAdmin_Xor1,
            GameAdmin_Ror1,
            GameAdmin_Add2,
            GameAdmin_Ror2,
        };

        inline constexpr RuntimeOffsetProfile kCnNeRuntimeProfile{
            "cn/ne",
            ComponentTransformMode::Identity,
            0x42D2298, // live-verified 2026-06-03: NE entity root
            0x411E4FC, // live-verified 2026-06-03: input.MouseScaleX
            0x411E514, // live-verified 2026-06-03: input.MouseScaleY
            0,         // unresolved: CN viewport width
            0,         // unresolved: CN viewport height
            0x98,      // live-verified 2026-06-01: CN visibility raw bool, raw == 1 means visible
            0x49A6A80, // live-verified 2026-06-03: direct ViewMatrix root
            0,         // not used by CN direct ViewMatrix path
            0,         // not used by CN direct ViewMatrix path
            0x20,
            0x48,
            0x6C8,
            0x8,
            0xC0,
            0x140,
            0xB0,
            0,         // unresolved: CN GameAdmin root/formula/table
            0,
            0,
            0,
            0,
            0,
            0,
        };

        inline std::atomic<RuntimeProfile>& ActiveProfileStorage()
        {
            static std::atomic<RuntimeProfile> profile{ RuntimeProfile::WorldBz };
            return profile;
        }

        inline void SetActiveProfile(RuntimeProfile profile)
        {
            ActiveProfileStorage().store(profile, std::memory_order_release);
        }

        inline RuntimeProfile ActiveProfile()
        {
            return ActiveProfileStorage().load(std::memory_order_acquire);
        }

        inline const RuntimeOffsetProfile& Active()
        {
            return ActiveProfile() == RuntimeProfile::CnNe
                ? kCnNeRuntimeProfile
                : kWorldBzRuntimeProfile;
        }

        inline const char* ActiveProfileName()
        {
            return Active().name;
        }

        inline bool IsCnNeProfile()
        {
            return ActiveProfile() == RuntimeProfile::CnNe;
        }

        // =========================================================================
        // Legacy / unresolved offsets
        // =========================================================================

        // Deprecated Rigel HeapManager chain. The old base RVA is NULL on current builds;
        // kept only as a last-resort compatibility probe.
        static constexpr auto HeapManager          = 0x38B55F0;
        static constexpr auto HeapManager_Var      = 0x3899DD5;
        static constexpr auto HeapManager_Key      = 0xE7E1F898E11B68B1;
        static constexpr auto HeapManager_Pointer  = 0x160;

        static constexpr auto SensitivePtr = 0x2054;

        static constexpr auto VisFN        = 0x79E722;  // broken legacy table-walk path
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // broken legacy table-walk path
        static constexpr auto DecryptTable_2 = 0x389A700;      // legacy table-walk path; not current DecryptVis

    }
}
