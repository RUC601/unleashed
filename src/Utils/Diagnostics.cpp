#include "Utils/Diagnostics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>

namespace Diagnostics {
namespace {

std::atomic<bool> g_initialized{ false };
std::atomic<int> g_minLevel{ static_cast<int>(LogLevel::Info) };

std::mutex g_logMutex;
std::ofstream g_logFile;
std::string g_logPath = "./unleashed_diag.log";

std::atomic<uint64_t> g_dmaReadSucceeded{ 0 };
std::atomic<uint64_t> g_dmaReadFailed{ 0 };
std::atomic<uint64_t> g_dmaReadLatencyTotalUs{ 0 };
std::atomic<uint64_t> g_dmaReadLatencyMinUs{ std::numeric_limits<uint64_t>::max() };
std::atomic<uint64_t> g_dmaReadLatencyMaxUs{ 0 };

std::atomic<uint64_t> g_decryptFailures{ 0 };
std::atomic<uint64_t> g_invalidEntities{ 0 };

std::atomic<uint64_t> g_entityCount{ 0 };
std::atomic<uint64_t> g_lastScanEntityCount{ 0 };
std::atomic<uint64_t> g_entityScanCycles{ 0 };
std::atomic<uint64_t> g_framesRendered{ 0 };

std::atomic<bool> g_dmaReady{ false };
std::atomic<bool> g_processAttached{ false };
std::atomic<int> g_keyStatus{ static_cast<int>(KeyStatus::Unknown) };
std::atomic<uint64_t> g_globalKey1{ 0 };
std::atomic<uint64_t> g_globalKey2{ 0 };

std::mutex g_fpsMutex;
std::chrono::steady_clock::time_point g_lastFpsTime = std::chrono::steady_clock::now();
uint64_t g_lastFpsFrameCount = 0;
double g_lastFps = 0.0;

bool ShouldLog(LogLevel level)
{
    return static_cast<int>(level) <= g_minLevel.load(std::memory_order_relaxed);
}

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

void LogV(LogLevel level, const char* fmt, va_list args)
{
    if (!ShouldLog(level))
        return;

    std::array<char, 2048> message{};
    std::vsnprintf(message.data(), message.size(), fmt, args);

    char line[2300] = {};
    std::snprintf(line, sizeof(line), "[%s] [%s] %s",
        BuildTimestamp().c_str(), ToString(level), message.data());

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::printf("%s\n", line);

    if (g_logFile.is_open()) {
        g_logFile << line << '\n';
        if (level == LogLevel::Error || level == LogLevel::Warn)
            g_logFile.flush();
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
    RecordDmaRead(success, us > 0 ? static_cast<uint64_t>(us) : 0);
}

void RecordDmaRead(bool success, uint64_t latencyUs)
{
    if (success)
        g_dmaReadSucceeded.fetch_add(1, std::memory_order_relaxed);
    else
        g_dmaReadFailed.fetch_add(1, std::memory_order_relaxed);

    g_dmaReadLatencyTotalUs.fetch_add(latencyUs, std::memory_order_relaxed);
    UpdateAtomicMin(g_dmaReadLatencyMinUs, latencyUs);
    UpdateAtomicMax(g_dmaReadLatencyMaxUs, latencyUs);
}

void RecordDecryptFailure()
{
    g_decryptFailures.fetch_add(1, std::memory_order_relaxed);
}

void RecordInvalidEntity()
{
    g_invalidEntities.fetch_add(1, std::memory_order_relaxed);
}

void RecordEntityScanCycle(size_t entityCount)
{
    g_lastScanEntityCount.store(static_cast<uint64_t>(entityCount), std::memory_order_relaxed);
    g_entityScanCycles.fetch_add(1, std::memory_order_relaxed);
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

StatusSnapshot Snapshot()
{
    StatusSnapshot snapshot{};
    snapshot.entityCount = static_cast<size_t>(g_entityCount.load(std::memory_order_relaxed));
    snapshot.lastScanEntityCount = static_cast<size_t>(g_lastScanEntityCount.load(std::memory_order_relaxed));
    snapshot.entityScanCycles = g_entityScanCycles.load(std::memory_order_relaxed);

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
        snapshot.dmaReads.total == 0 || minLatency == std::numeric_limits<uint64_t>::max()
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
    return snapshot;
}

void DumpStatus()
{
    UpdateFps();
    const StatusSnapshot snapshot = Snapshot();

    Info("STATUS entities=%zu last_scan=%zu scan_cycles=%llu fps=%.1f dma_reads=%llu ok=%llu fail=%llu latency_us[min/avg/max]=%llu/%llu/%llu errors[dma/decrypt/invalid]=%llu/%llu/%llu key=%s key1=0x%llX key2=0x%llX dma=%s process=%s",
        snapshot.entityCount,
        snapshot.lastScanEntityCount,
        static_cast<unsigned long long>(snapshot.entityScanCycles),
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

} // namespace Diagnostics
