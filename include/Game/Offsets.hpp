#pragma once

// =============================================================================
// Overwatch Offsets - static dump verified on 2026-05-27
//
// Dump: D:\Desktop\downp\0527\dump_SCY.exe
// ImageBase=0x7FF7BD100000  SizeOfImage=0x4725000  Timestamp=2026-05-14T18:32:55Z
//
// Changes from 0521/0525:
//   - Component key source RVAs changed; key qword offset moved 0x10C -> 0x1D4.
//   - Component tail changed from add/xor/add/xor/xor/xor/ROR3 to xor/ROR/add/sub/ROR60/ROR57.
//   - Visibility flag moved from +0x98 to +0x2D8 and now uses a dedicated magic byte.
//   - ViewMatrix root moved and lost the third key: new formula is (enc + key1) ^ key2.
//   - GameAdmin root moved and now reads +0x30 with a two-rotate formula.
// =============================================================================

namespace OW {
    namespace offset {

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

        // Visibility/data flag resolver (forum p331 full disassembly, IDA RVA 0x58C880).
        // OLD (0521): static constexpr auto VisibilityGlobalKeyPtr_RVA = 0x3B76970;
        static constexpr auto VisibilityGlobalKeyPtr_RVA = 0x3A92E70; // verified 0527: key ptr RIP ref
        // OLD (0521): static constexpr auto VisibilityQwordOffset = 0x1B1;
        static constexpr auto VisibilityQwordOffset      = 0x16A;     // verified 0527: [keyPtr+0x16A]
        // OLD (0521): static constexpr auto VisibilityValueOffset = 0x98;
        static constexpr auto VisibilityValueOffset      = 0x2D8;     // verified 0527: mov rax,[rcx+2D8h]
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
