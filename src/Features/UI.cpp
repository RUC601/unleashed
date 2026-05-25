#include "Features/UI.hpp"
#include "Utils/Config.hpp"
#include "Renderer/Renderer.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <functional>
#include <vector>

// =====================================================================
// LOCAL CONFIGURATION FALLBACKS
// These variables match settings from the React UI prototype that do not
// yet have corresponding entries in OW::Config (Config.hpp).  When those
// are added, remove the local versions and reference OW::Config directly.
// =====================================================================
namespace {

    // ---- Aimbot ----
    inline int    g_aimbotHero              = 0;     // MISSING from OW::Config
    inline int    g_aimbotAttack            = 0;     // MISSING from OW::Config
    inline bool   g_aimbotAutoshot          = false; // MISSING from OW::Config
    inline bool   g_aimbotKeepFiring        = true;  // MISSING from OW::Config
    inline float  g_aimbotMaxHead           = 100.0f;// MISSING from OW::Config
    inline int    g_aimbotSmoothType        = 0;     // MISSING from OW::Config (0=Constant Speed)
    inline float  g_aimbotStickiness        = 100.0f;// MISSING from OW::Config
    inline float  g_aimbotSmoothY           = 50.0f; // MISSING from OW::Config
    inline int    g_aimbotPriority          = 0;     // MISSING from OW::Config
    inline int    g_aimbotTeam              = 0;     // MISSING from OW::Config
    inline float  g_aimbotMaxAim            = 100.0f;// MISSING from OW::Config
    inline float  g_aimbotHitbox            = 25.0f; // MISSING from OW::Config
    inline float  g_aimbotMinCharge         = 5.0f;  // MISSING from OW::Config
    inline float  g_aimbotMaxCharge         = 100.0f;// MISSING from OW::Config
    inline bool   g_aimbotIgnoreInvisible   = false; // MISSING from OW::Config
    inline int    g_aimbotTrace             = 0;     // MISSING from OW::Config
    inline int    g_aimbotUnlock            = 0;     // MISSING from OW::Config
    inline float  g_aimbotLockTime          = 20.0f; // MISSING from OW::Config
    inline float  g_aimbotMaxDist           = 100.0f;// MISSING from OW::Config
    inline float  g_aimbotMinDist           = 0.0f;  // MISSING from OW::Config

    // ---- Visuals ----
    inline bool   g_visualBox               = true;  // Partial: OW::Config::draw_info
    inline bool   g_visualSkeleton          = true;  // Maps to OW::Config::draw_skel
    inline bool   g_visualRadar             = true;  // Maps to OW::Config::radar
    inline bool   g_visualHealthbar         = true;  // Maps to OW::Config::drawhealth / healthbar
    inline bool   g_visualGlow              = true;  // MISSING from OW::Config
    inline bool   g_visualHero              = false; // MISSING from OW::Config
    inline bool   g_visualDamage            = true;  // MISSING from OW::Config
    inline bool   g_visualLines             = false; // Maps to OW::Config::drawline
    inline bool   g_visualDistance          = false; // Maps to OW::Config::dist
    inline float  g_visualFloat             = 32.0f; // MISSING from OW::Config
    inline float  g_visualDisplay           = 24.0f; // MISSING from OW::Config
    inline bool   g_visualHideTeam          = true;  // MISSING from OW::Config
    inline bool   g_visualHideInvisibleText  = true; // MISSING from OW::Config
    inline bool   g_visualShowTeamRadar     = true;  // MISSING from OW::Config
    inline bool   g_visualHideInvisibleHealth = true;// MISSING from OW::Config
    inline float  g_visualMaxDist           = 100.0f;// MISSING from OW::Config
    inline float  g_visualMaxTextDist       = 100.0f;// MISSING from OW::Config

} // anonymous namespace

// =====================================================================
// SELECT OPTION STRINGS (matching React selectOptions)
// =====================================================================
static const char* kHero[]         = { "All", "Tracer", "Widowmaker", "Soldier: 76" };
static const char* kAimKey[]       = { "Undefined", "Mouse 4", "Mouse 5", "Left Shift" };
static const char* kAttack[]       = { "Shoot", "Ability 1", "Ability 2" };
static const char* kBone[]         = { "Head", "Neck", "Chest" };
static const char* kAimSmoothType[] = { "Constant Speed", "Linear", "Bezier" };
static const char* kPriority[]     = { "Lowest FOV", "Lowest HP", "Distance" };
static const char* kTeam[]         = { "Enemies", "Allies", "All" };
static const char* kTrace[]        = { "Strict", "Relaxed", "Off" };
static const char* kUnlock[]       = { "Anytime", "On Release", "Never" };
static const char* kSlotNums[]     = { "1", "2", "3", "4", "5", "6", "7" };
static const char* kMenuToggleKeys[] = {
    "Home", "Insert", "End", "Delete",
    "F1", "F2", "F3", "F4", "F5", "F6",
    "F7", "F8", "F9", "F10", "F11", "F12"
};
static constexpr int kMenuToggleVk[] = {
    VK_HOME, VK_INSERT, VK_END, VK_DELETE,
    VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6,
    VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12
};

// =====================================================================
// GROUP BOX STATE (lazy border drawing)
// =====================================================================
static bool     g_gbOpen      = false;
static char     g_gbTitle[64] = "";
static ImVec2   g_gbStartPos(0, 0);
static float    g_gbWidth     = 0.0f;
static float    g_gbMinHeight = 0.0f;
static ImDrawListSplitter g_gbDrawSplitter;

static constexpr float kShellWidth = 628.0f;
static constexpr float kShellBorder = 2.0f;
static constexpr float kHeaderHeight = 84.0f;
static constexpr float kDefaultLabelWidth = 120.0f;
static constexpr float kAimbotHeroLabelWidth = 98.0f;
static constexpr float kAimbotLeftLabelWidth = 112.0f;
static constexpr float kAimbotRightLabelWidth = 138.0f;
static constexpr float kControlHeight = 22.0f;
static constexpr float kControlRounding = 4.0f;
static constexpr float kGroupRounding = 5.0f;
static constexpr float kGroupContentIndent = 14.0f;

static const ImU32 kColShell0       = IM_COL32(0x07, 0x09, 0x0e, 0xFF);
static const ImU32 kColShell1       = IM_COL32(0x0d, 0x12, 0x1a, 0xFF);
static const ImU32 kColShell2       = IM_COL32(0x12, 0x0d, 0x13, 0xFF);
static const ImU32 kColPanel        = IM_COL32(0x0f, 0x13, 0x1a, 0xF2);
static const ImU32 kColPanelSoft    = IM_COL32(0x13, 0x18, 0x21, 0xD8);
static const ImU32 kColControl      = IM_COL32(0x18, 0x1e, 0x27, 0xFF);
static const ImU32 kColControlHover = IM_COL32(0x20, 0x28, 0x34, 0xFF);
static const ImU32 kColControlHot   = IM_COL32(0x28, 0x32, 0x40, 0xFF);
static const ImU32 kColStroke       = IM_COL32(0x2b, 0x35, 0x45, 0xBB);
static const ImU32 kColStrokeDark   = IM_COL32(0x06, 0x08, 0x0c, 0xE8);
static const ImU32 kColText         = IM_COL32(0xf3, 0xf6, 0xfb, 0xFF);
static const ImU32 kColTextMuted    = IM_COL32(0xa4, 0xad, 0xba, 0xFF);
static const ImU32 kColTextDim      = IM_COL32(0x78, 0x83, 0x91, 0xFF);
static const ImU32 kColAccent       = IM_COL32(0xff, 0x2e, 0x62, 0xFF);
static const ImU32 kColAccentDark   = IM_COL32(0xb9, 0x12, 0x3b, 0xFF);
static const ImU32 kColAccentSoft   = IM_COL32(0xff, 0x4b, 0x75, 0x52);
static const ImU32 kColAccentGlow   = IM_COL32(0xff, 0x2e, 0x62, 0x28);

static ImFont* s_regularFont = nullptr;
static ImFont* s_boldFont = nullptr;
static ImGuiID s_preNewFrameInitHook = 0;
static ID3D11ShaderResourceView* s_logoTexture = nullptr;
static constexpr int kLogoTextureSize = 32;

// Forward declarations
static void CloseGroupBox();
static void SettingRow(const char* label, float labelWidthPx = kDefaultLabelWidth);

static void InitStyleBeforeNewFrame(ImGuiContext*, ImGuiContextHook*) {
    if (!UI::state.initialized)
        UI::InitStyle();
}

static void EnsurePreNewFrameInitHook() {
    if (s_preNewFrameInitHook != 0)
        return;

    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context)
        return;

    ImGuiContextHook hook;
    hook.Type = ImGuiContextHookType_NewFramePre;
    hook.Callback = InitStyleBeforeNewFrame;
    s_preNewFrameInitHook = ImGui::AddContextHook(context, &hook);
}

static float MaxFloat(float a, float b) {
    return (a > b) ? a : b;
}

static ImU32 MixColor(ImU32 from, ImU32 to, float t) {
    t = ImClamp(t, 0.0f, 1.0f);
    ImVec4 a = ImGui::ColorConvertU32ToFloat4(from);
    ImVec4 b = ImGui::ColorConvertU32ToFloat4(to);
    return ImGui::ColorConvertFloat4ToU32(ImVec4(
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t,
        a.w + (b.w - a.w) * t
    ));
}

static float VisualTransition(ImGuiID id, bool enabled, float speed = 16.0f) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float current = storage->GetFloat(id, enabled ? 1.0f : 0.0f);
    float target = enabled ? 1.0f : 0.0f;
    float step = ImClamp(ImGui::GetIO().DeltaTime * speed, 0.0f, 1.0f);
    current += (target - current) * step;
    storage->SetFloat(id, current);
    return current;
}

static int FindMenuToggleKeyIndex(int vk) {
    for (int i = 0; i < IM_ARRAYSIZE(kMenuToggleVk); ++i) {
        if (kMenuToggleVk[i] == vk)
            return i;
    }
    return 0;
}

static bool CreateTextureFromIconResource(ID3D11Device* device, int resourceId, int size,
                                          ID3D11ShaderResourceView** outTexture) {
    if (!device || !outTexture || size <= 0)
        return false;

    *outTexture = nullptr;

    HICON icon = static_cast<HICON>(
        LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(resourceId),
                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR)
    );
    if (!icon)
        return false;

    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = size;
    bitmapInfo.bmiHeader.biHeight = -size;
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = screenDc ? CreateCompatibleDC(screenDc) : nullptr;
    void* bgraBits = nullptr;
    HBITMAP bitmap = screenDc
        ? CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &bgraBits, nullptr, 0)
        : nullptr;
    if (screenDc)
        ReleaseDC(nullptr, screenDc);

    if (!memoryDc || !bitmap || !bgraBits) {
        if (bitmap)
            DeleteObject(bitmap);
        if (memoryDc)
            DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    const size_t pixelBytes = static_cast<size_t>(size) * static_cast<size_t>(size) * 4;
    std::memset(bgraBits, 0, pixelBytes);

    HGDIOBJ oldBitmap = SelectObject(memoryDc, bitmap);
    const BOOL drewIcon = DrawIconEx(memoryDc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    if (oldBitmap)
        SelectObject(memoryDc, oldBitmap);

    if (!drewIcon) {
        DeleteObject(bitmap);
        DeleteDC(memoryDc);
        DestroyIcon(icon);
        return false;
    }

    std::vector<std::uint8_t> rgba(pixelBytes);
    const auto* bgra = static_cast<const std::uint8_t*>(bgraBits);
    bool hasAlpha = false;
    for (int i = 0; i < size * size; ++i) {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = bgra[i * 4 + 3];
        hasAlpha = hasAlpha || rgba[i * 4 + 3] != 0;
    }

    if (!hasAlpha) {
        for (int i = 0; i < size * size; ++i) {
            if (rgba[i * 4 + 0] || rgba[i * 4 + 1] || rgba[i * 4 + 2])
                rgba[i * 4 + 3] = 0xFF;
        }
    }

    DeleteObject(bitmap);
    DeleteDC(memoryDc);
    DestroyIcon(icon);

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(size);
    textureDesc.Height = static_cast<UINT>(size);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = rgba.data();
    initData.SysMemPitch = static_cast<UINT>(size * 4);

    ID3D11Texture2D* texture = nullptr;
    HRESULT hr = device->CreateTexture2D(&textureDesc, &initData, &texture);
    if (FAILED(hr) || !texture)
        return false;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = textureDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = device->CreateShaderResourceView(texture, &srvDesc, &srv);
    texture->Release();

    if (FAILED(hr) || !srv)
        return false;

    *outTexture = srv;
    return true;
}

static void DrawText(ImDrawList* drawList, ImFont* font, const ImVec2& pos,
                     ImU32 color, const char* text) {
    if (font)
        ImGui::PushFont(font);
    drawList->AddText(pos, color, text);
    if (font)
        ImGui::PopFont();
}

// =====================================================================
// FORMAT SLIDER VALUE  (mirrors React formatSliderText)
// =====================================================================
static const char* FormatSliderValue(char* buf, size_t size, float value, const char* text) {
    if (!text || text[0] == '\0') {
        std::snprintf(buf, size, "%.0f %%", value);
        return buf;
    }

    // Keyword-based formats
    if (std::strcmp(text, "Max") == 0) {
        if (value >= 99.5f) return "Max";
        std::snprintf(buf, size, "%.0f %%", value);
        return buf;
    }
    if (std::strcmp(text, "Endless") == 0) {
        if (value >= 99.5f) return "Endless";
        std::snprintf(buf, size, "%.0f ms", value * 20.0f);
        return buf;
    }
    if (std::strcmp(text, "Instant") == 0) {
        if (value <= 0.5f) return "Instant";
        std::snprintf(buf, size, "%.0f ms", value * 3.0f);
        return buf;
    }
    if (std::strcmp(text, "Unlimited") == 0) {
        if (value >= 99.5f) return "Unlimited";
        std::snprintf(buf, size, "%.0f m", value);
        return buf;
    }
    // Pattern-based formats (encoded as "base-value unit")
    if (std::strcmp(text, "50.00 %") == 0 || std::strcmp(text, "50.00 %%") == 0) {
        std::snprintf(buf, size, "%.2f %%", value);
        return buf;
    }
    if (std::strcmp(text, "10.00 deg") == 0) {
        std::snprintf(buf, size, "%.2f deg", value);
        return buf;
    }
    if (std::strcmp(text, "200 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 10.0f);
        return buf;
    }
    if (std::strcmp(text, "200 cm") == 0) {
        std::snprintf(buf, size, "%.0f cm", value * 6.25f);
        return buf;
    }
    if (std::strcmp(text, "1000 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 41.6667f);
        return buf;
    }
    if (std::strcmp(text, "180 deg") == 0) {
        std::snprintf(buf, size, "%.0f deg", value * 1.8f);
        return buf;
    }
    if (std::strcmp(text, "90 deg/s") == 0) {
        std::snprintf(buf, size, "%.0f deg/s", value * 45.0f);
        return buf;
    }
    if (std::strcmp(text, "7.5 m/s") == 0) {
        std::snprintf(buf, size, "%.1f m/s", value * 3.75f);
        return buf;
    }
    if (std::strcmp(text, "1.0 m/s^2") == 0) {
        std::snprintf(buf, size, "%.1f m/s^2", value * 0.125f);
        return buf;
    }
    if (std::strcmp(text, "35 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 17.5f);
        return buf;
    }
    if (std::strcmp(text, "135 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 15.0f);
        return buf;
    }
    if (std::strcmp(text, "500 ms") == 0) {
        std::snprintf(buf, size, "%.0f ms", value * 13.8889f);
        return buf;
    }

    // Catch-all mirrors React's /^(-?\d+(?:\.\d+)?)(.*)$/ fallback.
    const char* p = text;
    if (*p == '-')
        ++p;

    bool hasDigit = false;
    int decimals = 0;
    while (*p >= '0' && *p <= '9') {
        hasDigit = true;
        ++p;
    }
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') {
            hasDigit = true;
            ++decimals;
            ++p;
        }
    }

    if (hasDigit) {
        std::snprintf(buf, size, "%.*f%s", decimals, value, p);
        return buf;
    }

    return text;
}

// =====================================================================
// UIGroupBox  --  Opens a group box.  Closes any previously open group
//                 box (drawing its border lazily via CloseGroupBox).
// =====================================================================
static bool UIGroupBox(const char* title, float minHeightPx = 0.0f) {
    // Close previous group box first (draws its border)
    if (g_gbOpen)
        CloseGroupBox();

    g_gbOpen = true;
    g_gbMinHeight = minHeightPx;
    std::strncpy(g_gbTitle, title, sizeof(g_gbTitle) - 1);
    g_gbTitle[sizeof(g_gbTitle) - 1] = '\0';

    if (s_boldFont)
        ImGui::PushFont(s_boldFont);
    ImVec2 textSize = ImGui::CalcTextSize(title);
    if (s_boldFont)
        ImGui::PopFont();

    ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    g_gbDrawSplitter.Split(dl, 2);
    g_gbDrawSplitter.SetCurrentChannel(dl, 1);

    DrawText(dl, s_boldFont, ImVec2(pos.x + 16.0f, pos.y + 1.0f),
             kColText, title);

    ImGui::Dummy(ImVec2(0.0f, textSize.y + 10.0f));

    // Save state for lazy border drawing
    g_gbStartPos = ImGui::GetCursorScreenPos();
    g_gbWidth    = ImGui::GetContentRegionAvail().x;

    ImGui::Indent(kGroupContentIndent);
    ImGui::BeginGroup();
    return true;
}

// =====================================================================
// CloseGroupBox  --  Draws the border and legend of the current group
//                    box and restores indentation.
// =====================================================================
static void CloseGroupBox() {
    if (!g_gbOpen) return;

    ImVec2 textSize = s_boldFont ? ImGui::CalcTextSize(g_gbTitle) : ImGui::CalcTextSize(g_gbTitle);
    float  titleH   = textSize.y + 10.0f;
    float  borderTopY = g_gbStartPos.y - titleH;

    if (g_gbMinHeight > 0.0f) {
        float desiredContentEndY = borderTopY + g_gbMinHeight - 6.0f;
        float currentY = ImGui::GetCursorScreenPos().y;
        if (currentY < desiredContentEndY)
            ImGui::Dummy(ImVec2(0.0f, desiredContentEndY - currentY));
    }

    ImGui::EndGroup();   // tracks the content bounds
    ImGui::Unindent(kGroupContentIndent);

    ImVec2 contentEnd = ImGui::GetItemRectMax();
    auto*  dl = ImGui::GetWindowDrawList();

    ImVec2 borderMin = g_gbStartPos;
    borderMin.y       = borderTopY;
    ImVec2 borderMax  = ImVec2(g_gbStartPos.x + g_gbWidth, contentEnd.y + 9.0f);
    if (g_gbMinHeight > 0.0f)
        borderMax.y = MaxFloat(borderMax.y, borderMin.y + g_gbMinHeight);

    g_gbDrawSplitter.SetCurrentChannel(dl, 0);
    dl->AddRectFilled(ImVec2(borderMin.x + 1.0f, borderMin.y + 2.0f),
                      ImVec2(borderMax.x + 1.0f, borderMax.y + 2.0f),
                      IM_COL32(0x00, 0x00, 0x00, 0x38), kGroupRounding);
    dl->AddRectFilled(borderMin, borderMax, kColPanelSoft, kGroupRounding);
    dl->AddRectFilledMultiColor(borderMin, ImVec2(borderMax.x, borderMin.y + 26.0f),
                                IM_COL32(0x18, 0x1e, 0x29, 0x78),
                                IM_COL32(0x18, 0x1e, 0x29, 0x42),
                                IM_COL32(0x10, 0x14, 0x1c, 0x00),
                                IM_COL32(0x10, 0x14, 0x1c, 0x00));
    dl->AddRect(borderMin, borderMax, kColStroke, kGroupRounding, 0, 1.0f);
    dl->AddRect(ImVec2(borderMin.x + 1.0f, borderMin.y + 1.0f),
                ImVec2(borderMax.x - 1.0f, borderMax.y - 1.0f),
                kColStrokeDark, kGroupRounding, 0, 1.0f);
    dl->AddLine(ImVec2(borderMin.x + 10.0f, borderMin.y + 1.0f),
                ImVec2(borderMax.x - 10.0f, borderMin.y + 1.0f),
                IM_COL32(0xff, 0x4b, 0x75, 0x34), 1.0f);

    dl->AddRectFilled(
        ImVec2(borderMin.x + 13.0f, borderMin.y - 1.0f),
        ImVec2(borderMin.x + 18.0f + textSize.x + 12.0f, borderMin.y + textSize.y + 4.0f),
        kColPanel
    );

    g_gbDrawSplitter.SetCurrentChannel(dl, 1);
    DrawText(dl, s_boldFont, ImVec2(borderMin.x + 17.0f, borderMin.y + 2.0f),
             kColText, g_gbTitle);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    if (cursor.y < borderMax.y + 7.0f)
        ImGui::Dummy(ImVec2(0.0f, borderMax.y + 7.0f - cursor.y));

    g_gbDrawSplitter.Merge(dl);
    g_gbOpen = false;
    g_gbMinHeight = 0.0f;
}

// =====================================================================
// UICheckbox  --  compact custom checkbox with premium dark-state styling.
// =====================================================================
static bool UICheckbox(const char* label, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g    = *GImGui;
    const ImGuiID id   = window->GetID(label);
    const float sz     = 12.0f;
    const float height = kControlHeight;
    const float spacing = g.Style.ItemInnerSpacing.x + 2.0f;

    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const bool hasVisibleLabel = labelEnd > label;
    const ImVec2 labelSize = hasVisibleLabel
        ? ImGui::CalcTextSize(label, labelEnd, false)
        : ImVec2(0.0f, 0.0f);
    ImVec2 pos = window->DC.CursorPos;

    ImRect bb(pos, ImVec2(pos.x + sz + (hasVisibleLabel ? spacing + labelSize.x : 0.0f), pos.y + height));

    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *value = !*value;

    float hoverT = VisualTransition(id ^ 0x23a7, hovered || held, 18.0f);
    float checkedT = VisualTransition(id ^ 0x51d3, *value, 18.0f);
    ImVec2 boxMin(bb.Min.x, bb.Min.y + (height - sz) * 0.5f);
    ImVec2 boxMax(boxMin.x + sz, boxMin.y + sz);
    ImU32 baseCol = MixColor(kColControl, kColControlHover, hoverT);
    ImU32 squareCol = MixColor(baseCol, kColAccent, checkedT);
    float rounding = 2.5f;

    if (hovered || held) {
        window->DrawList->AddRectFilled(ImVec2(boxMin.x - 2.0f, boxMin.y - 2.0f),
                                        ImVec2(boxMax.x + 2.0f, boxMax.y + 2.0f),
                                        MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00), kColAccentGlow, hoverT),
                                        rounding + 2.0f);
    }
    window->DrawList->AddRectFilled(boxMin, boxMax, squareCol, rounding);
    window->DrawList->AddRect(boxMin, boxMax,
                              MixColor(kColStroke, IM_COL32(0xff, 0x8a, 0xa6, 0xC0), checkedT),
                              rounding, 0, 1.0f);

    if (checkedT > 0.18f) {
        ImU32 tickCol = MixColor(IM_COL32(0xff, 0xff, 0xff, 0x00),
                                 IM_COL32(0xff, 0xff, 0xff, 0xF4), checkedT);
        window->DrawList->AddLine(ImVec2(boxMin.x + 3.0f, boxMin.y + 6.0f),
                                  ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  tickCol, 1.4f);
        window->DrawList->AddLine(ImVec2(boxMin.x + 5.2f, boxMin.y + 8.1f),
                                  ImVec2(boxMin.x + 9.1f, boxMin.y + 3.8f),
                                  tickCol, 1.4f);
    }

    if (hasVisibleLabel) {
        ImVec2 labelPos(bb.Min.x + sz + spacing, bb.Min.y + (height - labelSize.y) * 0.5f);
        ImU32 labelCol = MixColor(kColTextMuted, kColText, MaxFloat(hoverT, checkedT * 0.5f));
        window->DrawList->AddText(labelPos, labelCol, label, labelEnd);
    }

    return pressed;
}

// =====================================================================
// UISlider  --  Custom dark track with accent fill and value text.
// =====================================================================
static bool UISlider(const char* label, float* value, float v_min, float v_max,
                     const char* formatText) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id   = window->GetID(label);
    const float height = kControlHeight;

    ImVec2 pos  = window->DC.CursorPos;
    float width = ImGui::GetContentRegionAvail().x;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    ImGui::ItemSize(bb);
    if (!ImGui::ItemAdd(bb, id, nullptr, ImGuiItemFlags_Inputable)) return false;

    // Normalise value to [0,1]
    float range  = (v_max - v_min);
    float v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    // Interaction
    ImGuiContext& g = *GImGui;
    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held,
                                         ImGuiButtonFlags_MouseButtonLeft |
                                         ImGuiButtonFlags_PressedOnClick);
    bool valueChanged = false;

    auto setFromNorm = [&](float normalized) {
        normalized = ImClamp(normalized, 0.0f, 1.0f);
        *value = v_min + normalized * range;
        valueChanged = true;
    };

    const bool mousePressed = pressed && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool mouseDragging = held && g.ActiveId == id && ImGui::IsMouseDown(ImGuiMouseButton_Left);

    if (mousePressed)
        ImGui::SetKeyboardFocusHere(-1);

    if (mousePressed || mouseDragging) {
        const ImVec2 mousePos = ImGui::GetMousePos();
        setFromNorm((mousePos.x - bb.Min.x) / (bb.Max.x - bb.Min.x));
    }

    if (ImGui::IsItemFocused()) {
        ImGuiIO& io = ImGui::GetIO();
        float step = io.KeyAlt ? 0.1f : (io.KeyShift ? 5.0f : 1.0f);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            *value = ImClamp(*value + step, v_min, v_max);
            valueChanged = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            *value = ImClamp(*value - step, v_min, v_max);
            valueChanged = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            *value = v_min;
            valueChanged = true;
        } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            *value = v_max;
            valueChanged = true;
        }
    }

    v_norm = (range > 0.0f) ? (*value - v_min) / range : 0.0f;
    v_norm = ImClamp(v_norm, 0.0f, 1.0f);

    float hoverT = VisualTransition(id ^ 0x6f47, hovered || held || ImGui::IsItemFocused(), 18.0f);
    ImU32 trackCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x28), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f),
                                    bb.Max, trackCol, kControlRounding);
    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    window->DrawList->AddLine(ImVec2(bb.Min.x + 5.0f, bb.Min.y + 1.0f),
                              ImVec2(bb.Max.x - 5.0f, bb.Min.y + 1.0f),
                              IM_COL32(0xff, 0xff, 0xff, 0x12), 1.0f);

    float fillW = v_norm * (bb.Max.x - bb.Min.x);
    if (fillW > 0.0f) {
        ImVec2 fillMax(bb.Min.x + fillW, bb.Max.y);
        window->DrawList->AddRectFilledMultiColor(
            bb.Min, fillMax,
            kColAccent, IM_COL32(0xff, 0x56, 0x7e, 0xFF),
            kColAccentDark, kColAccent);
        window->DrawList->AddRectFilled(bb.Min, fillMax,
                                        IM_COL32(0xff, 0xff, 0xff, 0x08),
                                        kControlRounding);
    }

    // Value text
    char tmp[64];
    const char* displayText = FormatSliderValue(tmp, sizeof(tmp), *value, formatText);
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    ImVec2 textPos(bb.Max.x - textSize.x - 8.0f, bb.Min.y + (height - textSize.y) * 0.5f);
    window->DrawList->AddText(
        ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
        IM_COL32(0x00, 0x00, 0x00, 0x80), displayText);
    window->DrawList->AddText(textPos, kColText, displayText);

    return valueChanged;
}

// =====================================================================
// UISelect  --  Custom-styled dropdown using ImGui BeginCombo.
//               Dropdown items get a red #e41143 background on hover/active.
// =====================================================================
static bool UISelect(const char* label, int* current, const char* items[], int itemCount) {
    if (*current < 0 || *current >= itemCount)
        *current = 0;

    const char* preview = items[*current];
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    const float height = kControlHeight;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    bool preHovered = ImGui::IsMouseHoveringRect(bb.Min, bb.Max);
    float hoverT = VisualTransition(window->GetID(label) ^ 0x3911, preHovered, 18.0f);
    ImU32 frameCol = MixColor(kColControl, kColControlHover, hoverT);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x00, 0x00, 0x00, 0x2E), kControlRounding);
    window->DrawList->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), bb.Max,
                                    frameCol, kControlRounding);

    ImGui::PushStyleColor(ImGuiCol_Header,        kColAccentDark);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, kColAccent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  kColAccent);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       kColPanel);
    ImGui::PushStyleColor(ImGuiCol_Border,        kColStroke);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       frameCol);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, MixColor(kColControlHover, kColControlHot, hoverT));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kColControlHot);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(8.0f, MaxFloat(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, kControlRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kControlRounding);

    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, preview,
                          ImGuiComboFlags_HeightLargest | ImGuiComboFlags_NoArrowButton)) {
        for (int i = 0; i < itemCount; i++) {
            const bool isSelected = (*current == i);
            if (ImGui::Selectable(items[i], isSelected)) {
                *current = i;
                changed = true;
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);

    window->DrawList->AddRect(bb.Min, bb.Max,
                              MixColor(kColStrokeDark, kColStroke, hoverT),
                              kControlRounding, 0, 1.0f);
    ImVec2 caretCenter(bb.Max.x - 11.0f, bb.Min.y + height * 0.5f);
    ImU32 caretCol = MixColor(kColTextDim, kColText, hoverT);
    window->DrawList->AddLine(ImVec2(caretCenter.x - 4.0f, caretCenter.y - 2.0f),
                              ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              caretCol, 1.35f);
    window->DrawList->AddLine(ImVec2(caretCenter.x, caretCenter.y + 2.5f),
                              ImVec2(caretCenter.x + 4.0f, caretCenter.y - 2.0f),
                              caretCol, 1.35f);
    return changed;
}

// =====================================================================
// UISegmented  --  Row of buttons, active one gets accent background.
//                  Returns the new active index (or old if unchanged).
// =====================================================================
static int UISegmented(const char* items[], int itemCount, int active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float width  = ImGui::GetContentRegionAvail().x;
    float height = (itemCount <= 5) ? 21.0f : 17.0f;

    ImVec2 pos = window->DC.CursorPos;

    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                    kColControl, kControlRounding);
    window->DrawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
                              kColStrokeDark, kControlRounding, 0, 1.0f);

    float segW = width / itemCount;
    int   result = active;

    for (int i = 0; i < itemCount; i++) {
        ImVec2 segMin(pos.x + i * segW, pos.y);
        ImVec2 segMax(pos.x + (i + 1) * segW, pos.y + height);

        ImGui::SetCursorScreenPos(segMin);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(segW, height));
        bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked())
            result = i;
        ImGui::PopID();

        float hoverT = VisualTransition(window->GetID(items[i]) ^ 0x7791, hovered, 16.0f);
        if (hovered && i != active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                                     kColControlHover, hoverT),
                                            kControlRounding);
        }
        if (i == active) {
            window->DrawList->AddRectFilled(ImVec2(segMin.x + 1.0f, segMin.y + 1.0f),
                                            ImVec2(segMax.x - 1.0f, segMax.y - 1.0f),
                                            kColAccent, kControlRounding);
            window->DrawList->AddLine(ImVec2(segMin.x + 5.0f, segMin.y + 1.0f),
                                      ImVec2(segMax.x - 5.0f, segMin.y + 1.0f),
                                      IM_COL32(0xff, 0xff, 0xff, 0x28), 1.0f);
        }

        const char* txt = items[i];
        ImVec2 tsz = ImGui::CalcTextSize(txt);
        ImVec2 txtPos(segMin.x + (segW - tsz.x) * 0.5f,
                      segMin.y + (height - tsz.y) * 0.5f);
        ImU32 txtCol = (i == active)
            ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
            : MixColor(kColTextMuted, kColText, hoverT);
        window->DrawList->AddText(txtPos, txtCol, txt);
    }

    ImGui::Dummy(ImVec2(0.0f, height + 8.0f));

    return result;
}

// =====================================================================
// UITwoColumns  --  Two equal-width columns.
// =====================================================================
static void UITwoColumns(std::function<void()> left, std::function<void()> right) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 6.0f));
    ImGui::Columns(2, nullptr, false);
    left();
    ImGui::NextColumn();
    right();
    ImGui::Columns(1);
    ImGui::PopStyleVar();
}

// =====================================================================
// SettingRow  --  Renders a fixed-width label column + the next widget.
// =====================================================================
static void SettingRow(const char* label, float labelWidthPx) {
    float startX = ImGui::GetCursorPosX();
    float startY = ImGui::GetCursorPosY();
    ImVec2 screenPos = ImGui::GetCursorScreenPos();
    ImVec2 labelSize = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(screenPos.x, screenPos.y + (kControlHeight - labelSize.y) * 0.5f),
        kColTextMuted, label);
    ImGui::SetCursorPosX(startX + labelWidthPx);
    ImGui::SetCursorPosY(startY);
}

// =====================================================================
// DrawDivider  --  Thin separator line matching React .divider
// =====================================================================
static void DrawDivider() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        pos, ImVec2(pos.x + width, pos.y), IM_COL32(0x2b, 0x35, 0x45, 0x72));
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

static void DrawCheckboxGrid3(const char* labels[], bool* values[], int rowCount,
                              float gapX, const float ratios[3]) {
    float avail = ImGui::GetContentRegionAvail().x;
    float ratioTotal = ratios[0] + ratios[1] + ratios[2];
    float usable = MaxFloat(0.0f, avail - gapX * 2.0f);
    float colW[3] = {
        usable * ratios[0] / ratioTotal,
        usable * ratios[1] / ratioTotal,
        usable * ratios[2] / ratioTotal
    };

    ImVec2 start = ImGui::GetCursorScreenPos();
    float colX[3] = {
        start.x,
        start.x + colW[0] + gapX,
        start.x + colW[0] + gapX + colW[1] + gapX
    };
    const float rowH = kControlHeight;
    const float rowGap = 10.0f;

    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (!labels[idx] || !values[idx])
                continue;

            ImGui::SetCursorScreenPos(ImVec2(colX[col], start.y + row * (rowH + rowGap)));
            UICheckbox(labels[idx], values[idx]);
        }
    }

    float gridH = (rowCount > 0) ? (rowCount * rowH + (rowCount - 1) * rowGap) : 0.0f;
    ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + gridH));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

static void DrawTopTabIcon(ImDrawList* drawList, int tabIndex, const ImVec2& min, ImU32 color) {
    ImVec2 c(min.x + 9.0f, min.y + 9.0f);

    if (tabIndex == 0) {
        drawList->AddCircle(c, 6.0f, color, 24, 1.5f);
        drawList->AddCircle(c, 2.0f, color, 16, 1.5f);
        drawList->AddLine(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.5f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x + 4.5f, c.y), ImVec2(c.x + 8.0f, c.y), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y - 4.5f), color, 1.5f);
        drawList->AddLine(ImVec2(c.x, c.y + 4.5f), ImVec2(c.x, c.y + 8.0f), color, 1.5f);
    } else if (tabIndex == 1) {
        drawList->AddEllipse(c, ImVec2(8.0f, 4.8f), color, 0.0f, 24, 1.5f);
        drawList->AddCircle(c, 2.4f, color, 16, 1.5f);
    } else if (tabIndex == 2) {
        drawList->AddCircle(c, 7.0f, color, 24, 1.5f);
        drawList->AddCircleFilled(ImVec2(c.x - 2.5f, c.y - 2.2f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x + 2.0f, c.y - 2.5f), 1.1f, color, 8);
        drawList->AddCircleFilled(ImVec2(c.x - 0.5f, c.y + 2.4f), 1.1f, color, 8);
        drawList->AddCircle(ImVec2(c.x + 4.2f, c.y + 3.4f), 2.1f, color, 10, 1.5f);
    } else {
        drawList->AddCircle(c, 3.3f, color, 16, 1.5f);
        for (int i = 0; i < 8; ++i) {
            float a = (3.14159265f * 2.0f * i) / 8.0f;
            ImVec2 inner(c.x + std::cos(a) * 5.1f, c.y + std::sin(a) * 5.1f);
            ImVec2 outer(c.x + std::cos(a) * 8.0f, c.y + std::sin(a) * 8.0f);
            drawList->AddLine(inner, outer, color, 1.5f);
        }
    }
}

static float CurrentShellHeight() {
    if (UI::state.activeTab == UI::TAB_AIMING)
        return 548.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 476.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 190.0f;
    return 140.0f;
}

void UI::InitializeResources(ID3D11Device* device) {
    if (s_logoTexture || !device)
        return;

    CreateTextureFromIconResource(device, IDI_UNLEASHED, kLogoTextureSize, &s_logoTexture);
}

void UI::ShutdownResources() {
    if (s_logoTexture) {
        s_logoTexture->Release();
        s_logoTexture = nullptr;
    }
}

// =====================================================================
// UI::InitStyle  --  Refined dark overlay styling.
// =====================================================================
void UI::InitStyle() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableTooltip = false;

    if (!s_regularFont && !io.Fonts->Locked) {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 3;
        fontConfig.OversampleV = 2;
        fontConfig.PixelSnapH = false;
        fontConfig.RasterizerMultiply = 1.05f;
        s_regularFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 12.5f, &fontConfig);
        s_boldFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 12.5f, &fontConfig);
        if (s_regularFont)
            io.FontDefault = s_regularFont;
    } else if (s_regularFont) {
        io.FontDefault = s_regularFont;
    }

    ImGuiStyle& style   = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedFill  = true;
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = kControlRounding;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = kControlRounding;
    style.TabRounding       = 4.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = kControlRounding;
    style.PopupBorderSize   = 1.0f;
    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.ScrollbarSize     = 7.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_FrameBg]           = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_FrameBgHovered]    = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_FrameBgActive]     = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Button]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_ButtonHovered]     = ImGui::ColorConvertU32ToFloat4(kColControlHover);
    colors[ImGuiCol_ButtonActive]      = ImGui::ColorConvertU32ToFloat4(kColControlHot);
    colors[ImGuiCol_Header]            = ImGui::ColorConvertU32ToFloat4(kColControl);
    colors[ImGuiCol_HeaderHovered]     = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_HeaderActive]      = ImGui::ColorConvertU32ToFloat4(kColAccentDark);
    colors[ImGuiCol_CheckMark]         = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrab]        = ImGui::ColorConvertU32ToFloat4(kColAccent);
    colors[ImGuiCol_SliderGrabActive]  = ImGui::ColorConvertU32ToFloat4(IM_COL32(0xff, 0x56, 0x7e, 0xFF));
    colors[ImGuiCol_Text]              = ImGui::ColorConvertU32ToFloat4(kColText);
    colors[ImGuiCol_TextDisabled]      = ImGui::ColorConvertU32ToFloat4(kColTextDim);
    colors[ImGuiCol_Border]            = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_TitleBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_TitleBgActive]     = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_Separator]         = ImGui::ColorConvertU32ToFloat4(kColStroke);
    colors[ImGuiCol_ScrollbarBg]       = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x0b, 0x0f, 0x16, 0xE0));
    colors[ImGuiCol_ScrollbarGrab]     = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x2b, 0x35, 0x45, 0xD8));
    colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(IM_COL32(0x3a, 0x46, 0x59, 0xFF));
    colors[ImGuiCol_PopupBg]           = ImGui::ColorConvertU32ToFloat4(kColPanel);
    colors[ImGuiCol_TextSelectedBg]    = ImGui::ColorConvertU32ToFloat4(kColAccentSoft);
    state.initialized = s_regularFont != nullptr || !io.Fonts->Locked;
}

// =====================================================================
// UI::AimbotPage
// =====================================================================
void UI::AimbotPage() {
    // ---- 7-slot hero segmented ----
    state.heroSegActive = UISegmented(kSlotNums, 7, state.heroSegActive);

    // ---- Hero Selection ----
    UIGroupBox("Hero Selection");
    {
        SettingRow("Hero", kAimbotHeroLabelWidth);
        ImGui::PushItemWidth(-1);
        UISelect("##aimHero", &g_aimbotHero, kHero, IM_ARRAYSIZE(kHero));
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    // ---- Two columns ----
    UITwoColumns([]() {
        // LEFT: Aimbot Hero Basic Options
        UIGroupBox("Aimbot Hero Basic Options", 370.0f);
        {
            // AimKey  (maps to OW::Config::AimKey, but uses index vs VK)
            SettingRow("AimKey", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            static int aimKeyIdx = 0;
            UISelect("##aimKey", &aimKeyIdx, kAimKey, IM_ARRAYSIZE(kAimKey));
            ImGui::PopItemWidth();

            // Attack
            SettingRow("Attack", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimAttack", &g_aimbotAttack, kAttack, IM_ARRAYSIZE(kAttack));
            ImGui::PopItemWidth();

            // Autoshot
            SettingRow("Autoshot", kAimbotLeftLabelWidth);
            UICheckbox("##aimAutoshot", &g_aimbotAutoshot);

            // Keep Firing
            SettingRow("Keep Firing", kAimbotLeftLabelWidth);
            UICheckbox("##aimKeepFire", &g_aimbotKeepFiring);

            // Bone Preference  (maps to OW::Config::TargetBone)
            SettingRow("Bone Preference", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimBone", &OW::Config::TargetBone, kBone, IM_ARRAYSIZE(kBone));
            ImGui::PopItemWidth();

            // Max Head Distance
            SettingRow("Max Head Distance", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMaxHead", &g_aimbotMaxHead, 0.0f, 100.0f, "Max");
            ImGui::PopItemWidth();

            // Stickiness
            SettingRow("Stickiness", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimStick", &g_aimbotStickiness, 0.0f, 100.0f, "Max");
            ImGui::PopItemWidth();

            // REMOVED: smoothing is internal-only, DMA external uses raw angles

            // FOV  (partial: OW::Config::Fov uses 150.0 range)
            SettingRow("FOV", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimFov", &OW::Config::Fov, 0.0f, 100.0f, "10.00 deg");
            ImGui::PopItemWidth();

            // Target Priority
            SettingRow("Target Priority", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimPriority", &g_aimbotPriority, kPriority, IM_ARRAYSIZE(kPriority));
            ImGui::PopItemWidth();

            // Target Team
            SettingRow("Target Team", kAimbotLeftLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimTeam", &g_aimbotTeam, kTeam, IM_ARRAYSIZE(kTeam));
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    }, []() {
        // RIGHT: Aimbot Hero Expert Options
        UIGroupBox("Aimbot Hero Expert Options", 425.0f);
        {
            // Max Aim Time
            SettingRow("Max Aim Time", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMaxAim", &g_aimbotMaxAim, 0.0f, 100.0f, "Endless");
            ImGui::PopItemWidth();

            // Hitbox Size
            SettingRow("Hitbox Size", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimHitbox", &g_aimbotHitbox, 0.0f, 100.0f, "25 %");
            ImGui::PopItemWidth();

            // Aim Min Charge
            SettingRow("Aim Min Charge", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMinChg", &g_aimbotMinCharge, 0.0f, 100.0f, "5 %");
            ImGui::PopItemWidth();

            // Autoshot Max Charge
            SettingRow("Autoshot Max Charge", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMaxChg", &g_aimbotMaxCharge, 0.0f, 100.0f, "100 %");
            ImGui::PopItemWidth();

            // Ignore Invisible Targets
            SettingRow("Ignore Invisible Targets", kAimbotRightLabelWidth);
            UICheckbox("##aimIgnoreInvis", &g_aimbotIgnoreInvisible);

            // Trace Condition
            SettingRow("Trace Condition", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimTrace", &g_aimbotTrace, kTrace, IM_ARRAYSIZE(kTrace));
            ImGui::PopItemWidth();

            // Unlock Condition
            SettingRow("Unlock Condition", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISelect("##aimUnlock", &g_aimbotUnlock, kUnlock, IM_ARRAYSIZE(kUnlock));
            ImGui::PopItemWidth();

            // Lock Time
            SettingRow("Lock Time", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimLockTime", &g_aimbotLockTime, 0.0f, 100.0f, "200 ms");
            ImGui::PopItemWidth();

            // Max Distance
            SettingRow("Max Distance", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMaxDist", &g_aimbotMaxDist, 0.0f, 100.0f, "Max");
            ImGui::PopItemWidth();

            // Min Distance
            SettingRow("Min Distance", kAimbotRightLabelWidth);
            ImGui::PushItemWidth(-1);
            UISlider("##aimMinDist", &g_aimbotMinDist, 0.0f, 100.0f, "0 m");
            ImGui::PopItemWidth();
        }
        CloseGroupBox();
    });
}

// =====================================================================
// UI::VisualsPage
// =====================================================================
void UI::VisualsPage() {
    // Player Visual Features -- 3-column grid of checkboxes
    UIGroupBox("Player Visual Features");
    {
        const char* labels[] = {
            "Box ESP", "Healthbar", "Damage Text",
            "Skeleton", "Glow", "Lines",
            "Radar", "Hero", "Distance"
        };
        bool* values[] = {
            &g_visualBox, &g_visualHealthbar, &g_visualDamage,
            &g_visualSkeleton, &g_visualGlow, &g_visualLines,
            &g_visualRadar, &g_visualHero, &g_visualDistance
        };
        const float ratios[] = { 1.0f, 1.0f, 1.0f };
        DrawCheckboxGrid3(labels, values, 3, 26.0f, ratios);
    }
    CloseGroupBox();

    // Damage Text Options
    UIGroupBox("Damage Text Options");
    {
        SettingRow("Floating Distance");
        ImGui::PushItemWidth(-1);
        UISlider("##visFloat", &g_visualFloat, 0.0f, 100.0f, "200 cm");
        ImGui::PopItemWidth();

        SettingRow("Display Time");
        ImGui::PushItemWidth(-1);
        UISlider("##visDisplay", &g_visualDisplay, 0.0f, 100.0f, "1000 ms");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();

    // Filters
    UIGroupBox("Filters");
    {
        const char* labels[] = {
            "Hide Team", "Show Team On Radar", nullptr,
            "Hide Invisible Text", "Hide Invisible Healthbar", nullptr
        };
        bool* values[] = {
            &g_visualHideTeam, &g_visualShowTeamRadar, nullptr,
            &g_visualHideInvisibleText, &g_visualHideInvisibleHealth, nullptr
        };
        const float ratios[] = { 1.0f, 1.45f, 0.7f };
        DrawCheckboxGrid3(labels, values, 2, 19.0f, ratios);

        ImGui::Dummy(ImVec2(0.0f, 9.0f));

        SettingRow("Max Distance");
        ImGui::PushItemWidth(-1);
        UISlider("##visMaxDist", &g_visualMaxDist, 0.0f, 100.0f, "Unlimited");
        ImGui::PopItemWidth();

        SettingRow("Max Text Distance");
        ImGui::PushItemWidth(-1);
        UISlider("##visMaxTxtDist", &g_visualMaxTextDist, 0.0f, 100.0f, "Unlimited");
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

// =====================================================================
// UI::ThemePage  --  No controls in the React prototype.
// =====================================================================
void UI::ThemePage() {
}

// =====================================================================
// UI::MiscPage
// =====================================================================
void UI::MiscPage() {
    UIGroupBox("Menu");
    {
        SettingRow("Toggle Key");
        ImGui::PushItemWidth(-1);
        int toggleKeyIndex = FindMenuToggleKeyIndex(OW::Config::MenuToggleKey);
        OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        if (UISelect("##menuToggleKey", &toggleKeyIndex, kMenuToggleKeys, IM_ARRAYSIZE(kMenuToggleKeys)))
            OW::Config::MenuToggleKey = kMenuToggleVk[toggleKeyIndex];
        ImGui::PopItemWidth();
    }
    CloseGroupBox();
}

// =====================================================================
// UI::Render  --  Main entry point, called each frame.
//                 Draws the top bar, sub-tabs, and page body.
// =====================================================================
void UI::Render() {
    EnsurePreNewFrameInitHook();
    if (!state.initialized)
        InitStyle();

    if (!OW::Config::Menu)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    if (!ImGui::Begin("Unleashed##panel", nullptr,
                      ImGuiWindowFlags_NoDecoration |
                      ImGuiWindowFlags_NoMove |
                      ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoBackground |
                      ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        return;
    }

    auto* dl = ImGui::GetWindowDrawList();

    ImVec2 shellMin = viewport->Pos;
    float shellWidth = MaxFloat(kShellWidth, viewport->Size.x);
    float shellHeight = MaxFloat(CurrentShellHeight(), viewport->Size.y);

    ImVec2 shellMax(shellMin.x + shellWidth, shellMin.y + shellHeight);
    dl->AddRectFilled(shellMin, shellMax, kColShell0);
    dl->AddRectFilledMultiColor(shellMin, shellMax,
                                kColShell1,
                                kColShell0,
                                kColShell0,
                                kColShell2);
    dl->AddRectFilledMultiColor(shellMin, ImVec2(shellMax.x, shellMin.y + 180.0f),
                                IM_COL32(0x1a, 0x24, 0x32, 0x8A),
                                IM_COL32(0x10, 0x16, 0x20, 0x36),
                                IM_COL32(0x07, 0x09, 0x0e, 0x00),
                                IM_COL32(0x07, 0x09, 0x0e, 0x00));
    for (float x = shellMin.x + 24.0f; x < shellMax.x; x += 32.0f)
        dl->AddLine(ImVec2(x, shellMin.y), ImVec2(x, shellMax.y), IM_COL32(0xff, 0xff, 0xff, 0x05));
    for (float y = shellMin.y + 24.0f; y < shellMax.y; y += 32.0f)
        dl->AddLine(ImVec2(shellMin.x, y), ImVec2(shellMax.x, y), IM_COL32(0xff, 0xff, 0xff, 0x04));

    ImVec2 winPos(shellMin.x + kShellBorder, shellMin.y + kShellBorder);
    ImVec2 contentMax(shellMax.x - kShellBorder, shellMax.y - kShellBorder);
    float  winW = shellWidth - kShellBorder * 2.0f;

    // ==================================================================
    // TOP BAR
    // ==================================================================
    {
        ImRect headerRect(winPos, ImVec2(winPos.x + winW, winPos.y + kHeaderHeight));
        dl->AddRectFilled(headerRect.Min, headerRect.Max, kColPanel);
        dl->AddRectFilledMultiColor(headerRect.Min, headerRect.Max,
                                    IM_COL32(0x16, 0x1c, 0x27, 0xFF),
                                    IM_COL32(0x0d, 0x12, 0x1a, 0xFF),
                                    IM_COL32(0x0b, 0x0f, 0x16, 0xFF),
                                    IM_COL32(0x14, 0x10, 0x18, 0xFF));
        dl->AddRectFilled(ImVec2(headerRect.Min.x, headerRect.Max.y - 1.0f),
                          headerRect.Max, kColAccent);
        dl->AddLine(ImVec2(headerRect.Min.x + 10.0f, headerRect.Min.y + 4.0f),
                    ImVec2(headerRect.Max.x - 10.0f, headerRect.Min.y + 4.0f),
                    IM_COL32(0x7f, 0xa8, 0xff, 0x26), 1.0f);

        ImVec2 brandPos(winPos.x + 14.0f, winPos.y + 10.0f);
        if (s_logoTexture) {
            dl->AddImage(reinterpret_cast<ImTextureID>(s_logoTexture),
                         brandPos,
                         ImVec2(brandPos.x + 30.0f, brandPos.y + 30.0f));
        }

        DrawText(dl, s_boldFont, ImVec2(brandPos.x + 40.0f, brandPos.y + 2.0f),
                 kColText, "UNLEASHED");
        dl->AddText(ImVec2(brandPos.x + 40.0f, brandPos.y + 18.0f),
                    kColTextDim, "DX11 ANALYSIS");

        ImVec2 closeMin(headerRect.Max.x - 34.0f, headerRect.Min.y + 10.0f);
        ImGui::SetCursorScreenPos(closeMin);
        ImGui::InvisibleButton("##closeMenu", ImVec2(22.0f, 22.0f));
        if (ImGui::IsItemClicked())
            OW::Config::Menu = false;
        if (ImGui::IsItemHovered()) {
            dl->AddRectFilled(ImVec2(closeMin.x - 2.0f, closeMin.y - 2.0f),
                              ImVec2(closeMin.x + 24.0f, closeMin.y + 24.0f),
                              IM_COL32(0xff, 0x2e, 0x62, 0x1E), 5.0f);
        }
        ImU32 closeCol = ImGui::IsItemHovered()
            ? kColAccent
            : kColTextMuted;
        dl->AddLine(ImVec2(closeMin.x + 6.0f, closeMin.y + 6.0f),
                    ImVec2(closeMin.x + 16.0f, closeMin.y + 16.0f),
                    closeCol, 1.6f);
        dl->AddLine(ImVec2(closeMin.x + 16.0f, closeMin.y + 6.0f),
                    ImVec2(closeMin.x + 6.0f, closeMin.y + 16.0f),
                    closeCol, 1.6f);

        // Top tab bar at the bottom of the header
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 8.0f, winPos.y + kHeaderHeight - 43.0f));

        const char* topTabNames[] = { "Aiming", "Visuals", "Theme", "Misc" };
        for (int i = 0; i < IM_ARRAYSIZE(topTabNames); i++) {
            bool isActive = (state.activeTab == i);

            ImVec2 tabPos = ImGui::GetCursorScreenPos();
            float tabW = 109.0f;

            ImGui::PushID(i);
            ImGui::InvisibleButton("##topTab", ImVec2(tabW, 43.0f));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                state.activeTab = (Tab)i;
            ImGui::PopID();

            float tabT = VisualTransition(ImGui::GetID(topTabNames[i]) ^ 0x2261,
                                           isActive || hovered, 14.0f);
            if (isActive) {
                dl->AddRectFilled(ImVec2(tabPos.x + 1.0f, tabPos.y + 4.0f),
                                  ImVec2(tabPos.x + tabW - 3.0f, tabPos.y + 42.0f),
                                  IM_COL32(0x17, 0x1d, 0x27, 0xFF), 5.0f);
                dl->AddRectFilled(ImVec2(tabPos.x + 9.0f, tabPos.y + 40.0f),
                                  ImVec2(tabPos.x + tabW - 12.0f, tabPos.y + 42.0f),
                                  kColAccent, 2.0f);
            } else if (hovered) {
                dl->AddRectFilled(ImVec2(tabPos.x + 1.0f, tabPos.y + 6.0f),
                                  ImVec2(tabPos.x + tabW - 3.0f, tabPos.y + 40.0f),
                                  MixColor(IM_COL32(0x00, 0x00, 0x00, 0x00),
                                           IM_COL32(0x20, 0x28, 0x34, 0x9C), tabT),
                                  5.0f);
            }

            ImU32 txtCol = isActive
                ? kColAccent
                : MixColor(kColTextMuted, kColText, tabT);

            ImVec2 txtSize = ImGui::CalcTextSize(topTabNames[i]);
            DrawText(dl, isActive ? s_boldFont : nullptr,
                     ImVec2(tabPos.x + 29.0f, tabPos.y + (43.0f - txtSize.y) * 0.5f),
                     txtCol, topTabNames[i]);

            DrawTopTabIcon(dl, i, ImVec2(tabPos.x + 8.0f, tabPos.y + 12.5f), txtCol);

            ImGui::SetCursorScreenPos(ImVec2(tabPos.x + tabW, tabPos.y));
        }
    }

    // ==================================================================
    // CONTENT BAND (sub-tab bar + body area)
    // ==================================================================
    float contentBandY = winPos.y + kHeaderHeight;

    // Sub-tabs & page body content (drawn inside a child region)
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, contentBandY));

    // Determine which sub-tabs to show
    const char* subTabNames[4] = { nullptr };
    int subTabCount = 0;
    int fixedActiveSub = 0;
    int* activeSub  = nullptr;

    switch (state.activeTab) {
        case TAB_AIMING:
            subTabNames[0] = "Aimbot";
            subTabCount = 1;
            activeSub   = &fixedActiveSub;
            break;
        case TAB_VISUALS:
            subTabNames[0] = "Players";
            subTabCount = 1;
            activeSub   = &state.visualsSubTab;
            break;
        case TAB_THEME:
            subTabCount = 0;
            break;
        case TAB_MISC:
            subTabCount = 0;
            break;
    }

    float subBarHeight = 0.0f;
    if (subTabCount > 0) {
        subBarHeight = (state.activeTab == TAB_AIMING) ? 16.0f : 42.0f;
    }

    ImRect subBarRect(ImVec2(winPos.x, contentBandY),
                      ImVec2(winPos.x + winW, contentBandY + subBarHeight));
    if (subBarHeight > 0.0f) {
        dl->AddRectFilled(subBarRect.Min, subBarRect.Max, IM_COL32(0x11, 0x16, 0x1e, 0xF2));
        dl->AddLine(ImVec2(subBarRect.Min.x, subBarRect.Max.y - 1.0f),
                    ImVec2(subBarRect.Max.x, subBarRect.Max.y - 1.0f),
                    IM_COL32(0x2b, 0x35, 0x45, 0x86), 1.0f);
    }

    // Draw sub-tab buttons
    if (subTabCount > 0 && activeSub) {
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 8.0f, contentBandY));
        for (int i = 0; i < subTabCount; i++) {
            bool isActive = (*activeSub == i);
            ImVec2 pos = ImGui::GetCursorScreenPos();

            ImGui::PushID(i + 10);
            ImGui::InvisibleButton("##subTab", ImVec2(60.0f, subBarHeight));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked())
                *activeSub = i;
            ImGui::PopID();

            float subT = VisualTransition(ImGui::GetID(subTabNames[i]) ^ 0x2c91,
                                           isActive || hovered, 16.0f);
            ImVec2 subTextSize = ImGui::CalcTextSize(subTabNames[i]);
            ImVec2 subTextPos(pos.x, pos.y + (subBarHeight - subTextSize.y) * 0.5f);
            ImU32 col = isActive
                ? kColText
                : MixColor(kColTextDim, kColTextMuted, subT);
            dl->AddText(subTextPos, col, subTabNames[i]);
            if (isActive) {
                dl->AddRectFilled(ImVec2(subTextPos.x, subBarRect.Max.y - 3.0f),
                                  ImVec2(subTextPos.x + subTextSize.x, subBarRect.Max.y - 1.0f),
                                  kColAccent, 1.0f);
            }

            ImGui::SetCursorScreenPos(ImVec2(pos.x + 60.0f + 20.0f, pos.y));
        }
    }

    // ==================================================================
    // PAGE BODY
    // ==================================================================
    float bodyTopPad = 10.0f;
    if (state.activeTab == TAB_AIMING)       bodyTopPad = 8.0f;
    else if (state.activeTab == TAB_VISUALS) bodyTopPad = 10.0f;

    float bodyY = contentBandY + subBarHeight + bodyTopPad;
    float bodyH = MaxFloat(0.0f, contentMax.y - bodyY);

    // Begin a child region for scrollable content.
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, bodyY));
    ImGui::BeginChild("PageBody", ImVec2(winW, bodyH), false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration);

    // Apply page-body padding.
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::Indent(11.0f);

    // Render the active page
    if (state.activeTab == TAB_AIMING) {
        AimbotPage();
    } else if (state.activeTab == TAB_VISUALS) {
        VisualsPage();
    } else if (state.activeTab == TAB_THEME) {
        ThemePage();
    } else if (state.activeTab == TAB_MISC) {
        MiscPage();
    }

    // Close any remaining open group box
    CloseGroupBox();

    ImGui::Unindent(11.0f);
    ImGui::EndChild();

    dl->AddRect(shellMin, shellMax, IM_COL32(0x02, 0x03, 0x06, 0xFF), 0.0f, 0, 2.0f);
    dl->AddRect(ImVec2(shellMin.x + 2.0f, shellMin.y + 2.0f),
                ImVec2(shellMax.x - 2.0f, shellMax.y - 2.0f),
                IM_COL32(0x39, 0x45, 0x58, 0xB6));
    dl->AddRect(ImVec2(shellMin.x + 3.0f, shellMin.y + 3.0f),
                ImVec2(shellMax.x - 3.0f, shellMax.y - 3.0f),
                IM_COL32(0xff, 0xff, 0xff, 0x08));

    ImGui::SetCursorScreenPos(shellMin);
    ImGui::Dummy(ImVec2(shellWidth, shellHeight));

    ImGui::End();
}
