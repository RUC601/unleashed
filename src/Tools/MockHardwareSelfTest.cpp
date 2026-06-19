#include "Game/Overwatch.hpp"
#include "Game/Target.hpp"
#include "Kmbox/KmBoxMock.h"
#include "Utils/Diagnostics.hpp"

#include <Windows.h>

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
        OW::Config::inputSource = 4;
        OW::Config::kmboxPort = 8808;
        OW::Config::kmboxMonitorPort = 8809;
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

        return EXIT_SUCCESS;
    }

    int VerifyKmboxOutputSuppressionWhileMenuOpen()
    {
        kmbox::MockHardwareMgr.Reset();
        OW::Config::kmboxSuppressOutputWhileMenuOpen = true;
        OW::Config::Menu = true;

        const kmbox::MockHardwareSnapshot before = kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(OW::Vector3(0.01f, -0.01f, 0.0f), 0);
        OW::SendMouseButton(0, true);
        if (OW::SendMouseButtonStateMask(0x3u, true))
            return Fail("button state mask reported success while menu suppression was active");

        const kmbox::MockHardwareSnapshot after = kmbox::MockHardwareMgr.Snapshot();
        if (after.moveEvents != before.moveEvents)
            return Fail("menu suppression did not block mock mouse move");
        if (after.buttonEvents != before.buttonEvents)
            return Fail("menu suppression did not block mock mouse button");
        if (after.outputMouseButtons != before.outputMouseButtons)
            return Fail("menu suppression changed mock button state");

        if (kmbox::MockHardwareMgr.RecordButton(0, true) != success)
            return Fail("failed to seed mock button state for force-release suppression");
        const kmbox::MockHardwareSnapshot held = kmbox::MockHardwareMgr.Snapshot();
        OW::ForceReleaseMouseButtons();
        if (kmbox::MockHardwareMgr.Snapshot().outputMouseButtons != held.outputMouseButtons)
            return Fail("menu suppression did not block force-release output");

        OW::Config::Menu = false;
        kmbox::MockHardwareMgr.Reset();
        const kmbox::MockHardwareSnapshot unsuppressedBefore = kmbox::MockHardwareMgr.Snapshot();
        OW::SendMouseMove(OW::Vector3(0.01f, -0.01f, 0.0f), 0);
        const kmbox::MockHardwareSnapshot unsuppressedAfter = kmbox::MockHardwareMgr.Snapshot();
        if (unsuppressedAfter.moveEvents <= unsuppressedBefore.moveEvents)
            return Fail("KMBox output stayed blocked after menu closed");

        OW::Config::kmboxSuppressOutputWhileMenuOpen = false;
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
    if (kmbox::MockHardwareMgr.Initialize() != success)
        return Fail("initialize returned failure");

    if (!kmbox::MockHardwareMgr.IsInitialized())
        return Fail("mock is not initialized");

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

    if (VerifyKmboxOutputSuppressionWhileMenuOpen() != EXIT_SUCCESS)
        return EXIT_FAILURE;

    if (kmbox::MockHardwareMgr.RecordKeyboardKey(0x09, true) != success)
        return Fail("keyboard output record failed");
    if (kmbox::MockHardwareMgr.Snapshot().keyboardEvents == 0)
        return Fail("keyboard event was not counted");

    if (!kmbox::MockHardwareMgr.SetInputVk(VK_RBUTTON, true))
        return Fail("SetInputVk failed");
    if (!AimbotDetail::IsInputVkDown(VK_RBUTTON) || !AimbotDetail::IsInputVkDownQuiet(VK_RBUTTON))
        return Fail("OW input path did not read mock right button");
    kmbox::MockHardwareMgr.SetInputVk(VK_RBUTTON, false);
    if (AimbotDetail::IsInputVkDownQuiet(VK_RBUTTON))
        return Fail("OW quiet input path did not observe mock release");

    kmbox::MockHardwareMgr.Reset();
    if (!kmbox::MockHardwareMgr.SetInputVk(VK_XBUTTON1, true))
        return Fail("SetInputVk Mouse4 failed");
    if (!AimbotDetail::IsInputVkDown(VK_XBUTTON1) || !AimbotDetail::IsInputVkDownQuiet(VK_XBUTTON1))
        return Fail("OW input path did not read mock Mouse4");
    kmbox::MockHardwareMgr.SetInputVk(VK_XBUTTON1, false);
    if (AimbotDetail::IsInputVkDownQuiet(VK_XBUTTON1))
        return Fail("OW quiet input path did not observe mock Mouse4 release");

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

    kmbox::MockHardwareMgr.Reset();
    kmbox::MockHardwareMgr.SetInputVk(VK_LBUTTON, true);
    kmbox::MockHardwareMgr.SetFaultMode(kmbox::MockFaultMode::InputJitter);
    if (!AimbotDetail::IsInputVkDown(VK_LBUTTON))
        return Fail("InputJitter first read should be current true");
    if (!AimbotDetail::IsInputVkDown(VK_LBUTTON))
        return Fail("InputJitter second read should be current true");
    if (AimbotDetail::IsInputVkDown(VK_LBUTTON))
        return Fail("InputJitter third read should return previous false");
    if (!AimbotDetail::IsInputVkDown(VK_LBUTTON))
        return Fail("InputJitter fourth read should return current true");

    kmbox::MockHardwareMgr.Reset();
    kmbox::MockHardwareMgr.SetFaultMode(kmbox::MockFaultMode::StuckButtons);
    kmbox::MockHardwareMgr.SetInputVk(VK_LBUTTON, true);
    if (!AimbotDetail::IsInputVkDownQuiet(VK_LBUTTON))
        return Fail("StuckButtons did not read initial down");
    kmbox::MockHardwareMgr.SetInputVk(VK_LBUTTON, false);
    if (!AimbotDetail::IsInputVkDownQuiet(VK_LBUTTON))
        return Fail("StuckButtons did not hold released mouse button");
    kmbox::MockHardwareMgr.Reset();
    if (AimbotDetail::IsInputVkDownQuiet(VK_LBUTTON))
        return Fail("Reset did not clear stuck input state");

    kmbox::MockHardwareMgr.Shutdown();
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
    return EXIT_SUCCESS;
}
