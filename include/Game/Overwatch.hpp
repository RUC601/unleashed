#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <ctime>
#include <windows.h>
#include <process.h>
#include <DirectXMath.h>

#include "Game/Target.hpp"

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
    while (OW::Config::doingentity == 1) {
        if (OW::abletotread == 0) {
            OW::ow_entities_scan = OW::get_ow_entities();
            OW::abletotread = 1;
        }
        Sleep(10);
    }
}

// =========================================================================
// Entity processing thread (decrypts components, builds c_entity list)
// =========================================================================

inline void entity_thread() {
    int entitytime = 0;
    Vector3 lastpos{};

    while (OW::Config::doingentity == 1) {
        if (entitytime == 0) entitytime = GetTickCount();
        if (GetTickCount() - entitytime >= 100 && OW::abletotread) {
            g_mutex.lock();
            OW::ow_entities = OW::ow_entities_scan;
            OW::abletotread = 0;
            entitytime = GetTickCount();
            g_mutex.unlock();
        }

        // No entities available
        if (!(OW::ow_entities.size() > 0)) {
            g_mutex.lock();
            OW::entities = {};
            OW::hp_dy_entities = {};
            g_mutex.unlock();
            Sleep(1000);
            continue;
        }

        std::vector<OW::c_entity> tmp_entities{};
        std::vector<OW::hpanddy> hpdy_entities{};
        OW::c_entity lastentity{};

        for (size_t i = 0; i < OW::ow_entities.size(); i++) {
            OW::c_entity entity{};
            if (!OW::ow_entities[i].first || !OW::ow_entities[i].second) continue;
            if (i >= OW::ow_entities.size()) continue;

            const auto& [ComponentParent, LinkParent] = OW::ow_entities[i];
            entity.address = ComponentParent;
            if (!entity.address || !LinkParent) continue;

            // Check for special entity IDs (HP packs, Bob, etc.)
            uint64_t Ptr = SDK->RPM<uint64_t>(ComponentParent + 0x30) & 0xFFFFFFFFFFFFFFC0;
            if (Ptr < 0xFFFFFFFFFFFFFFEF) {
                uint64_t EntityID = SDK->RPM<uint64_t>(Ptr + 0x10);
                if (EntityID == 0x400000000000060 || EntityID == 0x40000000000480A ||
                    EntityID == 0x40000000000005F || EntityID == 0x400000000002533) {
                    OW::hpanddy hpdyentity{};
                    hpdyentity.entityid = EntityID;
                    hpdyentity.MeshBase = OW::DecryptComponent(ComponentParent, OW::TYPE_VELOCITY);
                    hpdyentity.POS = SDK->RPM<XMFLOAT3>(hpdyentity.MeshBase + 0x380 + 0x50);
                    hpdy_entities.push_back(hpdyentity);
                    continue;
                }
            }

            // Decrypt all component bases
            entity.HealthBase    = OW::DecryptComponent(ComponentParent, OW::TYPE_HEALTH);
            entity.LinkBase      = OW::DecryptComponent(LinkParent, OW::TYPE_LINK);
            entity.TeamBase      = OW::DecryptComponent(ComponentParent, OW::TYPE_TEAM);
            entity.VelocityBase  = OW::DecryptComponent(ComponentParent, OW::TYPE_VELOCITY);
            entity.HeroBase      = OW::DecryptComponent(LinkParent, OW::TYPE_P_HEROID);
            entity.BoneBase      = OW::DecryptComponent(ComponentParent, OW::TYPE_BONE);
            entity.RotationBase  = OW::DecryptComponent(ComponentParent, OW::TYPE_ROTATION);
            entity.SkillBase     = OW::DecryptComponent(ComponentParent, OW::TYPE_SKILL);
            entity.VisBase       = OW::DecryptComponent(LinkParent, OW::TYPE_P_VISIBILITY);
            entity.AngleBase     = OW::DecryptComponent(LinkParent, OW::TYPE_PLAYERCONTROLLER);
            entity.EnemyAngleBase = OW::DecryptComponent(ComponentParent, OW::TYPE_ANGLE);

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
            } else continue;

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
                } else continue;
            }

            // ---- BattleTag (optional) ----
            if (OW::Config::draw_info && OW::Config::drawbattletag) {
                entity.statcombase = OW::DecryptComponent(LinkParent, OW::TYPE_STAT);
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

            // ---- Visibility ----
            if (entity.VisBase) {
                auto vis_compo = SDK->RPM<OW::vis_compo_t>(entity.VisBase);
                entity.Vis = (OW::DecryptVis(vis_compo.key1) ^ vis_compo.key2) ? true : false;
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
                    OW::local_entity = entity;
                    OW::Config::reloading = OW::IsSkillActivate1(OW::local_entity.SkillBase + 0x40, 0, 0x4BF);
                    SDK->g_player_controller = entity.AngleBase;
                    if (OW::local_entity.GetTeam() == OW::eTeam::TEAM_DEATHMATCH)
                        entity.Team = false;
                }
            }

            // Add to list if valid
            std::string name = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
            if (ComponentParent && LinkParent && name != "Unknown")
                tmp_entities.push_back(entity);
        }

        // Swap processed entities
        OW::entities = tmp_entities;
        OW::hp_dy_entities = hpdy_entities;
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
            OW::viewMatrix    = SDK->RPM<OW::Matrix>(viewMatrixPtr);
            OW::viewMatrix_xor = SDK->RPM<OW::Matrix>(viewMatrix_xor_ptr);

            Sleep(5);
        }
    } __except (1) {}
}

// =========================================================================
// ESP rendering helpers (require ImGui and Render:: namespace)
// =========================================================================

inline void PlayerInfo() {
    if (OW::entities.size() == 0) return;
    for (OW::c_entity entity : OW::entities) {
        if (!entity.Alive || !entity.Team || OW::local_entity.PlayerHealth <= 0.f) continue;

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
        // Health / text info is rendered via esp() below
    }
}

inline void skillinfo() {
    if (OW::entities.size() == 0) return;
    int i = 10;
    for (OW::c_entity entity : OW::entities) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Enemy: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            // Render::DrawSKILL is called by the overlay layer
            i += 20;
        } else if (entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Enemy Entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            i += 20;
        }
    }
    i += 60;
    for (OW::c_entity entity : OW::entities) {
        std::string heroname = OW::GetHeroEngNames(entity.HeroID, entity.LinkBase);
        if (!entity.Team && heroname != "Bot" && heroname != "Unknown" &&
            entity.HeroID != 0x16dd && entity.HeroID != 0x16ee && entity.HeroID != 0x16bb) {
            std::string info = "Ally: " + heroname + " Ult: " + std::to_string((int)entity.ultimate);
            i += 20;
        } else if (!entity.Team && (entity.HeroID == 0x16dd || entity.HeroID == 0x16ee || entity.HeroID == 0x16bb)) {
            std::string info = "Ally entity: " + heroname + " HP: " + std::to_string((int)entity.PlayerHealth) + "/" + std::to_string((int)entity.MaxHealth);
            i += 20;
        }
    }
}

// =========================================================================
// Main aimbot thread
// =========================================================================

inline void aimbot_thread() {
    __try {
        int hitbotdelaytime = 0;
        int afterdelaytime = 0;
        bool dodelay = 0;
        Vector2 CrossHair = Vector2(
            (float)GetSystemMetrics(SM_CXSCREEN) / 2.0f,
            (float)GetSystemMetrics(SM_CYSCREEN) / 2.0f
        );
        static float origin_sens = 0.f;

        while (true) {
            // Anti AFK
            if (OW::Config::AntiAFK) {
                OW::SetKey(0x57);
                Sleep(1000);
            }

            if (OW::entities.size() > 0) {
                // Sensitivity management
                if (SDK->RPM<float>(OW::GetSenstivePTR()))
                    origin_sens = SDK->RPM<float>(OW::GetSenstivePTR());
                else if (origin_sens)
                    SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);

                // ---- Triggerbot ----
                if (OW::Config::triggerbot) {
                    auto vec = OW::GetVector3(OW::Config::Prediction);
                    if (vec != Vector3(0, 0, 0) &&
                        !(OW::entities[OW::Config::Targetenemyi].skill2act &&
                          OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI &&
                          OW::entities[OW::Config::Targetenemyi].GetTeam() != OW::local_entity.GetTeam())) {
                        auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                        auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                        auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                        auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);
                        if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::hitbox)) {
                            if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                            OW::SetKey(0x1);
                            Sleep(2);
                            if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                        }
                    }
                }

                // ---- Triggerbot2 ----
                if (OW::Config::triggerbot2) {
                    auto vec = OW::GetVector3aim2(OW::Config::Prediction2);
                    if (vec != Vector3(0, 0, 0) &&
                        !(OW::entities[OW::Config::Targetenemyi].skill2act &&
                          OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI &&
                          OW::entities[OW::Config::Targetenemyi].GetTeam() != OW::local_entity.GetTeam())) {
                        auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                        auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                        auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                        auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);
                        if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::hitbox2)) {
                            if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                            OW::SetKey(0x1);
                            Sleep(2);
                            if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                        }
                    }
                }

                // ---- Tracking ----
                if (OW::Config::Tracking) {
                    while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::reloading) {
                        auto vec = OW::GetVector3(OW::Config::Prediction);
                        if (vec != Vector3(0, 0, 0) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill2act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill1act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_VENTURE) &&
                            ((!OW::entities[OW::Config::Targetenemyi].imort && !OW::entities[OW::Config::Targetenemyi].barrprot) || OW::Config::switch_team)) {
                            auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                            auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                            auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                            auto Target = OW::SmoothLinear(local_angle, vec_calc_target, OW::Config::Tracking_smooth / 10.f);
                            auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);

                            if (OW::Config::aiaim) {
                                Target.X += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 500.f : -(float)(rand()) / RAND_MAX / 500.f;
                                Target.Y += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 500.f : -(float)(rand()) / RAND_MAX / 500.f;
                                Target.Z += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 500.f : -(float)(rand()) / RAND_MAX / 500.f;
                                if (OW::Config::minFov1 > 500.f) OW::Config::minFov1 = 500.f;
                                if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                                if (OW::Config::minFov2 > 500.f) OW::Config::minFov1 = 500.f;
                                if (OW::Config::Fov2 > 500.f) OW::Config::Fov = 500.f;
                                if (OW::Config::fov360) OW::Config::fov360 = false;
                            }

                            if (Target != Vector3(0, 0, 0)) {
                                if (OW::Config::targetdelay) {
                                    if (OW::Config::lastenemy != OW::Config::Targetenemyi) OW::Config::doingdelay = 1;
                                    if (OW::Config::doingdelay == 1) {
                                        OW::Config::lastenemy = OW::Config::Targetenemyi;
                                        if (OW::Config::timebeforedelay == 0) {
                                            OW::Config::timebeforedelay = GetTickCount();
                                            continue;
                                        }
                                        if (GetTickCount() - OW::Config::timebeforedelay < (DWORD)OW::Config::targetdelaytime) continue;
                                        OW::Config::timebeforedelay = 0;
                                        OW::Config::doingdelay = 0;
                                    }
                                }
                                if (OW::Config::Rage)
                                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                else
                                    SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, Target);

                                float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                                if (OW::Config::health <= OW::Config::meleehealth && dist <= OW::Config::meleedistance && OW::Config::AutoMelee)
                                    OW::SetKey(0x800);
                                if (OW::Config::health <= OW::Config::AutoRMBhealth && dist <= OW::Config::AutoRMBdistance && OW::Config::AutoRMB)
                                    OW::SetKey(0x2);
                            }
                            if (OW::local_entity.PlayerHealth < OW::Config::SkillHealth) break;
                        }
                        Sleep(1);

                        // Auto FOV scaling
                        if (OW::Config::autoscalefov) {
                            auto fvec = OW::GetVector3forfov();
                            if (fvec != Vector3(0, 0, 0)) {
                                Vector2 high, low;
                                if (OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].head_pos, &high, Vector2(OW::WX, OW::WY)) &&
                                    OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].chest_pos, &low, Vector2(OW::WX, OW::WY))) {
                                    OW::Config::Fov = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                                    else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;
                                    OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
                                    else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
                                } else {
                                    OW::Config::Fov  = OW::Config::minFov1;
                                    OW::Config::Fov2 = OW::Config::minFov2;
                                }
                            } else {
                                OW::Config::Fov  = OW::Config::minFov1;
                                OW::Config::Fov2 = OW::Config::minFov2;
                            }
                        }
                        if (OW::Config::highPriority && GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2))) break;
                    }
                }

                // ---- Flick ----
                else if (OW::Config::Flick) {
                    if (OW::Config::hitboxdelayshoot) {
                        if (OW::Config::shooted || !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) {
                            dodelay = 1;
                            hitbotdelaytime = 0;
                        }
                    }
                    while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::shooted && !OW::Config::reloading) {
                        if (OW::local_entity.HeroID == OW::eHero::HERO_WIDOWMAKER && !GetAsyncKeyState(0x2)) continue;

                        auto vec = OW::GetVector3(OW::Config::Prediction);
                        if (vec == Vector3(0, 0, 0)) break;
                        if (vec != Vector3(0, 0, 0) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill2act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill1act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_VENTURE) &&
                            ((!OW::entities[OW::Config::Targetenemyi].imort && !OW::entities[OW::Config::Targetenemyi].barrprot) || OW::Config::switch_team)) {
                            // Target delay
                            if (OW::Config::targetdelay) {
                                if (OW::Config::lastenemy != OW::Config::Targetenemyi) OW::Config::doingdelay = 1;
                                if (OW::Config::doingdelay == 1) {
                                    OW::Config::lastenemy = OW::Config::Targetenemyi;
                                    if (OW::Config::timebeforedelay == 0) {
                                        OW::Config::timebeforedelay = GetTickCount();
                                        continue;
                                    }
                                    if (GetTickCount() - OW::Config::timebeforedelay < (DWORD)OW::Config::targetdelaytime) continue;
                                    OW::Config::timebeforedelay = 0;
                                    OW::Config::doingdelay = 0;
                                    hitbotdelaytime = GetTickCount();
                                }
                            } else if (OW::Config::doingdelay) OW::Config::doingdelay = 0;

                            if (dodelay && !OW::Config::doingdelay) {
                                hitbotdelaytime = GetTickCount();
                                dodelay = 0;
                            }

                            auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                            auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                            auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                            auto Target = OW::SmoothAccelerate(local_angle, vec_calc_target, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                            auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);

                            if (OW::Config::aiaim) {
                                Target.X += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Y += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Z += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                if (OW::Config::minFov1 > 500.f) OW::Config::minFov1 = 500.f;
                                if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                                if (OW::Config::fov360) OW::Config::fov360 = false;
                            }

                            if (Target != Vector3(0, 0, 0)) {
                                // Timeout shoot
                                if (OW::Config::hitboxdelayshoot && hitbotdelaytime != 0) {
                                    afterdelaytime = GetTickCount();
                                    if (afterdelaytime - hitbotdelaytime > OW::Config::hiboxdelaytime && !OW::Config::doingdelay) {
                                        if (OW::local_entity.HeroID == OW::eHero::HERO_GENJI || OW::local_entity.HeroID == OW::eHero::HERO_KIRIKO)
                                            OW::SetKey(0x2);
                                        else
                                            OW::SetKey(0x1);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }

                                // Rage
                                if (OW::Config::Rage) {
                                    if (OW::Config::fakesilent) {
                                        Vector3 orangle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                                        OW::SetKey(0x1);
                                        Sleep(25);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                                        OW::Config::shooted = true;
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, orangle);
                                        continue;
                                    } else {
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                                        OW::SetKey(0x1);
                                        Sleep(1);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }

                                // Normal flick
                                SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, Target);
                                if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::hitbox)) {
                                    if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                                    if (OW::local_entity.HeroID == OW::eHero::HERO_GENJI || OW::local_entity.HeroID == OW::eHero::HERO_KIRIKO) {
                                        OW::SetKey(0x2);
                                        if (OW::Config::dontshot) OW::Config::shotcount++;
                                    } else {
                                        if ((OW::local_entity.HeroID == OW::eHero::HERO_ANA || OW::local_entity.HeroID == OW::eHero::HERO_WIDOWMAKER || OW::local_entity.HeroID == OW::eHero::HERO_ASHE) && GetAsyncKeyState(0x2))
                                            OW::SetKeyscopeHold(0x1, 30);
                                        else
                                            OW::SetKey(0x1);
                                    }
                                    if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                                    OW::Config::shooted = true;
                                    if (OW::Config::dontshot) OW::Config::shotcount++;
                                    break;
                                } else if (OW::Config::dontshot && OW::Config::shotcount >= OW::Config::shotmanydont) {
                                    if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::missbox)) {
                                        OW::Config::shotcount = 0;
                                        if (OW::local_entity.HeroID == OW::eHero::HERO_GENJI || OW::local_entity.HeroID == OW::eHero::HERO_KIRIKO)
                                            OW::SetKey(0x2);
                                        else
                                            OW::SetKey(0x1);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }
                            }
                        }
                        Sleep(1);

                        if (OW::Config::autoscalefov) {
                            auto fvec = OW::GetVector3forfov();
                            if (fvec != Vector3(0, 0, 0)) {
                                Vector2 high, low;
                                if (OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].head_pos, &high, Vector2(OW::WX, OW::WY)) &&
                                    OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].chest_pos, &low, Vector2(OW::WX, OW::WY))) {
                                    OW::Config::Fov = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                                    else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;
                                    OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
                                    else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
                                } else {
                                    OW::Config::Fov  = OW::Config::minFov1;
                                    OW::Config::Fov2 = OW::Config::minFov2;
                                }
                            } else {
                                OW::Config::Fov  = OW::Config::minFov1;
                                OW::Config::Fov2 = OW::Config::minFov2;
                            }
                        }
                        if (OW::Config::highPriority && GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2))) break;
                    }
                }

                // ---- Hanzo flick ----
                else if (OW::Config::hanzo_flick) {
                    if (OW::Config::hitboxdelayshoot) {
                        if (OW::Config::shooted || !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) {
                            dodelay = 1;
                            hitbotdelaytime = 0;
                        }
                    }
                    while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) && !OW::Config::shooted) {
                        auto vec = OW::GetVector3(true);
                        if (vec == Vector3(0, 0, 0)) break;
                        if (vec != Vector3(0, 0, 0) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill2act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill1act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_VENTURE) &&
                            ((!OW::entities[OW::Config::Targetenemyi].imort && !OW::entities[OW::Config::Targetenemyi].barrprot) || OW::Config::switch_team)) {
                            auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                            auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                            auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                            auto Target = OW::SmoothAccelerate(local_angle, vec_calc_target, OW::Config::Flick_smooth / 10.f, OW::Config::accvalue);
                            auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);

                            if (dodelay && !OW::Config::doingdelay) {
                                hitbotdelaytime = GetTickCount();
                                dodelay = 0;
                            }

                            if (OW::Config::aiaim) {
                                Target.X += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Y += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Z += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                            }

                            if (Target != Vector3(0, 0, 0)) {
                                if (OW::Config::hitboxdelayshoot && hitbotdelaytime != 0) {
                                    afterdelaytime = GetTickCount();
                                    if (afterdelaytime - hitbotdelaytime > OW::Config::hiboxdelaytime && !OW::Config::doingdelay) {
                                        if (OW::local_entity.skill2act) OW::SetKey(0x1);
                                        else OW::SetKeyHold(0x1000, 100);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }

                                if (OW::Config::Rage) {
                                    if (OW::Config::fakesilent) {
                                        Vector3 orangle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                                        if (OW::local_entity.skill2act) OW::SetKey(0x1);
                                        else OW::SetKeyHold(0x1000, 100);
                                        Sleep(25);
                                        if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                                        OW::Config::shooted = true;
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, orangle);
                                        continue;
                                    } else {
                                        SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                        if (OW::local_entity.skill2act) OW::SetKey(0x1);
                                        else OW::SetKeyHold(0x1000, 100);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }

                                if (OW::Config::targetdelay) {
                                    if (OW::Config::lastenemy != OW::Config::Targetenemyi) OW::Config::doingdelay = 1;
                                    if (OW::Config::doingdelay == 1) {
                                        OW::Config::lastenemy = OW::Config::Targetenemyi;
                                        if (OW::Config::timebeforedelay == 0) {
                                            OW::Config::timebeforedelay = GetTickCount();
                                            continue;
                                        }
                                        if (GetTickCount() - OW::Config::timebeforedelay < (DWORD)OW::Config::targetdelaytime) continue;
                                        OW::Config::timebeforedelay = 0;
                                        OW::Config::doingdelay = 0;
                                        hitbotdelaytime = GetTickCount();
                                    }
                                } else if (OW::Config::doingdelay) OW::Config::doingdelay = 0;

                                SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, Target);
                                if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::hitbox)) {
                                    if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), 0.f);
                                    if (OW::local_entity.skill2act) OW::SetKey(0x1);
                                    else OW::SetKeyHold(0x1000, 100);
                                    Sleep(1);
                                    if (OW::Config::dontshot) OW::Config::shotcount++;
                                    if (OW::Config::lockontarget) SDK->WPM<float>(OW::GetSenstivePTR(), origin_sens);
                                    OW::Config::shooted = true;
                                } else if (OW::Config::dontshot && OW::Config::shotcount >= OW::Config::shotmanydont) {
                                    if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::missbox)) {
                                        OW::Config::shotcount = 0;
                                        if (OW::local_entity.skill2act) OW::SetKey(0x1);
                                        else OW::SetKeyHold(0x1000, 100);
                                        OW::Config::shooted = true;
                                        continue;
                                    }
                                }
                            }
                        }
                        Sleep(1);

                        if (OW::Config::autoscalefov) {
                            auto fvec = OW::GetVector3forfov();
                            if (fvec != Vector3(0, 0, 0)) {
                                Vector2 high, low;
                                if (OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].head_pos, &high, Vector2(OW::WX, OW::WY)) &&
                                    OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].chest_pos, &low, Vector2(OW::WX, OW::WY))) {
                                    OW::Config::Fov = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                                    else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;
                                    OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
                                    if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
                                    else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
                                } else {
                                    OW::Config::Fov  = OW::Config::minFov1;
                                    OW::Config::Fov2 = OW::Config::minFov2;
                                }
                            } else {
                                OW::Config::Fov  = OW::Config::minFov1;
                                OW::Config::Fov2 = OW::Config::minFov2;
                            }
                        }
                    }
                }

                // ---- Genji Blade ----
                if (OW::Config::GenjiBlade && GetAsyncKeyState(0x51) &&
                    OW::local_entity.HeroID == OW::eHero::HERO_GENJI &&
                    OW::local_entity.ultimate == 100.f) {
                    OW::Config::Qstarttime = GetTickCount();
                    OW::Config::Qtime = OW::Config::Qstarttime;
                    OW::Config::lastenemy = -1;
                    Sleep(1000);
                    int detecttoggle = 0;
                    int first = 1;
                    float speed = 0.f;
                    while (OW::Config::GenjiBlade && (OW::Config::Qtime - OW::Config::Qstarttime) <= 7000) {
                        if (!OW::local_entity.skillcd1) speed = OW::Config::Tracking_smooth;
                        else speed = OW::Config::bladespeed;
                        OW::Config::Qtime = GetTickCount();
                        auto vec = OW::GetVector3forgenji();
                        if (vec != Vector3(0, 0, 0)) {
                            float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                            if (dist > 20.f) continue;
                            auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                            auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                            auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                            auto Target = OW::SmoothLinear(local_angle, vec_calc_target, speed / 10.f);
                            auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);
                            if (Target != Vector3(0, 0, 0)) {
                                float dist2 = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                                if ((!OW::local_entity.skillcd1 && dist2 < 20.f) || dist2 < 7.f) {
                                    if (OW::Config::Rage) SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, vec_calc_target);
                                    else SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, Target);
                                }
                                if (!OW::local_entity.skillcd1 && OW::in_range(local_angle, vec_calc_target, local_loc, vec, 0.8f)) {
                                    if (detecttoggle && !first) {
                                        detecttoggle = 0;
                                        Sleep(50);
                                        continue;
                                    }
                                    OW::SetKeyHold(0x8, 70);
                                    first = 0;
                                }
                                if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, 1.f) && dist2 < 5.f)
                                    OW::SetKey(0x1);
                                if (OW::local_entity.skillcd1 != 0 && !detecttoggle) detecttoggle = 1;
                            }
                        }
                        Sleep(1);
                        OW::Config::lastenemy = OW::Config::Targetenemyi;
                    }
                }

                // ---- Auto FOV (outside aiming modes) ----
                if (OW::Config::autoscalefov) {
                    auto fvec = OW::GetVector3forfov();
                    if (fvec != Vector3(0, 0, 0)) {
                        Vector2 high, low;
                        if (OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].head_pos, &high, Vector2(OW::WX, OW::WY)) &&
                            OW::viewMatrix.WorldToScreen(OW::entities[OW::Config::Targetenemyifov].chest_pos, &low, Vector2(OW::WX, OW::WY))) {
                            OW::Config::Fov = -(high.Y - low.Y) * 4.f;
                            if (OW::Config::Fov > 500.f) OW::Config::Fov = 500.f;
                            else if (OW::Config::Fov < OW::Config::minFov1) OW::Config::Fov = OW::Config::minFov1;
                            OW::Config::Fov2 = -(high.Y - low.Y) * 4.f;
                            if (OW::Config::Fov2 > 500.f) OW::Config::Fov2 = 500.f;
                            else if (OW::Config::Fov2 < OW::Config::minFov2) OW::Config::Fov2 = OW::Config::minFov2;
                        } else {
                            OW::Config::Fov  = OW::Config::minFov1;
                            OW::Config::Fov2 = OW::Config::minFov2;
                        }
                    } else {
                        OW::Config::Fov  = OW::Config::minFov1;
                        OW::Config::Fov2 = OW::Config::minFov2;
                    }
                }

                // ---- Auto Melee ----
                if (OW::Config::AutoMelee) {
                    auto vec = OW::GetVector3(false);
                    if (vec != Vector3(0, 0, 0) && OW::entities[OW::Config::Targetenemyi].Team) {
                        float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                        if (OW::Config::health <= OW::Config::meleehealth && dist <= OW::Config::meleedistance &&
                            !(OW::entities[OW::Config::Targetenemyi].skill1act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_VENTURE)) {
                            OW::SetKey(0x800);
                            Sleep(1);
                        }
                    }
                }

                // ---- Auto RMB ----
                if (OW::Config::AutoRMB) {
                    auto vec = OW::GetVector3(false);
                    if (vec != Vector3(0, 0, 0) && OW::entities[OW::Config::Targetenemyi].Team) {
                        float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                        if (OW::Config::health <= OW::Config::AutoRMBhealth && dist <= OW::Config::AutoRMBdistance &&
                            !(OW::entities[OW::Config::Targetenemyi].skill1act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_VENTURE)) {
                            OW::SetKey(0x2);
                            Sleep(1);
                        }
                    }
                }

                // ---- Genji auto dash ----
                if (OW::Config::AutoShiftGenji) {
                    auto vec = OW::GetVector3(false);
                    if (vec != Vector3(0, 0, 0)) {
                        float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                        if (!OW::entities[OW::Config::Targetenemyi].imort && !OW::entities[OW::Config::Targetenemyi].barrprot) {
                            if (!OW::local_entity.skillcd1 && OW::Config::health <= 50.f && dist <= 15.f &&
                                OW::entities[OW::Config::Targetenemyi].HeroID != 0x16dd && OW::entities[OW::Config::Targetenemyi].HeroID != 0x16ee) {
                                auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                                auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                                auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                                auto Target = OW::SmoothLinear(local_angle, vec_calc_target, OW::Config::Tracking_smooth / 10.f);
                                auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);
                                if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, 1.f))
                                    OW::SetKeyHold(0x8, 40);
                            } else if (!OW::local_entity.skillcd1 && OW::Config::health <= 80.f && dist >= 15.f && dist <= 17.f &&
                                       OW::entities[OW::Config::Targetenemyi].HeroID != 0x16dd && OW::entities[OW::Config::Targetenemyi].HeroID != 0x16ee) {
                                auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                                auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                                auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                                auto Target = OW::SmoothLinear(local_angle, vec_calc_target, OW::Config::Tracking_smooth / 10.f);
                                auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);
                                if (OW::in_range(local_angle, vec_calc_target, local_loc, vec, 1.f)) {
                                    OW::SetKey(0x8);
                                    Sleep(500);
                                    OW::SetKey(0x800);
                                }
                            }
                        }
                    }
                }

                // ---- Auto heal skill ----
                if (OW::Config::AutoSkill) {
                    if (OW::local_entity.PlayerHealth > OW::Config::SkillHealth && OW::Config::skilled)
                        OW::Config::skilled = false;
                    else if (OW::local_entity.PlayerHealth < OW::Config::SkillHealth && OW::Config::skilled &&
                             OW::local_entity.PlayerHealth < OW::Config::lasthealth &&
                             OW::local_entity.HeroID != OW::eHero::HERO_DOOMFIST)
                        OW::Config::skilled = false;

                    if (OW::local_entity.PlayerHealth < OW::Config::SkillHealth && !OW::Config::skilled) {
                        auto hID = OW::local_entity.HeroID;
                        if (hID == OW::eHero::HERO_TRACER || hID == OW::eHero::HERO_SOMBRA ||
                            hID == OW::eHero::HERO_ROADHOG || hID == OW::eHero::HERO_TORBJORN ||
                            hID == OW::eHero::HERO_SOLDIER76 || hID == OW::eHero::HERO_VENTURE) {
                            OW::SetKey(0x10); OW::Config::skilled = true;
                            Sleep(1); OW::Config::lasthealth = OW::local_entity.PlayerHealth;
                        } else if (hID == OW::eHero::HERO_REAPER || hID == OW::eHero::HERO_MEI ||
                                   hID == OW::eHero::HERO_JUNKERQUEEN || hID == OW::eHero::HERO_MOIRA ||
                                   hID == OW::eHero::HERO_ZARYA) {
                            OW::SetKey(0x8); OW::Config::skilled = true;
                            Sleep(1); OW::Config::lasthealth = OW::local_entity.PlayerHealth;
                        } else if (hID == OW::eHero::HERO_WINSTON || hID == OW::eHero::HERO_ZENYATTA) {
                            OW::SetKey(0x20); OW::Config::skilled = true;
                            Sleep(1); OW::Config::lasthealth = OW::local_entity.PlayerHealth;
                        }
                    }
                }

                // ---- Auto shoot cooldown ----
                if (OW::Config::AutoShoot && OW::Config::shooted &&
                    !(OW::local_entity.HeroID == OW::eHero::HERO_HANJO && !OW::local_entity.skill2act)) {
                    int rectime = GetTickCount();
                    if (OW::Config::lasttime == 0) OW::Config::lasttime = rectime;
                    else {
                        if (rectime - OW::Config::lasttime >= OW::Config::Shoottime) {
                            OW::Config::lasttime = 0;
                            OW::Config::shooted = false;
                        }
                    }
                    if (OW::Config::reloading) {
                        OW::Config::lasttime = 0;
                        OW::Config::shooted = false;
                    }
                }

                // ---- Reset shoot state when key released ----
                if (!GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key))) {
                    OW::Config::shooted = false;
                    OW::Config::lasttime = 0;
                    if (OW::Config::reloading) {
                        OW::Config::lasttime = 0;
                        OW::Config::shooted = false;
                    }
                    OW::Config::Targetenemyi = -1;
                }

                // Reaper reload melee cancel
                if (OW::local_entity.HeroID == OW::eHero::HERO_REAPER && OW::Config::reloading) {
                    Sleep(300);
                    OW::SetKey(0x800);
                }

                // ---- Secondary aim ----
                if (OW::Config::secondaim) {
                    while (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)) && !OW::Config::shooted2) {
                        auto vec = OW::GetVector3aim2(OW::Config::Prediction2);
                        if (vec != Vector3(0, 0, 0) &&
                            !(OW::entities[OW::Config::Targetenemyi].skill2act && OW::entities[OW::Config::Targetenemyi].HeroID == OW::eHero::HERO_GENJI)) {
                            auto local_angle = SDK->RPM<Vector3>(SDK->g_player_controller + 0x1170);
                            auto calc_target = OW::CalcAngle(XMFLOAT3(vec.X, vec.Y, vec.Z), OW::viewMatrix_xor.get_location());
                            auto vec_calc_target = Vector3(calc_target.x, calc_target.y, calc_target.z);
                            Vector3 Target;
                            if (OW::Config::Tracking2) Target = OW::SmoothLinear(local_angle, vec_calc_target, OW::Config::Tracking_smooth2 / 10.f);
                            else if (OW::Config::Flick2) Target = OW::SmoothAccelerate(local_angle, vec_calc_target, OW::Config::Flick_smooth2 / 10.f, OW::Config::accvalue2);
                            if (OW::Config::Rage) Target = vec_calc_target;
                            auto local_loc = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z);

                            if (OW::Config::aiaim) {
                                Target.X += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Y += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                                Target.Z += (rand() % 10 > 5) ? (float)(rand()) / RAND_MAX / 300.f : -(float)(rand()) / RAND_MAX / 300.f;
                            }

                            if (Target != Vector3(0, 0, 0)) {
                                float dist = Vector3(OW::viewMatrix_xor.get_location().x, OW::viewMatrix_xor.get_location().y, OW::viewMatrix_xor.get_location().z).DistTo(vec);
                                if (OW::Config::health <= OW::Config::meleehealth && dist <= OW::Config::meleedistance && OW::Config::AutoMelee)
                                    OW::SetKey(0x800);
                                if (OW::Config::health <= OW::Config::AutoRMBhealth && dist <= OW::Config::AutoRMBdistance && OW::Config::AutoRMB)
                                    OW::SetKey(0x2);
                                SDK->WPM<Vector3>(SDK->g_player_controller + 0x1170, Target);
                                if (OW::Config::Flick2 && OW::in_range(local_angle, vec_calc_target, local_loc, vec, OW::Config::hitbox2)) {
                                    int tk = OW::Config::togglekey;
                                    if (tk == 0)       OW::SetKey(0x1);
                                    else if (tk == 1)  OW::SetKey(0x2);
                                    else if (tk == 2)  OW::SetKey(0x8);
                                    else if (tk == 3)  OW::SetKey(0x10);
                                    else if (tk == 4)  OW::SetKey(0x20);
                                    Sleep(1);
                                    OW::Config::shooted2 = true;
                                }
                            }
                            if (OW::local_entity.PlayerHealth < OW::Config::SkillHealth) break;
                        }
                        Sleep(1);
                    }
                    if (OW::Config::shooted2 && !GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key2)))
                        OW::Config::shooted2 = false;
                }
            }
            Sleep(2);
        }
    } __except (1) {}
}

// =========================================================================
// Config save/load thread
// =========================================================================

inline void configsavenloadthread() {
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
}

// =========================================================================
// Loop RPM thread (continuous recoil control / FOV change)
// =========================================================================

inline void looprpmthread() {
    while (1) {
        if (OW::entities.size() > 0) {
            if (OW::local_entity.AngleBase &&
                (GetAsyncKeyState(OW::get_bind_id(OW::Config::aim_key)) ||
                 GetAsyncKeyState(OW::Config::aim_key2) ||
                 GetAsyncKeyState(0x01) || GetAsyncKeyState(0x02))) {
                if (OW::Config::horizonreco)
                    SDK->WPM<float>(OW::local_entity.AngleBase + 0x1768, 0.f);
                if (OW::Config::norecoil)
                    SDK->WPM<float>(OW::local_entity.AngleBase + 0x1764, OW::Config::recoilnum);
            }
            if (OW::Config::enablechangefov)
                SDK->WPM<float>(SDK->dwGameBase + OW::offset::changefov, OW::Config::CHANGEFOV);
        }
        Sleep(10);
    }
}
