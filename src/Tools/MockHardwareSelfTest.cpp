#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Utils/Diagnostics.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

Memory::~Memory() = default;

namespace
{
    int Fail(const char* message)
    {
        std::printf("MockHardwareSelfTest failed: %s\n", message);
        return EXIT_FAILURE;
    }

    void ConfigureMockRuntime()
    {
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        OW::Config::kmboxPort = 8808;
        OW::Config::kmboxMonitorPort = 8809;
        OW::Config::kmboxMonitorPortManualOverride = false;
        OW::Config::kmboxCountsPerRadian = 1000.0f;
        OW::Config::calibratedCountsPerRadian = 0.0f;
        OW::Config::calibratedPitchCountsPerRadian = 0.0f;
        OW::Config::autoScaleByGameSensitivity = false;
        OW::Config::gameMouseSensitivity = 15.0f;
        OW::Config::referenceGameSensitivity = 15.0f;
        OW::Config::kmboxInputDelayMs = 0;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;
    }

    int VerifyKmboxMonitorPortNormalization()
    {
        if (OW::Config::RecommendedKmboxMonitorPort(5512) != 5513)
            return Fail("recommended KMBox monitor port did not use command port + 1");

        if (OW::Config::RecommendedKmboxMonitorPort(65535) != 65534)
            return Fail("recommended KMBox monitor port did not handle 65535 command port");

        if (OW::Config::RecommendedKmboxMonitorPort(0) != 8809)
            return Fail("recommended KMBox monitor port did not fall back for invalid command port");

        OW::Config::kmboxPort = 5512;
        OW::Config::kmboxMonitorPort = 5001;
        OW::Config::kmboxMonitorPortManualOverride = false;
        OW::Config::kmboxMonitorPort = OW::Config::EffectiveKmboxMonitorPort();
        if (OW::Config::kmboxMonitorPort != 5513 ||
            OW::Config::EffectiveKmboxMonitorPort() != 5513)
            return Fail("auto KMBox monitor port did not sync stale valid port to recommended");

        OW::Config::kmboxPort = 5512;
        OW::Config::kmboxMonitorPort = 6000;
        OW::Config::kmboxMonitorPortManualOverride = true;
        OW::Config::kmboxMonitorPort = OW::Config::EffectiveKmboxMonitorPort();
        if (OW::Config::kmboxMonitorPort != 6000 ||
            OW::Config::EffectiveKmboxMonitorPort() != 6000)
            return Fail("manual KMBox monitor port override was not preserved");

        OW::Config::kmboxPort = 5512;
        OW::Config::kmboxMonitorPort = 5512;
        OW::Config::kmboxMonitorPortManualOverride = true;
        if (OW::Config::EffectiveKmboxMonitorPort() != 5513)
            return Fail("effective KMBox monitor port did not reject monitor==command");
        OW::Config::kmboxMonitorPort = OW::Config::EffectiveKmboxMonitorPort();
        if (OW::Config::kmboxMonitorPort != 5513)
            return Fail("manual KMBox monitor port equal to command was not repaired");

        OW::Config::kmboxPort = 5512;
        OW::Config::kmboxMonitorPort = 0;
        OW::Config::kmboxMonitorPortManualOverride = true;
        if (OW::Config::EffectiveKmboxMonitorPort() != 5513)
            return Fail("effective KMBox monitor port did not reject invalid manual override");
        OW::Config::kmboxMonitorPort = OW::Config::EffectiveKmboxMonitorPort();
        if (OW::Config::kmboxMonitorPort != 5513)
            return Fail("invalid manual KMBox monitor port was not repaired");

        OW::Config::kmboxPort = 65535;
        OW::Config::kmboxMonitorPort = 5001;
        OW::Config::kmboxMonitorPortManualOverride = false;
        OW::Config::kmboxMonitorPort = OW::Config::EffectiveKmboxMonitorPort();
        if (OW::Config::kmboxMonitorPort != 65534 ||
            OW::Config::EffectiveKmboxMonitorPort() != 65534)
            return Fail("auto KMBox monitor port did not handle 65535 command port");

        return EXIT_SUCCESS;
    }

    int VerifyKmboxOutputIntentPolicy()
    {
        if (OutputIntentForState(true) != KmBoxOutputIntent::Normal)
            return Fail("button-down did not resolve to normal output intent");
        if (OutputIntentForState(false) != KmBoxOutputIntent::SafetyRelease)
            return Fail("button-up did not resolve to safety-release output intent");
        if (!ShouldSuppressOutputForMenu(true, KmBoxOutputIntent::Normal))
            return Fail("menu suppression did not block normal output intent");
        if (ShouldSuppressOutputForMenu(true, KmBoxOutputIntent::SafetyRelease))
            return Fail("menu suppression blocked safety-release output intent");
        if (ShouldSuppressOutputForMenu(false, KmBoxOutputIntent::Normal) ||
            ShouldSuppressOutputForMenu(false, KmBoxOutputIntent::SafetyRelease)) {
            return Fail("output intent was suppressed while menu suppression was inactive");
        }
        return EXIT_SUCCESS;
    }

    int VerifyMouseReleaseWhileMenuOpen()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = false;

        if (!OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::PrimaryFire, true))) {
            return Fail("failed to seed held mouse action before opening menu");
        }
        if ((kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x01u) == 0)
            return Fail("seeded mouse action was not held");

        OW::Config::Menu = true;
        if (!OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::PrimaryFire, false))) {
            return Fail("mouse release was suppressed while menu was open");
        }
        if (kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0)
            return Fail("mouse release did not clear held output while menu was open");

        const kmbox::MockHardwareSnapshot before = kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(OW::Vector3(0.01f, -0.01f, 0.0f), 0);
        if (OW::SetActionState(OW::GameAction::PrimaryFire, true) !=
            OW::ActionOutputStatus::Suppressed) {
            return Fail("menu suppression did not block a new mouse press");
        }
        if (OW::SendMouseButtonStateMask(0x3u, true))
            return Fail("button state mask reported success while menu suppression was active");

        const kmbox::MockHardwareSnapshot after = kmbox::MockHardwareMgr.Snapshot();
        if (after.moveEvents != before.moveEvents)
            return Fail("menu suppression did not block mock mouse move");
        if (after.buttonEvents != before.buttonEvents)
            return Fail("menu suppression did not block a new mock mouse press");
        if (after.outputMouseButtons != before.outputMouseButtons)
            return Fail("menu suppression changed mock button state");

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        return EXIT_SUCCESS;
    }

    int VerifyKeyboardReleaseWhileMenuOpen()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = false;

        if (!OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::Ability1, true))) {
            return Fail("failed to seed held keyboard action before opening menu");
        }

        OW::Config::Menu = true;
        if (!OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::Ability1, false))) {
            return Fail("keyboard release was suppressed while menu was open");
        }

        const auto releasedEvents = kmbox::MockHardwareMgr.RecentEvents();
        if (releasedEvents.size() != 2 ||
            releasedEvents[0].type != kmbox::MockEventType::Keyboard ||
            releasedEvents[1].type != kmbox::MockEventType::Keyboard ||
            !releasedEvents[0].down || releasedEvents[1].down) {
            return Fail("keyboard release emitted the wrong sequence while menu was open");
        }

        const auto before = kmbox::MockHardwareMgr.Snapshot();
        if (OW::SetActionState(OW::GameAction::Ability1, true) !=
            OW::ActionOutputStatus::Suppressed) {
            return Fail("menu suppression did not block a new keyboard press");
        }
        if (kmbox::MockHardwareMgr.Snapshot().keyboardEvents != before.keyboardEvents)
            return Fail("suppressed keyboard press emitted a mock event");

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        return EXIT_SUCCESS;
    }

    int VerifySafetyCleanupWhileMenuOpen()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;

        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed held mouse button for safety cleanup");
        if (kmbox::MockHardwareMgr.MaskMouse(0x03u) != success)
            return Fail("failed to seed mouse mask for safety cleanup");

        OW::ForceReleaseMouseButtons();
        if (kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0)
            return Fail("force release was blocked while menu was open");
        if (!OW::UnmaskPhysicalMouseButtons())
            return Fail("unmask was blocked while menu was open");
        if (kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0)
            return Fail("unmask did not clear mask while menu was open");

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        return EXIT_SUCCESS;
    }

    int VerifyKeyboardReportBuilder()
    {
        soft_keyboard_t report{};
        if (!KmBoxNetManager::BuildKeyboardReport(KEY_LEFTSHIFT, true, report))
            return Fail("Shift keyboard report was rejected");
        if ((static_cast<unsigned char>(report.ctrl) & BIT1) == 0)
            return Fail("Shift keyboard report did not set the modifier bit");
        for (char key : report.button) {
            if (key != 0)
                return Fail("Shift keyboard report incorrectly used a normal key slot");
        }

        if (!KmBoxNetManager::BuildKeyboardReport(KEY_E, true, report))
            return Fail("E keyboard report was rejected");
        if (report.ctrl != 0 || static_cast<unsigned char>(report.button[0]) != KEY_E)
            return Fail("E keyboard report did not use button[0]");

        if (!KmBoxNetManager::BuildKeyboardReport(KEY_E, false, report))
            return Fail("keyboard release report was rejected");
        if (report.ctrl != 0)
            return Fail("keyboard release report retained a modifier");
        for (char key : report.button) {
            if (key != 0)
                return Fail("keyboard release report retained a key");
        }

        if (KmBoxNetManager::BuildKeyboardReport(0, true, report))
            return Fail("zero HID code was accepted");
        return EXIT_SUCCESS;
    }

    int VerifyTypedActionOutput()
    {
        kmbox::MockHardwareMgr.Reset();
        if (!OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::PrimaryFire, 0)))
            return Fail("typed primary-fire pulse failed");
        {
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (events.size() != 2)
                return Fail("primary-fire pulse did not emit exactly two events");
            for (const auto& event : events) {
                if (event.type != kmbox::MockEventType::Button || event.button != 0)
                    return Fail("primary-fire pulse emitted a non-left-button event");
            }
            if (!events[0].down || events[1].down)
                return Fail("primary-fire pulse did not emit down then up");
        }

        struct MouseActionCase {
            OW::GameAction action;
            int button;
        };
        constexpr MouseActionCase mouseCases[] = {
            { OW::GameAction::SecondaryFire, 1 },
            { OW::GameAction::MiddleMouse, 2 }
        };
        for (const auto& testCase : mouseCases) {
            kmbox::MockHardwareMgr.Reset();
            if (!OW::ActionOutputSucceeded(OW::PulseAction(testCase.action, 0)))
                return Fail("typed mouse action pulse failed");
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (events.size() != 2 ||
                events[0].type != kmbox::MockEventType::Button ||
                events[1].type != kmbox::MockEventType::Button ||
                events[0].button != testCase.button ||
                events[1].button != testCase.button ||
                !events[0].down || events[1].down) {
                return Fail("typed mouse action emitted the wrong button sequence");
            }
        }

        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(1, true) != success)
            return Fail("failed to seed held right button for scoped-fire test");
        if (!OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::PrimaryFire, 0)))
            return Fail("scoped primary-fire pulse failed");
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (snapshot.outputMouseButtons != 0x02u)
                return Fail("scoped primary-fire pulse changed held right-button state");
            if (events.size() != 3 ||
                events[1].type != kmbox::MockEventType::Button ||
                events[2].type != kmbox::MockEventType::Button ||
                events[1].button != 0 || events[2].button != 0 ||
                !events[1].down || events[2].down) {
                return Fail("scoped primary-fire pulse emitted events other than left down/up");
            }
        }

        struct KeyboardActionCase {
            OW::GameAction action;
            unsigned char hidCode;
        };
        constexpr KeyboardActionCase cases[] = {
            { OW::GameAction::Ability1, KEY_LEFTSHIFT },
            { OW::GameAction::Ability2, KEY_E },
            { OW::GameAction::Ultimate, KEY_Q },
            { OW::GameAction::Melee, KEY_V },
            { OW::GameAction::MoveForward, KEY_W }
        };

        for (const auto& testCase : cases) {
            kmbox::MockHardwareMgr.Reset();
            if (!OW::ActionOutputSucceeded(OW::PulseAction(testCase.action, 0)))
                return Fail("typed keyboard action pulse failed");
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (events.size() != 2 ||
                events[0].type != kmbox::MockEventType::Keyboard ||
                events[1].type != kmbox::MockEventType::Keyboard ||
                events[0].hidCode != testCase.hidCode ||
                events[1].hidCode != testCase.hidCode ||
                !events[0].down || events[1].down) {
                return Fail("typed keyboard action emitted the wrong HID sequence");
            }
        }

        OW::Config::kmboxEnabled = false;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) != success)
            return Fail("failed to disable active mock runtime");
        if (OW::SetActionState(OW::GameAction::Melee, true) != OW::ActionOutputStatus::Disabled)
            return Fail("disabled keyboard output did not fail closed");
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) != success)
            return Fail("failed to restore active mock runtime");

        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        if (OW::SetActionState(OW::GameAction::Melee, true) != OW::ActionOutputStatus::Suppressed)
            return Fail("menu-suppressed keyboard output did not report suppression");
        if (kmbox::MockHardwareMgr.Snapshot().keyboardEvents != 0)
            return Fail("menu-suppressed keyboard output emitted an event");
        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;

        if (OW::SetActionState(static_cast<OW::GameAction>(0xFF), true) != OW::ActionOutputStatus::InvalidAction)
            return Fail("invalid typed action did not fail closed");

        OW::Config::kmboxDeviceType = 1;
        if (!OW::ActionOutputSucceeded(OW::PulseAction(OW::GameAction::Melee, 0)))
            return Fail("mutable desired config rerouted the still-active mock backend");
        if (kmbox::RuntimeController().Applied().descriptor.backend !=
            kmbox::KmboxRuntimeBackend::Mock) {
            return Fail("active runtime changed without a reconcile transaction");
        }
        OW::Config::kmboxDeviceType = 2;

        return EXIT_SUCCESS;
    }
}

int main()
{
    Diagnostics::Initialize(Diagnostics::LogLevel::Error, "NUL");
    Diagnostics::InitializeAimLog("NUL");

    if (VerifyKmboxMonitorPortNormalization() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    ConfigureMockRuntime();
    if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) != success)
        return Fail("mock runtime reconcile returned failure");

    if (!kmbox::MockHardwareMgr.IsInitialized())
        return Fail("mock is not initialized");

    if (VerifyKeyboardReportBuilder() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyTypedActionOutput() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyKmboxOutputIntentPolicy() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyMouseReleaseWhileMenuOpen() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyKeyboardReleaseWhileMenuOpen() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifySafetyCleanupWhileMenuOpen() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    {
        const kmbox::MockHardwareSnapshot before = kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(OW::Vector3(0.0f, -0.02f, 0.0f), 0);
        const kmbox::MockHardwareSnapshot after = kmbox::MockHardwareMgr.Snapshot();
        if (after.moveEvents <= before.moveEvents)
            return Fail("OW::SendMouseMove did not record a mock move");
    }

    OW::SendMouseButton(0, true);
    if ((kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x01u) == 0)
        return Fail("OW::SendMouseButton did not press left output");
    OW::SendMouseButton(0, false);
    if ((kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x01u) != 0)
        return Fail("OW::SendMouseButton did not release left output");

    if (!OW::SendMouseButtonStateMask(0x3u, true))
        return Fail("OW::SendMouseButtonStateMask failed in mock mode");
    if ((kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x3u) != 0x3u)
        return Fail("mock button state mask did not apply");
    OW::ForceReleaseMouseButtons();
    if (kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0)
        return Fail("OW::ForceReleaseMouseButtons did not clear output buttons");

    if (!OW::MaskPhysicalMouseButtons(0x1u))
        return Fail("OW::MaskPhysicalMouseButtons failed in mock mode");
    if ((kmbox::MockHardwareMgr.Snapshot().maskedButtons & 0x1u) == 0)
        return Fail("mock mask state did not apply");
    if (!OW::UnmaskPhysicalMouseButtons())
        return Fail("OW::UnmaskPhysicalMouseButtons failed in mock mode");
    if (kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0)
        return Fail("mock unmask state did not clear");

    if (kmbox::MockHardwareMgr.RecordKeyboardKey(0x09, true) != success)
        return Fail("keyboard output record failed");
    if (kmbox::MockHardwareMgr.Snapshot().keyboardEvents == 0)
        return Fail("keyboard event was not counted");

    {
        const kmbox::MockHardwareSnapshot before = kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(OW::Vector3(0.02f, -0.03f, 0.0f), 8);
        OW::SendMouseButton(1, true);
        OW::SendMouseButton(1, false);
        kmbox::MockHardwareMgr.RecordKeyboardKey(0xE1, true);
        kmbox::MockHardwareMgr.RecordKeyboardKey(0xE1, false);
        kmbox::MockHardwareMgr.RecordKeyboardKey(0x19, true);
        kmbox::MockHardwareMgr.RecordKeyboardKey(0x19, false);
        const kmbox::MockHardwareSnapshot after = kmbox::MockHardwareMgr.Snapshot();
        if (after.moveEvents <= before.moveEvents)
            return Fail("Genji combo mock choreography did not record a move");
        if (after.buttonEvents < before.buttonEvents + 2)
            return Fail("Genji combo mock choreography did not record right click");
        if (after.keyboardEvents < before.keyboardEvents + 4)
            return Fail("Genji combo mock choreography did not record Shift/V");
    }

    kmbox::MockHardwareMgr.Reset();
    kmbox::MockHardwareMgr.SetFaultMode(kmbox::MockFaultMode::OutputTimeout);
    if (OW::PulseAction(OW::GameAction::Melee, 1) != OW::ActionOutputStatus::TransportError)
        return Fail("typed action did not propagate mock output timeout");
    {
        const auto events = kmbox::MockHardwareMgr.RecentEvents();
        if (events.size() != 1 || events.back().type != kmbox::MockEventType::Keyboard ||
            !events.back().down || !events.back().attempted)
            return Fail("failed typed action emitted an unexpected release");
    }
    if (kmbox::MockHardwareMgr.RecordMove(1, 2, 0) != err_net_rx_timeout)
        return Fail("OutputTimeout did not return timeout status");
    {
        const auto events = kmbox::MockHardwareMgr.RecentEvents();
        if (events.empty() || !events.back().attempted || events.back().status != err_net_rx_timeout)
            return Fail("OutputTimeout did not record attempted event");
    }

    kmbox::MockHardwareMgr.Reset();
    kmbox::MockHardwareMgr.SetFaultMode(kmbox::MockFaultMode::DropOutput);
    if (kmbox::MockHardwareMgr.RecordMove(1, 2, 0) != success)
        return Fail("DropOutput did not return success");
    if (kmbox::MockHardwareMgr.Snapshot().totalEvents != 0)
        return Fail("DropOutput recorded an event");

    kmbox::ShutdownActiveRuntime(std::chrono::milliseconds(50));
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
    return EXIT_SUCCESS;
}
