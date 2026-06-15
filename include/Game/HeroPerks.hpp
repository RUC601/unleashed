#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "Game/HeroPerkLookupSeed.hpp"
#include "Game/SDK.hpp"

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

struct State {
    bool available = false;
    bool lookupReady = false;
    uint64_t heroId = 0;
    uint64_t skillBase = 0;
    bool e44Read = false;
    uint32_t e44U32 = 0;
    uint64_t e44U64 = 0;
    bool e78Read = false;
    uint32_t e78U32 = 0;
    uint64_t e78U64 = 0;
    std::array<PointerTarget, kPointerOffsets.size()> pointers{};
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

inline State ReadCurrent(uint64_t heroId, uint64_t skillBase)
{
    State state{};
    state.heroId = heroId;
    state.skillBase = skillBase;
    state.available = heroId != 0 && IsPlausibleUserPointer(skillBase) && SDK != nullptr;
    if (!state.available)
        return state;

    state.e44Read = TryRead(skillBase + 0xE44, state.e44U32);
    TryRead(skillBase + 0xE44, state.e44U64);
    state.e78Read = TryRead(skillBase + 0xE78, state.e78U32);
    TryRead(skillBase + 0xE78, state.e78U64);

    for (size_t index = 0; index < kPointerOffsets.size(); ++index)
        state.pointers[index] = ReadPointerTarget(skillBase, kPointerOffsets[index]);

    state.lookupReady = state.available &&
        state.e44Read &&
        state.e78Read &&
        HasRequiredPointerTargets(state);
    if (!state.lookupReady)
        return state;

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
    return state;
}

} // namespace OW::HeroPerks
