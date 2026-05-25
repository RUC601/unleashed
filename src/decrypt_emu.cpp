#include "Game/DecryptEmulator.hpp"

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr uint64_t kDataBase = 0x60000000;
constexpr uint64_t kParentCodeAddr = 0x50010000;
constexpr uint64_t kComponentCodeAddr = 0x50020000;
constexpr uint32_t kGetGlobalKeyRva = 0x581D20;

uint64_t Ror64(uint64_t value, unsigned int bits) {
    bits &= 63;
    if (bits == 0) {
        return value;
    }
    return (value >> bits) | (value << (64 - bits));
}

uint64_t Rol64(uint64_t value, unsigned int bits) {
    bits &= 63;
    if (bits == 0) {
        return value;
    }
    return (value << bits) | (value >> (64 - bits));
}

void AppendU64(std::vector<uint8_t>& code, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        code.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void EmitMovRaxMoffs64(std::vector<uint8_t>& code, uint64_t addr) {
    code.push_back(0x48);
    code.push_back(0xA1);
    AppendU64(code, addr);
}

void EmitMovRcxImm64(std::vector<uint8_t>& code, uint64_t value) {
    code.push_back(0x48);
    code.push_back(0xB9);
    AppendU64(code, value);
}

void EmitMovRcxMem64(std::vector<uint8_t>& code, uint64_t addr) {
    EmitMovRcxImm64(code, addr);
    code.push_back(0x48);
    code.push_back(0x8B);
    code.push_back(0x09);
}

void EmitMovzxEcxBytePtrRcx(std::vector<uint8_t>& code) {
    code.push_back(0x0F);
    code.push_back(0xB6);
    code.push_back(0x09);
}

void EmitAddRaxRcx(std::vector<uint8_t>& code) {
    code.push_back(0x48);
    code.push_back(0x01);
    code.push_back(0xC8);
}

void EmitSubRaxRcx(std::vector<uint8_t>& code) {
    code.push_back(0x48);
    code.push_back(0x29);
    code.push_back(0xC8);
}

void EmitXorRaxRcx(std::vector<uint8_t>& code) {
    code.push_back(0x48);
    code.push_back(0x31);
    code.push_back(0xC8);
}

void EmitRolRaxImm8(std::vector<uint8_t>& code, uint8_t bits) {
    code.push_back(0x48);
    code.push_back(0xC1);
    code.push_back(0xC0);
    code.push_back(bits);
}

void EmitRorRaxImm8(std::vector<uint8_t>& code, uint8_t bits) {
    code.push_back(0x48);
    code.push_back(0xC1);
    code.push_back(0xC8);
    code.push_back(bits);
}

void EmitRet(std::vector<uint8_t>& code) {
    code.push_back(0xC3);
}

void PrintResult(uint64_t expected, uint64_t got, bool emulated) {
    std::printf("Expected: 0x%016llX\n", static_cast<unsigned long long>(expected));
    std::printf("Got:      0x%016llX\n", static_cast<unsigned long long>(got));
    std::printf("[%s]\n\n", (emulated && got == expected) ? "PASS" : "FAIL");
}

bool RunEmulateAtTest(DecryptEmulator& emu, const char* name,
                      uint64_t code_addr, const std::vector<uint8_t>& code,
                      uint64_t expected) {
    std::printf("[TEST %s] code_addr=0x%llX\n",
                name,
                static_cast<unsigned long long>(code_addr));
    std::printf("Emulating 0x%llX-0x%llX...\n",
                static_cast<unsigned long long>(code_addr),
                static_cast<unsigned long long>(code_addr + code.size()));

    uint64_t got = 0;
    const bool emulated = emu.EmulateAt(code_addr, code.data(), code.size(), got);
    PrintResult(expected, got, emulated);
    return emulated && got == expected;
}

uint64_t ReferenceGetParent(uint64_t encrypted) {
    uint64_t result = Ror64(encrypted, 32);
    result ^= 0x4B920A7072A077C5ull;
    result -= 0x107816B001CA79C8ull;
    result = Ror64(result, 35);
    result += 0xFD2150D0AEF24514ull;
    return result;
}

uint64_t ReferenceDecryptComponentTransform(uint64_t component,
                                            uint64_t key_material,
                                            uint8_t key_byte) {
    component += 0x4C8675CDE55BA1B2ull;
    component ^= key_material;
    component += 0x7BE57670994040F6ull;
    component ^= static_cast<uint64_t>(key_byte);
    component ^= 0x3864150DB528414Cull;
    component ^= 0xA4764E53CD34159Bull;
    component = Rol64(component, 0x2A);
    component = Ror64(component, 0x2D);
    return component;
}

std::vector<uint8_t> BuildGetParentCode() {
    std::vector<uint8_t> code;
    EmitMovRaxMoffs64(code, kDataBase);
    EmitRorRaxImm8(code, 0x20);
    EmitMovRcxImm64(code, 0x4B920A7072A077C5ull);
    EmitXorRaxRcx(code);
    EmitMovRcxImm64(code, 0x107816B001CA79C8ull);
    EmitSubRaxRcx(code);
    EmitRorRaxImm8(code, 0x23);
    EmitMovRcxImm64(code, 0xFD2150D0AEF24514ull);
    EmitAddRaxRcx(code);
    EmitRet(code);
    return code;
}

std::vector<uint8_t> BuildDecryptComponentCode() {
    std::vector<uint8_t> code;
    EmitMovRaxMoffs64(code, kDataBase);
    EmitMovRcxImm64(code, 0x4C8675CDE55BA1B2ull);
    EmitAddRaxRcx(code);
    EmitMovRcxMem64(code, kDataBase + 0x08);
    EmitXorRaxRcx(code);
    EmitMovRcxImm64(code, 0x7BE57670994040F6ull);
    EmitAddRaxRcx(code);
    EmitMovRcxImm64(code, kDataBase + 0x18);
    EmitMovzxEcxBytePtrRcx(code);
    EmitXorRaxRcx(code);
    EmitMovRcxImm64(code, 0x3864150DB528414Cull);
    EmitXorRaxRcx(code);
    EmitMovRcxImm64(code, 0xA4764E53CD34159Bull);
    EmitXorRaxRcx(code);
    EmitRolRaxImm8(code, 0x2A);
    EmitRorRaxImm8(code, 0x2D);
    EmitRet(code);
    return code;
}

bool RunGetParentTest(DecryptEmulator& emu) {
    const std::vector<uint8_t> code = BuildGetParentCode();
    const uint64_t encrypted = 0x0123456789ABCDEFull;
    const uint64_t expected = ReferenceGetParent(encrypted);

    std::printf("[TEST GetParent] code_addr=0x%llX\n",
                static_cast<unsigned long long>(kParentCodeAddr));
    std::printf("Emulating 0x%llX-0x%llX...\n",
                static_cast<unsigned long long>(kParentCodeAddr),
                static_cast<unsigned long long>(kParentCodeAddr + code.size()));

    uint64_t got = 0;
    const bool emulated = emu.EmulateGetParent(code.data(), code.size(), encrypted, got);
    PrintResult(expected, got, emulated);
    return emulated && got == expected;
}

bool RunDecryptComponentTest(DecryptEmulator& emu) {
    const std::vector<uint8_t> code = BuildDecryptComponentCode();
    const uint64_t component = 0x1122334455667788ull;
    const uint64_t key_material = 0xCAFEBABEDEADBEEFull;
    const uint8_t key_byte = 0x5A;
    const uint64_t expected =
        ReferenceDecryptComponentTransform(component, key_material, key_byte);

    std::printf("[TEST DecryptComponent transform] code_addr=0x%llX\n",
                static_cast<unsigned long long>(kComponentCodeAddr));
    std::printf("Emulating 0x%llX-0x%llX...\n",
                static_cast<unsigned long long>(kComponentCodeAddr),
                static_cast<unsigned long long>(kComponentCodeAddr + code.size()));

    uint64_t got = 0;
    const bool emulated = emu.EmulateDecryptComponent(
        code.data(),
        code.size(),
        component,
        key_byte,
        key_material,
        0,
        got);
    PrintResult(expected, got, emulated);
    return emulated && got == expected;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("[PE] Failed to open %s\n", path.c_str());
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        std::printf("[PE] Empty file: %s\n", path.c_str());
        return false;
    }

    bytes.resize(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    return file.good();
}

bool RvaToFileOffset(const std::vector<uint8_t>& image,
                     uint32_t rva,
                     size_t& file_offset,
                     uint64_t& image_base) {
    if (image.size() < sizeof(IMAGE_DOS_HEADER)) {
        return false;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
        return false;
    }

    const size_t nt_offset = static_cast<size_t>(dos->e_lfanew);
    if (nt_offset + sizeof(IMAGE_NT_HEADERS64) > image.size()) {
        return false;
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.data() + nt_offset);
    if (nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        return false;
    }

    image_base = nt->OptionalHeader.ImageBase;
    if (rva < nt->OptionalHeader.SizeOfHeaders) {
        file_offset = rva;
        return file_offset < image.size();
    }

    const IMAGE_SECTION_HEADER* section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const uint32_t start = section[i].VirtualAddress;
        const uint32_t span = section[i].Misc.VirtualSize > section[i].SizeOfRawData
            ? section[i].Misc.VirtualSize
            : section[i].SizeOfRawData;
        const uint32_t end = start + span;
        if (rva >= start && rva < end) {
            file_offset = section[i].PointerToRawData + (rva - start);
            return file_offset < image.size();
        }
    }

    return false;
}

void TryGetGlobalKeyFromPe(DecryptEmulator& emu, const std::string& path) {
    std::vector<uint8_t> image;
    if (!ReadFileBytes(path, image)) {
        return;
    }

    size_t file_offset = 0;
    uint64_t image_base = 0;
    if (!RvaToFileOffset(image, kGetGlobalKeyRva, file_offset, image_base)) {
        std::printf("[PE] Could not resolve RVA 0x%X\n", kGetGlobalKeyRva);
        return;
    }

    const size_t available = image.size() - file_offset;
    const size_t code_size = std::min<size_t>(available, 0x100);
    if (code_size == 0) {
        std::printf("[PE] No bytes available at RVA 0x%X\n", kGetGlobalKeyRva);
        return;
    }

    const uint64_t code_addr = image_base + kGetGlobalKeyRva;
    std::printf("[PE] GetGlobalKey RVA 0x%X -> file offset 0x%zX, image addr 0x%llX\n",
                kGetGlobalKeyRva,
                file_offset,
                static_cast<unsigned long long>(code_addr));
    std::printf("[TEST GetGlobalKey PE bytes] code_addr=0x%llX\n",
                static_cast<unsigned long long>(code_addr));
    std::printf("Emulating 0x%llX-0x%llX...\n",
                static_cast<unsigned long long>(code_addr),
                static_cast<unsigned long long>(code_addr + code_size));

    uint64_t got = 0;
    const bool ok = emu.EmulateGetGlobalKey(
        image.data() + file_offset,
        code_size,
        code_addr,
        got,
        true);
    if (ok) {
        std::printf("Got:      0x%016llX\n", static_cast<unsigned long long>(got));
        std::printf("[PASS]\n\n");
    } else {
        std::printf("[FAIL] PE bytes could not be emulated in the synthetic memory map\n\n");
    }
}

} // namespace

int main(int argc, char** argv) {
    DecryptEmulator emu;
    if (!emu.IsLoaded()) {
        std::printf("[ERROR] Failed to load unicorn.dll from the executable directory\n");
        return 1;
    }

    int passed = 0;
    int total = 0;

    {
        const uint64_t code_addr = 0x50000000;
        const std::vector<uint8_t> code = {
            0x48, 0xB8, 0xEF, 0xCD, 0xAB, 0x90, 0x78, 0x56, 0x34, 0x12,
            0x48, 0xC1, 0xC8, 0x0D,
            0xC3
        };
        const uint64_t expected = Ror64(0x1234567890ABCDEFull, 13);
        ++total;
        if (RunEmulateAtTest(emu, "ROR64", code_addr, code, expected)) {
            ++passed;
        }
    }

    {
        const uint64_t code_addr = 0x50001000;
        const std::vector<uint8_t> code = {
            0x48, 0xB8, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
            0x48, 0x35, 0xBB, 0xBB, 0xBB, 0xBB,
            0x48, 0x35, 0xCC, 0xCC, 0xCC, 0xCC,
            0xC3
        };
        const uint64_t expected =
            0xAAAAAAAAAAAAAAAAull ^ 0xFFFFFFFFBBBBBBBBull ^ 0xFFFFFFFFCCCCCCCCull;
        ++total;
        if (RunEmulateAtTest(emu, "XOR chain", code_addr, code, expected)) {
            ++passed;
        }
    }

    {
        const uint64_t code_addr = 0x50002000;
        const std::vector<uint8_t> code = {
            0x48, 0x8D, 0x05, 0xFC, 0x01, 0x00, 0x00,
            0xC3
        };
        const uint64_t expected = code_addr + 7 + 0x1FC;
        ++total;
        if (RunEmulateAtTest(emu, "LEA", code_addr, code, expected)) {
            ++passed;
        }
    }

    ++total;
    if (RunGetParentTest(emu)) {
        ++passed;
    }

    ++total;
    if (RunDecryptComponentTest(emu)) {
        ++passed;
    }

    std::printf("[SUMMARY] %d/%d tests passed\n\n", passed, total);

    if (argc > 1) {
        TryGetGlobalKeyFromPe(emu, argv[1]);
    }

    return passed == total ? 0 : 2;
}
