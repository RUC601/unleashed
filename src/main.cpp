// =============================================================================
// Unleashed DMA -- Overwatch 2 External Cheat
//
// Entry point.  Initialises DMA, waits for Overwatch.exe, resolves global
// encryption keys, starts all background threads, then hands control to the
// DX11 full-screen display (message loop).
// =============================================================================

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>

#include "Memory/Memory.h"        // ::mem   (global DMA instance)
#include "Game/SDK.hpp"           // OW::SDK
#include "Game/Decrypt.hpp"       // OW::GetGlobalKey, OW::DecryptComponent
#include "Game/Overwatch.hpp"     // main threads: viewmatrix, entity, aimbot, ...
#include "Game/Offsets.hpp"       // offset constants
#include "Utils/Config.hpp"       // OW::Config
#include "Renderer/Overlay.hpp"   // g_Overlay
#include "Renderer/Renderer.hpp"  // Render:: drawing primitives
#include "Features/UI.hpp"        // UI::Render

// =============================================================================
// Render callback  --  called every frame by the overlay
// =============================================================================

namespace {

    static float CanvasWidth()
    {
        return OW::WX > 0.0f ? OW::WX : static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
    }

    static float CanvasHeight()
    {
        return OW::WY > 0.0f ? OW::WY : static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    }

    static Render::Color ToRenderColor(const ImVec4& color)
    {
        return Render::Color(
            static_cast<int>(color.x * 255.0f),
            static_cast<int>(color.y * 255.0f),
            static_cast<int>(color.z * 255.0f),
            static_cast<int>(color.w * 255.0f)
        );
    }

    static Render::Color EntityRadarColor(const OW::c_entity& entity, size_t index)
    {
        if (entity.Team && OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi)) {
            return ToRenderColor(OW::Config::targetargb);
        }
        return ToRenderColor(entity.Team ? OW::Config::enargb : OW::Config::allyargb);
    }

    static void DrawFovCircle()
    {
        if (!OW::Config::draw_fov)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f || OW::Config::Fov <= 0.0f)
            return;

        Render::DrawCircle(
            OW::Vector2(width * 0.5f, height * 0.5f),
            OW::Config::Fov,
            ToRenderColor(OW::Config::fovcol),
            128,
            1.5f
        );
    }

    static void DrawCrosshair()
    {
        if (!OW::Config::crosscircle)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        const Render::Color color = ToRenderColor(OW::Config::fovcol);
        const float arm = 7.0f;
        const float radius = OW::Config::therad > 0 ? static_cast<float>(OW::Config::therad) : 8.0f;

        Render::DrawLine(OW::Vector2(center.X - arm, center.Y), OW::Vector2(center.X + arm, center.Y), color, 1.5f);
        Render::DrawLine(OW::Vector2(center.X, center.Y - arm), OW::Vector2(center.X, center.Y + arm), color, 1.5f);
        Render::DrawCircle(center, radius, color, 48, 1.0f);
    }

    static void DrawRadar()
    {
        if (!OW::Config::radar || OW::entities.empty() || OW::local_entity.PlayerHealth <= 0.0f)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const float radius = 88.0f;
        const float padding = 24.0f;
        const OW::Vector2 center(width - radius - padding, height - radius - padding);
        const Render::Color border(255, 255, 255, 170);
        const Render::Color background(10, 12, 16, 95);

        Render::DrawFilledCircle(center, radius, background, 72);
        Render::DrawCircle(center, radius, border, 72, 1.0f);
        Render::DrawLine(OW::Vector2(center.X - radius, center.Y), OW::Vector2(center.X + radius, center.Y), Render::Color(255, 255, 255, 55), 1.0f);
        Render::DrawLine(OW::Vector2(center.X, center.Y - radius), OW::Vector2(center.X, center.Y + radius), Render::Color(255, 255, 255, 55), 1.0f);
        Render::DrawFilledCircle(center, 3.0f, Render::Color(120, 255, 120, 220), 20);

        DirectX::XMFLOAT3 forward = OW::viewMatrix_xor.get_rotation();
        const float yaw = static_cast<float>(std::atan2(forward.x, forward.z));
        const float cosYaw = std::cos(-yaw);
        const float sinYaw = std::sin(-yaw);
        const float scale = 1.8f;

        for (size_t index = 0; index < OW::entities.size(); ++index) {
            const OW::c_entity& entity = OW::entities[index];
            if (!entity.Alive || entity.address == OW::local_entity.address)
                continue;

            const float dx = entity.pos.X - OW::local_entity.pos.X;
            const float dz = entity.pos.Z - OW::local_entity.pos.Z;
            float rx = (dx * cosYaw - dz * sinYaw) * scale;
            float rz = (dx * sinYaw + dz * cosYaw) * scale;

            const float len = std::sqrt(rx * rx + rz * rz);
            const float maxLen = radius - 7.0f;
            if (len > maxLen && len > 0.001f) {
                const float k = maxLen / len;
                rx *= k;
                rz *= k;
            }

            const OW::Vector2 point(center.X + rx, center.Y + rz);
            const Render::Color entityColor = EntityRadarColor(entity, index);
            if (OW::Config::radarline)
                Render::DrawLine(center, point, Render::Color(entityColor.R, entityColor.G, entityColor.B, 90), 1.0f);
            Render::DrawFilledCircle(point, entity.Team ? 3.5f : 3.0f, entityColor, 20);
        }
    }

    static void DrawHealthPacks()
    {
        if (!OW::Config::draw_hp_pack || OW::hp_dy_entities.empty())
            return;

        const OW::Vector2 windowSize(CanvasWidth(), CanvasHeight());
        for (const OW::hpanddy& pack : OW::hp_dy_entities) {
            OW::Vector2 screen{};
            OW::Vector3 world(pack.POS.x, pack.POS.y, pack.POS.z);
            if (!OW::viewMatrix.WorldToScreen(world, &screen, windowSize))
                continue;

            const bool isBob = pack.entityid == 0x400000000002533;
            const Render::Color color = isBob ? Render::Color(255, 170, 40, 220)
                                              : Render::Color(60, 220, 120, 220);
            const std::string label = isBob ? "Bob" : "HP";

            Render::DrawFilledCircle(screen, 4.0f, color, 20);
            Render::DrawStrokeText(ImVec2(screen.X + 7.0f, screen.Y - 7.0f), color.ToImU32(), label.c_str(), 13.0f);
        }
    }

} // namespace

void RenderCallback()
{
    // The menu is rendered by the separate overlay menu window. This callback
    // only draws the transparent full-screen canvas layer.
    DrawRadar();

    if (OW::entities.size() > 0) {
        PlayerInfo();
        skillinfo();
    }

    DrawHealthPacks();
    DrawFovCircle();
    DrawCrosshair();
}

// =============================================================================
// Background thread launcher
// =============================================================================

static void StartBackgroundThreads()
{
    std::printf("[MAIN] Starting background threads...\n");

    std::thread(viewmatrix_thread).detach();
    std::printf("  [+] viewmatrix_thread\n");

    std::thread(entity_scan_thread).detach();
    std::printf("  [+] entity_scan_thread\n");

    std::thread(entity_thread).detach();
    std::printf("  [+] entity_thread\n");

    std::thread(aimbot_thread).detach();
    std::printf("  [+] aimbot_thread\n");

    std::thread(configsavenloadthread).detach();
    std::printf("  [+] configsavenloadthread\n");

    std::thread(looprpmthread).detach();
    std::printf("  [+] looprpmthread\n");
}

// =============================================================================
// Main
// =============================================================================

int main()
{
    // ---- Console ----
    SetConsoleTitleA("UNLEASHED");

    std::printf("\n");
    std::printf("  ============================================\n");
    std::printf("     Unleashed DMA  --  Overwatch External\n");
    std::printf("  ============================================\n");
    std::printf("\n");

    // ---- Config ----
    OW::Config::doingentity = 1;

    // ---------------------------------------------------------------
    // Step 1 -- Initialise the DMA subsystem (configured VMMDLL device)
    // ---------------------------------------------------------------
    mem.LoadDmaDeviceConfig();
    std::printf("[MAIN] Initialising DMA subsystem...\n");
    if (!mem.InitDma()) {
        std::fprintf(stderr, "[FATAL] DMA initialisation failed -- check configured DMA device.\n");
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] DMA subsystem ready.\n\n");

    // ---------------------------------------------------------------
    // Step 2 -- Wait for Overwatch.exe to appear
    // ---------------------------------------------------------------
    std::printf("[MAIN] Waiting for Overwatch.exe...\n");
    int attempt = 0;
    while (!mem.AttachToProcess("Overwatch.exe")) {
        if (++attempt % 15 == 0)
            std::printf("[MAIN] Still waiting for Overwatch.exe (attempt %d)...\n", attempt);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::printf("[MAIN] Overwatch.exe found.\n\n");

    // ---------------------------------------------------------------
    // Step 3 -- Initialise the OW memory SDK
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising OW SDK...\n");
    if (!OW::SDK->Initialize()) {
        std::fprintf(stderr, "[FATAL] SDK initialisation failed.\n");
        mem.CloseDma();
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] SDK ready.  Game base: 0x%llX\n\n", OW::SDK->dwGameBase);

    // ---------------------------------------------------------------
    // Step 4 -- Resolve global encryption keys (SKIP: vestigial)
    // May 2026 DecryptComponent reads key material directly from game
    // memory and does not use GlobalKey1/2.  GetGlobalKey() is kept
    // as a no-op for diagnostic probes but no longer blocks startup.
    // ---------------------------------------------------------------
    std::printf("[MAIN] Skipping global key resolution (not used by current decrypt).\n\n");

    // ---------------------------------------------------------------
    // Step 5 -- Start all background threads
    // ---------------------------------------------------------------
    StartBackgroundThreads();
    std::printf("\n");

    // ---------------------------------------------------------------
    // Step 6 -- Initialise the DX11 overlay windows
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising overlay...\n");
    if (!g_Overlay.Initialize(L"Unleashed DMA Overlay")) {
        std::fprintf(stderr, "[FATAL] Overlay initialisation failed.\n");
        OW::Config::doingentity = 0;
        mem.CloseDma();
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] Overlay ready.\n\n");

    // ---------------------------------------------------------------
    // Step 7 -- Main loop (blocks until overlay / game closes)
    // ---------------------------------------------------------------
    std::printf("[MAIN] Entering message loop.  Press HOME to toggle menu.\n");
    std::printf("[MAIN] Close the canvas process/window to exit.\n\n");

    g_Overlay.Run(RenderCallback);

    // ---------------------------------------------------------------
    // Cleanup  --  Run() calls Shutdown() internally, so we just tidy up
    // ---------------------------------------------------------------
    std::printf("\n[MAIN] Display closed.  Shutting down...\n");

    OW::Config::doingentity = 0;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mem.CloseDma();

    std::printf("[MAIN] Goodbye.\n");
    return 0;
}
