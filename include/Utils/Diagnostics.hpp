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

struct StatusSnapshot {
    size_t entityCount = 0;
    size_t lastScanEntityCount = 0;
    uint64_t entityScanCycles = 0;
    uint64_t entityProcessCycles = 0;
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
    EntityProcessStats entityProcess{};
    PlayerInfoStats playerInfo{};
    LocalEntityStats localEntity{};
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
void RecordEntityScanCycle(size_t entityCount);
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

StatusSnapshot Snapshot();
void DumpStatus();

const char* ToString(LogLevel level);
const char* ToString(KeyStatus status);

} // namespace Diagnostics
