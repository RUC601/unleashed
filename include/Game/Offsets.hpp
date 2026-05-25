#pragma once

// =============================================================================
// Overwatch Offsets — DMA Verified on 2026-05-25 (live OW build)
//
// Game: Overwatch.exe  PID=26092  Base=0x7FF790F30000  Size=0x4718000 (71MB)
//
// Changes from RIGEL-2411 (Nov 2024):
//   - All 4 XOR keys replaced
//   - DecryptComponent table: 512 → 2048 entries (mask 0x1FF → 0x7FF)
//   - Table format completely changed: old = qword array, new = 24-byte structs
//   - GetGlobalKey algorithm changed (now uses heap-based key gen)
//   - GlobalKey stored at RVA 0x40405F8 (different from old)
//   - Entity base shifted, entity struct layout changed
//   - ViewMatrix XOR key and location both changed
// =============================================================================

namespace OW {
    namespace offset {

        // =========================================================================
        // DMA-VERIFIED (2026-05-25) — confirmed working on live May 2026 build
        // =========================================================================

        // Key system
        static constexpr auto KeyGlobalStore_RVA = 0x40405F8; // mov [rip+disp],rax in GetGlobalKey
        static constexpr auto KeyGlobal_XOR    = 0xF5;        // key_raw ^ 0xF5 = actual_key
        static constexpr auto GetGlobalKey_RVA = 0x581D20;    // new GetGlobalKey function

        // DecryptComponent: real function at RVA 0x563700 (sub_7FF6A80B3200)
        //   OLD RVA 0x666016 was a MISIDENTIFICATION (IEEE 754 exponent extraction)
        static constexpr auto DecryptComponent_RVA = 0x563700;
        static constexpr auto DecryptTable_Mask    = 0x7FF;      // 2048 entries (was 0x1FF for 512)

        // Live DMA verified key sources (2026-05-25)
        static constexpr auto ComponentXorQword_RVA = 0x3A86E30; // RPM(RPM(base+this) + 0x10C)
        static constexpr auto ComponentXorQword_Off = 0x10C;     // offset from dereferenced ptr
        static constexpr auto ComponentXorByte_RVA  = 0x3772769; // RPM<uint8>(base+this), value=0x6B

        // New decryption table base (24-byte entry format, not simple qword array!)
        static constexpr auto DecryptTable_New = 0x3800000;

        // Entity system — LIVE DMA VERIFIED 2026-05-25
        static constexpr auto Address_entity_base = 0x39298C8; // confirmed: 0x224071A0000
        static constexpr auto entity_entry_stride = 0x30;       // 48 bytes per entity table entry

        // ViewMatrix — VM11 (May 2026, UC p323/p329)
        //   Chain: enc = RPM(base+Addr); dec = ((enc - k1) ^ k2) - k3
        //          p1 = RPM(dec + VM_P1); p2 = RPM(p1 + VM_P2)
        //          view = p2 + VM_ViewMatrix; proj = p2 + VM_ProjMatrix
        static constexpr auto Address_viewmatrix_base = 0x38D0220;
        static constexpr auto offset_viewmatrix_xor_key  = 0x59D406B75C2A4377;
        static constexpr auto offset_viewmatrix_xor_key2 = 0xD54D81BA4EED36CE;
        static constexpr auto offset_viewmatrix_xor_key3 = 0x1C840F09D6923D76;
        static constexpr auto VM_P1          = 0x20;
        static constexpr auto VM_P2          = 0x48;
        static constexpr auto VM_ViewMatrix  = 0x140;
        static constexpr auto VM_ProjMatrix  = 0xB0;

        // GameAdmin / input globals (2026-05-25 current candidates)
        static constexpr auto Address_game_admin_root = 0x3A8CCB0; // GameAdmin root pointer RVA
        static constexpr auto GameAdmin_RootPtr       = 0x160;
        static constexpr auto GameAdmin_Add1          = 0x78B568A5D3C8EF76;
        static constexpr auto GameAdmin_Xor1          = 0x8B846BECDFD77B79;
        static constexpr auto GameAdmin_Add2          = 0x73978469CB862683;
        static constexpr auto GameAdmin_Ror           = 48;

        static constexpr auto HeapSlotIndex_InputSystem = 6; // slot-6 input system helper
        static constexpr auto InputMouseScaleX_RVA      = 0x3778BCC; // input.MouseScaleX
        static constexpr auto InputMouseScaleY_RVA      = 0x3778BE4; // input.MouseScaleY

        static constexpr auto changefov = 0x402B658; // current candidate; replaces dead 0x395EDB8

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

        static constexpr auto VisFN        = 0x79E722;  // BROKEN — .text shifted, needs new RVA
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // BROKEN
        static constexpr auto DecryptTable_2 = 0x389A700;      // used by DecryptVis

    }
}
