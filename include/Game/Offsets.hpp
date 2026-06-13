#pragma once

#include <atomic>
#include <cstdint>

// =============================================================================
// Overwatch Offsets - static dump verified on 2026-05-27
//
// Dump: D:\Desktop\SenseZen\downp\0527\dump_SCY.exe
// ImageBase=0x7FF7BD100000  SizeOfImage=0x4725000  Timestamp=2026-05-14T18:32:55Z
//
// Changes from 0521/0525:
//   - Component key source RVAs changed; key qword offset moved 0x10C -> 0x1D4.
//   - Component tail changed from add/xor/add/xor/xor/xor/ROR3 to xor/ROR/add/sub/ROR60/ROR57.
//   - Visibility uses profile-specific runtime polarity; CN/NE uses +0x98 as
//     a live-verified raw bool state.
//   - ViewMatrix root moved and lost the third key: new formula is (enc + key1) ^ key2.
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

        enum class SingletonListMode {
            EncryptedWorldBz,
            Plain,
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

            uint64_t GlobalAdmin_RVA = 0;
            SingletonListMode singletonListMode = SingletonListMode::EncryptedWorldBz;
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
        static constexpr auto EntityList_SlotCount_RVA = 0x39358F4; // carry-forward p337
        static constexpr auto entity_entry_stride = 0x10;      // entity list slot: { entity, pad }
        static constexpr auto Entity_MatchId      = 0x138;     // u32 on component parent; retained from 0521
        static constexpr auto Entity_PoolPtr      = 0x30;      // masked with 0xFFFFFFFFFFFFFFC0
        static constexpr auto Pool_PoolId         = 0x10;
        static constexpr auto Link_UniqueId       = 0xD4;      // u32 on decrypted link component
        static constexpr auto Link_UniqueIdAlt    = 0xD8;

        // p344 verified/carry-forward structural constants. Keep these named even
        // when a runtime path still uses older local helpers.
        static constexpr auto Address_LiveFov_RVA = 0x4037628;
        static constexpr auto Default_FOV_Horizontal_Deg = 103.0f;
        static constexpr auto kOpenProcessFlags = 0x438;

        static constexpr auto Transform_PositionOffset = 0x3D0;
        static constexpr auto ReplicatedTransform_PositionOffset = 0x180;
        static constexpr auto Velocity_PositionOffset = 0x200;
        static constexpr auto Velocity_LocationOffset = 0x200;
        static constexpr auto Velocity_VelocityOffset = 0x050;
        static constexpr auto Velocity_LocationYBias = 1.0f;
        static constexpr auto Velocity_BoneDataPtrOffset = 0x8B0;
        static constexpr auto BoneData_BonesArrayOffset = 0x20;
        static constexpr auto BoneEntryStride = 0x30;
        static constexpr auto Bone_PosOffset = 0x20;
        static constexpr auto Actor_TeamOffsetFromHeroId = 0x180;
        static constexpr auto Actor_PositionOffsetFromHeroId = 0x1C;
        static constexpr auto HeroId_HighWord = 0x02E0;
        static constexpr auto RotationBase_Sub1 = 0x888;
        static constexpr auto RotationBase_Sub2 = 0x90C;
        static constexpr auto Vis_DataOffsetInComp = 0x98;
        static constexpr auto Vis_OccludedBitMask = 0x800;
        static constexpr auto Team1_Mask = 0x00800000;
        static constexpr auto Team2_Mask = 0x01000000;

        static constexpr auto Outline_ByteRva = 0x377E6E4;
        static constexpr auto Outline_QwordBaseRva = ComponentXorQword_RVA;
        static constexpr auto Outline_QwordOffset = 0x97;
        static constexpr auto Outline_Rol1Amount = 23;
        static constexpr auto Outline_XorK1 = 0xEF781B6466FAB59B;
        static constexpr auto Outline_SubK2 = 0x2240EA534C11100D;
        static constexpr auto Outline_AddK3 = 0x605EC85DF1D6897D;
        static constexpr auto Outline_AddK4 = 0x1F21DE5151741226;

        static constexpr auto Healthpack_Small = 0x40000000000005F;
        static constexpr auto Healthpack_Large = 0x400000000000060;
        static constexpr auto Healthpack_Mega = 0x40000000000480A;

        // GlobalAdmin singleton path for current game sensitivity. The legacy
        // Sensitivity_EncryptedPtr direct object can read, but live checks showed
        // singleton[0x6] + 0x2238 is the value that follows setting changes.
        static constexpr auto Sensitivity_EncryptedPtr_RVA = 0x38DC290;
        static constexpr auto GlobalAdmin_WorldBz_RVA = 0x3A98CF0;
        static constexpr auto GlobalAdmin_Ne_2_22_1_1_150434_RVA = 0x43F1BF0;
        static constexpr auto GlobalAdmin_RVA = GlobalAdmin_WorldBz_RVA;
        static constexpr auto Singleton_K1_xor = 0x41D346232DA856D8;
        static constexpr auto Singleton_K2_sub = 0x53B5A9FA87525DFC;
        static constexpr auto Singleton_K3_add = 0x57374166C207BDC2;
        static constexpr auto Singleton_Ror = 59;
        static constexpr auto Singleton_InputOffset = 0x160;
        static constexpr auto SensitivitySingletonIndex = 0x6;
        static constexpr auto Sensitivity = 0x2238;
        static constexpr auto SensX_Scale = 0x2224;
        static constexpr auto SensY_Scale = 0x2228;
        static constexpr auto Invert_Y_Flag = 0x2156;

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

        // Input globals.
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
            GlobalAdmin_WorldBz_RVA,
            SingletonListMode::EncryptedWorldBz,
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
            GlobalAdmin_Ne_2_22_1_1_150434_RVA,
            SingletonListMode::Plain,
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

        static constexpr auto VisFN        = 0x79E722;  // broken legacy table-walk path
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // broken legacy table-walk path
        static constexpr auto DecryptTable_2 = 0x389A700;      // legacy table-walk path; not current DecryptVis

    }
}
