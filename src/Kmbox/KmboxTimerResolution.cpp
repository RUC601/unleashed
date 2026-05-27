#include "Kmbox/KmboxTimerResolution.h"

#include "Utils/Diagnostics.hpp"

#include <Windows.h>
#include <mmsystem.h>
#include <atomic>

namespace
{
    std::atomic<bool> g_TimerResolutionRaised{ false };
}

namespace kmbox
{
    bool EnsureTimerResolution()
    {
        bool expected = false;
        if (!g_TimerResolutionRaised.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return true;

        const MMRESULT result = timeBeginPeriod(1);
        if (result != TIMERR_NOERROR) {
            g_TimerResolutionRaised.store(false, std::memory_order_release);
            Diagnostics::Warn("timeBeginPeriod(1) failed; KMBox flush may be slow. result=%u",
                static_cast<unsigned int>(result));
            return false;
        }

        Diagnostics::Info("timeBeginPeriod(1) enabled for KMBox queue timing.");
        Diagnostics::Aim("kmbox.timer_resolution enabled period_ms=1");
        return true;
    }

    void ReleaseTimerResolution()
    {
        if (!g_TimerResolutionRaised.exchange(false, std::memory_order_acq_rel))
            return;

        const MMRESULT result = timeEndPeriod(1);
        if (result != TIMERR_NOERROR) {
            Diagnostics::Warn("timeEndPeriod(1) failed. result=%u",
                static_cast<unsigned int>(result));
            return;
        }

        Diagnostics::Info("timeEndPeriod(1) released for KMBox queue timing.");
        Diagnostics::Aim("kmbox.timer_resolution released period_ms=1");
    }

    bool IsTimerResolutionRaised()
    {
        return g_TimerResolutionRaised.load(std::memory_order_acquire);
    }
}
