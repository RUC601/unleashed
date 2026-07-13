#pragma once

#include "Kmbox/KmBoxConfig.h"

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace kmbox
{
    enum class MockFaultMode : int {
        None = 0,
        OutputTimeout,
        DropOutput
    };

    enum class MockEventType : int {
        Move = 0,
        AutoMove,
        Button,
        ButtonStateMask,
        Keyboard,
        MaskMouse,
        UnmaskAll
    };

    const char* ToString(MockFaultMode mode);
    const char* ToString(MockEventType type);

    struct MockEvent {
        MockEventType type = MockEventType::Move;
        unsigned long long sequence = 0;
        int x = 0;
        int y = 0;
        int runtimeMs = 0;
        int button = -1;
        bool down = false;
        uint32_t stateMask = 0;
        uint32_t mask = 0;
        unsigned char hidCode = 0;
        int status = success;
        bool attempted = false;
    };

    struct MockHardwareSnapshot {
        bool initialized = false;
        MockFaultMode faultMode = MockFaultMode::None;
        uint32_t outputMouseButtons = 0;
        uint32_t maskedButtons = 0;
        std::size_t outputKeyboardKeys = 0;
        unsigned long long totalEvents = 0;
        unsigned long long moveEvents = 0;
        unsigned long long buttonEvents = 0;
        unsigned long long keyboardEvents = 0;
        unsigned long long maskEvents = 0;
        std::size_t recentEventCount = 0;
    };

    class MockHardware
    {
    public:
        int Initialize();
        void Shutdown();
        void Reset();

        bool IsInitialized() const;
        void SetFaultMode(MockFaultMode mode);
        MockFaultMode GetFaultMode() const;

        int RecordMove(int x, int y, int runtimeMs = 0);
        int RecordButton(int button, bool down);
        int SetMouseButtonStateMask(uint32_t stateMask, bool force = false);
        int ForceReleaseMouseButton(int button);
        int ForceReleaseMouseButtons();
        int ReleaseAllOutputAndWait(std::chrono::milliseconds timeout);
        int MaskMouse(uint32_t mask);
        int UnmaskAll();
        int RecordKeyboardKey(unsigned char hidCode, bool down);

        MockHardwareSnapshot Snapshot() const;
        std::vector<MockEvent> RecentEvents() const;

    private:
        uint32_t MouseMaskForButton(int button) const;
        bool PushOutputEventLocked(MockEvent event);
        void ClearStateLocked(bool keepInitialized);

        mutable std::mutex mutex_;
        bool initialized_ = false;
        MockFaultMode faultMode_ = MockFaultMode::None;
        std::array<bool, 256> hidStates_{};
        uint32_t outputMouseButtons_ = 0;
        uint32_t maskedButtons_ = 0;
        unsigned long long totalEvents_ = 0;
        unsigned long long moveEvents_ = 0;
        unsigned long long buttonEvents_ = 0;
        unsigned long long keyboardEvents_ = 0;
        unsigned long long maskEvents_ = 0;
        std::deque<MockEvent> events_;
    };

    extern MockHardware MockHardwareMgr;
}
