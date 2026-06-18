#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "Game/GameData.hpp"
#include "Game/HeroPerkLookupSeed.hpp"
#include "Game/SDK.hpp"

namespace OW {
uintptr_t DecryptComponent(uintptr_t parent, uint32_t idx);
}

namespace OW::HeroPerks {

inline constexpr uint64_t kPointerBegin = 0x320;
inline constexpr uint64_t kPointerEnd = 0x348;
inline constexpr std::array<uint64_t, 6> kPointerOffsets{
    0x320, 0x328, 0x330, 0x338, 0x340, 0x348
};
inline constexpr std::array<uint64_t, 5> kUniqueNo338Offsets{
    0x320, 0x328, 0x330, 0x340, 0x348
};
inline constexpr std::array<uint64_t, 6> kTargetFieldOffsets{
    0x00, 0x38, 0x40, 0x48, 0x50, 0x58
};
inline constexpr std::array<uint64_t, 11> kCandidateSlotOffsets{
    0x320, 0x328, 0x330, 0x338, 0x340, 0x348,
    0x0B30, 0x0B38, 0x0B50, 0x0B58, 0x0B60
};
// Matches the StateScript Var Pool bucket layout: 16 buckets at +0x20,
// each bucket 0x20 bytes wide with its entry count at +0x08.
inline constexpr uint64_t kStateScriptScanEnd = 0x2200;
inline constexpr uint64_t kStateScriptScanStride = sizeof(uint64_t);
inline constexpr uint64_t kStateScriptResolvedStorageOffset = 0xC0;
inline constexpr uint64_t kStateScriptStorageBucketBegin = 0x20;
inline constexpr uint64_t kStateScriptStorageBucketStride = 0x20;
inline constexpr uint64_t kStateScriptStorageBucketCountOffset = 0x08;
inline constexpr uint64_t kStateScriptStorageEntryStride = 0x10;
inline constexpr uint64_t kStateScriptStorageEntryRecordOffset = 0x08;
inline constexpr uint32_t kStateScriptStorageBucketCount = 16;
inline constexpr uint32_t kMaxPlausibleStateScriptEntriesPerBucket = 1024;
inline constexpr uint32_t kMaxStateScriptStorageShapeEntriesPerBucket = 64;
inline constexpr uint32_t kStateScriptCandidateMinValidBuckets = 4;
inline constexpr uint32_t kStateScriptCandidateMinMatchedIds = 16;
inline constexpr uint32_t kStateScriptEa0cRecordId = 0x0000EA0C;
inline constexpr uint32_t kStateScript10127RecordId = 0x00010127;
inline constexpr uint64_t kStateScriptSelectedFalse50 = 0x0000000000000001ull;
inline constexpr uint64_t kStateScriptSelectedFalse58 = 0x0000000100000000ull;
inline constexpr uint64_t kStateScriptSelectedTrue50 = 0x0000000000000000ull;
inline constexpr uint64_t kStateScriptSelectedTrue58 = 0x0000000200000000ull;
inline constexpr uint64_t kTargetCompletionArrayPtrOffset = 0x370;
inline constexpr uint64_t kTargetCompletionArrayCountOffset = 0x378;
inline constexpr uint64_t kTargetCompletionFlagsOffset = 0x3A8;
inline constexpr uint32_t kMaxPlausibleCompletionCount = 128;
inline constexpr size_t kMaxCompletionValuesToRead = 32;
inline constexpr uint64_t kAnaComponent7fScanBegin = 0x000;
inline constexpr uint64_t kAnaComponent7fScanEnd = 0x200;
inline constexpr uint64_t kAnaComponent7fScanStride = sizeof(uint64_t);
inline constexpr size_t kAnaComponent7fScanQwordCount =
    (kAnaComponent7fScanEnd - kAnaComponent7fScanBegin) / kAnaComponent7fScanStride;
inline constexpr uint64_t kAnaComponent7fRawSlotScanBegin = 0x000;
inline constexpr uint64_t kAnaComponent7fRawSlotScanEnd = 0x240;
inline constexpr uint64_t kAnaComponent7fRawSlotScanStride = sizeof(uint64_t);
inline constexpr size_t kAnaComponent7fRawSlotScanQwordCount =
    (kAnaComponent7fRawSlotScanEnd - kAnaComponent7fRawSlotScanBegin) /
    kAnaComponent7fRawSlotScanStride;

enum class Result {
    UnknownMissing,
    UnknownCollision,
    KnownFalse,
    KnownTrue,
};

enum class PointerSignatureMode {
    Ordered,
    Multiset,
    Unique,
};

struct PointerTarget {
    uint64_t offset = 0;
    bool pointerRead = false;
    uint64_t pointer = 0;
    bool plausible = false;
    std::array<bool, kTargetFieldOffsets.size()> fieldRead{};
    std::array<uint64_t, kTargetFieldOffsets.size()> fieldValue{};
};

struct CompletionArraySignature {
    bool flagsRead = false;
    uint32_t flagsU32 = 0;
    uint64_t flagsU64 = 0;
    bool pointerRead = false;
    uint64_t pointer = 0;
    bool pointerPlausible = false;
    bool countU32Read = false;
    uint32_t countU32 = 0;
    bool countU64Read = false;
    uint64_t countU64 = 0;
    bool countPlausible = false;
    size_t valuesRequested = 0;
    size_t valuesRead = 0;
    std::array<uint32_t, kMaxCompletionValuesToRead> values{};
    bool allOnes = false;
    bool anyNonZero = false;
};

struct CandidateSlot {
    uint64_t offset = 0;
    bool qwordRead = false;
    uint64_t qword = 0;
    bool pointerPlausible = false;
    std::array<bool, kTargetFieldOffsets.size()> fieldRead{};
    std::array<uint64_t, kTargetFieldOffsets.size()> fieldValue{};
    CompletionArraySignature targetCompletion{};
    uint64_t targetFieldSignature = 0;
    uint64_t targetCompletionSignature = 0;
    uint64_t targetSemanticSignature = 0;
};

struct SmallTupleKey {
    bool plausible = false;
    std::array<bool, 7> read{};
    std::array<uint16_t, 7> value{};

    bool operator<(const SmallTupleKey& other) const
    {
        if (plausible != other.plausible)
            return plausible < other.plausible;
        if (read != other.read)
            return read < other.read;
        return value < other.value;
    }
};

struct CheckedTier {
    const char* tier = "";
    uint64_t key = 0;
    bool hit = false;
    bool collision = false;
};

struct Classification {
    Result result = Result::UnknownMissing;
    const char* tier = "";
    uint64_t key = 0;
    bool selected = false;
    std::array<CheckedTier, 4> checked{};
    size_t checkedCount = 0;
};

struct ResearchCandidateKey {
    const char* name = "";
    uint64_t signature = 0;
    uint64_t key = 0;
};

struct ResearchSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selected = false;
    const char* rule = "";
    bool skill16C2Read = false;
    uint16_t skill16C2 = 0;
    bool symmetra02E8Read = false;
    uint64_t symmetra02E8 = 0;
    uint64_t component55Base = 0;
    bool component55_270Read = false;
    uint32_t component55_270 = 0;
};

struct CandidateSlotSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selected = false;
    const char* result = "unknown_missing_candidate_slots";
    const char* family = "";
    const char* confidence = "none";
    const char* side = "";
    std::array<uint64_t, 6> matchedOffsets{};
    size_t matchedOffsetCount = 0;
};

struct RawSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selected = false;
    const char* rule = "";
};

struct MergedSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selected = false;
    const char* result = "unknown_no_selected_evidence";
    const char* source = "";
    const char* rule = "";
};

struct StateScriptEa0cRecordSignal {
    bool found = false;
    bool qword50Read = false;
    uint64_t qword50 = 0;
    bool qword58Read = false;
    uint64_t qword58 = 0;
};

struct StateScriptEa0cSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selectedKnown = false;
    bool selected = false;
    const char* result = "not_evaluated";
    const char* resolution = "not_resolved";
    bool heroMapKnown = false;
    uint64_t mappedSourceOffset = 0;
    bool mapConsistent = false;
    uint32_t knownCandidateCount = 0;
    bool sourceFound = false;
    uint64_t matchedSourceOffset = 0;
    StateScriptEa0cRecordSignal recordEa0c{};
    StateScriptEa0cRecordSignal record10127{};
};

struct AnaHeadshotSelectedBoolean {
    bool supportedHero = false;
    bool available = false;
    bool selected = false;
    const char* result = "not_evaluated";
    bool primaryGateRead = false;
    uint32_t primaryGateE44 = 0;
    bool primaryGateActive = false;
    bool majorSelectedKnown = false;
    bool majorSelected = false;
    bool skill02E8Read = false;
    uint64_t skill02E8 = 0;
    uint32_t skill02E8High32 = 0;
    uint32_t skill02E8Low32 = 0;
    bool skill02E8GateKnown = false;
    bool skill02E8GateActive = false;
    bool skill09ARead = false;
    uint16_t skill09A = 0;
    bool skill592Read = false;
    uint16_t skill592 = 0;
    bool skill0BD0Read = false;
    uint64_t skill0BD0 = 0;
    bool skill0348TargetRead = false;
    uint64_t skill0348Target = 0;
    bool skill0348TargetPlausible = false;
    bool skill0348Target1D4Read = false;
    uint32_t skill0348Target1D4 = 0;
    bool skill0348Target1D4QwordRead = false;
    uint64_t skill0348Target1D4Qword = 0;
    uint64_t component21Base = 0;
    bool component21_021CRead = false;
    uint32_t component21_021C = 0;
    bool component21_0228Read = false;
    uint32_t component21_0228 = 0;
    bool component21_0228QwordRead = false;
    uint64_t component21_0228Qword = 0;
    bool component21_02E0Read = false;
    uint32_t component21_02E0 = 0;
    bool component21_02E8Read = false;
    uint32_t component21_02E8 = 0;
    bool component21_02E8QwordRead = false;
    uint64_t component21_02E8Qword = 0;
    uint64_t component22Base = 0;
    bool component22_0140Read = false;
    uint16_t component22_0140 = 0;
    bool component22_01F2Read = false;
    uint16_t component22_01F2 = 0;
    bool component22_0200Read = false;
    uint32_t component22_0200 = 0;
    bool component22_0202Read = false;
    uint16_t component22_0202 = 0;
    bool component22_0203Read = false;
    uint8_t component22_0203 = 0;
    bool component22_0350Read = false;
    uint16_t component22_0350 = 0;
    uint64_t component7fBase = 0;
    bool component7fD4Read = false;
    uint64_t component7fD4 = 0;
    bool component7fD8Read = false;
    uint32_t component7fD8 = 0;
    bool component7fD8QwordRead = false;
    uint64_t component7fD8Qword = 0;
    uint64_t component7fScanHash = 0;
    size_t component7fScanReadCount = 0;
    size_t component7fScanNonzeroCount = 0;
    std::array<bool, kAnaComponent7fScanQwordCount> component7fScanRead{};
    std::array<uint64_t, kAnaComponent7fScanQwordCount> component7fScanQword{};
    bool component7fRawSlotBitsetRead = false;
    bool component7fRawSlotPresent = false;
    bool component7fRawSlotIndexRead = false;
    bool component7fRawSlotTableRead = false;
    bool component7fRawSlotTablePlausible = false;
    bool component7fRawSlotRead = false;
    bool component7fRawSlotPlausible = false;
    uint64_t component7fRawSlotBitset = 0;
    uint8_t component7fRawSlotIndexBase = 0;
    uint64_t component7fRawSlotComponentIndex = 0;
    uint64_t component7fRawSlotTable = 0;
    uint64_t component7fRawSlotBase = 0;
    bool component7fRawSlotD8Read = false;
    uint32_t component7fRawSlotD8 = 0;
    bool component7fRawSlotD8QwordRead = false;
    uint64_t component7fRawSlotD8Qword = 0;
    uint64_t component7fRawSlotScanHash = 0;
    size_t component7fRawSlotScanReadCount = 0;
    size_t component7fRawSlotScanNonzeroCount = 0;
    std::array<bool, kAnaComponent7fRawSlotScanQwordCount> component7fRawSlotScanRead{};
    std::array<uint64_t, kAnaComponent7fRawSlotScanQwordCount> component7fRawSlotScanQword{};
};

struct StateScriptEa0cStorageMatch {
    uint64_t sourceOffset = 0;
    uint64_t owner = 0;
    uint64_t storageBase = 0;
    StateScriptEa0cRecordSignal recordEa0c{};
    StateScriptEa0cRecordSignal record10127{};
    bool selected = false;
};

struct State {
    bool available = false;
    bool lookupReady = false;
    uint64_t heroId = 0;
    uint64_t skillBase = 0;
    uint64_t componentParent = 0;
    bool e44Read = false;
    uint32_t e44U32 = 0;
    uint64_t e44U64 = 0;
    bool e78Read = false;
    uint32_t e78U32 = 0;
    uint64_t e78U64 = 0;
    std::array<PointerTarget, kPointerOffsets.size()> pointers{};
    std::array<CandidateSlot, kCandidateSlotOffsets.size()> candidateSlots{};
    uint64_t orderedSignature = 0;
    uint64_t clusterSignature = 0;
    uint64_t uniqueNo338Signature = 0;
    uint64_t multisetSignature = 0;
    uint64_t orderedKey = 0;
    uint64_t clusterKey = 0;
    uint64_t uniqueNo338Key = 0;
    uint64_t multisetKey = 0;
    uint64_t stateCodeKey = 0;
    std::array<ResearchCandidateKey, 5> researchCandidateKeys{};
    size_t researchCandidateKeyCount = 0;
    ResearchSelectedBoolean researchSelected{};
    RawSelectedBoolean rawSelected{};
    CandidateSlotSelectedBoolean candidateSlotSelected{};
    MergedSelectedBoolean mergedSelected{};
    StateScriptEa0cSelectedBoolean stateScriptEa0cSelected{};
    AnaHeadshotSelectedBoolean anaHeadshotSelected{};
    Classification classification{};
};

inline const char* ResultName(Result result)
{
    switch (result) {
    case Result::KnownTrue: return "known_true";
    case Result::KnownFalse: return "known_false";
    case Result::UnknownCollision: return "unknown_collision";
    case Result::UnknownMissing:
    default: return "unknown_missing";
    }
}

inline bool IsKnown(Result result)
{
    return result == Result::KnownTrue || result == Result::KnownFalse;
}

inline bool IsPlausibleUserPointer(uint64_t value)
{
    return value >= 0x10000ull &&
        value < 0x0000800000000000ull &&
        (value % alignof(void*) == 0);
}

template <typename T>
inline bool TryRead(uint64_t address, T& value)
{
    value = {};
    return SDK && SDK->read_range(address, &value, sizeof(T));
}

inline uint32_t Popcount64(uint64_t value)
{
    value -= (value >> 1) & 0x5555555555555555ull;
    value = (value & 0x3333333333333333ull) +
        ((value >> 2) & 0x3333333333333333ull);
    value = (value + (value >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return static_cast<uint32_t>((value * 0x0101010101010101ull) >> 56);
}

struct RawComponentSlotProbe {
    bool bitsetRead = false;
    bool present = false;
    bool indexRead = false;
    bool tableRead = false;
    bool tablePlausible = false;
    bool slotRead = false;
    bool slotPlausible = false;
    uint64_t bitset = 0;
    uint8_t indexBase = 0;
    uint64_t table = 0;
    uint64_t componentIndex = 0;
    uint64_t slot = 0;
};

inline RawComponentSlotProbe ProbeRawComponentSlot(uint64_t parent, uint32_t type)
{
    RawComponentSlotProbe probe{};
    if (!IsPlausibleUserPointer(parent))
        return probe;

    const uint32_t bucket = type / 64u;
    const uint32_t shift = type % 64u;
    const uint64_t bitMask = 1ull << shift;
    const uint64_t lowerMask = shift == 0 ? 0ull : ((1ull << shift) - 1ull);

    probe.bitsetRead = TryRead(parent + 0x110 + 8ull * bucket, probe.bitset);
    probe.present = probe.bitsetRead && ((probe.bitset & bitMask) != 0);
    probe.indexRead = TryRead(parent + 0x130 + bucket, probe.indexBase);
    probe.tableRead = TryRead(parent + 0x80, probe.table);
    probe.tablePlausible = probe.tableRead && IsPlausibleUserPointer(probe.table);

    if (!probe.present || !probe.indexRead || !probe.tablePlausible)
        return probe;

    probe.componentIndex =
        static_cast<uint64_t>(probe.indexBase) + Popcount64(probe.bitset & lowerMask);
    probe.slotRead = TryRead(probe.table + 8ull * probe.componentIndex, probe.slot);
    probe.slotPlausible = probe.slotRead && IsPlausibleUserPointer(probe.slot);
    return probe;
}

inline uint64_t Fnv1aUpdate(uint64_t hash, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ull;
    }
    return hash;
}

template <typename T>
inline uint64_t Fnv1aValue(uint64_t hash, const T& value)
{
    return Fnv1aUpdate(hash, &value, sizeof(T));
}

inline uint16_t Low16(uint64_t value)
{
    return static_cast<uint16_t>(value & 0xFFFFu);
}

inline PointerTarget ReadPointerTarget(uint64_t skillBase, uint64_t offset)
{
    PointerTarget target{};
    target.offset = offset;
    target.pointerRead = TryRead(skillBase + offset, target.pointer);
    target.plausible = target.pointerRead && IsPlausibleUserPointer(target.pointer);
    if (!target.plausible)
        return target;

    for (size_t index = 0; index < kTargetFieldOffsets.size(); ++index)
        target.fieldRead[index] =
            TryRead(target.pointer + kTargetFieldOffsets[index], target.fieldValue[index]);
    return target;
}

inline CompletionArraySignature ReadTargetCompletionArray(uint64_t object)
{
    CompletionArraySignature entry{};
    entry.flagsRead = TryRead(object + kTargetCompletionFlagsOffset, entry.flagsU32);
    TryRead(object + kTargetCompletionFlagsOffset, entry.flagsU64);
    entry.pointerRead = TryRead(object + kTargetCompletionArrayPtrOffset, entry.pointer);
    entry.pointerPlausible = entry.pointerRead && IsPlausibleUserPointer(entry.pointer);
    entry.countU32Read = TryRead(object + kTargetCompletionArrayCountOffset, entry.countU32);
    entry.countU64Read = TryRead(object + kTargetCompletionArrayCountOffset, entry.countU64);
    entry.countPlausible =
        entry.countU32Read && entry.countU32 <= kMaxPlausibleCompletionCount;

    if (!entry.pointerPlausible || !entry.countPlausible)
        return entry;

    entry.valuesRequested =
        std::min<size_t>(entry.countU32, kMaxCompletionValuesToRead);
    for (size_t index = 0; index < entry.valuesRequested; ++index) {
        uint32_t value = 0;
        if (!TryRead(entry.pointer + index * sizeof(uint32_t), value))
            break;
        entry.values[index] = value;
        ++entry.valuesRead;
        entry.anyNonZero = entry.anyNonZero || value != 0;
    }
    entry.allOnes = entry.valuesRead > 0;
    for (size_t index = 0; index < entry.valuesRead; ++index)
        entry.allOnes = entry.allOnes && entry.values[index] == 1;
    return entry;
}

inline uint64_t BuildCandidateTargetFieldSignature(const CandidateSlot& slot)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, slot.pointerPlausible);
    if (!slot.pointerPlausible)
        return hash;

    constexpr std::array<size_t, 4> kFieldIndexes{ 2, 3, 4, 5 };
    for (const size_t fieldIndex : kFieldIndexes) {
        hash = Fnv1aValue(hash, kTargetFieldOffsets[fieldIndex]);
        hash = Fnv1aValue(hash, slot.fieldRead[fieldIndex]);
        if (!slot.fieldRead[fieldIndex])
            continue;
        const uint16_t low16 = Low16(slot.fieldValue[fieldIndex]);
        const uint16_t high16 = Low16(slot.fieldValue[fieldIndex] >> 32);
        hash = Fnv1aValue(hash, low16);
        hash = Fnv1aValue(hash, high16);
    }
    return hash;
}

inline uint64_t BuildCandidateCompletionSignature(const CompletionArraySignature& completion)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, completion.countU32Read);
    hash = Fnv1aValue(hash, completion.countU32);
    hash = Fnv1aValue(hash, completion.countPlausible);
    hash = Fnv1aValue(hash, completion.valuesRead);
    for (size_t index = 0; index < completion.valuesRead; ++index)
        hash = Fnv1aValue(hash, completion.values[index]);
    hash = Fnv1aValue(hash, completion.allOnes);
    hash = Fnv1aValue(hash, completion.anyNonZero);
    return hash;
}

inline uint64_t BuildCandidateTargetSemanticSignature(const CandidateSlot& slot)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, slot.pointerPlausible);
    hash = Fnv1aValue(hash, slot.targetFieldSignature);
    hash = Fnv1aValue(hash, slot.targetCompletionSignature);
    return hash;
}

inline void FinishCandidateSlotDerivedFields(CandidateSlot& slot)
{
    slot.targetFieldSignature = BuildCandidateTargetFieldSignature(slot);
    slot.targetCompletionSignature =
        BuildCandidateCompletionSignature(slot.targetCompletion);
    slot.targetSemanticSignature = BuildCandidateTargetSemanticSignature(slot);
}

inline CandidateSlot ReadCandidateSlot(uint64_t skillBase, uint64_t offset)
{
    CandidateSlot slot{};
    slot.offset = offset;
    slot.qwordRead = TryRead(skillBase + offset, slot.qword);
    slot.pointerPlausible = slot.qwordRead && IsPlausibleUserPointer(slot.qword);
    if (slot.pointerPlausible) {
        for (size_t index = 0; index < kTargetFieldOffsets.size(); ++index) {
            slot.fieldRead[index] =
                TryRead(slot.qword + kTargetFieldOffsets[index], slot.fieldValue[index]);
        }
        slot.targetCompletion = ReadTargetCompletionArray(slot.qword);
    }
    FinishCandidateSlotDerivedFields(slot);
    return slot;
}

inline bool TryGetStateScriptEa0cSourceOffset(uint64_t heroId, uint64_t& sourceOffset)
{
    constexpr uint64_t kHeroReaper = OW::GameData::MakeHeroId(0x002);
    constexpr uint64_t kHeroTracer = OW::GameData::MakeHeroId(0x003);
    constexpr uint64_t kHeroHanzo = OW::GameData::MakeHeroId(0x005);
    constexpr uint64_t kHeroReinhardt = OW::GameData::MakeHeroId(0x007);
    constexpr uint64_t kHeroWinston = OW::GameData::MakeHeroId(0x009);
    constexpr uint64_t kHeroRoadhog = OW::GameData::MakeHeroId(0x040);
    constexpr uint64_t kHeroCassidy = OW::GameData::MakeHeroId(0x042);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroDoomfist = OW::GameData::MakeHeroId(0x12F);
    constexpr uint64_t kHeroOrisa = OW::GameData::MakeHeroId(0x13E);
    constexpr uint64_t kHeroBaptiste = OW::GameData::MakeHeroId(0x221);
    constexpr uint64_t kHeroJunkerQueen = OW::GameData::MakeHeroId(0x236);
    constexpr uint64_t kHeroRamattra = OW::GameData::MakeHeroId(0x28D);

    switch (heroId) {
    case kHeroZarya:
        sourceOffset = 0x01C0;
        return true;
    case kHeroBaptiste:
    case kHeroDVa:
    case kHeroWinston:
        sourceOffset = 0x01C8;
        return true;
    case kHeroCassidy:
    case kHeroDoomfist:
    case kHeroRamattra:
    case kHeroRoadhog:
    case kHeroTracer:
        sourceOffset = 0x01D0;
        return true;
    case kHeroHanzo:
    case kHeroJunkerQueen:
    case kHeroReaper:
    case kHeroReinhardt:
        sourceOffset = 0x01D8;
        return true;
    case kHeroOrisa:
        sourceOffset = 0x01E0;
        return true;
    default:
        sourceOffset = 0;
        return false;
    }
}

inline bool TryGetMergedStateScriptEa0cSupportOffset(uint64_t heroId, uint64_t& sourceOffset)
{
    constexpr uint64_t kHeroMercy = OW::GameData::MakeHeroId(0x004);
    constexpr uint64_t kHeroZenyatta = OW::GameData::MakeHeroId(0x020);
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroRamattra = OW::GameData::MakeHeroId(0x28D);

    switch (heroId) {
    case kHeroDVa:
    case kHeroZenyatta:
        sourceOffset = 0x01C8;
        return true;
    case kHeroRamattra:
        sourceOffset = 0x01D0;
        return true;
    case kHeroMercy:
        sourceOffset = 0x01E0;
        return true;
    default:
        sourceOffset = 0;
        return false;
    }
}

inline bool StateScriptStorageLooksPlausible(uint64_t storageBase)
{
    if (!IsPlausibleUserPointer(storageBase))
        return false;

    uint32_t validBuckets = 0;
    uint32_t totalIdsRead = 0;
    uint32_t totalIdsBucketMatched = 0;
    for (uint32_t bucketIndex = 0; bucketIndex < kStateScriptStorageBucketCount; ++bucketIndex) {
        const uint64_t bucketAddress =
            storageBase +
            kStateScriptStorageBucketBegin +
            static_cast<uint64_t>(bucketIndex) * kStateScriptStorageBucketStride;
        uint64_t entries = 0;
        uint32_t count = 0;
        if (!TryRead(bucketAddress, entries) ||
            !TryRead(bucketAddress + kStateScriptStorageBucketCountOffset, count)) {
            continue;
        }
        if (count > kMaxPlausibleStateScriptEntriesPerBucket)
            return false;
        if (count == 0)
            continue;
        if (!IsPlausibleUserPointer(entries))
            return false;

        uint32_t idsRead = 0;
        uint32_t idsBucketMatched = 0;
        const uint32_t toRead =
            std::min<uint32_t>(count, kMaxStateScriptStorageShapeEntriesPerBucket);
        for (uint32_t entryIndex = 0; entryIndex < toRead; ++entryIndex) {
            const uint64_t entryAddress =
                entries + static_cast<uint64_t>(entryIndex) * kStateScriptStorageEntryStride;
            uint32_t id = 0;
            if (!TryRead(entryAddress, id))
                continue;
            ++idsRead;
            if ((id & 0x0Fu) == bucketIndex)
                ++idsBucketMatched;
        }
        if (idsRead > 0 && idsRead == idsBucketMatched)
            ++validBuckets;
        totalIdsRead += idsRead;
        totalIdsBucketMatched += idsBucketMatched;
    }

    return validBuckets >= kStateScriptCandidateMinValidBuckets &&
        totalIdsRead > 0 &&
        totalIdsBucketMatched >= kStateScriptCandidateMinMatchedIds &&
        totalIdsBucketMatched == totalIdsRead;
}

inline StateScriptEa0cRecordSignal ReadStateScriptEa0cRecordSignal(
    uint64_t storageBase,
    uint32_t recordId)
{
    StateScriptEa0cRecordSignal signal{};
    if (!IsPlausibleUserPointer(storageBase))
        return signal;

    const uint32_t bucketIndex = recordId & 0x0Fu;
    const uint64_t bucketAddress =
        storageBase +
        kStateScriptStorageBucketBegin +
        static_cast<uint64_t>(bucketIndex) * kStateScriptStorageBucketStride;
    uint64_t entries = 0;
    uint32_t count = 0;
    if (!TryRead(bucketAddress, entries) ||
        !TryRead(bucketAddress + kStateScriptStorageBucketCountOffset, count) ||
        !IsPlausibleUserPointer(entries) ||
        count == 0 ||
        count > kMaxPlausibleStateScriptEntriesPerBucket) {
        return signal;
    }

    for (uint32_t entryIndex = 0; entryIndex < count; ++entryIndex) {
        const uint64_t entryAddress =
            entries + static_cast<uint64_t>(entryIndex) * kStateScriptStorageEntryStride;
        uint32_t id = 0;
        if (!TryRead(entryAddress, id) || id != recordId)
            continue;

        uint64_t recordPointer = 0;
        if (!TryRead(entryAddress + kStateScriptStorageEntryRecordOffset, recordPointer) ||
            !IsPlausibleUserPointer(recordPointer)) {
            return signal;
        }

        signal.found = true;
        signal.qword50Read = TryRead(recordPointer + 0x50, signal.qword50);
        signal.qword58Read = TryRead(recordPointer + 0x58, signal.qword58);
        return signal;
    }
    return signal;
}

inline bool StateScriptEa0cRecordMatches(
    const StateScriptEa0cRecordSignal& signal,
    uint64_t qword50,
    uint64_t qword58)
{
    return signal.found &&
        signal.qword50Read &&
        signal.qword58Read &&
        signal.qword50 == qword50 &&
        signal.qword58 == qword58;
}

inline bool TryClassifyStateScriptEa0cRecords(
    const StateScriptEa0cRecordSignal& recordEa0c,
    const StateScriptEa0cRecordSignal& record10127,
    bool& selected)
{
    const bool ea0cTrue =
        StateScriptEa0cRecordMatches(
            recordEa0c,
            kStateScriptSelectedTrue50,
            kStateScriptSelectedTrue58);
    const bool record10127True =
        StateScriptEa0cRecordMatches(
            record10127,
            kStateScriptSelectedTrue50,
            kStateScriptSelectedTrue58);
    const bool ea0cFalse =
        StateScriptEa0cRecordMatches(
            recordEa0c,
            kStateScriptSelectedFalse50,
            kStateScriptSelectedFalse58);
    const bool record10127False =
        StateScriptEa0cRecordMatches(
            record10127,
            kStateScriptSelectedFalse50,
            kStateScriptSelectedFalse58);

    if (ea0cTrue && record10127True) {
        selected = true;
        return true;
    }
    if (ea0cFalse && record10127False) {
        selected = false;
        return true;
    }
    return false;
}

inline bool TryBuildStateScriptEa0cStorageMatch(
    uint64_t sourceOffset,
    uint64_t owner,
    uint64_t storageBase,
    StateScriptEa0cStorageMatch& match)
{
    match = {};
    if (!StateScriptStorageLooksPlausible(storageBase))
        return false;

    match.sourceOffset = sourceOffset;
    match.owner = owner;
    match.storageBase = storageBase;
    match.recordEa0c =
        ReadStateScriptEa0cRecordSignal(storageBase, kStateScriptEa0cRecordId);
    match.record10127 =
        ReadStateScriptEa0cRecordSignal(storageBase, kStateScript10127RecordId);
    return TryClassifyStateScriptEa0cRecords(
        match.recordEa0c,
        match.record10127,
        match.selected);
}

inline void EvaluateStateScriptEa0cSelectedBoolean(State& state)
{
    StateScriptEa0cSelectedBoolean& result = state.stateScriptEa0cSelected;
    result.heroMapKnown =
        TryGetStateScriptEa0cSourceOffset(state.heroId, result.mappedSourceOffset);
    result.supportedHero = result.heroMapKnown;
    if (!state.available) {
        result.result = "state_unavailable";
        return;
    }

    std::vector<uint64_t> seenStorageBases;
    std::vector<StateScriptEa0cStorageMatch> knownMatches;
    StateScriptEa0cStorageMatch mappedMatch{};
    bool mappedSourceSeen = false;

    for (uint64_t sourceOffset = 0;
         sourceOffset <= kStateScriptScanEnd;
         sourceOffset += kStateScriptScanStride) {
        uint64_t object = 0;
        if (!TryRead(state.skillBase + sourceOffset, object) ||
            !IsPlausibleUserPointer(object)) {
            continue;
        }

        uint64_t storageBase = 0;
        if (!TryRead(object + kStateScriptResolvedStorageOffset, storageBase) ||
            !IsPlausibleUserPointer(storageBase) ||
            std::find(seenStorageBases.begin(), seenStorageBases.end(), storageBase) !=
                seenStorageBases.end()) {
            continue;
        }
        seenStorageBases.push_back(storageBase);

        StateScriptEa0cStorageMatch match{};
        if (!TryBuildStateScriptEa0cStorageMatch(sourceOffset, object, storageBase, match))
            continue;

        if (result.heroMapKnown && sourceOffset == result.mappedSourceOffset) {
            mappedSourceSeen = true;
            mappedMatch = match;
        }
        knownMatches.push_back(match);
    }

    result.knownCandidateCount = static_cast<uint32_t>(knownMatches.size());
    if (knownMatches.size() == 1) {
        const StateScriptEa0cStorageMatch& match = knownMatches.front();
        result.supportedHero = true;
        result.available = true;
        result.selectedKnown = true;
        result.selected = match.selected;
        result.result = match.selected ? "known_selected" : "known_unselected";
        result.resolution = "unique_record_match";
        result.sourceFound = true;
        result.matchedSourceOffset = match.sourceOffset;
        result.mapConsistent =
            !result.heroMapKnown || match.sourceOffset == result.mappedSourceOffset;
        result.recordEa0c = match.recordEa0c;
        result.record10127 = match.record10127;
        return;
    }
    if (knownMatches.size() > 1) {
        result.result = "ambiguous_known_candidates";
        return;
    }

    if (mappedSourceSeen) {
        result.supportedHero = true;
        result.sourceFound = true;
        result.matchedSourceOffset = mappedMatch.sourceOffset;
        result.mapConsistent = true;
        result.recordEa0c = mappedMatch.recordEa0c;
        result.record10127 = mappedMatch.record10127;
        result.result = "mapped_source_unclassified";
        return;
    }

    result.result = result.heroMapKnown ? "mapped_source_missing" : "no_known_candidate";
}

inline SmallTupleKey BuildSmallTupleKey(const PointerTarget& pointer)
{
    SmallTupleKey key{};
    key.plausible = pointer.plausible;
    if (!pointer.plausible)
        return key;

    constexpr size_t kField40Index = 2;
    constexpr size_t kField48Index = 3;
    constexpr size_t kField50Index = 4;
    constexpr size_t kField58Index = 5;

    const auto setPart = [&](size_t part, size_t fieldIndex, uint64_t shift) {
        key.read[part] = pointer.fieldRead[fieldIndex];
        if (key.read[part])
            key.value[part] = Low16(pointer.fieldValue[fieldIndex] >> shift);
    };

    setPart(0, kField40Index, 0);
    setPart(1, kField48Index, 0);
    setPart(2, kField48Index, 32);
    setPart(3, kField50Index, 0);
    setPart(4, kField50Index, 32);
    setPart(5, kField58Index, 0);
    setPart(6, kField58Index, 32);
    return key;
}

inline uint64_t HashSmallTuple(uint64_t hash, const SmallTupleKey& key)
{
    hash = Fnv1aValue(hash, key.plausible);
    for (size_t index = 0; index < key.value.size(); ++index) {
        hash = Fnv1aValue(hash, key.read[index]);
        if (key.read[index])
            hash = Fnv1aValue(hash, key.value[index]);
    }
    return hash;
}

inline const PointerTarget* FindPointer(const State& state, uint64_t offset)
{
    const auto it = std::find_if(
        state.pointers.begin(),
        state.pointers.end(),
        [offset](const PointerTarget& pointer) {
            return pointer.offset == offset;
        });
    return it == state.pointers.end() ? nullptr : &(*it);
}

inline bool HasRequiredPointerTargets(const State& state)
{
    for (const uint64_t offset : kPointerOffsets) {
        const PointerTarget* pointer = FindPointer(state, offset);
        if (!pointer || !pointer->pointerRead || !pointer->plausible)
            return false;

        for (const bool read : pointer->fieldRead) {
            if (!read)
                return false;
        }
    }
    return true;
}

inline uint64_t BuildClusterSignature(const State& state)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);
    hash = Fnv1aValue(hash, state.e44U32);

    for (const PointerTarget& pointer : state.pointers) {
        hash = Fnv1aValue(hash, pointer.offset);
        hash = Fnv1aValue(hash, pointer.plausible);
        for (size_t index = 0; index < kTargetFieldOffsets.size(); ++index) {
            hash = Fnv1aValue(hash, kTargetFieldOffsets[index]);
            hash = Fnv1aValue(hash, pointer.fieldRead[index]);
            if (pointer.fieldRead[index])
                hash = Fnv1aValue(hash, pointer.fieldValue[index]);
        }
    }
    return hash;
}

inline uint64_t BuildSmallOrderedSignature(const State& state)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);
    for (const PointerTarget& pointer : state.pointers) {
        hash = Fnv1aValue(hash, pointer.offset);
        hash = HashSmallTuple(hash, BuildSmallTupleKey(pointer));
    }
    return hash;
}

inline uint64_t BuildSmallMultisetSignature(const State& state)
{
    std::vector<SmallTupleKey> keys;
    keys.reserve(state.pointers.size());
    for (const PointerTarget& pointer : state.pointers)
        keys.push_back(BuildSmallTupleKey(pointer));
    std::sort(keys.begin(), keys.end());

    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);
    for (const SmallTupleKey& key : keys)
        hash = HashSmallTuple(hash, key);
    return hash;
}

inline void UniqueSortedSmallTupleKeys(std::vector<SmallTupleKey>& keys)
{
    std::sort(keys.begin(), keys.end());
    keys.erase(
        std::unique(
            keys.begin(),
            keys.end(),
            [](const SmallTupleKey& a, const SmallTupleKey& b) {
                return !(a < b) && !(b < a);
            }),
        keys.end());
}

template <size_t N>
inline uint64_t BuildSmallSubsetSignature(
    const State& state,
    PointerSignatureMode mode,
    const std::array<uint64_t, N>& offsets)
{
    std::vector<SmallTupleKey> keys;
    keys.reserve(offsets.size());

    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);

    if (mode == PointerSignatureMode::Ordered) {
        for (const uint64_t offset : offsets) {
            hash = Fnv1aValue(hash, offset);
            if (const PointerTarget* pointer = FindPointer(state, offset))
                hash = HashSmallTuple(hash, BuildSmallTupleKey(*pointer));
            else
                hash = HashSmallTuple(hash, SmallTupleKey{});
        }
        return hash;
    }

    for (const uint64_t offset : offsets) {
        if (const PointerTarget* pointer = FindPointer(state, offset))
            keys.push_back(BuildSmallTupleKey(*pointer));
        else
            keys.push_back(SmallTupleKey{});
    }

    std::sort(keys.begin(), keys.end());
    if (mode == PointerSignatureMode::Unique)
        UniqueSortedSmallTupleKeys(keys);

    for (const SmallTupleKey& key : keys)
        hash = HashSmallTuple(hash, key);
    return hash;
}

inline uint64_t BuildSmallUniqueNo338Signature(const State& state)
{
    return BuildSmallSubsetSignature(state, PointerSignatureMode::Unique, kUniqueNo338Offsets);
}

inline uint64_t BuildUltimateKey(const State& state, uint64_t signature)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);
    hash = Fnv1aValue(hash, state.e44U32);
    hash = Fnv1aValue(hash, state.e78U32);
    hash = Fnv1aValue(hash, signature);
    return hash;
}

inline uint64_t BuildStateCodeKey(const State& state)
{
    uint64_t hash = 14695981039346656037ull;
    hash = Fnv1aValue(hash, state.heroId);
    hash = Fnv1aValue(hash, state.e44U32);
    hash = Fnv1aValue(hash, state.e78U32);
    return hash;
}

inline void AddResearchCandidateKey(State& state, const char* name, uint64_t signature)
{
    if (state.researchCandidateKeyCount >= state.researchCandidateKeys.size())
        return;

    state.researchCandidateKeys[state.researchCandidateKeyCount++] = ResearchCandidateKey{
        name,
        signature,
        BuildUltimateKey(state, signature),
    };
}

inline void BuildResearchCandidateKeys(State& state)
{
    constexpr std::array<uint64_t, 2> kOrdered320338{ 0x320, 0x338 };
    constexpr std::array<uint64_t, 3> kUnique328330340{ 0x328, 0x330, 0x340 };
    constexpr std::array<uint64_t, 3> kUnique320328348{ 0x320, 0x328, 0x348 };
    constexpr std::array<uint64_t, 1> kUnique340{ 0x340 };
    constexpr std::array<uint64_t, 2> kUnique320328{ 0x320, 0x328 };

    state.researchCandidateKeyCount = 0;
    AddResearchCandidateKey(
        state,
        "norm_ordered_320_338",
        BuildSmallSubsetSignature(state, PointerSignatureMode::Ordered, kOrdered320338));
    AddResearchCandidateKey(
        state,
        "norm_unique_328_330_340",
        BuildSmallSubsetSignature(state, PointerSignatureMode::Unique, kUnique328330340));
    AddResearchCandidateKey(
        state,
        "norm_unique_320_328_348",
        BuildSmallSubsetSignature(state, PointerSignatureMode::Unique, kUnique320328348));
    AddResearchCandidateKey(
        state,
        "norm_unique_340",
        BuildSmallSubsetSignature(state, PointerSignatureMode::Unique, kUnique340));
    AddResearchCandidateKey(
        state,
        "norm_unique_320_328",
        BuildSmallSubsetSignature(state, PointerSignatureMode::Unique, kUnique320328));
}

inline bool IsResearchSelectedSupportedHero(uint64_t heroId)
{
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroHanzo = OW::GameData::MakeHeroId(0x005);
    constexpr uint64_t kHeroJunkerQueen = OW::GameData::MakeHeroId(0x236);
    constexpr uint64_t kHeroOrisa = OW::GameData::MakeHeroId(0x13E);
    constexpr uint64_t kHeroReinhardt = OW::GameData::MakeHeroId(0x007);
    constexpr uint64_t kHeroSymmetra = OW::GameData::MakeHeroId(0x016);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);

    switch (heroId) {
    case kHeroDVa:
    case kHeroHanzo:
    case kHeroJunkerQueen:
    case kHeroOrisa:
    case kHeroReinhardt:
    case kHeroSymmetra:
    case kHeroZarya:
        return true;
    default:
        return false;
    }
}

inline bool IsCandidateSlotSelectedSupportedHero(uint64_t heroId)
{
    constexpr uint64_t kHeroAna = OW::GameData::MakeHeroId(0x13B);
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroHanzo = OW::GameData::MakeHeroId(0x005);
    constexpr uint64_t kHeroJunkerQueen = OW::GameData::MakeHeroId(0x236);
    constexpr uint64_t kHeroOrisa = OW::GameData::MakeHeroId(0x13E);
    constexpr uint64_t kHeroReinhardt = OW::GameData::MakeHeroId(0x007);
    constexpr uint64_t kHeroSymmetra = OW::GameData::MakeHeroId(0x016);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);

    switch (heroId) {
    case kHeroAna:
    case kHeroDVa:
    case kHeroHanzo:
    case kHeroJunkerQueen:
    case kHeroOrisa:
    case kHeroReinhardt:
    case kHeroSymmetra:
    case kHeroZarya:
        return true;
    default:
        return false;
    }
}

inline const CandidateSlot* FindCandidateSlot(const State& state, uint64_t offset)
{
    const auto it = std::find_if(
        state.candidateSlots.begin(),
        state.candidateSlots.end(),
        [offset](const CandidateSlot& slot) {
            return slot.offset == offset;
        });
    return it == state.candidateSlots.end() ? nullptr : &(*it);
}

template <size_t N>
inline bool CompletionValuesEqual(
    const CandidateSlot* slot,
    uint32_t count,
    const std::array<uint32_t, N>& values)
{
    if (!slot ||
        !slot->targetCompletion.countU32Read ||
        slot->targetCompletion.countU32 != count ||
        slot->targetCompletion.valuesRead != values.size()) {
        return false;
    }

    for (size_t index = 0; index < values.size(); ++index) {
        if (slot->targetCompletion.values[index] != values[index])
            return false;
    }
    return true;
}

inline bool IsCount5Ultimate(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 5> kValues{ 1, 1, 0, 1, 1 };
    return CompletionValuesEqual(slot, 5, kValues);
}

inline bool IsCount6Enabled(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 6> kValues{ 1, 1, 1, 0, 0, 0 };
    return CompletionValuesEqual(slot, 6, kValues);
}

inline bool IsCount6Empty(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 6> kValues{ 0, 0, 0, 0, 0, 0 };
    return CompletionValuesEqual(slot, 6, kValues);
}

inline bool IsCount3Left(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 3> kValues{ 1, 0, 0 };
    return CompletionValuesEqual(slot, 3, kValues);
}

inline bool IsCount10Right(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 10> kValues{ 0, 1, 0, 0, 1, 0, 1, 1, 0, 0 };
    return CompletionValuesEqual(slot, 10, kValues);
}

inline bool IsCount14Empty(const CandidateSlot* slot)
{
    constexpr std::array<uint32_t, 14> kValues{ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    return CompletionValuesEqual(slot, 14, kValues);
}

inline bool IsCount66Combo(const CandidateSlot* slot)
{
    return slot &&
        slot->targetCompletion.countU32Read &&
        slot->targetCompletion.countU32 == 0x66;
}

inline bool SameSemantic(const CandidateSlot* a, const CandidateSlot* b)
{
    return a &&
        b &&
        a->pointerPlausible &&
        b->pointerPlausible &&
        a->targetSemanticSignature != 0 &&
        a->targetSemanticSignature == b->targetSemanticSignature;
}

inline bool NonzeroTarget(const CandidateSlot* slot)
{
    constexpr uint64_t kZeroSemantic = 0x6738829C9F5491B9ull;
    return slot &&
        slot->qwordRead &&
        slot->qword != 0 &&
        slot->pointerPlausible &&
        slot->targetSemanticSignature != kZeroSemantic;
}

template <size_t N>
inline void SetCandidateSlotResult(
    CandidateSlotSelectedBoolean& result,
    bool available,
    bool selected,
    const char* resultName,
    const char* family,
    const char* confidence,
    const char* side,
    const std::array<uint64_t, N>& offsets)
{
    result.available = available;
    result.selected = available ? selected : false;
    result.result = resultName;
    result.family = family;
    result.confidence = confidence;
    result.side = side ? side : "";
    result.matchedOffsetCount =
        std::min<size_t>(offsets.size(), result.matchedOffsets.size());
    for (size_t index = 0; index < result.matchedOffsetCount; ++index)
        result.matchedOffsets[index] = offsets[index];
}

inline CandidateSlotSelectedBoolean ClassifyCandidateSlotSelection(const State& state)
{
    CandidateSlotSelectedBoolean result{};
    result.supportedHero = IsCandidateSlotSelectedSupportedHero(state.heroId);
    if (!state.available || !result.supportedHero) {
        result.result = result.supportedHero
            ? "unknown_unread"
            : "unsupported_or_unread";
        return result;
    }

    constexpr uint64_t kHeroAna = OW::GameData::MakeHeroId(0x13B);
    constexpr uint64_t kHeroDVa = OW::GameData::MakeHeroId(0x07A);
    constexpr uint64_t kHeroHanzo = OW::GameData::MakeHeroId(0x005);
    constexpr uint64_t kHeroJunkerQueen = OW::GameData::MakeHeroId(0x236);
    constexpr uint64_t kHeroOrisa = OW::GameData::MakeHeroId(0x13E);
    constexpr uint64_t kHeroReinhardt = OW::GameData::MakeHeroId(0x007);
    constexpr uint64_t kHeroSymmetra = OW::GameData::MakeHeroId(0x016);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);

    const CandidateSlot* s320 = FindCandidateSlot(state, 0x320);
    const CandidateSlot* s328 = FindCandidateSlot(state, 0x328);
    const CandidateSlot* s330 = FindCandidateSlot(state, 0x330);
    const CandidateSlot* s338 = FindCandidateSlot(state, 0x338);
    const CandidateSlot* s340 = FindCandidateSlot(state, 0x340);
    const CandidateSlot* s348 = FindCandidateSlot(state, 0x348);
    const CandidateSlot* b30 = FindCandidateSlot(state, 0x0B30);
    const CandidateSlot* b38 = FindCandidateSlot(state, 0x0B38);
    const CandidateSlot* b50 = FindCandidateSlot(state, 0x0B50);
    const CandidateSlot* b58 = FindCandidateSlot(state, 0x0B58);
    const CandidateSlot* b60 = FindCandidateSlot(state, 0x0B60);

    if (state.heroId == kHeroAna || state.heroId == kHeroHanzo) {
        if (state.heroId == kHeroHanzo &&
            state.e44Read &&
            state.e44U32 == 0x1C &&
            IsCount6Enabled(s320) &&
            IsCount6Enabled(s328) &&
            IsCount6Enabled(s338) &&
            SameSemantic(s320, s328) &&
            SameSemantic(s320, s338) &&
            IsCount6Empty(s330) &&
            IsCount5Ultimate(s340)) {
            SetCandidateSlotResult(
                result,
                true,
                true,
                "known_selected",
                "hanzo_e44_1c_0320_0328_0338_count6_join_0340_count5",
                "medium",
                "",
                std::array<uint64_t, 5>{ 0x320, 0x328, 0x330, 0x338, 0x340 });
            return result;
        }
        if (IsCount5Ultimate(s338) &&
            IsCount5Ultimate(s340) &&
            SameSemantic(s338, s340)) {
            SetCandidateSlotResult(
                result,
                true,
                true,
                "known_selected",
                "front_0338_0340_count5_join",
                "high",
                "",
                std::array<uint64_t, 2>{ 0x338, 0x340 });
            return result;
        }
        if (IsCount5Ultimate(s340) && !IsCount5Ultimate(s338)) {
            SetCandidateSlotResult(
                result,
                true,
                false,
                "known_unselected_available",
                "front_0340_count5_available",
                "medium",
                "",
                std::array<uint64_t, 2>{ 0x338, 0x340 });
            return result;
        }
    }

    if (state.heroId == kHeroSymmetra) {
        if (IsCount5Ultimate(s348)) {
            SetCandidateSlotResult(
                result,
                true,
                true,
                "known_selected",
                "front_0348_count5_queue",
                "high",
                "right",
                std::array<uint64_t, 2>{ 0x340, 0x348 });
            return result;
        }
        if (IsCount5Ultimate(s340)) {
            SetCandidateSlotResult(
                result,
                false,
                false,
                "unknown_ambiguous_left_or_available",
                "front_0340_count5_queue",
                "ambiguous",
                "left",
                std::array<uint64_t, 2>{ 0x340, 0x348 });
            return result;
        }
    }

    if (state.heroId == kHeroOrisa) {
        if (NonzeroTarget(b50) && NonzeroTarget(b58) && SameSemantic(b50, b58)) {
            SetCandidateSlotResult(
                result,
                true,
                true,
                "known_selected",
                "b_record_0b50_0b58_nonzero_join",
                "medium",
                "",
                std::array<uint64_t, 3>{ 0x0B50, 0x0B58, 0x0B60 });
            return result;
        }
        if (!NonzeroTarget(b50) && !NonzeroTarget(b58)) {
            SetCandidateSlotResult(
                result,
                true,
                false,
                "known_unselected_empty_b_record",
                "b_record_0b50_0b58_empty",
                "medium",
                "",
                std::array<uint64_t, 3>{ 0x0B50, 0x0B58, 0x0B60 });
            return result;
        }
    }

    if (state.heroId == kHeroJunkerQueen) {
        if (IsCount6Empty(s320) && IsCount6Enabled(s328) && IsCount5Ultimate(s330)) {
            SetCandidateSlotResult(
                result,
                true,
                false,
                "known_unselected_available",
                "front_0320_empty_0328_enabled_0330_count5_available",
                "medium",
                "",
                std::array<uint64_t, 3>{ 0x320, 0x328, 0x330 });
            return result;
        }
        if (IsCount6Enabled(s320) &&
            IsCount6Enabled(s328) &&
            SameSemantic(s320, s328) &&
            IsCount5Ultimate(s330)) {
            SetCandidateSlotResult(
                result,
                false,
                false,
                "unknown_junker_queen_count6_join_conflicting_recheck",
                "front_0320_0328_count6_join_0330_count5_ambiguous",
                "ambiguous",
                "",
                std::array<uint64_t, 3>{ 0x320, 0x328, 0x330 });
            return result;
        }
    }

    if (state.heroId == kHeroReinhardt) {
        if (IsCount6Enabled(s328) &&
            IsCount5Ultimate(s330) &&
            IsCount14Empty(s338) &&
            IsCount66Combo(s340) &&
            IsCount10Right(s348)) {
            SetCandidateSlotResult(
                result,
                true,
                false,
                "known_unselected_available",
                "front_0328_enabled_0330_count5_0338_0340_0348_available_chain",
                "medium",
                "",
                std::array<uint64_t, 5>{ 0x328, 0x330, 0x338, 0x340, 0x348 });
            return result;
        }
        if (IsCount5Ultimate(s328) &&
            IsCount14Empty(s330) &&
            IsCount66Combo(s338) &&
            IsCount10Right(s340) &&
            IsCount6Empty(s348)) {
            SetCandidateSlotResult(
                result,
                true,
                true,
                "known_selected",
                "front_0328_count5_0330_0338_0340_0348_selected_chain",
                "medium_high",
                "",
                std::array<uint64_t, 5>{ 0x328, 0x330, 0x338, 0x340, 0x348 });
            return result;
        }
    }

    if (state.heroId == kHeroZarya) {
        const bool primaryContext =
            state.e44Read &&
            state.e44U32 == 0x06 &&
            NonzeroTarget(b30) &&
            NonzeroTarget(b38);
        if (IsCount6Enabled(s340)) {
            if (!primaryContext) {
                SetCandidateSlotResult(
                    result,
                    false,
                    false,
                    "unknown_zarya_count6_preprimary_or_left_selected",
                    "front_0338_0340_count6_join_without_primary_context",
                    "ambiguous",
                    "left",
                    std::array<uint64_t, 5>{ 0x338, 0x340, 0x348, 0x0B30, 0x0B38 });
                return result;
            }
            if (IsCount6Enabled(s338) && SameSemantic(s338, s340)) {
                if (IsCount6Enabled(s348) && SameSemantic(s340, s348)) {
                    SetCandidateSlotResult(
                        result,
                        true,
                        true,
                        "known_selected_side_unknown",
                        "front_0338_0340_0348_count6_join_selected_boolean",
                        "medium_high",
                        "",
                        std::array<uint64_t, 5>{ 0x338, 0x340, 0x348, 0x0B30, 0x0B38 });
                    return result;
                }
                SetCandidateSlotResult(
                    result,
                    true,
                    true,
                    "known_selected",
                    "front_0338_0340_count6_join",
                    "medium_high",
                    "left",
                    std::array<uint64_t, 4>{ 0x338, 0x340, 0x0B30, 0x0B38 });
                return result;
            }
            if (IsCount3Left(s338)) {
                SetCandidateSlotResult(
                    result,
                    false,
                    false,
                    "unknown_zarya_right_swap_transient_recheck",
                    "front_0340_count6_right_swap",
                    "ambiguous",
                    "right",
                    std::array<uint64_t, 4>{ 0x338, 0x340, 0x0B30, 0x0B38 });
                return result;
            }
        }
        if (IsCount6Empty(s338) && IsCount3Left(s340)) {
            SetCandidateSlotResult(
                result,
                true,
                false,
                "known_unselected_available",
                "front_0338_empty_0340_count3_available",
                "medium",
                "",
                std::array<uint64_t, 2>{ 0x338, 0x340 });
            return result;
        }
    }

    if (IsCount5Ultimate(s338) && IsCount5Ultimate(s340) && SameSemantic(s338, s340)) {
        SetCandidateSlotResult(
            result,
            false,
            false,
            "unknown_front_count5_join_candidate",
            "generic_front_0338_0340_count5_join",
            "diagnostic",
            "",
            std::array<uint64_t, 2>{ 0x338, 0x340 });
        return result;
    }

    if (NonzeroTarget(b50) && NonzeroTarget(b58) && SameSemantic(b50, b58)) {
        SetCandidateSlotResult(
            result,
            false,
            false,
            "unknown_b_record_0b50_0b58_join_candidate",
            "generic_b_record_0b50_0b58_nonzero_join",
            "diagnostic",
            "",
            std::array<uint64_t, 3>{ 0x0B50, 0x0B58, 0x0B60 });
        return result;
    }

    if (IsCount6Enabled(s340)) {
        SetCandidateSlotResult(
            result,
            false,
            false,
            "unknown_count6_candidate_without_family_validation",
            "generic_front_0340_count6_candidate",
            "diagnostic",
            "",
            std::array<uint64_t, 5>{ 0x338, 0x340, 0x348, 0x0B30, 0x0B38 });
        return result;
    }

    (void)b60;
    (void)kHeroDVa;
    result.result = "unknown_no_family_rule";
    result.family = "";
    result.confidence = "none";
    return result;
}

inline void EvaluateResearchSelectedBoolean(State& state)
{
    ResearchSelectedBoolean& candidate = state.researchSelected;
    candidate.supportedHero = IsResearchSelectedSupportedHero(state.heroId);

    if (state.classification.result == Result::KnownTrue ||
        state.classification.result == Result::KnownFalse) {
        candidate.available = true;
        candidate.selected = state.classification.selected;
        candidate.rule = state.classification.selected
            ? "legacy_lookup_known_true_fail_closed"
            : "legacy_lookup_known_false_fail_closed";
        return;
    }

    candidate.available = false;
    candidate.selected = false;
    candidate.rule = candidate.supportedHero
        ? "candidate_unresolved_fail_closed"
        : "unsupported_or_unread";
}

inline void EvaluateRawSelectedBoolean(State& state)
{
    constexpr uint64_t kHeroSymmetra = OW::GameData::MakeHeroId(0x016);
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);

    RawSelectedBoolean& raw = state.rawSelected;
    raw.supportedHero = IsResearchSelectedSupportedHero(state.heroId);
    raw.available = false;
    raw.selected = false;
    raw.rule = raw.supportedHero ? "raw_unresolved_fail_closed" : "unsupported_or_unread";
    if (!state.available || !raw.supportedHero)
        return;

    if (state.researchSelected.skill16C2Read && state.researchSelected.skill16C2 != 0) {
        raw.available = true;
        raw.selected = true;
        raw.rule = "skill_16c2_u16_nonzero";
        return;
    }

    if (state.heroId == kHeroSymmetra && state.researchSelected.symmetra02E8Read) {
        const uint32_t high32 =
            static_cast<uint32_t>(state.researchSelected.symmetra02E8 >> 32);
        const uint32_t low32 =
            static_cast<uint32_t>(state.researchSelected.symmetra02E8 & 0xFFFFFFFFu);
        if (high32 == 0x80000018u) {
            raw.available = true;
            raw.selected = low32 >= 7u;
            raw.rule = raw.selected
                ? "symmetra_skill_02e8_high_80000018_low_ge_7"
                : "symmetra_skill_02e8_high_80000018_low_lt_7";
            return;
        }
    }

    if (state.heroId == kHeroZarya && state.researchSelected.component55_270Read) {
        if (state.researchSelected.component55_270 == 6u) {
            raw.available = true;
            raw.selected = true;
            raw.rule = "zarya_component55_270_u32_eq_6";
            return;
        }
        if (state.researchSelected.component55_270 == 0u) {
            raw.available = true;
            raw.selected = false;
            raw.rule = "zarya_component55_270_u32_eq_0";
            return;
        }
    }
}

inline void ClassifyAnaHeadshotSelectedBoolean(AnaHeadshotSelectedBoolean& selected)
{
    if (!selected.supportedHero) {
        selected.available = false;
        selected.selected = false;
        selected.result = "unsupported_or_unread";
        return;
    }

    if (!selected.primaryGateRead) {
        selected.available = false;
        selected.selected = false;
        selected.result = "ana_primary_gate_e44_unread";
        return;
    }

    if (!selected.primaryGateActive) {
        selected.available = true;
        selected.selected = false;
        selected.result = "known_unselected_primary_gate_e44_no_pri";
        return;
    }

    if (selected.majorSelectedKnown && !selected.majorSelected) {
        selected.available = true;
        selected.selected = false;
        selected.result = "known_unselected_statescript_major_not_selected";
        return;
    }

    if (!selected.component21_02E8Read) {
        selected.available = false;
        selected.selected = false;
        selected.result = "component21_02e8_unread";
        return;
    }

    if (selected.component21_02E8 == 0x00000002u) {
        selected.available = true;
        selected.selected = true;
        selected.result = selected.majorSelectedKnown
            ? "known_selected_statescript_major_component21_02e8_eq_2"
            : "inferred_selected_e44_not_no_pri_component21_02e8_eq_2";
        return;
    }

    if (selected.component21_02E8 == 0x00000000u) {
        selected.available = true;
        selected.selected = false;
        selected.result = selected.majorSelectedKnown
            ? "known_unselected_statescript_major_component21_02e8_eq_0"
            : "known_unselected_e44_not_no_pri_component21_02e8_eq_0";
        return;
    }

    if (selected.component21_02E8 == 0x00000001u) {
        selected.available = true;
        selected.selected = false;
        selected.result = selected.majorSelectedKnown
            ? "known_unselected_statescript_major_component21_02e8_eq_1"
            : "known_unselected_e44_not_no_pri_component21_02e8_eq_1";
        return;
    }

    selected.available = false;
    selected.selected = false;
    selected.result = "unknown_component21_02e8_value";
}

inline void ReadAnaHeadshotSelectedBoolean(State& state)
{
    constexpr uint64_t kHeroAna = OW::GameData::MakeHeroId(0x13B);

    AnaHeadshotSelectedBoolean& selected = state.anaHeadshotSelected;
    selected.supportedHero = state.heroId == kHeroAna;
    selected.available = false;
    selected.selected = false;
    selected.result = selected.supportedHero
        ? "component7f_unavailable"
        : "unsupported_or_unread";

    if (!state.available || !selected.supportedHero ||
        !IsPlausibleUserPointer(state.componentParent)) {
        return;
    }

    selected.primaryGateRead = state.e44Read;
    selected.primaryGateE44 = state.e44U32;
    selected.primaryGateActive = state.e44Read && state.e44U32 != 0x00000001u;
    selected.majorSelectedKnown =
        state.stateScriptEa0cSelected.available &&
        state.stateScriptEa0cSelected.selectedKnown &&
        state.stateScriptEa0cSelected.sourceFound &&
        state.stateScriptEa0cSelected.mapConsistent;
    selected.majorSelected =
        selected.majorSelectedKnown && state.stateScriptEa0cSelected.selected;
    selected.skill02E8Read = state.researchSelected.symmetra02E8Read;
    selected.skill02E8 = state.researchSelected.symmetra02E8;
    selected.skill02E8High32 =
        static_cast<uint32_t>(selected.skill02E8 >> 32);
    selected.skill02E8Low32 =
        static_cast<uint32_t>(selected.skill02E8 & 0xFFFFFFFFu);
    selected.skill02E8GateKnown =
        selected.skill02E8Read && selected.skill02E8High32 == 0x80000018u;
    selected.skill02E8GateActive =
        selected.skill02E8GateKnown && selected.skill02E8Low32 >= 7u;
    selected.skill09ARead = TryRead(state.skillBase + 0x009A, selected.skill09A);
    selected.skill592Read = TryRead(state.skillBase + 0x0592, selected.skill592);
    selected.skill0BD0Read = TryRead(state.skillBase + 0x0BD0, selected.skill0BD0);
    selected.skill0348TargetRead =
        TryRead(state.skillBase + 0x0348, selected.skill0348Target);
    selected.skill0348TargetPlausible =
        selected.skill0348TargetRead && IsPlausibleUserPointer(selected.skill0348Target);
    if (selected.skill0348TargetPlausible) {
        selected.skill0348Target1D4Read =
            TryRead(selected.skill0348Target + 0x01D4, selected.skill0348Target1D4);
        selected.skill0348Target1D4QwordRead =
            TryRead(selected.skill0348Target + 0x01D4, selected.skill0348Target1D4Qword);
    }

    selected.component21Base = OW::DecryptComponent(
        static_cast<uintptr_t>(state.componentParent),
        0x21u);
    if (IsPlausibleUserPointer(selected.component21Base)) {
        selected.component21_021CRead =
            TryRead(selected.component21Base + 0x021C, selected.component21_021C);
        selected.component21_0228Read =
            TryRead(selected.component21Base + 0x0228, selected.component21_0228);
        selected.component21_0228QwordRead =
            TryRead(selected.component21Base + 0x0228, selected.component21_0228Qword);
        selected.component21_02E0Read =
            TryRead(selected.component21Base + 0x02E0, selected.component21_02E0);
        selected.component21_02E8Read =
            TryRead(selected.component21Base + 0x02E8, selected.component21_02E8);
        selected.component21_02E8QwordRead =
            TryRead(selected.component21Base + 0x02E8, selected.component21_02E8Qword);
    }

    selected.component22Base = OW::DecryptComponent(
        static_cast<uintptr_t>(state.componentParent),
        0x22u);
    if (IsPlausibleUserPointer(selected.component22Base)) {
        selected.component22_0140Read =
            TryRead(selected.component22Base + 0x0140, selected.component22_0140);
        selected.component22_01F2Read =
            TryRead(selected.component22Base + 0x01F2, selected.component22_01F2);
        selected.component22_0200Read =
            TryRead(selected.component22Base + 0x0200, selected.component22_0200);
        selected.component22_0202Read =
            TryRead(selected.component22Base + 0x0202, selected.component22_0202);
        selected.component22_0203Read =
            TryRead(selected.component22Base + 0x0203, selected.component22_0203);
        selected.component22_0350Read =
            TryRead(selected.component22Base + 0x0350, selected.component22_0350);
    }

    selected.component7fBase = OW::DecryptComponent(
        static_cast<uintptr_t>(state.componentParent),
        0x7Fu);
    if (!IsPlausibleUserPointer(selected.component7fBase)) {
        selected.result = "component7f_unavailable";
        return;
    }

    selected.component7fD4Read =
        TryRead(selected.component7fBase + 0x00D4, selected.component7fD4);
    selected.component7fD8Read =
        TryRead(selected.component7fBase + 0x00D8, selected.component7fD8);
    selected.component7fD8QwordRead =
        TryRead(selected.component7fBase + 0x00D8, selected.component7fD8Qword);

    selected.component7fScanHash = 14695981039346656037ull;
    for (size_t index = 0; index < kAnaComponent7fScanQwordCount; ++index) {
        const uint64_t offset =
            kAnaComponent7fScanBegin + static_cast<uint64_t>(index) * kAnaComponent7fScanStride;
        uint64_t value = 0;
        const bool read = TryRead(selected.component7fBase + offset, value);
        selected.component7fScanRead[index] = read;
        selected.component7fScanQword[index] = value;
        selected.component7fScanHash = Fnv1aValue(selected.component7fScanHash, read);
        selected.component7fScanHash = Fnv1aValue(selected.component7fScanHash, value);
        if (read)
            ++selected.component7fScanReadCount;
        if (read && value != 0)
            ++selected.component7fScanNonzeroCount;
    }

    const RawComponentSlotProbe raw7f =
        ProbeRawComponentSlot(state.componentParent, 0x7Fu);
    selected.component7fRawSlotBitsetRead = raw7f.bitsetRead;
    selected.component7fRawSlotPresent = raw7f.present;
    selected.component7fRawSlotIndexRead = raw7f.indexRead;
    selected.component7fRawSlotTableRead = raw7f.tableRead;
    selected.component7fRawSlotTablePlausible = raw7f.tablePlausible;
    selected.component7fRawSlotRead = raw7f.slotRead;
    selected.component7fRawSlotPlausible = raw7f.slotPlausible;
    selected.component7fRawSlotBitset = raw7f.bitset;
    selected.component7fRawSlotIndexBase = raw7f.indexBase;
    selected.component7fRawSlotComponentIndex = raw7f.componentIndex;
    selected.component7fRawSlotTable = raw7f.table;
    selected.component7fRawSlotBase = raw7f.slot;
    if (raw7f.slotPlausible) {
        selected.component7fRawSlotD8Read =
            TryRead(raw7f.slot + 0x00D8, selected.component7fRawSlotD8);
        selected.component7fRawSlotD8QwordRead =
            TryRead(raw7f.slot + 0x00D8, selected.component7fRawSlotD8Qword);

        selected.component7fRawSlotScanHash = 14695981039346656037ull;
        for (size_t index = 0; index < kAnaComponent7fRawSlotScanQwordCount; ++index) {
            const uint64_t offset =
                kAnaComponent7fRawSlotScanBegin +
                static_cast<uint64_t>(index) * kAnaComponent7fRawSlotScanStride;
            uint64_t value = 0;
            const bool read = TryRead(raw7f.slot + offset, value);
            selected.component7fRawSlotScanRead[index] = read;
            selected.component7fRawSlotScanQword[index] = value;
            selected.component7fRawSlotScanHash =
                Fnv1aValue(selected.component7fRawSlotScanHash, read);
            selected.component7fRawSlotScanHash =
                Fnv1aValue(selected.component7fRawSlotScanHash, value);
            if (read)
                ++selected.component7fRawSlotScanReadCount;
            if (read && value != 0)
                ++selected.component7fRawSlotScanNonzeroCount;
        }
    }
    ClassifyAnaHeadshotSelectedBoolean(selected);
}

inline void EvaluateMergedSelectedBoolean(State& state)
{
    constexpr uint64_t kHeroAna = OW::GameData::MakeHeroId(0x13B);

    MergedSelectedBoolean& merged = state.mergedSelected;
    uint64_t stateScriptSupportOffset = 0;
    const bool stateScriptSupported =
        TryGetMergedStateScriptEa0cSupportOffset(state.heroId, stateScriptSupportOffset);
    const bool anaHeadshotSupported = state.heroId == kHeroAna;
    merged.supportedHero =
        anaHeadshotSupported ||
        stateScriptSupported ||
        state.rawSelected.supportedHero ||
        state.candidateSlotSelected.supportedHero;
    merged.available = false;
    merged.selected = false;
    merged.result = merged.supportedHero
        ? "unknown_no_selected_evidence"
        : "unsupported_or_unread";
    merged.source = "";
    merged.rule = "";

    const bool rawAvailable = state.rawSelected.available;
    const bool candidateAvailable = state.candidateSlotSelected.available;

    if (anaHeadshotSupported) {
        if (state.anaHeadshotSelected.available) {
            merged.available = true;
            merged.selected = state.anaHeadshotSelected.selected;
            merged.result = merged.selected ? "known_selected" : "known_unselected";
            merged.source = "ana_headshot";
            merged.rule = "e44_not_no_pri_component21_02e8_eq_2";
            return;
        }

        merged.result = state.anaHeadshotSelected.result;
        merged.source = "ana_headshot";
        merged.rule = "e44_not_no_pri_component21_02e8_eq_2";
        return;
    }

    if (stateScriptSupported) {
        const StateScriptEa0cSelectedBoolean& selected = state.stateScriptEa0cSelected;
        if (selected.available &&
            selected.selectedKnown &&
            selected.sourceFound &&
            selected.mapConsistent &&
            selected.knownCandidateCount == 1u &&
            selected.matchedSourceOffset == stateScriptSupportOffset) {
            merged.available = true;
            merged.selected = selected.selected;
            merged.result = merged.selected ? "known_selected" : "known_unselected";
            merged.source = "statescript_ea0c";
            merged.rule = "ea0c_10127_support_table_20260616";
            return;
        }

        merged.result = "unknown_statescript_ea0c_unavailable_fail_closed";
        merged.source = "statescript_ea0c";
        merged.rule = "ea0c_10127_support_table_20260616";
        return;
    }

    if (rawAvailable && candidateAvailable) {
        if (state.rawSelected.selected == state.candidateSlotSelected.selected) {
            merged.available = true;
            merged.selected = state.rawSelected.selected;
            merged.result = merged.selected ? "known_selected" : "known_unselected";
            merged.source = "raw_and_candidate_slot";
            merged.rule = "raw_candidate_agree";
            return;
        }

        merged.result = "unknown_raw_candidate_conflict_fail_closed";
        merged.source = "raw_and_candidate_slot";
        merged.rule = "raw_candidate_conflict";
        return;
    }

    if (rawAvailable) {
        merged.available = true;
        merged.selected = state.rawSelected.selected;
        merged.result = merged.selected ? "known_selected" : "known_unselected";
        merged.source = "raw";
        merged.rule = state.rawSelected.rule;
        return;
    }

    if (candidateAvailable) {
        merged.available = true;
        merged.selected = state.candidateSlotSelected.selected;
        merged.result = merged.selected ? "known_selected" : "known_unselected";
        merged.source = "candidate_slot";
        merged.rule = state.candidateSlotSelected.family;
        return;
    }
}

inline void ReadResearchSelectedBoolean(State& state)
{
    constexpr uint64_t kHeroZarya = OW::GameData::MakeHeroId(0x068);

    ResearchSelectedBoolean& candidate = state.researchSelected;
    if (!state.available)
        return;

    candidate.skill16C2Read = TryRead(state.skillBase + 0x16C2, candidate.skill16C2);
    candidate.symmetra02E8Read = TryRead(state.skillBase + 0x02E8, candidate.symmetra02E8);

    if (state.heroId == kHeroZarya &&
        IsPlausibleUserPointer(state.componentParent)) {
        candidate.component55Base = OW::DecryptComponent(
            static_cast<uintptr_t>(state.componentParent),
            0x55u);
        if (IsPlausibleUserPointer(candidate.component55Base)) {
            candidate.component55_270Read =
                TryRead(candidate.component55Base + 0x0270, candidate.component55_270);
        }
    }

}

template <size_t N>
inline const HeroPerkLookupSeed::Entry* FindUsable(
    const std::array<HeroPerkLookupSeed::Entry, N>& entries,
    uint64_t key)
{
    const auto it = std::find_if(
        entries.begin(),
        entries.end(),
        [key](const HeroPerkLookupSeed::Entry& entry) {
            return entry.key == key;
        });
    return it == entries.end() ? nullptr : &(*it);
}

template <size_t N>
inline bool ContainsKey(const std::array<uint64_t, N>& keys, uint64_t key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

inline const HeroPerkLookupSeed::Entry* FindUsableForTier(
    HeroPerkLookupSeed::Tier tier,
    uint64_t key)
{
    using namespace HeroPerkLookupSeed;
    switch (tier) {
    case Tier::Ordered: return FindUsable(kOrderedUsable, key);
    case Tier::Cluster: return FindUsable(kClusterUsable, key);
    case Tier::UniqueNo338: return FindUsable(kUniqueNo338Usable, key);
    case Tier::Multiset: return FindUsable(kMultisetUsable, key);
    default: return nullptr;
    }
}

inline bool IsCollisionForTier(HeroPerkLookupSeed::Tier tier, uint64_t key)
{
    using namespace HeroPerkLookupSeed;
    switch (tier) {
    case Tier::Ordered: return ContainsKey(kOrderedCollisions, key);
    case Tier::Cluster: return ContainsKey(kClusterCollisions, key);
    case Tier::UniqueNo338: return ContainsKey(kUniqueNo338Collisions, key);
    case Tier::Multiset: return ContainsKey(kMultisetCollisions, key);
    default: return false;
    }
}

inline uint64_t KeyForTier(const State& state, HeroPerkLookupSeed::Tier tier)
{
    using namespace HeroPerkLookupSeed;
    switch (tier) {
    case Tier::Ordered: return state.orderedKey;
    case Tier::Cluster: return state.clusterKey;
    case Tier::UniqueNo338: return state.uniqueNo338Key;
    case Tier::Multiset: return state.multisetKey;
    default: return 0;
    }
}

inline Classification Classify(const State& state)
{
    Classification classification{};
    if (!state.lookupReady)
        return classification;

    for (const HeroPerkLookupSeed::Tier tier : HeroPerkLookupSeed::kSafePolicy) {
        const uint64_t key = KeyForTier(state, tier);
        const HeroPerkLookupSeed::Entry* hit = FindUsableForTier(tier, key);
        const bool collision = IsCollisionForTier(tier, key);
        if (classification.checkedCount < classification.checked.size()) {
            classification.checked[classification.checkedCount++] = CheckedTier{
                HeroPerkLookupSeed::TierName(tier),
                key,
                hit != nullptr,
                collision
            };
        }

        if (hit) {
            classification.result = hit->ultimateSelected ? Result::KnownTrue : Result::KnownFalse;
            classification.tier = HeroPerkLookupSeed::TierName(tier);
            classification.key = key;
            classification.selected = hit->ultimateSelected;
            return classification;
        }

        if (collision) {
            classification.result = Result::UnknownCollision;
            classification.tier = HeroPerkLookupSeed::TierName(tier);
            classification.key = key;
            return classification;
        }
    }
    return classification;
}

inline State ReadCurrent(uint64_t heroId, uint64_t skillBase, uint64_t componentParent = 0)
{
    State state{};
    state.heroId = heroId;
    state.skillBase = skillBase;
    state.componentParent = componentParent;
    state.available = heroId != 0 && IsPlausibleUserPointer(skillBase) && SDK != nullptr;
    if (!state.available)
        return state;

    state.e44Read = TryRead(skillBase + 0xE44, state.e44U32);
    TryRead(skillBase + 0xE44, state.e44U64);
    state.e78Read = TryRead(skillBase + 0xE78, state.e78U32);
    TryRead(skillBase + 0xE78, state.e78U64);

    ReadResearchSelectedBoolean(state);
    EvaluateStateScriptEa0cSelectedBoolean(state);
    ReadAnaHeadshotSelectedBoolean(state);
    EvaluateRawSelectedBoolean(state);

    for (size_t index = 0; index < kPointerOffsets.size(); ++index)
        state.pointers[index] = ReadPointerTarget(skillBase, kPointerOffsets[index]);

    for (size_t index = 0; index < kCandidateSlotOffsets.size(); ++index)
        state.candidateSlots[index] = ReadCandidateSlot(skillBase, kCandidateSlotOffsets[index]);
    state.candidateSlotSelected = ClassifyCandidateSlotSelection(state);
    EvaluateMergedSelectedBoolean(state);

    state.lookupReady = state.available &&
        state.e44Read &&
        state.e78Read &&
        HasRequiredPointerTargets(state);
    if (!state.lookupReady) {
        EvaluateResearchSelectedBoolean(state);
        return state;
    }

    state.orderedSignature = BuildSmallOrderedSignature(state);
    state.clusterSignature = BuildClusterSignature(state);
    state.uniqueNo338Signature = BuildSmallUniqueNo338Signature(state);
    state.multisetSignature = BuildSmallMultisetSignature(state);
    state.orderedKey = BuildUltimateKey(state, state.orderedSignature);
    state.clusterKey = BuildUltimateKey(state, state.clusterSignature);
    state.uniqueNo338Key = BuildUltimateKey(state, state.uniqueNo338Signature);
    state.multisetKey = BuildUltimateKey(state, state.multisetSignature);
    state.stateCodeKey = BuildStateCodeKey(state);
    BuildResearchCandidateKeys(state);
    state.classification = Classify(state);
    EvaluateResearchSelectedBoolean(state);
    EvaluateMergedSelectedBoolean(state);
    return state;
}

} // namespace OW::HeroPerks
