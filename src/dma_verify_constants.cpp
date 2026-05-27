// =============================================================================
// DMA constant validation tool
// =============================================================================

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "leechcore.h"
#include "vmmdll.h"

#include "Game/Offsets.hpp"

static VMM_HANDLE g_vmm = nullptr;
static DWORD g_pid = 0;
static uint64_t g_base = 0;
static uint64_t g_image_size = 0;
static PVMMDLL_MAP_VAD g_vad_map = nullptr;

static constexpr uint64_t kRvaKeySource = OW::offset::ComponentXorQword_RVA;
static constexpr uint64_t kKeyMaterialOffset = OW::offset::ComponentXorQword_Off;
static constexpr uint64_t kRvaKeyByte = OW::offset::ComponentXorByte_RVA;
static constexpr uint64_t kRvaPairArray = OW::offset::Address_entity_base;
static constexpr uint64_t kRvaPointerG = OW::offset::Address_viewmatrix_base;
static constexpr uint64_t kRvaVisibilityGlobalKeyPtr = OW::offset::VisibilityGlobalKeyPtr_RVA;
static constexpr uint64_t kVisibilityQwordOffset = OW::offset::VisibilityQwordOffset;
static constexpr uint64_t kRvaVisibilityMagicByte = OW::offset::VisibilityMagicByte_RVA;

static constexpr ULONG64 kReadFlags =
    VMMDLL_FLAG_NOCACHE |
    VMMDLL_FLAG_NOPAGING |
    VMMDLL_FLAG_NOPAGING_IO;

struct Pair64 {
    uint64_t first;
    uint64_t second;
};

struct PointerCheck {
    bool non_zero;
    bool canonical_user;
    bool in_vad;
    bool readable;
    bool vad_available;

    bool ok() const
    {
        const bool in_process = vad_available ? in_vad : true;
        return non_zero && canonical_user && in_process && readable;
    }
};

template <typename T>
bool ReadExact(uint64_t address, T& out)
{
    out = {};
    DWORD bytes_read = 0;
    const BOOL ok = VMMDLL_MemReadEx(
        g_vmm,
        g_pid,
        address,
        reinterpret_cast<PBYTE>(&out),
        static_cast<DWORD>(sizeof(T)),
        &bytes_read,
        kReadFlags);
    return ok && bytes_read == sizeof(T);
}

bool IsCanonicalUserPointer(uint64_t value)
{
    return value >= 0x10000ull && value <= 0x00007FFFFFFFFFFFull;
}

bool IsInVad(uint64_t address, size_t size)
{
    if (!g_vad_map || size == 0) {
        return false;
    }

    const uint64_t last = address + static_cast<uint64_t>(size - 1);
    if (last < address) {
        return false;
    }

    for (DWORD i = 0; i < g_vad_map->cMap; ++i) {
        const VMMDLL_MAP_VADENTRY& entry = g_vad_map->pMap[i];
        if (address >= entry.vaStart && last <= entry.vaEnd) {
            return true;
        }
    }
    return false;
}

bool CanRead(uint64_t address, size_t size)
{
    if (size == 0 || size > 0x1000) {
        return false;
    }

    uint8_t buffer[0x1000] = {};
    DWORD bytes_read = 0;
    const BOOL ok = VMMDLL_MemReadEx(
        g_vmm,
        g_pid,
        address,
        buffer,
        static_cast<DWORD>(size),
        &bytes_read,
        kReadFlags);
    return ok && bytes_read == size;
}

PointerCheck CheckPointer(uint64_t value, size_t readable_size)
{
    PointerCheck check{};
    check.non_zero = value != 0;
    check.canonical_user = IsCanonicalUserPointer(value);
    check.vad_available = g_vad_map != nullptr;
    check.in_vad = check.vad_available ? IsInVad(value, readable_size) : false;
    check.readable = check.non_zero && check.canonical_user && CanRead(value, readable_size);
    return check;
}

const char* PassFail(bool ok)
{
    return ok ? "PASS" : "FAIL";
}

void PrintRead64(const char* label, uint64_t address, bool read_ok, uint64_t value)
{
    std::printf(
        "%-24s @ 0x%016llX = 0x%016llX  [%s]\n",
        label,
        static_cast<unsigned long long>(address),
        static_cast<unsigned long long>(value),
        PassFail(read_ok));
}

void PrintRead8(const char* label, uint64_t address, bool read_ok, uint8_t value)
{
    std::printf(
        "%-24s @ 0x%016llX = 0x%02X                [%s]\n",
        label,
        static_cast<unsigned long long>(address),
        static_cast<unsigned>(value),
        PassFail(read_ok));
}

bool PrintPointer(const char* label, uint64_t slot_address, bool read_ok, uint64_t value, size_t readable_size)
{
    const PointerCheck check = CheckPointer(value, readable_size);
    const bool ok = read_ok && check.ok();
    std::printf(
        "%-24s slot=0x%016llX value=0x%016llX  [%s]\n"
        "                         nonzero=%s canonical=%s vad=%s readable=%s\n",
        label,
        static_cast<unsigned long long>(slot_address),
        static_cast<unsigned long long>(value),
        PassFail(ok),
        PassFail(check.non_zero),
        PassFail(check.canonical_user),
        check.vad_available ? PassFail(check.in_vad) : "N/A",
        PassFail(check.readable));
    return ok;
}

uint64_t Rol64(uint64_t value, unsigned bits)
{
    bits &= 63;
    if (bits == 0) {
        return value;
    }
    return (value << bits) | (value >> (64 - bits));
}

uint64_t Ror64(uint64_t value, unsigned bits)
{
    bits &= 63;
    if (bits == 0) {
        return value;
    }
    return (value >> bits) | (value << (64 - bits));
}

uint64_t TransformComponentValue(uint64_t value, uint64_t key_material, uint8_t key_byte)
{
    value ^= key_material;
    value ^= OW::offset::Component_Xor1;
    value = Ror64(value, OW::offset::Component_Ror1);
    value += OW::offset::Component_Add1;
    value ^= static_cast<uint64_t>(key_byte);
    value -= OW::offset::Component_Sub1;
    value = Ror64(value, OW::offset::Component_Ror2);
    value = Ror64(value, OW::offset::Component_Ror3);
    return value;
}

bool InitDma()
{
    LPSTR args[] = {
        const_cast<LPSTR>(""),
        const_cast<LPSTR>("-device"),
        const_cast<LPSTR>("fpga://algo=0"),
        const_cast<LPSTR>("-norefresh")
    };
    g_vmm = VMMDLL_Initialize(4, args);
    return g_vmm != nullptr;
}

bool AttachProcess(const std::string& process_name)
{
    if (!VMMDLL_PidGetFromName(g_vmm, const_cast<LPSTR>(process_name.c_str()), &g_pid) || !g_pid) {
        return false;
    }

    g_base = VMMDLL_ProcessGetModuleBaseU(g_vmm, g_pid, const_cast<LPSTR>(process_name.c_str()));
    if (!g_base) {
        return false;
    }

    PVMMDLL_MAP_MODULEENTRY module_entry = nullptr;
    if (VMMDLL_Map_GetModuleFromNameU(
            g_vmm,
            g_pid,
            const_cast<LPSTR>(process_name.c_str()),
            &module_entry,
            VMMDLL_MODULE_FLAG_NORMAL)) {
        g_image_size = module_entry->cbImageSize;
        VMMDLL_MemFree(module_entry);
    }

    if (!VMMDLL_Map_GetVadU(g_vmm, g_pid, FALSE, &g_vad_map)) {
        g_vad_map = nullptr;
    }

    return true;
}

int main(int argc, char** argv)
{
    const std::string process_name = (argc > 1 && argv[1][0] != '\0')
        ? argv[1]
        : "Overwatch.exe";

    std::printf("=== DMA Constants Verification ===\n");
    std::printf("Target process: %s\n\n", process_name.c_str());

    std::printf("[1] Initializing VMMDLL with fpga://algo=0...\n");
    if (!InitDma()) {
        std::printf("FAIL: VMMDLL_Initialize returned null.\n");
        return 1;
    }
    std::printf("PASS: VMM handle acquired.\n\n");

    std::printf("[2] Locating process and module base...\n");
    if (!AttachProcess(process_name)) {
        std::printf("FAIL: Could not resolve PID/base for %s.\n", process_name.c_str());
        VMMDLL_Close(g_vmm);
        return 1;
    }

    std::printf("PID        = %u\n", g_pid);
    std::printf("Base       = 0x%016llX\n", static_cast<unsigned long long>(g_base));
    std::printf("Image size = 0x%016llX\n", static_cast<unsigned long long>(g_image_size));
    if (g_vad_map) {
        std::printf("VAD map    = %u regions\n\n", g_vad_map->cMap);
    } else {
        std::printf("VAD map    = unavailable; pointer validation falls back to exact reads\n\n");
    }

    int resolved_chains = 0;
    constexpr int total_chains = 3;

    std::printf("[3] Fixed-address validation reads...\n");

    const uint64_t key_source_slot = g_base + kRvaKeySource;
    uint64_t p1 = 0;
    const bool p1_read_ok = ReadExact(key_source_slot, p1);
    const bool p1_ok = PrintPointer("L1 key source", key_source_slot, p1_read_ok, p1, sizeof(uint64_t));

    const uint64_t key_material_address = p1 + kKeyMaterialOffset;
    uint64_t key_material = 0;
    const bool key_material_read_ok = p1_ok && ReadExact(key_material_address, key_material);
    const bool key_material_ok = key_material_read_ok && key_material != 0;
    PrintRead64("L2 key material", key_material_address, key_material_ok, key_material);
    if (p1_ok && key_material_ok) {
        ++resolved_chains;
    }

    uint8_t key_byte = 0;
    const uint64_t key_byte_address = g_base + kRvaKeyByte;
    const bool key_byte_read_ok = ReadExact(key_byte_address, key_byte);
    const bool key_byte_ok = key_byte_read_ok && key_byte != 0;
    PrintRead8("key byte", key_byte_address, key_byte_ok, key_byte);

    uint8_t visibility_magic = 0;
    const uint64_t visibility_magic_address = g_base + kRvaVisibilityMagicByte;
    const bool visibility_magic_ok = ReadExact(visibility_magic_address, visibility_magic);
    PrintRead8("visibility magic byte", visibility_magic_address, visibility_magic_ok, visibility_magic);

    const uint64_t visibility_key_slot = g_base + kRvaVisibilityGlobalKeyPtr;
    uint64_t visibility_key_ptr = 0;
    const bool visibility_key_ptr_read_ok = ReadExact(visibility_key_slot, visibility_key_ptr);
    const bool visibility_key_ptr_ok = PrintPointer(
        "visibility key ptr",
        visibility_key_slot,
        visibility_key_ptr_read_ok,
        visibility_key_ptr,
        static_cast<size_t>(kVisibilityQwordOffset + sizeof(uint64_t)));

    uint64_t visibility_key1 = 0;
    const uint64_t visibility_key1_address = visibility_key_ptr + kVisibilityQwordOffset;
    const bool visibility_key1_ok =
        visibility_key_ptr_ok && ReadExact(visibility_key1_address, visibility_key1);
    PrintRead64("visibility key1", visibility_key1_address, visibility_key1_ok, visibility_key1);

    const uint64_t pair_array_slot = g_base + kRvaPairArray;
    uint64_t pair_array = 0;
    const bool pair_array_read_ok = ReadExact(pair_array_slot, pair_array);
    const bool pair_array_ok = PrintPointer("pair array ptr", pair_array_slot, pair_array_read_ok, pair_array, sizeof(Pair64));
    if (pair_array_ok) {
        ++resolved_chains;
    }

    const uint64_t pointer_g_slot = g_base + kRvaPointerG;
    uint64_t pointer_g = 0;
    const bool pointer_g_read_ok = ReadExact(pointer_g_slot, pointer_g);
    const bool pointer_g_ok = PrintPointer("secondary ptr", pointer_g_slot, pointer_g_read_ok, pointer_g, sizeof(uint64_t));
    if (pointer_g_ok) {
        ++resolved_chains;
    }

    std::printf("\n[4] Transform validation...\n");
    bool transform_nontrivial = false;
    bool transform_valid_pointer = false;
    uint64_t transformed = 0;

    if (p1_ok && key_material_ok && key_byte_ok && pair_array_ok) {
        Pair64 first_pair{};
        const bool first_pair_ok = ReadExact(pair_array, first_pair);
        std::printf(
            "first pair              @ 0x%016llX = {0x%016llX, 0x%016llX}  [%s]\n",
            static_cast<unsigned long long>(pair_array),
            static_cast<unsigned long long>(first_pair.first),
            static_cast<unsigned long long>(first_pair.second),
            PassFail(first_pair_ok));

        if (first_pair_ok) {
            transformed = TransformComponentValue(first_pair.first, key_material, key_byte);
            const PointerCheck transform_check = CheckPointer(transformed, sizeof(uint64_t));
            transform_nontrivial =
                transformed != 0 &&
                transformed != first_pair.first &&
                transformed != first_pair.second;
            transform_valid_pointer = transform_check.ok();

            std::printf(
                "transformed first qword             = 0x%016llX  [nontrivial=%s pointer=%s]\n"
                "                         nonzero=%s canonical=%s vad=%s readable=%s\n",
                static_cast<unsigned long long>(transformed),
                PassFail(transform_nontrivial),
                PassFail(transform_valid_pointer),
                PassFail(transform_check.non_zero),
                PassFail(transform_check.canonical_user),
                transform_check.vad_available ? PassFail(transform_check.in_vad) : "N/A",
                PassFail(transform_check.readable));
        }
    } else {
        std::printf("SKIP: required key material, key byte, or pair-array pointer did not validate.\n");
    }

    std::printf("\n[5] Summary\n");
    const bool overall_ok =
        resolved_chains == total_chains &&
        key_byte_ok &&
        transform_nontrivial &&
        transform_valid_pointer;
    std::printf("Pointer chains resolved: %d/%d\n", resolved_chains, total_chains);
    std::printf("Key byte check:          %s\n", PassFail(key_byte_ok));
    std::printf("Transform nontrivial:    %s\n", PassFail(transform_nontrivial));
    std::printf("Transform valid pointer: %s\n", PassFail(transform_valid_pointer));
    std::printf("Overall:                 %s\n", PassFail(overall_ok));

    if (g_vad_map) {
        VMMDLL_MemFree(g_vad_map);
        g_vad_map = nullptr;
    }
    VMMDLL_Close(g_vmm);
    return overall_ok ? 0 : 2;
}
