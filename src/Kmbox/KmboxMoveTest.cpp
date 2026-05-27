#include "Kmbox/KmboxMoveTest.h"

#include <Windows.h>

#include "Kmbox/KmBoxNetManager.h"
#include "Kmbox/KmboxB.h"
#include "Kmbox/KmboxTimerResolution.h"
#include "Utils/Config.hpp"
#include "Utils/Diagnostics.hpp"

#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

namespace
{
    struct MoveDelta
    {
        int dx;
        int dy;
    };

    constexpr std::array<MoveDelta, 40> kSquareTrajectory = { {
        { 50, 0 }, { 50, 0 }, { 50, 0 }, { 50, 0 }, { 50, 0 },
        { 50, 0 }, { 50, 0 }, { 50, 0 }, { 50, 0 }, { 50, 0 },
        { 0, 30 }, { 0, 30 }, { 0, 30 }, { 0, 30 }, { 0, 30 },
        { 0, 30 }, { 0, 30 }, { 0, 30 }, { 0, 30 }, { 0, 30 },
        { -50, 0 }, { -50, 0 }, { -50, 0 }, { -50, 0 }, { -50, 0 },
        { -50, 0 }, { -50, 0 }, { -50, 0 }, { -50, 0 }, { -50, 0 },
        { 0, -30 }, { 0, -30 }, { 0, -30 }, { 0, -30 }, { 0, -30 },
        { 0, -30 }, { 0, -30 }, { 0, -30 }, { 0, -30 }, { 0, -30 },
    } };

    std::string Timestamp()
    {
        using namespace std::chrono;

        const auto now = system_clock::now();
        const auto millis = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
        const std::time_t time = system_clock::to_time_t(now);

        std::tm localTime{};
        localtime_s(&localTime, &time);

        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03lld",
            localTime.tm_hour,
            localTime.tm_min,
            localTime.tm_sec,
            static_cast<long long>(millis.count()));
        return buffer;
    }

    void PrintLine(const char* format, ...)
    {
        char message[512] = {};
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);

        const std::string timestamp = Timestamp();
        std::printf("[%s] %s\n", timestamp.c_str(), message);
        Diagnostics::Aim("kmbox.move_test %s", message);
    }

    bool EnsureNetworkKmboxReady()
    {
        KmBoxConnectionState state = kmbox::KmBoxMgr.GetConnectionState();
        PrintLine("Network KMBox state=%s.", ToString(state));
        if (state == KmBoxConnectionState::Connected)
            return true;

        PrintLine("Network KMBox is not connected; attempting init from current config.");
        kmbox::EnsureTimerResolution();
        const int status = kmbox::KmBoxMgr.InitDevice(
            OW::Config::kmboxIp,
            static_cast<WORD>(OW::Config::kmboxPort),
            OW::Config::kmboxMac);
        state = kmbox::KmBoxMgr.GetConnectionState();
        PrintLine("Network KMBox init status=%d state=%s.", status, ToString(state));

        if (status != success || state != KmBoxConnectionState::Connected) {
            PrintLine("Network KMBox is not connected; aborting move test.");
            return false;
        }

        return true;
    }

    bool EnsureSerialKmboxReady()
    {
        KmBoxConnectionState state = kmbox::kmBoxBMgr.GetConnectionState();
        PrintLine("Serial KMBox state=%s.", ToString(state));
        if (state == KmBoxConnectionState::Connected)
            return true;

        PrintLine("Serial KMBox is not connected; attempting init from current config.");
        kmbox::EnsureTimerResolution();
        const int status = kmbox::kmBoxBMgr.init(OW::Config::kmboxComPort);
        state = kmbox::kmBoxBMgr.GetConnectionState();
        PrintLine("Serial KMBox init status=%d state=%s.", status, ToString(state));

        if (status != success || state != KmBoxConnectionState::Connected) {
            PrintLine("Serial KMBox is not connected; aborting move test.");
            return false;
        }

        return true;
    }
}

void RunKmboxMoveTest()
{
    PrintLine("KMBox move stress test starting.");
    PrintLine("Configured: enabled=%d deviceType=%d", OW::Config::kmboxEnabled ? 1 : 0,
        OW::Config::kmboxDeviceType);

    if (!OW::Config::kmboxEnabled) {
        PrintLine("KMBox output is disabled in config; aborting move test.");
        return;
    }

    const int deviceType = OW::Config::kmboxDeviceType;
    if (deviceType != 0 && deviceType != 1) {
        PrintLine("Unsupported KMBox deviceType=%d; expected 0=network or 1=serial.", deviceType);
        return;
    }

    if (deviceType == 0) {
        if (!EnsureNetworkKmboxReady())
            return;
    } else if (!EnsureSerialKmboxReady()) {
        return;
    }

    PrintLine("Trajectory: 500px right, 300px down, 500px left, 300px up.");
    PrintLine("Submitting %zu moves in a tight loop with no sleep.", kSquareTrajectory.size());

    int movesSent = 0;
    const auto start = std::chrono::steady_clock::now();

    for (std::size_t index = 0; index < kSquareTrajectory.size(); ++index) {
        const MoveDelta& move = kSquareTrajectory[index];

        if (deviceType == 0) {
            const int status = kmbox::KmBoxMgr.Mouse.Move(move.dx, move.dy);
            if (status == success)
                ++movesSent;
            PrintLine("step=%02zu/%zu dx=%d dy=%d status=%d sent=%d",
                index + 1,
                kSquareTrajectory.size(),
                move.dx,
                move.dy,
                status,
                movesSent);
        } else {
            kmbox::kmBoxBMgr.km_move(move.dx, move.dy);
            ++movesSent;
            PrintLine("step=%02zu/%zu dx=%d dy=%d status=queued sent=%d",
                index + 1,
                kSquareTrajectory.size(),
                move.dx,
                move.dy,
                movesSent);
        }

        std::this_thread::yield();
    }

    const auto end = std::chrono::steady_clock::now();
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(end - start).count();
    const double movesPerSecond = elapsedMs > 0.0
        ? (static_cast<double>(movesSent) * 1000.0) / elapsedMs
        : 0.0;

    PrintLine("KMBox move stress test complete.");
    PrintLine("Total moves sent: %d", movesSent);
    PrintLine("Total elapsed time: %.3f ms", elapsedMs);
    PrintLine("Average moves per second: %.2f", movesPerSecond);
    PrintLine("Conclusion hint: If total time >> expected (~200ms for 60 UDP packets at 4ms flush), KMBox pipeline may be slow");
    PrintLine("Note: network move commands may be coalesced by the existing KMBox queue; compare this timing with visible cursor motion.");
}
