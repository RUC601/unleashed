#pragma once

#include <d3d11.h>

namespace UI {

    enum Tab { TAB_AIMING = 0, TAB_VISUALS = 1, TAB_THEME = 2, TAB_MISC = 3 };

    struct State {
        Tab   activeTab      = TAB_AIMING;
        int   visualsSubTab  = 0;   // 0 = Players
        int   heroSegActive  = 0;   // Hero config slot (0-6)
        bool  initialized    = false;
    };
    inline State state;

    void InitStyle();
    void InitializeResources(ID3D11Device* device);
    void ShutdownResources();
    void Render();

    // Per-page render functions
    void AimbotPage();
    void VisualsPage();
    void ThemePage();
    void MiscPage();

} // namespace UI
