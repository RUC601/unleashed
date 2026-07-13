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
    KeyState::KeyStateBitmap bitmap{};
    KeyState::keyStateByteCount.store(KeyState::kKeyStateByteCount, std::memory_order_release);

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

    KeyState::KeyStateBitmap mouseBitmap{};
    mouseBitmap[static_cast<size_t>(VK_XBUTTON2) / 4] =
        static_cast<uint8_t>(1u << ((VK_XBUTTON2 % 4) * 2));

    const KeyState::KeyStateVkSample x1 = KeyState::SampleVkInBitmap(mouseBitmap, VK_XBUTTON1);
    const KeyState::KeyStateVkSample x2 = KeyState::SampleVkInBitmap(mouseBitmap, VK_XBUTTON2);
    if (!x1.available || x1.byteIndex != 1 || x1.downMask != 0x04 || x1.down)
        return false;
    if (!x2.available || x2.byteIndex != 1 || x2.downMask != 0x10 || x2.rawByte != 0x10 || !x2.down)
        return false;

    return true;
}

bool TestSessionProbeOrder()
{
    KeyState::KeyboardProxyProcess proxy{};

    proxy.sessionId = 2;
    std::vector<DWORD> sessions = KeyState::BuildSessionProbeList(proxy);
    if (sessions.size() != 2 || sessions[0] != 2 || sessions[1] != 1)
        return false;

    proxy.sessionId = 0;
    sessions = KeyState::BuildSessionProbeList(proxy);
    return sessions.size() == 2 && sessions[0] == 1 && sessions[1] == 2;
}

bool TestWin1124H2FallbackCandidates()
{
    const KeyState::SessionSlotsProfile* profile = KeyState::SelectSessionSlotsProfile(26100);
    if (!profile)
        return false;

    std::vector<KeyState::ResolverValueCandidate> slots;
    KeyState::AddProfileSlotsRvaCandidates(slots, 26100, *profile);
    if (slots.size() != 3 ||
        slots[0].value != 0x824F0 ||
        slots[1].value != 0x82530 ||
        slots[2].value != 0x82538) {
        return false;
    }

    std::vector<KeyState::ResolverValueCandidate> offsets;
    KeyState::AddProfileKeyOffsetCandidates(offsets, 26100, *profile);
    return offsets.size() == 2 &&
        offsets[0].value == 0x3808 &&
        offsets[1].value == 0x3830;
}

bool TestWin1124H2TrustedKeyOffsets()
{
    const KeyState::SessionSlotsProfile* profile = KeyState::SelectSessionSlotsProfile(26100);
    if (!profile)
        return false;

    return KeyState::IsProfileKeyOffsetCandidate(26100, *profile, 0x3808) &&
        KeyState::IsProfileKeyOffsetCandidate(26100, *profile, 0x3830) &&
        !KeyState::IsProfileKeyOffsetCandidate(26100, *profile, 0x3800);
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

bool TestU32ValueHelper()
{
    std::vector<uint8_t> image(64, 0x90);
    constexpr size_t matchOffset = 8;
    constexpr uint32_t keyStateOffset = 0x3830;

    image[matchOffset + 0] = 0x48;
    image[matchOffset + 1] = 0x8D;
    image[matchOffset + 2] = 0x90;
    std::memcpy(image.data() + matchOffset + 3, &keyStateOffset, sizeof(keyStateOffset));
    image[matchOffset + 7] = 0xE8;
    image[matchOffset + 12] = 0x0F;
    image[matchOffset + 13] = 0x57;
    image[matchOffset + 14] = 0xC0;

    uint64_t resolved = 0;
    return KeyState::TryResolveU32ValueInImage(
        image,
        { 0x48, 0x8D, 0x90, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x0F, 0x57, 0xC0 },
        3,
        resolved) &&
        resolved == keyStateOffset;
}

} // namespace

int main()
{
    if (!TestProfileSelection())
        return Fail("profile selection");
    if (!TestCompactBitmapDecode())
        return Fail("64-byte compact bitmap decode");
    if (!TestSessionProbeOrder())
        return Fail("session probe order");
    if (!TestWin1124H2FallbackCandidates())
        return Fail("Win11 24H2 fallback candidates");
    if (!TestWin1124H2TrustedKeyOffsets())
        return Fail("Win11 24H2 trusted key offsets");
    if (!TestRipRelativeHelper())
        return Fail("RIP-relative helper");
    if (!TestU32ValueHelper())
        return Fail("U32 value helper");

    KeyState::keyStateByteCount.store(KeyState::kKeyStateByteCount, std::memory_order_release);
    return EXIT_SUCCESS;
}
