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
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <mutex>
#include <thread>
#include <chrono>
#include <string>

#include "Memory/Memory.h"        // ::mem   (global DMA instance)
#include "Memory/KeyState.hpp"    // host keyboard/mouse state via DMA
#include "Game/AbilityIcons.hpp"  // ability icon lookup table
#include "Game/HeroPerkRuntime.hpp" // manual perk variant runtime
#include "Game/HeroSkills.hpp"    // generic hero skill primitives
#include "Game/InputOrchestrator.hpp" // shared generated-output ownership
#include "Game/SDK.hpp"           // OW::SDK
#include "Game/Decrypt.hpp"       // OW::GetGlobalKey, OW::DecryptComponent
#include "Game/Overwatch.hpp"     // main threads: viewmatrix, entity, aimbot, ...
#include "Game/Offsets.hpp"       // offset constants
#include "Kmbox/KmboxRuntime.hpp" // single-owner KMBox runtime controller
#include "Kmbox/KmboxMoveTest.h"  // RunKmboxMoveTest
#include "Utils/Config.hpp"       // OW::Config
#include "Utils/HostMouseDpi.hpp" // OW::RefreshHostMouseDpi
#include "Utils/ProcessConnection.hpp"
#include "Renderer/IconManager.hpp" // IconManager
#include "Renderer/Overlay.hpp"   // g_Overlay
#include "Renderer/Renderer.hpp"  // Render:: drawing primitives
#include "Features/UI.hpp"        // UI::Render
#include "Utils/CrashDump.hpp"    // crash dump capture for postmortem analysis
#include "Utils/Diagnostics.hpp"   // lightweight runtime diagnostics
#include "Utils/TestServer.hpp"    // optional Scenario telemetry server

// =============================================================================
// Render callback  --  called every frame by the overlay
// =============================================================================

namespace {

    constexpr const char* kTargetProcessName = "Overwatch.exe";
    constexpr const char* kCnDetectorProcessPrefix = "Neac";
    constexpr DWORD kProcessScanIntervalMs = 1000;

    std::atomic<bool> g_DiagnosticsThreadRunning{ false };
    std::atomic<bool> g_ProcessConnectionThreadRunning{ false };
    std::atomic<bool> g_HeroSkillRuntimeThreadRunning{ false };
    std::atomic<bool> g_BackgroundThreadsStarted{ false };
    std::thread g_HeroSkillRuntimeThread;
    std::mutex g_ProcessConnectionMutex;
    constexpr DWORD kHeroSkillRuntimeIntervalMs = 16;

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

    std::string AbsolutePathForLog(const std::string& path)
    {
        try {
            return std::filesystem::absolute(path).string();
        } catch (...) {
            return path;
        }
    }

    bool HasCommandLineFlag(int argc, char** argv, const char* flag)
    {
        for (int index = 1; index < argc; ++index) {
            if (argv[index] && std::strcmp(argv[index], flag) == 0)
                return true;
        }
        return false;
    }

    bool TryGetCommandLineFloat(int argc, char** argv, const char* flag, float& value)
    {
        for (int index = 1; index + 1 < argc; ++index) {
            if (!argv[index] || std::strcmp(argv[index], flag) != 0)
                continue;

            char* end = nullptr;
            const float parsed = std::strtof(argv[index + 1], &end);
            if (end == argv[index + 1] || !std::isfinite(parsed))
                return false;

            value = parsed;
            return true;
        }
        return false;
    }

    bool TryGetCommandLineInt(int argc, char** argv, const char* flag, int& value)
    {
        for (int index = 1; index + 1 < argc; ++index) {
            if (!argv[index] || std::strcmp(argv[index], flag) != 0)
                continue;

            char* end = nullptr;
            errno = 0;
            const long parsed = std::strtol(argv[index + 1], &end, 10);
            if (end == argv[index + 1] ||
                *end != '\0' ||
                errno == ERANGE ||
                parsed < (std::numeric_limits<int>::min)() ||
                parsed > (std::numeric_limits<int>::max)()) {
                return false;
            }

            value = static_cast<int>(parsed);
            return true;
        }
        return false;
    }

    static Render::Color EntityRadarColor(const OW::c_entity& entity, size_t index)
    {
        const OW::TargetingDetail::TargetLockRuntime targetLock =
            OW::TargetingDetail::SnapshotTargetLockRuntime();
        const uint64_t entityKey = entity.address
            ? entity.address
            : (entity.LinkBase ? entity.LinkBase : entity.roster_key);
        const bool selectedByKey =
            targetLock.active &&
            targetLock.entityKey != 0 &&
            entityKey != 0 &&
            targetLock.entityKey == entityKey;
        const bool selectedByIndex =
            OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi);

        if (entity.Team && (selectedByKey || selectedByIndex)) {
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
            "Shion", "Sierra", "Sigma", "Sojourn", "Soldier 76", "Sombra",
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

        std::snprintf(line, sizeof(line), "Entity Hz: scan %.1f proc %.1f",
            snapshot.entityScanHz,
            snapshot.entityProcessHz);
        drawLine(snapshot.entityProcessHz >= 30.0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Entity cycles: scan %llu proc %llu",
            static_cast<unsigned long long>(snapshot.entityScanCycles),
            static_cast<unsigned long long>(snapshot.entityProcessCycles));
        drawLine(textColor, line);

        std::snprintf(line, sizeof(line), "Validated: %zu", snapshot.entityProcess.validated);
        drawLine(snapshot.entityCount > 0 ? okColor : warnColor, line);

        std::snprintf(line, sizeof(line), "Roster: fresh %zu dead %zu miss %zu exp %zu hero %zu",
            snapshot.roster.fresh,
            snapshot.roster.dead,
            snapshot.roster.missing,
            snapshot.roster.expired,
            snapshot.roster.heroChanged);
        drawLine(snapshot.roster.fresh > 0 ? okColor :
            (snapshot.roster.dead > 0 || snapshot.roster.missing > 0 ? warnColor : textColor), line);

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

    static DirectX::XMMATRIX ToDirectXMatrix(const OW::Matrix& matrix)
    {
        return DirectX::XMMATRIX(
            matrix.m11, matrix.m12, matrix.m13, matrix.m14,
            matrix.m21, matrix.m22, matrix.m23, matrix.m24,
            matrix.m31, matrix.m32, matrix.m33, matrix.m34,
            matrix.m41, matrix.m42, matrix.m43, matrix.m44);
    }

    static OW::TargetingDetail::FovRuntimeContext BuildFovRuntimeContext(const OW::Matrix& viewMatrixXor)
    {
        const DirectX::XMFLOAT3 camera = viewMatrixXor.get_location();
        const DirectX::XMFLOAT3 forward = viewMatrixXor.get_rotation();

        OW::TargetingDetail::FovRuntimeContext context{};
        context.camera = OW::Vector3(camera.x, camera.y, camera.z);
        context.forward = OW::TargetingDetail::NormalizeVector(
            OW::Vector3(forward.x, forward.y, forward.z));
        context.valid = OW::TargetingDetail::IsFiniteVector(context.camera) &&
                        !OW::TargetingDetail::IsZeroVector(context.camera) &&
                        !OW::TargetingDetail::IsZeroVector(context.forward);
        return context;
    }

    struct FovProjectionContext {
        DirectX::XMMATRIX inverseViewProjection{};
        bool valid = false;
    };

    static FovProjectionContext BuildFovProjectionContext(const OW::Matrix& viewProjection)
    {
        DirectX::XMVECTOR determinant{};
        const DirectX::XMMATRIX inverse = DirectX::XMMatrixInverse(
            &determinant,
            ToDirectXMatrix(viewProjection));
        const float det = DirectX::XMVectorGetX(determinant);

        FovProjectionContext context{};
        context.inverseViewProjection = inverse;
        context.valid = std::isfinite(det) && std::fabs(det) > 0.000001f;
        return context;
    }

    static bool ScreenRayDirection(const FovProjectionContext& projection,
                                   const OW::Vector2& screen,
                                   float width,
                                   float height,
                                   OW::Vector3& outDirection)
    {
        if (!projection.valid || width <= 0.0f || height <= 0.0f)
            return false;

        const float ndcX = (screen.X / width) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screen.Y / height) * 2.0f;

        const DirectX::XMVECTOR nearClip = DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        const DirectX::XMVECTOR farClip = DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);
        const DirectX::XMVECTOR nearWorld = DirectX::XMVector3TransformCoord(
            nearClip,
            projection.inverseViewProjection);
        const DirectX::XMVECTOR farWorld = DirectX::XMVector3TransformCoord(
            farClip,
            projection.inverseViewProjection);

        DirectX::XMFLOAT3 nearPoint{};
        DirectX::XMFLOAT3 farPoint{};
        DirectX::XMStoreFloat3(&nearPoint, nearWorld);
        DirectX::XMStoreFloat3(&farPoint, farWorld);

        const OW::Vector3 direction = OW::TargetingDetail::NormalizeVector(
            OW::Vector3(farPoint.x - nearPoint.x,
                        farPoint.y - nearPoint.y,
                        farPoint.z - nearPoint.z));
        if (OW::TargetingDetail::IsZeroVector(direction) ||
            !OW::TargetingDetail::IsFiniteVector(direction)) {
            return false;
        }

        outDirection = direction;
        return true;
    }

    static bool ScreenRayAngleDeg(const FovProjectionContext& projection,
                                  const OW::TargetingDetail::FovRuntimeContext& context,
                                  const OW::Vector2& screen,
                                  float width,
                                  float height,
                                  float& outAngleDeg)
    {
        if (!context.valid)
            return false;

        OW::Vector3 direction{};
        if (!ScreenRayDirection(projection, screen, width, height, direction))
            return false;

        const float dot = std::clamp(context.forward | direction, -1.0f, 1.0f);
        outAngleDeg = RAD2DEG(std::acos(dot));
        return std::isfinite(outAngleDeg);
    }

    static float FovRadiusLinearFallback(float width, float height, float fovDeg)
    {
        const float fullViewportRadius = std::sqrt(width * width + height * height) * 0.5f;
        return fullViewportRadius * (OW::Config::ClampFovDeg(fovDeg) / OW::Config::kMaxFovDeg);
    }

    static float FovRadiusForViewport(float width,
                                      float height,
                                      float fovDeg,
                                      const FovProjectionContext& projection,
                                      const OW::TargetingDetail::FovRuntimeContext& fovContext)
    {
        const float targetAngleDeg = OW::Config::FovCircleRenderAngleDeg(fovDeg);
        if (targetAngleDeg <= 0.0f)
            return 0.0f;

        const float maxRadius = std::sqrt(width * width + height * height) * 0.5f;
        const OW::Vector2 center(width * 0.5f, height * 0.5f);

        float centerAngleDeg = 0.0f;
        float maxAngleDeg = 0.0f;
        if (!ScreenRayAngleDeg(projection, fovContext, center, width, height, centerAngleDeg) ||
            !ScreenRayAngleDeg(projection,
                               fovContext,
                               OW::Vector2(center.X + maxRadius, center.Y),
                               width,
                               height,
                               maxAngleDeg)) {
            return FovRadiusLinearFallback(width, height, targetAngleDeg);
        }

        if (targetAngleDeg <= centerAngleDeg)
            return FovRadiusLinearFallback(width, height, targetAngleDeg);
        if (targetAngleDeg >= maxAngleDeg)
            return maxRadius;

        float low = 0.0f;
        float high = maxRadius;
        for (int iteration = 0; iteration < 24; ++iteration) {
            const float mid = (low + high) * 0.5f;
            float angleDeg = 0.0f;
            if (!ScreenRayAngleDeg(projection,
                                   fovContext,
                                   OW::Vector2(center.X + mid, center.Y),
                                   width,
                                   height,
                                   angleDeg)) {
                return FovRadiusLinearFallback(width, height, targetAngleDeg);
            }

            if (angleDeg < targetAngleDeg)
                low = mid;
            else
                high = mid;
        }

        return high;
    }

    static void DrawDashedCircle(const OW::Vector2& center,
                                 float radius,
                                 const Render::Color& color,
                                 float thickness)
    {
        constexpr float kPi = 3.14159265358979323846f;
        constexpr int kSegments = 128;
        constexpr int kDashSegments = 3;
        constexpr int kGapSegments = 2;

        for (int start = 0; start < kSegments; start += kDashSegments + kGapSegments) {
            const int end = (std::min)(start + kDashSegments, kSegments);
            for (int segment = start; segment < end; ++segment) {
                const float a0 = (2.0f * kPi * static_cast<float>(segment)) / static_cast<float>(kSegments);
                const float a1 = (2.0f * kPi * static_cast<float>(segment + 1)) / static_cast<float>(kSegments);
                Render::DrawLine(
                    OW::Vector2(center.X + std::cos(a0) * radius, center.Y + std::sin(a0) * radius),
                    OW::Vector2(center.X + std::cos(a1) * radius, center.Y + std::sin(a1) * radius),
                    color,
                    thickness);
            }
        }
    }

    static bool DrawStyledFovRing(const OW::Vector2& center,
                                  float width,
                                  float height,
                                  float fovDeg,
                                  const FovProjectionContext& projection,
                                  const OW::TargetingDetail::FovRuntimeContext& fovContext,
                                  const OW::Config::FovRingSlotStyle& rawStyle,
                                  bool active,
                                  const char* label)
    {
        OW::Config::FovRingSlotStyle style = rawStyle;
        if (!style.visible)
            return false;

        const float radius = FovRadiusForViewport(width, height, fovDeg, projection, fovContext);
        if (radius <= 0.0f)
            return false;

        const float thickness = active
            ? (std::max)(style.thickness + 0.8f, 2.0f)
            : style.thickness;
        const Render::Color color = ToRenderColor(style.color);
        if (style.lineStyle == 1)
            DrawDashedCircle(center, radius, color, thickness);
        else
            Render::DrawCircle(center, radius, color, 128, thickness);

        if (style.showLabel && label && *label) {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = -0.25f * kPi;
            const float labelX = std::clamp(center.X + std::cos(angle) * radius + 6.0f,
                                            4.0f,
                                            (std::max)(4.0f, width - 92.0f));
            const float labelY = std::clamp(center.Y + std::sin(angle) * radius - 7.0f,
                                            4.0f,
                                            (std::max)(4.0f, height - 18.0f));
            Render::DrawText(ImVec2(labelX, labelY), color.ToImU32(), label, active ? 14.0f : 13.0f);
        }

        return true;
    }

    static bool DrawHeroAimFovRings(uint64_t heroId,
                                    const OW::Vector2& center,
                                    float width,
                                    float height,
                                    const FovProjectionContext& projection,
                                    const OW::TargetingDetail::FovRuntimeContext& fovContext,
                                    const OW::Config::RuntimeDrawFovState& activeState)
    {
        bool drewAny = false;
        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool found = OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot);
            if (!found || !slot.enabled)
                continue;

            const OW::Config::FovRingSlotStyle style = OW::Config::ClampFovRingStyle(
                OW::Config::FovRingStyleFor(OW::Config::FovRingSlotKind::Aim, slotIndex),
                OW::Config::FovRingSlotKind::Aim,
                slotIndex);
            const bool active = activeState.active &&
                activeState.slotKind == static_cast<int>(OW::Config::FovRingSlotKind::Aim) &&
                activeState.slotIndex == slotIndex;
            const float ringFovDeg = active
                ? activeState.fovDeg
                : OW::Config::ResolveHeroPresetFovForDistance(slot.preset, 0.0f);

            char label[32] = {};
            std::snprintf(label,
                          sizeof(label),
                          "A%d %.0f deg",
                          slotIndex + 1,
                          OW::Config::ClampFovDeg(ringFovDeg));
            drewAny |= DrawStyledFovRing(center,
                                         width,
                                         height,
                                         ringFovDeg,
                                         projection,
                                         fovContext,
                                         style,
                                         active,
                                         label);
        }
        return drewAny;
    }

    static bool DrawHeroTrackingDeadzoneRings(uint64_t heroId,
                                              const OW::Vector2& center)
    {
        if (!OW::Config::drawTrackingDeadzones)
            return false;

        bool drewAny = false;
        for (int slotIndex = 0; slotIndex < OW::Config::kMaxHeroPresetSlots; ++slotIndex) {
            OW::Config::HeroSlotPreset slot{};
            const bool found = OW::Config::TryGetHeroAimSlot(heroId, slotIndex, slot);
            if (!found || !slot.enabled ||
                !OW::Config::UsesTrackingDeadzone(slot.preset.aimBehavior)) {
                continue;
            }

            const float radius = OW::Config::ClampTrackingDeadzonePixels(slot.preset.trackingDeadzone);
            if (radius <= 0.0f)
                continue;

            const OW::Config::FovRingSlotStyle style = OW::Config::ClampFovRingStyle(
                OW::Config::FovRingStyleFor(OW::Config::FovRingSlotKind::Aim, slotIndex),
                OW::Config::FovRingSlotKind::Aim,
                slotIndex);
            DrawDashedCircle(center, radius, ToRenderColor(style.color), (std::max)(1.0f, style.thickness));
            drewAny = true;
        }
        return drewAny;
    }

    static void DrawFovCircle(const OW::c_entity& localSnapshot)
    {
        if (!OW::Config::draw_fov)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
        OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);
        const FovProjectionContext projection = BuildFovProjectionContext(renderViewMatrix);
        const OW::TargetingDetail::FovRuntimeContext fovContext =
            BuildFovRuntimeContext(renderViewMatrixXor);

        const OW::Config::RuntimeDrawFovState activeState = OW::Config::SnapshotRuntimeDrawFov();
        bool drewAny = false;
        if (localSnapshot.HeroID != 0) {
            drewAny |= DrawHeroAimFovRings(localSnapshot.HeroID,
                                           center,
                                           width,
                                           height,
                                           projection,
                                           fovContext,
                                           activeState);
        }
        if (drewAny)
            return;

        OW::Config::FovRingSlotStyle fallbackStyle{};
        fallbackStyle.visible = true;
        fallbackStyle.color = OW::Config::fovcol;
        fallbackStyle.thickness = 1.5f;
        fallbackStyle.lineStyle = 0;
        fallbackStyle.showLabel = false;
        DrawStyledFovRing(center,
                          width,
                          height,
                          OW::Config::RuntimeDrawFovOrDefault(OW::Config::Fov),
                          projection,
                          fovContext,
                          fallbackStyle,
                          false,
                          nullptr);
    }

    static void DrawTrackingDeadzoneCircles(const OW::c_entity& localSnapshot)
    {
        if (!OW::Config::drawTrackingDeadzones)
            return;

        const float width = CanvasWidth();
        const float height = CanvasHeight();
        if (width <= 0.0f || height <= 0.0f)
            return;

        if (localSnapshot.HeroID == 0)
            return;

        const OW::Vector2 center(width * 0.5f, height * 0.5f);
        DrawHeroTrackingDeadzoneRings(localSnapshot.HeroID, center);
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

    static void DrawRadar(const std::vector<OW::c_entity>& entitySnapshot,
                          const OW::c_entity& localSnapshot)
    {
        if (!OW::Config::radar)
            return;

        if (entitySnapshot.empty() || localSnapshot.PlayerHealth <= 0.0f)
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

        OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
        OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);
        DirectX::XMFLOAT3 forward = renderViewMatrixXor.get_rotation();
        const float yaw = static_cast<float>(std::atan2(forward.x, forward.z));
        const float cosYaw = std::cos(-yaw);
        const float sinYaw = std::sin(-yaw);
        const float scale = 1.8f;

        for (size_t index = 0; index < entitySnapshot.size(); ++index) {
            const OW::c_entity& entity = entitySnapshot[index];
            if (!entity.Alive || entity.address == localSnapshot.address)
                continue;

            const float dx = entity.pos.X - localSnapshot.pos.X;
            const float dz = entity.pos.Z - localSnapshot.pos.Z;
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

    static void DrawHealthPacks(const std::vector<OW::hpanddy>& dynamicEntitySnapshot)
    {
        if (!OW::Config::draw_hp_pack)
            return;

        if (dynamicEntitySnapshot.empty())
            return;

        const OW::Vector2 windowSize(CanvasWidth(), CanvasHeight());
        OW::Matrix renderViewMatrix{}, renderViewMatrixXor{};
        OW::GetViewMatricesSnapshot(renderViewMatrix, renderViewMatrixXor);

        for (const OW::hpanddy& pack : dynamicEntitySnapshot) {
            OW::Vector2 screen{};
            OW::Vector3 world(pack.POS.x, pack.POS.y, pack.POS.z);
            if (!renderViewMatrix.WorldToScreen(world, &screen, windowSize))
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

    static void HeroSkillRuntimeThread()
    {
        bool disconnectHandled = false;

        while (g_HeroSkillRuntimeThreadRunning.load(std::memory_order_acquire)) {
            const DWORD tick = GetTickCount();

            if (OW::Config::doingentity == 0)
                break;

            if (OW::ProcessConnection::IsConnected()) {
                disconnectHandled = false;
                const OW::c_entity localSnapshot = OW::TargetingDetail::SnapshotLocalEntity();
                OW::HeroPerkRuntime::UpdateContext(localSnapshot.HeroID, true, localSnapshot.Team);
                OW::ProcessHeroSkills();
            } else if (!disconnectHandled) {
                OW::HeroPerkRuntime::ResetForDisconnect();
                OW::CancelActiveSkill();
                disconnectHandled = true;
            }

            const DWORD elapsed = GetTickCount() - tick;
            if (elapsed < kHeroSkillRuntimeIntervalMs)
                Sleep(kHeroSkillRuntimeIntervalMs - elapsed);
            else
                Sleep(1);
        }

        OW::HeroPerkRuntime::ResetForDisconnect();
        OW::CancelActiveSkill();
    }

    static void StopHeroSkillRuntimeThread()
    {
        g_HeroSkillRuntimeThreadRunning.store(false, std::memory_order_release);
        if (g_HeroSkillRuntimeThread.joinable())
            g_HeroSkillRuntimeThread.join();
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
    const bool boxPerfMode = OW::Config::boxPerfMode;
    Diagnostics::BeginRenderWorkloadFrame(boxPerfMode);

    // The menu is rendered by the separate overlay menu window. This callback
    // only draws the transparent full-screen canvas layer.
    const bool entityListEmpty = !OW::TargetingDetail::HasPublishedEntities();
    bool playerInfoCalled = false;
    bool skillInfoCalled = false;
    std::vector<OW::c_entity> frameEntities;
    std::vector<OW::hpanddy> frameDynamicEntities;
    OW::c_entity frameLocal;
    bool frameEntitiesReady = false;
    bool frameDynamicEntitiesReady = false;
    bool frameLocalReady = false;

    auto frameEntitySnapshot = [&]() -> const std::vector<OW::c_entity>& {
        if (!frameEntitiesReady) {
            if (OW::PresentUseForRenderEnabled())
                frameEntities = OW::TargetingDetail::SnapshotPresentRenderEntities();
            if (frameEntities.empty())
                frameEntities = OW::TargetingDetail::SnapshotEntities();
            frameEntitiesReady = true;
        }
        return frameEntities;
    };
    auto frameDynamicEntitySnapshot = [&]() -> const std::vector<OW::hpanddy>& {
        if (!frameDynamicEntitiesReady) {
            if (OW::PresentUseForRenderEnabled())
                frameDynamicEntities = OW::TargetingDetail::SnapshotPresentRenderDynamicEntities();
            if (frameDynamicEntities.empty())
                frameDynamicEntities = OW::TargetingDetail::SnapshotDynamicEntities();
            frameDynamicEntitiesReady = true;
        }
        return frameDynamicEntities;
    };
    auto frameLocalSnapshot = [&]() -> const OW::c_entity& {
        if (!frameLocalReady) {
            frameLocal = OW::TargetingDetail::SnapshotLocalEntity();
            frameLocalReady = true;
        }
        return frameLocal;
    };

    if (boxPerfMode) {
        if (!entityListEmpty) {
            playerInfoCalled = true;
            PlayerInfoFromSnapshot(frameEntitySnapshot(), frameLocalSnapshot(), true);
        } else {
            Diagnostics::PlayerInfoStats emptyPlayerInfoStats{};
            emptyPlayerInfoStats.boxPerfMode = true;
            emptyPlayerInfoStats.fastBoxPath = OW::Config::boxPerfFastRect;
            Diagnostics::SetPlayerInfoStats(emptyPlayerInfoStats);
        }

        Diagnostics::SetRenderPipelineStatus(false, playerInfoCalled, false, entityListEmpty);
        return;
    }

    if (!entityListEmpty && OW::Config::radar)
        DrawRadar(frameEntitySnapshot(), frameLocalSnapshot());

    if (!entityListEmpty) {
        playerInfoCalled = true;
        PlayerInfoFromSnapshot(frameEntitySnapshot(), frameLocalSnapshot(), false);
        if (OW::Config::ult || (OW::Config::skillinfo && OW::Config::skillDisplayMode != 0)) {
            skillInfoCalled = true;
            skillinfo(frameEntitySnapshot());
        }
    } else {
        Diagnostics::PlayerInfoStats emptyPlayerInfoStats{};
        Diagnostics::SetPlayerInfoStats(emptyPlayerInfoStats);
    }

    DrawAimTriggerStatusPanel(frameLocalSnapshot());
    if (OW::Config::draw_hp_pack)
        DrawHealthPacks(frameDynamicEntitySnapshot());
    if (OW::Config::draw_fov)
        DrawFovCircle(frameLocalSnapshot());
    if (OW::Config::drawTrackingDeadzones)
        DrawTrackingDeadzoneCircles(frameLocalSnapshot());
    DrawCrosshair();
    Diagnostics::SetRenderPipelineStatus(true, playerInfoCalled, skillInfoCalled, entityListEmpty);

    static DWORD lastRenderPipelineLogTick = 0;
    if (OW::Config::kmboxDebugLog) {
        const DWORD now = GetTickCount();
        if (lastRenderPipelineLogTick == 0 || now - lastRenderPipelineLogTick >= 1000) {
            Diagnostics::Info("[PIPELINE] Stage 5 render callback mode=normal DrawRadar=1 PlayerInfo=%d skillinfo=%d entities_empty=%d.",
                playerInfoCalled ? 1 : 0,
                skillInfoCalled ? 1 : 0,
                entityListEmpty ? 1 : 0);
            lastRenderPipelineLogTick = now;
        }
    }
    DrawPipelineDiagnostics();
    DrawDiagnosticLogOverlay();
}

static int InitializeKmBoxFromConfig()
{
    constexpr auto timeout = std::chrono::milliseconds(500);
    const int status = kmbox::ReconcileRuntimeFromConfig(timeout);
    const kmbox::KmboxRuntimeAppliedState applied = kmbox::RuntimeController().Applied();

    if (!OW::Config::kmboxEnabled) {
        std::printf("[KMBOX] Disabled by config; runtime is stopped.\n");
        Diagnostics::Info("KMBox disabled by config; runtime reconciled to None.");
        Diagnostics::Aim("kmbox.init reconciled disabled status=%d generation=%llu",
            status,
            static_cast<unsigned long long>(applied.generation));
        return status;
    }

    if (status == success && applied.descriptor.backend != kmbox::KmboxRuntimeBackend::None) {
        std::printf("[KMBOX] Runtime ready. backend=%d generation=%llu\n",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        Diagnostics::Info("KMBox runtime ready. backend=%d generation=%llu",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        Diagnostics::Aim("kmbox.init reconciled success backend=%d generation=%llu",
            static_cast<int>(applied.descriptor.backend),
            static_cast<unsigned long long>(applied.generation));
        return success;
    }

    std::printf("[KMBOX] Runtime initialisation failed. status=%d\n", status);
    Diagnostics::Error("KMBox runtime initialisation failed. status=%d", status);
    Diagnostics::Aim("kmbox.init reconciled failure status=%d", status);
    return status != success ? status : err_queue_stopped;
}

static void StartBackgroundThreads();
static void StartProcessConnectionThread();
static void StopProcessConnectionThread();

static int ReleaseKmboxMouseStateForShutdown(
    const char* reason,
    DWORD timeoutMs = 500)
{
    Diagnostics::Aim("kmbox.shutdown_output_cleanup reason=%s timeoutMs=%lu",
        reason ? reason : "unknown",
        static_cast<unsigned long>(timeoutMs));
    const auto timeout = std::chrono::milliseconds(timeoutMs);
    const int status = kmbox::ReleaseActiveRuntime(timeout);
    if (status != success) {
        Diagnostics::Warn(
            "KMBox output cleanup completed with status=%d reason=%s timeoutMs=%lu.",
            status,
            reason ? reason : "unknown",
            static_cast<unsigned long>(timeoutMs));
    }
    return status;
}

static int ShutdownKmboxRuntime(const char* reason, DWORD timeoutMs = 500)
{
    Diagnostics::Aim("kmbox.lifecycle_shutdown reason=%s timeoutMs=%lu",
        reason ? reason : "unknown",
        static_cast<unsigned long>(timeoutMs));

    const int status = kmbox::ShutdownActiveRuntime(
        std::chrono::milliseconds(timeoutMs));
    if (status != success) {
        Diagnostics::Warn("KMBox lifecycle shutdown completed with status=%d reason=%s.",
            status, reason ? reason : "unknown");
    }
    return status;
}

static void LoadRuntimeConfigForDiagnostics()
{
    const std::string configPath = OW::Config::ConfigPath();
    const std::string configPathForLog = AbsolutePathForLog(configPath);
    OW::Config::LoadConfig(configPath);
    OW::RefreshHostMouseDpi();
    OW::RefreshScreenSizeFromConfig();
    Diagnostics::Aim("main.config_loaded configPath=%s screen=%.0fx%.0f kmboxEnabled=%d deviceType=%d ip=%s commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d hostMouseDpi=%.6f hostDpiDetected=%d",
        configPathForLog.c_str(),
        OW::WX,
        OW::WY,
        OW::Config::kmboxEnabled ? 1 : 0,
        OW::Config::kmboxDeviceType,
        OW::Config::kmboxIp,
        OW::Config::kmboxPort,
        OW::Config::EffectiveKmboxMonitorPort(),
        OW::Config::kmboxMonitorPort,
        OW::Config::kmboxMonitorPortManualOverride ? 1 : 0,
        OW::Config::kmboxCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0,
        OW::Config::hostMouseDpi,
        OW::Config::hostMouseDpiAutoDetected ? 1 : 0);
}

static void ShutdownHeadlessRuntime()
{
    OW::Config::doingentity = 0;
    StopHeroSkillRuntimeThread();
    OW::CancelActiveSkill();
    TestServer::Stop();
    KeyState::Stop();
    StopProcessConnectionThread();
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Shutting down");
    StopDiagnosticStatusThread();
    mem.CloseDma();
    Diagnostics::SetDmaReady(false);
    Diagnostics::SetProcessAttached(false);
    ShutdownKmboxRuntime("headless_shutdown");
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
}

static int RunConfigCheckCli()
{
    const std::string configPath = OW::Config::ConfigPath();
    OW::Config::LoadConfig(configPath);

    std::printf("[CONFIG] directory=%s\n", OW::Config::ConfigDirectoryPath().c_str());
    std::printf("[CONFIG] profile=%s\n", OW::Config::configFileName.c_str());
    std::printf("[CONFIG] path=%s\n", AbsolutePathForLog(configPath).c_str());
    std::printf("[CONFIG] heroPath=%s\n", AbsolutePathForLog(OW::Config::HeroConfigPath(configPath)).c_str());
    std::printf("[CONFIG] countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d\n",
        OW::Config::kmboxCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0);
    std::printf("[CONFIG] commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d\n",
        OW::Config::kmboxPort,
        OW::Config::EffectiveKmboxMonitorPort(),
        OW::Config::kmboxMonitorPort,
        OW::Config::kmboxMonitorPortManualOverride ? 1 : 0);
    return 0;
}

static int RunKmboxMoveTestCli()
{
    Diagnostics::Initialize(Diagnostics::LogLevel::Info, "./unleashed_diag.log");
    Diagnostics::InitializeAimLog("./unleashed_aim_diag.log");
    LoadRuntimeConfigForDiagnostics();
    OW::Config::Menu = false;
    const int initStatus = InitializeKmBoxFromConfig();
    const auto applied = kmbox::RuntimeController().Applied();
    const bool runtimeReady = kmbox::IsRuntimeReady(
        initStatus,
        applied,
        kmbox::RuntimeController().CanDispatch(applied.generation));
    if (runtimeReady)
        RunKmboxMoveTest();
    ShutdownKmboxRuntime("kmbox_move_test");
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
    if (!runtimeReady) {
        std::fprintf(stderr, "[KMBOX] Move test aborted: no active runtime. status=%d\n",
            initStatus != success ? initStatus : err_queue_stopped);
        return 1;
    }
    return 0;
}

static int RunKmboxRecoverMouseCli()
{
    Diagnostics::Initialize(Diagnostics::LogLevel::Info, "./unleashed_diag.log");
    Diagnostics::InitializeAimLog("./unleashed_aim_diag.log");
    LoadRuntimeConfigForDiagnostics();
    OW::Config::Menu = false;
    const int initStatus = InitializeKmBoxFromConfig();
    const auto applied = kmbox::RuntimeController().Applied();
    const bool runtimeReady = kmbox::IsRuntimeReady(
        initStatus,
        applied,
        kmbox::RuntimeController().CanDispatch(applied.generation));
    int recoveryStatus = runtimeReady
        ? ReleaseKmboxMouseStateForShutdown("kmbox_recover_mouse_cli", 1500)
        : (initStatus != success ? initStatus : err_queue_stopped);
    const int shutdownStatus = ShutdownKmboxRuntime("kmbox_recover_mouse_cli", 1500);
    if (recoveryStatus == success)
        recoveryStatus = shutdownStatus;
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();
    if (recoveryStatus == success) {
        std::printf("[KMBOX] Output safety recovery transport completed.\n");
        return 0;
    }
    std::fprintf(stderr,
        "[KMBOX] Output safety recovery failed or timed out. status=%d\n",
        recoveryStatus);
    return 1;
}

static int RunKmboxCalibrationCli(float referenceGameSensitivityOverride = 0.0f)
{
    mem.LoadDmaDeviceConfig();
    std::printf("[MAIN] Initialising DMA subsystem for KMBox calibration...\n");
    if (!mem.InitDma()) {
        std::fprintf(stderr, "[FATAL] DMA initialisation failed -- check configured DMA device.\n");
        return 1;
    }

    Diagnostics::Initialize(Diagnostics::LogLevel::Info, "./unleashed_diag.log");
    Diagnostics::InitializeAimLog("./unleashed_aim_diag.log");
    LoadRuntimeConfigForDiagnostics();
    OW::Config::Menu = false;
    const int initStatus = InitializeKmBoxFromConfig();
    const auto applied = kmbox::RuntimeController().Applied();
    if (!kmbox::IsRuntimeReady(
            initStatus,
            applied,
            kmbox::RuntimeController().CanDispatch(applied.generation))) {
        std::fprintf(stderr, "[KMBOX] Calibration failed: no active runtime. status=%d\n",
            initStatus != success ? initStatus : err_queue_stopped);
        ShutdownHeadlessRuntime();
        return 2;
    }
    Diagnostics::SetDmaReady(true);
    StartProcessConnectionThread();
    StartBackgroundThreads();

    constexpr DWORD kWaitForControllerMs = 15000;
    const DWORD waitStarted = GetTickCount();
    while (OW::SDK && OW::SDK->g_player_controller == 0 &&
           GetTickCount() - waitStarted < kWaitForControllerMs) {
        Sleep(100);
    }

    if (!OW::SDK || OW::SDK->g_player_controller == 0) {
        std::fprintf(stderr, "[KMBOX] Calibration failed: player controller was not resolved within %lu ms.\n",
            static_cast<unsigned long>(kWaitForControllerMs));
        Diagnostics::Aim("kmbox.calibration_cli failure reason=player_controller_timeout waitMs=%lu",
            static_cast<unsigned long>(kWaitForControllerMs));
        ShutdownHeadlessRuntime();
        return 2;
    }

    if (std::isfinite(referenceGameSensitivityOverride) && referenceGameSensitivityOverride > 0.0f) {
        OW::Config::referenceGameSensitivity = referenceGameSensitivityOverride;
        std::printf("[KMBOX] Calibration reference game sensitivity override: %.3f\n",
            referenceGameSensitivityOverride);
        Diagnostics::Aim("kmbox.calibration_cli reference_override gameSens=%.6f",
            referenceGameSensitivityOverride);
    }

    const float result = OW::RunCalibrationSamples(referenceGameSensitivityOverride);
    if (result > 0.0f) {
        OW::Config::SaveConfig(OW::Config::ConfigPath());
        std::printf("[KMBOX] Calibration complete: yaw=%.3f counts/rad refGameSens=%.3f\n",
            result, OW::Config::referenceGameSensitivity);
        Diagnostics::Aim("kmbox.calibration_cli success yawCountsPerRad=%.6f pitchCountsPerRad=%.6f refGameSens=%.6f",
            OW::Config::calibratedCountsPerRadian,
            OW::Config::calibratedPitchCountsPerRadian > 0.0f
                ? OW::Config::calibratedPitchCountsPerRadian
                : OW::Config::calibratedCountsPerRadian,
            OW::Config::referenceGameSensitivity);
    } else {
        std::fprintf(stderr, "[KMBOX] Calibration failed: zero view-angle delta.\n");
        Diagnostics::Aim("kmbox.calibration_cli failure reason=zero_angle_delta");
    }

    ShutdownHeadlessRuntime();
    return result > 0.0f ? 0 : 3;
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

    if (OW::PresentInterpolationEnabled()) {
        std::thread(present_interp_thread).detach();
        std::printf("  [+] present_interp_thread\n");
    }

    std::thread(aimbot_thread).detach();
    // TEST: RunKmboxMoveTest();
    std::printf("  [+] aimbot_thread\n");

    g_HeroSkillRuntimeThreadRunning.store(true, std::memory_order_release);
    g_HeroSkillRuntimeThread = std::thread(HeroSkillRuntimeThread);
    std::printf("  [+] hero_skill_runtime_thread\n");

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

static void ClearProcessRuntimeSnapshots()
{
    OW::SDK->Reset();
    OW::ClearCriticalMatrixReadPlan();
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        OW::ow_entities.clear();
        OW::ow_entities_scan.clear();
        OW::local_entity = OW::c_entity{};
        OW::abletotread = 0;
        OW::entity_fast_scan_until_tick = 0;
        OW::entity_topology_rescan_request_count = 0;
    }
    {
        std::lock_guard<std::mutex> lock(OW::raw_scan_mutex);
        OW::latest_raw_scan_snapshot = OW::RawEntityScanSnapshot{};
        OW::last_consumed_raw_scan_generation = 0;
    }

    OW::TargetingDetail::PublishEntitySnapshots({}, {});
    OW::SetViewMatrices(OW::Matrix{}, OW::Matrix{});
    Diagnostics::SetEntityCount(0);
    Diagnostics::SetEntityProcessStats(Diagnostics::EntityProcessStats{});
    Diagnostics::SetPlayerInfoStats(Diagnostics::PlayerInfoStats{});
    Diagnostics::SetLocalEntityStats(Diagnostics::LocalEntityStats{});
    Diagnostics::SetRosterStats(Diagnostics::RosterStats{});
    Diagnostics::SetViewMatrixStatus(false, false);
    Diagnostics::SetDmaProbeResult(false, false);
}

static void MarkProcessDisconnected(const char* statusText)
{
    // Close the process-scoped output session before stopping producers. This
    // prevents a late scheduler call from self-synchronizing while disconnect
    // cleanup is still draining the old hardware generation.
    OW::SetRuntimeOutputSessionEnabled(false);
    const bool wasConnected = OW::ProcessConnection::IsConnected();
    OW::ProcessConnection::SetStatus(false, false, 0, 0,
        statusText ? statusText : "Waiting for Overwatch.exe");

    if (wasConnected) {
        Diagnostics::Warn("Target process disconnected.");
        std::printf("[MAIN] Target process disconnected; waiting for %s...\n", kTargetProcessName);
        // Stop independent hero/sequence producers before clearing shared
        // ownership so an old worker cannot reacquire during disconnect.
        OW::CancelActiveSkill();
        OW::RuntimeOutputScheduler().CancelAllAndJoin(
            OW::OutputActionCancelReason::RuntimeChanged);
        ReleaseKmboxMouseStateForShutdown("target_disconnect");
    }

    ClearProcessRuntimeSnapshots();
    mem.DetachProcess();
    Diagnostics::SetProcessAttached(false);
}

static bool TryConnectTargetProcess(bool forceReconnect, bool manualRequest)
{
    std::lock_guard<std::mutex> lock(g_ProcessConnectionMutex);

    const DWORD livePid = mem.GetPidFromName(kTargetProcessName);
    if (livePid == 0) {
        if (manualRequest)
            Diagnostics::Info("Manual reconnect requested, but %s is not running.", kTargetProcessName);
        MarkProcessDisconnected("Waiting for Overwatch.exe");
        return false;
    }

    const bool wasConnected = OW::ProcessConnection::IsConnected();
    // A manual/PID-change reconnect is the same output ownership boundary as
    // a detected disconnect. Close normal output before publishing the
    // reconnecting state, then drain the old process generation completely.
    OW::SetRuntimeOutputSessionEnabled(false);
    OW::ProcessConnection::SetStatus(false, true, 0, 0,
        manualRequest ? "Reconnecting UN..." : "Connecting UN...");
    if (forceReconnect && wasConnected) {
        OW::CancelActiveSkill();
        OW::RuntimeOutputScheduler().CancelAllAndJoin(
            OW::OutputActionCancelReason::RuntimeChanged);
        ReleaseKmboxMouseStateForShutdown("target_reconnect");
    }
    Diagnostics::SetProcessAttached(false);
    Diagnostics::Info("%s process connect attempt. target=%s pid=%lu",
        manualRequest ? "Manual" : "Automatic",
        kTargetProcessName,
        static_cast<unsigned long>(livePid));

    const auto neacProcess = mem.FindProcessByPrefix(kCnDetectorProcessPrefix);
    OW::offset::SetActiveProfile(neacProcess
        ? OW::offset::RuntimeProfile::CnNe
        : OW::offset::RuntimeProfile::WorldBz);
    Diagnostics::Info("Offset profile selected: %s reason=%s%s pid=%lu name=%s.",
        OW::offset::ActiveProfileName(),
        neacProcess ? "Neac process detected" : "Neac process not detected",
        neacProcess ? "" : " (world fallback)",
        static_cast<unsigned long>(neacProcess.pid),
        neacProcess.name.empty() ? "<none>" : neacProcess.name.c_str());
    std::printf("[MAIN] Offset profile: %s (%s)\n",
        OW::offset::ActiveProfileName(),
        neacProcess ? neacProcess.name.c_str() : "world fallback");

    ClearProcessRuntimeSnapshots();
    if (forceReconnect)
        mem.DetachProcess();

    if (!OW::SDK->Initialize()) {
        Diagnostics::SetProcessAttached(false);
        OW::ProcessConnection::SetStatus(false, false, 0, 0,
            "Connect failed; waiting for Overwatch.exe");
        return false;
    }

    const int attachedPid = mem.GetCurrentProcessId();
    Diagnostics::SetProcessAttached(true);
    OW::ProcessConnection::SetStatus(true, false, attachedPid, OW::SDK->dwGameBase,
        std::string("Connected to Overwatch.exe (") + OW::offset::ActiveProfileName() + ")");
    // Reopen only after SDK attach and process identity have both committed.
    OW::SetRuntimeOutputSessionEnabled(true);
    Diagnostics::Info("Process attached: %s pid=%d base=0x%llX.",
        kTargetProcessName,
        attachedPid,
        static_cast<unsigned long long>(OW::SDK->dwGameBase));
    std::printf("[MAIN] %s attached. PID=%d base=0x%llX\n",
        kTargetProcessName,
        attachedPid,
        static_cast<unsigned long long>(OW::SDK->dwGameBase));

    RunDmaPeHeaderProbe();

    if (!g_BackgroundThreadsStarted.exchange(true, std::memory_order_acq_rel)) {
        StartBackgroundThreads();
        std::printf("\n");
    }

    return true;
}

static void StartProcessConnectionThread()
{
    g_ProcessConnectionThreadRunning.store(true, std::memory_order_release);
    OW::SetRuntimeOutputSessionEnabled(false);
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Waiting for Overwatch.exe");
    std::thread([]() {
        Diagnostics::Info("Process connection thread started. target=%s interval_ms=%lu.",
            kTargetProcessName,
            static_cast<unsigned long>(kProcessScanIntervalMs));

        DWORD lastAttemptTick = 0;
        while (g_ProcessConnectionThreadRunning.load(std::memory_order_acquire)) {
            const bool manualRequest = OW::ProcessConnection::ConsumeReconnectRequest();
            const DWORD now = GetTickCount();
            const bool scanDue = lastAttemptTick == 0 || now - lastAttemptTick >= kProcessScanIntervalMs;

            if (manualRequest) {
                TryConnectTargetProcess(true, true);
                lastAttemptTick = now;
            } else if (!OW::ProcessConnection::IsConnected()) {
                if (scanDue) {
                    TryConnectTargetProcess(false, false);
                    lastAttemptTick = now;
                }
            } else if (scanDue) {
                const DWORD livePid = mem.GetPidFromName(kTargetProcessName);
                const int attachedPid = OW::ProcessConnection::ConnectedPid();
                if (livePid == 0) {
                    MarkProcessDisconnected("Waiting for Overwatch.exe");
                } else if (attachedPid != 0 && livePid != static_cast<DWORD>(attachedPid)) {
                    Diagnostics::Info("Target process PID changed. old=%d new=%lu.",
                        attachedPid,
                        static_cast<unsigned long>(livePid));
                    TryConnectTargetProcess(true, false);
                }
                lastAttemptTick = now;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        Diagnostics::Info("Process connection thread stopping.");
    }).detach();
}

static void StopProcessConnectionThread()
{
    g_ProcessConnectionThreadRunning.store(false, std::memory_order_release);
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv)
{
    CrashDump::InstallUnhandledExceptionFilter(L"crash_dumps");

    // ---- Console ----
    SetConsoleTitleA("UNLEASHED");

    std::printf("\n");
    std::printf("  ============================================\n");
    std::printf("     Unleashed DMA  --  Overwatch External\n");
    std::printf("  ============================================\n");
    std::printf("\n");

    // ---- Config ----
    OW::Config::doingentity = 1;

    if (HasCommandLineFlag(argc, argv, "--config-check"))
        return RunConfigCheckCli();
    if (HasCommandLineFlag(argc, argv, "--kmbox-recover-mouse"))
        return RunKmboxRecoverMouseCli();
    if (HasCommandLineFlag(argc, argv, "--kmbox-move-test"))
        return RunKmboxMoveTestCli();
    if (HasCommandLineFlag(argc, argv, "--kmbox-calibrate")) {
        float referenceGameSensitivityOverride = 0.0f;
        TryGetCommandLineFloat(argc, argv, "--kmbox-reference-sens", referenceGameSensitivityOverride);
        return RunKmboxCalibrationCli(referenceGameSensitivityOverride);
    }

    const bool testServerRequested = HasCommandLineFlag(argc, argv, "--test-server");
    const bool testServerCorsRequested = HasCommandLineFlag(argc, argv, "--test-server-cors");
    const bool boxPerfModeRequested = HasCommandLineFlag(argc, argv, "--box-perf-mode");
    int testServerPortValue = 19550;
    if (HasCommandLineFlag(argc, argv, "--test-server-port")) {
        if (!TryGetCommandLineInt(argc, argv, "--test-server-port", testServerPortValue) ||
            testServerPortValue <= 0 ||
            testServerPortValue > 65535) {
            std::fprintf(stderr, "[FATAL] Invalid --test-server-port. Expected 1..65535.\n");
            return 1;
        }
    }

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
    const std::string configPath = OW::Config::ConfigPath();
    const std::string configPathForLog = AbsolutePathForLog(configPath);
    OW::Config::LoadConfig(configPath);
    if (boxPerfModeRequested) {
        OW::Config::boxPerfMode = true;
        OW::Config::boxPerfFastRect = true;
    }
    OW::RefreshHostMouseDpi();
    OW::RefreshScreenSizeFromConfig();
    Diagnostics::Aim("main.config_loaded configPath=%s screen=%.0fx%.0f kmboxEnabled=%d deviceType=%d ip=%s commandPort=%d effectiveMonitorPort=%d monitorPortSetting=%d manualOverride=%d countsPerRadian=%.6f calibratedCountsPerRadian=%.6f gameMouseSensitivity=%.6f referenceGameSensitivity=%.6f autoScaleByGameSensitivity=%d hostMouseDpi=%.6f hostDpiDetected=%d",
        configPathForLog.c_str(),
        OW::WX,
        OW::WY,
        OW::Config::kmboxEnabled ? 1 : 0,
        OW::Config::kmboxDeviceType,
        OW::Config::kmboxIp,
        OW::Config::kmboxPort,
        OW::Config::EffectiveKmboxMonitorPort(),
        OW::Config::kmboxMonitorPort,
        OW::Config::kmboxMonitorPortManualOverride ? 1 : 0,
        OW::Config::kmboxCountsPerRadian,
        OW::Config::calibratedCountsPerRadian,
        OW::Config::gameMouseSensitivity,
        OW::Config::referenceGameSensitivity,
        OW::Config::autoScaleByGameSensitivity ? 1 : 0,
        OW::Config::hostMouseDpi,
        OW::Config::hostMouseDpiAutoDetected ? 1 : 0);
    std::printf("[MAIN] Screen size: %.0fx%.0f\n", OW::WX, OW::WY);
    if (OW::Config::boxPerfMode) {
        std::printf("[MAIN] Box performance mode enabled (%s box path).\n",
            OW::Config::boxPerfFastRect ? "fast rect" : "corner");
    }
    Diagnostics::Info("Overlay box performance mode: enabled=%d fast_rect=%d cli=%d.",
        OW::Config::boxPerfMode ? 1 : 0,
        OW::Config::boxPerfFastRect ? 1 : 0,
        boxPerfModeRequested ? 1 : 0);
    InitializeKmBoxFromConfig();
    Diagnostics::SetDmaReady(true);
    Diagnostics::Info("DMA subsystem ready. device=%s",
        mem.GetDmaDeviceString().empty() ? "<unknown>" : mem.GetDmaDeviceString().c_str());
    StartDiagnosticStatusThread();

    if (testServerRequested) {
        TestServer::Options testServerOptions{};
        testServerOptions.port = static_cast<uint16_t>(testServerPortValue);
        testServerOptions.allowWildcardCors = testServerCorsRequested;
        if (!TestServer::Start(testServerOptions)) {
            std::fprintf(stderr, "[FATAL] Failed to start Unleashed test server on 127.0.0.1:%d.\n",
                testServerPortValue);
            ShutdownHeadlessRuntime();
            return 2;
        }
        std::printf("[MAIN] Test server ready on http://127.0.0.1:%d%s\n\n",
            testServerPortValue,
            testServerCorsRequested ? " (wildcard CORS enabled)" : "");
    }

    // ---------------------------------------------------------------
    // Step 2 -- Start target process connector
    // ---------------------------------------------------------------
    StartProcessConnectionThread();
    std::printf("[MAIN] Process connector ready; overlay can open before %s.\n\n",
        kTargetProcessName);
    Diagnostics::Info("Process connector ready; waiting for %s in background.",
        kTargetProcessName);

    // ---------------------------------------------------------------
    // Step 3 -- Resolve global encryption keys (SKIP: vestigial)
    // May 2026 DecryptComponent reads key material directly from game
    // memory and does not use GlobalKey1/2.  GetGlobalKey() is kept
    // as a no-op for diagnostic probes but no longer blocks startup.
    // ---------------------------------------------------------------
    std::printf("[MAIN] Skipping global key resolution (not used by current decrypt).\n\n");
    Diagnostics::SetKeyStatus(Diagnostics::KeyStatus::Skipped);
    Diagnostics::Info("Global key resolution skipped; current decrypt path reads key material directly.");

    // ---------------------------------------------------------------
    // Step 4 -- Initialise the DX11 overlay windows
    // ---------------------------------------------------------------
    std::printf("[MAIN] Initialising overlay...\n");
    Diagnostics::Info("Initialising overlay.");
    if (!g_Overlay.Initialize(L"Unleashed DMA Overlay")) {
        std::fprintf(stderr, "[FATAL] Overlay initialisation failed.\n");
        Diagnostics::Error("Overlay initialisation failed.");
        OW::Config::doingentity = 0;
        TestServer::Stop();
        KeyState::Stop();
        StopProcessConnectionThread();
        OW::ProcessConnection::SetStatus(false, false, 0, 0, "Shutting down");
        StopDiagnosticStatusThread();
        mem.CloseDma();
        Diagnostics::SetDmaReady(false);
        Diagnostics::SetProcessAttached(false);
        ShutdownKmboxRuntime("overlay_init_failure");
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
    // Step 5 -- Main loop (blocks until overlay / game closes)
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
    StopHeroSkillRuntimeThread();
    OW::CancelActiveSkill();
    TestServer::Stop();
    StopProcessConnectionThread();
    StopDiagnosticStatusThread();
    Diagnostics::DumpStatus();

    OW::Config::doingentity = 0;
    KeyState::Stop();
    OW::ProcessConnection::SetStatus(false, false, 0, 0, "Shutting down");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    mem.CloseDma();
    Diagnostics::SetDmaReady(false);
    Diagnostics::SetProcessAttached(false);
    Diagnostics::Info("DMA subsystem closed.");
    ShutdownKmboxRuntime("overlay_shutdown");
    Diagnostics::ShutdownAimLog();
    Diagnostics::Shutdown();

    std::printf("[MAIN] Goodbye.\n");
    return 0;
}
