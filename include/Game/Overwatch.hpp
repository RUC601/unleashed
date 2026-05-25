#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <ctime>
#include <utility>
#include <cstring>
#include <windows.h>
#include <process.h>
#include <DirectXMath.h>

#include "Game/Target.hpp"
#include "Renderer/Renderer.hpp"
#include "Utils/Diagnostics.hpp"

using namespace OW;

// =========================================================================
// Global state variables
// =========================================================================

namespace OW {

    // ---- View matrices (updated by viewmatrix_thread) ----
    inline uint64_t viewMatrixPtr = 0;
    inline uint64_t viewMatrix_xor_ptr = 0;
    inline Matrix viewMatrix{};
    inline Matrix viewMatrix_xor{};

    // ---- Entity containers ----
    inline std::vector<c_entity> entities{};
    inline std::vector<hpanddy> hp_dy_entities{};
    inline c_entity local_entity{};

    // ---- Raw entity scan exchange buffer ----
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities{};
    inline std::vector<std::pair<uint64_t, uint64_t>> ow_entities_scan{};

    // ---- Screen / window ----
    inline float WX = 0.f, WY = 0.f;

    // ---- Scan coordination ----
    inline int abletotread = 0;
    inline int howbigentitysize = 0;
} // namespace OW

inline std::mutex g_mutex;

// =========================================================================
// Entity scan thread (lightweight, just calls get_ow_entities)
// =========================================================================

inline void entity_scan_thread() {
    Diagnostics::Info("Entity scan thread started.");
    while (OW::Config::doingentity == 1) {
        bool should_scan = false;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            should_scan = OW::abletotread == 0;
        }

        if (should_scan) {
            std::vector<std::pair<uint64_t, uint64_t>> scanned = OW::get_ow_entities();
            Diagnostics::RecordEntityScanCycle(scanned.size());
            Diagnostics::Trace("Entity scan cycle found %zu raw entities.", scanned.size());
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (OW::abletotread == 0) {
                    OW::ow_entities_scan = std::move(scanned);
                    OW::abletotread = 1;
                }
            }
        }
        Sleep(10);
    }
    Diagnostics::Info("Entity scan thread stopping.");
}

// =========================================================================
// Entity processing thread (decrypts components, builds c_entity list)
// =========================================================================

inline void entity_thread() {
    int entitytime = 0;
    Vector3 lastpos{};

    while (OW::Config::doingentity == 1) {
        SDK->BeginFrame();

        if (entitytime == 0) entitytime = GetTickCount();
        if (GetTickCount() - entitytime >= 100) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (OW::abletotread) {
                OW::ow_entities = OW::ow_entities_scan;
                OW::abletotread = 0;
                entitytime = GetTickCount();
            }
        }

        std::vector<std::pair<uint64_t, uint64_t>> raw_entities;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            raw_entities = OW::ow_entities;
        }

        // No entities available
        if (raw_entities.empty()) {
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                OW::entities = {};
                OW::hp_dy_entities = {};
                Diagnostics::SetEntityCount(0);
            }
            Sleep(1000);
            continue;
        }

        std::vector<OW::c_entity> tmp_entities{};
        std::vector<OW::hpanddy> hpdy_entities{};
        OW::c_entity lastentity{};

        for (size_t i = 0; i < raw_entities.size(); i++) {
            OW::c_entity entity{};
            if (!raw_entities[i].first || !raw_entities[i].second) {
                Diagnostics::RecordInvalidEntity();
                continue;
            }
            if (i >= raw_entities.size()) continue;

            const auto& [ComponentParent, LinkParent] = raw_entities[i];
            entity.address = ComponentParent;
            if (!entity.address || !LinkParent) {
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            OW::EntityHeaderSnapshot componentHeader{};
            componentHeader.Read(ComponentParent);
            const OW::EntityHeaderSnapshot* componentSnapshot =
                componentHeader.valid ? &componentHeader : nullptr;

            OW::EntityHeaderSnapshot linkHeader{};
            const OW::EntityHeaderSnapshot* linkSnapshot = componentSnapshot;
            if (LinkParent != ComponentParent) {
                linkHeader.Read(LinkParent);
                linkSnapshot = linkHeader.valid ? &linkHeader : nullptr;
            }

            // Check for special entity IDs (HP packs, Bob, etc.)
            uint64_t ptrValue = 0;
            if (!componentHeader.ReadParentOffset(0x30, ptrValue))
                ptrValue = SDK->RPM<uint64_t>(ComponentParent + 0x30);
            uint64_t Ptr = ptrValue & 0xFFFFFFFFFFFFFFC0;
            if (Ptr && Ptr < 0xFFFFFFFFFFFFFFEF) {
                uint64_t EntityID = SDK->RPM<uint64_t>(Ptr + 0x10);
                if (EntityID == 0x400000000000060 || EntityID == 0x40000000000480A ||
                    EntityID == 0x40000000000005F || EntityID == 0x400000000002533) {
                    OW::hpanddy hpdyentity{};
                    hpdyentity.entityid = EntityID;
                    hpdyentity.MeshBase = OW::DecryptComponent(
                        ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
                    hpdyentity.POS = SDK->RPM<XMFLOAT3>(hpdyentity.MeshBase + 0x380 + 0x50);
                    hpdy_entities.push_back(hpdyentity);
                    continue;
                }
            }

            // Decrypt all component bases
            entity.HealthBase    = OW::DecryptComponent(ComponentParent, OW::TYPE_HEALTH, componentSnapshot);
            entity.LinkBase      = OW::DecryptComponent(LinkParent, OW::TYPE_LINK, linkSnapshot);
            entity.TeamBase      = OW::DecryptComponent(ComponentParent, OW::TYPE_TEAM, componentSnapshot);
            entity.VelocityBase  = OW::DecryptComponent(ComponentParent, OW::TYPE_VELOCITY, componentSnapshot);
            entity.HeroBase      = OW::DecryptComponent(LinkParent, OW::TYPE_P_HEROID, linkSnapshot);
            entity.BoneBase      = OW::DecryptComponent(ComponentParent, OW::TYPE_BONE, componentSnapshot);
            entity.RotationBase  = OW::DecryptComponent(ComponentParent, OW::TYPE_ROTATION, componentSnapshot);
            entity.SkillBase     = OW::DecryptComponent(ComponentParent, OW::TYPE_SKILL, componentSnapshot);
            entity.VisBase       = OW::DecryptComponent(LinkParent, OW::TYPE_P_VISIBILITY, linkSnapshot);
            entity.AngleBase     = OW::DecryptComponent(LinkParent, OW::TYPE_PLAYERCONTROLLER, linkSnapshot);
            entity.EnemyAngleBase = OW::DecryptComponent(ComponentParent, OW::TYPE_ANGLE, componentSnapshot);

            // Skip duplicates
            if (entity == lastentity) continue;
            lastentity = entity;

            // ---- Health ----
            if (entity.HealthBase) {
                auto health_compo = SDK->RPM<OW::health_compo_t>(entity.HealthBase);
                Vector2 healthext = SDK->RPM<Vector2>(entity.HealthBase + 0xF0);
                entity.PlayerHealth    = health_compo.health + health_compo.armor + health_compo.barrier + healthext.Y;
                entity.PlayerHealthMax = health_compo.health_max + health_compo.armor_max + health_compo.barrier_max + healthext.X;
                entity.MinHealth       = health_compo.health;
                entity.MaxHealth       = health_compo.health_max;
                entity.MinArmorHealth   = health_compo.armor;
                entity.MaxArmorHealth   = health_compo.armor_max;
                entity.MinBarrierHealth = health_compo.barrier;
                entity.MaxBarrierHealth = health_compo.barrier_max;
                entity.Alive   = (entity.PlayerHealth > 0.f);
                entity.imort   = health_compo.isImmortal;
                entity.barrprot = health_compo.isBarrierProjected;
            } else {
                Diagnostics::RecordInvalidEntity();
                continue;
            }

            // ---- Rotation ----
            if (entity.RotationBase) {
                uint64_t rotPtr = SDK->RPM<uint64_t>(entity.RotationBase + 0x7C0 + 0x10);
                entity.Rot = SDK->RPM<Vector3>(rotPtr + 0x8FC);
            }

            // ---- Velocity / position / bones ----
            if (entity.VelocityBase) {
                auto velo_compo = SDK->RPM<OW::velocity_compo_t>(entity.VelocityBase);
                entity.pos      = Vector3(velo_compo.location.x, velo_compo.location.y - 1.f, velo_compo.location.z);
                entity.velocity = Vector3(velo_compo.velocity.x, velo_compo.velocity.y, velo_compo.velocity.z);

                int head_index  = entity.GetSkel()[0];
                int neck_index  = entity.GetSkel()[1];
                int chest_index = entity.GetSkel()[2];
                entity.head_pos  = entity.GetBonePos(head_index);
                entity.neck_pos  = entity.GetBonePos(neck_index);
                entity.chest_pos = entity.GetBonePos(chest_index);
            }

            // ---- Hero ID ----
            if (entity.HeroBase) {
                auto hero_compo = SDK->RPM<OW::hero_compo_t>(entity.HeroBase);
                entity.HeroID = hero_compo.heroid;

                if (entity.HeroID == OW::eHero::HERO_WRECKINGBALL) {
                    entity.head_pos = entity.GetBonePos(entity.GetSkel()[0]);
                    entity.head_pos.Y += 0.02f;
                    entity.neck_pos = entity.GetBonePos(entity.GetSkel()[1]);
                    entity.chest_pos = entity.GetBonePos(entity.GetSkel()[2]);
                }

                if (entity.HeroID == OW::eHero::HERO_DVA &&
                    OW::GetHeroEngNames(entity.HeroID, entity.LinkBase) != "Hana") {
                    entity.imort = false;
                    entity.head_pos.Y -= 0.1f;
                    entity.chest_pos = entity.neck_pos;
                    entity.chest_pos.Y -= 0.3f;
                }

                bool isStandardBot = (entity.HeroID == OW::eHero::HERO_TRAININGBOT1 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT2 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT3 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT4 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT5 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT6 ||
                                      entity.HeroID == OW::eHero::HERO_TRAININGBOT7);
                if (isStandardBot)
                    entity.chest_pos = entity.GetBonePos(83);
            } else {
                // Fallback: identify by MaxHealth
                if (entity.MaxHealth == 225) {
                    XMFLOAT3 temppos = SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y + 1.f, temppos.z);
                    entity.HeroID = 0x16dd; // TOBTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 30) {
                    XMFLOAT3 temppos = SDK->RPM<XMFLOAT3>(entity.VelocityBase + 0x380 + 0x50);
                    entity.head_pos = Vector3(temppos.x, temppos.y, temppos.z);
                    entity.HeroID = 0x16ee; // SYMTERT
                    entity.neck_pos = entity.head_pos;
                    entity.chest_pos = entity.head_pos;
                    entity.pos = entity.neck_pos;
                } else if (entity.MaxHealth == 1000) {
                    entity.HeroID = 0x16bb; // Bob
                } else {
                    Diagnostics::RecordInvalidEntity();
                    continue;
                }
            }

            // ---- BattleTag (optional) ----
            if (OW::Config::draw_info && OW::Config::drawbattletag) {
                entity.statcombase = OW::DecryptComponent(LinkParent, OW::TYPE_STAT, linkSnapshot);
                if (entity.statcombase && entity != OW::local_entity) {
                    uintptr_t off = SDK->RPM<uintptr_t>(entity.statcombase + 0xE0);
                    char buffer[64] = "";
                    SDK->read_buf(off, buffer, sizeof(buffer));
                    entity.battletag = buffer;
                }
            }

            // ---- Team ----
            if (entity.TeamBase) {
                auto team = entity.GetTeam();
                entity.Team = (team == OW::eTeam::TEAM_DEATHMATCH || team != OW::local_entity.GetTeam());
            }

            // ---- Visibility (May 2026 UC p330: new direct decrypt from VisBase) ----
            if (entity.VisBase) {
                entity.Vis = OW::DecryptVis(entity.VisBase) != 0;
            }

            // ---- Skills ----
            if (entity.SkillBase) {
                entity.skill1act = OW::IsSkillActive(entity.SkillBase + 0x40, 0, 0x28E3);
                entity.skill2act = OW::IsSkillActive(entity.SkillBase + 0x40, 0, 0x28E9);
                entity.ultimate  = OW::readult(entity.SkillBase + 0x40, 0, 0x1e32);

                // Sombra stealth: treat as invisible when translocated
                if (entity.HeroID == OW::eHero::HERO_SOMBRA && entity.Team &&
                    !OW::Config::Rage && !OW::Config::fov360 &&
                    !OW::Config::silent && !OW::Config::fakesilent) {
                    entity.Vis = (entity.Vis && !OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x7C5));
                }
            }

            // ---- Player controller / local entity detection ----
            if (entity.AngleBase) {
                float dist = Vector3(OW::viewMatrix_xor.get_location().x,
                                     OW::viewMatrix_xor.get_location().y,
                                     OW::viewMatrix_xor.get_location().z)
                             .DistTo(entity.head_pos);
                if (dist <= 1.f && OW::GetHeroEngNames(entity.HeroID, entity.LinkBase) != "Unknown") {
                    entity.skillcd1 = OW::readskillcd(entity.SkillBase + 0x40, 0, 0x189c);
                    entity.skillcd2 = OW::readskillcd(entity.SkillBase + 0x40, 0, 0x1f89);
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        OW::local_entity = entity;
                        SDK->g_player_controller = entity.AngleBase;
                    }
                    OW::Config::reloading = OW::IsSkillActivate1(entity.SkillBase + 0x40, 0, 0x4BF);
                    if (entity.GetTeam() == OW::eTeam::TEAM_DEATHMATCH)
                        entity.Team = false;
                }
            }

            // Add to list if valid
            std::string name = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
            if (ComponentParent && LinkParent && name != "Unknown")
                tmp_entities.push_back(entity);
            else
                Diagnostics::RecordInvalidEntity();
        }

        // Swap processed entities
        const size_t valid_count = tmp_entities.size();
        const size_t dynamic_count = hpdy_entities.size();
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            OW::entities = std::move(tmp_entities);
            OW::hp_dy_entities = std::move(hpdy_entities);
        }
        Diagnostics::SetEntityCount(valid_count);
        Diagnostics::Trace("Entity process cycle: valid=%zu hp_dynamic=%zu raw=%zu.",
            valid_count, dynamic_count, raw_entities.size());
        Sleep(3);
    }
}

// =========================================================================
// View matrix reader thread
// =========================================================================

inline void viewmatrix_thread() {
    __try {
        while (true) {
            // VM11 (May 2026): three-key subtract-XOR-subtract chain
            // enc = RPM(base + Addr); dec = ((enc - k1) ^ k2) - k3
            // p1 = RPM(dec + 0x20); p2 = RPM(p1 + 0x48)
            // view = p2 + 0x140; proj = p2 + 0xB0
            uint64_t enc = SDK->RPM<uint64_t>(SDK->dwGameBase + OW::offset::Address_viewmatrix_base);
            if (!enc) { Sleep(5); continue; }
            uint64_t dec = ((enc - OW::offset::offset_viewmatrix_xor_key)
                         ^ OW::offset::offset_viewmatrix_xor_key2)
                         - OW::offset::offset_viewmatrix_xor_key3;
            if (!dec) { Sleep(5); continue; }

            uint64_t p1 = SDK->RPM<uint64_t>(dec + OW::offset::VM_P1);
            if (!p1) { Sleep(5); continue; }
            uint64_t p2 = SDK->RPM<uint64_t>(p1 + OW::offset::VM_P2);
            if (!p2) { Sleep(5); continue; }

            // Get window rectangle from Overwatch main window
            static RECT TempRect = { 0 };
            static POINT TempPoint;
            HWND tWnd = FindWindowA("TankWindowClass", NULL);
            if (tWnd) {
                GetClientRect(tWnd, &TempRect);
                ClientToScreen(tWnd, &TempPoint);
                TempRect.left = TempPoint.x;
                TempRect.top  = TempPoint.y;
                OW::WX = (float)TempRect.right;
                OW::WY = (float)TempRect.bottom;
            }

            // Read matrices: proj at +0xB0, view at +0x140
            viewMatrixPtr = p2 + OW::offset::VM_ProjMatrix;
            viewMatrix_xor_ptr = p2 + OW::offset::VM_ViewMatrix;
            static constexpr size_t viewOffset = static_cast<size_t>(
                OW::offset::VM_ViewMatrix - OW::offset::VM_ProjMatrix);
            uint8_t matrixBuffer[viewOffset + sizeof(OW::Matrix)] = {};
            if (SDK->read_range(viewMatrixPtr, matrixBuffer, sizeof(matrixBuffer))) {
                memcpy(&OW::viewMatrix, matrixBuffer, sizeof(OW::Matrix));
                memcpy(&OW::viewMatrix_xor, matrixBuffer + viewOffset, sizeof(OW::Matrix));
            } else {
                OW::viewMatrix = SDK->RPM<OW::Matrix>(viewMatrixPtr);
                OW::viewMatrix_xor = SDK->RPM<OW::Matrix>(viewMatrix_xor_ptr);
            }

            Sleep(5);
        }
    } __except (1) {}
}

// =========================================================================
// ESP rendering helpers (require ImGui and Render:: namespace)
// =========================================================================

namespace OverlayRenderDetail {

    inline ImU32 ToImU32(const ImVec4& color) {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    inline bool IsValidScreenPoint(const Vector2& point) {
        return point.X > 0.0f && point.Y > 0.0f &&
               point.X < OW::WX && point.Y < OW::WY &&
               std::isfinite(point.X) && std::isfinite(point.Y);
    }

    inline bool IsSpecialEntity(const OW::c_entity& entity) {
        return entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb;
    }

    inline ImU32 EntityColor(const OW::c_entity& entity, size_t index) {
        if (entity.Team && OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi)) {
            return ToImU32(OW::Config::targetargb);
        }
        return entity.Team ? ToImU32(OW::Config::enargb)
                           : ToImU32(OW::Config::allyargb);
    }

    inline Render::Color EntityRenderColor(const OW::c_entity& entity, size_t index) {
        ImVec4 src = entity.Team ? OW::Config::enargb : OW::Config::allyargb;
        if (entity.Team && OW::Config::Targetenemyi >= 0 &&
            index == static_cast<size_t>(OW::Config::Targetenemyi)) {
            src = OW::Config::targetargb;
        }
        return Render::Color(
            static_cast<int>(src.x * 255.0f),
            static_cast<int>(src.y * 255.0f),
            static_cast<int>(src.z * 255.0f),
            static_cast<int>(src.w * 255.0f)
        );
    }

    inline void DrawCenteredText(const ImVec2& center, ImU32 color, const std::string& text, float fontSize) {
        if (text.empty()) return;
        ImVec2 size = ImGui::CalcTextSize(text.c_str());
        Render::DrawStrokeText(ImVec2(center.x - size.x * 0.5f, center.y), color, text.c_str(), fontSize);
    }

    inline void DrawBoneSegment(const Vector2& from, const Vector2& to, const Render::Color& color) {
        if (IsValidScreenPoint(from) && IsValidScreenPoint(to))
            Render::DrawLine(from, to, color, 1.0f);
    }

    inline void DrawSkeleton(OW::c_entity entity, const Render::Color& color) {
        std::array<int, 18> indices = entity.GetSkel();
        Vector2 points[18]{};
        bool projected[18]{};
        const Vector2 windowSize(OW::WX, OW::WY);

        for (int i = 0; i < 18; ++i) {
            Vector3 bonePos = entity.GetBonePos(indices[i]);
            projected[i] = OW::viewMatrix.WorldToScreen(bonePos, &points[i], windowSize);
        }

        auto draw = [&](int from, int to) {
            if (from < 0 || from >= 18 || to < 0 || to >= 18) return;
            if (projected[from] && projected[to])
                DrawBoneSegment(points[from], points[to], color);
        };

        draw(0, 1);   // head to neck
        draw(1, 2);   // neck to chest
        draw(2, 3);   // chest to pelvis

        draw(1, 4);   // left arm
        draw(4, 6);
        draw(6, 12);

        draw(1, 5);   // right arm
        draw(5, 7);
        draw(7, 13);

        draw(3, 8);   // left leg
        draw(8, 10);

        draw(3, 9);   // right leg
        draw(9, 11);
    }

} // namespace OverlayRenderDetail

inline void PlayerInfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    auto local_snapshot = OW::TargetingDetail::SnapshotLocalEntity();

    if (entity_snapshot.empty()) return;
    if (OW::WX <= 0.0f || OW::WY <= 0.0f) return;

    for (size_t index = 0; index < entity_snapshot.size(); ++index) {
        OW::c_entity entity = entity_snapshot[index];
        if (!entity.Alive || local_snapshot.PlayerHealth <= 0.f) continue;
        if (entity.address == local_snapshot.address) continue;

        Vector3 Vec3 = entity.head_pos;
        float dist = Vector3(OW::viewMatrix_xor.get_location().x,
                             OW::viewMatrix_xor.get_location().y,
                             OW::viewMatrix_xor.get_location().z).DistTo(Vec3);
        Vector2 Vec2_A{}, Vec2_B{};
        if (!OW::viewMatrix.WorldToScreen(Vector3(Vec3.X, Vec3.Y - 1.5f, Vec3.Z), &Vec2_A, Vector2(OW::WX, OW::WY)))
            continue;
        if (!OW::viewMatrix.WorldToScreen(Vector3(Vec3.X, Vec3.Y + 1.f, Vec3.Z), &Vec2_B, Vector2(OW::WX, OW::WY)))
            continue;

        float height = fabsf(Vec2_A.Y - Vec2_B.Y);
        float width  = height * 0.85f;
        if (height <= 2.0f || width <= 2.0f || !std::isfinite(height) || !std::isfinite(width))
            continue;

        float top    = (Vec2_A.Y < Vec2_B.Y) ? Vec2_A.Y : Vec2_B.Y;
        float bottom = (Vec2_A.Y > Vec2_B.Y) ? Vec2_A.Y : Vec2_B.Y;
        float centerX = (Vec2_A.X + Vec2_B.X) * 0.5f;
        float left = centerX - width * 0.5f;

        ImU32 color = OverlayRenderDetail::EntityColor(entity, index);
        Render::Color lineColor = OverlayRenderDetail::EntityRenderColor(entity, index);

        if (OW::Config::draw_info || OW::Config::draw_edge || OW::Config::drawbox3d) {
            Render::DrawCorneredBox(left, top, width, bottom - top, color, 1.5f);
        }

        if (OW::Config::drawhealth && OW::Config::healthbar) {
            Render::DrawHealthBar(Vector2(left - 7.0f, top), bottom - top,
                                  entity.PlayerHealth, entity.PlayerHealthMax);
        }

        if (OW::Config::drawhealth && OW::Config::healthbar2) {
            int shield = static_cast<int>(entity.MinArmorHealth + entity.MinBarrierHealth);
            int maxShield = static_cast<int>(entity.MaxArmorHealth + entity.MaxBarrierHealth);
            Render::DrawSeerLikeHealth(centerX, bottom + 26.0f, shield, maxShield,
                                        static_cast<int>(entity.MinHealth),
                                        static_cast<int>(entity.MaxHealth));
        }

        if (OW::Config::drawline) {
            Render::DrawLine(Vector2(OW::WX * 0.5f, OW::WY), Vector2(centerX, bottom), lineColor, 1.0f);
        }

        if (OW::Config::draw_skel && !OverlayRenderDetail::IsSpecialEntity(entity)) {
            OverlayRenderDetail::DrawSkeleton(entity, lineColor);
        }

        if (OW::Config::eyeray) {
            Vector2 eyeStart{}, eyeEnd{};
            Vector3 rayEnd(Vec3.X + sinf(entity.Rot.X) * 5.0f, Vec3.Y, Vec3.Z + cosf(entity.Rot.X) * 5.0f);
            if (OW::viewMatrix.WorldToScreen(Vec3, &eyeStart, Vector2(OW::WX, OW::WY)) &&
                OW::viewMatrix.WorldToScreen(rayEnd, &eyeEnd, Vector2(OW::WX, OW::WY))) {
                Render::DrawLine(eyeStart, eyeEnd, lineColor, 1.0f);
            }
        }

        if (OW::Config::dist) {
            std::string distanceText = std::to_string(static_cast<int>(dist)) + "m";
            OverlayRenderDetail::DrawCenteredText(ImVec2(centerX, bottom + 4.0f), color, distanceText, 14.0f);
        }

        if (OW::Config::draw_info && (OW::Config::name || OW::Config::ult)) {
            std::string label;
            std::string heroName = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
            if (OW::Config::name && heroName != "Unknown") {
                label = heroName;
            }
            if (OW::Config::ult && !OverlayRenderDetail::IsSpecialEntity(entity)) {
                if (!label.empty()) label += " ";
                label += "Ult " + std::to_string(static_cast<int>(entity.ultimate)) + "%";
            }
            if (!label.empty()) {
                Render::DrawInfo(ImVec2(centerX, top - 10.0f), color, 14.0f, label.c_str(),
                                 dist, entity.PlayerHealth, entity.PlayerHealthMax);
            }
        }
    }
}

inline void skillinfo() {
    auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
    if (!OW::Config::skillinfo || entity_snapshot.empty()) return;
    int i = 10;
    for (OW::c_entity entity : entity_snapshot) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Enemy: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        } else if (entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Enemy Entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        }
    }
    i += 60;
    for (OW::c_entity entity : entity_snapshot) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (!entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Ally: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        } else if (!entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Ally entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            Render::DrawSKILL(ImVec2(10.0f, static_cast<float>(i)), info);
            i += 20;
        }
    }
}

// =========================================================================
// Main aimbot thread
// =========================================================================

namespace AimbotDetail {

    struct RuntimeState {
        int hitbotdelaytime = 0;
        int afterdelaytime = 0;
        bool dodelay = false;
    };

    struct AimData {
        Vector3 local_angle{};
        Vector3 target_angle{};
        Vector3 smoothed_angle{};
        Vector3 local_pos{};
    };

    inline bool IsZeroVector(const Vector3& value) {
        return value == Vector3(0, 0, 0);
    }

    inline Vector3 CameraPosition() {
        const auto camera = OW::viewMatrix_xor.get_location();
        return Vector3(camera.x, camera.y, camera.z);
    }

    inline OW::c_entity LocalEntity() {
        return OW::TargetingDetail::SnapshotLocalEntity();
    }

    inline bool HasEntitySnapshot() {
        return !OW::TargetingDetail::SnapshotEntities().empty();
    }

    inline void MaintainSensitivity(float& origin_sens) {
        const uintptr_t sensitive_ptr = OW::GetSenstivePTR();
        const float current_sens = SDK->RPM<float>(sensitive_ptr);
        if (current_sens) origin_sens = current_sens;
        else if (origin_sens) SDK->WPM<float>(sensitive_ptr, origin_sens);
    }

    inline void SetSensitivityLocked(bool locked, float origin_sens) {
        if (!OW::Config::lockontarget) return;
        SDK->WPM<float>(OW::GetSenstivePTR(), locked ? 0.f : origin_sens);
    }

    inline void PressWithSensitivity(uint32_t key, float origin_sens, DWORD sleep_ms = 1) {
        SetSensitivityLocked(true, origin_sens);
        OW::SetKey(key);
        Sleep(sleep_ms);
        SetSensitivityLocked(false, origin_sens);
    }

    inline bool CurrentTarget(c_entity& target, bool requireVisible = true) {
        return OW::TryGetTargetEntity(OW::Config::Targetenemyi, target, requireVisible);
    }

    inline bool IsPrimaryTargetActionable(c_entity& target) {
        if (!CurrentTarget(target, true)) return false;
        if (target.skill2act && target.HeroID == OW::eHero::HERO_GENJI) return false;
        if (target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE) return false;
        if ((target.imort || target.barrprot) && !OW::Config::switch_team) return false;
        return true;
    }

    inline bool IsTriggerTargetActionable() {
        c_entity target{};
        if (!CurrentTarget(target, true)) return false;

        c_entity local = LocalEntity();
        if (target.skill2act &&
            target.HeroID == OW::eHero::HERO_GENJI &&
            target.GetTeam() != local.GetTeam()) {
            return false;
        }
        return true;
    }

    inline AimData BuildAimData(const Vector3& world_target, bool accelerated, float smooth, float acceleration) {
        AimData data{};
        data.local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
        const auto calc_target = OW::CalcAngle(XMFLOAT3(world_target.X, world_target.Y, world_target.Z),
                                               OW::viewMatrix_xor.get_location());
        data.target_angle = Vector3(calc_target.x, calc_target.y, calc_target.z);
        data.smoothed_angle = accelerated
            ? OW::SmoothAccelerate(data.local_angle, data.target_angle, smooth, acceleration)
            : OW::SmoothLinear(data.local_angle, data.target_angle, smooth);
        data.local_pos = CameraPosition();
        return data;
    }

    inline float AimNoise(float divisor) {
        const float direction = (rand() % 10 > 5) ? 1.f : -1.f;
        return direction * static_cast<float>(rand()) / RAND_MAX / divisor;
    }

    inline void ApplyAiAimNoise(Vector3& target, float divisor, bool clampSecondaryFov) {
        if (!OW::Config::aiaim) return;
        target.X += AimNoise(divisor);
        target.Y += AimNoise(divisor);
        target.Z += AimNoise(divisor);

        if (OW::Config::minFov1 > 500.f) OW::Config::minFov1 = 500.f;
        if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
        if (clampSecondaryFov) {
            if (OW::Config::minFov2 > 500.f) OW::Config::minFov2 = 500.f;
            if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
        }
        if (OW::Config::fov360) OW::Config::fov360 = false;
    }

    inline bool TargetDelayReady(RuntimeState* state, bool stampHitDelay, bool resetWhenDisabled) {
        if (!OW::Config::targetdelay) {
            if (resetWhenDisabled && OW::Config::doingdelay) OW::Config::doingdelay = false;
            return true;
        }

        if (OW::Config::lastenemy != OW::Config::Targetenemyi) OW::Config::doingdelay = true;
        if (!OW::Config::doingdelay) return true;

        OW::Config::lastenemy = OW::Config::Targetenemyi;
        if (OW::Config::timebeforedelay == 0) {
            OW::Config::timebeforedelay = GetTickCount();
            return false;
        }

        if (GetTickCount() - OW::Config::timebeforedelay < static_cast<DWORD>(OW::Config::targetdelaytime))
            return false;

        OW::Config::timebeforedelay = 0;
        OW::Config::doingdelay = false;
        if (stampHitDelay && state) state->hitbotdelaytime = GetTickCount();
        return true;
    }

    inline void ArmDelayedShot(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot) return;
        if (OW::Config::shooted || !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) {
            state.dodelay = true;
            state.hitbotdelaytime = 0;
        }
    }

    inline void PrimeDelayedShot(RuntimeState& state) {
        if (state.dodelay && !OW::Config::doingdelay) {
            state.hitbotdelaytime = GetTickCount();
            state.dodelay = false;
        }
    }

    inline bool DelayedShotTimedOut(RuntimeState& state) {
        if (!OW::Config::hitboxdelayshoot || state.hitbotdelaytime == 0) return false;
        state.afterdelaytime = GetTickCount();
        return state.afterdelaytime - state.hitbotdelaytime > OW::Config::hiboxdelaytime &&
               !OW::Config::doingdelay;
    }

    inline void FirePrimaryNormal() {
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_GENJI || local.HeroID == OW::eHero::HERO_KIRIKO) {
            OW::SetKey(0x2);
            if (OW::Config::dontshot) OW::Config::shotcount++;
            return;
        }

        if ((local.HeroID == OW::eHero::HERO_ANA ||
             local.HeroID == OW::eHero::HERO_WIDOWMAKER ||
             local.HeroID == OW::eHero::HERO_ASHE) && GetAsyncKeyState(0x2)) {
            OW::SetKeyscopeHold(0x1, 30);
        } else {
            OW::SetKey(0x1);
        }
    }

    inline void FireHanzo() {
        const c_entity local = LocalEntity();
        if (local.skill2act) OW::SetKey(0x1);
        else OW::SetKeyHold(0x1000, 100);
    }

    inline void RunAutoScaleFov() {
        if (!OW::Config::autoscalefov) return;

        auto fvec = OW::GetVector3forfov();
        c_entity fov_target{};
        if (IsZeroVector(fvec) || !OW::TryGetTargetEntity(OW::Config::Targetenemyifov, fov_target, true)) {
            OW::Config::Fov = OW::Config::minFov1;
            OW::Config::Fov2 = OW::Config::minFov2;
            return;
        }

        Vector2 high{}, low{};
        if (OW::viewMatrix.WorldToScreen(fov_target.head_pos, &high, Vector2(OW::WX, OW::WY)) &&
            OW::viewMatrix.WorldToScreen(fov_target.chest_pos, &low, Vector2(OW::WX, OW::WY))) {
            OW::Config::Fov = -(high.Y - low.Y) * 4.f;
            if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
            else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;

            OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
            if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
            else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
        } else {
            OW::Config::Fov = OW::Config::minFov1;
            OW::Config::Fov2 = OW::Config::minFov2;
        }
    }

    inline void RunCloseRangeActions(const Vector3& target_pos) {
        const float dist = CameraPosition().DistTo(target_pos);
        if (OW::Config::health <= OW::Config::meleehealth &&
            dist <= OW::Config::meleedistance &&
            OW::Config::AutoMelee) {
            OW::SetKey(0x800);
        }
        if (OW::Config::health <= OW::Config::AutoRMBhealth &&
            dist <= OW::Config::AutoRMBdistance &&
            OW::Config::AutoRMB) {
            OW::SetKey(0x2);
        }
    }

    inline void RunTriggerbot(bool secondary, float origin_sens) {
        const Vector3 vec = secondary ? OW::GetVector3aim2(OW::Config::Prediction2)
                                      : OW::GetVector3(OW::Config::Prediction);
        if (IsZeroVector(vec) || !IsTriggerTargetActionable()) return;

        AimData aim = BuildAimData(vec, false, 1.0f, 0.0f);
        const float hitbox = secondary ? OW::Config::hitbox2 : OW::Config::hitbox;
        if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, hitbox))
            PressWithSensitivity(0x1, origin_sens, 2);
    }

    inline bool ShouldYieldToSecondaryAim() {
        return OW::Config::highPriority && GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2));
    }

    inline void RunTracking(float origin_sens) {
        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::reloading) {
            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            c_entity target{};
            if (!IsZeroVector(vec) && IsPrimaryTargetActionable(target)) {
                AimData aim = BuildAimData(vec, false, OW::Config::Tracking_smooth / 10.f, 0.0f);
                ApplyAiAimNoise(aim.smoothed_angle, 500.f, true);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (!TargetDelayReady(nullptr, false, false)) continue;
                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170,
                                      OW::Config::Rage ? aim.target_angle : aim.smoothed_angle);
                    RunCloseRangeActions(vec);
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
    }

    inline bool RunPrimaryRageShot(const AimData& aim, float origin_sens) {
        if (!OW::Config::Rage) return false;

        if (OW::Config::fakesilent) {
            Vector3 original_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.target_angle);
            PressWithSensitivity(0x1, origin_sens, 25);
            OW::Config::shooted = true;
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, original_angle);
        } else {
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.target_angle);
            PressWithSensitivity(0x1, origin_sens, 1);
            OW::Config::shooted = true;
        }
        return true;
    }

    inline bool RunHanzoRageShot(const AimData& aim, float origin_sens) {
        if (!OW::Config::Rage) return false;

        if (OW::Config::fakesilent) {
            Vector3 original_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.target_angle);
            SetSensitivityLocked(true, origin_sens);
            FireHanzo();
            Sleep(25);
            SetSensitivityLocked(false, origin_sens);
            OW::Config::shooted = true;
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, original_angle);
        } else {
            SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.target_angle);
            FireHanzo();
            OW::Config::shooted = true;
        }
        return true;
    }

    inline void RunFlick(RuntimeState& state, float origin_sens) {
        ArmDelayedShot(state);

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) &&
               !OW::Config::shooted &&
               !OW::Config::reloading) {
            if (LocalEntity().HeroID == OW::eHero::HERO_WIDOWMAKER && !GetAsyncKeyState(0x2)) {
                Sleep(1);
                continue;
            }

            const Vector3 vec = OW::GetVector3(OW::Config::Prediction);
            if (IsZeroVector(vec)) break;

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                if (!TargetDelayReady(&state, true, true)) continue;
                PrimeDelayedShot(state);

                AimData aim = BuildAimData(vec, true, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (DelayedShotTimedOut(state)) {
                        const c_entity local = LocalEntity();
                        OW::SetKey((local.HeroID == OW::eHero::HERO_GENJI ||
                                    local.HeroID == OW::eHero::HERO_KIRIKO) ? 0x2 : 0x1);
                        OW::Config::shooted = true;
                        continue;
                    }

                    if (RunPrimaryRageShot(aim, origin_sens)) continue;

                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.smoothed_angle);
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox)) {
                        SetSensitivityLocked(true, origin_sens);
                        FirePrimaryNormal();
                        SetSensitivityLocked(false, origin_sens);
                        OW::Config::shooted = true;
                        if (OW::Config::dontshot) OW::Config::shotcount++;
                        break;
                    }

                    if (OW::Config::dontshot &&
                        OW::Config::shotcount >= OW::Config::shotmanydont &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::missbox)) {
                        OW::Config::shotcount = 0;
                        const c_entity local = LocalEntity();
                        OW::SetKey((local.HeroID == OW::eHero::HERO_GENJI ||
                                    local.HeroID == OW::eHero::HERO_KIRIKO) ? 0x2 : 0x1);
                        OW::Config::shooted = true;
                        continue;
                    }
                }
            }

            Sleep(1);
            RunAutoScaleFov();
            if (ShouldYieldToSecondaryAim()) break;
        }
    }

    inline void RunHanzoFlick(RuntimeState& state, float origin_sens) {
        ArmDelayedShot(state);

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::shooted) {
            const Vector3 vec = OW::GetVector3(true);
            if (IsZeroVector(vec)) break;

            c_entity target{};
            if (IsPrimaryTargetActionable(target)) {
                AimData aim = BuildAimData(vec, true, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                PrimeDelayedShot(state);
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    if (DelayedShotTimedOut(state)) {
                        FireHanzo();
                        OW::Config::shooted = true;
                        continue;
                    }

                    if (RunHanzoRageShot(aim, origin_sens)) continue;
                    if (!TargetDelayReady(&state, true, true)) continue;

                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.smoothed_angle);
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox)) {
                        SetSensitivityLocked(true, origin_sens);
                        FireHanzo();
                        Sleep(1);
                        if (OW::Config::dontshot) OW::Config::shotcount++;
                        SetSensitivityLocked(false, origin_sens);
                        OW::Config::shooted = true;
                    } else if (OW::Config::dontshot &&
                               OW::Config::shotcount >= OW::Config::shotmanydont &&
                               OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::missbox)) {
                        OW::Config::shotcount = 0;
                        FireHanzo();
                        OW::Config::shooted = true;
                        continue;
                    }
                }
            }

            Sleep(1);
            RunAutoScaleFov();
        }
    }

    inline void RunGenjiBlade() {
        c_entity local = LocalEntity();
        if (!OW::Config::GenjiBlade || !GetAsyncKeyState(0x51) ||
            local.HeroID != OW::eHero::HERO_GENJI ||
            local.ultimate != 100.f) {
            return;
        }

        OW::Config::Qstarttime = GetTickCount();
        OW::Config::Qtime = OW::Config::Qstarttime;
        OW::Config::lastenemy = -1;
        Sleep(1000);

        int detecttoggle = 0;
        int first = 1;
        float speed = 0.f;
        while (OW::Config::GenjiBlade && (OW::Config::Qtime - OW::Config::Qstarttime) <= 7000) {
            local = LocalEntity();
            speed = !local.skillcd1 ? OW::Config::Tracking_smooth : OW::Config::bladespeed;
            OW::Config::Qtime = GetTickCount();

            const Vector3 vec = OW::GetVector3forgenji();
            if (!IsZeroVector(vec)) {
                const float dist = CameraPosition().DistTo(vec);
                if (dist > 20.f) continue;

                AimData aim = BuildAimData(vec, false, speed / 10.f, 0.0f);
                if (!IsZeroVector(aim.smoothed_angle)) {
                    const float dist2 = CameraPosition().DistTo(vec);
                    if ((!local.skillcd1 && dist2 < 20.f) || dist2 < 7.f) {
                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170,
                                          OW::Config::Rage ? aim.target_angle : aim.smoothed_angle);
                    }
                    if (!local.skillcd1 && OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 0.8f)) {
                        if (detecttoggle && !first) {
                            detecttoggle = 0;
                            Sleep(50);
                            continue;
                        }
                        OW::SetKeyHold(0x8, 70);
                        first = 0;
                    }
                    if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f) && dist2 < 5.f)
                        OW::SetKey(0x1);
                    if (local.skillcd1 != 0 && !detecttoggle) detecttoggle = 1;
                }
            }
            Sleep(1);
            OW::Config::lastenemy = OW::Config::Targetenemyi;
        }
    }

    inline void RunAutoMelee() {
        if (!OW::Config::AutoMelee) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target, true) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::meleehealth &&
                dist <= OW::Config::meleedistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                OW::SetKey(0x800);
                Sleep(1);
            }
        }
    }

    inline void RunAutoRmb() {
        if (!OW::Config::AutoRMB) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (!IsZeroVector(vec) && CurrentTarget(target, true) && target.Team) {
            const float dist = CameraPosition().DistTo(vec);
            if (OW::Config::health <= OW::Config::AutoRMBhealth &&
                dist <= OW::Config::AutoRMBdistance &&
                !(target.skill1act && target.HeroID == OW::eHero::HERO_VENTURE)) {
                OW::SetKey(0x2);
                Sleep(1);
            }
        }
    }

    inline void RunAutoShiftGenji() {
        if (!OW::Config::AutoShiftGenji) return;

        const Vector3 vec = OW::GetVector3(false);
        c_entity target{};
        if (IsZeroVector(vec) || !CurrentTarget(target, true)) return;
        if (target.imort || target.barrprot) return;
        if (target.HeroID == 0x16dd || target.HeroID == 0x16ee) return;

        const c_entity local = LocalEntity();
        if (local.skillcd1) return;

        const float dist = CameraPosition().DistTo(vec);
        AimData aim = BuildAimData(vec, false, OW::Config::Tracking_smooth / 10.f, 0.0f);
        if (OW::Config::health <= 50.f && dist <= 15.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f))
                OW::SetKeyHold(0x8, 40);
        } else if (OW::Config::health <= 80.f && dist >= 15.f && dist <= 17.f) {
            if (OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, 1.f)) {
                OW::SetKey(0x8);
                Sleep(500);
                OW::SetKey(0x800);
            }
        }
    }

    inline void RunAutoSkill() {
        if (!OW::Config::AutoSkill) return;

        const c_entity local = LocalEntity();
        if (local.PlayerHealth > OW::Config::SkillHealth && OW::Config::skilled)
            OW::Config::skilled = false;
        else if (local.PlayerHealth < OW::Config::SkillHealth && OW::Config::skilled &&
                 local.PlayerHealth < OW::Config::lasthealth &&
                 local.HeroID != OW::eHero::HERO_DOOMFIST)
            OW::Config::skilled = false;

        if (local.PlayerHealth >= OW::Config::SkillHealth || OW::Config::skilled) return;

        const auto hID = local.HeroID;
        if (hID == OW::eHero::HERO_TRACER || hID == OW::eHero::HERO_SOMBRA ||
            hID == OW::eHero::HERO_ROADHOG || hID == OW::eHero::HERO_TORBJORN ||
            hID == OW::eHero::HERO_SOLDIER76 || hID == OW::eHero::HERO_VENTURE) {
            OW::SetKey(0x10);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        } else if (hID == OW::eHero::HERO_REAPER || hID == OW::eHero::HERO_MEI ||
                   hID == OW::eHero::HERO_JUNKERQUEEN || hID == OW::eHero::HERO_MOIRA ||
                   hID == OW::eHero::HERO_ZARYA) {
            OW::SetKey(0x8);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        } else if (hID == OW::eHero::HERO_WINSTON || hID == OW::eHero::HERO_ZENYATTA) {
            OW::SetKey(0x20);
            OW::Config::skilled = true;
            Sleep(1);
            OW::Config::lasthealth = local.PlayerHealth;
        }
    }

    inline void RunAutoShootCooldown() {
        const c_entity local = LocalEntity();
        if (!OW::Config::AutoShoot || !OW::Config::shooted ||
            (local.HeroID == OW::eHero::HERO_HANJO && !local.skill2act)) {
            return;
        }

        const int rectime = GetTickCount();
        if (OW::Config::lasttime == 0) OW::Config::lasttime = rectime;
        else if (rectime - OW::Config::lasttime >= OW::Config::Shoottime) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }

        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
    }

    inline void ResetShootStateOnRelease() {
        if (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) return;

        OW::Config::shooted = false;
        OW::Config::lasttime = 0;
        if (OW::Config::reloading) {
            OW::Config::lasttime = 0;
            OW::Config::shooted = false;
        }
        OW::Config::Targetenemyi = -1;
    }

    inline void RunReaperReloadCancel() {
        const c_entity local = LocalEntity();
        if (local.HeroID == OW::eHero::HERO_REAPER && OW::Config::reloading) {
            Sleep(300);
            OW::SetKey(0x800);
        }
    }

    inline void RunSecondAim() {
        if (!OW::Config::secondaim) return;

        while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)) && !OW::Config::shooted2) {
            const Vector3 vec = OW::GetVector3aim2(OW::Config::Prediction2);
            c_entity target{};
            if (!IsZeroVector(vec) && CurrentTarget(target, true) &&
                !(target.skill2act && target.HeroID == OW::eHero::HERO_GENJI)) {
                AimData aim{};
                if (OW::Config::Tracking2)
                    aim = BuildAimData(vec, false, OW::Config::Tracking_smooth2 / 10.f, 0.0f);
                else if (OW::Config::Flick2)
                    aim = BuildAimData(vec, true, OW::Config::Flick_smooth2 / 10.f, OW::Config::accvalue2);
                else
                    aim = BuildAimData(vec, false, 1.0f, 0.0f);

                if (OW::Config::Rage) aim.smoothed_angle = aim.target_angle;
                ApplyAiAimNoise(aim.smoothed_angle, 300.f, false);

                if (!IsZeroVector(aim.smoothed_angle)) {
                    RunCloseRangeActions(vec);
                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, aim.smoothed_angle);
                    if (OW::Config::Flick2 &&
                        OW::in_range(aim.local_angle, aim.target_angle, aim.local_pos, vec, OW::Config::hitbox2)) {
                        const int tk = OW::Config::togglekey;
                        if (tk == 0)       OW::SetKey(0x1);
                        else if (tk == 1)  OW::SetKey(0x2);
                        else if (tk == 2)  OW::SetKey(0x8);
                        else if (tk == 3)  OW::SetKey(0x10);
                        else if (tk == 4)  OW::SetKey(0x20);
                        Sleep(1);
                        OW::Config::shooted2 = true;
                    }
                }

                if (LocalEntity().PlayerHealth < OW::Config::SkillHealth) break;
            }
            Sleep(1);
        }

        if (OW::Config::shooted2 && !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)))
            OW::Config::shooted2 = false;
    }

    inline void RunAimbotTick(RuntimeState& state, float& origin_sens) {
        if (OW::Config::AntiAFK) {
            OW::SetKey(0x57);
            Sleep(1000);
        }

        if (!HasEntitySnapshot()) return;

        MaintainSensitivity(origin_sens);

        if (OW::Config::triggerbot) RunTriggerbot(false, origin_sens);
        if (OW::Config::triggerbot2) RunTriggerbot(true, origin_sens);

        if (OW::Config::Tracking) RunTracking(origin_sens);
        else if (OW::Config::Flick) RunFlick(state, origin_sens);
        else if (OW::Config::hanzo_flick) RunHanzoFlick(state, origin_sens);

        RunGenjiBlade();
        RunAutoScaleFov();
        RunAutoMelee();
        RunAutoRmb();
        RunAutoShiftGenji();
        RunAutoSkill();
        RunAutoShootCooldown();
        ResetShootStateOnRelease();
        RunReaperReloadCancel();
        RunSecondAim();
    }
}

inline void aimbot_thread() {
    __try {
        AimbotDetail::RuntimeState state{};
        static float origin_sens = 0.f;

        while (true) {
            AimbotDetail::RunAimbotTick(state, origin_sens);
            Sleep(2);
        }
    } __except (1) {}
}

// =========================================================================
// Config save/load thread
// =========================================================================

namespace OW { namespace Config {
    void SaveConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
    void LoadConfigForHero(const std::string& path, uint64_t heroId, uint64_t linkBase);
}}

inline void configsavenloadthread() {
    uint64_t lastHeroId = 0;
    while (1) {
        const uint64_t currentHeroId = OW::local_entity.HeroID;
        if (!OW::Config::Menu && currentHeroId != 0 && lastHeroId != currentHeroId) {
            if (lastHeroId != 0) {
                OW::Config::SaveConfigForHero(".\\config.ini", lastHeroId, OW::local_entity.LinkBase);
            }

            OW::Config::LoadConfigForHero(".\\config.ini", currentHeroId, OW::local_entity.LinkBase);
            lastHeroId = currentHeroId;
            OW::Config::nowhero = "Now using: " + OW::GetHeroEngNames(currentHeroId, OW::local_entity.LinkBase);
        } else if (OW::Config::manualsave && lastHeroId != 0) {
            OW::Config::SaveConfigForHero(".\\config.ini", lastHeroId, OW::local_entity.LinkBase);
            OW::Config::manualsave = false;
        }
        Sleep(2);
    }

#if 0
    TCHAR bufsave[100];
    if (OW::Config::lastheroid == -2) {
        OW::Config::lastheroid = 0;
    }
    while (1) {
        if (!OW::Config::Menu && OW::Config::lastheroid != OW::local_entity.HeroID) {
            // Auto-save previous hero config
            if (OW::Config::lastheroid != 0) {
                auto saveHero = [&](const char* section, const char* key, int value) {
                    sprintf(bufsave, "%d",value);
                    WritePrivateProfileStringA(section, key, bufsave, ".\\config.ini");
                };
                auto saveHeroFloat = [&](const char* section, const char* key, float value) {
                    sprintf(bufsave, "%d",(int)(value * 10000));
                    WritePrivateProfileStringA(section, key, bufsave, ".\\config.ini");
                };

                std::string heroName = OW::GetHeroEngNames(OW::Config::lastheroid, OW::local_entity.LinkBase);
                const char* sec = heroName.c_str();

                // Save per-hero settings
                saveHero(sec, "highPriority",  OW::Config::highPriority);
                saveHero(sec, "aiaim",          OW::Config::aiaim);
                saveHero(sec, "hanzoautospeed", OW::Config::hanzoautospeed);
                saveHero(sec, "autoscalefov",   OW::Config::autoscalefov);
                saveHero(sec, "lockontarget",   OW::Config::lockontarget);
                saveHero(sec, "trackc",         OW::Config::trackcompensate);
                saveHeroFloat(sec, "comarea",   OW::Config::comarea);
                saveHeroFloat(sec, "comspeed",  OW::Config::comspeed);
                saveHero(sec, "FOV",            (int)OW::Config::Fov);
                saveHeroFloat(sec, "hitbox",    OW::Config::hitbox);
                saveHeroFloat(sec, "missbox",   OW::Config::missbox);
                saveHeroFloat(sec, "Tracking_smooth", OW::Config::Tracking_smooth);
                saveHeroFloat(sec, "Flick_smooth",    OW::Config::Flick_smooth);
                saveHero(sec, "AutoShootTime",        OW::Config::Shoottime);
                saveHero(sec, "predit_level",         (int)OW::Config::predit_level);
                saveHero(sec, "aim_key",              OW::Config::aim_key);
                saveHero(sec, "Gravitypredit",        OW::Config::Gravitypredit);
                saveHero(sec, "SkillHealth",          (int)OW::Config::SkillHealth);
                saveHero(sec, "AutoSkill",            OW::Config::AutoSkill);
                saveHero(sec, "AntiAFK",              OW::Config::AntiAFK);

                int dec = OW::Config::Tracking ? 0 : OW::Config::Flick ? 1 : OW::Config::hanzo_flick ? 2 : OW::Config::silent ? 3 : 4;
                saveHero(sec, "Aim Mode",    dec);
                saveHero(sec, "autoshootonoff", OW::Config::AutoShoot ? 1 : 0);
                saveHero(sec, "predictdec",     OW::Config::Prediction ? 1 : 0);
                saveHero(sec, "dontshot",       OW::Config::dontshot ? 1 : 0);
                saveHero(sec, "targetdelay",    OW::Config::targetdelay ? 1 : 0);
                saveHero(sec, "targetdelaytime", OW::Config::targetdelaytime);
                saveHero(sec, "dontmanyshot",   OW::Config::shotmanydont);
                saveHero(sec, "hitboxdelayshoot", OW::Config::hitboxdelayshoot);
                saveHero(sec, "hitboxdelaytime",  OW::Config::hiboxdelaytime);

                saveHeroFloat(sec, "recoilnum", OW::Config::recoilnum);
                saveHeroFloat(sec, "accvalue",  OW::Config::accvalue);
                saveHero(sec, "norecoil",    OW::Config::norecoil);
                saveHero(sec, "horizonreco", OW::Config::horizonreco);
                saveHero(sec, "switch_team", OW::Config::switch_team);
                saveHero(sec, "switch_team2", OW::Config::switch_team2);

                saveHero(sec, "Bone",      OW::Config::Bone);
                saveHero(sec, "autobone",  OW::Config::autobone);
                saveHero(sec, "Bone2",     OW::Config::Bone2);
                saveHero(sec, "autobone2", OW::Config::autobone2);
                saveHero(sec, "AutoMelee", OW::Config::AutoMelee);
                saveHeroFloat(sec, "meleedistance", OW::Config::meleedistance);
                saveHeroFloat(sec, "meleehealth",   OW::Config::meleehealth);
                saveHero(sec, "AutoRMB",   OW::Config::AutoRMB);
                saveHeroFloat(sec, "AutoRMBdistance", OW::Config::AutoRMBdistance);
                saveHeroFloat(sec, "AutoRMBhealth",   OW::Config::AutoRMBhealth);

                saveHero(sec, "secondaim",    OW::Config::secondaim);
                saveHero(sec, "triggerbot2",  OW::Config::triggerbot2);
                saveHero(sec, "Tracking2",    OW::Config::Tracking2);
                saveHero(sec, "Flick2",       OW::Config::Flick2);
                saveHero(sec, "Prediction2",  OW::Config::Prediction2);
                saveHero(sec, "Gravitypredit2", OW::Config::Gravitypredit2);
                saveHero(sec, "aim_key2",     OW::Config::aim_key2);
                saveHero(sec, "togglekey",    OW::Config::togglekey);
                saveHeroFloat(sec, "predit_level2",    OW::Config::predit_level2);
                saveHeroFloat(sec, "Tracking_smooth2", OW::Config::Tracking_smooth2);
                saveHeroFloat(sec, "Flick_smooth2",    OW::Config::Flick_smooth2);
                saveHeroFloat(sec, "accvalue2",        OW::Config::accvalue2);
                saveHeroFloat(sec, "hitbox2",          OW::Config::hitbox2);
                saveHeroFloat(sec, "Fov2",             OW::Config::Fov2);

                saveHero(sec, "enablechangefov", OW::Config::enablechangefov);
                saveHeroFloat(sec, "CHANGEFOV",  OW::Config::CHANGEFOV);

                // Genji-specific
                if (OW::Config::lastheroid == OW::eHero::HERO_GENJI) {
                    saveHero(sec, "GenjiBlade",     OW::Config::GenjiBlade);
                    saveHero(sec, "AutoShiftGenji", OW::Config::AutoShiftGenji);
                    saveHeroFloat(sec, "bladespeed", OW::Config::bladespeed);
                }
                // Widow-specific
                if (OW::Config::lastheroid == OW::eHero::HERO_WIDOWMAKER)
                    saveHero(sec, "widowautounscope", OW::Config::widowautounscope);

                // Global settings
                saveHero("Global", "draw_hp_pack",   OW::Config::draw_hp_pack);
                saveHero("Global", "crosscircle",    OW::Config::crosscircle);
                saveHero("Global", "eyeray",         OW::Config::eyeray);
                saveHero("Global", "trackback",      OW::Config::trackback);
                saveHero("Global", "draw_info",      OW::Config::draw_info);
                saveHero("Global", "drawbattletag",  OW::Config::drawbattletag);
                saveHero("Global", "drawhealth",     OW::Config::drawhealth);
                saveHero("Global", "healthbar",      OW::Config::healthbar);
                saveHero("Global", "healthbar2",     OW::Config::healthbar2);
                saveHeroFloat("Global", "healthbartextsize", OW::Config::healthbartextsize);
                saveHero("Global", "dist",           OW::Config::dist);
                saveHero("Global", "name",           OW::Config::name);
                saveHero("Global", "ult",            OW::Config::ult);
                saveHero("Global", "draw_skel",      OW::Config::draw_skel);
                saveHero("Global", "skillinfo",      OW::Config::skillinfo);
                saveHero("Global", "outline",        OW::Config::outline);
                saveHero("Global", "externaloutline", OW::Config::externaloutline);
                saveHero("Global", "teamoutline",    OW::Config::teamoutline);
                saveHero("Global", "healthoutline",  OW::Config::healthoutline);
                saveHero("Global", "rainbowoutline", OW::Config::rainbowoutline);
                saveHero("Global", "draw_edge",      OW::Config::draw_edge);
                saveHero("Global", "drawbox3d",      OW::Config::drawbox3d);
                saveHero("Global", "radar",          OW::Config::radar);
                saveHero("Global", "radarline",      OW::Config::radarline);
                saveHero("Global", "drawline",       OW::Config::drawline);
                saveHero("Global", "draw_fov",       OW::Config::draw_fov);
                saveHero("Global", "MenuToggleKey",  OW::Config::MenuToggleKey);

                // Save colors
                auto saveColor = [&](const char* section, const char* prefix, const ImVec4& c) {
                    sprintf(bufsave, "%d",(int)(c.x * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "x").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.y * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "y").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.z * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "z").c_str(), bufsave, ".\\config.ini");
                    sprintf(bufsave, "%d",(int)(c.w * 10000));
                    WritePrivateProfileStringA(section, (std::string(prefix) + "w").c_str(), bufsave, ".\\config.ini");
                };
                saveColor("Global", "EnemyCol",    OW::Config::EnemyCol);
                saveColor("Global", "fovcol",      OW::Config::fovcol);
                saveColor("Global", "fovcol2",     OW::Config::fovcol2);
                saveColor("Global", "invisenargb", OW::Config::invisnenargb);
                saveColor("Global", "enargb",      OW::Config::enargb);
                saveColor("Global", "targetargb",  OW::Config::targetargb);
                saveColor("Global", "targetargb2", OW::Config::targetargb2);
                saveColor("Global", "allyargb",    OW::Config::allyargb);

                std::string saveMsg = "Saved: " + heroName;
                // Notification is handled by overlay layer
            }

            // Load config for new hero
            auto loadHero = [&](const char* section, const char* key, int def) -> int {
                return GetPrivateProfileIntA(section, key, def, ".\\config.ini");
            };
            auto loadHeroFloat = [&](const char* section, const char* key, int def) -> float {
                return (float)GetPrivateProfileIntA(section, key, def, ".\\config.ini") / 10000.f;
            };

            std::string heroName = OW::GetHeroEngNames(OW::local_entity.HeroID, OW::local_entity.LinkBase);
            const char* sec = heroName.c_str();

            OW::Config::Fov             = (float)loadHero(sec, "FOV", 200);
            OW::Config::minFov1         = (float)loadHero(sec, "FOV", 200);
            OW::Config::comarea         = loadHeroFloat(sec, "comarea", 100);
            OW::Config::comspeed        = loadHeroFloat(sec, "comspeed", 5000);
            OW::Config::hitbox          = loadHeroFloat(sec, "hitbox", 1300);
            OW::Config::missbox         = loadHeroFloat(sec, "missbox", 6000);
            OW::Config::Tracking_smooth = loadHeroFloat(sec, "Tracking_smooth", 1000);
            OW::Config::Flick_smooth    = loadHeroFloat(sec, "Flick_smooth", 1000);
            OW::Config::Shoottime       = loadHero(sec, "AutoShootTime", 500);
            OW::Config::predit_level    = (float)loadHero(sec, "predit_level", 110);
            OW::Config::aim_key         = loadHero(sec, "aim_key", 6);
            OW::Config::dontshot        = loadHero(sec, "dontshot", 0);
            OW::Config::targetdelay     = loadHero(sec, "targetdelay", 0);
            OW::Config::targetdelaytime = loadHero(sec, "targetdelaytime", 200);
            OW::Config::shotmanydont    = loadHero(sec, "dontmanyshot", 3);
            OW::Config::hitboxdelayshoot = loadHero(sec, "hitboxdelayshoot", 0);
            OW::Config::hiboxdelaytime  = loadHero(sec, "hitboxdelaytime", 200);
            OW::Config::predit_level    = (float)loadHero(sec, "predit_level", 110);
            OW::Config::Gravitypredit   = loadHero(sec, "Gravitypredit", 0);
            OW::Config::SkillHealth     = (float)loadHero(sec, "SkillHealth", 50);
            OW::Config::AutoSkill       = loadHero(sec, "AutoSkill", 0);
            OW::Config::AntiAFK         = loadHero(sec, "AntiAFK", 0);
            OW::Config::recoilnum       = loadHeroFloat(sec, "recoilnum", 5000);
            OW::Config::accvalue        = loadHeroFloat(sec, "accvalue", 1000);
            OW::Config::norecoil        = loadHero(sec, "norecoil", 0);
            OW::Config::horizonreco     = loadHero(sec, "horizonreco", 0);
            OW::Config::switch_team     = loadHero(sec, "switch_team", 0);
            OW::Config::switch_team2    = loadHero(sec, "switch_team2", 0);
            OW::Config::Bone            = loadHero(sec, "Bone", 1);
            OW::Config::autobone        = loadHero(sec, "autobone", 0);
            OW::Config::Bone2           = loadHero(sec, "Bone2", 1);
            OW::Config::autobone2       = loadHero(sec, "autobone2", 0);
            OW::Config::AutoMelee       = loadHero(sec, "AutoMelee", 0);
            OW::Config::meleedistance   = loadHeroFloat(sec, "meleedistance", 5000);
            OW::Config::meleehealth     = loadHeroFloat(sec, "meleehealth", 3000);
            OW::Config::AutoRMB         = loadHero(sec, "AutoRMB", 0);
            OW::Config::AutoRMBdistance = loadHeroFloat(sec, "AutoRMBdistance", 3000);
            OW::Config::AutoRMBhealth   = loadHeroFloat(sec, "AutoRMBhealth", 10000);

            OW::Config::secondaim       = loadHero(sec, "secondaim", 0);
            OW::Config::triggerbot2     = loadHero(sec, "triggerbot2", 0);
            OW::Config::Tracking2       = loadHero(sec, "Tracking2", 0);
            OW::Config::Flick2          = loadHero(sec, "Flick2", 0);
            OW::Config::Prediction2     = loadHero(sec, "Prediction2", 0);
            OW::Config::Gravitypredit2  = loadHero(sec, "Gravitypredit2", 0);
            OW::Config::aim_key2        = loadHero(sec, "aim_key2", 5);
            OW::Config::togglekey       = loadHero(sec, "togglekey", 0);
            OW::Config::predit_level2   = loadHeroFloat(sec, "predit_level2", 11000);
            OW::Config::Tracking_smooth2 = loadHeroFloat(sec, "Tracking_smooth2", 1000);
            OW::Config::Flick_smooth2   = loadHeroFloat(sec, "Flick_smooth2", 1000);
            OW::Config::accvalue2       = loadHeroFloat(sec, "accvalue2", 1000);
            OW::Config::hitbox2         = loadHeroFloat(sec, "hitbox2", 1300);
            OW::Config::Fov2            = (float)loadHero(sec, "Fov2", 200);
            OW::Config::minFov2         = (float)loadHero(sec, "Fov2", 200);

            OW::Config::enablechangefov = loadHero(sec, "enablechangefov", 0);
            OW::Config::CHANGEFOV       = loadHeroFloat(sec, "CHANGEFOV", 1030000);

            OW::Config::lockontarget    = loadHero(sec, "lockontarget", 1);
            OW::Config::trackcompensate = loadHero(sec, "trackc", 0);
            OW::Config::autoscalefov    = loadHero(sec, "autoscalefov", 0);
            OW::Config::highPriority    = loadHero(sec, "highPriority", 0);
            OW::Config::aiaim           = loadHero(sec, "aiaim", 0);
            OW::Config::hanzoautospeed  = loadHero(sec, "hanzoautospeed", 0);

            OW::Config::trackback       = loadHero("Global", "trackback", 0);
            OW::Config::draw_info       = loadHero("Global", "draw_info", 1);
            OW::Config::drawbattletag   = loadHero("Global", "drawbattletag", 0);
            OW::Config::drawhealth      = loadHero("Global", "drawhealth", 1);
            OW::Config::healthbar       = loadHero("Global", "healthbar", 1);
            OW::Config::healthbar2      = loadHero("Global", "healthbar2", 0);
            OW::Config::healthbartextsize = loadHeroFloat("Global", "healthbartextsize", 160000);
            OW::Config::dist            = loadHero("Global", "dist", 1);
            OW::Config::name            = loadHero("Global", "name", 1);
            OW::Config::ult             = loadHero("Global", "ult", 1);
            OW::Config::draw_skel       = loadHero("Global", "draw_skel", 1);
            OW::Config::skillinfo       = loadHero("Global", "skillinfo", 0);
            OW::Config::externaloutline = loadHero("Global", "externaloutline", 0);
            OW::Config::teamoutline     = loadHero("Global", "teamoutline", 0);
            OW::Config::healthoutline   = loadHero("Global", "healthoutline", 0);
            OW::Config::rainbowoutline  = loadHero("Global", "rainbowoutline", 0);
            OW::Config::draw_edge       = loadHero("Global", "draw_edge", 0);
            OW::Config::drawbox3d       = loadHero("Global", "drawbox3d", 0);
            OW::Config::radar           = loadHero("Global", "radar", 0);
            OW::Config::radarline       = loadHero("Global", "radarline", 0);
            OW::Config::drawline        = loadHero("Global", "drawline", 0);
            OW::Config::draw_fov        = loadHero("Global", "draw_fov", 0);
            OW::Config::MenuToggleKey   = loadHero("Global", "MenuToggleKey", VK_HOME);
            OW::Config::eyeray          = loadHero("Global", "eyeray", 0);
            OW::Config::crosscircle     = loadHero("Global", "crosscircle", 0);
            OW::Config::draw_hp_pack    = loadHero("Global", "draw_hp_pack", 0);

            // Load colors
            auto loadColor = [&](const char* section, const char* prefix, ImVec4& c, float dx, float dy, float dz, float dw) {
                c.x = loadHeroFloat(section, (std::string(prefix) + "x").c_str(), (int)(dx * 10000));
                c.y = loadHeroFloat(section, (std::string(prefix) + "y").c_str(), (int)(dy * 10000));
                c.z = loadHeroFloat(section, (std::string(prefix) + "z").c_str(), (int)(dz * 10000));
                c.w = loadHeroFloat(section, (std::string(prefix) + "w").c_str(), (int)(dw * 10000));
            };

            loadColor("Global", "EnemyCol",    OW::Config::EnemyCol,    1.f, 1.f, 1.f, 1.f);
            loadColor("Global", "fovcol",      OW::Config::fovcol,      1.f, 0.9f, 0.f, 1.f);
            loadColor("Global", "fovcol2",     OW::Config::fovcol2,     0.855f, 0.439f, 0.839f, 0.5f);
            loadColor("Global", "invisenargb", OW::Config::invisnenargb, 0.4f, 0.37f, 0.91f, 1.f);
            loadColor("Global", "enargb",      OW::Config::enargb,      1.f, 0.3f, 0.f, 1.f);
            loadColor("Global", "targetargb",  OW::Config::targetargb,  1.f, 1.f, 0.f, 0.8f);
            loadColor("Global", "targetargb2", OW::Config::targetargb2, 1.f, 1.f, 0.4f, 0.8f);
            loadColor("Global", "allyargb",    OW::Config::allyargb,    0.4f, 1.f, 1.f, 0.4f);

            // Restore aim mode
            int dec = loadHero(sec, "Aim Mode", 0);
            OW::Config::Tracking = (dec == 0);
            OW::Config::Flick = (dec == 1);
            OW::Config::hanzo_flick = (dec == 2);
            OW::Config::silent = (dec == 3);
            OW::Config::triggerbot = (dec == 4);

            OW::Config::AutoShoot   = (loadHero(sec, "autoshootonoff", 0) == 1);
            OW::Config::Prediction  = (loadHero(sec, "predictdec", 0) == 1);

            // Genji-specific
            if (OW::local_entity.HeroID == OW::eHero::HERO_GENJI) {
                OW::Config::GenjiBlade     = loadHero(sec, "GenjiBlade", 0);
                OW::Config::AutoShiftGenji = loadHero(sec, "AutoShiftGenji", 0);
                OW::Config::bladespeed     = loadHeroFloat(sec, "bladespeed", 5000);
            } else {
                OW::Config::GenjiBlade = false;
                OW::Config::AutoShiftGenji = false;
            }
            if (OW::local_entity.HeroID == OW::eHero::HERO_WIDOWMAKER)
                OW::Config::widowautounscope = loadHero(sec, "widowautounscope", 0);
            else
                OW::Config::widowautounscope = false;

            OW::Config::lastheroid = OW::local_entity.HeroID;
            Sleep(2);
            OW::Config::nowhero = "Now using: " + heroName;
        } else if (OW::Config::manualsave && OW::Config::lastheroid != 0) {
            // Manual save is handled the same as auto-save above
            OW::Config::manualsave = false;
            std::string heroName = OW::GetHeroEngNames(OW::Config::lastheroid, OW::local_entity.LinkBase);
            std::string saveMsg = "Saved: " + heroName;
            // Notification handled by overlay layer
        }
        Sleep(2);
    }
#endif
}

// =========================================================================
// Loop RPM thread (continuous recoil control / FOV change)
// =========================================================================

inline void looprpmthread() {
    while (1) {
        auto entity_snapshot = OW::TargetingDetail::SnapshotEntities();
        auto local_snapshot = OW::TargetingDetail::SnapshotLocalEntity();
        if (!entity_snapshot.empty()) {
            if (local_snapshot.AngleBase &&
                (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) ||
                 GetAsyncKeyState(OW::Config::aim_key2) ||
                 GetAsyncKeyState(0x01) || GetAsyncKeyState(0x02))) {
                if (OW::Config::horizonreco)
                    SDK->WPM<float>(local_snapshot.AngleBase + 0x1768, 0.f);
                if (OW::Config::norecoil)
                    SDK->WPM<float>(local_snapshot.AngleBase + 0x1764, OW::Config::recoilnum);
            }
            if (OW::Config::enablechangefov)
                SDK->WPM<float>(SDK->dwGameBase + OW::offset::changefov, OW::Config::CHANGEFOV);
        }
        Sleep(10);
    }
}
