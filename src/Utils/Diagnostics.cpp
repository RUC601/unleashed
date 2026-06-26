#include "Utils/Diagnostics.hpp"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace Diagnostics {
namespace {

} // anonymous namespace

// ---- Callsite tag stack (RAII, thread-local) ----

thread_local DmaCallsite tls_dmaCallsite{ DmaCallsite::Unknown };

ScopedDmaCallsite::ScopedDmaCallsite(DmaCallsite cs) {
    m_previous = tls_dmaCallsite;
    tls_dmaCallsite = cs;
}

ScopedDmaCallsite::~ScopedDmaCallsite() {
    tls_dmaCallsite = m_previous;
}

DmaCallsite ScopedDmaCallsite::Current() {
    return tls_dmaCallsite;
}

namespace {
    thread_local DmaCallsite tls_manualStack[8]{};
    thread_local int tls_manualDepth = 0;
}

void ScopedDmaCallsite::Push(DmaCallsite cs) {
    if (tls_manualDepth < 8) {
        tls_manualStack[tls_manualDepth++] = tls_dmaCallsite;
        tls_dmaCallsite = cs;
    }
}

void ScopedDmaCallsite::Pop() {
    if (tls_manualDepth > 0) {
        tls_dmaCallsite = tls_manualStack[--tls_manualDepth];
    }
}

// ---- Lightweight ring buffer for DMA read samples ----

namespace {

struct DmaSample {
    uint64_t timestampMs;
    uint64_t latencyUs;
    DmaCallsite callsite;
    bool success;
};

constexpr size_t kDmaRingCapacity = 8192;
std::atomic<size_t> g_dmaRingWrite{ 0 };
DmaSample g_dmaRing[kDmaRingCapacity]{};

std::atomic<uint64_t> g_renderThreadId{ 0 };
std::atomic<uint64_t> g_frameDmaReads{ 0 };
std::atomic<uint64_t> g_frameDmaFailures{ 0 };
std::atomic<uint64_t> g_frameDmaTotalUs{ 0 };
std::atomic<uint64_t> g_frameDmaMaxUs{ 0 };

constexpr uint64_t kSlowFrameLogIntervalMs = 1000;
uint64_t g_slowFrameLastLogMs = 0;
uint64_t g_slowFrameWindowCount = 0;
double g_slowFrameWindowMaxTotalMs = 0.0;
double g_slowFrameWindowMaxRenderMs = 0.0;
double g_slowFrameWindowMaxPresentMs = 0.0;
uint64_t g_slowFrameWindowMaxRtDmaReads = 0;
uint64_t g_slowFrameWindowMaxRtDmaUs = 0;

} // anonymous namespace

// ---- Atomic helpers ----

void UpdateAtomicMin(std::atomic<uint64_t>& target, uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (value < current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void UpdateAtomicMax(std::atomic<uint64_t>& target, uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

namespace {

void RecordDmaSample(DmaCallsite callsite, bool success, uint64_t latencyUs) {
    const size_t idx = g_dmaRingWrite.fetch_add(1, std::memory_order_relaxed) % kDmaRingCapacity;
    g_dmaRing[idx].timestampMs = GetTickCount64();
    g_dmaRing[idx].latencyUs = latencyUs;
    g_dmaRing[idx].callsite = callsite;
    g_dmaRing[idx].success = success;

    const uint64_t renderTid = g_renderThreadId.load(std::memory_order_relaxed);
    if (renderTid != 0) {
        if (GetCurrentThreadId() == static_cast<DWORD>(renderTid)) {
            g_frameDmaReads.fetch_add(1, std::memory_order_relaxed);
            if (!success)
                g_frameDmaFailures.fetch_add(1, std::memory_order_relaxed);
            g_frameDmaTotalUs.fetch_add(latencyUs, std::memory_order_relaxed);
            UpdateAtomicMax(g_frameDmaMaxUs, latencyUs);
        }
    }
}

} // anonymous namespace

namespace {

void ResetFrameDmaCounters() {
    g_frameDmaReads.store(0, std::memory_order_relaxed);
    g_frameDmaFailures.store(0, std::memory_order_relaxed);
    g_frameDmaTotalUs.store(0, std::memory_order_relaxed);
    g_frameDmaMaxUs.store(0, std::memory_order_relaxed);
}

std::atomic<bool> g_initialized{ false };
std::atomic<int> g_minLevel{ static_cast<int>(LogLevel::Info) };

std::mutex g_logMutex;
std::ofstream g_logFile;
std::string g_logPath = "./unleashed_diag.log";

std::atomic<bool> g_aimLogInitialized{ false };
std::mutex g_aimLogMutex;
std::ofstream g_aimLogFile;
std::string g_aimLogPath = "./unleashed_aim_diag.log";
uint64_t g_aimLogLastFlushMs = 0;

std::mutex g_ringLogMutex;
std::deque<std::string> g_ringLogLines;
size_t g_ringLogCapacity = DefaultLogLineCapacity;
std::atomic<bool> g_logOverlayVisible{ false };

std::atomic<uint64_t> g_dmaReadSucceeded{ 0 };
std::atomic<uint64_t> g_dmaReadFailed{ 0 };
std::atomic<uint64_t> g_dmaReadLatencyTotalUs{ 0 };
std::atomic<uint64_t> g_dmaReadLatencyMinUs{ (std::numeric_limits<uint64_t>::max)() };
std::atomic<uint64_t> g_dmaReadLatencyMaxUs{ 0 };

std::atomic<uint64_t> g_decryptFailures{ 0 };
std::atomic<uint64_t> g_invalidEntities{ 0 };

std::atomic<uint64_t> g_entityCount{ 0 };
std::atomic<uint64_t> g_lastScanEntityCount{ 0 };
std::atomic<uint64_t> g_entityScanCycles{ 0 };
std::atomic<uint64_t> g_entityProcessCycles{ 0 };
std::atomic<uint64_t> g_entityScanHzMilli{ 0 };
std::atomic<uint64_t> g_entityProcessHzMilli{ 0 };
std::atomic<uint64_t> g_viewMatrixPublishCycles{ 0 };
std::atomic<uint64_t> g_viewMatrixPublishHzMilli{ 0 };
std::atomic<uint64_t> g_viewMatrixPublishLastTickMs{ 0 };
std::atomic<uint64_t> g_viewMatrixPublishLastIntervalMs{ 0 };
std::atomic<uint64_t> g_viewMatrixPublishMaxIntervalMs{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseCount{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseLastAgeMs{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseMaxAgeMs{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseMissingPublish{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseOver16Ms{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseOver33Ms{ 0 };
std::atomic<uint64_t> g_renderViewMatrixUseOver50Ms{ 0 };
std::atomic<uint64_t> g_entityPublishCycles{ 0 };
std::atomic<uint64_t> g_entityPublishHzMilli{ 0 };
std::atomic<uint64_t> g_entityPublishLastTickMs{ 0 };
std::atomic<uint64_t> g_entityPublishLastIntervalMs{ 0 };
std::atomic<uint64_t> g_entityPublishMaxIntervalMs{ 0 };
std::atomic<uint64_t> g_entityPublishLastCount{ 0 };
std::atomic<uint64_t> g_entitySnapshotCopyCount{ 0 };
std::atomic<uint64_t> g_entitySnapshotCopyLastCount{ 0 };
std::atomic<uint64_t> g_entitySnapshotCopyLastUs{ 0 };
std::atomic<uint64_t> g_entitySnapshotCopyMaxUs{ 0 };
std::atomic<uint64_t> g_dynamicSnapshotCopyCount{ 0 };
std::atomic<uint64_t> g_dynamicSnapshotCopyLastCount{ 0 };
std::atomic<uint64_t> g_dynamicSnapshotCopyLastUs{ 0 };
std::atomic<uint64_t> g_dynamicSnapshotCopyMaxUs{ 0 };
std::atomic<uint64_t> g_rosterFresh{ 0 };
std::atomic<uint64_t> g_rosterDead{ 0 };
std::atomic<uint64_t> g_rosterMissing{ 0 };
std::atomic<uint64_t> g_rosterExpired{ 0 };
std::atomic<uint64_t> g_rosterHeroChanged{ 0 };
std::atomic<uint64_t> g_framesRendered{ 0 };

std::atomic<bool> g_dmaReady{ false };
std::atomic<bool> g_processAttached{ false };
std::atomic<int> g_keyStatus{ static_cast<int>(KeyStatus::Unknown) };
std::atomic<uint64_t> g_globalKey1{ 0 };
std::atomic<uint64_t> g_globalKey2{ 0 };
std::atomic<bool> g_dmaProbeAttempted{ false };
std::atomic<bool> g_dmaProbeSucceeded{ false };
std::atomic<uint64_t> g_dmaProbeAddress{ 0 };
std::atomic<uint32_t> g_dmaProbeMagic{ 0 };
std::atomic<bool> g_viewMatrixResolved{ false };
std::atomic<bool> g_viewMatrixValid{ false };
std::atomic<uint64_t> g_viewMatrixRejected{ 0 };
std::atomic<uint64_t> g_viewMatrixTransientRejected{ 0 };
std::atomic<uint64_t> g_viewMatrixAcceptedLargeJump{ 0 };
std::atomic<uint64_t> g_viewMatrixLastRejectTickMs{ 0 };
std::atomic<uint64_t> g_viewMatrixLastRejectDeltaMilli{ 0 };
std::atomic<uint64_t> g_viewMatrixMaxRejectDeltaMilli{ 0 };
std::atomic<uint64_t> g_viewMatrixLastAcceptedJumpDeltaMilli{ 0 };
std::atomic<uint64_t> g_projectionGlobalJumpFrames{ 0 };
std::atomic<uint64_t> g_projectionLastGlobalJumpTickMs{ 0 };
std::atomic<uint64_t> g_projectionLastGlobalJumpMatched{ 0 };
std::atomic<int64_t> g_projectionLastGlobalJumpMedianDxPx{ 0 };
std::atomic<int64_t> g_projectionLastGlobalJumpMedianDyPx{ 0 };
std::atomic<uint64_t> g_projectionLastGlobalJumpDeltaPx{ 0 };
std::atomic<uint64_t> g_projectionMaxGlobalJumpDeltaPx{ 0 };
std::atomic<int> g_overlayCanvasX{ 0 };
std::atomic<int> g_overlayCanvasY{ 0 };
std::atomic<uint64_t> g_overlayCanvasWindowWidth{ 0 };
std::atomic<uint64_t> g_overlayCanvasWindowHeight{ 0 };
std::atomic<uint64_t> g_overlayCanvasClientWidth{ 0 };
std::atomic<uint64_t> g_overlayCanvasClientHeight{ 0 };
std::atomic<uint64_t> g_overlayCanvasSwapchainWidth{ 0 };
std::atomic<uint64_t> g_overlayCanvasSwapchainHeight{ 0 };
std::atomic<int> g_overlayCanvasDisplayWidth{ 0 };
std::atomic<int> g_overlayCanvasDisplayHeight{ 0 };
std::atomic<bool> g_overlayCanvasVisible{ false };
std::atomic<uint64_t> g_overlayCanvasBoundsChanges{ 0 };
std::atomic<uint64_t> g_overlayCanvasSwapchainResizes{ 0 };
std::atomic<uint64_t> g_overlayCanvasLastBoundsChangeTickMs{ 0 };
std::atomic<uint64_t> g_overlayCanvasLastSwapchainResizeTickMs{ 0 };
std::atomic<bool> g_renderDrawRadarCalled{ false };
std::atomic<bool> g_renderPlayerInfoCalled{ false };
std::atomic<bool> g_renderSkillInfoCalled{ false };
std::atomic<bool> g_renderEntityListEmpty{ true };
std::atomic<uint64_t> g_entityProcessRaw{ 0 };
std::atomic<uint64_t> g_entityProcessValidated{ 0 };
std::atomic<uint64_t> g_entityProcessDynamic{ 0 };
std::atomic<uint64_t> g_entityProcessNullPair{ 0 };
std::atomic<uint64_t> g_entityProcessDuplicate{ 0 };
std::atomic<uint64_t> g_entityProcessHealthBaseFail{ 0 };
std::atomic<uint64_t> g_entityProcessHealthBaseMissing{ 0 };
std::atomic<uint64_t> g_entityProcessHealthReadFail{ 0 };
std::atomic<uint64_t> g_entityProcessLinkBaseFail{ 0 };
std::atomic<uint64_t> g_entityProcessHeroBaseMissing{ 0 };
std::atomic<uint64_t> g_entityProcessHeroFallbackFail{ 0 };
std::atomic<uint64_t> g_entityProcessNameUnknown{ 0 };
std::atomic<uint64_t> g_entityProcessBoneCandidates{ 0 };
std::atomic<uint64_t> g_entityProcessBoneBaseNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessVelocityBoneDataNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessBoneDataPtrNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessBonesBaseNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessVelocityBoneIdTableNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessVelocityBoneCountValid{ 0 };
std::atomic<uint64_t> g_entityProcessVelocityBoneIdTableReadable{ 0 };
std::atomic<uint64_t> g_entityProcessVelocityBoneHeadIdFound{ 0 };
std::atomic<uint64_t> g_entityProcessSkeletonAnyValid{ 0 };
std::atomic<uint64_t> g_entityProcessSkeletonHeadValid{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeCandidates{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeResolved{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeIdFound{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeLocalFinite{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeLocalNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeWorldNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeExceptions{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeNearCandidates{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeNearWorldNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeFarCandidates{ 0 };
std::atomic<uint64_t> g_entityProcessHeadProbeFarWorldNonZero{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneAddress{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailComponentParent{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailLinkParent{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailHealthBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailLinkBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailVelocityBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailHeroBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailTeamBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleHealthFailBoneBase{ 0 };
std::atomic<int> g_entityProcessSampleHealthFailReadOk{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownComponentParent{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownLinkParent{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownComponentMatchId{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownLinkMatchId{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownHeroBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownHeroId{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownHeroIdCandidate{ 0 };
std::atomic<int> g_entityProcessSampleNameUnknownHeroIdCandidateOffset{ -1 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownComponentHeroBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownComponentHeroId{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownComponentHeroIdCandidate{ 0 };
std::atomic<int> g_entityProcessSampleNameUnknownComponentHeroIdCandidateOffset{ -1 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownLinkBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownSkillBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownTeamBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleNameUnknownBoneBase{ 0 };
std::atomic<int> g_entityProcessSampleNameUnknownKind{ 0 };
std::atomic<uint64_t> g_entityProcessSampleVelocityBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleVelocityBoneData{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneDataPtr{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBonesBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneIdTable{ 0 };
std::atomic<int> g_entityProcessSampleBoneCount{ 0 };
std::atomic<int> g_entityProcessSampleBoneIdTableReadable{ 0 };
std::atomic<int> g_entityProcessSampleBoneHeadIndex{ -1 };
std::atomic<bool> g_playerInfoBoxPerfMode{ false };
std::atomic<bool> g_playerInfoFastBoxPath{ false };
std::atomic<uint64_t> g_playerInfoElapsedUs{ 0 };
std::atomic<uint64_t> g_playerInfoInput{ 0 };
std::atomic<uint64_t> g_playerInfoProjected{ 0 };
std::atomic<uint64_t> g_playerInfoDrawn{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedDead{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedLocalHealth{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedLocalEntity{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedDistance{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedOpacity{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedWorldToScreen{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedWorldToScreenLow{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedWorldToScreenHigh{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedBox{ 0 };
std::atomic<uint64_t> g_playerInfoSkippedWindow{ 0 };
std::atomic<bool> g_playerInfoSampleProjected{ false };
std::atomic<bool> g_playerInfoSampleDrawn{ false };
std::atomic<uint64_t> g_playerInfoSampleProjectedAddress{ 0 };
std::atomic<uint64_t> g_playerInfoSampleProjectedHeroId{ 0 };
std::atomic<int> g_playerInfoSampleProjectedLeft{ 0 };
std::atomic<int> g_playerInfoSampleProjectedTop{ 0 };
std::atomic<int> g_playerInfoSampleProjectedWidth{ 0 };
std::atomic<int> g_playerInfoSampleProjectedHeight{ 0 };
std::atomic<int> g_playerInfoSampleProjectedCenterX{ 0 };
std::atomic<int> g_playerInfoSampleProjectedBottom{ 0 };
std::atomic<int> g_playerInfoSampleProjectedDistanceM{ 0 };
std::atomic<uint64_t> g_playerInfoSampleDrawnAddress{ 0 };
std::atomic<uint64_t> g_playerInfoSampleDrawnHeroId{ 0 };
std::atomic<int> g_playerInfoSampleDrawnLeft{ 0 };
std::atomic<int> g_playerInfoSampleDrawnTop{ 0 };
std::atomic<int> g_playerInfoSampleDrawnWidth{ 0 };
std::atomic<int> g_playerInfoSampleDrawnHeight{ 0 };
std::atomic<int> g_playerInfoSampleDrawnCenterX{ 0 };
std::atomic<int> g_playerInfoSampleDrawnBottom{ 0 };
std::atomic<int> g_playerInfoSampleDrawnDistanceM{ 0 };
std::atomic<uint64_t> g_playerInfoTrainingBotPredictionCandidates{ 0 };
std::atomic<uint64_t> g_playerInfoTrainingBotPredictionApplied{ 0 };
std::atomic<uint64_t> g_playerInfoTrainingBotPredictionLeadDrops{ 0 };
std::atomic<int> g_playerInfoTrainingBotPredictionMaxLeadMs{ 0 };
std::atomic<int> g_playerInfoTrainingBotPredictionMaxOffsetCm{ 0 };
std::atomic<uint64_t> g_playerInfoTrainingBotPredictionLastDropAddress{ 0 };
std::atomic<int> g_playerInfoTrainingBotPredictionLastDropFromMs{ 0 };
std::atomic<int> g_playerInfoTrainingBotPredictionLastDropToMs{ 0 };
std::atomic<int> g_playerInfoTrainingBotPredictionLastDropOffsetCm{ 0 };
std::atomic<uint64_t> g_localAngleCandidates{ 0 };
std::atomic<uint64_t> g_localNearCameraCandidates{ 0 };
std::atomic<uint64_t> g_localNamedCandidates{ 0 };
std::atomic<uint64_t> g_localSelected{ 0 };
std::atomic<uint64_t> g_localZeroHeadCandidates{ 0 };
std::atomic<uint64_t> g_localNonZeroPositionCandidates{ 0 };
std::atomic<uint64_t> g_localSelectedAddress{ 0 };
std::atomic<uint64_t> g_localSelectedHeroId{ 0 };
std::atomic<uint64_t> g_localSelectedAngleBase{ 0 };
std::atomic<int> g_localSelectedHealth{ 0 };
std::atomic<int> g_localBestDistanceCm{ -1 };
std::atomic<uint64_t> g_localBestAddress{ 0 };
std::atomic<uint64_t> g_localBestHeroId{ 0 };
std::atomic<uint64_t> g_localBestAngleBase{ 0 };
std::atomic<int> g_localBestHealth{ 0 };
std::atomic<int> g_localBestHeadXCm{ 0 };
std::atomic<int> g_localBestHeadYCm{ 0 };
std::atomic<int> g_localBestHeadZCm{ 0 };
std::atomic<int> g_localBestPosXCm{ 0 };
std::atomic<int> g_localBestPosYCm{ 0 };
std::atomic<int> g_localBestPosZCm{ 0 };
std::atomic<int> g_localCameraXCm{ 0 };
std::atomic<int> g_localCameraYCm{ 0 };
std::atomic<int> g_localCameraZCm{ 0 };

std::mutex g_entityScanDetailMutex;
EntityScanDetailStats g_entityScanDetail{};

std::mutex g_fpsMutex;
std::chrono::steady_clock::time_point g_lastFpsTime = std::chrono::steady_clock::now();
uint64_t g_lastFpsFrameCount = 0;
double g_lastFps = 0.0;

std::atomic<bool> g_renderWorkloadBoxPerfMode{ false };
std::atomic<uint64_t> g_renderWorkloadLinePrimitives{ 0 };
std::atomic<uint64_t> g_renderWorkloadRectPrimitives{ 0 };
std::atomic<uint64_t> g_renderWorkloadFilledRectPrimitives{ 0 };
std::atomic<uint64_t> g_renderWorkloadTextCalls{ 0 };
std::atomic<uint64_t> g_renderWorkloadIconCalls{ 0 };
std::atomic<uint64_t> g_renderWorkloadCornerBoxes{ 0 };
std::atomic<uint64_t> g_renderWorkloadFastBoxes{ 0 };
std::mutex g_renderWorkloadMutex;
RenderWorkloadStats g_lastRenderWorkload{};

bool ShouldLog(LogLevel level)
{
    return static_cast<int>(level) <= g_minLevel.load(std::memory_order_relaxed);
}

std::string BuildTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

    std::tm localTime{};
    localtime_s(&localTime, &nowTime);

    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &localTime);

    char stamped[80] = {};
    std::snprintf(stamped, sizeof(stamped), "%s.%03lld",
        buffer, static_cast<long long>(ms.count()));
    return stamped;
}

void AppendRingLogLine(const char* line)
{
    if (!line || !*line)
        return;

    std::lock_guard<std::mutex> lock(g_ringLogMutex);
    while (g_ringLogLines.size() >= g_ringLogCapacity)
        g_ringLogLines.pop_front();
    g_ringLogLines.emplace_back(line);
}

void LogV(LogLevel level, const char* fmt, va_list args)
{
    if (!ShouldLog(level))
        return;

    std::array<char, 2048> message{};
    std::vsnprintf(message.data(), message.size(), fmt, args);

    char line[2300] = {};
    std::snprintf(line, sizeof(line), "[%s] [%s] %s",
        BuildTimestamp().c_str(), ToString(level), message.data());

    AppendRingLogLine(line);

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::printf("%s\n", line);

    if (g_logFile.is_open()) {
        g_logFile << line << '\n';
        if (level == LogLevel::Error || level == LogLevel::Warn)
            g_logFile.flush();
    }
}

void AimLogV(const char* fmt, va_list args)
{
    std::array<char, 2048> message{};
    std::vsnprintf(message.data(), message.size(), fmt, args);

    char line[2300] = {};
    std::snprintf(line, sizeof(line), "[%s] [AIM] %s",
        BuildTimestamp().c_str(), message.data());

    AppendRingLogLine(line);

    std::lock_guard<std::mutex> lock(g_aimLogMutex);
    if (!g_aimLogInitialized.load(std::memory_order_acquire)) {
        g_aimLogFile.open(g_aimLogPath, std::ios::out | std::ios::app);
        g_aimLogInitialized.store(true, std::memory_order_release);
    }

    if (g_aimLogFile.is_open()) {
        g_aimLogFile << line << '\n';
        const uint64_t nowMs = GetTickCount64();
        if (g_aimLogLastFlushMs == 0 || nowMs - g_aimLogLastFlushMs >= 250) {
            g_aimLogFile.flush();
            g_aimLogLastFlushMs = nowMs;
        }
    }
}

double UpdateFps()
{
    constexpr double kMinFpsSampleSeconds = 0.25;
    const auto now = std::chrono::steady_clock::now();
    const uint64_t frames = g_framesRendered.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(g_fpsMutex);
    const std::chrono::duration<double> elapsed = now - g_lastFpsTime;
    if (elapsed.count() >= kMinFpsSampleSeconds) {
        const uint64_t deltaFrames = frames - g_lastFpsFrameCount;
        g_lastFps = static_cast<double>(deltaFrames) / elapsed.count();
        g_lastFpsFrameCount = frames;
        g_lastFpsTime = now;
    }
    return g_lastFps;
}

void RecordPublishCadence(std::atomic<uint64_t>& cycles,
                          std::atomic<uint64_t>& hzMilli,
                          std::atomic<uint64_t>& lastTickMs,
                          std::atomic<uint64_t>& lastIntervalMs,
                          std::atomic<uint64_t>& maxIntervalMs,
                          size_t count,
                          std::atomic<uint64_t>* lastCount)
{
    const uint64_t nowMs = GetTickCount64();
    const uint64_t previousMs = lastTickMs.exchange(nowMs, std::memory_order_relaxed);
    cycles.fetch_add(1, std::memory_order_relaxed);
    if (lastCount)
        lastCount->store(static_cast<uint64_t>(count), std::memory_order_relaxed);

    if (previousMs == 0 || nowMs <= previousMs)
        return;

    const uint64_t intervalMs = nowMs - previousMs;
    lastIntervalMs.store(intervalMs, std::memory_order_relaxed);
    UpdateAtomicMax(maxIntervalMs, intervalMs);
    hzMilli.store(intervalMs > 0 ? (1000000ULL / intervalMs) : 0ULL,
                  std::memory_order_relaxed);
}

void RecordSnapshotCopy(std::atomic<uint64_t>& copies,
                        std::atomic<uint64_t>& lastCount,
                        std::atomic<uint64_t>& lastUs,
                        std::atomic<uint64_t>& maxUs,
                        size_t count,
                        std::chrono::steady_clock::duration elapsed)
{
    const auto measuredUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    const uint64_t elapsedUs = measuredUs > 0 ? static_cast<uint64_t>(measuredUs) : 0ULL;
    copies.fetch_add(1, std::memory_order_relaxed);
    lastCount.store(static_cast<uint64_t>(count), std::memory_order_relaxed);
    lastUs.store(elapsedUs, std::memory_order_relaxed);
    UpdateAtomicMax(maxUs, elapsedUs);
}

PublishCadenceStats BuildPublishStats(const std::atomic<uint64_t>& cycles,
                                      const std::atomic<uint64_t>& hzMilli,
                                      const std::atomic<uint64_t>& lastTickMs,
                                      const std::atomic<uint64_t>& lastIntervalMs,
                                      const std::atomic<uint64_t>& maxIntervalMs,
                                      const std::atomic<uint64_t>* lastCount)
{
    PublishCadenceStats stats{};
    stats.cycles = cycles.load(std::memory_order_relaxed);
    stats.hz = static_cast<double>(hzMilli.load(std::memory_order_relaxed)) / 1000.0;
    const uint64_t lastTick = lastTickMs.load(std::memory_order_relaxed);
    stats.ageMs = lastTick == 0 ? 0 : GetTickCount64() - lastTick;
    stats.lastIntervalMs = lastIntervalMs.load(std::memory_order_relaxed);
    stats.maxIntervalMs = maxIntervalMs.load(std::memory_order_relaxed);
    if (lastCount)
        stats.lastCount = static_cast<size_t>(lastCount->load(std::memory_order_relaxed));
    return stats;
}

SnapshotCopyStats BuildSnapshotCopyStats(const std::atomic<uint64_t>& copies,
                                         const std::atomic<uint64_t>& lastCount,
                                         const std::atomic<uint64_t>& lastUs,
                                         const std::atomic<uint64_t>& maxUs)
{
    SnapshotCopyStats stats{};
    stats.copies = copies.load(std::memory_order_relaxed);
    stats.lastCount = static_cast<size_t>(lastCount.load(std::memory_order_relaxed));
    stats.lastMs = static_cast<double>(lastUs.load(std::memory_order_relaxed)) / 1000.0;
    stats.maxMs = static_cast<double>(maxUs.load(std::memory_order_relaxed)) / 1000.0;
    return stats;
}

ViewMatrixConsumerStats BuildViewMatrixConsumerStats()
{
    ViewMatrixConsumerStats stats{};
    stats.uses = g_renderViewMatrixUseCount.load(std::memory_order_relaxed);
    stats.lastAgeMs = g_renderViewMatrixUseLastAgeMs.load(std::memory_order_relaxed);
    stats.maxAgeMs = g_renderViewMatrixUseMaxAgeMs.load(std::memory_order_relaxed);
    stats.missingPublishUses =
        g_renderViewMatrixUseMissingPublish.load(std::memory_order_relaxed);
    stats.over16Ms = g_renderViewMatrixUseOver16Ms.load(std::memory_order_relaxed);
    stats.over33Ms = g_renderViewMatrixUseOver33Ms.load(std::memory_order_relaxed);
    stats.over50Ms = g_renderViewMatrixUseOver50Ms.load(std::memory_order_relaxed);
    return stats;
}

} // namespace

const char* ToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn:  return "WARN";
    case LogLevel::Info:  return "INFO";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    default:              return "UNKNOWN";
    }
}

const char* ToString(DmaCallsite cs)
{
    switch (cs) {
    case DmaCallsite::EntityScan:   return "EntityScan";
    case DmaCallsite::EntityDecrypt:return "EntityDecrypt";
    case DmaCallsite::ViewMatrix:   return "ViewMatrix";
    case DmaCallsite::BoneChain:    return "BoneChain";
    case DmaCallsite::KeyState:     return "KeyState";
    case DmaCallsite::Aimbot:       return "Aimbot";
    case DmaCallsite::RenderCanvas: return "RenderCanvas";
    case DmaCallsite::EntityPrefetch: return "EntityPrefetch";
    default:                        return "Unknown";
    }
}

const char* ToString(KeyStatus status)
{
    switch (status) {
    case KeyStatus::Unknown:   return "unknown";
    case KeyStatus::Skipped:   return "skipped";
    case KeyStatus::Resolving: return "resolving";
    case KeyStatus::Resolved:  return "resolved";
    case KeyStatus::Failed:    return "failed";
    default:                   return "unknown";
    }
}

void Initialize(LogLevel minLevel, const char* logPath)
{
    SetLogLevel(minLevel);
    if (logPath && *logPath)
        g_logPath = logPath;

    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (g_logFile.is_open())
            g_logFile.close();
        g_logFile.open(g_logPath, std::ios::out | std::ios::trunc);
    }

    g_lastFpsTime = std::chrono::steady_clock::now();
    g_lastFpsFrameCount = g_framesRendered.load(std::memory_order_relaxed);
    g_lastFps = 0.0;
    g_initialized.store(true, std::memory_order_release);

    Info("Diagnostics initialized. log=%s level=%s", g_logPath.c_str(), ToString(minLevel));
    if (!g_logFile.is_open())
        Warn("Diagnostics file logging is unavailable for %s", g_logPath.c_str());
}

void Shutdown()
{
    if (!g_initialized.load(std::memory_order_acquire))
        return;

    Info("Diagnostics shutting down.");
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile.flush();
        g_logFile.close();
    }
    g_initialized.store(false, std::memory_order_release);
}

bool IsInitialized()
{
    return g_initialized.load(std::memory_order_acquire);
}

void InitializeAimLog(const char* logPath)
{
    std::lock_guard<std::mutex> lock(g_aimLogMutex);
    if (logPath && *logPath)
        g_aimLogPath = logPath;

    if (g_aimLogFile.is_open())
        g_aimLogFile.close();

    g_aimLogFile.open(g_aimLogPath, std::ios::out | std::ios::trunc);
    g_aimLogInitialized.store(true, std::memory_order_release);
    g_aimLogLastFlushMs = GetTickCount64();

    if (g_aimLogFile.is_open()) {
        g_aimLogFile << "[" << BuildTimestamp() << "] [AIM] Aim diagnostics initialized. log="
            << g_aimLogPath << '\n';
        g_aimLogFile.flush();
    }
}

void ShutdownAimLog()
{
    std::lock_guard<std::mutex> lock(g_aimLogMutex);
    if (g_aimLogFile.is_open()) {
        g_aimLogFile << "[" << BuildTimestamp() << "] [AIM] Aim diagnostics shutting down.\n";
        g_aimLogFile.flush();
        g_aimLogFile.close();
    }
    g_aimLogInitialized.store(false, std::memory_order_release);
}

void Aim(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    AimLogV(fmt, args);
    va_end(args);
}

void SetLogLineCapacity(size_t maxLines)
{
    if (maxLines == 0)
        maxLines = 1;

    std::lock_guard<std::mutex> lock(g_ringLogMutex);
    g_ringLogCapacity = maxLines;
    while (g_ringLogLines.size() > g_ringLogCapacity)
        g_ringLogLines.pop_front();
}

size_t GetLogLineCapacity()
{
    std::lock_guard<std::mutex> lock(g_ringLogMutex);
    return g_ringLogCapacity;
}

std::vector<std::string> GetLogLines()
{
    std::lock_guard<std::mutex> lock(g_ringLogMutex);
    return std::vector<std::string>(g_ringLogLines.begin(), g_ringLogLines.end());
}

void ClearLogLines()
{
    std::lock_guard<std::mutex> lock(g_ringLogMutex);
    g_ringLogLines.clear();
}

bool IsLogOverlayVisible()
{
    return g_logOverlayVisible.load(std::memory_order_acquire);
}

void SetLogOverlayVisible(bool visible)
{
    g_logOverlayVisible.store(visible, std::memory_order_release);
}

void SetLogLevel(LogLevel minLevel)
{
    g_minLevel.store(static_cast<int>(minLevel), std::memory_order_relaxed);
}

LogLevel GetLogLevel()
{
    return static_cast<LogLevel>(g_minLevel.load(std::memory_order_relaxed));
}

void Log(LogLevel level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(level, fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Error, fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Warn, fmt, args);
    va_end(args);
}

void Info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Info, fmt, args);
    va_end(args);
}

void Debug(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Debug, fmt, args);
    va_end(args);
}

void Trace(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    LogV(LogLevel::Trace, fmt, args);
    va_end(args);
}

void RecordFrame()
{
    g_framesRendered.fetch_add(1, std::memory_order_relaxed);
}

void BeginRenderWorkloadFrame(bool boxPerfMode)
{
    g_renderWorkloadBoxPerfMode.store(boxPerfMode, std::memory_order_relaxed);
    g_renderWorkloadLinePrimitives.store(0, std::memory_order_relaxed);
    g_renderWorkloadRectPrimitives.store(0, std::memory_order_relaxed);
    g_renderWorkloadFilledRectPrimitives.store(0, std::memory_order_relaxed);
    g_renderWorkloadTextCalls.store(0, std::memory_order_relaxed);
    g_renderWorkloadIconCalls.store(0, std::memory_order_relaxed);
    g_renderWorkloadCornerBoxes.store(0, std::memory_order_relaxed);
    g_renderWorkloadFastBoxes.store(0, std::memory_order_relaxed);
}

void RecordRenderPrimitive(RenderPrimitiveKind kind, uint64_t count)
{
    if (count == 0)
        return;

    switch (kind) {
    case RenderPrimitiveKind::Line:
        g_renderWorkloadLinePrimitives.fetch_add(count, std::memory_order_relaxed);
        break;
    case RenderPrimitiveKind::Rect:
        g_renderWorkloadRectPrimitives.fetch_add(count, std::memory_order_relaxed);
        break;
    case RenderPrimitiveKind::FilledRect:
        g_renderWorkloadFilledRectPrimitives.fetch_add(count, std::memory_order_relaxed);
        break;
    case RenderPrimitiveKind::Text:
        g_renderWorkloadTextCalls.fetch_add(count, std::memory_order_relaxed);
        break;
    case RenderPrimitiveKind::Icon:
        g_renderWorkloadIconCalls.fetch_add(count, std::memory_order_relaxed);
        break;
    }
}

void RecordRenderBox(bool fastPath)
{
    if (fastPath)
        g_renderWorkloadFastBoxes.fetch_add(1, std::memory_order_relaxed);
    else
        g_renderWorkloadCornerBoxes.fetch_add(1, std::memory_order_relaxed);
}

void RecordDmaRead(bool success, std::chrono::steady_clock::duration latency)
{
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(latency).count();
    RecordDmaRead(success, us > 0 ? static_cast<uint64_t>(us) : 0, ScopedDmaCallsite::Current());
}

void RecordDmaRead(bool success, uint64_t latencyUs)
{
    RecordDmaRead(success, latencyUs, ScopedDmaCallsite::Current());
}

void RecordDmaRead(bool success, uint64_t latencyUs, DmaCallsite callsite)
{
    if (success)
        g_dmaReadSucceeded.fetch_add(1, std::memory_order_relaxed);
    else
        g_dmaReadFailed.fetch_add(1, std::memory_order_relaxed);

    g_dmaReadLatencyTotalUs.fetch_add(latencyUs, std::memory_order_relaxed);
    UpdateAtomicMin(g_dmaReadLatencyMinUs, latencyUs);
    UpdateAtomicMax(g_dmaReadLatencyMaxUs, latencyUs);

    RecordDmaSample(callsite, success, latencyUs);
}

void RecordDecryptFailure()
{
    g_decryptFailures.fetch_add(1, std::memory_order_relaxed);
}

void RecordInvalidEntity()
{
    g_invalidEntities.fetch_add(1, std::memory_order_relaxed);
}

void RecordEntityScanCycle(size_t entityCount, double measuredHz)
{
    g_lastScanEntityCount.store(static_cast<uint64_t>(entityCount), std::memory_order_relaxed);
    g_entityScanCycles.fetch_add(1, std::memory_order_relaxed);
    if (measuredHz >= 0.0) {
        g_entityScanHzMilli.store(
            static_cast<uint64_t>(measuredHz * 1000.0 + 0.5),
            std::memory_order_relaxed);
    }
}

void RecordEntityProcessCycle(double measuredHz)
{
    g_entityProcessCycles.fetch_add(1, std::memory_order_relaxed);
    if (measuredHz >= 0.0) {
        g_entityProcessHzMilli.store(
            static_cast<uint64_t>(measuredHz * 1000.0 + 0.5),
            std::memory_order_relaxed);
    }
}

void RecordViewMatrixPublish()
{
    RecordPublishCadence(
        g_viewMatrixPublishCycles,
        g_viewMatrixPublishHzMilli,
        g_viewMatrixPublishLastTickMs,
        g_viewMatrixPublishLastIntervalMs,
        g_viewMatrixPublishMaxIntervalMs,
        0,
        nullptr);
}

void RecordRenderViewMatrixUse()
{
    g_renderViewMatrixUseCount.fetch_add(1, std::memory_order_relaxed);

    const uint64_t lastPublishTick =
        g_viewMatrixPublishLastTickMs.load(std::memory_order_relaxed);
    if (lastPublishTick == 0) {
        g_renderViewMatrixUseMissingPublish.fetch_add(1, std::memory_order_relaxed);
        g_renderViewMatrixUseLastAgeMs.store(0, std::memory_order_relaxed);
        return;
    }

    const uint64_t ageMs = GetTickCount64() - lastPublishTick;
    g_renderViewMatrixUseLastAgeMs.store(ageMs, std::memory_order_relaxed);
    UpdateAtomicMax(g_renderViewMatrixUseMaxAgeMs, ageMs);
    if (ageMs > 16)
        g_renderViewMatrixUseOver16Ms.fetch_add(1, std::memory_order_relaxed);
    if (ageMs > 33)
        g_renderViewMatrixUseOver33Ms.fetch_add(1, std::memory_order_relaxed);
    if (ageMs > 50)
        g_renderViewMatrixUseOver50Ms.fetch_add(1, std::memory_order_relaxed);
}

void RecordEntityPublish(size_t count)
{
    RecordPublishCadence(
        g_entityPublishCycles,
        g_entityPublishHzMilli,
        g_entityPublishLastTickMs,
        g_entityPublishLastIntervalMs,
        g_entityPublishMaxIntervalMs,
        count,
        &g_entityPublishLastCount);
}

void RecordEntitySnapshotCopy(size_t count, std::chrono::steady_clock::duration elapsed)
{
    RecordSnapshotCopy(
        g_entitySnapshotCopyCount,
        g_entitySnapshotCopyLastCount,
        g_entitySnapshotCopyLastUs,
        g_entitySnapshotCopyMaxUs,
        count,
        elapsed);
}

void RecordDynamicSnapshotCopy(size_t count, std::chrono::steady_clock::duration elapsed)
{
    RecordSnapshotCopy(
        g_dynamicSnapshotCopyCount,
        g_dynamicSnapshotCopyLastCount,
        g_dynamicSnapshotCopyLastUs,
        g_dynamicSnapshotCopyMaxUs,
        count,
        elapsed);
}

void SetEntityCount(size_t entityCount)
{
    g_entityCount.store(static_cast<uint64_t>(entityCount), std::memory_order_relaxed);
}

void SetDmaReady(bool ready)
{
    g_dmaReady.store(ready, std::memory_order_relaxed);
}

void SetProcessAttached(bool attached)
{
    g_processAttached.store(attached, std::memory_order_relaxed);
}

void SetKeyStatus(KeyStatus status, uint64_t key1, uint64_t key2)
{
    g_keyStatus.store(static_cast<int>(status), std::memory_order_relaxed);
    g_globalKey1.store(key1, std::memory_order_relaxed);
    g_globalKey2.store(key2, std::memory_order_relaxed);
}

void SetDmaProbeResult(bool attempted, bool succeeded, uint64_t address, uint16_t magic)
{
    g_dmaProbeAttempted.store(attempted, std::memory_order_relaxed);
    g_dmaProbeSucceeded.store(succeeded, std::memory_order_relaxed);
    g_dmaProbeAddress.store(address, std::memory_order_relaxed);
    g_dmaProbeMagic.store(static_cast<uint32_t>(magic), std::memory_order_relaxed);
}

void SetViewMatrixStatus(bool resolved, bool valid)
{
    g_viewMatrixResolved.store(resolved, std::memory_order_relaxed);
    g_viewMatrixValid.store(valid, std::memory_order_relaxed);
}

void RecordViewMatrixStability(bool acceptedLargeJump, bool transientRejected, float elementDelta)
{
    const float safeDelta = std::isfinite(elementDelta) ? std::fabs(elementDelta) : 0.0f;
    const uint64_t deltaMilli = static_cast<uint64_t>(safeDelta * 1000.0f + 0.5f);
    if (acceptedLargeJump) {
        g_viewMatrixAcceptedLargeJump.fetch_add(1, std::memory_order_relaxed);
        g_viewMatrixLastAcceptedJumpDeltaMilli.store(deltaMilli, std::memory_order_relaxed);
        return;
    }

    g_viewMatrixRejected.fetch_add(1, std::memory_order_relaxed);
    if (transientRejected)
        g_viewMatrixTransientRejected.fetch_add(1, std::memory_order_relaxed);
    g_viewMatrixLastRejectTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_viewMatrixLastRejectDeltaMilli.store(deltaMilli, std::memory_order_relaxed);
    UpdateAtomicMax(g_viewMatrixMaxRejectDeltaMilli, deltaMilli);
}

void RecordProjectionGlobalJump(size_t matchedCount, float medianDxPx, float medianDyPx, float medianDeltaPx)
{
    const float safeDx = std::isfinite(medianDxPx) ? medianDxPx : 0.0f;
    const float safeDy = std::isfinite(medianDyPx) ? medianDyPx : 0.0f;
    const float safeDelta = std::isfinite(medianDeltaPx) ? std::fabs(medianDeltaPx) : 0.0f;
    const uint64_t deltaPx = static_cast<uint64_t>(safeDelta + 0.5f);

    g_projectionGlobalJumpFrames.fetch_add(1, std::memory_order_relaxed);
    g_projectionLastGlobalJumpTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    g_projectionLastGlobalJumpMatched.store(static_cast<uint64_t>(matchedCount), std::memory_order_relaxed);
    g_projectionLastGlobalJumpMedianDxPx.store(static_cast<int64_t>(safeDx + (safeDx >= 0.0f ? 0.5f : -0.5f)), std::memory_order_relaxed);
    g_projectionLastGlobalJumpMedianDyPx.store(static_cast<int64_t>(safeDy + (safeDy >= 0.0f ? 0.5f : -0.5f)), std::memory_order_relaxed);
    g_projectionLastGlobalJumpDeltaPx.store(deltaPx, std::memory_order_relaxed);
    UpdateAtomicMax(g_projectionMaxGlobalJumpDeltaPx, deltaPx);
}

void RecordOverlayCanvasBounds(int x, int y, uint32_t windowWidth, uint32_t windowHeight,
                               uint32_t clientWidth, uint32_t clientHeight,
                               bool visible, bool boundsChanged)
{
    g_overlayCanvasX.store(x, std::memory_order_relaxed);
    g_overlayCanvasY.store(y, std::memory_order_relaxed);
    g_overlayCanvasWindowWidth.store(windowWidth, std::memory_order_relaxed);
    g_overlayCanvasWindowHeight.store(windowHeight, std::memory_order_relaxed);
    g_overlayCanvasClientWidth.store(clientWidth, std::memory_order_relaxed);
    g_overlayCanvasClientHeight.store(clientHeight, std::memory_order_relaxed);
    g_overlayCanvasVisible.store(visible, std::memory_order_relaxed);

    if (boundsChanged) {
        g_overlayCanvasBoundsChanges.fetch_add(1, std::memory_order_relaxed);
        g_overlayCanvasLastBoundsChangeTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
}

void RecordOverlayCanvasFrame(uint32_t swapchainWidth, uint32_t swapchainHeight,
                              float displayWidth, float displayHeight,
                              bool swapchainResized)
{
    const int displayW = std::isfinite(displayWidth) ? static_cast<int>(displayWidth + 0.5f) : 0;
    const int displayH = std::isfinite(displayHeight) ? static_cast<int>(displayHeight + 0.5f) : 0;

    g_overlayCanvasSwapchainWidth.store(swapchainWidth, std::memory_order_relaxed);
    g_overlayCanvasSwapchainHeight.store(swapchainHeight, std::memory_order_relaxed);
    g_overlayCanvasDisplayWidth.store(displayW, std::memory_order_relaxed);
    g_overlayCanvasDisplayHeight.store(displayH, std::memory_order_relaxed);

    if (swapchainResized) {
        g_overlayCanvasSwapchainResizes.fetch_add(1, std::memory_order_relaxed);
        g_overlayCanvasLastSwapchainResizeTickMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
}

void SetRenderPipelineStatus(bool drawRadarCalled, bool playerInfoCalled, bool skillInfoCalled, bool entityListEmpty)
{
    g_renderDrawRadarCalled.store(drawRadarCalled, std::memory_order_relaxed);
    g_renderPlayerInfoCalled.store(playerInfoCalled, std::memory_order_relaxed);
    g_renderSkillInfoCalled.store(skillInfoCalled, std::memory_order_relaxed);
    g_renderEntityListEmpty.store(entityListEmpty, std::memory_order_relaxed);
}

void SetEntityProcessStats(const EntityProcessStats& stats)
{
    g_entityProcessRaw.store(static_cast<uint64_t>(stats.raw), std::memory_order_relaxed);
    g_entityProcessValidated.store(static_cast<uint64_t>(stats.validated), std::memory_order_relaxed);
    g_entityProcessDynamic.store(static_cast<uint64_t>(stats.dynamic), std::memory_order_relaxed);
    g_entityProcessNullPair.store(static_cast<uint64_t>(stats.nullPair), std::memory_order_relaxed);
    g_entityProcessDuplicate.store(static_cast<uint64_t>(stats.duplicate), std::memory_order_relaxed);
    g_entityProcessHealthBaseFail.store(static_cast<uint64_t>(stats.healthBaseFail), std::memory_order_relaxed);
    g_entityProcessHealthBaseMissing.store(static_cast<uint64_t>(stats.healthBaseMissing), std::memory_order_relaxed);
    g_entityProcessHealthReadFail.store(static_cast<uint64_t>(stats.healthReadFail), std::memory_order_relaxed);
    g_entityProcessLinkBaseFail.store(static_cast<uint64_t>(stats.linkBaseFail), std::memory_order_relaxed);
    g_entityProcessHeroBaseMissing.store(static_cast<uint64_t>(stats.heroBaseMissing), std::memory_order_relaxed);
    g_entityProcessHeroFallbackFail.store(static_cast<uint64_t>(stats.heroFallbackFail), std::memory_order_relaxed);
    g_entityProcessNameUnknown.store(static_cast<uint64_t>(stats.nameUnknown), std::memory_order_relaxed);
    g_entityProcessBoneCandidates.store(static_cast<uint64_t>(stats.boneCandidates), std::memory_order_relaxed);
    g_entityProcessBoneBaseNonZero.store(static_cast<uint64_t>(stats.boneBaseNonZero), std::memory_order_relaxed);
    g_entityProcessVelocityBoneDataNonZero.store(static_cast<uint64_t>(stats.velocityBoneDataNonZero), std::memory_order_relaxed);
    g_entityProcessBoneDataPtrNonZero.store(static_cast<uint64_t>(stats.boneDataPtrNonZero), std::memory_order_relaxed);
    g_entityProcessBonesBaseNonZero.store(static_cast<uint64_t>(stats.bonesBaseNonZero), std::memory_order_relaxed);
    g_entityProcessVelocityBoneIdTableNonZero.store(static_cast<uint64_t>(stats.velocityBoneIdTableNonZero), std::memory_order_relaxed);
    g_entityProcessVelocityBoneCountValid.store(static_cast<uint64_t>(stats.velocityBoneCountValid), std::memory_order_relaxed);
    g_entityProcessVelocityBoneIdTableReadable.store(static_cast<uint64_t>(stats.velocityBoneIdTableReadable), std::memory_order_relaxed);
    g_entityProcessVelocityBoneHeadIdFound.store(static_cast<uint64_t>(stats.velocityBoneHeadIdFound), std::memory_order_relaxed);
    g_entityProcessSkeletonAnyValid.store(static_cast<uint64_t>(stats.skeletonAnyValid), std::memory_order_relaxed);
    g_entityProcessSkeletonHeadValid.store(static_cast<uint64_t>(stats.skeletonHeadValid), std::memory_order_relaxed);
    g_entityProcessHeadProbeCandidates.store(static_cast<uint64_t>(stats.headProbeCandidates), std::memory_order_relaxed);
    g_entityProcessHeadProbeResolved.store(static_cast<uint64_t>(stats.headProbeResolved), std::memory_order_relaxed);
    g_entityProcessHeadProbeIdFound.store(static_cast<uint64_t>(stats.headProbeIdFound), std::memory_order_relaxed);
    g_entityProcessHeadProbeLocalFinite.store(static_cast<uint64_t>(stats.headProbeLocalFinite), std::memory_order_relaxed);
    g_entityProcessHeadProbeLocalNonZero.store(static_cast<uint64_t>(stats.headProbeLocalNonZero), std::memory_order_relaxed);
    g_entityProcessHeadProbeWorldNonZero.store(static_cast<uint64_t>(stats.headProbeWorldNonZero), std::memory_order_relaxed);
    g_entityProcessHeadProbeExceptions.store(static_cast<uint64_t>(stats.headProbeExceptions), std::memory_order_relaxed);
    g_entityProcessHeadProbeNearCandidates.store(static_cast<uint64_t>(stats.headProbeNearCandidates), std::memory_order_relaxed);
    g_entityProcessHeadProbeNearWorldNonZero.store(static_cast<uint64_t>(stats.headProbeNearWorldNonZero), std::memory_order_relaxed);
    g_entityProcessHeadProbeFarCandidates.store(static_cast<uint64_t>(stats.headProbeFarCandidates), std::memory_order_relaxed);
    g_entityProcessHeadProbeFarWorldNonZero.store(static_cast<uint64_t>(stats.headProbeFarWorldNonZero), std::memory_order_relaxed);
    g_entityProcessSampleBoneAddress.store(stats.sampleBoneAddress, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailComponentParent.store(stats.sampleHealthFailComponentParent, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailLinkParent.store(stats.sampleHealthFailLinkParent, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailHealthBase.store(stats.sampleHealthFailHealthBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailLinkBase.store(stats.sampleHealthFailLinkBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailVelocityBase.store(stats.sampleHealthFailVelocityBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailHeroBase.store(stats.sampleHealthFailHeroBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailTeamBase.store(stats.sampleHealthFailTeamBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailBoneBase.store(stats.sampleHealthFailBoneBase, std::memory_order_relaxed);
    g_entityProcessSampleHealthFailReadOk.store(stats.sampleHealthFailReadOk, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentParent.store(stats.sampleNameUnknownComponentParent, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownLinkParent.store(stats.sampleNameUnknownLinkParent, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentMatchId.store(stats.sampleNameUnknownComponentMatchId, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownLinkMatchId.store(stats.sampleNameUnknownLinkMatchId, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownHeroBase.store(stats.sampleNameUnknownHeroBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownHeroId.store(stats.sampleNameUnknownHeroId, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownHeroIdCandidate.store(stats.sampleNameUnknownHeroIdCandidate, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownHeroIdCandidateOffset.store(stats.sampleNameUnknownHeroIdCandidateOffset, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentHeroBase.store(stats.sampleNameUnknownComponentHeroBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentHeroId.store(stats.sampleNameUnknownComponentHeroId, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentHeroIdCandidate.store(stats.sampleNameUnknownComponentHeroIdCandidate, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownComponentHeroIdCandidateOffset.store(stats.sampleNameUnknownComponentHeroIdCandidateOffset, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownLinkBase.store(stats.sampleNameUnknownLinkBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownSkillBase.store(stats.sampleNameUnknownSkillBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownTeamBase.store(stats.sampleNameUnknownTeamBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownBoneBase.store(stats.sampleNameUnknownBoneBase, std::memory_order_relaxed);
    g_entityProcessSampleNameUnknownKind.store(stats.sampleNameUnknownKind, std::memory_order_relaxed);
    g_entityProcessSampleVelocityBase.store(stats.sampleVelocityBase, std::memory_order_relaxed);
    g_entityProcessSampleBoneBase.store(stats.sampleBoneBase, std::memory_order_relaxed);
    g_entityProcessSampleVelocityBoneData.store(stats.sampleVelocityBoneData, std::memory_order_relaxed);
    g_entityProcessSampleBoneDataPtr.store(stats.sampleBoneDataPtr, std::memory_order_relaxed);
    g_entityProcessSampleBonesBase.store(stats.sampleBonesBase, std::memory_order_relaxed);
    g_entityProcessSampleBoneIdTable.store(stats.sampleBoneIdTable, std::memory_order_relaxed);
    g_entityProcessSampleBoneCount.store(stats.sampleBoneCount, std::memory_order_relaxed);
    g_entityProcessSampleBoneIdTableReadable.store(stats.sampleBoneIdTableReadable, std::memory_order_relaxed);
    g_entityProcessSampleBoneHeadIndex.store(stats.sampleBoneHeadIndex, std::memory_order_relaxed);
}

void SetRosterStats(const RosterStats& stats)
{
    g_rosterFresh.store(static_cast<uint64_t>(stats.fresh), std::memory_order_relaxed);
    g_rosterDead.store(static_cast<uint64_t>(stats.dead), std::memory_order_relaxed);
    g_rosterMissing.store(static_cast<uint64_t>(stats.missing), std::memory_order_relaxed);
    g_rosterExpired.store(static_cast<uint64_t>(stats.expired), std::memory_order_relaxed);
    g_rosterHeroChanged.store(static_cast<uint64_t>(stats.heroChanged), std::memory_order_relaxed);
}

void SetEntityScanDetailStats(const EntityScanDetailStats& stats)
{
    std::lock_guard<std::mutex> lock(g_entityScanDetailMutex);
    g_entityScanDetail = stats;
}

void SetPlayerInfoStats(const PlayerInfoStats& stats)
{
    g_playerInfoBoxPerfMode.store(stats.boxPerfMode, std::memory_order_relaxed);
    g_playerInfoFastBoxPath.store(stats.fastBoxPath, std::memory_order_relaxed);
    g_playerInfoElapsedUs.store(static_cast<uint64_t>(stats.elapsedMs * 1000.0 + 0.5), std::memory_order_relaxed);
    g_playerInfoInput.store(static_cast<uint64_t>(stats.input), std::memory_order_relaxed);
    g_playerInfoProjected.store(static_cast<uint64_t>(stats.projected), std::memory_order_relaxed);
    g_playerInfoDrawn.store(static_cast<uint64_t>(stats.drawn), std::memory_order_relaxed);
    g_playerInfoSkippedDead.store(static_cast<uint64_t>(stats.skippedDead), std::memory_order_relaxed);
    g_playerInfoSkippedLocalHealth.store(static_cast<uint64_t>(stats.skippedLocalHealth), std::memory_order_relaxed);
    g_playerInfoSkippedLocalEntity.store(static_cast<uint64_t>(stats.skippedLocalEntity), std::memory_order_relaxed);
    g_playerInfoSkippedDistance.store(static_cast<uint64_t>(stats.skippedDistance), std::memory_order_relaxed);
    g_playerInfoSkippedOpacity.store(static_cast<uint64_t>(stats.skippedOpacity), std::memory_order_relaxed);
    g_playerInfoSkippedWorldToScreen.store(static_cast<uint64_t>(stats.skippedWorldToScreen), std::memory_order_relaxed);
    g_playerInfoSkippedWorldToScreenLow.store(static_cast<uint64_t>(stats.skippedWorldToScreenLow), std::memory_order_relaxed);
    g_playerInfoSkippedWorldToScreenHigh.store(static_cast<uint64_t>(stats.skippedWorldToScreenHigh), std::memory_order_relaxed);
    g_playerInfoSkippedBox.store(static_cast<uint64_t>(stats.skippedBox), std::memory_order_relaxed);
    g_playerInfoSkippedWindow.store(static_cast<uint64_t>(stats.skippedWindow), std::memory_order_relaxed);
    g_playerInfoSampleProjected.store(stats.sampleProjected, std::memory_order_relaxed);
    g_playerInfoSampleDrawn.store(stats.sampleDrawn, std::memory_order_relaxed);
    g_playerInfoSampleProjectedAddress.store(stats.sampleProjectedAddress, std::memory_order_relaxed);
    g_playerInfoSampleProjectedHeroId.store(stats.sampleProjectedHeroId, std::memory_order_relaxed);
    g_playerInfoSampleProjectedLeft.store(stats.sampleProjectedLeft, std::memory_order_relaxed);
    g_playerInfoSampleProjectedTop.store(stats.sampleProjectedTop, std::memory_order_relaxed);
    g_playerInfoSampleProjectedWidth.store(stats.sampleProjectedWidth, std::memory_order_relaxed);
    g_playerInfoSampleProjectedHeight.store(stats.sampleProjectedHeight, std::memory_order_relaxed);
    g_playerInfoSampleProjectedCenterX.store(stats.sampleProjectedCenterX, std::memory_order_relaxed);
    g_playerInfoSampleProjectedBottom.store(stats.sampleProjectedBottom, std::memory_order_relaxed);
    g_playerInfoSampleProjectedDistanceM.store(stats.sampleProjectedDistanceM, std::memory_order_relaxed);
    g_playerInfoSampleDrawnAddress.store(stats.sampleDrawnAddress, std::memory_order_relaxed);
    g_playerInfoSampleDrawnHeroId.store(stats.sampleDrawnHeroId, std::memory_order_relaxed);
    g_playerInfoSampleDrawnLeft.store(stats.sampleDrawnLeft, std::memory_order_relaxed);
    g_playerInfoSampleDrawnTop.store(stats.sampleDrawnTop, std::memory_order_relaxed);
    g_playerInfoSampleDrawnWidth.store(stats.sampleDrawnWidth, std::memory_order_relaxed);
    g_playerInfoSampleDrawnHeight.store(stats.sampleDrawnHeight, std::memory_order_relaxed);
    g_playerInfoSampleDrawnCenterX.store(stats.sampleDrawnCenterX, std::memory_order_relaxed);
    g_playerInfoSampleDrawnBottom.store(stats.sampleDrawnBottom, std::memory_order_relaxed);
    g_playerInfoSampleDrawnDistanceM.store(stats.sampleDrawnDistanceM, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionCandidates.store(
        static_cast<uint64_t>(stats.trainingBotPredictionCandidates), std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionApplied.store(
        static_cast<uint64_t>(stats.trainingBotPredictionApplied), std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionLeadDrops.store(
        static_cast<uint64_t>(stats.trainingBotPredictionLeadDrops), std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionMaxLeadMs.store(
        stats.trainingBotPredictionMaxLeadMs, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionMaxOffsetCm.store(
        stats.trainingBotPredictionMaxOffsetCm, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionLastDropAddress.store(
        stats.trainingBotPredictionLastDropAddress, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionLastDropFromMs.store(
        stats.trainingBotPredictionLastDropFromMs, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionLastDropToMs.store(
        stats.trainingBotPredictionLastDropToMs, std::memory_order_relaxed);
    g_playerInfoTrainingBotPredictionLastDropOffsetCm.store(
        stats.trainingBotPredictionLastDropOffsetCm, std::memory_order_relaxed);
}

void SetLocalEntityStats(const LocalEntityStats& stats)
{
    g_localAngleCandidates.store(static_cast<uint64_t>(stats.angleCandidates), std::memory_order_relaxed);
    g_localNearCameraCandidates.store(static_cast<uint64_t>(stats.nearCameraCandidates), std::memory_order_relaxed);
    g_localNamedCandidates.store(static_cast<uint64_t>(stats.namedCandidates), std::memory_order_relaxed);
    g_localSelected.store(static_cast<uint64_t>(stats.selected), std::memory_order_relaxed);
    g_localZeroHeadCandidates.store(static_cast<uint64_t>(stats.zeroHeadCandidates), std::memory_order_relaxed);
    g_localNonZeroPositionCandidates.store(static_cast<uint64_t>(stats.nonZeroPositionCandidates), std::memory_order_relaxed);
    g_localSelectedAddress.store(stats.selectedAddress, std::memory_order_relaxed);
    g_localSelectedHeroId.store(stats.selectedHeroId, std::memory_order_relaxed);
    g_localSelectedAngleBase.store(stats.selectedAngleBase, std::memory_order_relaxed);
    g_localSelectedHealth.store(stats.selectedHealth, std::memory_order_relaxed);
    g_localBestDistanceCm.store(stats.bestDistanceCm, std::memory_order_relaxed);
    g_localBestAddress.store(stats.bestAddress, std::memory_order_relaxed);
    g_localBestHeroId.store(stats.bestHeroId, std::memory_order_relaxed);
    g_localBestAngleBase.store(stats.bestAngleBase, std::memory_order_relaxed);
    g_localBestHealth.store(stats.bestHealth, std::memory_order_relaxed);
    g_localBestHeadXCm.store(stats.bestHeadXCm, std::memory_order_relaxed);
    g_localBestHeadYCm.store(stats.bestHeadYCm, std::memory_order_relaxed);
    g_localBestHeadZCm.store(stats.bestHeadZCm, std::memory_order_relaxed);
    g_localBestPosXCm.store(stats.bestPosXCm, std::memory_order_relaxed);
    g_localBestPosYCm.store(stats.bestPosYCm, std::memory_order_relaxed);
    g_localBestPosZCm.store(stats.bestPosZCm, std::memory_order_relaxed);
    g_localCameraXCm.store(stats.cameraXCm, std::memory_order_relaxed);
    g_localCameraYCm.store(stats.cameraYCm, std::memory_order_relaxed);
    g_localCameraZCm.store(stats.cameraZCm, std::memory_order_relaxed);
}

StatusSnapshot Snapshot()
{
    const double currentFps = UpdateFps();
    StatusSnapshot snapshot{};
    snapshot.entityCount = static_cast<size_t>(g_entityCount.load(std::memory_order_relaxed));
    snapshot.lastScanEntityCount = static_cast<size_t>(g_lastScanEntityCount.load(std::memory_order_relaxed));
    snapshot.entityScanCycles = g_entityScanCycles.load(std::memory_order_relaxed);
    snapshot.entityProcessCycles = g_entityProcessCycles.load(std::memory_order_relaxed);
    snapshot.entityScanHz =
        static_cast<double>(g_entityScanHzMilli.load(std::memory_order_relaxed)) / 1000.0;
    snapshot.entityProcessHz =
        static_cast<double>(g_entityProcessHzMilli.load(std::memory_order_relaxed)) / 1000.0;
    snapshot.roster.fresh = static_cast<size_t>(g_rosterFresh.load(std::memory_order_relaxed));
    snapshot.roster.dead = static_cast<size_t>(g_rosterDead.load(std::memory_order_relaxed));
    snapshot.roster.missing = static_cast<size_t>(g_rosterMissing.load(std::memory_order_relaxed));
    snapshot.roster.expired = static_cast<size_t>(g_rosterExpired.load(std::memory_order_relaxed));
    snapshot.roster.heroChanged = static_cast<size_t>(g_rosterHeroChanged.load(std::memory_order_relaxed));
    snapshot.fps = currentFps;
    snapshot.viewMatrixPublish = BuildPublishStats(
        g_viewMatrixPublishCycles,
        g_viewMatrixPublishHzMilli,
        g_viewMatrixPublishLastTickMs,
        g_viewMatrixPublishLastIntervalMs,
        g_viewMatrixPublishMaxIntervalMs,
        nullptr);
    snapshot.entityPublish = BuildPublishStats(
        g_entityPublishCycles,
        g_entityPublishHzMilli,
        g_entityPublishLastTickMs,
        g_entityPublishLastIntervalMs,
        g_entityPublishMaxIntervalMs,
        &g_entityPublishLastCount);
    snapshot.entitySnapshotCopy = BuildSnapshotCopyStats(
        g_entitySnapshotCopyCount,
        g_entitySnapshotCopyLastCount,
        g_entitySnapshotCopyLastUs,
        g_entitySnapshotCopyMaxUs);
    snapshot.dynamicSnapshotCopy = BuildSnapshotCopyStats(
        g_dynamicSnapshotCopyCount,
        g_dynamicSnapshotCopyLastCount,
        g_dynamicSnapshotCopyLastUs,
        g_dynamicSnapshotCopyMaxUs);
    snapshot.renderViewMatrixUse = BuildViewMatrixConsumerStats();

    snapshot.dmaReads.succeeded = g_dmaReadSucceeded.load(std::memory_order_relaxed);
    snapshot.dmaReads.failed = g_dmaReadFailed.load(std::memory_order_relaxed);
    snapshot.dmaReads.total = snapshot.dmaReads.succeeded + snapshot.dmaReads.failed;
    snapshot.dmaReads.maxLatencyUs = g_dmaReadLatencyMaxUs.load(std::memory_order_relaxed);

    const uint64_t minLatency = g_dmaReadLatencyMinUs.load(std::memory_order_relaxed);
    snapshot.dmaReads.minLatencyUs =
        snapshot.dmaReads.total == 0 || minLatency == (std::numeric_limits<uint64_t>::max)()
            ? 0
            : minLatency;
    snapshot.dmaReads.avgLatencyUs =
        snapshot.dmaReads.total == 0
            ? 0
            : g_dmaReadLatencyTotalUs.load(std::memory_order_relaxed) / snapshot.dmaReads.total;

    snapshot.errors.failedDmaReads = snapshot.dmaReads.failed;
    snapshot.errors.decryptFailures = g_decryptFailures.load(std::memory_order_relaxed);
    snapshot.errors.invalidEntities = g_invalidEntities.load(std::memory_order_relaxed);

    snapshot.dmaReady = g_dmaReady.load(std::memory_order_relaxed);
    snapshot.processAttached = g_processAttached.load(std::memory_order_relaxed);
    snapshot.keyStatus = static_cast<KeyStatus>(g_keyStatus.load(std::memory_order_relaxed));
    snapshot.globalKey1 = g_globalKey1.load(std::memory_order_relaxed);
    snapshot.globalKey2 = g_globalKey2.load(std::memory_order_relaxed);
    snapshot.dmaProbeAttempted = g_dmaProbeAttempted.load(std::memory_order_relaxed);
    snapshot.dmaProbeSucceeded = g_dmaProbeSucceeded.load(std::memory_order_relaxed);
    snapshot.dmaProbeAddress = g_dmaProbeAddress.load(std::memory_order_relaxed);
    snapshot.dmaProbeMagic = static_cast<uint16_t>(g_dmaProbeMagic.load(std::memory_order_relaxed));
    snapshot.viewMatrixResolved = g_viewMatrixResolved.load(std::memory_order_relaxed);
    snapshot.viewMatrixValid = g_viewMatrixValid.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.rejected = g_viewMatrixRejected.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.transientRejected = g_viewMatrixTransientRejected.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.acceptedLargeJump = g_viewMatrixAcceptedLargeJump.load(std::memory_order_relaxed);
    const uint64_t lastRejectTick = g_viewMatrixLastRejectTickMs.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.lastRejectAgeMs =
        lastRejectTick == 0 ? 0 : GetTickCount64() - lastRejectTick;
    snapshot.viewMatrixStability.lastRejectDeltaMilli =
        g_viewMatrixLastRejectDeltaMilli.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.maxRejectDeltaMilli =
        g_viewMatrixMaxRejectDeltaMilli.load(std::memory_order_relaxed);
    snapshot.viewMatrixStability.lastAcceptedJumpDeltaMilli =
        g_viewMatrixLastAcceptedJumpDeltaMilli.load(std::memory_order_relaxed);

    snapshot.projectionStability.globalJumpFrames =
        g_projectionGlobalJumpFrames.load(std::memory_order_relaxed);
    const uint64_t lastProjectionJumpTick =
        g_projectionLastGlobalJumpTickMs.load(std::memory_order_relaxed);
    snapshot.projectionStability.lastGlobalJumpAgeMs =
        lastProjectionJumpTick == 0 ? 0 : GetTickCount64() - lastProjectionJumpTick;
    snapshot.projectionStability.lastGlobalJumpMatched =
        g_projectionLastGlobalJumpMatched.load(std::memory_order_relaxed);
    snapshot.projectionStability.lastGlobalJumpMedianDxPx =
        g_projectionLastGlobalJumpMedianDxPx.load(std::memory_order_relaxed);
    snapshot.projectionStability.lastGlobalJumpMedianDyPx =
        g_projectionLastGlobalJumpMedianDyPx.load(std::memory_order_relaxed);
    snapshot.projectionStability.lastGlobalJumpDeltaPx =
        g_projectionLastGlobalJumpDeltaPx.load(std::memory_order_relaxed);
    snapshot.projectionStability.maxGlobalJumpDeltaPx =
        g_projectionMaxGlobalJumpDeltaPx.load(std::memory_order_relaxed);

    snapshot.overlayCanvas.x = g_overlayCanvasX.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.y = g_overlayCanvasY.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.windowWidth =
        static_cast<uint32_t>(g_overlayCanvasWindowWidth.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.windowHeight =
        static_cast<uint32_t>(g_overlayCanvasWindowHeight.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.clientWidth =
        static_cast<uint32_t>(g_overlayCanvasClientWidth.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.clientHeight =
        static_cast<uint32_t>(g_overlayCanvasClientHeight.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.swapchainWidth =
        static_cast<uint32_t>(g_overlayCanvasSwapchainWidth.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.swapchainHeight =
        static_cast<uint32_t>(g_overlayCanvasSwapchainHeight.load(std::memory_order_relaxed));
    snapshot.overlayCanvas.displayWidth = g_overlayCanvasDisplayWidth.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.displayHeight = g_overlayCanvasDisplayHeight.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.visible = g_overlayCanvasVisible.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.boundsChanges =
        g_overlayCanvasBoundsChanges.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.swapchainResizes =
        g_overlayCanvasSwapchainResizes.load(std::memory_order_relaxed);
    const uint64_t lastBoundsChangeTick =
        g_overlayCanvasLastBoundsChangeTickMs.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.lastBoundsChangeAgeMs =
        lastBoundsChangeTick == 0 ? 0 : GetTickCount64() - lastBoundsChangeTick;
    const uint64_t lastSwapchainResizeTick =
        g_overlayCanvasLastSwapchainResizeTickMs.load(std::memory_order_relaxed);
    snapshot.overlayCanvas.lastSwapchainResizeAgeMs =
        lastSwapchainResizeTick == 0 ? 0 : GetTickCount64() - lastSwapchainResizeTick;

    snapshot.renderDrawRadarCalled = g_renderDrawRadarCalled.load(std::memory_order_relaxed);
    snapshot.renderPlayerInfoCalled = g_renderPlayerInfoCalled.load(std::memory_order_relaxed);
    snapshot.renderSkillInfoCalled = g_renderSkillInfoCalled.load(std::memory_order_relaxed);
    snapshot.renderEntityListEmpty = g_renderEntityListEmpty.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_renderWorkloadMutex);
        snapshot.renderWorkload = g_lastRenderWorkload;
    }
    snapshot.entityProcess.raw = static_cast<size_t>(g_entityProcessRaw.load(std::memory_order_relaxed));
    snapshot.entityProcess.validated = static_cast<size_t>(g_entityProcessValidated.load(std::memory_order_relaxed));
    snapshot.entityProcess.dynamic = static_cast<size_t>(g_entityProcessDynamic.load(std::memory_order_relaxed));
    snapshot.entityProcess.nullPair = static_cast<size_t>(g_entityProcessNullPair.load(std::memory_order_relaxed));
    snapshot.entityProcess.duplicate = static_cast<size_t>(g_entityProcessDuplicate.load(std::memory_order_relaxed));
    snapshot.entityProcess.healthBaseFail = static_cast<size_t>(g_entityProcessHealthBaseFail.load(std::memory_order_relaxed));
    snapshot.entityProcess.healthBaseMissing = static_cast<size_t>(g_entityProcessHealthBaseMissing.load(std::memory_order_relaxed));
    snapshot.entityProcess.healthReadFail = static_cast<size_t>(g_entityProcessHealthReadFail.load(std::memory_order_relaxed));
    snapshot.entityProcess.linkBaseFail = static_cast<size_t>(g_entityProcessLinkBaseFail.load(std::memory_order_relaxed));
    snapshot.entityProcess.heroBaseMissing = static_cast<size_t>(g_entityProcessHeroBaseMissing.load(std::memory_order_relaxed));
    snapshot.entityProcess.heroFallbackFail = static_cast<size_t>(g_entityProcessHeroFallbackFail.load(std::memory_order_relaxed));
    snapshot.entityProcess.nameUnknown = static_cast<size_t>(g_entityProcessNameUnknown.load(std::memory_order_relaxed));
    snapshot.entityProcess.boneCandidates = static_cast<size_t>(g_entityProcessBoneCandidates.load(std::memory_order_relaxed));
    snapshot.entityProcess.boneBaseNonZero = static_cast<size_t>(g_entityProcessBoneBaseNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.velocityBoneDataNonZero = static_cast<size_t>(g_entityProcessVelocityBoneDataNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.boneDataPtrNonZero = static_cast<size_t>(g_entityProcessBoneDataPtrNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.bonesBaseNonZero = static_cast<size_t>(g_entityProcessBonesBaseNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.velocityBoneIdTableNonZero = static_cast<size_t>(g_entityProcessVelocityBoneIdTableNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.velocityBoneCountValid = static_cast<size_t>(g_entityProcessVelocityBoneCountValid.load(std::memory_order_relaxed));
    snapshot.entityProcess.velocityBoneIdTableReadable = static_cast<size_t>(g_entityProcessVelocityBoneIdTableReadable.load(std::memory_order_relaxed));
    snapshot.entityProcess.velocityBoneHeadIdFound = static_cast<size_t>(g_entityProcessVelocityBoneHeadIdFound.load(std::memory_order_relaxed));
    snapshot.entityProcess.skeletonAnyValid = static_cast<size_t>(g_entityProcessSkeletonAnyValid.load(std::memory_order_relaxed));
    snapshot.entityProcess.skeletonHeadValid = static_cast<size_t>(g_entityProcessSkeletonHeadValid.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeCandidates = static_cast<size_t>(g_entityProcessHeadProbeCandidates.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeResolved = static_cast<size_t>(g_entityProcessHeadProbeResolved.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeIdFound = static_cast<size_t>(g_entityProcessHeadProbeIdFound.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeLocalFinite = static_cast<size_t>(g_entityProcessHeadProbeLocalFinite.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeLocalNonZero = static_cast<size_t>(g_entityProcessHeadProbeLocalNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeWorldNonZero = static_cast<size_t>(g_entityProcessHeadProbeWorldNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeExceptions = static_cast<size_t>(g_entityProcessHeadProbeExceptions.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeNearCandidates = static_cast<size_t>(g_entityProcessHeadProbeNearCandidates.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeNearWorldNonZero = static_cast<size_t>(g_entityProcessHeadProbeNearWorldNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeFarCandidates = static_cast<size_t>(g_entityProcessHeadProbeFarCandidates.load(std::memory_order_relaxed));
    snapshot.entityProcess.headProbeFarWorldNonZero = static_cast<size_t>(g_entityProcessHeadProbeFarWorldNonZero.load(std::memory_order_relaxed));
    snapshot.entityProcess.sampleBoneAddress = g_entityProcessSampleBoneAddress.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailComponentParent = g_entityProcessSampleHealthFailComponentParent.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailLinkParent = g_entityProcessSampleHealthFailLinkParent.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailHealthBase = g_entityProcessSampleHealthFailHealthBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailLinkBase = g_entityProcessSampleHealthFailLinkBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailVelocityBase = g_entityProcessSampleHealthFailVelocityBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailHeroBase = g_entityProcessSampleHealthFailHeroBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailTeamBase = g_entityProcessSampleHealthFailTeamBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailBoneBase = g_entityProcessSampleHealthFailBoneBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleHealthFailReadOk = g_entityProcessSampleHealthFailReadOk.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentParent = g_entityProcessSampleNameUnknownComponentParent.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownLinkParent = g_entityProcessSampleNameUnknownLinkParent.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentMatchId = g_entityProcessSampleNameUnknownComponentMatchId.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownLinkMatchId = g_entityProcessSampleNameUnknownLinkMatchId.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownHeroBase = g_entityProcessSampleNameUnknownHeroBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownHeroId = g_entityProcessSampleNameUnknownHeroId.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownHeroIdCandidate = g_entityProcessSampleNameUnknownHeroIdCandidate.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownHeroIdCandidateOffset = g_entityProcessSampleNameUnknownHeroIdCandidateOffset.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentHeroBase = g_entityProcessSampleNameUnknownComponentHeroBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentHeroId = g_entityProcessSampleNameUnknownComponentHeroId.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentHeroIdCandidate = g_entityProcessSampleNameUnknownComponentHeroIdCandidate.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownComponentHeroIdCandidateOffset = g_entityProcessSampleNameUnknownComponentHeroIdCandidateOffset.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownLinkBase = g_entityProcessSampleNameUnknownLinkBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownSkillBase = g_entityProcessSampleNameUnknownSkillBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownTeamBase = g_entityProcessSampleNameUnknownTeamBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownBoneBase = g_entityProcessSampleNameUnknownBoneBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleNameUnknownKind = g_entityProcessSampleNameUnknownKind.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleVelocityBase = g_entityProcessSampleVelocityBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneBase = g_entityProcessSampleBoneBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleVelocityBoneData = g_entityProcessSampleVelocityBoneData.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneDataPtr = g_entityProcessSampleBoneDataPtr.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBonesBase = g_entityProcessSampleBonesBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneIdTable = g_entityProcessSampleBoneIdTable.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneCount = g_entityProcessSampleBoneCount.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneIdTableReadable = g_entityProcessSampleBoneIdTableReadable.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneHeadIndex = g_entityProcessSampleBoneHeadIndex.load(std::memory_order_relaxed);
    snapshot.playerInfo.boxPerfMode = g_playerInfoBoxPerfMode.load(std::memory_order_relaxed);
    snapshot.playerInfo.fastBoxPath = g_playerInfoFastBoxPath.load(std::memory_order_relaxed);
    snapshot.playerInfo.elapsedMs =
        static_cast<double>(g_playerInfoElapsedUs.load(std::memory_order_relaxed)) / 1000.0;
    snapshot.playerInfo.input = static_cast<size_t>(g_playerInfoInput.load(std::memory_order_relaxed));
    snapshot.playerInfo.projected = static_cast<size_t>(g_playerInfoProjected.load(std::memory_order_relaxed));
    snapshot.playerInfo.drawn = static_cast<size_t>(g_playerInfoDrawn.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedDead = static_cast<size_t>(g_playerInfoSkippedDead.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedLocalHealth = static_cast<size_t>(g_playerInfoSkippedLocalHealth.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedLocalEntity = static_cast<size_t>(g_playerInfoSkippedLocalEntity.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedDistance = static_cast<size_t>(g_playerInfoSkippedDistance.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedOpacity = static_cast<size_t>(g_playerInfoSkippedOpacity.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedWorldToScreen = static_cast<size_t>(g_playerInfoSkippedWorldToScreen.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedWorldToScreenLow = static_cast<size_t>(g_playerInfoSkippedWorldToScreenLow.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedWorldToScreenHigh = static_cast<size_t>(g_playerInfoSkippedWorldToScreenHigh.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedBox = static_cast<size_t>(g_playerInfoSkippedBox.load(std::memory_order_relaxed));
    snapshot.playerInfo.skippedWindow = static_cast<size_t>(g_playerInfoSkippedWindow.load(std::memory_order_relaxed));
    snapshot.playerInfo.sampleProjected = g_playerInfoSampleProjected.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawn = g_playerInfoSampleDrawn.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedAddress = g_playerInfoSampleProjectedAddress.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedHeroId = g_playerInfoSampleProjectedHeroId.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedLeft = g_playerInfoSampleProjectedLeft.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedTop = g_playerInfoSampleProjectedTop.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedWidth = g_playerInfoSampleProjectedWidth.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedHeight = g_playerInfoSampleProjectedHeight.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedCenterX = g_playerInfoSampleProjectedCenterX.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedBottom = g_playerInfoSampleProjectedBottom.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleProjectedDistanceM = g_playerInfoSampleProjectedDistanceM.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnAddress = g_playerInfoSampleDrawnAddress.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnHeroId = g_playerInfoSampleDrawnHeroId.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnLeft = g_playerInfoSampleDrawnLeft.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnTop = g_playerInfoSampleDrawnTop.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnWidth = g_playerInfoSampleDrawnWidth.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnHeight = g_playerInfoSampleDrawnHeight.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnCenterX = g_playerInfoSampleDrawnCenterX.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnBottom = g_playerInfoSampleDrawnBottom.load(std::memory_order_relaxed);
    snapshot.playerInfo.sampleDrawnDistanceM = g_playerInfoSampleDrawnDistanceM.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionCandidates =
        static_cast<size_t>(g_playerInfoTrainingBotPredictionCandidates.load(std::memory_order_relaxed));
    snapshot.playerInfo.trainingBotPredictionApplied =
        static_cast<size_t>(g_playerInfoTrainingBotPredictionApplied.load(std::memory_order_relaxed));
    snapshot.playerInfo.trainingBotPredictionLeadDrops =
        static_cast<size_t>(g_playerInfoTrainingBotPredictionLeadDrops.load(std::memory_order_relaxed));
    snapshot.playerInfo.trainingBotPredictionMaxLeadMs =
        g_playerInfoTrainingBotPredictionMaxLeadMs.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionMaxOffsetCm =
        g_playerInfoTrainingBotPredictionMaxOffsetCm.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionLastDropAddress =
        g_playerInfoTrainingBotPredictionLastDropAddress.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionLastDropFromMs =
        g_playerInfoTrainingBotPredictionLastDropFromMs.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionLastDropToMs =
        g_playerInfoTrainingBotPredictionLastDropToMs.load(std::memory_order_relaxed);
    snapshot.playerInfo.trainingBotPredictionLastDropOffsetCm =
        g_playerInfoTrainingBotPredictionLastDropOffsetCm.load(std::memory_order_relaxed);
    snapshot.localEntity.angleCandidates = static_cast<size_t>(g_localAngleCandidates.load(std::memory_order_relaxed));
    snapshot.localEntity.nearCameraCandidates = static_cast<size_t>(g_localNearCameraCandidates.load(std::memory_order_relaxed));
    snapshot.localEntity.namedCandidates = static_cast<size_t>(g_localNamedCandidates.load(std::memory_order_relaxed));
    snapshot.localEntity.selected = static_cast<size_t>(g_localSelected.load(std::memory_order_relaxed));
    snapshot.localEntity.zeroHeadCandidates = static_cast<size_t>(g_localZeroHeadCandidates.load(std::memory_order_relaxed));
    snapshot.localEntity.nonZeroPositionCandidates = static_cast<size_t>(g_localNonZeroPositionCandidates.load(std::memory_order_relaxed));
    snapshot.localEntity.selectedAddress = g_localSelectedAddress.load(std::memory_order_relaxed);
    snapshot.localEntity.selectedHeroId = g_localSelectedHeroId.load(std::memory_order_relaxed);
    snapshot.localEntity.selectedAngleBase = g_localSelectedAngleBase.load(std::memory_order_relaxed);
    snapshot.localEntity.selectedHealth = g_localSelectedHealth.load(std::memory_order_relaxed);
    snapshot.localEntity.bestDistanceCm = g_localBestDistanceCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestAddress = g_localBestAddress.load(std::memory_order_relaxed);
    snapshot.localEntity.bestHeroId = g_localBestHeroId.load(std::memory_order_relaxed);
    snapshot.localEntity.bestAngleBase = g_localBestAngleBase.load(std::memory_order_relaxed);
    snapshot.localEntity.bestHealth = g_localBestHealth.load(std::memory_order_relaxed);
    snapshot.localEntity.bestHeadXCm = g_localBestHeadXCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestHeadYCm = g_localBestHeadYCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestHeadZCm = g_localBestHeadZCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestPosXCm = g_localBestPosXCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestPosYCm = g_localBestPosYCm.load(std::memory_order_relaxed);
    snapshot.localEntity.bestPosZCm = g_localBestPosZCm.load(std::memory_order_relaxed);
    snapshot.localEntity.cameraXCm = g_localCameraXCm.load(std::memory_order_relaxed);
    snapshot.localEntity.cameraYCm = g_localCameraYCm.load(std::memory_order_relaxed);
    snapshot.localEntity.cameraZCm = g_localCameraZCm.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_entityScanDetailMutex);
        snapshot.entityScanDetail = g_entityScanDetail;
    }
    return snapshot;
}

void DumpStatus()
{
    const StatusSnapshot snapshot = Snapshot();

    Info("STATUS entities=%zu last_scan=%zu scan_cycles=%llu process_cycles=%llu scan_hz=%.1f process_hz=%.1f roster[fresh/dead/missing/expired/hero_change]=%zu/%zu/%zu/%zu/%zu fps=%.1f dma_reads=%llu ok=%llu fail=%llu latency_us[min/avg/max]=%llu/%llu/%llu errors[dma/decrypt/invalid]=%llu/%llu/%llu key=%s key1=0x%llX key2=0x%llX dma=%s process=%s",
        snapshot.entityCount,
        snapshot.lastScanEntityCount,
        static_cast<unsigned long long>(snapshot.entityScanCycles),
        static_cast<unsigned long long>(snapshot.entityProcessCycles),
        snapshot.entityScanHz,
        snapshot.entityProcessHz,
        snapshot.roster.fresh,
        snapshot.roster.dead,
        snapshot.roster.missing,
        snapshot.roster.expired,
        snapshot.roster.heroChanged,
        snapshot.fps,
        static_cast<unsigned long long>(snapshot.dmaReads.total),
        static_cast<unsigned long long>(snapshot.dmaReads.succeeded),
        static_cast<unsigned long long>(snapshot.dmaReads.failed),
        static_cast<unsigned long long>(snapshot.dmaReads.minLatencyUs),
        static_cast<unsigned long long>(snapshot.dmaReads.avgLatencyUs),
        static_cast<unsigned long long>(snapshot.dmaReads.maxLatencyUs),
        static_cast<unsigned long long>(snapshot.errors.failedDmaReads),
        static_cast<unsigned long long>(snapshot.errors.decryptFailures),
        static_cast<unsigned long long>(snapshot.errors.invalidEntities),
        ToString(snapshot.keyStatus),
        static_cast<unsigned long long>(snapshot.globalKey1),
        static_cast<unsigned long long>(snapshot.globalKey2),
        snapshot.dmaReady ? "ready" : "not-ready",
        snapshot.processAttached ? "attached" : "not-attached");

    Info("STATUS-CADENCE render_fps=%.1f viewmatrix_pub_hz=%.1f age_ms=%llu interval_ms[last/max]=%llu/%llu render_vm_age_ms[last/max]=%llu/%llu stale[16/33/50]=%llu/%llu/%llu entity_pub_hz=%.1f age_ms=%llu count=%zu interval_ms[last/max]=%llu/%llu snapshot_copy_ms[entities last/max=%.3f/%.3f dynamic last/max=%.3f/%.3f].",
        snapshot.fps,
        snapshot.viewMatrixPublish.hz,
        static_cast<unsigned long long>(snapshot.viewMatrixPublish.ageMs),
        static_cast<unsigned long long>(snapshot.viewMatrixPublish.lastIntervalMs),
        static_cast<unsigned long long>(snapshot.viewMatrixPublish.maxIntervalMs),
        static_cast<unsigned long long>(snapshot.renderViewMatrixUse.lastAgeMs),
        static_cast<unsigned long long>(snapshot.renderViewMatrixUse.maxAgeMs),
        static_cast<unsigned long long>(snapshot.renderViewMatrixUse.over16Ms),
        static_cast<unsigned long long>(snapshot.renderViewMatrixUse.over33Ms),
        static_cast<unsigned long long>(snapshot.renderViewMatrixUse.over50Ms),
        snapshot.entityPublish.hz,
        static_cast<unsigned long long>(snapshot.entityPublish.ageMs),
        snapshot.entityPublish.lastCount,
        static_cast<unsigned long long>(snapshot.entityPublish.lastIntervalMs),
        static_cast<unsigned long long>(snapshot.entityPublish.maxIntervalMs),
        snapshot.entitySnapshotCopy.lastMs,
        snapshot.entitySnapshotCopy.maxMs,
        snapshot.dynamicSnapshotCopy.lastMs,
        snapshot.dynamicSnapshotCopy.maxMs);
}

void SetRenderThread()
{
    g_renderThreadId.store(static_cast<uint64_t>(GetCurrentThreadId()), std::memory_order_relaxed);
    Info("Render thread registered. tid=%lu", GetCurrentThreadId());
}

void RecordFrameTiming(const FrameTiming& timing, double slowThresholdMs)
{
    // Collect render-thread DMA counters before resetting
    const uint64_t frameDmaReads = g_frameDmaReads.load(std::memory_order_relaxed);
    const uint64_t frameDmaFailures = g_frameDmaFailures.load(std::memory_order_relaxed);
    const uint64_t frameDmaTotalUs = g_frameDmaTotalUs.load(std::memory_order_relaxed);
    const uint64_t frameDmaMaxUs = g_frameDmaMaxUs.load(std::memory_order_relaxed);
    ResetFrameDmaCounters();

    RenderWorkloadStats workload{};
    workload.boxPerfMode = g_renderWorkloadBoxPerfMode.load(std::memory_order_relaxed);
    workload.totalMs = timing.totalMs;
    workload.renderCallbackMs = timing.renderCallbackMs;
    workload.presentMs = timing.presentMs;
    workload.linePrimitives = g_renderWorkloadLinePrimitives.load(std::memory_order_relaxed);
    workload.rectPrimitives = g_renderWorkloadRectPrimitives.load(std::memory_order_relaxed);
    workload.filledRectPrimitives = g_renderWorkloadFilledRectPrimitives.load(std::memory_order_relaxed);
    workload.textCalls = g_renderWorkloadTextCalls.load(std::memory_order_relaxed);
    workload.iconCalls = g_renderWorkloadIconCalls.load(std::memory_order_relaxed);
    workload.cornerBoxes = g_renderWorkloadCornerBoxes.load(std::memory_order_relaxed);
    workload.fastBoxes = g_renderWorkloadFastBoxes.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_renderWorkloadMutex);
        g_lastRenderWorkload = workload;
    }

    if (timing.totalMs <= slowThresholdMs)
        return;

    const uint64_t nowMs = GetTickCount64();
    ++g_slowFrameWindowCount;
    g_slowFrameWindowMaxTotalMs = (std::max)(g_slowFrameWindowMaxTotalMs, timing.totalMs);
    g_slowFrameWindowMaxRenderMs = (std::max)(g_slowFrameWindowMaxRenderMs, timing.renderCallbackMs);
    g_slowFrameWindowMaxPresentMs = (std::max)(g_slowFrameWindowMaxPresentMs, timing.presentMs);
    g_slowFrameWindowMaxRtDmaReads =
        (std::max)(g_slowFrameWindowMaxRtDmaReads, frameDmaReads);
    g_slowFrameWindowMaxRtDmaUs =
        (std::max)(g_slowFrameWindowMaxRtDmaUs, frameDmaMaxUs);

    if (g_slowFrameLastLogMs != 0 &&
        nowMs - g_slowFrameLastLogMs < kSlowFrameLogIntervalMs) {
        return;
    }

    const uint64_t slowFrameCount = g_slowFrameWindowCount;
    const double maxTotalMs = g_slowFrameWindowMaxTotalMs;
    const double maxRenderMs = g_slowFrameWindowMaxRenderMs;
    const double maxPresentMs = g_slowFrameWindowMaxPresentMs;
    const uint64_t maxRtDmaReads = g_slowFrameWindowMaxRtDmaReads;
    const uint64_t maxRtDmaUs = g_slowFrameWindowMaxRtDmaUs;
    g_slowFrameWindowCount = 0;
    g_slowFrameWindowMaxTotalMs = 0.0;
    g_slowFrameWindowMaxRenderMs = 0.0;
    g_slowFrameWindowMaxPresentMs = 0.0;
    g_slowFrameWindowMaxRtDmaReads = 0;
    g_slowFrameWindowMaxRtDmaUs = 0;
    g_slowFrameLastLogMs = nowMs;

    // Slow frame -- grab a recent DMA window for correlation only when logging.
    const DmaWindowStats dmaWindow = GetDmaWindowStats(100);

    // Build per-callsite breakdown string
    char callsiteDetail[512] = {};
    int off = 0;
    for (int i = 0; i < static_cast<int>(DmaCallsite::Count); ++i) {
        if (dmaWindow.perCallsiteReads[i] == 0 && dmaWindow.perCallsiteMaxUs[i] == 0)
            continue;
        off += std::snprintf(callsiteDetail + off, sizeof(callsiteDetail) - off,
            "%s[rd=%llu mx=%lluus] ",
            ToString(static_cast<DmaCallsite>(i)),
            static_cast<unsigned long long>(dmaWindow.perCallsiteReads[i]),
            static_cast<unsigned long long>(dmaWindow.perCallsiteMaxUs[i]));
    }
    if (off == 0) {
        std::snprintf(callsiteDetail, sizeof(callsiteDetail), "(no DMA in window)");
    }

    Warn("SLOW_FRAME total=%.1fms render=%.1fms present=%.1fms "
         "slowCount=%llu max[total=%.1fms render=%.1fms present=%.1fms rtDmaReads=%llu rtDmaMax=%lluus] "
         "rtDma[reads=%llu fail=%llu total=%lluus max=%lluus] "
         "dma100ms[reads=%llu fail=%llu max=%lluus] %s",
         timing.totalMs,
         timing.renderCallbackMs,
         timing.presentMs,
         static_cast<unsigned long long>(slowFrameCount),
         maxTotalMs,
         maxRenderMs,
         maxPresentMs,
         static_cast<unsigned long long>(maxRtDmaReads),
         static_cast<unsigned long long>(maxRtDmaUs),
         static_cast<unsigned long long>(frameDmaReads),
         static_cast<unsigned long long>(frameDmaFailures),
         static_cast<unsigned long long>(frameDmaTotalUs),
         static_cast<unsigned long long>(frameDmaMaxUs),
         static_cast<unsigned long long>(dmaWindow.totalReads),
         static_cast<unsigned long long>(dmaWindow.failedReads),
         static_cast<unsigned long long>(dmaWindow.maxLatencyUs),
         callsiteDetail);
}

DmaWindowStats GetDmaWindowStats(uint64_t windowMs)
{
    DmaWindowStats stats{};
    const uint64_t now = GetTickCount64();
    const uint64_t cutoff = (now > windowMs) ? (now - windowMs) : 0;

    const size_t writeIdx = g_dmaRingWrite.load(std::memory_order_acquire);
    const size_t start = (writeIdx >= kDmaRingCapacity) ? (writeIdx - kDmaRingCapacity) : 0;
    const size_t count = (writeIdx >= kDmaRingCapacity) ? kDmaRingCapacity : writeIdx;

    for (size_t i = 0; i < count; ++i) {
        const size_t idx = (start + i) % kDmaRingCapacity;
        const DmaSample& s = g_dmaRing[idx];
        if (s.timestampMs < cutoff)
            continue;

        stats.totalReads++;
        if (!s.success)
            stats.failedReads++;
        if (s.latencyUs > stats.maxLatencyUs)
            stats.maxLatencyUs = s.latencyUs;

        const int ci = static_cast<int>(s.callsite);
        if (ci >= 0 && ci < static_cast<int>(DmaCallsite::Count)) {
            stats.perCallsiteReads[ci]++;
            if (s.latencyUs > stats.perCallsiteMaxUs[ci])
                stats.perCallsiteMaxUs[ci] = s.latencyUs;
        }
    }

    return stats;
}

} // namespace Diagnostics
