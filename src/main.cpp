// =============================================================================
// Unleashed DMA -- Overwatch 2 External Cheat
//
// Entry point.  Initialises DMA, waits for Overwatch.exe, resolves global
// encryption keys, starts all background threads, then hands control to the
// DX11 full-screen display (message loop).
// =============================================================================

#include <Windows.h>
#include <cstdio>
#include <thread>
#include <chrono>

#include "Memory/Memory.h"        // ::mem   (global DMA instance)
#include "Game/SDK.hpp"           // OW::SDK
#include "Game/Decrypt.hpp"       // OW::GetGlobalKey, OW::DecryptComponent
#include "Game/Overwatch.hpp"     // main threads: viewmatrix, entity, aimbot, ...
#include "Game/Offsets.hpp"       // offset constants
#include "Utils/Config.hpp"       // OW::Config
#include "Renderer/Overlay.hpp"   // g_Overlay
#include "Renderer/Renderer.hpp"  // Render:: drawing primitives
#include "Renderer/IconManager.hpp"
#include "Features/UI.hpp"        // UI::Render

// =============================================================================
// Render callback  --  called every frame by the overlay
// =============================================================================

void RenderCallback()
{
    // --- ESP data calculation (W2S positions) ---
    // The menu is rendered by the separate overlay menu window. This callback
    // only draws the transparent full-screen canvas layer.
    // PlayerInfo and skillinfo scan OW::entities and compute world-to-screen
    // bounding boxes / skill info strings.  They are read-only and safe to call
    // alongside the entity processing threads.
    // TODO: pipe the computed positions into Render::DrawRect / DrawLine / etc.
    if (OW::entities.size() > 0) {
        PlayerInfo();
        skillinfo();
    }

    if (IconManager* icons = Render::GetIconManager()) {
        if (ID3D11ShaderResourceView* ana_e = icons->GetIcon("AnaE"))
            Render::DrawIcon(ana_e, ImVec2(100.0f, 100.0f), ImVec2(48.0f, 48.0f));
    }
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
    // Step 4 -- Resolve global encryption keys
    // ---------------------------------------------------------------
    std::printf("[MAIN] Resolving global encryption keys (may take a moment)...\n");
    if (!OW::GetGlobalKey()) {
        std::fprintf(stderr, "[FATAL] Failed to resolve global keys.\n");
        mem.CloseDma();
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] Global keys resolved.\n\n");

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
