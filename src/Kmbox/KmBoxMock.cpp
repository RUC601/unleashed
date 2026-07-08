#include "Kmbox/KmBoxMock.h"

#include "Utils/Diagnostics.hpp"

namespace kmbox
{
    namespace
    {
        constexpr std::size_t kMaxRecentEvents = 512;
    }

    MockHardware MockHardwareMgr;

    const char* ToString(MockFaultMode mode)
    {
        switch (mode) {
        case MockFaultMode::None:          return "None";
        case MockFaultMode::OutputTimeout: return "OutputTimeout";
        case MockFaultMode::DropOutput:    return "DropOutput";
        default:                           return "Unknown";
        }
    }

    const char* ToString(MockEventType type)
    {
        switch (type) {
        case MockEventType::Move:            return "move";
        case MockEventType::AutoMove:        return "automove";
        case MockEventType::Button:          return "button";
        case MockEventType::ButtonStateMask: return "button_state_mask";
        case MockEventType::Keyboard:        return "keyboard";
        case MockEventType::MaskMouse:       return "mask_mouse";
        case MockEventType::UnmaskAll:       return "unmask_all";
        default:                             return "unknown";
        }
    }

    int MockHardware::Initialize()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearStateLocked(true);
        Diagnostics::Info("[KMBOX-MOCK] initialized.");
        Diagnostics::Aim("kmbox.mock init success");
        return success;
    }

    void MockHardware::Shutdown()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ClearStateLocked(false);
        Diagnostics::Info("[KMBOX-MOCK] shutdown.");
    }

    void MockHardware::Reset()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool keepInitialized = initialized_;
        ClearStateLocked(keepInitialized);
        Diagnostics::Info("[KMBOX-MOCK] state reset.");
        Diagnostics::Aim("kmbox.mock reset initialized=%d", keepInitialized ? 1 : 0);
    }

    bool MockHardware::IsInitialized() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    void MockHardware::SetFaultMode(MockFaultMode mode)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        faultMode_ = mode;
        Diagnostics::Info("[KMBOX-MOCK] fault mode set to %s.", ToString(mode));
        Diagnostics::Aim("kmbox.mock fault_mode=%s", ToString(mode));
    }

    MockFaultMode MockHardware::GetFaultMode() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return faultMode_;
    }

    int MockHardware::RecordMove(int x, int y, int runtimeMs)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        MockEvent event{};
        event.type = runtimeMs > 0 ? MockEventType::AutoMove : MockEventType::Move;
        event.x = x;
        event.y = y;
        event.runtimeMs = runtimeMs;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    int MockHardware::RecordButton(int button, bool down)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        const uint32_t buttonMask = MouseMaskForButton(button);
        if (buttonMask == 0)
            return err_net_cmd;

        const uint32_t next = down
            ? (outputMouseButtons_ | buttonMask)
            : (outputMouseButtons_ & ~buttonMask);
        if (next == outputMouseButtons_)
            return success;

        MockEvent event{};
        event.type = MockEventType::Button;
        event.button = button;
        event.down = down;
        event.stateMask = next;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        outputMouseButtons_ = next;
        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    int MockHardware::SetMouseButtonStateMask(uint32_t stateMask, bool force)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        const uint32_t next = stateMask & 0x7u;
        if (!force && next == outputMouseButtons_)
            return success;

        MockEvent event{};
        event.type = MockEventType::ButtonStateMask;
        event.stateMask = next;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        outputMouseButtons_ = next;
        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    int MockHardware::ForceReleaseMouseButton(int button)
    {
        const uint32_t buttonMask = MouseMaskForButton(button);
        if (buttonMask == 0)
            return err_net_cmd;

        uint32_t next = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            next = outputMouseButtons_ & ~buttonMask;
        }
        return SetMouseButtonStateMask(next, true);
    }

    int MockHardware::ForceReleaseMouseButtons()
    {
        return SetMouseButtonStateMask(0x00, true);
    }

    int MockHardware::MaskMouse(uint32_t mask)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        MockEvent event{};
        event.type = MockEventType::MaskMouse;
        event.mask = mask & 0x7Fu;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        maskedButtons_ |= event.mask;
        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    int MockHardware::UnmaskAll()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        MockEvent event{};
        event.type = MockEventType::UnmaskAll;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        maskedButtons_ = 0;
        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    int MockHardware::RecordKeyboardKey(unsigned char hidCode, bool down)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_)
            return err_creat_socket;

        MockEvent event{};
        event.type = MockEventType::Keyboard;
        event.hidCode = hidCode;
        event.down = down;

        if (faultMode_ == MockFaultMode::OutputTimeout) {
            event.status = err_net_rx_timeout;
            event.attempted = true;
            PushOutputEventLocked(event);
            return err_net_rx_timeout;
        }

        if (faultMode_ == MockFaultMode::DropOutput)
            return success;

        hidStates_[hidCode] = down;
        event.status = success;
        PushOutputEventLocked(event);
        return success;
    }

    MockHardwareSnapshot MockHardware::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        MockHardwareSnapshot snapshot{};
        snapshot.initialized = initialized_;
        snapshot.faultMode = faultMode_;
        snapshot.outputMouseButtons = outputMouseButtons_;
        snapshot.maskedButtons = maskedButtons_;
        snapshot.totalEvents = totalEvents_;
        snapshot.moveEvents = moveEvents_;
        snapshot.buttonEvents = buttonEvents_;
        snapshot.keyboardEvents = keyboardEvents_;
        snapshot.maskEvents = maskEvents_;
        snapshot.recentEventCount = events_.size();
        return snapshot;
    }

    std::vector<MockEvent> MockHardware::RecentEvents() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::vector<MockEvent>(events_.begin(), events_.end());
    }

    uint32_t MockHardware::MouseMaskForButton(int button) const
    {
        switch (button) {
        case 0: return 0x01u;
        case 1: return 0x02u;
        case 2: return 0x04u;
        default: return 0u;
        }
    }

    bool MockHardware::PushOutputEventLocked(MockEvent event)
    {
        event.sequence = ++totalEvents_;
        events_.push_back(event);
        while (events_.size() > kMaxRecentEvents)
            events_.pop_front();

        switch (event.type) {
        case MockEventType::Move:
        case MockEventType::AutoMove:
            ++moveEvents_;
            break;
        case MockEventType::Button:
        case MockEventType::ButtonStateMask:
            ++buttonEvents_;
            break;
        case MockEventType::Keyboard:
            ++keyboardEvents_;
            break;
        case MockEventType::MaskMouse:
        case MockEventType::UnmaskAll:
            ++maskEvents_;
            break;
        default:
            break;
        }

        Diagnostics::Aim("kmbox.mock event seq=%llu type=%s status=%d attempted=%d x=%d y=%d runtimeMs=%d button=%d down=%d stateMask=0x%02X mask=0x%02X hid=0x%02X",
            event.sequence,
            ToString(event.type),
            event.status,
            event.attempted ? 1 : 0,
            event.x,
            event.y,
            event.runtimeMs,
            event.button,
            event.down ? 1 : 0,
            event.stateMask,
            event.mask,
            static_cast<unsigned int>(event.hidCode));
        return true;
    }

    void MockHardware::ClearStateLocked(bool keepInitialized)
    {
        initialized_ = keepInitialized;
        faultMode_ = MockFaultMode::None;
        hidStates_.fill(false);
        outputMouseButtons_ = 0;
        maskedButtons_ = 0;
        totalEvents_ = 0;
        moveEvents_ = 0;
        buttonEvents_ = 0;
        keyboardEvents_ = 0;
        maskEvents_ = 0;
        events_.clear();
    }
}
