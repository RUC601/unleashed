// =============================================================================
// Unleashed DMA -- Overwatch 2 External Cheat
//
// Entry point.  Initialises DMA, waits for Overwatch.exe, resolves global
// encryption keys, starts all background threads, then hands control to the
// DX11 full-screen display (message loop).
// =============================================================================

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <chrono>
#include <string>

#include "Memory/Memory.h"        // ::mem   (global DMA instance)
#include "Memory/KeyState.hpp"    // host keyboard/mouse state via DMA
#include "Game/AbilityIcons.hpp"  // ability icon lookup table
#include "Game/SDK.hpp"           // OW::SDK
#include "Game/Decrypt.hpp"       // OW::GetGlobalKey, OW::DecryptComponent
#include "Game/Overwatch.hpp"     // main threads: viewmatrix, entity, aimbot, ...
#include "Game/Offsets.hpp"       // offset constants
#include "Kmbox/KmBoxNetManager.h" // kmbox::KmBoxMgr
#include "Kmbox/KmboxB.h"         // kmbox::kmBoxBMgr
#include "Kmbox/KmboxTimerResolution.h" // kmbox::EnsureTimerResolution
#include "Utils/Config.hpp"       // OW::Config
#include "Utils/HostMouseDpi.hpp" // OW::RefreshHostMouseDpi
#include "Renderer/IconManager.hpp" // IconManager
#include "Renderer/Overlay.hpp"   // g_Overlay
#include "Renderer/Renderer.hpp"  // Render:: drawing primitives
#include "Features/UI.hpp"        // UI::Render
#include "Utils/Diagnostics.hpp"   // lightweight runtime diagnostics

// =============================================================================
// Render callback  --  called every frame by the overlay
// =============================================================================

namespace {

    std::atomic<bool> g_DiagnosticsThreadRunning{ false };

    static float CanvasWidth()
    {
        return OW::WX > 0.0f ? OW::WX : OW::ResolveScreenWidth();
    }

    static float CanvasHeight()
    {
        return OW::WY > 0.0f ? OW::WY : OW::ResolveScreenHeight();
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

    static void RunDmaPeHeaderProbe()
    {
        if (!OW::Config::kmboxDebugLog) {
            Diagnostics::SetDmaProbeResult(false, false);
            return;
        }

        const uint64_t baseAddress = OW::SDK->dwGameBase;
        IMAGE_DOS_HEADER dosHeader{};
        bool readOk = false;
        if (baseAddress)
            readOk = mem.Read(static_cast<uintptr_t>(baseAddress), &dosHeader, sizeof(dosHeader));

        const uint16_t magic = readOk ? static_cast<uint16_t>(dosHeader.e_magic) : 0;
        const bool mzOk = readOk && dosHeader.e_magic == IMAGE_DOS_SIGNATURE;
        Diagnostics::SetDmaProbeResult(true, mzOk, baseAddress, magic);

        if (mzOk) {
            Diagnostics::Info("[PIPELINE] Stage 1 DMA PE header read OK: base=0x%llX magic=0x%04X.",
                static_cast<unsigned long long>(baseAddress),
                static_cast<unsigned int>(magic));
        } else {
            Diagnostics::Error("[PIPELINE] Stage 1 DMA PE header read failed: base=0x%llX read=%d magic=0x%04X.",
                static_cast<unsigned long long>(baseAddress),
                readOk ? 1 : 0,
                static_cast<unsigned int>(magic));
        }
    }

    static void PreloadHeroAvatars()
    {
        IconManager* icons = Render::GetIconManager();
        if (!icons)
            return;

        static constexpr const char* kHeroAvatarNames[] = {
            "Ana", "Anran", "Ashe", "Baptiste", "Bastion", "Brigitte",
            "Cassidy", "Domina", "Doomfist", "D.Va", "Echo", "Emre",
            "Freja", "Genji", "Hanzo", "Hazard", "Illari", "Jetpack Cat",
            "Junker Queen", "Junkrat", "Juno", "Kiriko", "LifeWeaver",
            "Lucio", "Mauga", "Mei", "Mercy", "Mizuki", "Moira", "Orisa",
            "Pharah", "Ramattra", "Reaper", "Reinhardt", "Roadhog",
            "Sierra", "Sigma", "Sojourn", "Soldier 76", "Sombra",
            "Symmetra", "Torbjorn", "Tracer", "Vendetta", "Venture",
            "Widowmaker", "Winston", "Wrecking Ball", "Wuyang", "Zarya",
            "Zenyatta"
        };

        int loadedCount = 0;
        for (const char* heroName : kHeroAvatarNames) {
            const std::string slug = OW::HeroDisplayNameToSlug(heroName);
            if (!slug.empty() && icons->LoadHeroAvatar(slug))
                ++loadedCount;
        }

        constexpr size_t heroCount = sizeof(kHeroAvatarNames) / sizeof(kHeroAvatarNames[0]);
        std::printf("[MAIN] Hero avatars loaded: %d/%zu.\n", loadedCount, heroCount);
        Diagnostics::Info("Hero avatars loaded: %d/%zu.", loadedCount, heroCount);
    }

    static void DrawPipelineDiagnostics()
    {
        if (!OW::Config::kmboxDebugLog)
            return;

        const Diagnostics::StatusSnapshot snapshot = Diagnostics::Snapshot();
        constexpr float x = 10.0f;
        constexpr float fontSize = 13.0f;
        float y = 10.0f;

        const ImU32 okColor = IM_COL32(120, 255, 160, 235);
        const ImU32 warnColor = IM_COL32(255, 215, 90, 235);
        const ImU32 badColor = IM_COL32(255, 90, 90, 235);
        const ImU32 textColor = IM_COL32(230, 235, 245, 235);

        auto drawLine = [&](ImU32 color, const char* text) {
            Render::DrawText(ImVec2(x, y), color, text, fontSize);
            y += 16.0f;
        };

        char line[160] = {};
        if (snapshot.dmaProbeAttempted) {
            std::snprintf(line, sizeof(line), "DMA: %s MZ=0x%04X",
                snapshot.dmaProbeSucceeded ? "MZ ok" : "MZ fail",
                static_cast<unsigned int>(snapshot.dmaProbeMagic));
            drawLine(snapshot.dmaProbeSucceeded ? okColor : badColor, line);
        } else {
            drawLine(warnColor, "DMA: probe not run");
        }

        drawLine(snapshot.viewMatrixResolved && snapshot.viewMatrixValid ? okColor : badColor,
            snapshot.viewMatrixResolved && snapshot.viewMatrixValid ? "VM: valid" : "VM: zero/invalid");

        const ImGuiIO& io = ImGui::GetIO();
        std::snprintf(line, sizeof(line), "Scale: src %.0fx%.0f canvas %.0fx%.0f fps %.0f",
            OW::WX,
            OW::WY,
            io.DisplaySize.x,
            io.DisplaySize.y,
            io.Framerate);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Entities: scan %zu proc %zu",
            snapshot.lastScanEntityCount,
            snapshot.entityProcess.raw);
        drawLine(snapshot.lastScanEntityCount > 0 && snapshot.entityProcess.raw == 0 ? warnColor :
            snapshot.lastScanEntityCount > 0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Entity Hz: %.1f cyc %llu",
            snapshot.entityProcessHz,
            static_cast<unsigned long long>(snapshot.entityProcessCycles));
        drawLine(snapshot.entityProcessHz >= 30.0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Validated: %zu", snapshot.entityProcess.validated);
        drawLine(snapshot.entityCount > 0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Drop: null %zu dup %zu hp %zu link %zu",
            snapshot.entityProcess.nullPair,
            snapshot.entityProcess.duplicate,
            snapshot.entityProcess.healthBaseFail,
            snapshot.entityProcess.linkBaseFail);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Hero: miss %zu fbFail %zu name? %zu",
            snapshot.entityProcess.heroBaseMissing,
            snapshot.entityProcess.heroFallbackFail,
            snapshot.entityProcess.nameUnknown);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Bone: cand %zu base %zu vbd %zu skel %zu/%zu",
            snapshot.entityProcess.boneCandidates,
            snapshot.entityProcess.boneBaseNonZero,
            snapshot.entityProcess.velocityBoneDataNonZero,
            snapshot.entityProcess.skeletonHeadValid,
            snapshot.entityProcess.skeletonAnyValid);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.skeletonHeadValid == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Bone ptr: ptr %zu base %zu",
            snapshot.entityProcess.boneDataPtrNonZero,
            snapshot.entityProcess.bonesBaseNonZero);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.bonesBaseNonZero == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Bone id: tbl %zu cnt %zu rd %zu head %zu",
            snapshot.entityProcess.velocityBoneIdTableNonZero,
            snapshot.entityProcess.velocityBoneCountValid,
            snapshot.entityProcess.velocityBoneIdTableReadable,
            snapshot.entityProcess.velocityBoneHeadIdFound);
        drawLine(snapshot.entityProcess.boneCandidates > 0 &&
            snapshot.entityProcess.velocityBoneHeadIdFound < snapshot.entityProcess.boneCandidates ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Head: res %zu id %zu loc %zu world %zu",
            snapshot.entityProcess.headProbeResolved,
            snapshot.entityProcess.headProbeIdFound,
            snapshot.entityProcess.headProbeLocalNonZero,
            snapshot.entityProcess.headProbeWorldNonZero);
        drawLine(snapshot.entityProcess.headProbeCandidates > 0 &&
            snapshot.entityProcess.headProbeWorldNonZero < snapshot.entityProcess.headProbeCandidates ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Head path: near %zu/%zu far %zu/%zu ex %zu",
            snapshot.entityProcess.headProbeNearWorldNonZero,
            snapshot.entityProcess.headProbeNearCandidates,
            snapshot.entityProcess.headProbeFarWorldNonZero,
            snapshot.entityProcess.headProbeFarCandidates,
            snapshot.entityProcess.headProbeExceptions);
        drawLine(snapshot.entityProcess.headProbeFarCandidates > 0 &&
            snapshot.entityProcess.headProbeFarWorldNonZero == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Render: radar=%d player=%d skill=%d",
            snapshot.renderDrawRadarCalled ? 1 : 0,
            snapshot.renderPlayerInfoCalled ? 1 : 0,
            snapshot.renderSkillInfoCalled ? 1 : 0);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "PI: in %zu proj %zu draw %zu",
            snapshot.playerInfo.input,
            snapshot.playerInfo.projected,
            snapshot.playerInfo.drawn);
        drawLine(snapshot.playerInfo.input > 0 && snapshot.playerInfo.drawn == 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "PI skip: hp %zu dead %zu self %zu dist %zu",
            snapshot.playerInfo.skippedLocalHealth,
            snapshot.playerInfo.skippedDead,
            snapshot.playerInfo.skippedLocalEntity,
            snapshot.playerInfo.skippedDistance);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "W2S: low %zu high %zu box %zu win %zu",
            snapshot.playerInfo.skippedWorldToScreenLow,
            snapshot.playerInfo.skippedWorldToScreenHigh,
            snapshot.playerInfo.skippedBox,
            snapshot.playerInfo.skippedWindow);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Local: sel %zu hp %d best %dcm",
            snapshot.localEntity.selected,
            snapshot.localEntity.selectedHealth,
            snapshot.localEntity.bestDistanceCm);
        drawLine(snapshot.localEntity.selected == 0 || snapshot.localEntity.selectedHealth <= 0 ? warnColor : textColor, line);

        std::snprintf(line, sizeof(line), "Local cand: angle %zu near %zu named %zu",
            snapshot.localEntity.angleCandidates,
            snapshot.localEntity.nearCameraCandidates,
            snapshot.localEntity.namedCandidates);
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Best: hp %d hero 0x%llX",
            snapshot.localEntity.bestHealth,
            static_cast<unsigned long long>(snapshot.localEntity.bestHeroId));
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Coords: head0 %zu pos!0 %zu",
            snapshot.localEntity.zeroHeadCandidates,
            snapshot.localEntity.nonZeroPositionCandidates);
        drawLine(snapshot.localEntity.zeroHeadCandidates > 0 ? warnColor : textColor, line);

        if (snapshot.renderEntityListEmpty)
            drawLine(badColor, "NO ENTITY DATA \xE2\x80\x94 check pipeline");
    }

    static void DrawDiagnosticLogOverlay()
    {
        if (!Diagnostics::IsLogOverlayVisible())
            return;

        std::vector<std::string> lines = Diagnostics::GetLogLines();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 viewportPos = viewport ? viewport->Pos : ImVec2(0.0f, 0.0f);
        const ImVec2 viewportSize = viewport ? viewport->Size : ImGui::GetIO().DisplaySize;
        const float maxWidth = (std::max)(360.0f, viewportSize.x - 32.0f);
        const float logWidth = (std::min)(980.0f, maxWidth);
        const float maxHeight = (std::max)(120.0f, viewportSize.y * 0.28f);
        const float logHeight = (std::min)(260.0f, maxHeight);

        ImGui::SetNextWindowPos(
            ImVec2(viewportPos.x + viewportSize.x * 0.5f, viewportPos.y + viewportSize.y - 24.0f),
            ImGuiCond_Always,
            ImVec2(0.5f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Diagnostic Log", nullptr, flags)) {
            ImGui::Text("Lines: %zu / %zu", lines.size(), Diagnostics::GetLogLineCapacity());
            ImGui::SameLine();
            if (ImGui::Button("Clear")) {
                Diagnostics::ClearLogLines();
                lines.clear();
            }

            ImGui::Separator();
            ImGui::BeginChild(
                "##DiagnosticLogLines",
                ImVec2(logWidth, logHeight),
                true,
                ImGuiWindowFlags_HorizontalScrollbar);

            if (lines.empty()) {
                ImGui::TextDisabled("No diagnostics yet.");
            } else {
                for (const std::string& line : lines)
                    ImGui::TextUnformatted(line.c_str());
                ImGui::SetScrollHereY(1.0f);
            }

            ImGui::EndChild();
        }
        ImGui::End();
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
        float centerX = width - radius - padding;
        float centerY = height - radius - padding;
        switch (OW::Config::radarCorner) {
            case 1: // Bottom Left
                centerX = radius + padding;
                centerY = height - radius - padding;
                break;
            case 2: // Top Right
                centerX = width - radius - padding;
                centerY = radius + padding;
                break;
            case 3: // Top Left
                centerX = radius + padding;
                centerY = radius + padding;
                break;
            default: // Bottom Right
                break;
        }
        const OW::Vector2 center(centerX, centerY);
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

    static void StartDiagnosticStatusThread()
    {
        g_DiagnosticsThreadRunning.store(true, std::memory_order_release);
        std::thread([]() {
            Diagnostics::Info("Diagnostic status thread started with 5s interval.");
            while (g_DiagnosticsThreadRunning.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (!g_DiagnosticsThreadRunning.load(std::memory_order_acquire))
                    break;
                Diagnostics::DumpStatus();
            }
        }).detach();
    }

    static void StopDiagnosticStatusThread()
    {
        g_DiagnosticsThreadRunning.store(false, std::memory_order_release);
    }

    static void PreloadAbilityIcons()
    {
        IconManager* iconManager = Render::GetIconManager();
        if (!iconManager) {
            Diagnostics::Warn("Ability icon preload skipped: icon manager is not ready.");
            return;
        }

        int loadedHeroes = 0;
        for (const OW::HeroAbilityIcons& icons : OW::AllHeroAbilityIcons()) {
            if (iconManager->LoadAbilityIcons(icons.heroSlug, {
                    icons.ability1Icon,
                    icons.ability2Icon,
                    icons.ultimateIcon
                })) {
                ++loadedHeroes;
            }
        }

        Diagnostics::Info("Ability icon preload complete. heroes=%d/%zu.",
            loadedHeroes,
            OW::HeroAbilityIconCount());
        std::printf("[MAIN] Ability icons preloaded for %d/%zu hero entries.\n",
            loadedHeroes,
            OW::HeroAbilityIconCount());
    }

} // namespace

void RenderCallback()
{
    Diagnostics::RecordFrame();

    // The menu is rendered by the separate overlay menu window. This callback
    // only draws the transparent full-screen canvas layer.
    const bool entityListEmpty = OW::entities.empty();
    bool playerInfoCalled = false;
    bool skillInfoCalled = false;
    DrawRadar();

    if (!entityListEmpty) {
        playerInfoCalled = true;
        PlayerInfo();
        if (OW::Config::skillDisplayMode != 0 || OW::Config::ultimateDisplayMode != 0) {
            skillInfoCalled = true;
            skillinfo();
        }
    } else {
        Diagnostics::PlayerInfoStats emptyPlayerInfoStats{};
        Diagnostics::SetPlayerInfoStats(emptyPlayerInfoStats);
    }

    DrawHealthPacks();
    DrawFovCircle();
    DrawCrosshair();
    Diagnostics::SetRenderPipelineStatus(true, playerInfoCalled, skillInfoCalled, entityListEmpty);

    static DWORD lastRenderPipelineLogTick = 0;
    if (OW::Config::kmboxDebugLog) {
        const DWORD now = GetTickCount();
        if (lastRenderPipelineLogTick == 0 || now - lastRenderPipelineLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 render callback DrawRadar=1 PlayerInfo=%d skillinfo=%d entities_empty=%d.",
                playerInfoCalled ? 1 : 0,
                skillInfoCalled ? 1 : 0,
                entityListEmpty ? 1 : 0);
            lastRenderPipelineLogTick = now;
        }
    }
    DrawPipelineDiagnostics();
    DrawDiagnosticLogOverlay();
}

static void InitializeKmBoxFromConfig()
{
    if (!OW::Config::kmboxEnabled) {
        std::printf("[KMBOX] Disabled by config; output is disabled.\n");
        Diagnostics::Info("KMBox disabled by config.");
        Diagnostics::Aim("kmbox.init early_return disabled_by_config");
        return;
    }

    kmbox::EnsureTimerResolution();

    if (OW::Config::kmboxDeviceType == 0) {
        std::printf("[KMBOX] Initialising network device %s:%d...\n",
            OW::Config::kmboxIp, OW::Config::kmboxPort);
        Diagnostics::Aim("kmbox.init network start ip=%s port=%d mac=%s",
            OW::Config::kmboxIp,
            OW::Config::kmboxPort,
            OW::Config::kmboxMac);
        const int status = kmbox::KmBoxMgr.InitDevice(
            OW::Config::kmboxIp,
            static_cast<WORD>(OW::Config::kmboxPort),
            OW::Config::kmboxMac);
        if (status == success) {
            std::printf("[KMBOX] Network device ready.\n");
            Diagnostics::Info("KMBox network device ready. ip=%s port=%d",
                OW::Config::kmboxIp, OW::Config::kmboxPort);
            Diagnostics::Aim("kmbox.init network success ip=%s port=%d",
                OW::Config::kmboxIp,
                OW::Config::kmboxPort);

            const WORD monitorPort = static_cast<WORD>(OW::Config::kmboxPort + 1);
            const int monitorStatus = kmbox::KmBoxMgr.KeyBoard.StartMonitor(monitorPort);
            if (monitorStatus == success) {
                std::printf("[KMBOX] Monitor started on port %u.\n", monitorPort);
                Diagnostics::Info("KMBox monitor started. port=%u", monitorPort);
                Diagnostics::Aim("kmbox.monitor success port=%u", monitorPort);
            } else {
                std::printf("[KMBOX] Monitor failed to start on port %u. status=%d\n",
                    monitorPort, monitorStatus);
                Diagnostics::Warn("KMBox monitor failed to start. port=%u status=%d",
                    monitorPort, monitorStatus);
                Diagnostics::Aim("kmbox.monitor failure port=%u status=%d", monitorPort, monitorStatus);
            }
        } else {
            std::printf("[KMBOX] Network initialisation failed. status=%d\n", status);
            Diagnostics::Error("KMBox network initialisation failed. status=%d", status);
            Diagnostics::Aim("kmbox.init network failure status=%d", status);
        }
    } else {
        std::printf("[KMBOX] Initialising serial device on %s...\n", OW::Config::kmboxComPort);
        Diagnostics::Aim("kmbox.init serial start port=%s", OW::Config::kmboxComPort);
        const int status = kmbox::kmBoxBMgr.init(OW::Config::kmboxComPort);
        if (status == success) {
            std::printf("[KMBOX] Serial device ready.\n");
            Diagnostics::Info("KMBox serial device ready. port=%s", OW::Config::kmboxComPort);
            Diagnostics::Aim("kmbox.init serial success port=%s", OW::Config::kmboxComPort);
        } else {
            std::printf("[KMBOX] Serial initialisation failed. status=%d\n", status);
            Diagnostics::Error("KMBox serial initialisation failed. status=%d", status);
            Diagnostics::Aim("kmbox.init serial failure status=%d", status);
        }
    }
}

// =============================================================================
// Background thread launcher
// =============================================================================

static void StartBackgroundThreads()
{
    std::printf("[MAIN] Starting background threads...\n");
    Diagnostics::Info("Starting background threads.");

    std::thread(viewmatrix_thread).detach();
    std::printf("  [+] viewmatrix_thread\n");

    std::thread(entity_scan_thread).detach();
    std::printf("  [+] entity_scan_thread\n");

    std::thread(entity_thread).detach();
    std::printf("  [+] entity_thread\n");

    std::thread(aimbot_thread).detach();
    // TEST: RunKmboxMoveTest();
    std::printf("  [+] aimbot_thread\n");

    std::thread(configsavenloadthread).detach();
    std::printf("  [+] configsavenloadthread\n");

    std::thread(looprpmthread).detach();
    std::printf("  [+] looprpmthread\n");

    // KeyState polls the primary host's VK state through DMA. It auto-resolves
    // common Win11 session-slot layouts and still allows a manual RVA override.
    KeyState::Start();
    std::printf("  [+] keystate_thread%s\n",
        KeyState::initialized.load(std::memory_order_acquire) ? "" : " (pending resolver)");
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
    Diagnostics::Initialize(Diagnostics::LogLevel::Info, "./unleashed_diag.log");
    Diagnostics::InitializeAimLog("./unleashed_aim_diag.log");
    OW::Config::LoadConfig(OW::Config::ConfigPath());
    OW::RefreshHostMouseDpi();
    OW::RefreshScreenSizeFromConfig();
    Diagnostics::Aim("main.config_loaded screen=%.0fx%.0f kmboxEnabled=%d deviceType=%d ip=%s port=%d aimSensitivity=%.6f gameMouseSensitivity=%.6f sensReference=%.6f autoSync=%d hostMouseDpi=%.6f hostDpiDetected=%d",
        OW::WX,
        OW::WY,
        OW::Config::kmboxEnabled ? 1 : 0,
        OW::Config::kmboxDeviceType,
        OW::Config::kmboxIp,
        OW::Config::kmboxPort,
        OW::Config::kmboxAimSensitivity,
        OW::Config::gameMouseSensitivity,
        OW::Config::sensReference,
        OW::Config::autoSyncSensitivity ? 1 : 0,
        OW::Config::hostMouseDpi,
        OW::Config::hostMouseDpiAutoDetected ? 1 : 0);
    std::printf("[MAIN] Screen size: %.0fx%.0f\n", OW::WX, OW::WY);
    InitializeKmBoxFromConfig();
    Diagnostics::SetDmaReady(true);
    Diagnostics::Info("DMA subsystem ready. device=%s",
        mem.GetDmaDeviceString().empty() ? "<unknown>" : mem.GetDmaDeviceString().c_str());
    StartDiagnosticStatusThread();

    // ---------------------------------------------------------------
    // Step 2 -- Wait for Overwatch.exe to appear
    // ---------------------------------------------------------------
    std::printf("[MAIN] Waiting for Overwatch.exe...\n");
    Diagnostics::Info("Waiting for Overwatch.exe.");
    int attempt = 0;
    while (!mem.AttachToProcess("Overwatch.exe")) {
        if (++attempt % 15 == 0)
            std::printf("[MAIN] Still waiting for Overwatch.exe (attempt %d)...\n", attempt);
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    std::printf("[MAIN] Overwatch.exe found.\n\n");
    Diagnostics::SetProcessAttached(true);
    Diagnostics::Info("Process attached: Overwatch.exe.");

    // ---------------------------------------------------------------
    // Step 3 -- Initialise the OW memory SDK
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising OW SDK...\n");
    Diagnostics::Info("Initialising OW SDK.");
    if (!OW::SDK->Initialize()) {
        std::fprintf(stderr, "[FATAL] SDK initialisation failed.\n");
        Diagnostics::Error("SDK initialisation failed.");
        StopDiagnosticStatusThread();
        mem.CloseDma();
        Diagnostics::SetDmaReady(false);
        Diagnostics::SetProcessAttached(false);
        kmbox::ReleaseTimerResolution();
        Diagnostics::ShutdownAimLog();
        Diagnostics::Shutdown();
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] SDK ready.  Game base: 0x%llX\n\n", OW::SDK->dwGameBase);
    Diagnostics::Info("SDK ready. Game base=0x%llX",
        static_cast<unsigned long long>(OW::SDK->dwGameBase));
    RunDmaPeHeaderProbe();

    // ---------------------------------------------------------------
    // Step 4 -- Resolve global encryption keys (SKIP: vestigial)
    // May 2026 DecryptComponent reads key material directly from game
    // memory and does not use GlobalKey1/2.  GetGlobalKey() is kept
    // as a no-op for diagnostic probes but no longer blocks startup.
    // ---------------------------------------------------------------
    std::printf("[MAIN] Skipping global key resolution (not used by current decrypt).\n\n");
    Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Skipped);
    Diagnostics::Info("Global key resolution skipped; current decrypt path reads key material directly.");

    // ---------------------------------------------------------------
    // Step 5 -- Start all background threads
    // ---------------------------------------------------------------
    StartBackgroundThreads();
    std::printf("\n");

    // ---------------------------------------------------------------
    // Step 6 -- Initialise the DX11 overlay windows
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising overlay...\n");
    Diagnostics::Info("Initialising overlay.");
    if (!g_Overlay.Initialize(L"Unleashed DMA Overlay")) {
        std::fprintf(stderr, "[FATAL] Overlay initialisation failed.\n");
        Diagnostics::Error("Overlay initialisation failed.");
        OW::Config::doingentity = 0;
        KeyState::Stop();
        StopDiagnosticStatusThread();
        mem.CloseDma();
        Diagnostics::SetDmaReady(false);
        Diagnostics::SetProcessAttached(false);
        kmbox::ReleaseTimerResolution();
        Diagnostics::ShutdownAimLog();
        Diagnostics::Shutdown();
        std::printf("[INFO] Press Enter to exit.\n");
        std::getchar();
        return 1;
    }
    std::printf("[MAIN] Overlay ready.\n\n");
    Diagnostics::Info("Overlay ready.");
    PreloadHeroAvatars();
    PreloadAbilityIcons();

    // ---------------------------------------------------------------
    // Step 7 -- Main loop (blocks until overlay / game closes)
    // ---------------------------------------------------------------
    std::printf("[MAIN] Entering message loop.  Press HOME to toggle menu.\n");
    std::printf("[MAIN] Close the canvas process/window to exit.\n\n");
    Diagnostics::Info("Entering overlay message loop.");
    Diagnostics::SetRenderThread();

    g_Overlay.Run(RenderCallback);

    // ---------------------------------------------------------------
    // Cleanup  --  Run() calls Shutdown() internally, so we just tidy up
    // ---------------------------------------------------------------
    std::printf("\n[MAIN] Display closed.  Shutting down...\n");
    Diagnostics::Info("Display closed. Shutting down.");
    StopDiagnosticStatusThread();
    Diagnostics::DumpStatus();

    OW::Config::doingentity = 0;
    KeyState::Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mem.CloseDma();
    Diagnostics::SetDmaReady(false);
    Diagnostics::SetProcessAttached(false);
    Diagnostics::Info("DMA subsystem closed.");
    kmbox::ReleaseTimerResolution();
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();

    std::printf("[MAIN] Goodbye.\n");
    return 0;
}
