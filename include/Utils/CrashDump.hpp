#pragma once

namespace CrashDump {

void InstallUnhandledExceptionFilter(const wchar_t* dumpDirectory = L"crash_dumps");
const wchar_t* LastDumpPath();

} // namespace CrashDump
