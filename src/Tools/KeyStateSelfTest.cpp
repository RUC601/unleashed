#include "Memory/KeyState.hpp"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

Memory::~Memory() = default;

namespace {

int Fail(const char* message)
{
    std::printf("KeyStateSelfTest failed: %s\n", message);
    return EXIT_FAILURE;
}

bool ExpectNoProfile(DWORD build)
{
    return KeyState::SelectSessionSlotsProfile(build) == nullptr;
}

bool ExpectProfile(
    DWORD build,
    const char* label,
    const char* moduleName,
    uint64_t slotsRva,
    uint64_t keyStateOffset)
{
    const KeyState::SessionSlotsProfile* profile = KeyState::SelectSessionSlotsProfile(build);
    return profile &&
        std::strcmp(profile->label, label) == 0 &&
        std::strcmp(profile->moduleName, moduleName) == 0 &&
        profile->slotsRva == slotsRva &&
        profile->keyStateOffset == keyStateOffset;
}

bool TestProfileSelection()
{
    return ExpectNoProfile(17763) &&
        ExpectNoProfile(19045) &&
        ExpectNoProfile(22000) &&
        ExpectProfile(22621, "Win11 22H2/23H2", "win32ksgd.sys", 0x3110, 0x36A8) &&
        ExpectProfile(22631, "Win11 22H2/23H2", "win32ksgd.sys", 0x3110, 0x36A8) &&
        ExpectProfile(26099, "Win11 22H2/23H2", "win32ksgd.sys", 0x3110, 0x36A8) &&
        ExpectProfile(26100, "Win11 24H2", "win32k.sys", 0x824F0, 0x3808) &&
        ExpectProfile(26199, "Win11 24H2", "win32k.sys", 0x824F0, 0x3808) &&
        ExpectProfile(26200, "Win11 25H2+", "win32k.sys", 0x86678, 0x3808);
}

bool TestCompactBitmapDecode()
{
    std::array<uint8_t, 256> bitmap{};
    KeyState::keyStateByteCount.store(64, std::memory_order_release);

    constexpr int vkA = 'A';
    bitmap[static_cast<size_t>(vkA) / 4] =
        static_cast<uint8_t>(1u << ((vkA % 4) * 2));

    if (!KeyState::IsKeyDownInBitmap(bitmap, vkA))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, 'B'))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, -1))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, 256))
        return false;

    return true;
}

bool TestByteBitmapDecode()
{
    std::array<uint8_t, 256> bitmap{};
    KeyState::keyStateByteCount.store(256, std::memory_order_release);

    bitmap[VK_SPACE] = 0x80;
    bitmap[VK_SHIFT] = 0x01;

    if (!KeyState::IsKeyDownInBitmap(bitmap, VK_SPACE))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, VK_SHIFT))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, -1))
        return false;
    if (KeyState::IsKeyDownInBitmap(bitmap, 256))
        return false;

    return true;
}

bool TestRipRelativeHelper()
{
    std::vector<uint8_t> image(128, 0x90);
    constexpr size_t matchOffset = 16;
    constexpr size_t rel32OffsetInPattern = 3;
    constexpr size_t rel32Offset = matchOffset + rel32OffsetInPattern;
    constexpr uint64_t targetRva = 96;
    const int32_t rel32 = static_cast<int32_t>(targetRva - (rel32Offset + sizeof(int32_t)));

    image[matchOffset + 0] = 0x48;
    image[matchOffset + 1] = 0x8B;
    image[matchOffset + 2] = 0x05;
    std::memcpy(image.data() + rel32Offset, &rel32, sizeof(rel32));
    image[matchOffset + 7] = 0x48;
    image[matchOffset + 8] = 0x8B;
    image[matchOffset + 9] = 0x04;
    image[matchOffset + 10] = 0xC8;

    uint64_t resolvedRva = 0;
    if (!KeyState::TryResolveRipRelativeRvaInImage(
            image,
            { 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x8B, 0x04, 0xC8 },
            rel32OffsetInPattern,
            resolvedRva)) {
        return false;
    }
    if (resolvedRva != targetRva)
        return false;

    uint64_t rejectedRva = 0;
    if (KeyState::ComputeRipRelativeTargetRva(image.size(), rel32Offset, 4096, rejectedRva))
        return false;

    return true;
}

} // namespace

int main()
{
    if (!TestProfileSelection())
        return Fail("profile selection");
    if (!TestCompactBitmapDecode())
        return Fail("64-byte compact bitmap decode");
    if (!TestByteBitmapDecode())
        return Fail("256-byte bitmap decode");
    if (!TestRipRelativeHelper())
        return Fail("RIP-relative helper");

    KeyState::keyStateByteCount.store(KeyState::keyStateBitmap.size(), std::memory_order_release);
    return EXIT_SUCCESS;
}
