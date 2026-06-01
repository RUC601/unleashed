#include "Memory/Memory.h"
#include "Game/GameData.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr const char* kDefaultTargetProcess = "Overwatch.exe";
constexpr const char* kDefaultLogPath = "un_cn_probe.log";
constexpr const char* kCnDetectorPrefix = "Neac";

constexpr uint64_t kWorldEntityBaseRva = 0x3935908;
constexpr uint64_t kWorldInputMouseScaleXRva = 0x3778BCC;
constexpr uint64_t kWorldInputMouseScaleYRva = 0x3778BE4;
constexpr uint64_t kWorldViewportWidthRva = 0x4037C38;
constexpr uint64_t kWorldViewportHeightRva = 0x4037CA8;

constexpr uint64_t kCnEntityBaseRva = 0x42D2268;
constexpr uint64_t kCnInputMouseScaleXRva = 0x411E4EC;
constexpr uint64_t kCnInputMouseScaleYRva = 0x411E504;
constexpr uint64_t kCnViewMatrixDirectRva = 0x49A6A90;
constexpr uint64_t kViewMatrixP1 = 0x20;
constexpr uint64_t kViewMatrixP2 = 0x48;
constexpr uint64_t kViewMatrixViewProjectionParent = 0x6C8;
constexpr uint64_t kViewMatrixViewProjectionPtr = 0x8;
constexpr uint64_t kViewMatrixViewProjectionMatrix = 0xC0;
constexpr uint64_t kViewMatrixViewMatrix = 0x140;
constexpr uint64_t kViewMatrixProjMatrix = 0xB0;
constexpr size_t kViewMatrixSampleCount = 5;
constexpr uint64_t kPlayerControllerViewDirection = 0x1260;
constexpr uint64_t kCnDataScanStartRva = 0x4116000;
constexpr uint64_t kCnFullScreenWidthStringRva = 0x4273214;
constexpr uint64_t kCnFullScreenHeightStringRva = 0x4273234;
constexpr size_t kDataScanChunkSize = 0x20000;
constexpr size_t kMaxGameAdminPointerCandidates = 50000;

constexpr uint64_t kEntityMatchId = 0x138;
constexpr uint64_t kEntityPoolPtr = 0x30;
constexpr uint64_t kPoolPoolId = 0x10;
constexpr size_t kEntityEntryStride = 0x10;
constexpr size_t kEntityScanSize = 0x10000;
constexpr size_t kEntityScanChunkSize = 0x1000;

constexpr uint32_t kTypeVelocity = 0x04;
constexpr uint32_t kTypeTeam = 0x21;
constexpr uint32_t kTypeBone = 0x24;
constexpr uint32_t kTypeLink = 0x34;
constexpr uint32_t kTypeVisibility = 0x35;
constexpr uint32_t kTypePlayerController = 0x43;
constexpr uint32_t kTypeHeroId = 0x54;

constexpr uint64_t kHeroIdField = 0xD0;
constexpr uint64_t kTeamFlagsField = 0x58;
constexpr uint64_t kTeamFlagsMask = 0x0F800000;
constexpr uint64_t kLinkUniqueId = 0xD4;
constexpr uint64_t kLinkUniqueIdAlt = 0xD8;
constexpr size_t kMaxTeamSemanticSamples = 96;
constexpr size_t kMaxLinkSemanticSamples = 64;
constexpr size_t kMaxHeroSemanticSamples = 64;
constexpr size_t kMaxRuntimeComponentSamples = 96;

struct OffsetProfile {
    const char* name;
    const char* reason;
    uint64_t entityBaseRva;
    uint64_t inputMouseScaleXRva;
    uint64_t inputMouseScaleYRva;
};

constexpr OffsetProfile kWorldProfile{
    "world/bz",
    "no Neac* process detected",
    kWorldEntityBaseRva,
    kWorldInputMouseScaleXRva,
    kWorldInputMouseScaleYRva,
};

constexpr OffsetProfile kCnProfile{
    "cn/ne",
    "Neac* process detected",
    kCnEntityBaseRva,
    kCnInputMouseScaleXRva,
    kCnInputMouseScaleYRva,
};

struct Options {
    std::string targetProcess = kDefaultTargetProcess;
    std::string logPath = kDefaultLogPath;
    bool forceCn = false;
    bool forceWorld = false;
};

class ProbeLog {
public:
    explicit ProbeLog(const std::string& path)
    {
        m_file.open(path, std::ios::out | std::ios::trunc);
    }

    template <typename... Args>
    void Print(const char* format, Args... args)
    {
        char buffer[2048]{};
        std::snprintf(buffer, sizeof(buffer), format, args...);
        std::printf("%s\n", buffer);
        if (m_file.is_open())
            m_file << buffer << '\n';
    }

private:
    std::ofstream m_file;
};

bool ReadBytes(uint64_t address, void* buffer, size_t size)
{
    return address != 0 && buffer != nullptr && size != 0 &&
           mem.Read(static_cast<uintptr_t>(address), buffer, size);
}

template <typename T>
bool ReadValue(uint64_t address, T& out)
{
    out = {};
    return ReadBytes(address, &out, sizeof(T));
}

bool IsPlausibleUserPointer(uint64_t value)
{
    return value >= 0x10000ull &&
           value < 0x0000800000000000ull &&
           (value % alignof(void*) == 0);
}

bool IsPlausibleSensitivity(float value)
{
    return value > 0.0f && value < 100.0f;
}

const char* ComponentTypeName(uint32_t type)
{
    switch (type) {
    case kTypeTeam:
        return "TYPE_TEAM";
    case kTypeLink:
        return "TYPE_LINK";
    case kTypeHeroId:
        return "TYPE_P_HEROID";
    default:
        return "TYPE_OTHER";
    }
}

bool HasHeroIdPrefix(uint64_t value)
{
    return (value & 0xFFF0000000000000ull) == OW::GameData::kHeroIdPrefix;
}

bool IsLikelyEntityMatchId(uint32_t value)
{
    return (value & 0x80000000u) != 0;
}

bool IsPrimaryGameAdminUidOffset(uint64_t value)
{
    return value == 0x2F0 || value == 0x4E4;
}

uint64_t MaskPoolPointer(uint64_t value)
{
    return value & 0xFFFFFFFFFFFFFFC0ull;
}

uint32_t PopcountBelow(uint64_t bits, uint32_t shift)
{
    if (shift == 0)
        return 0;
    const uint64_t mask = (shift >= 64) ? std::numeric_limits<uint64_t>::max() : ((1ull << shift) - 1ull);
    return static_cast<uint32_t>(std::popcount(bits & mask));
}

struct ComponentLayoutProbe {
    uint32_t type = 0;
    uint32_t bucket = 0;
    uint32_t shift = 0;
    bool bitsetRead = false;
    bool present = false;
    bool indexRead = false;
    bool tableRead = false;
    bool tablePlausible = false;
    bool encryptedSlotRead = false;
    bool encryptedSlotNonZero = false;
    uint64_t bitset = 0;
    uint8_t indexBase = 0;
    uint64_t table = 0;
    uint64_t encryptedSlot = 0;
    uint64_t componentIndex = 0;
};

ComponentLayoutProbe ProbeComponentLayout(uint64_t parent, uint32_t type)
{
    ComponentLayoutProbe probe{};
    probe.type = type;

    const uint32_t bucket = type / 64;
    const uint32_t shift = type % 64;
    const uint64_t bitMask = 1ull << shift;
    probe.bucket = bucket;
    probe.shift = shift;

    probe.bitsetRead = ReadValue(parent + 0x110 + 8ull * bucket, probe.bitset);
    probe.present = probe.bitsetRead && ((probe.bitset & bitMask) != 0);
    probe.indexRead = ReadValue(parent + 0x130 + bucket, probe.indexBase);
    probe.tableRead = ReadValue(parent + 0x80, probe.table);
    probe.tablePlausible = probe.tableRead && IsPlausibleUserPointer(probe.table);

    if (probe.present && probe.indexRead && probe.tablePlausible) {
        probe.componentIndex =
            static_cast<uint64_t>(probe.indexBase) + PopcountBelow(probe.bitset, shift);
        probe.encryptedSlotRead =
            ReadValue(probe.table + 8ull * probe.componentIndex, probe.encryptedSlot);
        probe.encryptedSlotNonZero = probe.encryptedSlotRead && probe.encryptedSlot != 0;
    }

    return probe;
}

struct ComponentSemanticSample {
    uint64_t parent = 0;
    uint32_t matchId = 0;
    bool matchIdRead = false;
    ComponentLayoutProbe layout{};
    uint64_t decodedIdentity = 0;
    bool decodedPlausible = false;

    bool heroIdRead = false;
    uint64_t heroId = 0;
    bool heroIdHasPrefix = false;
    bool heroIdKnown = false;

    bool teamFlagsRead = false;
    bool teamFlagsSecondRead = false;
    uint32_t teamFlags = 0;
    uint32_t teamFlagsSecond = 0;
    uint32_t teamMask = 0;
    bool teamMaskPresent = false;
    bool teamStable = false;

    bool linkRead = false;
    uint32_t linkId = 0;
    uint32_t linkIdAlt = 0;
    bool linkIdMatchesKnownEntity = false;
    bool linkIdAltMatchesKnownEntity = false;
};

struct RuntimeComponentSample {
    uint64_t parent = 0;
    uint32_t matchId = 0;
    bool matchIdRead = false;
    ComponentLayoutProbe layout{};
    uint64_t base = 0;
    bool basePlausible = false;
};

struct EntityProbeStats {
    uint64_t entityList = 0;
    size_t readableChunks = 0;
    size_t readableBytes = 0;
    size_t slotsScanned = 0;
    size_t nonZeroSlots = 0;
    size_t plausibleParents = 0;
    size_t uniqueParents = 0;
    size_t matchIdReads = 0;
    size_t matchIdNonZero = 0;
    size_t poolPtrReads = 0;
    size_t poolPtrNonZero = 0;
    size_t poolIdReads = 0;
    size_t poolIdNonZero = 0;
    size_t componentTableReads = 0;
    size_t componentTablePlausible = 0;
    size_t componentTypePresent = 0;
    size_t encryptedSlotsNonZero = 0;
    size_t identityDecodedPlausible = 0;
    size_t heroIdReads = 0;
    size_t heroIdPrefixMatches = 0;
    size_t heroIdKnownMatches = 0;
    size_t teamFlagReads = 0;
    size_t teamFlagStableReads = 0;
    size_t teamFlagMaskPresent = 0;
    size_t linkReads = 0;
    size_t linkMatchesKnownEntity = 0;
    size_t teamSamples = 0;
    size_t linkSamples = 0;
    size_t heroSamples = 0;
    std::unordered_set<uint32_t> matchIds;
    std::vector<uint64_t> sampleParents;
    std::vector<ComponentSemanticSample> componentSamples;
    std::vector<RuntimeComponentSample> playerControllerSamples;
    std::vector<RuntimeComponentSample> visibilitySamples;
};

struct ProbeVector3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ProbeMatrix {
    float m11 = 1.0f, m12 = 0.0f, m13 = 0.0f, m14 = 0.0f;
    float m21 = 0.0f, m22 = 1.0f, m23 = 0.0f, m24 = 0.0f;
    float m31 = 0.0f, m32 = 0.0f, m33 = 1.0f, m34 = 0.0f;
    float m41 = 0.0f, m42 = 0.0f, m43 = 0.0f, m44 = 1.0f;
};

struct ViewMatrixSample {
    bool rootRead = false;
    bool rootPlausible = false;
    bool p1Read = false;
    bool p1Plausible = false;
    bool p2Read = false;
    bool p2Plausible = false;
    bool p3Read = false;
    bool p3Plausible = false;
    bool p4Read = false;
    bool p4Plausible = false;
    bool renderMatrixRead = false;
    bool cameraMatrixRead = false;
    bool projectionMatrixRead = false;
    bool renderMatrixValid = false;
    bool cameraMatrixValid = false;
    bool projectionMatrixValid = false;
    uint64_t root = 0;
    uint64_t p1 = 0;
    uint64_t p2 = 0;
    uint64_t p3 = 0;
    uint64_t p4 = 0;
    uint64_t renderMatrixPtr = 0;
    uint64_t cameraMatrixPtr = 0;
    uint64_t projectionMatrixPtr = 0;
    ProbeMatrix renderMatrix{};
    ProbeMatrix cameraMatrix{};
    ProbeMatrix projectionMatrix{};
};

struct ViewMatrixProbeResult {
    std::vector<ViewMatrixSample> samples;
    size_t rootPlausible = 0;
    size_t chainComplete = 0;
    size_t renderValid = 0;
    size_t cameraValid = 0;
    size_t projectionValid = 0;
    size_t stableRootTransitions = 0;
    size_t stableP2Transitions = 0;
    bool accepted = false;
};

bool IsMatrixFinite(const ProbeMatrix& matrix)
{
    const float values[] = {
        matrix.m11, matrix.m12, matrix.m13, matrix.m14,
        matrix.m21, matrix.m22, matrix.m23, matrix.m24,
        matrix.m31, matrix.m32, matrix.m33, matrix.m34,
        matrix.m41, matrix.m42, matrix.m43, matrix.m44,
    };
    for (float value : values) {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

bool IsVectorFinite(const ProbeVector3& value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

float VectorLength(const ProbeVector3& value)
{
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

ProbeVector3 NormalizeVector(const ProbeVector3& value)
{
    const float length = VectorLength(value);
    if (length <= 0.0001f || !std::isfinite(length))
        return {};
    return { value.x / length, value.y / length, value.z / length };
}

float DotVector(const ProbeVector3& left, const ProbeVector3& right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

ProbeVector3 MatrixForward(const ProbeMatrix& matrix)
{
    return { matrix.m13, matrix.m23, matrix.m33 };
}

bool IsMatrixNonIdentity(const ProbeMatrix& matrix)
{
    const float values[] = {
        matrix.m11, matrix.m12, matrix.m13, matrix.m14,
        matrix.m21, matrix.m22, matrix.m23, matrix.m24,
        matrix.m31, matrix.m32, matrix.m33, matrix.m34,
        matrix.m41, matrix.m42, matrix.m43, matrix.m44,
    };
    const float identity[] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    bool hasNonZeroValue = false;
    bool differsFromIdentity = false;
    for (size_t index = 0; index < std::size(values); ++index) {
        if (!std::isfinite(values[index]))
            return false;
        if (std::fabs(values[index]) > 0.0001f)
            hasNonZeroValue = true;
        if (std::fabs(values[index] - identity[index]) > 0.001f)
            differsFromIdentity = true;
    }
    return hasNonZeroValue && differsFromIdentity;
}

bool IsViewProjectionLike(const ProbeMatrix& matrix)
{
    if (!IsMatrixNonIdentity(matrix))
        return false;
    if (std::fabs(matrix.m44) > 100000.0f)
        return false;

    const float diagonalMagnitude =
        std::fabs(matrix.m11) + std::fabs(matrix.m22) +
        std::fabs(matrix.m33) + std::fabs(matrix.m44);
    return diagonalMagnitude > 0.01f && diagonalMagnitude < 100000.0f;
}

ViewMatrixSample ProbeViewMatrixSample(uint64_t base)
{
    ViewMatrixSample sample{};
    sample.rootRead = ReadValue(base + kCnViewMatrixDirectRva, sample.root);
    sample.rootPlausible = sample.rootRead && IsPlausibleUserPointer(sample.root);
    if (!sample.rootPlausible)
        return sample;

    sample.p1Read = ReadValue(sample.root + kViewMatrixP1, sample.p1);
    sample.p1Plausible = sample.p1Read && IsPlausibleUserPointer(sample.p1);
    if (!sample.p1Plausible)
        return sample;

    sample.p2Read = ReadValue(sample.p1 + kViewMatrixP2, sample.p2);
    sample.p2Plausible = sample.p2Read && IsPlausibleUserPointer(sample.p2);
    if (!sample.p2Plausible)
        return sample;

    sample.p3Read = ReadValue(sample.p2 + kViewMatrixViewProjectionParent, sample.p3);
    sample.p3Plausible = sample.p3Read && IsPlausibleUserPointer(sample.p3);
    if (sample.p3Plausible) {
        sample.p4Read = ReadValue(sample.p3 + kViewMatrixViewProjectionPtr, sample.p4);
        sample.p4Plausible = sample.p4Read && IsPlausibleUserPointer(sample.p4);
        if (sample.p4Plausible) {
            sample.renderMatrixPtr = sample.p4 + kViewMatrixViewProjectionMatrix;
            sample.renderMatrixRead = ReadValue(sample.renderMatrixPtr, sample.renderMatrix);
            sample.renderMatrixValid =
                sample.renderMatrixRead && IsViewProjectionLike(sample.renderMatrix);
        }
    }

    sample.cameraMatrixPtr = sample.p2 + kViewMatrixViewMatrix;
    sample.cameraMatrixRead = ReadValue(sample.cameraMatrixPtr, sample.cameraMatrix);
    sample.cameraMatrixValid =
        sample.cameraMatrixRead && IsMatrixNonIdentity(sample.cameraMatrix);

    sample.projectionMatrixPtr = sample.p2 + kViewMatrixProjMatrix;
    sample.projectionMatrixRead = ReadValue(sample.projectionMatrixPtr, sample.projectionMatrix);
    sample.projectionMatrixValid =
        sample.projectionMatrixRead && IsMatrixFinite(sample.projectionMatrix) &&
        IsMatrixNonIdentity(sample.projectionMatrix);

    return sample;
}

ViewMatrixProbeResult ProbeViewMatrixCandidate(uint64_t base)
{
    ViewMatrixProbeResult result{};
    result.samples.reserve(kViewMatrixSampleCount);

    for (size_t index = 0; index < kViewMatrixSampleCount; ++index) {
        ViewMatrixSample sample = ProbeViewMatrixSample(base);

        if (sample.rootPlausible)
            ++result.rootPlausible;
        if (sample.rootPlausible && sample.p1Plausible && sample.p2Plausible &&
            sample.p3Plausible && sample.p4Plausible) {
            ++result.chainComplete;
        }
        if (sample.renderMatrixValid)
            ++result.renderValid;
        if (sample.cameraMatrixValid)
            ++result.cameraValid;
        if (sample.projectionMatrixValid)
            ++result.projectionValid;

        if (!result.samples.empty()) {
            const ViewMatrixSample& previous = result.samples.back();
            if (previous.root != 0 && previous.root == sample.root)
                ++result.stableRootTransitions;
            if (previous.p2 != 0 && previous.p2 == sample.p2)
                ++result.stableP2Transitions;
        }

        result.samples.push_back(sample);
        if (index + 1 < kViewMatrixSampleCount)
            Sleep(50);
    }

    result.accepted =
        result.rootPlausible == kViewMatrixSampleCount &&
        result.chainComplete >= 3 &&
        result.renderValid >= 3 &&
        result.cameraValid >= 3 &&
        result.stableRootTransitions >= 2;
    return result;
}

void LogMatrixSummary(ProbeLog& log, const char* label, const ProbeMatrix& matrix)
{
    log.Print("[SAMPLE] %s rows=[%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f]",
        label,
        matrix.m11, matrix.m12, matrix.m13, matrix.m14,
        matrix.m21, matrix.m22, matrix.m23, matrix.m24,
        matrix.m31, matrix.m32, matrix.m33, matrix.m34,
        matrix.m41, matrix.m42, matrix.m43, matrix.m44);
}

void LogViewMatrixProbe(ProbeLog& log, const ViewMatrixProbeResult& result)
{
    log.Print("[CHECK] CN ViewMatrix candidate: rva=0x%llX formula=direct_read samples=%zu root_plausible=%zu chain_complete=%zu render_valid=%zu camera_valid=%zu projection_valid=%zu stable_root=%zu stable_p2=%zu accepted=%d",
        static_cast<unsigned long long>(kCnViewMatrixDirectRva),
        result.samples.size(),
        result.rootPlausible,
        result.chainComplete,
        result.renderValid,
        result.cameraValid,
        result.projectionValid,
        result.stableRootTransitions,
        result.stableP2Transitions,
        result.accepted ? 1 : 0);

    for (size_t index = 0; index < result.samples.size(); ++index) {
        const ViewMatrixSample& sample = result.samples[index];
        log.Print("[SAMPLE] viewmatrix[%zu] root_read=%d root=0x%llX root_plausible=%d p1=0x%llX p1_ok=%d p2=0x%llX p2_ok=%d p3=0x%llX p3_ok=%d p4=0x%llX p4_ok=%d render_ptr=0x%llX render_read=%d render_valid=%d camera_ptr=0x%llX camera_read=%d camera_valid=%d proj_ptr=0x%llX proj_read=%d proj_valid=%d",
            index,
            sample.rootRead ? 1 : 0,
            static_cast<unsigned long long>(sample.root),
            sample.rootPlausible ? 1 : 0,
            static_cast<unsigned long long>(sample.p1),
            sample.p1Plausible ? 1 : 0,
            static_cast<unsigned long long>(sample.p2),
            sample.p2Plausible ? 1 : 0,
            static_cast<unsigned long long>(sample.p3),
            sample.p3Plausible ? 1 : 0,
            static_cast<unsigned long long>(sample.p4),
            sample.p4Plausible ? 1 : 0,
            static_cast<unsigned long long>(sample.renderMatrixPtr),
            sample.renderMatrixRead ? 1 : 0,
            sample.renderMatrixValid ? 1 : 0,
            static_cast<unsigned long long>(sample.cameraMatrixPtr),
            sample.cameraMatrixRead ? 1 : 0,
            sample.cameraMatrixValid ? 1 : 0,
            static_cast<unsigned long long>(sample.projectionMatrixPtr),
            sample.projectionMatrixRead ? 1 : 0,
            sample.projectionMatrixValid ? 1 : 0);

        if (index == 0) {
            if (sample.renderMatrixRead)
                LogMatrixSummary(log, "viewmatrix.render_vp[0]", sample.renderMatrix);
            if (sample.cameraMatrixRead)
                LogMatrixSummary(log, "viewmatrix.camera_view[0]", sample.cameraMatrix);
            if (sample.projectionMatrixRead)
                LogMatrixSummary(log, "viewmatrix.proj[0]", sample.projectionMatrix);
        }
    }

    log.Print("[RESULT] cn_viewmatrix_direct_candidate=%s rva=0x%llX ok=%d",
        "base+0x49A6A90",
        static_cast<unsigned long long>(kCnViewMatrixDirectRva),
        result.accepted ? 1 : 0);
}

void AddRuntimeComponentSample(
    std::vector<RuntimeComponentSample>& samples,
    uint64_t parent,
    uint32_t matchId,
    bool matchIdRead,
    const ComponentLayoutProbe& component)
{
    if (!component.encryptedSlotNonZero ||
        samples.size() >= kMaxRuntimeComponentSamples) {
        return;
    }

    RuntimeComponentSample sample{};
    sample.parent = parent;
    sample.matchId = matchId;
    sample.matchIdRead = matchIdRead;
    sample.layout = component;
    sample.base = component.encryptedSlot;
    sample.basePlausible = IsPlausibleUserPointer(sample.base);
    samples.push_back(sample);
}

void AddComponentSemanticSample(
    EntityProbeStats& stats,
    uint64_t parent,
    uint32_t matchId,
    bool matchIdRead,
    const ComponentLayoutProbe& component)
{
    if (component.type == kTypePlayerController) {
        AddRuntimeComponentSample(
            stats.playerControllerSamples, parent, matchId, matchIdRead, component);
    } else if (component.type == kTypeVisibility) {
        AddRuntimeComponentSample(
            stats.visibilitySamples, parent, matchId, matchIdRead, component);
    }

    if (!component.encryptedSlotNonZero ||
        (component.type != kTypeHeroId &&
         component.type != kTypeTeam &&
         component.type != kTypeLink)) {
        return;
    }

    if (component.type == kTypeTeam && stats.teamSamples >= kMaxTeamSemanticSamples)
        return;
    if (component.type == kTypeLink && stats.linkSamples >= kMaxLinkSemanticSamples)
        return;
    if (component.type == kTypeHeroId && stats.heroSamples >= kMaxHeroSemanticSamples)
        return;

    ComponentSemanticSample sample{};
    sample.parent = parent;
    sample.matchId = matchId;
    sample.matchIdRead = matchIdRead;
    sample.layout = component;
    sample.decodedIdentity = component.encryptedSlot;
    sample.decodedPlausible = IsPlausibleUserPointer(sample.decodedIdentity);
    if (sample.decodedPlausible)
        ++stats.identityDecodedPlausible;

    if (sample.decodedPlausible && component.type == kTypeHeroId) {
        sample.heroIdRead = ReadValue(sample.decodedIdentity + kHeroIdField, sample.heroId);
        if (sample.heroIdRead) {
            ++stats.heroIdReads;
            sample.heroIdHasPrefix = HasHeroIdPrefix(sample.heroId);
            sample.heroIdKnown = OW::GameData::IsKnownHeroId(sample.heroId);
            if (sample.heroIdHasPrefix)
                ++stats.heroIdPrefixMatches;
            if (sample.heroIdKnown)
                ++stats.heroIdKnownMatches;
        }
    }

    if (sample.decodedPlausible && component.type == kTypeTeam) {
        sample.teamFlagsRead = ReadValue(sample.decodedIdentity + kTeamFlagsField, sample.teamFlags);
        sample.teamFlagsSecondRead = ReadValue(sample.decodedIdentity + kTeamFlagsField, sample.teamFlagsSecond);
        if (sample.teamFlagsRead) {
            ++stats.teamFlagReads;
            sample.teamMask = sample.teamFlags & static_cast<uint32_t>(kTeamFlagsMask);
            sample.teamMaskPresent = sample.teamMask != 0;
            sample.teamStable = sample.teamFlagsSecondRead && sample.teamFlagsSecond == sample.teamFlags;
            if (sample.teamStable)
                ++stats.teamFlagStableReads;
            if (sample.teamMaskPresent)
                ++stats.teamFlagMaskPresent;
        }
    }

    if (sample.decodedPlausible && component.type == kTypeLink) {
        const bool firstRead = ReadValue(sample.decodedIdentity + kLinkUniqueId, sample.linkId);
        const bool secondRead = ReadValue(sample.decodedIdentity + kLinkUniqueIdAlt, sample.linkIdAlt);
        sample.linkRead = firstRead && secondRead;
        if (sample.linkRead)
            ++stats.linkReads;
    }

    stats.componentSamples.push_back(sample);
    if (component.type == kTypeTeam)
        ++stats.teamSamples;
    else if (component.type == kTypeLink)
        ++stats.linkSamples;
    else if (component.type == kTypeHeroId)
        ++stats.heroSamples;
}

void FinalizeLinkSemanticSamples(EntityProbeStats& stats)
{
    for (ComponentSemanticSample& sample : stats.componentSamples) {
        if (sample.layout.type != kTypeLink || !sample.linkRead)
            continue;

        sample.linkIdMatchesKnownEntity =
            sample.linkId != 0 && stats.matchIds.contains(sample.linkId);
        sample.linkIdAltMatchesKnownEntity =
            sample.linkIdAlt != 0 && stats.matchIds.contains(sample.linkIdAlt);
        if (sample.linkIdMatchesKnownEntity || sample.linkIdAltMatchesKnownEntity)
            ++stats.linkMatchesKnownEntity;
    }
}

void ScanEntityTable(uint64_t entityList, EntityProbeStats& stats)
{
    stats.entityList = entityList;
    if (!IsPlausibleUserPointer(entityList))
        return;

    std::vector<uint8_t> chunk(kEntityScanChunkSize);
    std::unordered_set<uint64_t> uniqueParents;
    constexpr std::array<uint32_t, 7> componentTypes{
        kTypeVelocity,
        kTypeTeam,
        kTypeBone,
        kTypeLink,
        kTypeVisibility,
        kTypePlayerController,
        kTypeHeroId,
    };

    for (size_t offset = 0; offset < kEntityScanSize; offset += kEntityScanChunkSize) {
        const size_t remaining = kEntityScanSize - offset;
        const size_t chunkSize = std::min(remaining, kEntityScanChunkSize);
        if (!ReadBytes(entityList + offset, chunk.data(), chunkSize))
            continue;

        ++stats.readableChunks;
        stats.readableBytes += chunkSize;

        for (size_t slotOffset = 0; slotOffset + kEntityEntryStride <= chunkSize;
             slotOffset += kEntityEntryStride) {
            ++stats.slotsScanned;

            uint64_t parent = 0;
            std::memcpy(&parent, chunk.data() + slotOffset, sizeof(parent));
            if (!parent)
                continue;

            ++stats.nonZeroSlots;
            if (!IsPlausibleUserPointer(parent))
                continue;

            ++stats.plausibleParents;
            if (!uniqueParents.insert(parent).second)
                continue;

            ++stats.uniqueParents;
            if (stats.sampleParents.size() < 8)
                stats.sampleParents.push_back(parent);

            uint32_t matchId = 0;
            const bool matchIdRead = ReadValue(parent + kEntityMatchId, matchId);
            if (matchIdRead) {
                ++stats.matchIdReads;
                if (matchId != 0) {
                    ++stats.matchIdNonZero;
                    stats.matchIds.insert(matchId);
                }
            }

            uint64_t poolPtr = 0;
            if (ReadValue(parent + kEntityPoolPtr, poolPtr)) {
                ++stats.poolPtrReads;
                if (poolPtr != 0)
                    ++stats.poolPtrNonZero;
            }

            const uint64_t poolBase = MaskPoolPointer(poolPtr);
            uint64_t poolId = 0;
            if (IsPlausibleUserPointer(poolBase) && ReadValue(poolBase + kPoolPoolId, poolId)) {
                ++stats.poolIdReads;
                if (poolId != 0)
                    ++stats.poolIdNonZero;
            }

            uint64_t table = 0;
            if (ReadValue(parent + 0x80, table)) {
                ++stats.componentTableReads;
                if (IsPlausibleUserPointer(table))
                    ++stats.componentTablePlausible;
            }

            for (const uint32_t type : componentTypes) {
                const ComponentLayoutProbe component = ProbeComponentLayout(parent, type);
                if (component.present)
                    ++stats.componentTypePresent;
                if (component.encryptedSlotNonZero)
                    ++stats.encryptedSlotsNonZero;
                AddComponentSemanticSample(stats, parent, matchId, matchIdRead, component);
            }
        }
    }

    FinalizeLinkSemanticSamples(stats);
}

struct PlayerControllerVectorSample {
    uint64_t parent = 0;
    uint32_t matchId = 0;
    uint64_t base = 0;
    bool read = false;
    ProbeVector3 value{};
    float length = 0.0f;
    float cameraDot = 0.0f;
    bool finite = false;
    bool normalized = false;
    bool cameraAligned = false;
};

struct PlayerControllerProbeResult {
    std::vector<PlayerControllerVectorSample> samples;
    size_t componentSamples = 0;
    size_t plausibleBases = 0;
    size_t vectorReads = 0;
    size_t finiteVectors = 0;
    size_t normalizedVectors = 0;
    size_t cameraAlignedVectors = 0;
    bool cameraForwardValid = false;
    ProbeVector3 cameraForward{};
    bool accepted = false;
};

PlayerControllerProbeResult ProbePlayerControllerViewDirection(
    const EntityProbeStats& stats,
    const ViewMatrixProbeResult& viewMatrixResult)
{
    PlayerControllerProbeResult result{};
    result.componentSamples = stats.playerControllerSamples.size();

    for (const ViewMatrixSample& sample : viewMatrixResult.samples) {
        if (!sample.cameraMatrixValid)
            continue;
        result.cameraForward = NormalizeVector(MatrixForward(sample.cameraMatrix));
        result.cameraForwardValid = IsVectorFinite(result.cameraForward) &&
            VectorLength(result.cameraForward) > 0.5f;
        if (result.cameraForwardValid)
            break;
    }

    for (const RuntimeComponentSample& component : stats.playerControllerSamples) {
        PlayerControllerVectorSample sample{};
        sample.parent = component.parent;
        sample.matchId = component.matchId;
        sample.base = component.base;
        if (component.basePlausible)
            ++result.plausibleBases;

        sample.read = component.basePlausible &&
            ReadValue(component.base + kPlayerControllerViewDirection, sample.value);
        if (sample.read)
            ++result.vectorReads;

        sample.finite = sample.read && IsVectorFinite(sample.value);
        if (sample.finite)
            ++result.finiteVectors;

        sample.length = sample.finite ? VectorLength(sample.value) : 0.0f;
        sample.normalized = sample.finite && sample.length >= 0.80f && sample.length <= 1.20f;
        if (sample.normalized)
            ++result.normalizedVectors;

        if (sample.normalized && result.cameraForwardValid) {
            const ProbeVector3 normalized = NormalizeVector(sample.value);
            sample.cameraDot = DotVector(normalized, result.cameraForward);
            sample.cameraAligned = std::fabs(sample.cameraDot) >= 0.75f;
            if (sample.cameraAligned)
                ++result.cameraAlignedVectors;
        }

        if (result.samples.size() < 12)
            result.samples.push_back(sample);
    }

    result.accepted =
        result.plausibleBases > 0 &&
        result.vectorReads > 0 &&
        result.normalizedVectors > 0 &&
        (!result.cameraForwardValid || result.cameraAlignedVectors > 0);
    return result;
}

void LogPlayerControllerProbe(ProbeLog& log, const PlayerControllerProbeResult& result)
{
    log.Print("[CHECK] CN PlayerController +0x1260: component_samples=%zu plausible_bases=%zu reads=%zu finite=%zu normalized=%zu camera_forward_valid=%d camera_aligned=%zu accepted=%d",
        result.componentSamples,
        result.plausibleBases,
        result.vectorReads,
        result.finiteVectors,
        result.normalizedVectors,
        result.cameraForwardValid ? 1 : 0,
        result.cameraAlignedVectors,
        result.accepted ? 1 : 0);
    if (result.cameraForwardValid) {
        log.Print("[SAMPLE] playercontroller.camera_forward x=%.6f y=%.6f z=%.6f",
            result.cameraForward.x,
            result.cameraForward.y,
            result.cameraForward.z);
    }
    for (size_t index = 0; index < result.samples.size(); ++index) {
        const PlayerControllerVectorSample& sample = result.samples[index];
        log.Print("[SAMPLE] playercontroller[%zu] parent=0x%llX match=0x%X base=0x%llX read=%d vec=(%.6f,%.6f,%.6f) len=%.6f normalized=%d camera_dot=%.6f camera_aligned=%d",
            index,
            static_cast<unsigned long long>(sample.parent),
            sample.matchId,
            static_cast<unsigned long long>(sample.base),
            sample.read ? 1 : 0,
            sample.value.x,
            sample.value.y,
            sample.value.z,
            sample.length,
            sample.normalized ? 1 : 0,
            sample.cameraDot,
            sample.cameraAligned ? 1 : 0);
    }
    log.Print("[RESULT] cn_playercontroller_viewdir_offset=0x%llX ok=%d",
        static_cast<unsigned long long>(kPlayerControllerViewDirection),
        result.accepted ? 1 : 0);
}

struct VisibilityOffsetProbe {
    uint64_t offset = 0;
    size_t reads = 0;
    size_t stableReads = 0;
    size_t nonZero = 0;
    size_t zero = 0;
    size_t one = 0;
    size_t otherSmall = 0;
    size_t smallMask = 0;
    size_t pointerLike = 0;
    size_t bit11Clear = 0;
    size_t bit11Set = 0;
    uint64_t firstValue = 0;
    bool hasFirstValue = false;
};

struct VisibilitySemanticSample {
    uint64_t parent = 0;
    uint32_t matchId = 0;
    uint64_t base = 0;
    bool raw98Read = false;
    bool raw98Stable = false;
    uint64_t raw98 = 0;
    bool byteA0Read = false;
    bool byteA0Stable = false;
    uint8_t byteA0 = 0;
    bool staticStateSet = false;
};

struct VisibilityProbeResult {
    size_t componentSamples = 0;
    size_t plausibleBases = 0;
    VisibilityOffsetProbe offsets[3]{};
    bool offset98BitfieldCandidate = false;
    size_t semanticPairs = 0;
    size_t semanticPairsStable = 0;
    size_t offset98BoolLike = 0;
    size_t byteA0BoolLike = 0;
    size_t staticStateSet = 0;
    size_t staticStateClear = 0;
    bool offset98BoolStateCandidate = false;
    std::vector<VisibilitySemanticSample> semanticSamples;
};

VisibilityProbeResult ProbeVisibilityOffsets(const EntityProbeStats& stats)
{
    VisibilityProbeResult result{};
    result.componentSamples = stats.visibilitySamples.size();
    const uint64_t offsets[] = { 0x98, 0x2D8, 0x2E8 };
    for (size_t index = 0; index < std::size(offsets); ++index)
        result.offsets[index].offset = offsets[index];

    for (const RuntimeComponentSample& component : stats.visibilitySamples) {
        if (!component.basePlausible)
            continue;
        ++result.plausibleBases;

        for (VisibilityOffsetProbe& offset : result.offsets) {
            uint64_t first = 0;
            uint64_t second = 0;
            const bool firstRead = ReadValue(component.base + offset.offset, first);
            const bool secondRead = ReadValue(component.base + offset.offset, second);
            if (!firstRead)
                continue;

            ++offset.reads;
            if (secondRead && second == first)
                ++offset.stableReads;
            if (first == 0)
                ++offset.zero;
            else if (first == 1)
                ++offset.one;
            else if (first < 0x100000000ull)
                ++offset.otherSmall;
            if (first != 0)
                ++offset.nonZero;
            if (!IsPlausibleUserPointer(first) && first < 0x100000000ull)
                ++offset.smallMask;
            if (IsPlausibleUserPointer(first))
                ++offset.pointerLike;
            if ((first & 0x800ull) == 0)
                ++offset.bit11Clear;
            else
                ++offset.bit11Set;
            if (!offset.hasFirstValue) {
                offset.firstValue = first;
                offset.hasFirstValue = true;
            }
        }

        VisibilitySemanticSample semantic{};
        semantic.parent = component.parent;
        semantic.matchId = component.matchId;
        semantic.base = component.base;
        uint64_t raw98Second = 0;
        uint8_t byteA0Second = 0;
        semantic.raw98Read = ReadValue(component.base + 0x98, semantic.raw98);
        semantic.raw98Stable =
            semantic.raw98Read &&
            ReadValue(component.base + 0x98, raw98Second) &&
            raw98Second == semantic.raw98;
        semantic.byteA0Read = ReadValue(component.base + 0xA0, semantic.byteA0);
        semantic.byteA0Stable =
            semantic.byteA0Read &&
            ReadValue(component.base + 0xA0, byteA0Second) &&
            byteA0Second == semantic.byteA0;

        if (semantic.raw98Read && semantic.byteA0Read) {
            ++result.semanticPairs;
            if (semantic.raw98Stable && semantic.byteA0Stable)
                ++result.semanticPairsStable;
            if (semantic.raw98 <= 1)
                ++result.offset98BoolLike;
            if (semantic.byteA0 <= 1)
                ++result.byteA0BoolLike;
            semantic.staticStateSet = semantic.raw98 == 1 || semantic.byteA0 != 0;
            if (semantic.staticStateSet)
                ++result.staticStateSet;
            else
                ++result.staticStateClear;
            if (result.semanticSamples.size() < 24)
                result.semanticSamples.push_back(semantic);
        }
    }

    const VisibilityOffsetProbe& offset98 = result.offsets[0];
    result.offset98BitfieldCandidate =
        offset98.reads > 0 &&
        offset98.stableReads == offset98.reads &&
        offset98.smallMask > 0 &&
        offset98.pointerLike == 0;
    result.offset98BoolStateCandidate =
        result.semanticPairs > 0 &&
        result.semanticPairsStable == result.semanticPairs &&
        result.offset98BoolLike == result.semanticPairs &&
        result.byteA0BoolLike == result.semanticPairs;
    return result;
}

void LogVisibilityProbe(ProbeLog& log, const VisibilityProbeResult& result)
{
    log.Print("[CHECK] CN Visibility runtime offsets: component_samples=%zu plausible_bases=%zu offset98_bitfield_candidate=%d",
        result.componentSamples,
        result.plausibleBases,
        result.offset98BitfieldCandidate ? 1 : 0);
    for (const VisibilityOffsetProbe& offset : result.offsets) {
        log.Print("[SAMPLE] visibility_offset off=0x%llX reads=%zu stable=%zu zero=%zu one=%zu other_small=%zu nonzero=%zu small_mask=%zu pointer_like=%zu bit11_clear=%zu bit11_set=%zu first=0x%llX",
            static_cast<unsigned long long>(offset.offset),
            offset.reads,
            offset.stableReads,
            offset.zero,
            offset.one,
            offset.otherSmall,
            offset.nonZero,
            offset.smallMask,
            offset.pointerLike,
            offset.bit11Clear,
            offset.bit11Set,
            static_cast<unsigned long long>(offset.firstValue));
    }
    log.Print("[CHECK] CN Visibility semantic-state probes: pairs=%zu stable=%zu raw98_bool_like=%zu byteA0_bool_like=%zu static_state_set=%zu static_state_clear=%zu bool_state_candidate=%d",
        result.semanticPairs,
        result.semanticPairsStable,
        result.offset98BoolLike,
        result.byteA0BoolLike,
        result.staticStateSet,
        result.staticStateClear,
        result.offset98BoolStateCandidate ? 1 : 0);
    for (size_t index = 0; index < result.semanticSamples.size(); ++index) {
        const VisibilitySemanticSample& sample = result.semanticSamples[index];
        log.Print("[SAMPLE] visibility_component[%zu] parent=0x%llX match=0x%X base=0x%llX raw98_read=%d raw98=0x%llX raw98_stable=%d byteA0_read=%d byteA0=0x%02X byteA0_stable=%d static_predicate_set=%d",
            index,
            static_cast<unsigned long long>(sample.parent),
            sample.matchId,
            static_cast<unsigned long long>(sample.base),
            sample.raw98Read ? 1 : 0,
            static_cast<unsigned long long>(sample.raw98),
            sample.raw98Stable ? 1 : 0,
            sample.byteA0Read ? 1 : 0,
            static_cast<unsigned>(sample.byteA0),
            sample.byteA0Stable ? 1 : 0,
            sample.staticStateSet ? 1 : 0);
    }
    log.Print("[RESULT] cn_visibility_runtime_offset_0x98_bool_state_candidate=%d",
        result.offset98BoolStateCandidate ? 1 : 0);
    log.Print("[RESULT] cn_visibility_runtime_offset_0x98_bitfield_candidate=%d",
        result.offset98BitfieldCandidate ? 1 : 0);
    log.Print("[RESULT] cn_visibility_runtime_offset_0x98_bit11_semantic_verified=0");
}

bool IsPlausibleViewport(uint32_t width, uint32_t height)
{
    if (width < 800 || width > 7680 || height < 600 || height > 4320)
        return false;
    const float ratio = static_cast<float>(width) / static_cast<float>(height);
    return ratio >= 1.0f && ratio <= 3.0f;
}

struct ViewportProbeCandidate {
    uint64_t widthRva = 0;
    uint64_t heightRva = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool hostMatch = false;
};

struct ViewportProbeResult {
    uint32_t hostWidth = 0;
    uint32_t hostHeight = 0;
    bool oldWorldWidthRead = false;
    bool oldWorldHeightRead = false;
    uint32_t oldWorldWidth = 0;
    uint32_t oldWorldHeight = 0;
    size_t widthStringRefs = 0;
    size_t heightStringRefs = 0;
    size_t plausiblePairs = 0;
    size_t hostMatchPairs = 0;
    std::vector<ViewportProbeCandidate> candidates;
    bool accepted = false;
};

ViewportProbeResult ProbeViewportCandidates(uint64_t base, uint64_t imageSize)
{
    ViewportProbeResult result{};
    result.hostWidth = static_cast<uint32_t>(GetSystemMetrics(SM_CXSCREEN));
    result.hostHeight = static_cast<uint32_t>(GetSystemMetrics(SM_CYSCREEN));
    result.oldWorldWidthRead =
        ReadValue(base + kWorldViewportWidthRva, result.oldWorldWidth);
    result.oldWorldHeightRead =
        ReadValue(base + kWorldViewportHeightRva, result.oldWorldHeight);

    const uint64_t scanStart = base + kCnDataScanStartRva;
    const uint64_t scanEnd = base + imageSize;
    const uint64_t widthStringVa = base + kCnFullScreenWidthStringRva;
    const uint64_t heightStringVa = base + kCnFullScreenHeightStringRva;
    if (scanEnd <= scanStart)
        return result;

    std::vector<uint8_t> chunk(kDataScanChunkSize);
    for (uint64_t address = scanStart; address < scanEnd; address += kDataScanChunkSize) {
        const size_t toRead = static_cast<size_t>(
            std::min<uint64_t>(kDataScanChunkSize, scanEnd - address));
        if (!ReadBytes(address, chunk.data(), toRead))
            continue;

        for (size_t offset = 0; offset + sizeof(uint64_t) <= toRead; offset += sizeof(uint64_t)) {
            uint64_t qword = 0;
            std::memcpy(&qword, chunk.data() + offset, sizeof(qword));
            if (qword == widthStringVa)
                ++result.widthStringRefs;
            if (qword == heightStringVa)
                ++result.heightStringRefs;
        }

        for (size_t offset = 0; offset + 0x70 + sizeof(uint32_t) <= toRead; offset += sizeof(uint32_t)) {
            uint32_t width = 0;
            uint32_t height = 0;
            std::memcpy(&width, chunk.data() + offset, sizeof(width));
            std::memcpy(&height, chunk.data() + offset + 0x70, sizeof(height));
            if (!IsPlausibleViewport(width, height))
                continue;

            ++result.plausiblePairs;
            const bool hostMatch =
                (result.hostWidth != 0 && result.hostHeight != 0 &&
                 width == result.hostWidth && height == result.hostHeight);
            if (hostMatch)
                ++result.hostMatchPairs;

            if (hostMatch || result.candidates.size() < 12) {
                ViewportProbeCandidate candidate{};
                candidate.widthRva = (address + offset) - base;
                candidate.heightRva = candidate.widthRva + 0x70;
                candidate.width = width;
                candidate.height = height;
                candidate.hostMatch = hostMatch;
                if (result.candidates.size() < 24)
                    result.candidates.push_back(candidate);
            }
        }
    }

    result.accepted = result.hostMatchPairs > 0;
    return result;
}

void LogViewportProbe(ProbeLog& log, const ViewportProbeResult& result)
{
    log.Print("[CHECK] CN Viewport globals: host=%ux%u old_world_read=(%d,%d) old_world_values=%ux%u string_refs width=%zu height=%zu plausible_pairs=%zu host_match_pairs=%zu accepted=%d",
        result.hostWidth,
        result.hostHeight,
        result.oldWorldWidthRead ? 1 : 0,
        result.oldWorldHeightRead ? 1 : 0,
        result.oldWorldWidth,
        result.oldWorldHeight,
        result.widthStringRefs,
        result.heightStringRefs,
        result.plausiblePairs,
        result.hostMatchPairs,
        result.accepted ? 1 : 0);
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const ViewportProbeCandidate& candidate = result.candidates[index];
        log.Print("[SAMPLE] viewport_candidate[%zu] width_rva=0x%llX height_rva=0x%llX value=%ux%u host_match=%d",
            index,
            static_cast<unsigned long long>(candidate.widthRva),
            static_cast<unsigned long long>(candidate.heightRva),
            candidate.width,
            candidate.height,
            candidate.hostMatch ? 1 : 0);
    }
    log.Print("[RESULT] cn_viewport_host_match_pair_found=%d",
        result.accepted ? 1 : 0);
}

struct GameAdminCandidate {
    uint64_t globalRva = 0;
    uint64_t stored = 0;
    uint64_t table = 0;
    uint64_t admin = 0;
    uint64_t uidOffset = 0;
    uint32_t uid = 0;
    const char* mode = "";
};

bool IsPreferredGameAdminUidOffset(uint64_t offset)
{
    return offset == 0x2F0 || offset == 0x4E4;
}

struct GameAdminProbeResult {
    size_t pointerCandidates = 0;
    bool pointerCandidateCapHit = false;
    std::vector<GameAdminCandidate> candidates;
    bool accepted = false;
};

bool EvaluateGameAdminTable(
    uint64_t globalRva,
    uint64_t stored,
    uint64_t table,
    const char* mode,
    const std::unordered_set<uint32_t>& matchIds,
    GameAdminProbeResult& result)
{
    if (!IsPlausibleUserPointer(table))
        return false;

    uint64_t admin = 0;
    if (!ReadValue(table + 8ull * 79ull, admin) || !IsPlausibleUserPointer(admin))
        return false;

    const uint64_t offsets[] = {
        0x2F0, 0x4E4,
        0x2E0, 0x2E4, 0x2E8, 0x2EC, 0x2F4, 0x2F8, 0x2FC,
        0x4E0, 0x4E8, 0x4EC, 0x4F0,
        0x100, 0x104, 0x108, 0x10C,
        0x138,
        0x200, 0x208, 0x210,
        0x300, 0x308, 0x310,
        0x500, 0x508,
        0xE0, 0xE4, 0xE8
    };

    for (uint64_t uidOffset : offsets) {
        uint32_t uid = 0;
        if (!ReadValue(admin + uidOffset, uid))
            continue;
        if (!IsLikelyEntityMatchId(uid) || !matchIds.contains(uid))
            continue;

        GameAdminCandidate candidate{};
        candidate.globalRva = globalRva;
        candidate.stored = stored;
        candidate.table = table;
        candidate.admin = admin;
        candidate.uidOffset = uidOffset;
        candidate.uid = uid;
        candidate.mode = mode;
        if (result.candidates.size() < 12)
            result.candidates.push_back(candidate);
        return true;
    }

    return false;
}

GameAdminProbeResult ProbeGameAdminDirectCandidates(
    uint64_t base,
    uint64_t imageSize,
    const EntityProbeStats& stats)
{
    GameAdminProbeResult result{};
    if (stats.matchIds.empty())
        return result;

    const uint64_t scanStart = base + kCnDataScanStartRva;
    const uint64_t scanEnd = base + imageSize;
    if (scanEnd <= scanStart)
        return result;

    std::vector<uint8_t> chunk(kDataScanChunkSize);
    std::unordered_set<uint64_t> seenStored;
    for (uint64_t address = scanStart; address < scanEnd; address += kDataScanChunkSize) {
        const size_t toRead = static_cast<size_t>(
            std::min<uint64_t>(kDataScanChunkSize, scanEnd - address));
        if (!ReadBytes(address, chunk.data(), toRead))
            continue;

        for (size_t offset = 0; offset + sizeof(uint64_t) <= toRead; offset += sizeof(uint64_t)) {
            uint64_t stored = 0;
            std::memcpy(&stored, chunk.data() + offset, sizeof(stored));
            if (!IsPlausibleUserPointer(stored))
                continue;
            if (!seenStored.insert(stored).second)
                continue;

            ++result.pointerCandidates;
            if (result.pointerCandidates > kMaxGameAdminPointerCandidates) {
                result.pointerCandidateCapHit = true;
                result.accepted = false;
                return result;
            }

            const uint64_t globalRva = (address + offset) - base;
            EvaluateGameAdminTable(globalRva, stored, stored, "direct_table", stats.matchIds, result);

            uint64_t root30Table = 0;
            if (ReadValue(stored + 0x30, root30Table)) {
                EvaluateGameAdminTable(globalRva, stored, root30Table, "root_plus_0x30_direct_table", stats.matchIds, result);
            }

            uint64_t root160Table = 0;
            if (ReadValue(stored + 0x160, root160Table)) {
                EvaluateGameAdminTable(globalRva, stored, root160Table, "root_plus_0x160_direct_table", stats.matchIds, result);
            }
        }
    }

    result.accepted =
        !result.pointerCandidateCapHit &&
        result.candidates.size() == 1 &&
        IsPreferredGameAdminUidOffset(result.candidates.front().uidOffset);
    return result;
}

void LogGameAdminCandidateProbe(ProbeLog& log, const GameAdminProbeResult& result)
{
    log.Print("[CHECK] CN GameAdmin direct/table scan: pointer_candidates=%zu cap_hit=%d candidates=%zu accepted=%d",
        result.pointerCandidates,
        result.pointerCandidateCapHit ? 1 : 0,
        result.candidates.size(),
        result.accepted ? 1 : 0);
    for (size_t index = 0; index < result.candidates.size(); ++index) {
        const GameAdminCandidate& candidate = result.candidates[index];
        log.Print("[SAMPLE] gameadmin_candidate[%zu] mode=%s global_rva=0x%llX stored=0x%llX table=0x%llX admin=0x%llX uid_off=0x%llX uid=0x%X",
            index,
            candidate.mode,
            static_cast<unsigned long long>(candidate.globalRva),
            static_cast<unsigned long long>(candidate.stored),
            static_cast<unsigned long long>(candidate.table),
            static_cast<unsigned long long>(candidate.admin),
            static_cast<unsigned long long>(candidate.uidOffset),
            candidate.uid);
    }
    log.Print("[RESULT] cn_gameadmin_direct_table_candidate_found=%d accepted=%d",
        result.candidates.empty() ? 0 : 1,
        result.accepted ? 1 : 0);
}

Options ParseOptions(int argc, char** argv)
{
    Options options{};
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--force-cn") {
            options.forceCn = true;
        } else if (arg == "--force-world") {
            options.forceWorld = true;
        } else if (arg == "--target" && i + 1 < argc) {
            options.targetProcess = argv[++i];
        } else if (arg == "--log" && i + 1 < argc) {
            options.logPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::printf(
                "Usage: CnOffsetProbe [--force-cn|--force-world] [--target Overwatch.exe] [--log un_cn_probe.log]\n");
            std::exit(0);
        }
    }

    if (options.forceCn && options.forceWorld) {
        std::printf("Only one of --force-cn or --force-world may be used.\n");
        std::exit(2);
    }

    return options;
}

const OffsetProfile& SelectProfile(const Options& options, const Memory::ProcessLookupResult& neacProcess)
{
    if (options.forceCn)
        return kCnProfile;
    if (options.forceWorld)
        return kWorldProfile;
    return neacProcess ? kCnProfile : kWorldProfile;
}

void LogSkippedChecks(
    ProbeLog& log,
    const OffsetProfile& profile,
    bool identityComponentOk,
    bool viewMatrixCandidateOk,
    bool playerControllerOk,
    bool visibilityOffset98Candidate,
    bool viewportCandidateOk,
    bool gameAdminCandidateOk)
{
    if (std::strcmp(profile.name, kCnProfile.name) != 0)
        return;

    if (identityComponentOk) {
        log.Print("[INFO] CN DecryptComponent transform: identity/no-post-table-transform passed live semantic checks in this run.");
    } else {
        log.Print("[SKIP] CN DecryptComponent transform: identity/no-post-table-transform did not meet live semantic acceptance in this run.");
    }
    if (viewMatrixCandidateOk) {
        log.Print("[INFO] CN ViewMatrix candidate: direct base+0x49A6A90 matrix-chain checks passed in this run.");
    } else {
        log.Print("[SKIP] CN ViewMatrix candidate: direct base+0x49A6A90 did not meet live matrix acceptance in this run.");
    }
    if (playerControllerOk) {
        log.Print("[INFO] CN PlayerController +0x1260: live normalized direction-vector checks passed in this run.");
    } else {
        log.Print("[SKIP] CN PlayerController +0x1260: live normalized direction-vector acceptance did not pass in this run.");
    }
    if (visibilityOffset98Candidate) {
        log.Print("[INFO] CN Visibility +0x98: live readable stable structural candidate in this run; bit11 LOS semantics are not verified.");
    } else {
        log.Print("[SKIP] CN Visibility +0x98: live structural-candidate acceptance did not pass in this run.");
    }
    if (viewportCandidateOk) {
        log.Print("[INFO] CN Viewport width/height: live host-match pair candidate found in this run.");
    } else {
        log.Print("[SKIP] CN Viewport width/height: no live host-match pair candidate found in this run.");
    }
    if (gameAdminCandidateOk) {
        log.Print("[INFO] CN GameAdmin direct/table scan: narrow preferred candidate matched a live entity MatchId.");
    } else {
        log.Print("[SKIP] CN GameAdmin root/formula/table: unresolved; direct/table scan found no narrow preferred candidate in this run.");
    }
}

int RunProbe(const Options& options)
{
    ProbeLog log(options.logPath);
    log.Print("[PROBE] CN offset probe starting. log=%s", options.logPath.c_str());

    mem.LoadDmaDeviceConfig();
    if (!mem.InitDma()) {
        log.Print("[FAIL] DMA initialization failed.");
        return 1;
    }

    const auto neacProcess = mem.FindProcessByPrefix(kCnDetectorPrefix);
    const OffsetProfile& profile = SelectProfile(options, neacProcess);

    if (neacProcess) {
        log.Print("[INFO] Detected %s* process: pid=%lu name=%s",
            kCnDetectorPrefix,
            static_cast<unsigned long>(neacProcess.pid),
            neacProcess.name.c_str());
    } else {
        log.Print("[INFO] No %s* process detected.", kCnDetectorPrefix);
    }

    log.Print("[INFO] Selected profile: %s (%s)%s",
        profile.name,
        profile.reason,
        (options.forceCn || options.forceWorld) ? " [forced]" : "");

    const DWORD targetPid = mem.GetPidFromName(options.targetProcess);
    if (targetPid == 0) {
        log.Print("[FAIL] Target process not found: %s", options.targetProcess.c_str());
        return 1;
    }

    log.Print("[INFO] Target process: %s pid=%lu",
        options.targetProcess.c_str(),
        static_cast<unsigned long>(targetPid));

    if (!mem.AttachToProcess(options.targetProcess, true)) {
        log.Print("[FAIL] AttachToProcess failed for %s", options.targetProcess.c_str());
        return 1;
    }

    const uint64_t base = static_cast<uint64_t>(mem.GetBaseDaddy(options.targetProcess));
    const uint64_t imageSize = static_cast<uint64_t>(mem.GetBaseSize(options.targetProcess));
    if (!base) {
        log.Print("[FAIL] Module base lookup failed for %s", options.targetProcess.c_str());
        return 1;
    }

    uint16_t mz = 0;
    const bool peRead = ReadValue(base, mz);
    log.Print("[CHECK] PE header: read=%d base=0x%llX size=0x%llX mz=0x%04X ok=%d",
        peRead ? 1 : 0,
        static_cast<unsigned long long>(base),
        static_cast<unsigned long long>(imageSize),
        static_cast<unsigned>(mz),
        (peRead && mz == 0x5A4D) ? 1 : 0);

    uint64_t entityList = 0;
    const bool entityRootRead = ReadValue(base + profile.entityBaseRva, entityList);
    log.Print("[CHECK] Entity root: rva=0x%llX read=%d value=0x%llX plausible=%d",
        static_cast<unsigned long long>(profile.entityBaseRva),
        entityRootRead ? 1 : 0,
        static_cast<unsigned long long>(entityList),
        IsPlausibleUserPointer(entityList) ? 1 : 0);

    EntityProbeStats stats{};
    ScanEntityTable(entityList, stats);
    log.Print("[CHECK] Entity scan: list=0x%llX chunks=%zu bytes=%zu slots=%zu nonzero=%zu plausible=%zu unique=%zu",
        static_cast<unsigned long long>(stats.entityList),
        stats.readableChunks,
        stats.readableBytes,
        stats.slotsScanned,
        stats.nonZeroSlots,
        stats.plausibleParents,
        stats.uniqueParents);
    log.Print("[CHECK] Entity fields: match_reads=%zu match_nonzero=%zu pool_reads=%zu pool_nonzero=%zu pool_id_reads=%zu pool_id_nonzero=%zu",
        stats.matchIdReads,
        stats.matchIdNonZero,
        stats.poolPtrReads,
        stats.poolPtrNonZero,
        stats.poolIdReads,
        stats.poolIdNonZero);
    log.Print("[CHECK] Component layout: table_reads=%zu table_plausible=%zu type_present=%zu encrypted_slots_nonzero=%zu",
        stats.componentTableReads,
        stats.componentTablePlausible,
        stats.componentTypePresent,
        stats.encryptedSlotsNonZero);
    log.Print("[CHECK] Component identity semantic: samples=%zu decoded_plausible=%zu hero_reads=%zu hero_prefix=%zu hero_known=%zu team_reads=%zu team_stable=%zu team_mask_present=%zu link_reads=%zu link_match_entity=%zu",
        stats.componentSamples.size(),
        stats.identityDecodedPlausible,
        stats.heroIdReads,
        stats.heroIdPrefixMatches,
        stats.heroIdKnownMatches,
        stats.teamFlagReads,
        stats.teamFlagStableReads,
        stats.teamFlagMaskPresent,
        stats.linkReads,
        stats.linkMatchesKnownEntity);

    for (size_t i = 0; i < stats.sampleParents.size(); ++i) {
        log.Print("[SAMPLE] parent[%zu]=0x%llX",
            i,
            static_cast<unsigned long long>(stats.sampleParents[i]));
    }

    for (const ComponentSemanticSample& sample : stats.componentSamples) {
        log.Print("[SAMPLE] component parent=0x%llX match_read=%d match=0x%X type=%s(0x%X) bucket=%u bitset=0x%llX index_base=%u component_index=%llu encrypted_slot=0x%llX identity_decoded=0x%llX plausible=%d",
            static_cast<unsigned long long>(sample.parent),
            sample.matchIdRead ? 1 : 0,
            sample.matchId,
            ComponentTypeName(sample.layout.type),
            sample.layout.type,
            sample.layout.bucket,
            static_cast<unsigned long long>(sample.layout.bitset),
            static_cast<unsigned>(sample.layout.indexBase),
            static_cast<unsigned long long>(sample.layout.componentIndex),
            static_cast<unsigned long long>(sample.layout.encryptedSlot),
            static_cast<unsigned long long>(sample.decodedIdentity),
            sample.decodedPlausible ? 1 : 0);

        if (sample.layout.type == kTypeHeroId) {
            const char* heroName = sample.heroIdKnown ? OW::GameData::HeroName(sample.heroId) : "";
            log.Print("[SAMPLE] component_sanity parent=0x%llX type=TYPE_P_HEROID read=%d heroid=0x%llX prefix=%d known=%d name=%s",
                static_cast<unsigned long long>(sample.parent),
                sample.heroIdRead ? 1 : 0,
                static_cast<unsigned long long>(sample.heroId),
                sample.heroIdHasPrefix ? 1 : 0,
                sample.heroIdKnown ? 1 : 0,
                heroName);
        } else if (sample.layout.type == kTypeTeam) {
            log.Print("[SAMPLE] component_sanity parent=0x%llX type=TYPE_TEAM read=%d flags=0x%X flags2=0x%X mask=0x%X stable=%d mask_present=%d",
                static_cast<unsigned long long>(sample.parent),
                sample.teamFlagsRead ? 1 : 0,
                sample.teamFlags,
                sample.teamFlagsSecond,
                sample.teamMask,
                sample.teamStable ? 1 : 0,
                sample.teamMaskPresent ? 1 : 0);
        } else if (sample.layout.type == kTypeLink) {
            log.Print("[SAMPLE] component_sanity parent=0x%llX type=TYPE_LINK read=%d link_d4=0x%X link_d8=0x%X match_d4=%d match_d8=%d",
                static_cast<unsigned long long>(sample.parent),
                sample.linkRead ? 1 : 0,
                sample.linkId,
                sample.linkIdAlt,
                sample.linkIdMatchesKnownEntity ? 1 : 0,
                sample.linkIdAltMatchesKnownEntity ? 1 : 0);
        }
    }

    float mouseScaleX = 0.0f;
    float mouseScaleY = 0.0f;
    const bool mouseXRead = ReadValue(base + profile.inputMouseScaleXRva, mouseScaleX);
    const bool mouseYRead = ReadValue(base + profile.inputMouseScaleYRva, mouseScaleY);
    log.Print("[CHECK] InputMouseScaleX: rva=0x%llX read=%d value=%.6f plausible=%d",
        static_cast<unsigned long long>(profile.inputMouseScaleXRva),
        mouseXRead ? 1 : 0,
        mouseScaleX,
        (mouseXRead && IsPlausibleSensitivity(mouseScaleX)) ? 1 : 0);
    log.Print("[CHECK] InputMouseScaleY: rva=0x%llX read=%d value=%.6f plausible=%d",
        static_cast<unsigned long long>(profile.inputMouseScaleYRva),
        mouseYRead ? 1 : 0,
        mouseScaleY,
        (mouseYRead && IsPlausibleSensitivity(mouseScaleY)) ? 1 : 0);

    const bool identityComponentOk =
        std::strcmp(profile.name, kCnProfile.name) == 0 &&
        stats.identityDecodedPlausible > 0 &&
        stats.heroIdKnownMatches >= 2 &&
        stats.teamFlagStableReads > 0 &&
        stats.teamFlagMaskPresent > 0 &&
        stats.linkMatchesKnownEntity > 0;
    if (std::strcmp(profile.name, kCnProfile.name) == 0) {
        log.Print("[RESULT] cn_identity_component_transform=%s ok=%d hero_known=%zu team_mask_present=%zu link_match_entity=%zu",
            "identity/no-post-table-transform",
            identityComponentOk ? 1 : 0,
            stats.heroIdKnownMatches,
            stats.teamFlagMaskPresent,
            stats.linkMatchesKnownEntity);
    }

    bool viewMatrixCandidateOk = false;
    bool playerControllerOk = false;
    bool visibilityOffset98Candidate = false;
    bool viewportCandidateOk = false;
    bool gameAdminCandidateOk = false;
    if (std::strcmp(profile.name, kCnProfile.name) == 0) {
        const ViewMatrixProbeResult viewMatrixResult = ProbeViewMatrixCandidate(base);
        LogViewMatrixProbe(log, viewMatrixResult);
        viewMatrixCandidateOk = viewMatrixResult.accepted;

        const PlayerControllerProbeResult playerControllerResult =
            ProbePlayerControllerViewDirection(stats, viewMatrixResult);
        LogPlayerControllerProbe(log, playerControllerResult);
        playerControllerOk = playerControllerResult.accepted;

        const VisibilityProbeResult visibilityResult = ProbeVisibilityOffsets(stats);
        LogVisibilityProbe(log, visibilityResult);
        visibilityOffset98Candidate = visibilityResult.offset98BitfieldCandidate;

        const ViewportProbeResult viewportResult = ProbeViewportCandidates(base, imageSize);
        LogViewportProbe(log, viewportResult);
        viewportCandidateOk = viewportResult.accepted;

        const GameAdminProbeResult gameAdminResult =
            ProbeGameAdminDirectCandidates(base, imageSize, stats);
        LogGameAdminCandidateProbe(log, gameAdminResult);
        gameAdminCandidateOk = gameAdminResult.accepted;
    }

    LogSkippedChecks(
        log,
        profile,
        identityComponentOk,
        viewMatrixCandidateOk,
        playerControllerOk,
        visibilityOffset98Candidate,
        viewportCandidateOk,
        gameAdminCandidateOk);

    const bool directChecksOk =
        peRead &&
        mz == 0x5A4D &&
        entityRootRead &&
        IsPlausibleUserPointer(entityList) &&
        stats.readableBytes > 0 &&
        stats.uniqueParents > 0;

    log.Print("[RESULT] direct_checks_ok=%d profile=%s", directChecksOk ? 1 : 0, profile.name);
    return directChecksOk ? 0 : 2;
}

} // namespace

int main(int argc, char** argv)
{
    return RunProbe(ParseOptions(argc, argv));
}
