#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Diagnostics {

enum class LogLevel {
    Error = 0,
    Warn,
    Info,
    Debug,
    Trace
};

enum class KeyStatus {
    Unknown = 0,
    Skipped,
    Resolving,
    Resolved,
    Failed
};

// ---- DMA callsite tagging ----

enum class DmaCallsite : uint8_t {
    Unknown = 0,
    EntityScan,
    EntityDecrypt,
    ViewMatrix,
    BoneChain,
    KeyState,
    Aimbot,
    RenderCanvas,
    EntityPrefetch,
    Count
};

const char* ToString(DmaCallsite cs);

// RAII guard — pushes a callsite onto the thread-local stack on construction,
// restores the previous one on destruction.  No heap allocation.
class ScopedDmaCallsite {
public:
    explicit ScopedDmaCallsite(DmaCallsite cs);
    ~ScopedDmaCallsite();
    ScopedDmaCallsite(const ScopedDmaCallsite&) = delete;
    ScopedDmaCallsite& operator=(const ScopedDmaCallsite&) = delete;

    static DmaCallsite Current();
    static void Push(DmaCallsite cs);
    static void Pop();

private:
    DmaCallsite m_previous;
};

// ---- Per-frame timing ----

struct FrameTiming {
    double totalMs = 0.0;
    double renderCallbackMs = 0.0;
    double presentMs = 0.0;
    uint64_t renderThreadDmaReads = 0;
    uint64_t renderThreadDmaFailures = 0;
    uint64_t renderThreadDmaTotalUs = 0;
    uint64_t renderThreadDmaMaxUs = 0;
};

enum class RenderPrimitiveKind {
    Line,
    Rect,
    FilledRect,
    Text,
    Icon,
};

struct RenderWorkloadStats {
    bool boxPerfMode = false;
    double totalMs = 0.0;
    double renderCallbackMs = 0.0;
    double presentMs = 0.0;
    uint64_t linePrimitives = 0;
    uint64_t rectPrimitives = 0;
    uint64_t filledRectPrimitives = 0;
    uint64_t textCalls = 0;
    uint64_t iconCalls = 0;
    uint64_t cornerBoxes = 0;
    uint64_t fastBoxes = 0;
};

// ---- Ring-buffer DMA window stats (lightweight) ----

struct DmaWindowStats {
    uint64_t totalReads = 0;
    uint64_t failedReads = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t perCallsiteReads[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteMaxUs[static_cast<int>(DmaCallsite::Count)]{};
};

// ---- Existing structures ----

struct DmaReadStats {
    uint64_t total = 0;
    uint64_t succeeded = 0;
    uint64_t failed = 0;
    uint64_t minLatencyUs = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t avgLatencyUs = 0;
};

struct ErrorCounters {
    uint64_t failedDmaReads = 0;
    uint64_t decryptFailures = 0;
    uint64_t invalidEntities = 0;
};

struct EntityProcessStats {
    size_t raw = 0;
    size_t validated = 0;
    size_t dynamic = 0;
    size_t nullPair = 0;
    size_t duplicate = 0;
    size_t healthBaseFail = 0;
    size_t healthBaseMissing = 0;
    size_t healthReadFail = 0;
    size_t linkBaseFail = 0;
    size_t heroBaseMissing = 0;
    size_t heroFallbackFail = 0;
    size_t nameUnknown = 0;
    size_t boneCandidates = 0;
    size_t boneBaseNonZero = 0;
    size_t velocityBoneDataNonZero = 0;
    size_t boneDataPtrNonZero = 0;
    size_t bonesBaseNonZero = 0;
    size_t velocityBoneIdTableNonZero = 0;
    size_t velocityBoneCountValid = 0;
    size_t velocityBoneIdTableReadable = 0;
    size_t velocityBoneHeadIdFound = 0;
    size_t skeletonAnyValid = 0;
    size_t skeletonHeadValid = 0;
    size_t headProbeCandidates = 0;
    size_t headProbeResolved = 0;
    size_t headProbeIdFound = 0;
    size_t headProbeLocalFinite = 0;
    size_t headProbeLocalNonZero = 0;
    size_t headProbeWorldNonZero = 0;
    size_t headProbeExceptions = 0;
    size_t headProbeNearCandidates = 0;
    size_t headProbeNearWorldNonZero = 0;
    size_t headProbeFarCandidates = 0;
    size_t headProbeFarWorldNonZero = 0;
    uint64_t sampleBoneAddress = 0;
    uint64_t sampleHealthFailComponentParent = 0;
    uint64_t sampleHealthFailLinkParent = 0;
    uint64_t sampleHealthFailHealthBase = 0;
    uint64_t sampleHealthFailLinkBase = 0;
    uint64_t sampleHealthFailVelocityBase = 0;
    uint64_t sampleHealthFailHeroBase = 0;
    uint64_t sampleHealthFailTeamBase = 0;
    uint64_t sampleHealthFailBoneBase = 0;
    int sampleHealthFailReadOk = 0;
    uint64_t sampleNameUnknownComponentParent = 0;
    uint64_t sampleNameUnknownLinkParent = 0;
    uint64_t sampleNameUnknownComponentMatchId = 0;
    uint64_t sampleNameUnknownLinkMatchId = 0;
    uint64_t sampleNameUnknownHeroBase = 0;
    uint64_t sampleNameUnknownHeroId = 0;
    uint64_t sampleNameUnknownHeroIdCandidate = 0;
    int sampleNameUnknownHeroIdCandidateOffset = -1;
    uint64_t sampleNameUnknownComponentHeroBase = 0;
    uint64_t sampleNameUnknownComponentHeroId = 0;
    uint64_t sampleNameUnknownComponentHeroIdCandidate = 0;
    int sampleNameUnknownComponentHeroIdCandidateOffset = -1;
    uint64_t sampleNameUnknownLinkBase = 0;
    uint64_t sampleNameUnknownSkillBase = 0;
    uint64_t sampleNameUnknownTeamBase = 0;
    uint64_t sampleNameUnknownBoneBase = 0;
    int sampleNameUnknownKind = 0;
    uint64_t sampleVelocityBase = 0;
    uint64_t sampleBoneBase = 0;
    uint64_t sampleVelocityBoneData = 0;
    uint64_t sampleBoneDataPtr = 0;
    uint64_t sampleBonesBase = 0;
    uint64_t sampleBoneIdTable = 0;
    int sampleBoneCount = 0;
    int sampleBoneIdTableReadable = 0;
    int sampleBoneHeadIndex = -1;
    uint64_t sampleHeadGoodAddress = 0;
    uint64_t sampleHeadGoodHeroId = 0;
    uint64_t sampleHeadBadAddress = 0;
    uint64_t sampleHeadBadHeroId = 0;
    uint64_t sampleHeadBadBoneData = 0;
    uint64_t sampleHeadBadBonesBase = 0;
    uint64_t sampleHeadBadBonePtr = 0;
    uint64_t sampleHeadBadBoneIdTable = 0;
    int sampleHeadGoodMappedIndex = -1;
    int sampleHeadBadMappedIndex = -1;
    int sampleHeadBadBoneCount = 0;
    int sampleHeadGoodLocalXCm = 0;
    int sampleHeadGoodLocalYCm = 0;
    int sampleHeadGoodLocalZCm = 0;
    int sampleHeadGoodWorldXCm = 0;
    int sampleHeadGoodWorldYCm = 0;
    int sampleHeadGoodWorldZCm = 0;
    int sampleHeadGoodDistanceCm = -1;
    int sampleHeadBadLocalXCm = 0;
    int sampleHeadBadLocalYCm = 0;
    int sampleHeadBadLocalZCm = 0;
};

struct PlayerInfoStats {
    bool boxPerfMode = false;
    bool fastBoxPath = false;
    double elapsedMs = 0.0;
    size_t input = 0;
    size_t projected = 0;
    size_t drawn = 0;
    size_t skippedDead = 0;
    size_t skippedLocalHealth = 0;
    size_t skippedLocalEntity = 0;
    size_t skippedDistance = 0;
    size_t skippedOpacity = 0;
    size_t skippedWorldToScreen = 0;
    size_t skippedWorldToScreenLow = 0;
    size_t skippedWorldToScreenHigh = 0;
    size_t skippedBox = 0;
    size_t skippedWindow = 0;
    bool sampleProjected = false;
    bool sampleDrawn = false;
    uint64_t sampleProjectedAddress = 0;
    uint64_t sampleProjectedHeroId = 0;
    int sampleProjectedLeft = 0;
    int sampleProjectedTop = 0;
    int sampleProjectedWidth = 0;
    int sampleProjectedHeight = 0;
    int sampleProjectedCenterX = 0;
    int sampleProjectedBottom = 0;
    int sampleProjectedDistanceM = 0;
    uint64_t sampleDrawnAddress = 0;
    uint64_t sampleDrawnHeroId = 0;
    int sampleDrawnLeft = 0;
    int sampleDrawnTop = 0;
    int sampleDrawnWidth = 0;
    int sampleDrawnHeight = 0;
    int sampleDrawnCenterX = 0;
    int sampleDrawnBottom = 0;
    int sampleDrawnDistanceM = 0;
};

struct LocalEntityStats {
    size_t angleCandidates = 0;
    size_t nearCameraCandidates = 0;
    size_t namedCandidates = 0;
    size_t selected = 0;
    size_t zeroHeadCandidates = 0;
    size_t nonZeroPositionCandidates = 0;
    uint64_t selectedAddress = 0;
    uint64_t selectedHeroId = 0;
    uint64_t selectedAngleBase = 0;
    int selectedHealth = 0;
    int bestDistanceCm = -1;
    uint64_t bestAddress = 0;
    uint64_t bestHeroId = 0;
    uint64_t bestAngleBase = 0;
    int bestHealth = 0;
    int bestHeadXCm = 0;
    int bestHeadYCm = 0;
    int bestHeadZCm = 0;
    int bestPosXCm = 0;
    int bestPosYCm = 0;
    int bestPosZCm = 0;
    int cameraXCm = 0;
    int cameraYCm = 0;
    int cameraZCm = 0;
};

struct RosterStats {
    size_t fresh = 0;
    size_t dead = 0;
    size_t missing = 0;
    size_t expired = 0;
    size_t heroChanged = 0;
};

struct EntityScanDetailStats {
    uint64_t entityList = 0;
    size_t readableBytes = 0;
    size_t readableChunks = 0;
    size_t slotsScanned = 0;
    size_t nonZeroSlots = 0;
    size_t plausibleSlots = 0;
    size_t records = 0;
    size_t matchIds = 0;
    size_t linkPresent = 0;
    size_t linkUidNonZero = 0;
    size_t linkMatched = 0;
    size_t linkPairs = 0;
    size_t selfHealthBase = 0;
    size_t selfHealthRead = 0;
    size_t selfHealthPlausible = 0;
    size_t selfHeroBase = 0;
    size_t selfHeroRead = 0;
    size_t selfHeroKnown = 0;
    size_t selfVelocityBase = 0;
    size_t selfBoneBase = 0;
    size_t selfPlayable = 0;
    size_t dynamicPairs = 0;
    size_t totalPairs = 0;
    int sampleRejectReason = 0;
    uint64_t sampleRejectParent = 0;
    uint64_t sampleRejectMatchId = 0;
    uint64_t sampleRejectHealthBase = 0;
    uint64_t sampleRejectHeroBase = 0;
    uint64_t sampleRejectHeroId = 0;
    uint64_t sampleRejectVelocityBase = 0;
    uint64_t sampleRejectBoneBase = 0;
    int sampleRejectHealthCm = 0;
    int sampleRejectHealthMaxCm = 0;
};

struct StatusSnapshot {
    size_t entityCount = 0;
    size_t lastScanEntityCount = 0;
    uint64_t entityScanCycles = 0;
    uint64_t entityProcessCycles = 0;
    double entityScanHz = 0.0;
    double entityProcessHz = 0.0;
    double fps = 0.0;
    DmaReadStats dmaReads{};
    ErrorCounters errors{};
    bool dmaReady = false;
    bool processAttached = false;
    KeyStatus keyStatus = KeyStatus::Unknown;
    uint64_t globalKey1 = 0;
    uint64_t globalKey2 = 0;
    bool dmaProbeAttempted = false;
    bool dmaProbeSucceeded = false;
    uint64_t dmaProbeAddress = 0;
    uint16_t dmaProbeMagic = 0;
    bool viewMatrixResolved = false;
    bool viewMatrixValid = false;
    bool renderDrawRadarCalled = false;
    bool renderPlayerInfoCalled = false;
    bool renderSkillInfoCalled = false;
    bool renderEntityListEmpty = true;
    RenderWorkloadStats renderWorkload{};
    EntityProcessStats entityProcess{};
    PlayerInfoStats playerInfo{};
    LocalEntityStats localEntity{};
    RosterStats roster{};
    EntityScanDetailStats entityScanDetail{};
};

void Initialize(LogLevel minLevel = LogLevel::Info, const char* logPath = "./unleashed_diag.log");
void Shutdown();
bool IsInitialized();

void InitializeAimLog(const char* logPath = "./unleashed_aim_diag.log");
void ShutdownAimLog();
void Aim(const char* fmt, ...);

constexpr size_t DefaultLogLineCapacity = 100;
void SetLogLineCapacity(size_t maxLines);
size_t GetLogLineCapacity();
std::vector<std::string> GetLogLines();
void ClearLogLines();
bool IsLogOverlayVisible();
void SetLogOverlayVisible(bool visible);

void SetLogLevel(LogLevel minLevel);
LogLevel GetLogLevel();

void Log(LogLevel level, const char* fmt, ...);
void Error(const char* fmt, ...);
void Warn(const char* fmt, ...);
void Info(const char* fmt, ...);
void Debug(const char* fmt, ...);
void Trace(const char* fmt, ...);

void RecordFrame();
void BeginRenderWorkloadFrame(bool boxPerfMode);
void RecordRenderPrimitive(RenderPrimitiveKind kind, uint64_t count = 1);
void RecordRenderBox(bool fastPath);
void RecordDmaRead(bool success, std::chrono::steady_clock::duration latency);
void RecordDmaRead(bool success, uint64_t latencyUs);
void RecordDmaRead(bool success, uint64_t latencyUs, DmaCallsite callsite);
void RecordDecryptFailure();
void RecordInvalidEntity();

// Frame timing — call once per frame from the render thread after Present().
// Emits a SLOW_FRAME diagnostic line when total time exceeds the threshold.
void RecordFrameTiming(const FrameTiming& timing, double slowThresholdMs = 16.6);

// Called once at startup to tag the render thread for per-thread DMA tracking.
void SetRenderThread();

// Returns aggregate DMA stats for reads completed in the last `windowMs`
// milliseconds.  Scans a lock-free ring buffer — O(capacity) but intended
// for diagnostic sampling, not per-frame use.
DmaWindowStats GetDmaWindowStats(uint64_t windowMs);
void RecordEntityScanCycle(size_t entityCount, double measuredHz = -1.0);
void RecordEntityProcessCycle(double measuredHz);

void SetEntityCount(size_t entityCount);
void SetDmaReady(bool ready);
void SetProcessAttached(bool attached);
void SetKeyStatus(KeyStatus status, uint64_t key1 = 0, uint64_t key2 = 0);
void SetDmaProbeResult(bool attempted, bool succeeded, uint64_t address = 0, uint16_t magic = 0);
void SetViewMatrixStatus(bool resolved, bool valid);
void SetRenderPipelineStatus(bool drawRadarCalled, bool playerInfoCalled, bool skillInfoCalled, bool entityListEmpty);
void SetEntityProcessStats(const EntityProcessStats& stats);
void SetPlayerInfoStats(const PlayerInfoStats& stats);
void SetLocalEntityStats(const LocalEntityStats& stats);
void SetRosterStats(const RosterStats& stats);
void SetEntityScanDetailStats(const EntityScanDetailStats& stats);

StatusSnapshot Snapshot();
void DumpStatus();

const char* ToString(LogLevel level);
const char* ToString(KeyStatus status);

} // namespace Diagnostics
