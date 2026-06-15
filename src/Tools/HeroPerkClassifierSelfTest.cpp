#include "Game/HeroPerks.hpp"
#include "Memory/Memory.h"

#include <cstring>
#include <cstdlib>

Memory::~Memory() = default;

namespace {

int Fail()
{
    return EXIT_FAILURE;
}

OW::HeroPerks::PointerTarget MakePointer(
    uint64_t offset,
    uint64_t pointer,
    const std::array<uint64_t, OW::HeroPerks::kTargetFieldOffsets.size()>& fields)
{
    OW::HeroPerks::PointerTarget target{};
    target.offset = offset;
    target.pointerRead = true;
    target.pointer = pointer;
    target.plausible = true;
    target.fieldRead.fill(true);
    target.fieldValue = fields;
    return target;
}

void FinishDerivedState(OW::HeroPerks::State& state)
{
    state.lookupReady = state.available &&
        state.e44Read &&
        state.e78Read &&
        OW::HeroPerks::HasRequiredPointerTargets(state);
    state.orderedSignature = OW::HeroPerks::BuildSmallOrderedSignature(state);
    state.clusterSignature = OW::HeroPerks::BuildClusterSignature(state);
    state.uniqueNo338Signature = OW::HeroPerks::BuildSmallUniqueNo338Signature(state);
    state.multisetSignature = OW::HeroPerks::BuildSmallMultisetSignature(state);
    state.orderedKey = OW::HeroPerks::BuildUltimateKey(state, state.orderedSignature);
    state.clusterKey = OW::HeroPerks::BuildUltimateKey(state, state.clusterSignature);
    state.uniqueNo338Key = OW::HeroPerks::BuildUltimateKey(state, state.uniqueNo338Signature);
    state.multisetKey = OW::HeroPerks::BuildUltimateKey(state, state.multisetSignature);
    state.stateCodeKey = OW::HeroPerks::BuildStateCodeKey(state);
    OW::HeroPerks::BuildResearchCandidateKeys(state);
    state.classification = OW::HeroPerks::Classify(state);
}

OW::HeroPerks::State MakeBaptisteUltimateRightFixture()
{
    OW::HeroPerks::State state{};
    state.available = true;
    state.heroId = 0x02E0000000000221ull;
    state.skillBase = 0x000001D40B396640ull;
    state.e44Read = true;
    state.e44U32 = 0x0000003Fu;
    state.e44U64 = 0x3621CB400000003Full;
    state.e78Read = true;
    state.e78U32 = 0x000001C9u;
    state.e78U64 = 0x000001DA000001C9ull;

    constexpr std::array<uint64_t, OW::HeroPerks::kTargetFieldOffsets.size()> repeated{
        0x00007FF6DB9E5328ull,
        0x000001D40B396750ull,
        0x00007FF6DBB10031ull,
        0x000000310000002Full,
        0x0000001900000011ull,
        0x0000001172020000ull,
    };
    constexpr std::array<uint64_t, OW::HeroPerks::kTargetFieldOffsets.size()> slot338{
        0x00007FF6DB9E5328ull,
        0x000001D40B396750ull,
        0x326222803263FFFEull,
        0xFFFFFFFE00000039ull,
        0x0000006432610011ull,
        0x3260001132020001ull,
    };
    constexpr std::array<uint64_t, OW::HeroPerks::kTargetFieldOffsets.size()> slot340{
        0x00007FF6DB9E5328ull,
        0x000001D40B396750ull,
        0x00007FF6DBB10015ull,
        0x0000001500000014ull,
        0x0000000500000003ull,
        0x0000000371010000ull,
    };
    constexpr std::array<uint64_t, OW::HeroPerks::kTargetFieldOffsets.size()> slot348{
        0x00007FF6DB9E5328ull,
        0x000001D40B396750ull,
        0x00007FF6DBB10019ull,
        0x0000001900000018ull,
        0x0000004600000003ull,
        0x0000000368010000ull,
    };

    state.pointers[0] = MakePointer(0x320, 0x000001D472CB6760ull, repeated);
    state.pointers[1] = MakePointer(0x328, 0x000001D472CB6760ull, repeated);
    state.pointers[2] = MakePointer(0x330, 0x000001D472CB6760ull, repeated);
    state.pointers[3] = MakePointer(0x338, 0x000001D4358B6760ull, slot338);
    state.pointers[4] = MakePointer(0x340, 0x000001D471E2A110ull, slot340);
    state.pointers[5] = MakePointer(0x348, 0x000001D468C23D30ull, slot348);
    FinishDerivedState(state);
    return state;
}

} // namespace

int main()
{
    OW::HeroPerks::State fixture = MakeBaptisteUltimateRightFixture();
    if (fixture.uniqueNo338Signature != 0xC565375C7A6B154Dull)
        return Fail();
    if (fixture.uniqueNo338Key != 0x4E6783755B6E2E59ull)
        return Fail();
    if (fixture.classification.result != OW::HeroPerks::Result::KnownTrue)
        return Fail();
    if (!fixture.classification.selected)
        return Fail();
    if (fixture.classification.key != fixture.uniqueNo338Key)
        return Fail();
    if (fixture.researchCandidateKeyCount != 5)
        return Fail();
    constexpr std::array<OW::HeroPerks::ResearchCandidateKey, 5> expectedResearchCandidates{ {
        { "norm_ordered_320_338", 0x7E2B9CAEF625FFD8ull, 0x8A79F6A3D29E8DD8ull },
        { "norm_unique_328_330_340", 0xCD0051A21FDA888Dull, 0xBEE3E884233EEEA5ull },
        { "norm_unique_320_328_348", 0x915B5A7173C88306ull, 0xB7413F5B52FC736Cull },
        { "norm_unique_340", 0x8DFDC5D4B5E7DFFFull, 0x50D02514AC649C1Eull },
        { "norm_unique_320_328", 0xE54C404BE7BCA572ull, 0x9BFB65061CFA8FE5ull },
    } };
    for (size_t index = 0; index < fixture.researchCandidateKeyCount; ++index) {
        const OW::HeroPerks::ResearchCandidateKey& actual = fixture.researchCandidateKeys[index];
        const OW::HeroPerks::ResearchCandidateKey& expected = expectedResearchCandidates[index];
        if (!actual.name || std::strcmp(actual.name, expected.name) != 0)
            return Fail();
        if (actual.signature != expected.signature || actual.key != expected.key)
            return Fail();
    }

    OW::HeroPerks::State changed338 = fixture;
    changed338.pointers[3].fieldValue[2] ^= 0x12340000ull;
    FinishDerivedState(changed338);
    if (changed338.uniqueNo338Signature != fixture.uniqueNo338Signature)
        return Fail();
    if (changed338.uniqueNo338Key != fixture.uniqueNo338Key)
        return Fail();

    OW::HeroPerks::State unknown = fixture;
    unknown.heroId ^= 0x1000ull;
    FinishDerivedState(unknown);
    if (OW::HeroPerks::IsKnown(unknown.classification.result))
        return Fail();
    if (unknown.classification.result != OW::HeroPerks::Result::UnknownMissing)
        return Fail();

    OW::HeroPerks::State notReady = fixture;
    notReady.lookupReady = false;
    notReady.classification = OW::HeroPerks::Classify(notReady);
    if (OW::HeroPerks::IsKnown(notReady.classification.result))
        return Fail();
    if (notReady.classification.result != OW::HeroPerks::Result::UnknownMissing)
        return Fail();

    OW::HeroPerks::State collision{};
    collision.lookupReady = true;
    collision.orderedKey = 0;
    collision.clusterKey = 0;
    collision.uniqueNo338Key = 0;
    collision.multisetKey = OW::HeroPerkLookupSeed::kMultisetCollisions[0];
    collision.classification = OW::HeroPerks::Classify(collision);
    if (collision.classification.result != OW::HeroPerks::Result::UnknownCollision)
        return Fail();
    if (OW::HeroPerks::IsKnown(collision.classification.result))
        return Fail();

    return EXIT_SUCCESS;
}
