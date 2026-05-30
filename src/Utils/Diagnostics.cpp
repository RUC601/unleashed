#include "Utils/Diagnostics.hpp"

#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
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
std::atomic<uint64_t> g_entityProcessSampleVelocityBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleVelocityBoneData{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneDataPtr{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBonesBase{ 0 };
std::atomic<uint64_t> g_entityProcessSampleBoneIdTable{ 0 };
std::atomic<int> g_entityProcessSampleBoneCount{ 0 };
std::atomic<int> g_entityProcessSampleBoneIdTableReadable{ 0 };
std::atomic<int> g_entityProcessSampleBoneHeadIndex{ -1 };
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

std::mutex g_fpsMutex;
std::chrono::steady_clock::time_point g_lastFpsTime = std::chrono::steady_clock::now();
uint64_t g_lastFpsFrameCount = 0;
double g_lastFps = 0.0;

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
        g_aimLogFile.flush();
    }
}

double UpdateFps()
{
    const auto now = std::chrono::steady_clock::now();
    const uint64_t frames = g_framesRendered.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(g_fpsMutex);
    const std::chrono::duration<double> elapsed = now - g_lastFpsTime;
    if (elapsed.count() > 0.001) {
        const uint64_t deltaFrames = frames - g_lastFpsFrameCount;
        g_lastFps = static_cast<double>(deltaFrames) / elapsed.count();
        g_lastFpsFrameCount = frames;
        g_lastFpsTime = now;
    }
    return g_lastFps;
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

void SetPlayerInfoStats(const PlayerInfoStats& stats)
{
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

    {
        std::lock_guard<std::mutex> lock(g_fpsMutex);
        snapshot.fps = g_lastFps;
    }

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
    snapshot.renderDrawRadarCalled = g_renderDrawRadarCalled.load(std::memory_order_relaxed);
    snapshot.renderPlayerInfoCalled = g_renderPlayerInfoCalled.load(std::memory_order_relaxed);
    snapshot.renderSkillInfoCalled = g_renderSkillInfoCalled.load(std::memory_order_relaxed);
    snapshot.renderEntityListEmpty = g_renderEntityListEmpty.load(std::memory_order_relaxed);
    snapshot.entityProcess.raw = static_cast<size_t>(g_entityProcessRaw.load(std::memory_order_relaxed));
    snapshot.entityProcess.validated = static_cast<size_t>(g_entityProcessValidated.load(std::memory_order_relaxed));
    snapshot.entityProcess.dynamic = static_cast<size_t>(g_entityProcessDynamic.load(std::memory_order_relaxed));
    snapshot.entityProcess.nullPair = static_cast<size_t>(g_entityProcessNullPair.load(std::memory_order_relaxed));
    snapshot.entityProcess.duplicate = static_cast<size_t>(g_entityProcessDuplicate.load(std::memory_order_relaxed));
    snapshot.entityProcess.healthBaseFail = static_cast<size_t>(g_entityProcessHealthBaseFail.load(std::memory_order_relaxed));
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
    snapshot.entityProcess.sampleVelocityBase = g_entityProcessSampleVelocityBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneBase = g_entityProcessSampleBoneBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleVelocityBoneData = g_entityProcessSampleVelocityBoneData.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneDataPtr = g_entityProcessSampleBoneDataPtr.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBonesBase = g_entityProcessSampleBonesBase.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneIdTable = g_entityProcessSampleBoneIdTable.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneCount = g_entityProcessSampleBoneCount.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneIdTableReadable = g_entityProcessSampleBoneIdTableReadable.load(std::memory_order_relaxed);
    snapshot.entityProcess.sampleBoneHeadIndex = g_entityProcessSampleBoneHeadIndex.load(std::memory_order_relaxed);
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
    return snapshot;
}

void DumpStatus()
{
    UpdateFps();
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

    if (timing.totalMs <= slowThresholdMs)
        return;

    // Slow frame — grab a recent DMA window for correlation
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
         "rtDma[reads=%llu fail=%llu total=%lluus max=%lluus] "
         "dma100ms[reads=%llu fail=%llu max=%lluus] %s",
         timing.totalMs,
         timing.renderCallbackMs,
         timing.presentMs,
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
