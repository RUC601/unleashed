#pragma once

#include <d3d11.h>

namespace UI {

    enum Tab { TAB_AIMING = 0, TAB_VISUALS = 1, TAB_THEME = 2, TAB_MISC = 3 };

    struct State {
        Tab   activeTab      = TAB_AIMING;
        int   aimingSubTab   = 0;   // 0 = Aimbot, 1 = Trigger, 2 = Skills
        int   visualsSubTab  = 0;   // 0 = Players
        int   aimHeroSegActive = 0;     // Current hero Aim config slot (0-11)
        int   triggerHeroSegActive = 0; // Current hero Trigger config slot (0-11)
        int   selectedTypeIndex = 0; // Shared hero/type selection, 0 = All
        bool  initialized    = false;
    };
    inline State state;

    struct MenuClientSize {
        float width = 0.0f;
        float height = 0.0f;
    };

    void InitStyle();
    void InitializeResources(ID3D11Device* device);
    void ShutdownResources();
    MenuClientSize DesiredMenuClientSize();
    void Render();

    // Per-page render functions
    void AimbotPage();
    void TriggerPage();
    void SkillsPage();
    void VisualsPage();
    void ThemePage();
    void MiscPage();

} // namespace UI
