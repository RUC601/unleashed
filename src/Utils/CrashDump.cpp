#include "Utils/CrashDump.hpp"

#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace CrashDump {
namespace {

wchar_t g_dumpDirectory[MAX_PATH] = L"crash_dumps";
wchar_t g_lastDumpPath[MAX_PATH] = L"";
LONG g_dumpWritten = 0;

bool IsFatalExceptionCode(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_DATATYPE_MISALIGNMENT:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_STACK_CHECK:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_INVALID_DISPOSITION:
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
        return true;
    default:
        return false;
    }
}

void AppendCrashLogA(const char* message)
{
    HANDLE file = CreateFileA(
        "unleashed_crash.log",
        FILE_APPEND_DATA,
        FILE_SHARE_READ,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE)
        return;

    DWORD bytesWritten = 0;
    WriteFile(file, message, static_cast<DWORD>(std::strlen(message)), &bytesWritten, nullptr);
    CloseHandle(file);
}

void AppendCrashLogWPath(const wchar_t* path)
{
    char pathUtf8[MAX_PATH * 4]{};
    WideCharToMultiByte(CP_UTF8, 0, path, -1, pathUtf8, sizeof(pathUtf8), nullptr, nullptr);

    char line[MAX_PATH * 4 + 64]{};
    std::snprintf(line, sizeof(line), "dump=%s\r\n", pathUtf8);
    AppendCrashLogA(line);
}

bool BuildDumpPath(wchar_t* path, size_t pathCount)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    const int written = _snwprintf_s(
        path,
        pathCount,
        _TRUNCATE,
        L"%s\\Unleashed-%04u%02u%02u-%02u%02u%02u-pid%lu-tid%lu.dmp",
        g_dumpDirectory,
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()));

    return written > 0;
}

void WriteDumpForException(EXCEPTION_POINTERS* exceptionInfo)
{
    if (!exceptionInfo || !exceptionInfo->ExceptionRecord)
        return;

    if (!IsFatalExceptionCode(exceptionInfo->ExceptionRecord->ExceptionCode))
        return;

    if (InterlockedExchange(&g_dumpWritten, 1) != 0)
        return;

    CreateDirectoryW(g_dumpDirectory, nullptr);

    wchar_t dumpPath[MAX_PATH]{};
    if (!BuildDumpPath(dumpPath, _countof(dumpPath)))
        return;

    char header[256]{};
    std::snprintf(
        header,
        sizeof(header),
        "exception=0x%08lX address=0x%p pid=%lu tid=%lu\r\n",
        static_cast<unsigned long>(exceptionInfo->ExceptionRecord->ExceptionCode),
        exceptionInfo->ExceptionRecord->ExceptionAddress,
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()));
    AppendCrashLogA(header);

    HANDLE file = CreateFileW(
        dumpPath,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        AppendCrashLogA("dump_create_failed\r\n");
        return;
    }

    MINIDUMP_EXCEPTION_INFORMATION dumpException{};
    dumpException.ThreadId = GetCurrentThreadId();
    dumpException.ExceptionPointers = exceptionInfo;
    dumpException.ClientPointers = FALSE;

    const MINIDUMP_TYPE dumpType = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithDataSegs |
        MiniDumpWithHandleData |
        MiniDumpWithUnloadedModules |
        MiniDumpWithThreadInfo);

    const BOOL ok = MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        file,
        dumpType,
        &dumpException,
        nullptr,
        nullptr);

    CloseHandle(file);

    if (ok) {
        wcscpy_s(g_lastDumpPath, dumpPath);
        AppendCrashLogWPath(dumpPath);
    } else {
        char line[128]{};
        std::snprintf(line, sizeof(line), "dump_write_failed gle=%lu\r\n", GetLastError());
        AppendCrashLogA(line);
    }
}

LONG CALLBACK VectoredExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    WriteDumpForException(exceptionInfo);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI UnhandledExceptionHandler(EXCEPTION_POINTERS* exceptionInfo)
{
    WriteDumpForException(exceptionInfo);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

void InstallUnhandledExceptionFilter(const wchar_t* dumpDirectory)
{
    if (dumpDirectory && dumpDirectory[0] != L'\0')
        wcscpy_s(g_dumpDirectory, dumpDirectory);

    CreateDirectoryW(g_dumpDirectory, nullptr);
    AddVectoredExceptionHandler(1, VectoredExceptionHandler);
    SetUnhandledExceptionFilter(UnhandledExceptionHandler);
}

const wchar_t* LastDumpPath()
{
    return g_lastDumpPath;
}

} // namespace CrashDump
