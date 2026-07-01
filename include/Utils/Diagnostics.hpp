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

enum class SdkFrameSource : uint8_t {
    Unknown = 0,
    Scan,
    Process
};

// ---- DMA callsite tagging ----

enum class DmaCallsite : uint8_t {
    Unknown = 0,
    EntityScan,
    EntityScanRoot,
    EntityScanListRead,
    EntityScanRecordBuild,
    EntityScanRecordMatchId,
    EntityScanRecordHeader,
    EntityScanRecordPoolPtr,
    EntityScanRecordPoolId,
    EntityScanMatchLink,
    EntityScanTargetMap,
    EntityScanMapCandidate,
    EntityScanLinkTargetResolve,
    EntityScanSelfValidation,
    EntityScanComponentValidation,
    EntityDecrypt,
    EntityBaseDecrypt,
    EntityHeaderSpecial,
    EntityHotScatter,
    EntityHotFields,
    EntityRotationPosition,
    ViewMatrix,
    BoneChain,
    KeyState,
    Aimbot,
    RenderCanvas,
    EntityPrefetch,
    Count
};

const char* ToString(DmaCallsite cs);
const char* ToString(SdkFrameSource source);

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

struct PublishCadenceStats {
    uint64_t cycles = 0;
    double hz = 0.0;
    uint64_t ageMs = 0;
    uint64_t lastIntervalMs = 0;
    uint64_t maxIntervalMs = 0;
    size_t lastCount = 0;
};

struct SnapshotCopyStats {
    uint64_t copies = 0;
    double lastMs = 0.0;
    double maxMs = 0.0;
    size_t lastCount = 0;
};

struct ViewMatrixConsumerStats {
    uint64_t uses = 0;
    uint64_t lastAgeMs = 0;
    uint64_t maxAgeMs = 0;
    uint64_t missingPublishUses = 0;
    uint64_t over16Ms = 0;
    uint64_t over33Ms = 0;
    uint64_t over50Ms = 0;
};

// ---- Ring-buffer DMA window stats (lightweight) ----

struct DmaWindowStats {
    uint64_t windowMs = 0;
    uint64_t totalReads = 0;
    uint64_t failedReads = 0;
    uint64_t maxLatencyUs = 0;
    uint64_t slowThresholdUs = 50000;
    uint64_t slowReads = 0;
    uint64_t slowFailedReads = 0;
    uint64_t perCallsiteReads[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteFailedReads[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteMaxUs[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteSuccessMaxUs[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteFailedMaxUs[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteSlowReads[static_cast<int>(DmaCallsite::Count)]{};
    uint64_t perCallsiteSlowFailedReads[static_cast<int>(DmaCallsite::Count)]{};
};

struct DmaSlowReadSample {
    uint64_t sequence = 0;
    uint64_t threadId = 0;
    uint64_t startedTickMs = 0;
    uint64_t completedTickMs = 0;
    uint64_t completedAgeMs = 0;
    uint64_t startedAgeMs = 0;
    uint64_t latencyUs = 0;
    DmaCallsite callsite = DmaCallsite::Unknown;
    bool success = false;
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
    size_t renderPredictionCandidates = 0;
    size_t renderPredictionApplied = 0;
    size_t renderPredictionWorldDeltaFallback = 0;
    int renderPredictionMaxLeadMs = 0;
    int renderPredictionMaxOffsetCm = 0;
    size_t trainingBotPredictionCandidates = 0;
    size_t trainingBotPredictionApplied = 0;
    size_t trainingBotPredictionLeadDrops = 0;
    int trainingBotPredictionMaxLeadMs = 0;
    int trainingBotPredictionMaxOffsetCm = 0;
    uint64_t trainingBotPredictionLastDropAddress = 0;
    int trainingBotPredictionLastDropFromMs = 0;
    int trainingBotPredictionLastDropToMs = 0;
    int trainingBotPredictionLastDropOffsetCm = 0;
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

struct ViewMatrixStabilityStats {
    uint64_t rejected = 0;
    uint64_t transientRejected = 0;
    uint64_t acceptedLargeJump = 0;
    uint64_t lastRejectAgeMs = 0;
    uint64_t lastRejectDeltaMilli = 0;
    uint64_t maxRejectDeltaMilli = 0;
    uint64_t lastAcceptedJumpDeltaMilli = 0;
};

struct ProjectionStabilityStats {
    uint64_t globalJumpFrames = 0;
    uint64_t lastGlobalJumpAgeMs = 0;
    uint64_t lastGlobalJumpMatched = 0;
    int64_t lastGlobalJumpMedianDxPx = 0;
    int64_t lastGlobalJumpMedianDyPx = 0;
    uint64_t lastGlobalJumpDeltaPx = 0;
    uint64_t maxGlobalJumpDeltaPx = 0;
};

struct OverlayCanvasStats {
    int x = 0;
    int y = 0;
    uint32_t windowWidth = 0;
    uint32_t windowHeight = 0;
    uint32_t clientWidth = 0;
    uint32_t clientHeight = 0;
    uint32_t swapchainWidth = 0;
    uint32_t swapchainHeight = 0;
    int displayWidth = 0;
    int displayHeight = 0;
    bool visible = false;
    uint64_t boundsChanges = 0;
    uint64_t swapchainResizes = 0;
    uint64_t lastBoundsChangeAgeMs = 0;
    uint64_t lastSwapchainResizeAgeMs = 0;
};

struct EntityScanDetailStats {
    uint64_t entityList = 0;
    double scanRootMs = 0.0;
    double scanListReadMs = 0.0;
    double scanSlotWalkMs = 0.0;
    double scanRecordBuildMs = 0.0;
    double scanMatchLinkMs = 0.0;
    double scanCnNeTargetMapMs = 0.0;
    double scanCnNeSelfValidationMs = 0.0;
    double scanComponentOnlyValidationMs = 0.0;
    double scanDynamicPairMs = 0.0;
    double scanFinalizeMs = 0.0;
    uint64_t scanDmaReadsDelta = 0;
    uint64_t scanDmaFailDelta = 0;
    bool scanDmaRangeDiagEnabled = false;
    uint64_t scanDmaRangeReads = 0;
    uint64_t scanDmaRangeFailed = 0;
    uint64_t scanDmaRangeMaxLatencyUs = 0;
    uint8_t scanDmaRangeMaxCallsite = 0;
    uint64_t scanDmaRangeScannerReads = 0;
    uint64_t scanDmaRangeScannerMaxLatencyUs = 0;
    uint8_t scanDmaRangeScannerMaxCallsite = 0;
    uint64_t scanDmaRangeForeignReads = 0;
    uint64_t scanDmaRangeForeignMaxLatencyUs = 0;
    uint8_t scanDmaRangeForeignMaxCallsite = 0;
    uint64_t scanDmaRangeRootMaxUs = 0;
    uint64_t scanDmaRangeListReadMaxUs = 0;
    uint64_t scanDmaRangeRecordHeaderMaxUs = 0;
    uint64_t scanDmaRangeRecordPoolIdMaxUs = 0;
    uint64_t scanDmaRangeTargetMapMaxUs = 0;
    uint64_t scanDmaRangeComponentValidationMaxUs = 0;
    uint64_t scanDmaRangeViewMatrixMaxUs = 0;
    bool cnNeEntityListRootCacheEnabled = false;
    uint64_t cnNeEntityListRootCacheTtlMs = 0;
    size_t cnNeEntityListRootCacheHitCount = 0;
    size_t cnNeEntityListRootCacheReadCount = 0;
    size_t cnNeEntityListRootCacheStoreCount = 0;
    size_t cnNeEntityListRootCacheExpiredCount = 0;
    size_t cnNeEntityListRootCacheStaleHitCount = 0;
    size_t listReadCount = 0;
    size_t listReadFailCount = 0;
    size_t listFallbackReadCount = 0;
    size_t listReadSkippedCount = 0;
    size_t cnNeEntityListChunkSize = 0;
    size_t recordAddAttemptCount = 0;
    size_t recordDuplicateCount = 0;
    size_t recordHeaderReadCount = 0;
    size_t recordHeaderFailCount = 0;
    size_t recordRemoteFallbackReadCount = 0;
    size_t recordMatchIdDirectReadCount = 0;
    size_t recordMatchIdDirectZeroCount = 0;
    size_t recordMatchIdHeaderHitCount = 0;
    size_t recordMatchIdHeaderMissCount = 0;
    size_t recordMatchIdHeaderMatchCount = 0;
    size_t recordMatchIdHeaderMismatchCount = 0;
    size_t recordMatchIdHeaderUseCount = 0;
    bool cnNeRecordMatchIdFromHeaderEnabled = false;
    bool cnNeRecordSnapshotCacheEnabled = false;
    uint64_t cnNeRecordSnapshotCacheTtlMs = 0;
    size_t cnNeRecordSnapshotCacheLookupCount = 0;
    size_t cnNeRecordSnapshotCacheHitCount = 0;
    size_t cnNeRecordSnapshotCacheStoreCount = 0;
    size_t cnNeRecordSnapshotCacheExpiredCount = 0;
    size_t cnNeRecordSnapshotCacheRefreshBudget = 0;
    size_t cnNeRecordSnapshotCacheRefreshCount = 0;
    size_t cnNeRecordSnapshotCacheStaleHitCount = 0;
    bool cnNeEntityListReadNegativeCacheEnabled = false;
    uint64_t cnNeEntityListReadNegativeCacheTtlMs = 0;
    size_t cnNeEntityListReadNegativeCacheLookupCount = 0;
    size_t cnNeEntityListReadNegativeCacheHitCount = 0;
    size_t cnNeEntityListReadNegativeCacheStoreCount = 0;
    size_t cnNeEntityListReadNegativeCacheExpiredCount = 0;
    size_t cnNeEntityListReadNegativeCacheStaleHitCount = 0;
    bool cnNeEntityListReadCacheEnabled = false;
    uint64_t cnNeEntityListReadCacheTtlMs = 0;
    size_t cnNeEntityListReadCacheLookupCount = 0;
    size_t cnNeEntityListReadCacheHitCount = 0;
    size_t cnNeEntityListReadCacheStoreCount = 0;
    size_t cnNeEntityListReadCacheExpiredCount = 0;
    size_t cnNeEntityListReadCacheStaleHitCount = 0;
    uint64_t cnNeScannerStaleMetadataMs = 0;
    bool cnNeScannerStaleMetadataOnlyEnabled = false;
    size_t poolIdReadCount = 0;
    size_t matchLookupCount = 0;
    size_t matchLookupHitCount = 0;
    size_t addPairAttemptCount = 0;
    size_t addPairDuplicateCount = 0;
    size_t linkDecryptAttemptCount = 0;
    size_t linkDecryptSuccessCount = 0;
    bool cnNeLinkDecryptNegativeCacheEnabled = false;
    uint64_t cnNeLinkDecryptNegativeCacheTtlMs = 0;
    size_t cnNeLinkDecryptNegativeCacheLookupCount = 0;
    size_t cnNeLinkDecryptNegativeCacheHitCount = 0;
    size_t cnNeLinkDecryptNegativeCacheStoreCount = 0;
    size_t cnNeLinkDecryptNegativeCacheExpiredCount = 0;
    size_t cnNeLinkDecryptNegativeCacheStaleHitCount = 0;
    size_t playableValidationAttemptCount = 0;
    size_t playableValidationSuccessCount = 0;
    size_t cnNeMapCandidateCount = 0;
    size_t cnNeTargetMapAttemptCount = 0;
    size_t cnNeTargetMapSuccessCount = 0;
    size_t cnNeBucketEntryScanCount = 0;
    bool cnNeMapCandidateCacheEnabled = false;
    size_t cnNeMapCandidateCacheLookupCount = 0;
    size_t cnNeMapCandidateCacheHitCount = 0;
    size_t cnNeMapCandidateCacheMissCount = 0;
    bool cnNeMapCandidatePersistentCacheEnabled = false;
    uint64_t cnNeMapCandidatePersistentCacheTtlMs = 0;
    size_t cnNeMapCandidatePersistentCacheLookupCount = 0;
    size_t cnNeMapCandidatePersistentCacheHitCount = 0;
    size_t cnNeMapCandidatePersistentCacheMissCount = 0;
    size_t cnNeMapCandidatePersistentCacheStoreCount = 0;
    size_t cnNeMapCandidatePersistentCacheExpiredCount = 0;
    size_t cnNeMapCandidatePersistentCacheRefreshBudget = 0;
    size_t cnNeMapCandidatePersistentCacheRefreshCount = 0;
    size_t cnNeMapCandidatePersistentCacheStaleHitCount = 0;
    bool cnNeComponentNegativeCacheEnabled = false;
    uint64_t cnNeComponentNegativeCacheTtlMs = 0;
    size_t cnNeComponentNegativeCacheLookupCount = 0;
    size_t cnNeComponentNegativeCacheHitCount = 0;
    size_t cnNeComponentNegativeCacheStoreCount = 0;
    size_t cnNeComponentNegativeCacheExpiredCount = 0;
    size_t cnNeComponentNegativeCacheRefreshBudget = 0;
    size_t cnNeComponentNegativeCacheRefreshCount = 0;
    size_t cnNeComponentNegativeCacheStaleHitCount = 0;
    bool cnNeMapDiagEnabled = false;
    size_t cnNeMapCandidateParentLookupCount = 0;
    size_t cnNeMapCandidateUniqueParentCount = 0;
    size_t cnNeMapCandidateDuplicateParentCount = 0;
    size_t cnNeMapCandidateDirectSourceCount = 0;
    size_t cnNeMapCandidatePlus8SourceCount = 0;
    size_t cnNeMapCandidateWrapperSourceCount = 0;
    size_t componentOnlyValidationAttemptCount = 0;
    size_t componentOnlyValidationSuccessCount = 0;
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
    bool lightScanRequested = false;
    bool lightScanEnabled = false;
    size_t lightScanPairCap = 0;
    size_t lightScanCapHits = 0;
    size_t lightScanUnvalidatedPairs = 0;
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

struct EntityPipelineScanStats {
    uint64_t loopCount = 0;
    uint64_t dueCount = 0;
    uint64_t skipPendingCount = 0;
    uint64_t skipNotDueCount = 0;
    uint64_t skipStableTopologyCount = 0;
    uint64_t startedCount = 0;
    uint64_t completedCount = 0;
    uint64_t failedCount = 0;
    uint64_t publishAttemptCount = 0;
    uint64_t publishSuccessCount = 0;
    uint64_t overwrittenCount = 0;
    uint64_t generation = 0;
    double loopHz = 0.0;
    double dueHz = 0.0;
    double startedHz = 0.0;
    double completedHz = 0.0;
    double getOwEntitiesMs = 0.0;
    double maxGetOwEntitiesMs = 0.0;
    uint64_t maxGetOwEntitiesGeneration = 0;
    size_t maxGetOwEntitiesRecords = 0;
    size_t maxGetOwEntitiesPairs = 0;
    double maxGetOwEntitiesRecordBuildMs = 0.0;
    double maxGetOwEntitiesMatchLinkMs = 0.0;
    double maxGetOwEntitiesTargetMapMs = 0.0;
    double maxGetOwEntitiesComponentValidationMs = 0.0;
    uint64_t maxGetOwEntitiesDmaReadsDelta = 0;
    uint64_t maxGetOwEntitiesDmaFailDelta = 0;
    bool maxGetOwEntitiesDmaRangeDiagEnabled = false;
    uint64_t maxGetOwEntitiesDmaRangeReads = 0;
    uint64_t maxGetOwEntitiesDmaRangeFailed = 0;
    uint64_t maxGetOwEntitiesDmaRangeMaxLatencyUs = 0;
    uint8_t maxGetOwEntitiesDmaRangeMaxCallsite = 0;
    uint64_t maxGetOwEntitiesDmaRangeScannerReads = 0;
    uint64_t maxGetOwEntitiesDmaRangeScannerMaxLatencyUs = 0;
    uint8_t maxGetOwEntitiesDmaRangeScannerMaxCallsite = 0;
    uint64_t maxGetOwEntitiesDmaRangeForeignReads = 0;
    uint64_t maxGetOwEntitiesDmaRangeForeignMaxLatencyUs = 0;
    uint8_t maxGetOwEntitiesDmaRangeForeignMaxCallsite = 0;
    uint64_t maxGetOwEntitiesDmaRangeRootMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeListReadMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeRecordHeaderMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeRecordPoolIdMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeTargetMapMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeComponentValidationMaxUs = 0;
    uint64_t maxGetOwEntitiesDmaRangeViewMatrixMaxUs = 0;
    size_t maxGetOwEntitiesRootCacheHitCount = 0;
    size_t maxGetOwEntitiesRootCacheReadCount = 0;
    size_t maxGetOwEntitiesRootCacheStoreCount = 0;
    size_t maxGetOwEntitiesRootCacheExpiredCount = 0;
    size_t maxGetOwEntitiesRootCacheStaleHitCount = 0;
    size_t maxGetOwEntitiesListReadSkippedCount = 0;
    size_t maxGetOwEntitiesListReadNegativeCacheHitCount = 0;
    size_t maxGetOwEntitiesListReadNegativeCacheStoreCount = 0;
    size_t maxGetOwEntitiesListReadNegativeCacheExpiredCount = 0;
    size_t maxGetOwEntitiesListReadNegativeCacheStaleHitCount = 0;
    size_t maxGetOwEntitiesListReadCacheHitCount = 0;
    size_t maxGetOwEntitiesListReadCacheStoreCount = 0;
    size_t maxGetOwEntitiesListReadCacheExpiredCount = 0;
    size_t maxGetOwEntitiesListReadCacheStaleHitCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdDirectReadCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdDirectZeroCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdHeaderHitCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdHeaderMissCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdHeaderMatchCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdHeaderMismatchCount = 0;
    size_t maxGetOwEntitiesRecordMatchIdHeaderUseCount = 0;
    size_t maxGetOwEntitiesPersistentRefreshCount = 0;
    size_t maxGetOwEntitiesPersistentStaleHitCount = 0;
    size_t resultRawCount = 0;
    uint64_t pendingAgeMs = 0;
    uint64_t lastSuccessAgeMs = 0;
    bool coldTopologyScanEnabled = false;
    uint64_t topologyRescanRequestCount = 0;
    uint64_t topologyCountProbeCount = 0;
    uint64_t topologyCountProbeChangeCount = 0;
    size_t topologyCandidateCount = 0;
};

struct EntityPipelinePhaseStats {
    double beginFrameMs = 0.0;
    double consumeScanMs = 0.0;
    double previousSnapshotCopyMs = 0.0;
    double prefetchMs = 0.0;
    double previousIndexMs = 0.0;
    double hotScatterPrepareMs = 0.0;
    double hotScatterExecuteMs = 0.0;
    double baseCacheMs = 0.0;
    double baseDecryptMs = 0.0;
    double healthMs = 0.0;
    double heroMs = 0.0;
    double visibilityMs = 0.0;
    double skeletonMs = 0.0;
    double skeletonVelocityReadMs = 0.0;
    double skeletonCacheCallMs = 0.0;
    double skillMs = 0.0;
    double teamNameMs = 0.0;
    double teamNameHeroLookupMs = 0.0;
    double teamNameBotAdjustMs = 0.0;
    double teamNameBattleTagMs = 0.0;
    double teamNameTeamReadMs = 0.0;
    double localSelectMs = 0.0;
    double publishMs = 0.0;
    double recordSyncMs = 0.0;
    double entityLoopWallMs = 0.0;
    double entityLoopSetupMs = 0.0;
    double entityHeaderSpecialMs = 0.0;
    double entityHeaderComponentMs = 0.0;
    double entityHeaderLinkMs = 0.0;
    double entitySpecialProbeMs = 0.0;
    double entityCacheApplyMs = 0.0;
    double entityCacheMatchIdMs = 0.0;
    double entityCacheRecordUpdateMs = 0.0;
    double entityHotFieldsMs = 0.0;
    double entityRotationPositionMs = 0.0;
    double entityLoopGapMs = 0.0;
    double cycleGapMs = 0.0;
};

struct EntityLifecycleStats {
    size_t entityRecordCreatedCount = 0;
    size_t entityRecordUpdatedActorCount = 0;
    size_t entityRecordLinkChangedCount = 0;
    size_t entityRecordLinkChangedSameComponentCount = 0;
    size_t entityRecordLinkChangedComponentChangedCount = 0;
    size_t entityRecordLinkChangedSameHeroCount = 0;
    size_t entityRecordLinkChangedHeroChangedCount = 0;
    size_t entityRecordLinkChangedHeroUnknownCount = 0;
    size_t entityRecordLinkChangedMatchKeyCount = 0;
    size_t entityRecordLinkChangedLinkKeyCount = 0;
    size_t entityRecordLinkChangedComponentKeyCount = 0;
    size_t entityRecordMarkMissingCount = 0;
    size_t entityRecordMarkDeadCount = 0;
    size_t entityRecordExpiredCount = 0;

    size_t componentCacheHitCount = 0;
    size_t componentCacheMissCount = 0;
    size_t componentCacheInvalidateIntervalCount = 0;
    size_t componentCacheInvalidateIntervalSkippedLifetimeCount = 0;
    size_t componentCacheInvalidateLinkChangeCount = 0;
    size_t componentCacheInvalidateHealthResurrectCount = 0;
    size_t componentCacheInvalidateHeroChangeCount = 0;
    size_t componentCacheLinkChangePreviousMatchIdKnownCount = 0;
    size_t componentCacheLinkChangePreviousMatchIdZeroCount = 0;
    size_t componentCacheLinkChangePreviousMatchIdUnknownCount = 0;
    size_t componentCacheLinkChangeRecordAliasHitCount = 0;
    size_t componentCacheLinkChangeRecordAliasMissCount = 0;
    size_t componentCacheLinkChangeRecordPublishedCount = 0;
    size_t componentCacheLinkChangeRecordBasesValidCount = 0;
    size_t componentCacheLinkChangeRecordMatchKeyCount = 0;
    size_t componentCacheLinkChangeRecordLinkKeyCount = 0;
    size_t componentCacheLinkChangeRecordComponentKeyCount = 0;
    size_t componentCacheLinkRetainAttemptCount = 0;
    size_t componentCacheLinkRetainSuccessCount = 0;
    size_t componentCacheLinkRetainRejectedDisabledCount = 0;
    size_t componentCacheLinkRetainRejectedRecordStoreDisabledCount = 0;
    size_t componentCacheLinkRetainRejectedMissingRecordCount = 0;
    size_t componentCacheLinkRetainRejectedMissingMatchIdCount = 0;
    size_t componentCacheLinkRetainRejectedComponentChangedCount = 0;
    size_t componentCacheLinkRetainRejectedIntervalCount = 0;
    size_t componentCacheLinkRetainIntervalBypassedLifetimeCount = 0;
    size_t componentCacheLinkRetainRejectedHeroChangedCount = 0;
    size_t componentCacheLinkRetainRejectedHeroUnknownCount = 0;
    size_t componentCacheLinkRetainRejectedDecryptFailCount = 0;
    size_t componentCacheLinkRetainCachedHeroValidateCount = 0;
    size_t componentCacheLinkRetainRefreshDecryptAttemptCount = 0;
    size_t componentCacheLinkRetainRefreshDecryptSuccessCount = 0;
    size_t componentCacheLinkRetainRefreshDecryptFailCount = 0;
    size_t componentCacheLinkRetainRefreshLinkAttemptCount = 0;
    size_t componentCacheLinkRetainRefreshLinkSuccessCount = 0;
    size_t componentCacheLinkRetainRefreshLinkFailCount = 0;
    size_t componentCacheLinkRetainRefreshHeroAttemptCount = 0;
    size_t componentCacheLinkRetainRefreshHeroSuccessCount = 0;
    size_t componentCacheLinkRetainRefreshHeroFailCount = 0;
    size_t componentCacheLinkRetainRefreshVisibilityAttemptCount = 0;
    size_t componentCacheLinkRetainRefreshVisibilitySuccessCount = 0;
    size_t componentCacheLinkRetainRefreshVisibilityFailCount = 0;
    size_t componentCacheLinkRetainRefreshAngleAttemptCount = 0;
    size_t componentCacheLinkRetainRefreshAngleSuccessCount = 0;
    size_t componentCacheLinkRetainRefreshAngleFailCount = 0;
    size_t componentCacheLinkRetainRefreshAngleSkippedNoPriorCount = 0;
    size_t componentCacheLinkRetainRefreshAnglePriorCount = 0;
    size_t componentCacheLinkRetainRefreshAnglePriorFailRejectedCount = 0;

    size_t dynamicCacheCreatedCount = 0;
    size_t dynamicCacheReusedCount = 0;
    size_t dynamicCacheReplacedCount = 0;
    size_t dynamicCacheExpiredCount = 0;

    bool recordStoreEnabled = false;
    size_t recordStoreSize = 0;
    size_t recordStoreFreshCount = 0;
    size_t recordStoreMissingCount = 0;
    size_t recordStoreDeadCount = 0;
    size_t recordStoreExpiredCount = 0;
    size_t recordStoreBasesValidCount = 0;
    size_t recordStoreDynamicValidCount = 0;
    size_t recordStorePublishedValidCount = 0;
};

struct SdkCacheStats {
    uint64_t componentKeyCacheHitCount = 0;
    uint64_t componentKeyCacheMissCount = 0;
    uint64_t beginFrameScanCount = 0;
    uint64_t beginFrameProcessCount = 0;
    uint64_t beginFrameUnknownCount = 0;
};

struct EntityPipelineProcessStats {
    double entityCycleMs = 0.0;
    size_t rawCount = 0;
    size_t validatedCount = 0;
    size_t publishedCount = 0;
    uint64_t dmaReadsDelta = 0;
    uint64_t dmaFailDelta = 0;
    EntityPipelinePhaseStats phase{};
    size_t baseCacheHitCount = 0;
    size_t baseCacheMissCount = 0;
    size_t baseDecryptAttemptCount = 0;
    size_t baseDecryptSuccessCount = 0;
    size_t baseDecryptFailCount = 0;
    size_t baseDecryptSlowCallCount = 0;
    size_t baseDecryptFallbackAttemptCount = 0;
    size_t baseDecryptFallbackSuccessCount = 0;
    size_t baseDecryptFallbackFailCount = 0;
    size_t baseDecryptUniqueKeyCount = 0;
    size_t baseDecryptDuplicateKeyCount = 0;
    size_t baseDecryptMaxDuplicateKeyCount = 0;
    uint32_t baseDecryptMaxDuplicateKeyType = 0;
    uint64_t baseDecryptMaxDuplicateKeyParent = 0;
    double baseDecryptMaxCallMs = 0.0;
    uint32_t baseDecryptMaxCallType = 0;
    uint64_t baseDecryptMaxCallParent = 0;
    bool baseDecryptMaxCallSuccess = false;
    size_t teamNameSlowCallCount = 0;
    double teamNameMaxCallMs = 0.0;
    uint32_t teamNameMaxCallOp = 0;
    uint64_t teamNameMaxCallHeroId = 0;
    uint64_t teamNameMaxCallLinkBase = 0;
    uint64_t teamNameMaxCallTeamBase = 0;
    bool teamNameMaxCallSuccess = false;
    size_t hotScatterRequestedCount = 0;
    size_t hotScatterPrepareRequestedCount = 0;
    size_t hotScatterPrepareSuccessCount = 0;
    size_t hotScatterPrepareFailCount = 0;
    size_t hotScatterExecuteCount = 0;
    size_t hotScatterExecuteFailCount = 0;
    size_t hotScatterBytesRequested = 0;
    size_t hotScatterBytesRead = 0;
    size_t hotScatterShortReadCount = 0;
    size_t hotScatterBatchItems = 0;
    size_t hotScatterBatchRequests = 0;
    size_t hotScatterEstimatedUniquePages = 0;
    size_t hotScatterSuccessCount = 0;
    size_t hotScatterPartialCount = 0;
    size_t hotScatterFallbackReadCount = 0;
    size_t skeletonCacheHitCount = 0;
    size_t skeletonCacheMissCount = 0;
    size_t skeletonExactBoneReadCount = 0;
    size_t skeletonBlockReadBytes = 0;
    size_t skeletonFallbackGetBonePosCount = 0;
    size_t skeletonSlowCallCount = 0;
    double skeletonMaxCallMs = 0.0;
    uint32_t skeletonMaxCallOp = 0;
    uint64_t skeletonMaxCallHeroId = 0;
    uint64_t skeletonMaxCallEntity = 0;
    uint64_t skeletonMaxCallBoneBase = 0;
    uint64_t skeletonMaxCallVelocityBase = 0;
    uint64_t skeletonMaxCallVelocityBoneData = 0;
    bool skeletonMaxCallCacheHit = false;
    bool skeletonMaxCallCacheValid = false;
    bool skeletonMaxCallFallback = false;
    int skeletonMaxCallMaxMappedIndex = -1;
    bool skeletonMaxCallSuccess = false;
    size_t visibilityScatterHitCount = 0;
    size_t visibilityFallbackCount = 0;
    size_t skillDueCount = 0;
    size_t skillReadCount = 0;
    size_t skillSkippedNotDueCount = 0;
    EntityLifecycleStats lifecycle{};
};

struct StatusSnapshot {
    size_t entityCount = 0;
    size_t lastScanEntityCount = 0;
    uint64_t entityScanCycles = 0;
    uint64_t entityProcessCycles = 0;
    double entityScanHz = 0.0;
    double entityProcessHz = 0.0;
    double fps = 0.0;
    PublishCadenceStats viewMatrixPublish{};
    PublishCadenceStats entityPublish{};
    SnapshotCopyStats entitySnapshotCopy{};
    SnapshotCopyStats dynamicSnapshotCopy{};
    ViewMatrixConsumerStats renderViewMatrixUse{};
    DmaReadStats dmaReads{};
    DmaWindowStats dmaWindow{};
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
    ViewMatrixStabilityStats viewMatrixStability{};
    ProjectionStabilityStats projectionStability{};
    OverlayCanvasStats overlayCanvas{};
    bool renderDrawRadarCalled = false;
    bool renderPlayerInfoCalled = false;
    bool renderSkillInfoCalled = false;
    bool renderEntityListEmpty = true;
    RenderWorkloadStats renderWorkload{};
    EntityProcessStats entityProcess{};
    PlayerInfoStats playerInfo{};
    LocalEntityStats localEntity{};
    RosterStats roster{};
    SdkCacheStats sdkCache{};
    EntityScanDetailStats entityScanDetail{};
    EntityPipelineScanStats entityPipelineScan{};
    EntityPipelineProcessStats entityPipelineProcess{};
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
size_t DmaSampleCursor();
DmaWindowStats GetDmaRangeStats(size_t beginCursor, size_t endCursor);
std::vector<DmaSlowReadSample> GetDmaSlowReadSamples(
    uint64_t windowMs,
    uint64_t minLatencyUs,
    size_t maxSamples);
void RecordDecryptFailure();
void RecordInvalidEntity();
void RecordSdkComponentKeyCacheHit();
void RecordSdkComponentKeyCacheMiss();
void RecordSdkBeginFrame(SdkFrameSource source);

// Frame timing — call once per frame from the render thread after Present().
// Emits a SLOW_FRAME diagnostic line when total time exceeds the threshold.
void RecordFrameTiming(const FrameTiming& timing, double slowThresholdMs = 16.6);

// Called once at startup to tag the render thread for per-thread DMA tracking.
void SetRenderThread();

// Returns aggregate DMA stats for reads completed in the last `windowMs`
// milliseconds.  Scans a lock-free ring buffer — O(capacity) but intended
// for diagnostic sampling, not per-frame use.
DmaWindowStats GetDmaWindowStats(uint64_t windowMs);
DmaReadStats SnapshotDmaReadStats();
void RecordEntityScanCycle(size_t entityCount, double measuredHz = -1.0);
void RecordEntityProcessCycle(double measuredHz);
void RecordViewMatrixPublish();
void RecordRenderViewMatrixUse();
void RecordEntityPublish(size_t count);
void RecordEntitySnapshotCopy(size_t count, std::chrono::steady_clock::duration elapsed);
void RecordDynamicSnapshotCopy(size_t count, std::chrono::steady_clock::duration elapsed);

void SetEntityCount(size_t entityCount);
void SetDmaReady(bool ready);
void SetProcessAttached(bool attached);
void SetKeyStatus(KeyStatus status, uint64_t key1 = 0, uint64_t key2 = 0);
void SetDmaProbeResult(bool attempted, bool succeeded, uint64_t address = 0, uint16_t magic = 0);
void SetViewMatrixStatus(bool resolved, bool valid);
void RecordViewMatrixStability(bool acceptedLargeJump, bool transientRejected, float elementDelta);
void RecordProjectionGlobalJump(size_t matchedCount, float medianDxPx, float medianDyPx, float medianDeltaPx);
void RecordOverlayCanvasBounds(int x, int y, uint32_t windowWidth, uint32_t windowHeight,
                               uint32_t clientWidth, uint32_t clientHeight,
                               bool visible, bool boundsChanged);
void RecordOverlayCanvasFrame(uint32_t swapchainWidth, uint32_t swapchainHeight,
                              float displayWidth, float displayHeight,
                              bool swapchainResized);
void SetRenderPipelineStatus(bool drawRadarCalled, bool playerInfoCalled, bool skillInfoCalled, bool entityListEmpty);
void SetEntityProcessStats(const EntityProcessStats& stats);
void SetPlayerInfoStats(const PlayerInfoStats& stats);
void SetLocalEntityStats(const LocalEntityStats& stats);
void SetRosterStats(const RosterStats& stats);
void SetEntityScanDetailStats(const EntityScanDetailStats& stats);
EntityScanDetailStats SnapshotEntityScanDetailStats();
void SetEntityPipelineScanStats(const EntityPipelineScanStats& stats);
void SetEntityPipelineProcessStats(const EntityPipelineProcessStats& stats);

StatusSnapshot Snapshot();
void DumpStatus();

const char* ToString(LogLevel level);
const char* ToString(KeyStatus status);

} // namespace Diagnostics
