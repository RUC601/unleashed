#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Kmbox/KmBoxMock.h"
#include "Kmbox/KmBoxNetManager.h"
#include "Utils/Diagnostics.hpp"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <thread>

Memory::~Memory() = default;

namespace
{
    int Fail(const char* message)
    {
        std::printf("MockHardwareSelfTest failed: %s\n", message);
        return EXIT_FAILURE;
    }

    bool NearlyEqual(float left, float right, float epsilon = 0.00001f)
    {
        return std::fabs(left - right) <= epsilon;
    }

    int VerifyMagneticTriggerQuantizationRules()
    {
        {
            OW::MouseMoveQuantizationState state{};
            const OW::MouseMoveQuantizationResult result =
                OW::QuantizeMouseMoveCounts(
                    1.25f,
                    -2.5f,
                    1.25f,
                    -2.5f,
                    state,
                    true);
            if (result.pixelX != 1 || result.pixelY != -2 ||
                result.forcedMinimumStep ||
                !NearlyEqual(state.residualX, 0.25f) ||
                !NearlyEqual(state.residualY, -0.5f)) {
                return Fail("magnetic quantizer changed normal integer-count conversion");
            }
        }

        {
            OW::MouseMoveQuantizationState state{};
            const OW::MouseMoveQuantizationResult result =
                OW::QuantizeMouseMoveCounts(
                    0.4f,
                    -0.2f,
                    0.4f,
                    -0.2f,
                    state,
                    true);
            if (!result.forcedMinimumStep ||
                result.pixelX != 1 || result.pixelY != 0 ||
                !NearlyEqual(state.residualX, 0.0f) ||
                !NearlyEqual(state.residualY, -0.2f)) {
                return Fail("magnetic sub-count error did not force the dominant signed axis");
            }
        }

        {
            OW::MouseMoveQuantizationState state{};
            const OW::MouseMoveQuantizationResult first =
                OW::QuantizeMouseMoveCounts(
                    0.25f,
                    0.25f,
                    0.25f,
                    0.25f,
                    state,
                    true);
            const OW::MouseMoveQuantizationResult second =
                OW::QuantizeMouseMoveCounts(
                    0.25f,
                    0.25f,
                    0.25f,
                    0.25f,
                    state,
                    true);
            if (!first.forcedMinimumStep || !second.forcedMinimumStep ||
                first.pixelX != 1 || first.pixelY != 0 ||
                second.pixelX != 0 || second.pixelY != 1) {
                return Fail("magnetic equal-axis sub-count errors did not alternate axes");
            }
        }

        {
            OW::MouseMoveQuantizationState state{};
            state.residualX = 0.9f;
            state.residualY = -0.1f;
            const OW::MouseMoveQuantizationResult result =
                OW::QuantizeMouseMoveCounts(
                    0.0f,
                    0.0f,
                    0.2f,
                    -0.8f,
                    state,
                    true);
            if (!result.forcedMinimumStep ||
                result.pixelX != 0 || result.pixelY != -1 ||
                !NearlyEqual(state.residualX, 0.9f) ||
                !NearlyEqual(state.residualY, 0.0f)) {
                return Fail("magnetic deadzone fallback did not use raw target direction");
            }
        }

        {
            OW::MouseMoveQuantizationState state{};
            (void)OW::QuantizeMouseMoveCounts(
                0.6f,
                0.0f,
                0.6f,
                0.0f,
                state,
                false);
            const OW::MouseMoveQuantizationResult reversed =
                OW::QuantizeMouseMoveCounts(
                    -0.2f,
                    0.0f,
                    -0.2f,
                    0.0f,
                    state,
                    true);
            if (reversed.pixelX != -1 || reversed.pixelY != 0 ||
                !reversed.forcedMinimumStep ||
                !NearlyEqual(state.residualX, 0.0f)) {
                return Fail("magnetic direction reversal inherited old residual movement");
            }
        }

        {
            OW::MouseMoveQuantizationState state{};
            if (!OW::PrepareMouseMoveQuantizationState(state, 1, 10, 100))
                return Fail("magnetic quantizer did not initialize its identity");
            state.residualX = 0.4f;
            state.residualY = -0.3f;
            if (OW::PrepareMouseMoveQuantizationState(state, 1, 10, 100) ||
                !NearlyEqual(state.residualX, 0.4f) ||
                !NearlyEqual(state.residualY, -0.3f)) {
                return Fail("magnetic quantizer reset an unchanged identity");
            }
            if (!OW::PrepareMouseMoveQuantizationState(state, 1, 10, 101) ||
                !NearlyEqual(state.residualX, 0.0f) ||
                !NearlyEqual(state.residualY, 0.0f)) {
                return Fail("magnetic target change did not clear residual movement");
            }
            state.residualX = 0.4f;
            if (!OW::PrepareMouseMoveQuantizationState(state, 2, 10, 101) ||
                !NearlyEqual(state.residualX, 0.0f)) {
                return Fail("magnetic session change did not clear residual movement");
            }
            state.residualY = -0.3f;
            if (!OW::PrepareMouseMoveQuantizationState(state, 2, 11, 101) ||
                !NearlyEqual(state.residualY, 0.0f)) {
                return Fail("magnetic connection generation change did not clear residual movement");
            }
            OW::ResetMouseMoveQuantizationState(state);
            if (OW::HasMouseMoveQuantizationState(state))
                return Fail("magnetic inside-hit reset left quantization state behind");
        }

        {
            OW::MouseMoveQuantizationState state{};
            const OW::MouseMoveQuantizationResult first =
                OW::QuantizeMouseMoveCounts(
                    0.4f,
                    0.0f,
                    0.0f,
                    0.0f,
                    state,
                    false);
            const OW::MouseMoveQuantizationResult second =
                OW::QuantizeMouseMoveCounts(
                    0.4f,
                    0.0f,
                    0.0f,
                    0.0f,
                    state,
                    false);
            const OW::MouseMoveQuantizationResult third =
                OW::QuantizeMouseMoveCounts(
                    0.4f,
                    0.0f,
                    0.0f,
                    0.0f,
                    state,
                    false);
            if (first.pixelX != 0 || second.pixelX != 0 || third.pixelX != 1 ||
                first.forcedMinimumStep || second.forcedMinimumStep ||
                third.forcedMinimumStep ||
                !NearlyEqual(state.residualX, 0.2f)) {
                return Fail("default quantization no longer accumulates fractional counts");
            }
        }

        AimbotDetail::RuntimeState runtimeState{};
        if (!OW::PrepareMouseMoveQuantizationState(
                runtimeState.magneticMoveQuantization,
                1,
                10,
                100)) {
            return Fail("magnetic runtime state did not initialize");
        }
        runtimeState.magneticMoveQuantization.residualX = 0.5f;
        AimbotDetail::ResetTrackingSession(runtimeState);
        if (OW::HasMouseMoveQuantizationState(
                runtimeState.magneticMoveQuantization)) {
            return Fail("magnetic key/session release retained quantization state");
        }

        return EXIT_SUCCESS;
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
        if (!KmBoxNetManager::BuildKeyboardReport(
                BIT1, std::vector<unsigned char>{ KEY_E }, report))
            return Fail("Shift+E full keyboard report was rejected");
        if (static_cast<unsigned char>(report.ctrl) != BIT1 ||
            static_cast<unsigned char>(report.button[0]) != KEY_E) {
            return Fail("Shift+E full keyboard report was encoded incorrectly");
        }

        if (!KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ KEY_E }, report) ||
            report.ctrl != 0 ||
            static_cast<unsigned char>(report.button[0]) != KEY_E) {
            return Fail("releasing Shift did not preserve E in full report");
        }

        if (!KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ KEY_E, KEY_Q, KEY_V }, report) ||
            static_cast<unsigned char>(report.button[0]) != KEY_E ||
            static_cast<unsigned char>(report.button[1]) != KEY_Q ||
            static_cast<unsigned char>(report.button[2]) != KEY_V) {
            return Fail("multiple ordinary HID usages were encoded incorrectly");
        }

        if (!KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{}, report) ||
            report.ctrl != 0) {
            return Fail("zero keyboard report was rejected");
        }
        for (char key : report.button) {
            if (key != 0)
                return Fail("zero keyboard report retained an ordinary key");
        }

        std::vector<unsigned char> tooManyUsages(11, KEY_A);
        if (KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ KEY_NONE }, report) ||
            KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ KEY_LEFTSHIFT }, report) ||
            KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ 0xF0 }, report) ||
            KmBoxNetManager::BuildKeyboardReport(
                0, std::vector<unsigned char>{ KEY_E, KEY_E }, report) ||
            KmBoxNetManager::BuildKeyboardReport(0, tooManyUsages, report)) {
            return Fail("invalid or oversized keyboard report was accepted");
        }

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
        if (KmBoxNetManager::BuildKeyboardReport(0xF0, false, report))
            return Fail("invalid released HID code was accepted");
        return EXIT_SUCCESS;
    }

    int VerifyFullKeyboardReportRuntime()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;

        if (kmbox::SupportsKeyboardOutput(kmbox::KmboxRuntimeBackend::Serial))
            return Fail("serial backend incorrectly advertises keyboard output");

        if (kmbox::DispatchKeyboardReport(BIT1, { KEY_E }) != success)
            return Fail("mock Shift+E full report failed");
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (snapshot.outputKeyboardModifierMask != BIT1 ||
                snapshot.outputKeyboardUsages[0] != KEY_E ||
                snapshot.outputKeyboardKeys != 2 || events.size() != 2 ||
                events[0].hidCode != KEY_E || !events[0].down ||
                events[1].hidCode != KEY_LEFTSHIFT || !events[1].down) {
                return Fail("mock did not store/record exact Shift+E report");
            }
        }

        if (kmbox::DispatchKeyboardReport(0, { KEY_E }) != success)
            return Fail("mock Shift release with E held failed");
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (snapshot.outputKeyboardModifierMask != 0 ||
                snapshot.outputKeyboardUsages[0] != KEY_E ||
                snapshot.outputKeyboardKeys != 1 || events.size() != 3 ||
                events.back().hidCode != KEY_LEFTSHIFT || events.back().down) {
                return Fail("releasing Shift cleared E or recorded the wrong diff");
            }
        }

        if (kmbox::DispatchKeyboardReport(
                0, { KEY_E, KEY_Q, KEY_V }) != success) {
            return Fail("mock multi-key full report failed");
        }
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            if (snapshot.outputKeyboardKeys != 3 ||
                snapshot.outputKeyboardUsages[0] != KEY_E ||
                snapshot.outputKeyboardUsages[1] != KEY_Q ||
                snapshot.outputKeyboardUsages[2] != KEY_V) {
                return Fail("mock did not retain exact multi-key report");
            }
        }

        if (kmbox::DispatchKeyboardReport(BIT1, { KEY_E }) != success)
            return Fail("failed to seed Shift+E before menu safety test");

        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        const std::uint64_t generation =
            kmbox::ActiveRuntimeSnapshot().generation;
        if (kmbox::DispatchKeyboardReportForGeneration(
                generation,
                0,
                { KEY_E },
                KmBoxOutputIntent::SafetyRelease) != success) {
            return Fail("menu blocked release-only nonzero keyboard report");
        }
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            if (snapshot.outputKeyboardModifierMask != 0 ||
                snapshot.outputKeyboardUsages[0] != KEY_E ||
                snapshot.outputKeyboardKeys != 1) {
                return Fail("release-only report did not preserve held E");
            }
        }

        const auto beforeInvalidSafety = kmbox::MockHardwareMgr.Snapshot();
        if (kmbox::DispatchKeyboardReportForGeneration(
                generation,
                0,
                { KEY_E, KEY_W },
                KmBoxOutputIntent::SafetyRelease) != err_net_cmd) {
            return Fail("mock accepted safety report that added a new key");
        }
        const auto afterInvalidSafety = kmbox::MockHardwareMgr.Snapshot();
        if (afterInvalidSafety.totalEvents != beforeInvalidSafety.totalEvents ||
            afterInvalidSafety.outputKeyboardKeys !=
                beforeInvalidSafety.outputKeyboardKeys ||
            afterInvalidSafety.outputKeyboardUsages !=
                beforeInvalidSafety.outputKeyboardUsages) {
            return Fail("invalid mock safety report changed output state");
        }

        const auto beforeSuppressed = kmbox::MockHardwareMgr.Snapshot();
        if (kmbox::DispatchKeyboardReport(0, { KEY_W }) != err_output_suppressed)
            return Fail("menu did not suppress a nonzero full keyboard report");
        const auto afterSuppressed = kmbox::MockHardwareMgr.Snapshot();
        if (afterSuppressed.totalEvents != beforeSuppressed.totalEvents ||
            afterSuppressed.outputKeyboardKeys != beforeSuppressed.outputKeyboardKeys) {
            return Fail("menu-suppressed full report changed mock state");
        }

        if (kmbox::DispatchKeyboardReport(0, {}) != success)
            return Fail("menu blocked zero safety-release keyboard report");
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            if (snapshot.outputKeyboardKeys != 0 ||
                snapshot.outputKeyboardModifierMask != 0 ||
                snapshot.outputKeyboardUsages[0] != 0) {
                return Fail("zero safety report did not clear mock keyboard state");
            }
        }

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        const auto beforeStaleGeneration = kmbox::MockHardwareMgr.Snapshot();
        if (kmbox::DispatchKeyboardReportForGeneration(
                generation + 1,
                0,
                { KEY_W },
                KmBoxOutputIntent::Normal) != err_queue_stopped) {
            return Fail("stale generation keyboard report was accepted");
        }
        if (kmbox::MockHardwareMgr.Snapshot().totalEvents !=
            beforeStaleGeneration.totalEvents) {
            return Fail("stale generation keyboard report emitted mock events");
        }

        const auto beforeInvalid = kmbox::MockHardwareMgr.Snapshot();
        std::vector<unsigned char> tooManyUsages(11, KEY_A);
        if (kmbox::DispatchKeyboardReport(0, { KEY_NONE }) != err_net_cmd ||
            kmbox::DispatchKeyboardReport(0, { KEY_LEFTSHIFT }) != err_net_cmd ||
            kmbox::DispatchKeyboardReport(0, { 0xF0 }) != err_net_cmd ||
            kmbox::DispatchKeyboardReport(0, { KEY_E, KEY_E }) != err_net_cmd ||
            kmbox::DispatchKeyboardReport(0, tooManyUsages) != err_net_cmd) {
            return Fail("runtime accepted invalid full keyboard report");
        }
        if (kmbox::MockHardwareMgr.Snapshot().totalEvents !=
            beforeInvalid.totalEvents) {
            return Fail("invalid full keyboard report emitted mock events");
        }
        return EXIT_SUCCESS;
    }

    int VerifyTypedActionOutput()
    {
        kmbox::MockHardwareMgr.Reset();
        const OW::ActionOutputStatus primaryPulseStatus =
            OW::PulseAction(OW::GameAction::PrimaryFire, 0);
        if (!OW::ActionOutputSucceeded(primaryPulseStatus)) {
            const auto applied = kmbox::ActiveRuntimeSnapshot();
            const auto runtime = OW::CurrentKmboxOutputRuntimeState();
            std::fprintf(
                stderr,
                "primary pulse status=%d appliedGeneration=%llu runtimeGeneration=%llu gate=%d session=%d events=%llu\n",
                static_cast<int>(primaryPulseStatus),
                static_cast<unsigned long long>(applied.generation),
                static_cast<unsigned long long>(runtime.backendGeneration),
                runtime.outputGateOpen ? 1 : 0,
                OW::RuntimeOutputSessionEnabled() ? 1 : 0,
                kmbox::MockHardwareMgr.Snapshot().totalEvents);
            return Fail("typed primary-fire pulse failed");
        }
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

    int VerifyManualOutputOwnership()
    {
        OW::OutputScheduler& scheduler = OW::RuntimeOutputScheduler();
        scheduler.CancelAllAndJoin();
        if (!scheduler.SynchronizeRuntime())
            return Fail("manual ownership test could not synchronize mock runtime");

        kmbox::MockHardwareMgr.Reset();
        constexpr const char* globalDifferent =
            "selftest.global_aim.different_button";
        constexpr const char* triggerDifferent =
            "selftest.trigger.different_button";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalDifferent)) ||
            !OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                1, true, OW::OutputOwnerSource::Trigger, triggerDifferent))) {
            return Fail("different-button owners failed to acquire outputs");
        }
        if ((kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x03u) !=
            0x03u) {
            return Fail("different-button owners did not hold both outputs");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, false, OW::OutputOwnerSource::GlobalAim, globalDifferent)) ||
            kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0x02u) {
            return Fail("GlobalAim release cleared a different Trigger button");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                1, false, OW::OutputOwnerSource::Trigger, triggerDifferent)) ||
            kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0) {
            return Fail("Trigger release did not clear its different button");
        }

        kmbox::MockHardwareMgr.Reset();
        constexpr const char* globalSame =
            "selftest.global_aim.same_button";
        constexpr const char* triggerSame =
            "selftest.trigger.same_button";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalSame))) {
            return Fail("GlobalAim same-button owner failed to acquire output");
        }
        const auto afterFirstSamePress = kmbox::MockHardwareMgr.Snapshot();
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::Trigger, triggerSame))) {
            return Fail("Trigger same-button owner failed to overlap output");
        }
        const auto afterSecondSamePress = kmbox::MockHardwareMgr.Snapshot();
        if (afterSecondSamePress.buttonEvents != afterFirstSamePress.buttonEvents ||
            afterSecondSamePress.outputMouseButtons != 0x01u) {
            return Fail("overlapping same-button acquire emitted a duplicate down");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, false, OW::OutputOwnerSource::GlobalAim, globalSame))) {
            return Fail("GlobalAim same-button owner failed to release lease");
        }
        const auto afterFirstSameRelease = kmbox::MockHardwareMgr.Snapshot();
        if (afterFirstSameRelease.buttonEvents != afterSecondSamePress.buttonEvents ||
            afterFirstSameRelease.outputMouseButtons != 0x01u) {
            return Fail("first same-button release cleared the remaining owner");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, false, OW::OutputOwnerSource::Trigger, triggerSame))) {
            return Fail("final same-button owner failed to release output");
        }
        const auto afterFinalSameRelease = kmbox::MockHardwareMgr.Snapshot();
        if (afterFinalSameRelease.buttonEvents !=
                afterFirstSameRelease.buttonEvents + 1 ||
            afterFinalSameRelease.outputMouseButtons != 0) {
            return Fail("final same-button release did not emit exactly one up");
        }

        kmbox::MockHardwareMgr.Reset();
        constexpr const char* triggerAcrossGameAction =
            "selftest.trigger.game_action_overlap";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0,
                true,
                OW::OutputOwnerSource::Trigger,
                triggerAcrossGameAction))) {
            return Fail("Trigger owner failed before GameAction overlap");
        }
        const auto beforeGameAction = kmbox::MockHardwareMgr.Snapshot();
        if (!OW::ActionOutputSucceeded(
                OW::PulseAction(OW::GameAction::PrimaryFire, 0))) {
            return Fail("GameAction pulse failed while Trigger owned same button");
        }
        const auto afterGameAction = kmbox::MockHardwareMgr.Snapshot();
        if (afterGameAction.buttonEvents != beforeGameAction.buttonEvents ||
            afterGameAction.outputMouseButtons != 0x01u) {
            return Fail("GameAction pulse stole the Trigger button lease");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0,
                false,
                OW::OutputOwnerSource::Trigger,
                triggerAcrossGameAction)) ||
            kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0) {
            return Fail("Trigger owner did not survive and finish GameAction overlap");
        }

        kmbox::MockHardwareMgr.Reset();
        constexpr const char* globalTransition =
            "selftest.global_aim.transition";
        constexpr const char* triggerTransition =
            "selftest.trigger.transition";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalTransition)) ||
            !OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                1, true, OW::OutputOwnerSource::Trigger, triggerTransition)) ||
            !OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::Ability2, true))) {
            return Fail("failed to seed manual owners before menu transition");
        }

        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        if (scheduler.SynchronizeRuntime())
            return Fail("scheduler reported an open gate while menu suppressed output");
        const auto afterMenuTransition = kmbox::MockHardwareMgr.Snapshot();
        if (scheduler.ActiveActionCount() != 0 ||
            afterMenuTransition.outputMouseButtons != 0 ||
            afterMenuTransition.outputKeyboardKeys != 0) {
            return Fail("menu transition did not synchronously zero manual outputs");
        }
        const auto beforeSuppressedWrite = kmbox::MockHardwareMgr.Snapshot();
        if (OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalTransition) !=
            OW::ActionOutputStatus::Suppressed) {
            return Fail("menu transition did not suppress a replacement down");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, false, OW::OutputOwnerSource::GlobalAim, globalTransition))) {
            return Fail("ownerless safety release failed while menu was open");
        }
        const auto afterSuppressedWrite = kmbox::MockHardwareMgr.Snapshot();
        if (afterSuppressedWrite.totalEvents != beforeSuppressedWrite.totalEvents)
            return Fail("suppressed or ownerless menu output emitted a write");

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        if (!scheduler.SynchronizeRuntime())
            return Fail("scheduler did not reopen after menu transition");

        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalTransition)) ||
            !OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                1, true, OW::OutputOwnerSource::Trigger, triggerTransition)) ||
            !OW::ActionOutputSucceeded(
                OW::SetActionState(OW::GameAction::Ability2, true))) {
            return Fail("failed to seed manual owners before backend transition");
        }
        OW::Config::kmboxEnabled = false;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("manual ownership backend disable reconcile failed");
        }
        const auto afterBackendTransition = kmbox::MockHardwareMgr.Snapshot();
        if (scheduler.ActiveActionCount() != 0 ||
            afterBackendTransition.outputMouseButtons != 0 ||
            afterBackendTransition.outputKeyboardKeys != 0) {
            return Fail("backend transition did not synchronously zero manual outputs");
        }
        const auto beforeDisabledWrite = kmbox::MockHardwareMgr.Snapshot();
        if (OW::SendMouseButtonActionState(
                0, true, OW::OutputOwnerSource::GlobalAim, globalTransition) !=
            OW::ActionOutputStatus::Disabled) {
            return Fail("disabled backend did not reject replacement manual output");
        }
        if (kmbox::MockHardwareMgr.Snapshot().totalEvents !=
            beforeDisabledWrite.totalEvents) {
            return Fail("disabled backend attempt emitted a mock write");
        }

        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("manual ownership mock re-enable reconcile failed");
        }
        if (!scheduler.SynchronizeRuntime())
            return Fail("manual ownership scheduler did not accept new generation");
        return EXIT_SUCCESS;
    }

    int VerifyChargeReleasePhysicalFallback()
    {
        OW::OutputScheduler& scheduler = OW::RuntimeOutputScheduler();
        scheduler.CancelAllAndJoin();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;
        if (!scheduler.SynchronizeRuntime())
            return Fail("charge-release fallback could not synchronize runtime");

        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed physical passthrough button");
        const auto beforeFallback = kmbox::MockHardwareMgr.Snapshot();
        AimbotDetail::ReleaseDmaMouseKey(
            0x1u,
            0,
            false,
            OW::OutputOwnerSource::GlobalAim,
            "selftest.charge_release.fallback");
        const auto afterFallback = kmbox::MockHardwareMgr.Snapshot();
        if (afterFallback.outputMouseButtons != 0 ||
            afterFallback.buttonEvents != beforeFallback.buttonEvents + 1) {
            return Fail("ownerless charge release did not emit one physical up");
        }
        {
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (events.empty() ||
                events.back().type != kmbox::MockEventType::ButtonStateMask ||
                events.back().stateMask != 0) {
                return Fail("ownerless charge release emitted the wrong fallback event");
            }
        }

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed physical button for mask-failure guard");
        const auto beforeMaskFailureGuard =
            kmbox::MockHardwareMgr.Snapshot();
        const AimbotDetail::MouseOwnerReleaseResult guardedRelease =
            AimbotDetail::ReleaseMouseOwnerAfterPhysicalMask(
                0,
                OW::OutputOwnerSource::GlobalAim,
                "selftest.charge_release.mask_failed",
                kmbox::ActiveRuntimeSnapshot().generation,
                true,
                false);
        const auto afterMaskFailureGuard =
            kmbox::MockHardwareMgr.Snapshot();
        if (!OW::ActionOutputSucceeded(guardedRelease.ownerReleaseStatus) ||
            guardedRelease.physicalFallbackReleased ||
            afterMaskFailureGuard.outputMouseButtons != 0x01u ||
            afterMaskFailureGuard.totalEvents !=
                beforeMaskFailureGuard.totalEvents) {
            return Fail("failed physical mask allowed ownerless force release");
        }
        if (kmbox::DispatchForceReleaseMouseButtonForGeneration(
                kmbox::ActiveRuntimeSnapshot().generation,
                0) != success) {
            return Fail("mask-failure guard fixture cleanup failed");
        }

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        constexpr const char* sameOwnerKey =
            "selftest.charge_release.same_owner";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0,
                true,
                OW::OutputOwnerSource::GlobalAim,
                sameOwnerKey))) {
            return Fail("failed to seed same owner before combined release");
        }
        bool sameOwnerFallbackExecuted = false;
        if (!OW::ActionOutputSucceeded(
                OW::ReleaseMouseButtonActionStateOrPhysicalFallback(
                    0,
                    OW::OutputOwnerSource::GlobalAim,
                    sameOwnerKey,
                    kmbox::ActiveRuntimeSnapshot().generation,
                    &sameOwnerFallbackExecuted))) {
            return Fail("combined release failed for its existing owner");
        }
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (sameOwnerFallbackExecuted ||
                snapshot.outputMouseButtons != 0 || events.size() != 2 ||
                events.back().type != kmbox::MockEventType::Button ||
                events.back().down) {
                return Fail("existing owner release incorrectly used force fallback");
            }
        }

        struct HeldOwnerCase
        {
            OW::OutputOwnerSource heldSource;
            const char* heldKey;
            OW::OutputOwnerSource fallbackSource;
            const char* fallbackKey;
        };
        constexpr HeldOwnerCase heldCases[] = {
            {
                OW::OutputOwnerSource::GlobalAim,
                "selftest.charge_release.held_global",
                OW::OutputOwnerSource::Trigger,
                "selftest.charge_release.fallback_trigger"
            },
            {
                OW::OutputOwnerSource::Trigger,
                "selftest.charge_release.held_trigger",
                OW::OutputOwnerSource::GlobalAim,
                "selftest.charge_release.fallback_global"
            },
            {
                OW::OutputOwnerSource::Sequence,
                "selftest.charge_release.held_sequence",
                OW::OutputOwnerSource::GlobalAim,
                "selftest.charge_release.fallback_sequence"
            }
        };

        for (const HeldOwnerCase& testCase : heldCases) {
            scheduler.CancelAllAndJoin();
            kmbox::MockHardwareMgr.Reset();
            if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                    0,
                    true,
                    testCase.heldSource,
                    testCase.heldKey))) {
                return Fail("failed to seed generated owner for fallback guard");
            }
            const auto beforeGuardedRelease =
                kmbox::MockHardwareMgr.Snapshot();
            AimbotDetail::ReleaseDmaMouseKey(
                0x1u,
                0,
                false,
                testCase.fallbackSource,
                testCase.fallbackKey);
            const auto afterGuardedRelease =
                kmbox::MockHardwareMgr.Snapshot();
            if (afterGuardedRelease.outputMouseButtons != 0x01u ||
                afterGuardedRelease.buttonEvents !=
                    beforeGuardedRelease.buttonEvents) {
                return Fail("physical fallback stole a generated owner lease");
            }
            if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                    0,
                    false,
                    testCase.heldSource,
                    testCase.heldKey)) ||
                kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0) {
                return Fail("generated fallback-guard owner did not clean up");
            }
        }

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed combined-operation race button");
        constexpr const char* raceOwnerKey =
            "selftest.charge_release.operation_race";
        const OW::OutputControl raceControl{
            OW::OutputControlKind::MouseButton, 0
        };
        std::atomic<bool> callbackEntered{ false };
        std::atomic<bool> allowCallback{ false };
        std::atomic<bool> acquireFinished{ false };
        bool fallbackResult = false;
        bool acquireResult = false;
        std::thread fallbackThread([&]() {
            fallbackResult =
                scheduler.ReleaseManualControlOrExecuteIfUnowned(
                    raceOwnerKey,
                    OW::OutputOwnerSource::GlobalAim,
                    raceControl,
                    [&](std::uint64_t backendGeneration) {
                        callbackEntered.store(true, std::memory_order_release);
                        while (!allowCallback.load(std::memory_order_acquire))
                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(1));
                        return kmbox::DispatchForceReleaseMouseButtonForGeneration(
                            backendGeneration,
                            0) == success;
                    });
        });
        const auto callbackDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(1);
        while (!callbackEntered.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < callbackDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::thread acquireThread([&]() {
            acquireResult = scheduler.SetManualControl(
                raceOwnerKey,
                OW::OutputOwnerSource::GlobalAim,
                raceControl,
                true);
            acquireFinished.store(true, std::memory_order_release);
        });
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const bool acquireCrossedOperation =
            acquireFinished.load(std::memory_order_acquire);
        allowCallback.store(true, std::memory_order_release);
        fallbackThread.join();
        acquireThread.join();
        if (!callbackEntered.load(std::memory_order_acquire) ||
            acquireCrossedOperation || !fallbackResult || !acquireResult) {
            return Fail("combined fallback did not serialize a racing acquire");
        }
        {
            const auto snapshot = kmbox::MockHardwareMgr.Snapshot();
            const auto events = kmbox::MockHardwareMgr.RecentEvents();
            if (snapshot.outputMouseButtons != 0x01u || events.size() != 3 ||
                events[1].type != kmbox::MockEventType::ButtonStateMask ||
                events[1].stateMask != 0 ||
                events[2].type != kmbox::MockEventType::Button ||
                !events[2].down) {
                return Fail("racing acquire did not occur after physical fallback");
            }
        }
        if (!scheduler.SetManualControl(
                raceOwnerKey,
                OW::OutputOwnerSource::GlobalAim,
                raceControl,
                false)) {
            return Fail("failed to clean up combined-operation race owner");
        }

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed button for stale-generation fallback");
        const std::uint64_t generation =
            kmbox::ActiveRuntimeSnapshot().generation;
        const auto beforeStaleFallback = kmbox::MockHardwareMgr.Snapshot();
        if (kmbox::DispatchForceReleaseMouseButtonForGeneration(
                generation + 1, 0) != err_queue_stopped) {
            return Fail("stale generation physical fallback was accepted");
        }
        const auto afterStaleFallback = kmbox::MockHardwareMgr.Snapshot();
        if (afterStaleFallback.outputMouseButtons != 0x01u ||
            afterStaleFallback.buttonEvents != beforeStaleFallback.buttonEvents) {
            return Fail("stale generation physical fallback emitted a write");
        }
        if (kmbox::DispatchForceReleaseMouseButtonForGeneration(
                generation, 0) != success ||
            kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0) {
            return Fail("current generation physical fallback did not release");
        }

        const std::uint64_t oldOperationGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        OW::Config::kmboxEnabled = false;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("charge-release generation disable reconcile failed");
        }
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("charge-release generation re-enable reconcile failed");
        }
        if (!scheduler.SynchronizeRuntime())
            return Fail("charge-release replacement generation was not ready");
        const std::uint64_t replacementGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        if (replacementGeneration == oldOperationGeneration)
            return Fail("charge-release runtime switch did not advance generation");

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        constexpr const char* replacementOwnerKey =
            "selftest.charge_release.replacement_owner";
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0,
                true,
                OW::OutputOwnerSource::GlobalAim,
                replacementOwnerKey))) {
            return Fail("failed to seed replacement-generation owner");
        }
        const auto beforeStaleOperation = kmbox::MockHardwareMgr.Snapshot();
        AimbotDetail::ReleaseDmaMouseKey(
            0x1u,
            0,
            false,
            OW::OutputOwnerSource::GlobalAim,
            replacementOwnerKey,
            oldOperationGeneration);
        const auto afterStaleOperation = kmbox::MockHardwareMgr.Snapshot();
        if (afterStaleOperation.totalEvents != beforeStaleOperation.totalEvents ||
            afterStaleOperation.outputMouseButtons != 0x01u ||
            !scheduler.IsActive(replacementOwnerKey)) {
            return Fail("stale charge-release operation touched replacement backend");
        }
        if (!OW::ActionOutputSucceeded(OW::SendMouseButtonActionState(
                0,
                false,
                OW::OutputOwnerSource::GlobalAim,
                replacementOwnerKey))) {
            return Fail("replacement-generation owner cleanup failed");
        }

        scheduler.CancelAllAndJoin();
        kmbox::MockHardwareMgr.Reset();
        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed button for menu fallback boundary");
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        const auto beforeMenuFallback = kmbox::MockHardwareMgr.Snapshot();
        AimbotDetail::ReleaseDmaMouseKey(
            0x1u,
            0,
            false,
            OW::OutputOwnerSource::GlobalAim,
            "selftest.charge_release.menu_fallback");
        const auto afterMenuFallback = kmbox::MockHardwareMgr.Snapshot();
        if (afterMenuFallback.outputMouseButtons != 0x01u ||
            afterMenuFallback.buttonEvents != beforeMenuFallback.buttonEvents) {
            return Fail("menu boundary allowed a physical fallback write");
        }

        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        if (kmbox::MockHardwareMgr.RecordButton(0, false) != success)
            return Fail("failed to clean up menu fallback seed");
        return EXIT_SUCCESS;
    }

    int VerifyPhysicalMouseMaskLeaseCoordinator()
    {
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;
        if (!OW::RetryPendingPhysicalMouseMaskLeases() ||
            OW::PhysicalMouseMaskAggregate() != 0) {
            return Fail("physical mask coordinator was not clean at test start");
        }

        constexpr const char* zaryaSameBit =
            "selftest.mask.zarya.same_bit";
        constexpr const char* chargeSameBit =
            "selftest.mask.charge.same_bit";
        kmbox::MockHardwareMgr.Reset();
        if (!OW::AcquirePhysicalMouseMaskLease(
                zaryaSameBit,
                OW::OutputOwnerSource::ZaryaPulse,
                0x01u)) {
            return Fail("Zarya same-bit mask lease acquire failed");
        }
        const auto afterZaryaMask = kmbox::MockHardwareMgr.Snapshot();
        if (!OW::AcquirePhysicalMouseMaskLease(
                chargeSameBit,
                OW::OutputOwnerSource::GlobalAim,
                0x01u)) {
            return Fail("charge same-bit mask lease acquire failed");
        }
        const auto afterChargeMask = kmbox::MockHardwareMgr.Snapshot();
        if (afterChargeMask.maskedButtons != 0x01u ||
            afterChargeMask.maskEvents != afterZaryaMask.maskEvents) {
            return Fail("same-bit overlap emitted a duplicate hardware mask");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                chargeSameBit,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("charge same-bit mask lease release failed");
        }
        const auto afterChargeRelease = kmbox::MockHardwareMgr.Snapshot();
        if (afterChargeRelease.maskedButtons != 0x01u ||
            afterChargeRelease.maskEvents != afterChargeMask.maskEvents) {
            return Fail("partial same-bit release unmasked the Zarya lease");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                zaryaSameBit,
                OW::OutputOwnerSource::ZaryaPulse)) {
            return Fail("final same-bit mask lease release failed");
        }
        const auto afterZaryaRelease = kmbox::MockHardwareMgr.Snapshot();
        if (afterZaryaRelease.maskedButtons != 0 ||
            afterZaryaRelease.maskEvents != afterChargeRelease.maskEvents + 1) {
            return Fail("final same-bit release did not unmask exactly once");
        }

        constexpr const char* leftOwner =
            "selftest.mask.partial.left";
        constexpr const char* rightOwner =
            "selftest.mask.partial.right";
        kmbox::MockHardwareMgr.Reset();
        if (!OW::AcquirePhysicalMouseMaskLease(
                leftOwner,
                OW::OutputOwnerSource::ZaryaPulse,
                0x01u) ||
            !OW::AcquirePhysicalMouseMaskLease(
                rightOwner,
                OW::OutputOwnerSource::GlobalAim,
                0x02u)) {
            return Fail("different-bit mask lease acquire failed");
        }
        const auto beforePartialReduction =
            kmbox::MockHardwareMgr.Snapshot();
        if (beforePartialReduction.maskedButtons != 0x03u)
            return Fail("different-bit leases did not aggregate");
        if (!OW::ReleasePhysicalMouseMaskLease(
                rightOwner,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("different-bit partial release failed");
        }
        const auto afterPartialReduction =
            kmbox::MockHardwareMgr.Snapshot();
        if (afterPartialReduction.maskedButtons != 0x01u ||
            afterPartialReduction.maskEvents !=
                beforePartialReduction.maskEvents + 2) {
            return Fail("partial reduction did not unmask then remask remainder");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                leftOwner,
                OW::OutputOwnerSource::ZaryaPulse)) {
            return Fail("different-bit final release failed");
        }

        bool failNextLeftRemask = false;
        int selectiveMaskDispatchCalls = 0;
        OW::PhysicalMouseMaskLeaseCoordinator selectiveFailureCoordinator(
            [&](std::uint64_t generation, std::uint32_t mask) {
                ++selectiveMaskDispatchCalls;
                if (failNextLeftRemask && mask == 0x01u) {
                    failNextLeftRemask = false;
                    return static_cast<int>(err_net_rx_timeout);
                }
                return kmbox::DispatchMouseMaskForGeneration(
                    generation,
                    mask);
            },
            [](std::uint64_t generation) {
                return kmbox::DispatchMouseUnmaskForGeneration(generation);
            },
            []() { return OW::CurrentKmboxOutputRuntimeState(); });

        kmbox::MockHardwareMgr.Reset();
        if (!selectiveFailureCoordinator.Acquire(
                "selftest.mask.pending.left",
                OW::OutputOwnerSource::ZaryaPulse,
                0x01u) ||
            !selectiveFailureCoordinator.Acquire(
                "selftest.mask.pending.right",
                OW::OutputOwnerSource::GlobalAim,
                0x02u)) {
            return Fail("pending-remask fixture acquire failed");
        }
        failNextLeftRemask = true;
        if (selectiveFailureCoordinator.Release(
                "selftest.mask.pending.right",
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("selective remask failure incorrectly reported success");
        }
        if (!selectiveFailureCoordinator.HasPending() ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0) {
            return Fail("failed remask was not retained as pending");
        }
        if (!selectiveFailureCoordinator.RetryPending() ||
            selectiveFailureCoordinator.HasPending() ||
            selectiveFailureCoordinator.AggregateMask() != 0x01u ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0x01u) {
            return Fail("pending remask did not rebuild the remaining aggregate");
        }
        if (!selectiveFailureCoordinator.Release(
                "selftest.mask.pending.left",
                OW::OutputOwnerSource::ZaryaPulse)) {
            return Fail("pending-remask fixture cleanup failed");
        }

        kmbox::MockHardwareMgr.Reset();
        if (!selectiveFailureCoordinator.Acquire(
                "selftest.mask.final_release.left",
                OW::OutputOwnerSource::ZaryaPulse,
                0x01u) ||
            !selectiveFailureCoordinator.Acquire(
                "selftest.mask.final_release.right",
                OW::OutputOwnerSource::GlobalAim,
                0x02u)) {
            return Fail("pending final-release fixture acquire failed");
        }
        failNextLeftRemask = true;
        if (selectiveFailureCoordinator.Release(
                "selftest.mask.final_release.right",
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("pending final-release remask failure reported success");
        }
        const int maskCallsBeforeFinalRelease = selectiveMaskDispatchCalls;
        if (!selectiveFailureCoordinator.Release(
                "selftest.mask.final_release.left",
                OW::OutputOwnerSource::ZaryaPulse) ||
            selectiveFailureCoordinator.HasPending() ||
            selectiveFailureCoordinator.AggregateMask() != 0 ||
            selectiveMaskDispatchCalls != maskCallsBeforeFinalRelease ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0) {
            return Fail("final release rebuilt an owner that was being removed");
        }

        constexpr const char* failedUnmaskOwner =
            "selftest.mask.pending.unmask_failure";
        kmbox::MockHardwareMgr.Reset();
        if (!OW::AcquirePhysicalMouseMaskLease(
                failedUnmaskOwner,
                OW::OutputOwnerSource::GlobalAim,
                0x01u)) {
            return Fail("failed-unmask fixture acquire failed");
        }
        kmbox::MockHardwareMgr.SetFaultMode(
            kmbox::MockFaultMode::OutputTimeout);
        if (OW::ReleasePhysicalMouseMaskLease(
                failedUnmaskOwner,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("failed unmask incorrectly reported success");
        }
        if (!OW::RuntimePhysicalMouseMaskLeases().HasPending() ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0x01u) {
            return Fail("failed unmask did not retain pending cleanup");
        }
        kmbox::MockHardwareMgr.SetFaultMode(kmbox::MockFaultMode::None);
        if (!OW::RetryPendingPhysicalMouseMaskLeases() ||
            OW::RuntimePhysicalMouseMaskLeases().HasPending() ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0) {
            return Fail("failed unmask pending cleanup did not retry");
        }

        constexpr const char* menuPartialLeft =
            "selftest.mask.menu.partial_left";
        constexpr const char* menuPartialRight =
            "selftest.mask.menu.partial_right";
        kmbox::MockHardwareMgr.Reset();
        if (!OW::AcquirePhysicalMouseMaskLease(
                menuPartialLeft,
                OW::OutputOwnerSource::ZaryaPulse,
                0x01u) ||
            !OW::AcquirePhysicalMouseMaskLease(
                menuPartialRight,
                OW::OutputOwnerSource::GlobalAim,
                0x02u)) {
            return Fail("menu partial-reduction fixture acquire failed");
        }
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        const auto beforeMenuPartialRelease =
            kmbox::MockHardwareMgr.Snapshot();
        if (OW::ReleasePhysicalMouseMaskLease(
                menuPartialRight,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("closed menu gate reported partial remask success");
        }
        const auto afterMenuPartialRelease =
            kmbox::MockHardwareMgr.Snapshot();
        if (afterMenuPartialRelease.maskedButtons != 0 ||
            afterMenuPartialRelease.maskEvents !=
                beforeMenuPartialRelease.maskEvents + 1 ||
            !OW::RuntimePhysicalMouseMaskLeases().HasPending()) {
            return Fail("menu partial release did not safety-unmask into pending");
        }
        if (OW::RetryPendingPhysicalMouseMaskLeases() ||
            kmbox::MockHardwareMgr.Snapshot().maskEvents !=
                afterMenuPartialRelease.maskEvents) {
            return Fail("closed menu gate retried a pending positive mask");
        }
        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        if (!OW::RetryPendingPhysicalMouseMaskLeases() ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0x01u ||
            OW::PhysicalMouseMaskAggregate() != 0x01u) {
            return Fail("reopened menu gate did not rebuild pending mask");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                menuPartialLeft,
                OW::OutputOwnerSource::ZaryaPulse)) {
            return Fail("menu partial-reduction fixture cleanup failed");
        }

        constexpr const char* menuOwner =
            "selftest.mask.menu.owner";
        constexpr const char* blockedMenuOwner =
            "selftest.mask.menu.blocked";
        kmbox::MockHardwareMgr.Reset();
        if (!OW::AcquirePhysicalMouseMaskLease(
                menuOwner,
                OW::OutputOwnerSource::GlobalAim,
                0x01u)) {
            return Fail("menu-boundary mask fixture acquire failed");
        }
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;
        const auto beforeBlockedMenuAcquire =
            kmbox::MockHardwareMgr.Snapshot();
        if (OW::AcquirePhysicalMouseMaskLease(
                blockedMenuOwner,
                OW::OutputOwnerSource::Trigger,
                0x02u)) {
            return Fail("closed menu gate accepted a new mask lease");
        }
        if (kmbox::MockHardwareMgr.Snapshot().maskEvents !=
            beforeBlockedMenuAcquire.maskEvents) {
            return Fail("closed menu gate emitted a mask write");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                menuOwner,
                OW::OutputOwnerSource::GlobalAim) ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0) {
            return Fail("closed menu gate blocked an existing mask release");
        }
        OW::Config::Menu = false;
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;

        constexpr const char* staleOwner =
            "selftest.mask.generation.stale";
        kmbox::MockHardwareMgr.Reset();
        const std::uint64_t staleMaskGeneration =
            kmbox::ActiveRuntimeSnapshot().generation;
        if (!OW::AcquirePhysicalMouseMaskLease(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim,
                0x01u,
                staleMaskGeneration)) {
            return Fail("stale-generation mask fixture acquire failed");
        }
        OW::Config::kmboxEnabled = false;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("mask coordinator disable reconcile failed");
        }
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("mask coordinator re-enable reconcile failed");
        }
        if (!OW::RuntimeOutputScheduler().SynchronizeRuntime())
            return Fail("mask coordinator replacement generation was not ready");
        const auto beforeStaleCleanup = kmbox::MockHardwareMgr.Snapshot();
        if (OW::AcquirePhysicalMouseMaskLease(
                "selftest.mask.generation.stale_acquire",
                OW::OutputOwnerSource::Trigger,
                0x02u,
                staleMaskGeneration)) {
            return Fail("stale generation acquired a replacement mask lease");
        }
        if (kmbox::MockHardwareMgr.Snapshot().totalEvents !=
            beforeStaleCleanup.totalEvents) {
            return Fail("stale mask acquire emitted a replacement-backend event");
        }
        if (!OW::RetryPendingPhysicalMouseMaskLeases())
            return Fail("stale-generation mask model cleanup failed");
        const auto afterStaleCleanup = kmbox::MockHardwareMgr.Snapshot();
        if (afterStaleCleanup.maskEvents != beforeStaleCleanup.maskEvents ||
            OW::PhysicalMouseMaskAggregate() != 0 ||
            OW::IsPhysicalMouseMaskLeaseActive(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("stale mask model touched the replacement backend");
        }

        if (!OW::AcquirePhysicalMouseMaskLease(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim,
                0x01u)) {
            return Fail("replacement-generation mask owner acquire failed");
        }
        const auto beforeStaleRelease = kmbox::MockHardwareMgr.Snapshot();
        if (OW::ReleasePhysicalMouseMaskLease(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim,
                staleMaskGeneration)) {
            return Fail("stale generation released a replacement mask lease");
        }
        const auto afterStaleRelease = kmbox::MockHardwareMgr.Snapshot();
        if (afterStaleRelease.totalEvents != beforeStaleRelease.totalEvents ||
            afterStaleRelease.maskedButtons != 0x01u ||
            !OW::IsPhysicalMouseMaskLeaseActive(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim)) {
            return Fail("stale mask release touched replacement backend state");
        }
        if (!OW::ReleasePhysicalMouseMaskLease(
                staleOwner,
                OW::OutputOwnerSource::GlobalAim) ||
            kmbox::MockHardwareMgr.Snapshot().maskedButtons != 0) {
            return Fail("replacement-generation mask owner cleanup failed");
        }

        return EXIT_SUCCESS;
    }

    int VerifyProcessOutputSessionBoundary()
    {
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;
        OW::ProcessConnection::SetStatus(
            true,
            false,
            1001,
            0x100000,
            "MockHardwareSelfTest connected");
        OW::SetRuntimeOutputSessionEnabled(true);
        kmbox::MockHardwareMgr.Reset();

        OW::OutputScheduler& scheduler = OW::RuntimeOutputScheduler();
        scheduler.CancelAllAndJoin(OW::OutputActionCancelReason::RuntimeChanged);
        if (!scheduler.SynchronizeRuntime())
            return Fail("process-session scheduler did not accept active Mock runtime");

        constexpr const char* ownerKey =
            "selftest.process_session.old_generation";
        constexpr OW::OutputControl eControl{
            OW::OutputControlKind::KeyboardUsage, KEY_E };
        const auto beforeDisconnect = kmbox::ActiveRuntimeSnapshot();
        if (!scheduler.SetManualControlsForGeneration(
                ownerKey,
                OW::OutputOwnerSource::Sequence,
                { eControl },
                beforeDisconnect.generation) ||
            kmbox::MockHardwareMgr.Snapshot().outputKeyboardUsages[0] != KEY_E) {
            return Fail("process-session old-generation fixture did not press E");
        }

        // Match MarkProcessDisconnected ordering: close the session first,
        // cancel generated owners, then perform the transport-wide all-up.
        OW::SetRuntimeOutputSessionEnabled(false);
        OW::ProcessConnection::SetStatus(
            false,
            false,
            0,
            0,
            "MockHardwareSelfTest disconnected");
        scheduler.CancelAllAndJoin(OW::OutputActionCancelReason::RuntimeChanged);
        if (kmbox::ReleaseActiveRuntime(std::chrono::milliseconds(50)) != success)
            return Fail("process-session ReleaseActive failed");

        const auto afterDisconnect = kmbox::ActiveRuntimeSnapshot();
        const OW::OutputRuntimeState closedState =
            OW::CurrentKmboxOutputRuntimeState();
        if (afterDisconnect.generation == beforeDisconnect.generation ||
            closedState.backendGeneration != afterDisconnect.generation ||
            closedState.outputGateOpen ||
            !kmbox::CanDispatchRuntimeGeneration(afterDisconnect.generation) ||
            kmbox::CanDispatchRuntimeGeneration(beforeDisconnect.generation) ||
            kmbox::MockHardwareMgr.Snapshot().outputKeyboardKeys != 0) {
            return Fail("disconnect cleanup did not close session and rotate generation");
        }

        const auto beforeStaleOutput = kmbox::MockHardwareMgr.Snapshot();
        if (scheduler.SetManualControlsForGeneration(
                ownerKey,
                OW::OutputOwnerSource::Sequence,
                { eControl },
                beforeDisconnect.generation) ||
            scheduler.ScheduleTimedHold(
                "selftest.process_session.closed_timed",
                OW::OutputOwnerSource::HeroTimedAction,
                { eControl },
                std::chrono::milliseconds(20)) ||
            kmbox::DispatchMouseButtonForGeneration(
                beforeDisconnect.generation,
                0,
                true) != err_queue_stopped ||
            OW::EnqueueKmboxPixelMove(3, 4, 0) !=
                OW::Config::kKmboxOutputSuppressedStatus ||
            kmbox::DispatchMouseMoveForGeneration(
                beforeDisconnect.generation,
                3,
                4,
                0) != err_queue_stopped) {
            return Fail("closed process session accepted an old producer");
        }
        const auto afterStaleOutput = kmbox::MockHardwareMgr.Snapshot();
        if (afterStaleOutput.totalEvents != beforeStaleOutput.totalEvents)
            return Fail("closed process session emitted a stale output event");

        // Successful SDK reconnect reopens the process session. The scheduler
        // must explicitly synchronize to, and only accept, the new generation.
        OW::ProcessConnection::SetStatus(
            true,
            false,
            1002,
            0x200000,
            "MockHardwareSelfTest reconnected");
        OW::SetRuntimeOutputSessionEnabled(true);
        const auto beforeReconnectedMove = kmbox::MockHardwareMgr.Snapshot();
        if (!scheduler.SynchronizeRuntime() ||
            scheduler.SetManualControlsForGeneration(
                ownerKey,
                OW::OutputOwnerSource::Sequence,
                { eControl },
                beforeDisconnect.generation) ||
            !scheduler.SetManualControlsForGeneration(
                ownerKey,
                OW::OutputOwnerSource::Sequence,
                { eControl },
                afterDisconnect.generation) ||
            !scheduler.SetManualControlsForGeneration(
                ownerKey,
                OW::OutputOwnerSource::Sequence,
                {},
                afterDisconnect.generation) ||
            OW::EnqueueKmboxPixelMove(5, 6, 0) != success ||
            kmbox::DispatchMouseMoveForGeneration(
                beforeDisconnect.generation,
                7,
                8,
                0) != err_queue_stopped) {
            scheduler.CancelAllAndJoin();
            return Fail("reconnected process session did not accept only the new generation");
        }

        const auto afterReconnectedMove = kmbox::MockHardwareMgr.Snapshot();
        if (afterReconnectedMove.moveEvents !=
                beforeReconnectedMove.moveEvents + 1) {
            scheduler.CancelAllAndJoin();
            return Fail("reconnected process session move was not generation-bound");
        }

        constexpr const char* clickOwner =
            "selftest.process_session.generation_bound_click";
        if (OW::SendMouseButtonActionState(
                0,
                true,
                OW::OutputOwnerSource::GlobalAim,
                clickOwner,
                beforeDisconnect.generation) !=
                OW::ActionOutputStatus::TransportError ||
            OW::SendMouseButtonActionState(
                0,
                true,
                OW::OutputOwnerSource::GlobalAim,
                clickOwner,
                afterDisconnect.generation) !=
                OW::ActionOutputStatus::Sent ||
            OW::SendMouseButtonActionState(
                0,
                false,
                OW::OutputOwnerSource::GlobalAim,
                clickOwner,
                beforeDisconnect.generation) !=
                OW::ActionOutputStatus::TransportError ||
            (kmbox::MockHardwareMgr.Snapshot().outputMouseButtons & 0x01u) == 0 ||
            OW::SendMouseButtonActionState(
                0,
                false,
                OW::OutputOwnerSource::GlobalAim,
                clickOwner,
                afterDisconnect.generation) !=
                OW::ActionOutputStatus::Sent ||
            kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != 0) {
            scheduler.CancelAllAndJoin();
            return Fail("stale click release affected the reconnected generation");
        }

        scheduler.CancelAllAndJoin();
        if (kmbox::MockHardwareMgr.Snapshot().outputKeyboardKeys != 0)
            return Fail("reconnected process-session fixture leaked keyboard output");
        return EXIT_SUCCESS;
    }

    int VerifyRuntimeSchedulerTransitionBoundary()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
        OW::Config::Menu = false;
        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;

        OW::OutputScheduler& scheduler = OW::RuntimeOutputScheduler();
        if (!scheduler.SynchronizeRuntime())
            return Fail("runtime scheduler did not accept active mock generation");

        const auto initial = kmbox::ActiveRuntimeSnapshot();
        const std::uint64_t initialTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (!scheduler.ScheduleTimedHold(
                "mock-runtime-switch-hold",
                OW::OutputOwnerSource::HeroTimedAction,
                { { OW::OutputControlKind::KeyboardUsage, KEY_E } },
                std::chrono::seconds(5))) {
            return Fail("runtime scheduler failed to start long mock hold");
        }
        if (kmbox::MockHardwareMgr.Snapshot().outputKeyboardUsages[0] != KEY_E)
            return Fail("runtime scheduler did not press the scheduled key");

        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("same-descriptor runtime reconcile failed");
        }
        const auto afterNoOp = kmbox::ActiveRuntimeSnapshot();
        if (afterNoOp.generation != initial.generation ||
            OW::RuntimeOutputTransitionEpoch() != initialTransitionEpoch ||
            !scheduler.IsActive("mock-runtime-switch-hold")) {
            return Fail("no-op reconcile cancelled or regenerated active output");
        }

        OW::Config::kmboxEnabled = false;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("runtime disable reconcile failed");
        }
        const auto disabled = kmbox::MockHardwareMgr.Snapshot();
        if (scheduler.ActiveActionCount() != 0 ||
            scheduler.PendingDeadlineCount() != 0 ||
            scheduler.WorkerCount() != 0 ||
            disabled.outputKeyboardKeys != 0 ||
            OW::RuntimeOutputTransitionEpoch() == initialTransitionEpoch) {
            return Fail("runtime disable did not synchronously cancel timed output");
        }
        const std::uint64_t disabledTransitionEpoch =
            OW::RuntimeOutputTransitionEpoch();
        if (scheduler.ScheduleTimedHold(
                "mock-disabled-hold",
                OW::OutputOwnerSource::HeroTimedAction,
                { { OW::OutputControlKind::KeyboardUsage, KEY_W } },
                std::chrono::milliseconds(20))) {
            return Fail("paused scheduler accepted output while runtime disabled");
        }

        OW::Config::kmboxEnabled = true;
        OW::Config::kmboxDeviceType = 2;
        if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) !=
            success) {
            return Fail("runtime mock re-enable reconcile failed");
        }
        if (OW::RuntimeOutputTransitionEpoch() == disabledTransitionEpoch)
            return Fail("runtime re-enable did not advance the producer transition epoch");
        if (!scheduler.SynchronizeRuntime())
            return Fail("runtime scheduler did not accept replacement generation");
        if (!scheduler.ScheduleTimedHold(
                "mock-reenabled-hold",
                OW::OutputOwnerSource::HeroTimedAction,
                { { OW::OutputControlKind::KeyboardUsage, KEY_W } },
                std::chrono::milliseconds(20))) {
            return Fail("scheduler did not resume on replacement generation");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        scheduler.CancelAllAndJoin();
        if (kmbox::MockHardwareMgr.Snapshot().outputKeyboardKeys != 0 ||
            scheduler.WorkerCount() != 0) {
            return Fail("replacement-generation timed output did not release");
        }

        return EXIT_SUCCESS;
    }
}

int main()
{
    Diagnostics::Initialize(Diagnostics::LogLevel::Error, "NUL");
    Diagnostics::InitializeAimLog("NUL");

    if (VerifyKmboxMonitorPortNormalization() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyMagneticTriggerQuantizationRules() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    ConfigureMockRuntime();
    if (kmbox::ReconcileRuntimeFromConfig(std::chrono::milliseconds(50)) != success)
        return Fail("mock runtime reconcile returned failure");

    if (!kmbox::MockHardwareMgr.IsInitialized())
        return Fail("mock is not initialized");

    if (VerifyKeyboardReportBuilder() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyFullKeyboardReportRuntime() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyManualOutputOwnership() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyChargeReleasePhysicalFallback() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyPhysicalMouseMaskLeaseCoordinator() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyProcessOutputSessionBoundary() != EXIT_SUCCESS)
        return EXIT_FAILURE;
    if (VerifyRuntimeSchedulerTransitionBoundary() != EXIT_SUCCESS)
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
        OW::ResetMouseMoveQuantizationState(
            OW::DefaultMouseMoveQuantizationState());
        const float yawCountsPerRadian =
            OW::Config::KmboxYawCountsPerRadian();
        if (!std::isfinite(yawCountsPerRadian) ||
            yawCountsPerRadian <= 0.0f) {
            return Fail("default mouse quantization test has invalid sensitivity");
        }
        const OW::Vector3 fractionalYaw(
            0.0f,
            -0.4f / yawCountsPerRadian,
            0.0f);
        const kmbox::MockHardwareSnapshot before =
            kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(fractionalYaw, 0);
        OW::SendMouseMove(fractionalYaw, 0);
        const kmbox::MockHardwareSnapshot afterTwo =
            kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(fractionalYaw, 0);
        const kmbox::MockHardwareSnapshot afterThree =
            kmbox::MockHardwareMgr.Snapshot();
        if (afterTwo.moveEvents != before.moveEvents ||
            afterThree.moveEvents != before.moveEvents + 1) {
            return Fail("default SendMouseMove fractional accumulation changed");
        }
        const auto events = kmbox::MockHardwareMgr.RecentEvents();
        if (events.empty() ||
            events.back().type != kmbox::MockEventType::Move ||
            events.back().x != 1 || events.back().y != 0) {
            return Fail("default SendMouseMove emitted the wrong accumulated count");
        }
        OW::ResetMouseMoveQuantizationState(
            OW::DefaultMouseMoveQuantizationState());
    }

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
