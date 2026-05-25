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

        // Old decryption table bases (still present but data format changed)
        static constexpr auto DecryptTable_1 = 0x38996E0;      // OBSOLETE format
        static constexpr auto DecryptTable_2 = 0x389A700;      // OBSOLETE format

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

        // =========================================================================
        // OBSOLETE: RIGEL-2411 / pre-May 2026 offsets (kept for reference)
        // =========================================================================

        static constexpr auto Address_entity_base_old      = 0x37EC5E0; // DEAD
        static constexpr auto Address_viewmatrix_base_old  = 0x37F7618; // DEAD
        static constexpr auto Address_viewmatrix_base_test = 0x3EB6278; // DEAD
        static constexpr auto offset_viewmatrix_ptr_old    = 0x7E0;     // DEAD
        static constexpr auto offset_viewmatrix_xor_key_old = 0x544A3BA5BE911EE7; // DEAD

        static constexpr auto HeapManager          = 0x38B55F0; // DEAD — now NULL
        static constexpr auto HeapManager_Var      = 0x3899DD5; // DEAD
        static constexpr auto HeapManager_Key      = 0xE7E1F898E11B68B1; // DEAD
        static constexpr auto HeapManager_Pointer  = 0x160;

        static constexpr auto changefov    = 0x395EDB8; // DEAD
        static constexpr auto Silent       = 0xF909A5;  // DEAD
        static constexpr auto SensitivePtr = 0x2054;

        static constexpr auto VisFN        = 0x79E722;  // DEAD — .text shifted
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // DEAD
        // OutlineFN/Outline_Key — REMOVED 2026-05-25: DMA external cannot render outlines

    }
}
