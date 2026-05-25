#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

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

struct StatusSnapshot {
    size_t entityCount = 0;
    size_t lastScanEntityCount = 0;
    uint64_t entityScanCycles = 0;
    double fps = 0.0;
    DmaReadStats dmaReads{};
    ErrorCounters errors{};
    bool dmaReady = false;
    bool processAttached = false;
    KeyStatus keyStatus = KeyStatus::Unknown;
    uint64_t globalKey1 = 0;
    uint64_t globalKey2 = 0;
};

void Initialize(LogLevel minLevel = LogLevel::Info, const char* logPath = "./unleashed_diag.log");
void Shutdown();
bool IsInitialized();

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
void RecordDecryptFailure();
void RecordInvalidEntity();
void RecordEntityScanCycle(size_t entityCount);

void SetEntityCount(size_t entityCount);
void SetDmaReady(bool ready);
void SetProcessAttached(bool attached);
void SetKeyStatus(KeyStatus status, uint64_t key1 = 0, uint64_t key2 = 0);

StatusSnapshot Snapshot();
void DumpStatus();

const char* ToString(LogLevel level);
const char* ToString(KeyStatus status);

} // namespace Diagnostics
