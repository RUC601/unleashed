#include "Kmbox/KmBoxMock.h"

#include "Utils/Diagnostics.hpp"

#include <algorithm>

namespace kmbox
{
    namespace
    {
        constexpr std::size_t kMaxRecentEvents = 512;

        bool IsMouseVk(int vk)
        {
            switch (vk) {
            case VK_LBUTTON:
            case VK_RBUTTON:
            case VK_MBUTTON:
            case VK_XBUTTON1:
            case VK_XBUTTON2:
                return true;
            default:
                return false;
            }
        }
    }

    MockHardware MockHardwareMgr;

    const char* ToString(MockFaultMode mode)
    {
        switch (mode) {
        case MockFaultMode::None:          return "None";
        case MockFaultMode::OutputTimeout: return "OutputTimeout";
        case MockFaultMode::DropOutput:    return "DropOutput";
        case MockFaultMode::InputJitter:   return "InputJitter";
        case MockFaultMode::StuckButtons:  return "StuckButtons";
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
        monitorPackets_ = 1;
        Diagnostics::Info("[KMBOX-MOCK] initialized.");
        Diagnostics::Aim("kmbox.mock init success monitorPackets=%llu", monitorPackets_);
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
        if (keepInitialized)
            monitorPackets_ = 1;
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
        if (faultMode_ == MockFaultMode::StuckButtons) {
            stuckMouseButtons_ |= inputMouseButtons_;
        } else {
            stuckMouseButtons_ = 0;
        }
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

    bool MockHardware::SetInputVk(int vk, bool down)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidVk(vk))
            return false;

        InputState& state = inputStates_[static_cast<std::size_t>(vk)];
        state.previous = state.down;
        state.down = down;
        state.readCount = 0;

        const uint32_t mouseMask = MouseMaskForVk(vk);
        if (mouseMask != 0) {
            if (down) {
                inputMouseButtons_ |= mouseMask;
                if (faultMode_ == MockFaultMode::StuckButtons)
                    stuckMouseButtons_ |= mouseMask;
            } else {
                inputMouseButtons_ &= ~mouseMask;
                if (faultMode_ != MockFaultMode::StuckButtons)
                    stuckMouseButtons_ &= ~mouseMask;
            }
        }

        ++monitorPackets_;
        Diagnostics::Aim("kmbox.mock input_vk vk=0x%X down=%d mouseMask=0x%02X monitorPackets=%llu",
            vk,
            down ? 1 : 0,
            mouseMask,
            monitorPackets_);
        return true;
    }

    bool MockHardware::IsVkDown(int vk)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return ReadVkLocked(vk, true);
    }

    bool MockHardware::PeekVkDown(int vk) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ValidVk(vk))
            return false;

        const uint32_t mouseMask = MouseMaskForVk(vk);
        if (mouseMask != 0 && (stuckMouseButtons_ & mouseMask) != 0)
            return true;
        return inputStates_[static_cast<std::size_t>(vk)].down;
    }

    unsigned long long MockHardware::InputPacketCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return monitorPackets_;
    }

    MockHardwareSnapshot MockHardware::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        MockHardwareSnapshot snapshot{};
        snapshot.initialized = initialized_;
        snapshot.faultMode = faultMode_;
        snapshot.inputMouseButtons = inputMouseButtons_;
        snapshot.outputMouseButtons = outputMouseButtons_;
        snapshot.maskedButtons = maskedButtons_;
        snapshot.stuckMouseButtons = stuckMouseButtons_;
        snapshot.monitorPackets = monitorPackets_;
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

    bool MockHardware::ValidVk(int vk) const
    {
        return vk >= 0 && vk < static_cast<int>(inputStates_.size());
    }

    uint32_t MockHardware::MouseMaskForVk(int vk) const
    {
        switch (vk) {
        case VK_LBUTTON:  return 0x01u;
        case VK_RBUTTON:  return 0x02u;
        case VK_MBUTTON:  return 0x04u;
        case VK_XBUTTON1: return 0x08u;
        case VK_XBUTTON2: return 0x10u;
        default:          return 0u;
        }
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

    bool MockHardware::ReadVkLocked(int vk, bool countRead)
    {
        if (!initialized_ || !ValidVk(vk))
            return false;

        InputState& state = inputStates_[static_cast<std::size_t>(vk)];
        bool actual = state.down;
        const uint32_t mouseMask = MouseMaskForVk(vk);
        if (mouseMask != 0 && (stuckMouseButtons_ & mouseMask) != 0)
            actual = true;

        if (countRead && faultMode_ == MockFaultMode::InputJitter) {
            ++state.readCount;
            if (state.readCount % 3 == 0)
                return state.previous;
        }

        return actual;
    }

    void MockHardware::ClearStateLocked(bool keepInitialized)
    {
        initialized_ = keepInitialized;
        faultMode_ = MockFaultMode::None;
        inputStates_.fill(InputState{});
        hidStates_.fill(false);
        inputMouseButtons_ = 0;
        outputMouseButtons_ = 0;
        maskedButtons_ = 0;
        stuckMouseButtons_ = 0;
        monitorPackets_ = 0;
        totalEvents_ = 0;
        moveEvents_ = 0;
        buttonEvents_ = 0;
        keyboardEvents_ = 0;
        maskEvents_ = 0;
        events_.clear();
    }
}
