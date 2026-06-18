#include "Game/HeroPerks.hpp"
#include "Memory/Memory.h"

#include <cstring>
#include <cstdlib>
#include <initializer_list>

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

OW::HeroPerks::CandidateSlot MakeEmptyCandidateSlot(uint64_t offset)
{
    OW::HeroPerks::CandidateSlot slot{};
    slot.offset = offset;
    slot.qwordRead = true;
    slot.qword = 0;
    slot.pointerPlausible = false;
    OW::HeroPerks::FinishCandidateSlotDerivedFields(slot);
    return slot;
}

OW::HeroPerks::CandidateSlot MakeCandidateSlot(
    uint64_t offset,
    uint64_t semantic,
    uint32_t count,
    std::initializer_list<uint32_t> values,
    uint64_t qword = 0x0000010000001000ull)
{
    OW::HeroPerks::CandidateSlot slot{};
    slot.offset = offset;
    slot.qwordRead = true;
    slot.qword = qword + offset;
    slot.pointerPlausible = true;
    slot.targetSemanticSignature = semantic;
    slot.targetCompletion.countU32Read = true;
    slot.targetCompletion.countU32 = count;
    slot.targetCompletion.countPlausible = true;
    slot.targetCompletion.valuesRequested = values.size();
    slot.targetCompletion.valuesRead = values.size();
    size_t index = 0;
    for (const uint32_t value : values) {
        slot.targetCompletion.values[index++] = value;
        slot.targetCompletion.anyNonZero = slot.targetCompletion.anyNonZero || value != 0;
    }
    slot.targetCompletion.allOnes = values.size() > 0;
    for (size_t i = 0; i < slot.targetCompletion.valuesRead; ++i)
        slot.targetCompletion.allOnes =
            slot.targetCompletion.allOnes && slot.targetCompletion.values[i] == 1;
    return slot;
}

OW::HeroPerks::State MakeCandidateSlotState(uint64_t heroId)
{
    OW::HeroPerks::State state{};
    state.available = true;
    state.heroId = heroId;
    state.e44Read = true;
    for (size_t index = 0; index < OW::HeroPerks::kCandidateSlotOffsets.size(); ++index) {
        state.candidateSlots[index] =
            MakeEmptyCandidateSlot(OW::HeroPerks::kCandidateSlotOffsets[index]);
    }
    return state;
}

void PutCandidateSlot(OW::HeroPerks::State& state, OW::HeroPerks::CandidateSlot slot)
{
    for (OW::HeroPerks::CandidateSlot& existing : state.candidateSlots) {
        if (existing.offset == slot.offset) {
            existing = slot;
            return;
        }
    }
}

const OW::HeroPerks::CandidateSlotSelectedBoolean& ClassifyCandidateSlots(
    OW::HeroPerks::State& state)
{
    state.candidateSlotSelected = OW::HeroPerks::ClassifyCandidateSlotSelection(state);
    return state.candidateSlotSelected;
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
    OW::HeroPerks::EvaluateResearchSelectedBoolean(state);
    OW::HeroPerks::EvaluateRawSelectedBoolean(state);
    state.candidateSlotSelected = OW::HeroPerks::ClassifyCandidateSlotSelection(state);
    OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
}

OW::HeroPerks::StateScriptEa0cRecordSignal MakeStateScriptSignal(uint64_t qword50, uint64_t qword58)
{
    OW::HeroPerks::StateScriptEa0cRecordSignal signal{};
    signal.found = true;
    signal.qword50Read = true;
    signal.qword50 = qword50;
    signal.qword58Read = true;
    signal.qword58 = qword58;
    return signal;
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
    OW::HeroPerks::State unresolvedCandidate{};
    unresolvedCandidate.heroId = OW::GameData::MakeHeroId(0x005);
    unresolvedCandidate.researchSelected.skill16C2Read = true;
    unresolvedCandidate.researchSelected.skill16C2 = 1;
    OW::HeroPerks::EvaluateResearchSelectedBoolean(unresolvedCandidate);
    if (unresolvedCandidate.researchSelected.available || unresolvedCandidate.researchSelected.selected)
        return Fail();
    if (std::strcmp(unresolvedCandidate.researchSelected.rule, "candidate_unresolved_fail_closed") != 0)
        return Fail();

    OW::HeroPerks::State knownFalseCandidate{};
    knownFalseCandidate.heroId = OW::GameData::MakeHeroId(0x005);
    knownFalseCandidate.classification.result = OW::HeroPerks::Result::KnownFalse;
    knownFalseCandidate.classification.selected = false;
    OW::HeroPerks::EvaluateResearchSelectedBoolean(knownFalseCandidate);
    if (!knownFalseCandidate.researchSelected.available || knownFalseCandidate.researchSelected.selected)
        return Fail();
    if (std::strcmp(
            knownFalseCandidate.researchSelected.rule,
            "legacy_lookup_known_false_fail_closed") != 0)
        return Fail();

    OW::HeroPerks::State unsupportedCandidate{};
    unsupportedCandidate.heroId = OW::GameData::MakeHeroId(0x221);
    unsupportedCandidate.researchSelected.skill16C2Read = true;
    unsupportedCandidate.researchSelected.skill16C2 = 1;
    OW::HeroPerks::EvaluateResearchSelectedBoolean(unsupportedCandidate);
    if (unsupportedCandidate.researchSelected.available || unsupportedCandidate.researchSelected.selected)
        return Fail();

    constexpr uint64_t kHeroAna = OW::GameData::MakeHeroId(0x13B);
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroHanzo = OW::GameData::MakeHeroId(0x005);
    constexpr uint64_t kHeroOrisa = OW::GameData::MakeHeroId(0x13E);
    constexpr uint64_t kHeroPharah = OW::GameData::MakeHeroId(0x008);
    constexpr uint64_t kHeroReaper = OW::GameData::MakeHeroId(0x002);
    constexpr uint64_t kHeroReinhardt = OW::GameData::MakeHeroId(0x007);
    constexpr uint64_t kHeroSymmetra = OW::GameData::MakeHeroId(0x016);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);
    constexpr uint64_t kSemanticA = 0x1111222233334444ull;
    constexpr uint64_t kSemanticB = 0x5555666677778888ull;

    {
        bool selected = false;
        const OW::HeroPerks::StateScriptEa0cRecordSignal trueSignal =
            MakeStateScriptSignal(
                OW::HeroPerks::kStateScriptSelectedTrue50,
                OW::HeroPerks::kStateScriptSelectedTrue58);
        if (!OW::HeroPerks::TryClassifyStateScriptEa0cRecords(trueSignal, trueSignal, selected) ||
            !selected) {
            return Fail();
        }

        const OW::HeroPerks::StateScriptEa0cRecordSignal falseSignal =
            MakeStateScriptSignal(
                OW::HeroPerks::kStateScriptSelectedFalse50,
                OW::HeroPerks::kStateScriptSelectedFalse58);
        selected = true;
        if (!OW::HeroPerks::TryClassifyStateScriptEa0cRecords(falseSignal, falseSignal, selected) ||
            selected) {
            return Fail();
        }

        if (OW::HeroPerks::TryClassifyStateScriptEa0cRecords(trueSignal, falseSignal, selected))
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.available = true;
        state.heroId = kHeroHanzo;
        state.researchSelected.skill16C2Read = true;
        state.researchSelected.skill16C2 = 2;
        OW::HeroPerks::EvaluateRawSelectedBoolean(state);
        if (!state.rawSelected.available || !state.rawSelected.selected)
            return Fail();
        if (std::strcmp(state.rawSelected.rule, "skill_16c2_u16_nonzero") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.available = true;
        state.heroId = kHeroSymmetra;
        state.researchSelected.symmetra02E8Read = true;
        state.researchSelected.symmetra02E8 = 0x8000001800000006ull;
        OW::HeroPerks::EvaluateRawSelectedBoolean(state);
        if (!state.rawSelected.available || state.rawSelected.selected)
            return Fail();
        if (std::strcmp(state.rawSelected.rule, "symmetra_skill_02e8_high_80000018_low_lt_7") != 0)
            return Fail();

        state.researchSelected.symmetra02E8 = 0x8000001800000007ull;
        OW::HeroPerks::EvaluateRawSelectedBoolean(state);
        if (!state.rawSelected.available || !state.rawSelected.selected)
            return Fail();
        if (std::strcmp(state.rawSelected.rule, "symmetra_skill_02e8_high_80000018_low_ge_7") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.available = true;
        state.heroId = kHeroZarya;
        state.researchSelected.component55_270Read = true;
        state.researchSelected.component55_270 = 6;
        OW::HeroPerks::EvaluateRawSelectedBoolean(state);
        if (!state.rawSelected.available || !state.rawSelected.selected)
            return Fail();
        if (std::strcmp(state.rawSelected.rule, "zarya_component55_270_u32_eq_6") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || !state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "raw") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = false;
        state.candidateSlotSelected.family = "candidate_false_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "candidate_slot") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = true;
        state.candidateSlotSelected.family = "candidate_true_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || !state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "candidate_slot") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = true;
        state.candidateSlotSelected.family = "candidate_true_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || !state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "raw_and_candidate_slot") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = false;
        state.rawSelected.rule = "raw_false_fixture";
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = false;
        state.candidateSlotSelected.family = "candidate_false_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "raw_and_candidate_slot") != 0)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "known_unselected") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = false;
        state.candidateSlotSelected.family = "candidate_false_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unknown_raw_candidate_conflict_fail_closed") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unsupported_or_unread") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroDVa;
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "statescript_ea0c") != 0)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unknown_statescript_ea0c_unavailable_fail_closed") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroDVa;
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = false;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01C8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "statescript_ea0c") != 0)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "known_unselected") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroDVa;
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01C8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (!state.mergedSelected.available || !state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.rule, "ea0c_10127_support_table_20260616") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroDVa;
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        state.candidateSlotSelected.supportedHero = true;
        state.candidateSlotSelected.available = true;
        state.candidateSlotSelected.selected = true;
        state.candidateSlotSelected.family = "candidate_true_fixture";
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01D8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "statescript_ea0c") != 0)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unknown_statescript_ea0c_unavailable_fail_closed") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroDVa;
        state.rawSelected.supportedHero = true;
        state.rawSelected.available = true;
        state.rawSelected.selected = true;
        state.rawSelected.rule = "raw_true_fixture";
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 2;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01C8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.source, "statescript_ea0c") != 0)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unknown_statescript_ea0c_unavailable_fail_closed") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroOrisa;
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01E0;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unsupported_or_unread") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroReaper;
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01D8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unsupported_or_unread") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state{};
        state.heroId = kHeroPharah;
        state.stateScriptEa0cSelected.available = true;
        state.stateScriptEa0cSelected.selectedKnown = true;
        state.stateScriptEa0cSelected.selected = true;
        state.stateScriptEa0cSelected.sourceFound = true;
        state.stateScriptEa0cSelected.mapConsistent = true;
        state.stateScriptEa0cSelected.knownCandidateCount = 1;
        state.stateScriptEa0cSelected.matchedSourceOffset = 0x01D8;
        OW::HeroPerks::EvaluateMergedSelectedBoolean(state);
        if (state.mergedSelected.available || state.mergedSelected.selected)
            return Fail();
        if (std::strcmp(state.mergedSelected.result, "unsupported_or_unread") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroOrisa);
        PutCandidateSlot(state, MakeCandidateSlot(0x0B50, kSemanticA, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x0B58, kSemanticA, 0, {}));
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "b_record_0b50_0b58_nonzero_join") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroOrisa);
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "b_record_0b50_0b58_empty") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroHanzo);
        state.e44U32 = 0x1C;
        PutCandidateSlot(state, MakeCandidateSlot(0x320, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x328, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x330, kSemanticB, 6, { 0, 0, 0, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x338, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticB, 5, { 1, 1, 0, 1, 1 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "hanzo_e44_1c_0320_0328_0338_count6_join_0340_count5") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroHanzo);
        PutCandidateSlot(state, MakeCandidateSlot(0x338, kSemanticA, 6, { 0, 0, 0, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticB, 5, { 1, 1, 0, 1, 1 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "front_0340_count5_available") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroZarya);
        state.e44U32 = 0x06;
        PutCandidateSlot(state, MakeCandidateSlot(0x0B30, kSemanticA, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x0B38, kSemanticB, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x338, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x348, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "front_0338_0340_0348_count6_join_selected_boolean") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroZarya);
        state.e44U32 = 0x06;
        PutCandidateSlot(state, MakeCandidateSlot(0x0B30, kSemanticA, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x0B38, kSemanticB, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x338, kSemanticA, 3, { 1, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticB, 6, { 1, 1, 1, 0, 0, 0 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "front_0340_count6_right_swap") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroZarya);
        state.e44U32 = 0x06;
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticA, 6, { 1, 1, 1, 0, 0, 0 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "front_0338_0340_count6_join_without_primary_context") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroReinhardt);
        PutCandidateSlot(state, MakeCandidateSlot(0x328, kSemanticA, 5, { 1, 1, 0, 1, 1 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x330, kSemanticB, 14, { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x338, kSemanticA, 0x66, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x340, kSemanticB, 10, { 0, 1, 0, 0, 1, 0, 1, 1, 0, 0 }));
        PutCandidateSlot(state, MakeCandidateSlot(0x348, kSemanticA, 6, { 0, 0, 0, 0, 0, 0 }));
        const auto& selected = ClassifyCandidateSlots(state);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "front_0328_count5_0330_0338_0340_0348_selected_chain") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroAna);
        PutCandidateSlot(state, MakeCandidateSlot(0x0B50, kSemanticA, 0, {}));
        PutCandidateSlot(state, MakeCandidateSlot(0x0B58, kSemanticA, 0, {}));
        const auto& selected = ClassifyCandidateSlots(state);
        if (selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.family, "generic_b_record_0b50_0b58_nonzero_join") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::State state = MakeCandidateSlotState(kHeroDVa);
        const auto& selected = ClassifyCandidateSlots(state);
        if (selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.result, "unknown_no_family_rule") != 0)
            return Fail();
    }

    OW::HeroPerks::State fixture = MakeBaptisteUltimateRightFixture();
    if (fixture.uniqueNo338Signature != 0xC565375C7A6B154Dull)
        return Fail();
    if (fixture.uniqueNo338Key != 0x4E6783755B6E2E59ull)
        return Fail();
    if (fixture.classification.result != OW::HeroPerks::Result::KnownTrue)
        return Fail();
    if (!fixture.classification.selected)
        return Fail();
    if (!fixture.researchSelected.available || !fixture.researchSelected.selected)
        return Fail();
    if (std::strcmp(fixture.researchSelected.rule, "legacy_lookup_known_true_fail_closed") != 0)
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

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000006u;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.skill0348TargetRead = true;
        selected.skill0348Target = 0x0000010000000000ull;
        selected.skill0348TargetPlausible = true;
        selected.skill0348Target1D4Read = true;
        selected.skill0348Target1D4 = 0x00000000u;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000002u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000002u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_selected_statescript_major_component21_02e8_eq_2") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000001u;
        selected.primaryGateActive = false;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.skill0348TargetRead = true;
        selected.skill0348Target = 0x0000010000000000ull;
        selected.skill0348TargetPlausible = true;
        selected.skill0348Target1D4Read = true;
        selected.skill0348Target1D4 = 0x00000002u;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000002u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000002u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_unselected_primary_gate_e44_no_pri") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000006u;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.skill0348TargetRead = true;
        selected.skill0348Target = 0x0000010000000000ull;
        selected.skill0348TargetPlausible = true;
        selected.skill0348Target1D4Read = true;
        selected.skill0348Target1D4 = 0x00000002u;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000000u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000000u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_unselected_statescript_major_component21_02e8_eq_0") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000006u;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.skill0348TargetRead = true;
        selected.skill0348Target = 0x0000010000000000ull;
        selected.skill0348TargetPlausible = true;
        selected.skill0348Target1D4Read = true;
        selected.skill0348Target1D4 = 0x00000002u;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000000u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000002u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_selected_statescript_major_component21_02e8_eq_2") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x0000005Eu;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = false;
        selected.majorSelected = false;
        selected.skill0348TargetRead = true;
        selected.skill0348Target = 0x0000010000000000ull;
        selected.skill0348TargetPlausible = true;
        selected.skill0348Target1D4Read = true;
        selected.skill0348Target1D4 = 0x00000000u;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000000u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000002u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || !selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "inferred_selected_e44_not_no_pri_component21_02e8_eq_2") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000006u;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = false;
        selected.component21_0228Read = true;
        selected.component21_0228 = 0x00000000u;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000002u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_unselected_statescript_major_not_selected") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x0000003Fu;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000001u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (!selected.available || selected.selected)
            return Fail();
        if (std::strcmp(
                selected.result,
                "known_unselected_statescript_major_component21_02e8_eq_1") != 0)
            return Fail();
    }

    {
        OW::HeroPerks::AnaHeadshotSelectedBoolean selected{};
        selected.supportedHero = true;
        selected.primaryGateRead = true;
        selected.primaryGateE44 = 0x00000006u;
        selected.primaryGateActive = true;
        selected.majorSelectedKnown = true;
        selected.majorSelected = true;
        selected.component21_02E8Read = true;
        selected.component21_02E8 = 0x00000003u;
        OW::HeroPerks::ClassifyAnaHeadshotSelectedBoolean(selected);
        if (selected.available || selected.selected)
            return Fail();
        if (std::strcmp(selected.result, "unknown_component21_02e8_value") != 0)
            return Fail();
    }

    return EXIT_SUCCESS;
}
