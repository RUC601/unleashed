#pragma once

namespace kmbox
{
    bool EnsureTimerResolution();
    void ReleaseTimerResolution();
    bool IsTimerResolutionRaised();
}
