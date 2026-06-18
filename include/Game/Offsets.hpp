#pragma once

#include <atomic>
#include <cstdint>

// =============================================================================
// Overwatch Offsets - BZ 2.23.0.0.150818 / NE 2.23.0.0.150818 refresh
//
// BZ dump: D:\Desktop\SenseZen\downp\2.23.0.0.150818\bz\_dump.exe
// Diaphora export: D:\Desktop\SenseZen\downp\2.23.0.0.150818\bz\_dump.exe.sqlite
// NE dump: D:\Desktop\SenseZen\downp\2.23.0.0.150818\ne\_dump.exe
// Diaphora export: D:\Desktop\SenseZen\downp\2.23.0.0.150818\ne\_dump.exe.sqlite
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
            World150818,
            Identity,
        };

        enum class SingletonListMode {
            EncryptedWorldBz,
            LiveGameAdminWorldBz,
            Plain,
        };

        enum class ViewMatrixMode {
            EncryptedChain,
            EncryptedChainSubXorSub,
            DirectChain,
            EncryptedDirectMatrix,
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
            uint64_t offset_viewmatrix_xor_key3 = 0;
            uint64_t VM_P1 = 0;
            uint64_t VM_P2 = 0;
            uint64_t VM_ViewProjectionParent = 0;
            uint64_t VM_ViewProjectionPtr = 0;
            uint64_t VM_ViewProjectionMatrix = 0;
            uint64_t VM_ViewMatrix = 0;
            uint64_t VM_ProjMatrix = 0;

            uint64_t GlobalAdmin_RVA = 0;
            SingletonListMode singletonListMode = SingletonListMode::EncryptedWorldBz;
            ViewMatrixMode viewMatrixMode = ViewMatrixMode::EncryptedChain;
            uint64_t VM_DirectMatrix = 0;
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
        static constexpr auto DecryptComponent_RVA = 0x56CA00; // verified 2026-06-17: BZ 150818 Diaphora/IDA sub_7FF7A193CA00
        static constexpr auto DecryptTable_Mask    = 0x7FF;    // 2048 entries

        // Component pointer resolver (forum p331 NormPlayer, IDA tail at RVA 0x5637D2).
        // OLD (0521): static constexpr auto ComponentXorQword_RVA = 0x3A86E30;
        static constexpr auto ComponentXorQword_RVA = 0x3A5FAB0; // live-verified 2026-06-17: BZ 150818 key source
        // OLD (0521): static constexpr auto ComponentXorQword_Off = 0x10C;
        static constexpr auto ComponentXorQword_Off = 0x10C;     // live-verified 2026-06-17: [keySource+0x10C]
        // OLD (0521): static constexpr auto ComponentXorByte_RVA = 0x3772769;
        static constexpr auto ComponentXorByte_RVA  = 0x3746789; // live-verified 2026-06-17: BZ 150818 DecryptComponent tail byte
        static constexpr auto ComponentPreXorByte_RVA = 0x3752442; // dump-only helper/pre-xor byte; not the live active tail byte
        // OLD (0521): Component_Add1=0x4C8675CDE55BA1B2, Component_Add2=0x7BE57670994040F6
        // OLD (0521): Component_Xor1=0x3864150DB528414C, Component_Xor2=0xA4764E53CD34159B, Component_Ror=3
        static constexpr auto Component_Xor1        = 0xDC01B58B9BDFFB4B; // verified 0527: fixed xor
        static constexpr auto Component_Add1        = 0x024620C984E36588; // verified 0527: add after ROR32
        static constexpr auto Component_Sub1        = 0x7D957CD64821F39B; // verified 0527: subtract before final rotates
        static constexpr auto Component_Ror1        = 32;                 // verified 0527
        static constexpr auto Component_Ror2        = 60;                 // verified 0527
        static constexpr auto Component_Ror3        = 57;                 // verified 0527 helper call
        static constexpr auto Component150818_Add1  = 0x4C8675CDE55BA1B2;
        static constexpr auto Component150818_Add2  = 0x7BE57670994040F6;
        static constexpr auto Component150818_Xor1  = 0x3864150DB528414C;
        static constexpr auto Component150818_Xor2  = 0xA4764E53CD34159B;
        static constexpr auto Component150818_Ror   = 3;

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
        static constexpr auto VisibilityGlobalKeyPtr_RVA = ComponentXorQword_RVA; // live BZ 150818 shared key source
        // OLD (0521): static constexpr auto VisibilityQwordOffset = 0x1B1;
        static constexpr auto VisibilityQwordOffset      = 0x6A;      // live BZ 150818 sujung formula
        // OLD (0521): static constexpr auto VisibilityValueOffset = 0x98;
        static constexpr auto VisibilityValueOffset      = 0x98;     // reverted from 0x2D8 — IDA 0527 chain produced garbage
        static constexpr auto VisibilityMagicByte_RVA    = 0x3746659; // live BZ 150818 sujung formula
        static constexpr auto Visibility_Add1            = 0x5CE60F50EA1D337F;
        static constexpr auto Visibility_Ror1            = 3;
        static constexpr auto Visibility_Add2            = 0x78D75198F1D34D38;
        static constexpr auto Visibility_Ror2            = 12;
        static constexpr auto Visibility_Rol1            = 28;        // verified 0527
        static constexpr auto Visibility_Xor1            = 0x53DB07B6B873760C;
        static constexpr auto Visibility_Sub1            = 0x7A7DB4DE6CD03BBC;
        static constexpr auto Visibility_FinalRor        = 26;        // verified 0527

        // New decryption table base (24-byte entry format, not simple qword array).
        static constexpr auto DecryptTable_New = 0x3800000;

        // Entity system.
        // OLD (0521): static constexpr auto Address_entity_base = 0x39298C8;
        static constexpr auto Address_entity_base = 0x39017B8; // live-verified 2026-06-17: BZ 150818 forum/betsit root
        static constexpr auto EntityList_SlotCount_RVA = 0x0; // unresolved for live BZ 150818; runtime scans fixed window
        static constexpr auto entity_entry_stride = 0x10;      // entity list slot: { entity, pad }
        static constexpr auto Entity_MatchId      = 0x138;     // u32 on component parent; retained from 0521
        static constexpr auto Entity_PoolPtr      = 0x30;      // masked with 0xFFFFFFFFFFFFFFC0
        static constexpr auto Pool_PoolId         = 0x10;
        static constexpr auto Link_TargetId       = 0xD0;      // u32 on CN/NE link component for target-map lookup
        static constexpr auto Link_UniqueId       = 0xD4;      // u32 on decrypted link component
        static constexpr auto Link_UniqueIdAlt    = 0xD8;

        // p344 verified/carry-forward structural constants. Keep these named even
        // when a runtime path still uses older local helpers.
        static constexpr auto Address_LiveFov_RVA = 0x4037628; // unresolved for BZ 150818; not used by runtime path
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
        static constexpr auto Team_FlagsOffset = 0x58;
        static constexpr auto Team_LegacyMask = 0x0F800000;
        static constexpr auto Team_CnNeComparisonMask = 0x00FFFFFF;
        static constexpr auto Team_CnNeRelationMask = 0x000000FF;
        static constexpr auto Team1_Mask = 0x00800000;
        static constexpr auto Team2_Mask = 0x01000000;

        static constexpr auto Outline_ByteRva = 0x37528E4;
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

        // Current game sensitivity.
        // Static 150818: +0x2224/+0x2228 are per-frame input deltas scaled by
        // input.MouseScaleX/Y. The durable setting is singleton[0x6] + 0x2238,
        // stored as normalized UI sensitivity (4.5 -> 0.045).
        static constexpr auto Sensitivity_EncryptedPtr_RVA = 0x38B2B00; // direct pointer unresolved; keep for historical comparison
        static constexpr auto GlobalAdmin_WorldBz_RVA = 0x3A5FBC8; // live BZ 150818 Client_Game root
        static constexpr auto SensitivityAdmin_WorldBz_RVA = 0x3A65930; // live BZ 150818: admin +0x160 live-decode -> slot[6]
        static constexpr auto GlobalAdmin_Ne_2_22_1_1_150434_RVA = 0x43F1BF0;
        static constexpr auto GlobalAdmin_Ne_2_23_0_0_150818_RVA = 0x43C97F0;
        static constexpr auto GlobalAdmin_RVA = GlobalAdmin_WorldBz_RVA;
        static constexpr auto Singleton_K1_xor = 0x41D346232DA856D8;
        static constexpr auto Singleton_K2_sub = 0x53B5A9FA87525DFC;
        static constexpr auto Singleton_K3_add = 0x57374166C207BDC2;
        static constexpr auto Singleton_Ror = 59;
        static constexpr auto Singleton_InputOffset = 0x160;
        static constexpr auto LiveGameAdmin_InputOffset = 0x30;
        static constexpr auto LiveGameAdmin_Add1 = 0x78B568A5D3C8EF76;
        static constexpr auto LiveGameAdmin_Xor1 = 0x8B846BECDFD77B79;
        static constexpr auto LiveGameAdmin_Add2 = 0x73978469CB862683;
        static constexpr auto LiveGameAdmin_Ror = 48;
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit1 = 0x1D09C; // transient float hit; not wired
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit2 = 0x23F94; // input state +0x2224 in static ECS path
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit3 = 0x23F98; // input state +0x2228 in static ECS path
        static constexpr auto LiveGameAdmin_RejectedSensitivityHit4 = 0xB5E4; // transient float hit; not wired
        static constexpr auto SensitivitySingletonIndex = 0x6;
        static constexpr auto Sensitivity = 0x2238;
        static constexpr auto SensX_Scale = 0x2224;
        static constexpr auto SensY_Scale = 0x2228;
        static constexpr auto Sensitivity_NormalizedToUserScale = 100.0f;
        static constexpr auto Invert_Y_Flag = 0x2156;

        // ViewMatrix - BZ 2.23.0.0.150818
        //   Camera-world chain candidate:
        //   0x38A6980 -> decrypt -> +0x20 -> +0x48 -> +0x6D8 -> +0x8 -> +0xC0.
        // OLD (0521): static constexpr auto Address_viewmatrix_base = 0x38D0220;
        static constexpr auto Address_viewmatrix_primary_base = 0x38B2AA0; // BZ 150818 adjacent encrypted root candidate; not active
        static constexpr auto Address_viewmatrix_direct_base = 0x38B2A88; // BZ 150818 direct VP candidate; non-projecting live
        static constexpr auto Address_viewmatrix_base = 0x38A6980; // live-verified 2026-06-17: BZ 150818 camera-world encrypted root
        // OLD (0521): key1=0x59D406B75C2A4377, key2=0xD54D81BA4EED36CE, key3=0x1C840F09D6923D76
        static constexpr auto offset_viewmatrix_xor_key  = 0x59D406B75C2A4377;
        static constexpr auto offset_viewmatrix_xor_key2 = 0xD54D81BA4EED36CE;
        static constexpr auto offset_viewmatrix_xor_key3 = 0x1C840F09D6923D76;
        static constexpr auto VM_P1          = 0x20;
        static constexpr auto VM_P2          = 0x48;
        static constexpr auto VM_ViewProjectionParent = 0x6D8;
        static constexpr auto VM_ViewProjectionPtr    = 0x8;
        static constexpr auto VM_ViewProjectionMatrix = 0xC0;
        static constexpr auto VM_ViewMatrix  = 0x140;
        static constexpr auto VM_ProjMatrix  = 0xB0;
        static constexpr auto Bz150818_DirectViewProjectionMatrix = 0x1C0;

        // Viewport dimensions (2026-05-27, dump_SCY.exe).
        // IDA: FullScreenWidth/FullScreenHeight option objects at
        // 0x7FF7C1137C00 / 0x7FF7C1137C70; current uint32 value is at +0x38.
        // sub_7FF7BEBA3330 returns these as the resolved output width/height.
        static constexpr auto ViewportWidth_RVA  = 0x4019BC8;
        static constexpr auto ViewportHeight_RVA = 0x4019C38;

        // Input globals.
        static constexpr auto InputMouseScaleX_RVA      = 0x3758B2C; // input.MouseScaleX
        static constexpr auto InputMouseScaleY_RVA      = 0x3758B44; // input.MouseScaleY

        static constexpr auto changefov = 0x402B658; // current candidate; not revalidated in 0527 pass

        inline constexpr RuntimeOffsetProfile kWorldBzRuntimeProfile{
            "world/bz 150818",
            ComponentTransformMode::World150818,
            Address_entity_base,
            InputMouseScaleX_RVA,
            InputMouseScaleY_RVA,
            ViewportWidth_RVA,
            ViewportHeight_RVA,
            VisibilityValueOffset,
            Address_viewmatrix_base,
            offset_viewmatrix_xor_key,
            offset_viewmatrix_xor_key2,
            offset_viewmatrix_xor_key3,
            VM_P1,
            VM_P2,
            VM_ViewProjectionParent,
            VM_ViewProjectionPtr,
            VM_ViewProjectionMatrix,
            VM_ViewMatrix,
            VM_ProjMatrix,
            GlobalAdmin_WorldBz_RVA,
            SingletonListMode::LiveGameAdminWorldBz,
            ViewMatrixMode::EncryptedChainSubXorSub,
            0,
        };

        inline constexpr RuntimeOffsetProfile kCnNeRuntimeProfile{
            "cn/ne 150818",
            ComponentTransformMode::Identity,
            // NE 150818 entity root. Diaphora maps the 150434 root initializer
            // sub_7FF794332970 (writer of RVA 0x42D2298) to 150818
            // sub_7FF66B4CCC90, which writes qword_7FF66DCF9138 / RVA
            // 0x42A9138. Live probe closes the direct identity/raw-pair path
            // here; 0x4283A08 only passed via loose wrapper salvage.
            0x42A9138,
            0x40F145C, // live-verified 2026-06-17: NE input.MouseScaleX
            0x40F1474, // live-verified 2026-06-17: NE input.MouseScaleY
            0,         // unresolved: CN viewport width
            0,         // unresolved: CN viewport height
            0x98,      // live-verified 2026-06-01: CN visibility raw bool, raw == 1 means visible
            // Direct ViewMatrix root. The render VP slot can be zero on NE
            // 150818; runtime publishes camera_view * projection when both
            // matrices are valid.
            0x49878E0,
            0,         // not used by CN direct ViewMatrix path
            0,         // not used by CN direct ViewMatrix path
            0,         // not used by CN direct ViewMatrix path
            0x20,
            0x48,
            0x6C8,
            0x8,
            0xC0,
            0x140,
            0xB0,
            GlobalAdmin_Ne_2_23_0_0_150818_RVA,
            SingletonListMode::Plain,
            ViewMatrixMode::DirectChain,
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

        inline uint32_t TeamRawComparisonKeyFromFlags(uint32_t flags)
        {
            return IsCnNeProfile()
                ? (flags & Team_CnNeComparisonMask)
                : (flags & Team_LegacyMask);
        }

        inline uint32_t TeamRelationCodeFromFlags(uint32_t flags)
        {
            return flags & Team_CnNeRelationMask;
        }

        inline uint32_t TeamComparisonKeyFromFlags(uint32_t flags)
        {
            if (IsCnNeProfile())
                return flags & Team_CnNeComparisonMask;
            return flags & Team_LegacyMask;
        }

        // =========================================================================
        // Legacy / unresolved offsets
        // =========================================================================

        static constexpr auto VisFN        = 0x79E722;  // broken legacy table-walk path
        static constexpr auto Vis_Key      = 0x1AAC46FF0D473EBA; // broken legacy table-walk path
        static constexpr auto DecryptTable_2 = 0x389A700;      // legacy table-walk path; not current DecryptVis

    }
}
