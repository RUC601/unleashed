#include "Features/UI.hpp"
#include "Utils/Config.hpp"
#include "Renderer/Renderer.hpp"
#include "resource.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
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

static constexpr float kShellWidth = 628.0f;
static constexpr float kShellBorder = 2.0f;
static constexpr float kHeaderHeight = 84.0f;
static constexpr float kDefaultLabelWidth = 120.0f;
static constexpr float kAimbotHeroLabelWidth = 98.0f;
static constexpr float kAimbotLeftLabelWidth = 112.0f;
static constexpr float kAimbotRightLabelWidth = 138.0f;

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

    // Draw title text now
    ImVec2 pos = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    DrawText(dl, s_boldFont, ImVec2(pos.x + 14.0f, pos.y),
             IM_COL32(0xf4, 0xf4, 0xf4, 0xFF), title);

    // Reserve vertical room for the title
    ImGui::Dummy(ImVec2(0.0f, textSize.y + 6.0f));

    // Save state for lazy border drawing
    g_gbStartPos = ImGui::GetCursorScreenPos();
    g_gbWidth    = ImGui::GetContentRegionAvail().x;

    // Indent content
    ImGui::Indent(12.0f);
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
    float  titleH   = textSize.y + 6.0f;
    float  borderTopY = g_gbStartPos.y - titleH;

    if (g_gbMinHeight > 0.0f) {
        float desiredContentEndY = borderTopY + g_gbMinHeight - 6.0f;
        float currentY = ImGui::GetCursorScreenPos().y;
        if (currentY < desiredContentEndY)
            ImGui::Dummy(ImVec2(0.0f, desiredContentEndY - currentY));
    }

    ImGui::EndGroup();   // tracks the content bounds
    ImGui::Unindent(12.0f);

    ImVec2 contentEnd = ImGui::GetItemRectMax();
    auto*  dl = ImGui::GetWindowDrawList();
    ImU32  borderCol = IM_COL32(0x0d, 0x0e, 0x11, 0xFF);
    ImU32  bgCol    = ImGui::GetColorU32(ImGuiCol_WindowBg);

    ImVec2 borderMin = g_gbStartPos;
    borderMin.y       = borderTopY;
    ImVec2 borderMax  = ImVec2(g_gbStartPos.x + g_gbWidth, contentEnd.y + 6.0f);
    if (g_gbMinHeight > 0.0f)
        borderMax.y = MaxFloat(borderMax.y, borderMin.y + g_gbMinHeight);

    // Full rect outline
    dl->AddRect(borderMin, borderMax, borderCol);

    // Cut-out behind the legend text so the top border is interrupted
    dl->AddRectFilled(
        ImVec2(borderMin.x + 13.0f, borderMin.y),
        ImVec2(borderMin.x + 13.0f + textSize.x + 10.0f, borderMin.y + textSize.y + 2.0f),
        bgCol
    );

    // Re-draw legend text on top
    DrawText(dl, s_boldFont, ImVec2(borderMin.x + 16.0f, borderMin.y + 1.0f),
             IM_COL32(0xf4, 0xf4, 0xf4, 0xFF), g_gbTitle);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    if (cursor.y < borderMax.y)
        ImGui::Dummy(ImVec2(0.0f, borderMax.y - cursor.y));

    g_gbOpen = false;
    g_gbMinHeight = 0.0f;
}

// =====================================================================
// UICheckbox  --  10x10 coloured square, red (#e51245) when checked,
//                 dark (#1f2428) when unchecked.
// =====================================================================
static bool UICheckbox(const char* label, bool* value) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImGuiContext& g    = *GImGui;
    const ImGuiID id   = window->GetID(label);
    const float sz     = 10.0f;
    const float spacing = g.Style.ItemInnerSpacing.x;

    const char* labelEnd = ImGui::FindRenderedTextEnd(label);
    const bool hasVisibleLabel = labelEnd > label;
    const ImVec2 labelSize = hasVisibleLabel
        ? ImGui::CalcTextSize(label, labelEnd, false)
        : ImVec2(0.0f, 0.0f);
    ImVec2 pos = window->DC.CursorPos;

    ImRect bb(pos, ImVec2(pos.x + sz + (hasVisibleLabel ? spacing + labelSize.x : 0.0f), pos.y + sz));

    ImGui::ItemSize(bb, g.Style.FramePadding.y);
    if (!ImGui::ItemAdd(bb, id)) return false;

    bool hovered = false, held = false;
    bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    if (pressed)
        *value = !*value;

    // Draw the 10x10 solid square
    ImU32 squareCol = *value
        ? IM_COL32(0xe5, 0x12, 0x45, 0xFF)   // checked = red
        : IM_COL32(0x1f, 0x24, 0x28, 0xFF);  // unchecked = dark
    window->DrawList->AddRectFilled(
        ImVec2(bb.Min.x, bb.Min.y),
        ImVec2(bb.Min.x + sz, bb.Min.y + sz),
        squareCol
    );

    if (hasVisibleLabel) {
        ImVec2 labelPos(bb.Min.x + sz + spacing, bb.Min.y);
        window->DrawList->AddText(labelPos, ImGui::GetColorU32(ImGuiCol_Text), label, labelEnd);
    }

    return pressed;
}

// =====================================================================
// UISlider  --  Dark track (#202429) with red fill (#e41143), value text
//               on the right.  Range 0-100, format string for display.
// =====================================================================
static bool UISlider(const char* label, float* value, float v_min, float v_max,
                     const char* formatText) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id   = window->GetID(label);
    const float height = 20.0f;

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

    // Draw track
    ImU32 trackCol = hovered
        ? IM_COL32(0x25, 0x2a, 0x30, 0xFF)
        : IM_COL32(0x20, 0x24, 0x29, 0xFF);
    window->DrawList->AddRectFilled(bb.Min, bb.Max, trackCol);

    // Red fill
    float fillW = v_norm * (bb.Max.x - bb.Min.x);
    if (fillW > 0.0f) {
        window->DrawList->AddRectFilled(
            bb.Min, ImVec2(bb.Min.x + fillW, bb.Max.y),
            IM_COL32(0xe4, 0x11, 0x43, 0xFF));
    }

    // Value text
    char tmp[64];
    const char* displayText = FormatSliderValue(tmp, sizeof(tmp), *value, formatText);
    ImVec2 textSize = ImGui::CalcTextSize(displayText);
    window->DrawList->AddText(
        ImVec2(bb.Max.x - textSize.x - 6.0f, bb.Min.y + (height - textSize.y) * 0.5f),
        IM_COL32(0xf2, 0xf2, 0xf2, 0xFF), displayText);

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
    const float height = 20.0f;
    ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));

    window->DrawList->AddRectFilled(bb.Min, bb.Max, IM_COL32(0x20, 0x24, 0x29, 0xFF));
    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(0x08, 0x09, 0x0b, 0xFF));

    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0xe4, 0x11, 0x43, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0xe4, 0x11, 0x43, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0xe4, 0x11, 0x43, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,       IM_COL32(0x20, 0x24, 0x29, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_Border,        IM_COL32(0x08, 0x09, 0x0b, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,       IM_COL32(0x20, 0x24, 0x29, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0x20, 0x24, 0x29, 0xFF));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0x20, 0x24, 0x29, 0xFF));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(6.0f, MaxFloat(0.0f, (height - ImGui::GetTextLineHeight()) * 0.5f)));

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

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(8);

    window->DrawList->AddRect(bb.Min, bb.Max, IM_COL32(0x08, 0x09, 0x0b, 0xFF));
    ImVec2 caretCenter(bb.Max.x - 8.0f, bb.Min.y + height * 0.5f);
    ImU32 caretCol = IM_COL32(0xd9, 0xd9, 0xd9, 0xFF);
    window->DrawList->AddTriangleFilled(
        ImVec2(caretCenter.x - 3.0f, caretCenter.y - 3.0f),
        ImVec2(caretCenter.x + 3.0f, caretCenter.y - 3.0f),
        ImVec2(caretCenter.x, caretCenter.y - 7.0f),
        caretCol);
    window->DrawList->AddTriangleFilled(
        ImVec2(caretCenter.x - 3.0f, caretCenter.y + 3.0f),
        ImVec2(caretCenter.x + 3.0f, caretCenter.y + 3.0f),
        ImVec2(caretCenter.x, caretCenter.y + 7.0f),
        caretCol);
    return changed;
}

// =====================================================================
// UISegmented  --  Row of buttons, active one gets red #e41143 background.
//                  Returns the new active index (or old if unchanged).
// =====================================================================
static int UISegmented(const char* items[], int itemCount, int active) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();

    float width  = ImGui::GetContentRegionAvail().x;
    float height = (itemCount <= 5) ? 19.0f : 14.0f;

    ImVec2 pos = window->DC.CursorPos;

    // Background bar
    window->DrawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                                     IM_COL32(0x20, 0x24, 0x29, 0xFF));

    float segW = width / itemCount;
    int   result = active;

    for (int i = 0; i < itemCount; i++) {
        ImVec2 segMin(pos.x + i * segW, pos.y);
        ImVec2 segMax(pos.x + (i + 1) * segW, pos.y + height);

        // Active background
        if (i == active)
            window->DrawList->AddRectFilled(segMin, segMax,
                                            IM_COL32(0xe4, 0x11, 0x43, 0xFF));

        // Invisible click zone
        ImGui::SetCursorScreenPos(segMin);
        ImGui::PushID(i);
        ImGui::InvisibleButton("##seg", ImVec2(segW, height));
        if (ImGui::IsItemClicked())
            result = i;
        ImGui::PopID();

        // Label centred
        const char* txt = items[i];
        ImVec2 tsz = ImGui::CalcTextSize(txt);
        ImVec2 txtPos(segMin.x + (segW - tsz.x) * 0.5f,
                      segMin.y + (height - tsz.y) * 0.5f);
        ImU32 txtCol = (i == active)
            ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
            : IM_COL32(0xe5, 0xe5, 0xe5, 0xFF);
        window->DrawList->AddText(txtPos, txtCol, txt);
    }

    // Advance cursor
    ImGui::Dummy(ImVec2(0.0f, height));

    return result;
}

// =====================================================================
// UITwoColumns  --  Two equal-width columns.
// =====================================================================
static void UITwoColumns(std::function<void()> left, std::function<void()> right) {
    ImGui::Columns(2, nullptr, false);
    left();
    ImGui::NextColumn();
    right();
    ImGui::Columns(1);
}

// =====================================================================
// SettingRow  --  Renders a fixed-width label column + the next widget.
// =====================================================================
static void SettingRow(const char* label, float labelWidthPx) {
    float startX = ImGui::GetCursorPosX();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::SetCursorPosX(startX + labelWidthPx);
}

// =====================================================================
// DrawDivider  --  Thin separator line matching React .divider
// =====================================================================
static void DrawDivider() {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        pos, ImVec2(pos.x + width, pos.y), IM_COL32(0x11, 0x13, 0x17, 0xFF));
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
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
    const float rowH = 20.0f;
    const float rowGap = 8.0f;

    for (int row = 0; row < rowCount; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = row * 3 + col;
            if (!labels[idx] || !values[idx])
                continue;

            ImGui::SetCursorScreenPos(ImVec2(colX[col], start.y + row * (rowH + rowGap) + 5.0f));
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
        return 520.0f;
    if (UI::state.activeTab == UI::TAB_VISUALS)
        return 450.0f;
    if (UI::state.activeTab == UI::TAB_MISC)
        return 170.0f;
    return 120.0f;
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
// UI::InitStyle  --  Matches the CSS colour scheme exactly.
// =====================================================================
void UI::InitStyle() {
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigErrorRecoveryEnableTooltip = false;

    if (!s_regularFont && !io.Fonts->Locked) {
        s_regularFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 12.0f);
        s_boldFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeuib.ttf", 12.0f);
        if (s_regularFont)
            io.FontDefault = s_regularFont;
    } else if (s_regularFont) {
        io.FontDefault = s_regularFont;
    }

    ImGuiStyle& style   = ImGui::GetStyle();
    style.WindowRounding    = 0.0f;
    style.FrameRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 0.0f;
    style.TabRounding       = 0.0f;
    style.WindowBorderSize  = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupRounding     = 0.0f;
    style.PopupBorderSize   = 0.0f;
    style.WindowPadding     = ImVec2(0, 0);
    style.FramePadding      = ImVec2(6, 3);
    style.ItemSpacing       = ImVec2(8, 4);
    style.ItemInnerSpacing  = ImVec2(4, 2);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]          = ImVec4(0.110f, 0.114f, 0.133f, 1.0f); // #1c1d22
    colors[ImGuiCol_FrameBg]           = ImVec4(0.125f, 0.142f, 0.161f, 1.0f); // #202429
    colors[ImGuiCol_FrameBgHovered]    = ImVec4(0.145f, 0.160f, 0.180f, 1.0f);
    colors[ImGuiCol_FrameBgActive]     = ImVec4(0.125f, 0.142f, 0.161f, 1.0f);
    colors[ImGuiCol_Button]            = ImVec4(0.125f, 0.142f, 0.161f, 1.0f);
    colors[ImGuiCol_ButtonHovered]     = ImVec4(0.894f, 0.067f, 0.263f, 1.0f); // #e41143
    colors[ImGuiCol_ButtonActive]      = ImVec4(0.894f, 0.067f, 0.263f, 1.0f);
    colors[ImGuiCol_Header]            = ImVec4(0.125f, 0.142f, 0.161f, 1.0f);
    colors[ImGuiCol_HeaderHovered]     = ImVec4(0.894f, 0.067f, 0.263f, 1.0f);
    colors[ImGuiCol_HeaderActive]      = ImVec4(0.894f, 0.067f, 0.263f, 1.0f);
    colors[ImGuiCol_CheckMark]         = ImVec4(0.894f, 0.067f, 0.263f, 1.0f);
    colors[ImGuiCol_SliderGrab]        = ImVec4(0.894f, 0.067f, 0.263f, 1.0f);
    colors[ImGuiCol_SliderGrabActive]  = ImVec4(0.941f, 0.078f, 0.282f, 1.0f);
    colors[ImGuiCol_Text]              = ImVec4(0.945f, 0.945f, 0.945f, 1.0f); // #f1f1f1
    colors[ImGuiCol_Border]            = ImVec4(0.027f, 0.031f, 0.039f, 1.0f); // #07080a
    colors[ImGuiCol_TitleBg]           = ImVec4(0.106f, 0.110f, 0.129f, 1.0f); // #1b1c21
    colors[ImGuiCol_TitleBgActive]     = ImVec4(0.106f, 0.110f, 0.129f, 1.0f);
    colors[ImGuiCol_Separator]         = ImVec4(0.067f, 0.075f, 0.090f, 1.0f); // #111317
    colors[ImGuiCol_ScrollbarBg]       = ImVec4(0.067f, 0.075f, 0.090f, 1.0f);
    colors[ImGuiCol_ScrollbarGrab]     = ImVec4(0.125f, 0.142f, 0.161f, 1.0f);
    colors[ImGuiCol_PopupBg]           = ImVec4(0.125f, 0.142f, 0.161f, 1.0f);
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

    // Full viewport host window; the fixed 628px app shell is drawn manually.
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    ImGui::Begin("Unleashed", nullptr,
                 ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoSavedSettings);

    auto* dl = ImGui::GetWindowDrawList();

    float shellHeight = CurrentShellHeight();
    float shellX = viewport->Pos.x;

    ImVec2 shellMin(shellX, viewport->Pos.y + 1.0f);
    ImVec2 shellMax(shellMin.x + kShellWidth, shellMin.y + shellHeight);
    dl->AddRectFilled(shellMin, shellMax, IM_COL32(0x1c, 0x1d, 0x22, 0xFF));

    ImVec2 winPos(shellMin.x + kShellBorder, shellMin.y + kShellBorder);
    ImVec2 contentMax(shellMax.x - kShellBorder, shellMax.y - kShellBorder);
    float  winW = kShellWidth - kShellBorder * 2.0f;

    // ==================================================================
    // TOP BAR  (84 px height, #1b1c21 background)
    // ==================================================================
    {
        ImRect headerRect(winPos, ImVec2(winPos.x + winW, winPos.y + kHeaderHeight));
        dl->AddRectFilled(headerRect.Min, headerRect.Max, IM_COL32(0x1b, 0x1c, 0x21, 0xFF));

        // Brand: "Unleashed.cc"  (positioned top-left)
        ImVec2 brandPos(winPos.x + 10.0f, winPos.y + 8.0f);
        if (s_logoTexture) {
            dl->AddImage(reinterpret_cast<ImTextureID>(s_logoTexture),
                         brandPos,
                         ImVec2(brandPos.x + 24.0f, brandPos.y + 24.0f));
        }

        DrawText(dl, s_boldFont, ImVec2(brandPos.x + 30.0f, brandPos.y + 3.0f),
                 IM_COL32(0xf7, 0xf7, 0xf7, 0xFF), "Unleashed.cc");

        // Top tab bar at the bottom of the header
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 8.0f, winPos.y + kHeaderHeight - 43.0f));

        const char* topTabNames[] = { "Aiming", "Visuals", "Theme", "Misc" };
        for (int i = 0; i < IM_ARRAYSIZE(topTabNames); i++) {
            bool isActive = (state.activeTab == i);

            ImVec2 tabPos = ImGui::GetCursorScreenPos();
            float tabW = 109.0f;

            // Active tab gets a background
            if (isActive)
                dl->AddRectFilled(tabPos, ImVec2(tabPos.x + tabW, tabPos.y + 43.0f),
                                  IM_COL32(0x1f, 0x22, 0x27, 0xFF));

            // Invisible button for click
            ImGui::PushID(i);
            ImGui::InvisibleButton("##topTab", ImVec2(tabW, 43.0f));
            if (ImGui::IsItemClicked())
                state.activeTab = (Tab)i;
            ImGui::PopID();

            ImU32 txtCol = isActive
                ? IM_COL32(0xf0, 0x14, 0x48, 0xFF)
                : IM_COL32(0xf2, 0xf2, 0xf2, 0xFF);

            ImVec2 txtSize = ImGui::CalcTextSize(topTabNames[i]);
            DrawText(dl, isActive ? s_boldFont : nullptr,
                     ImVec2(tabPos.x + 26.0f, tabPos.y + (43.0f - txtSize.y) * 0.5f),
                     txtCol, topTabNames[i]);

            DrawTopTabIcon(dl, i, ImVec2(tabPos.x + 5.0f, tabPos.y + 12.5f), txtCol);

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

    // Sub-tab bar background (#202326)
    float subBarHeight = 0.0f;
    if (subTabCount > 0) {
        subBarHeight = (state.activeTab == TAB_AIMING) ? 16.0f : 42.0f;
    }

    ImRect subBarRect(ImVec2(winPos.x, contentBandY),
                      ImVec2(winPos.x + winW, contentBandY + subBarHeight));
    dl->AddRectFilled(subBarRect.Min, subBarRect.Max, IM_COL32(0x20, 0x23, 0x26, 0xFF));

    // Draw sub-tab buttons
    if (subTabCount > 0 && activeSub) {
        ImGui::SetCursorScreenPos(ImVec2(winPos.x + 8.0f, contentBandY));
        for (int i = 0; i < subTabCount; i++) {
            bool isActive = (*activeSub == i);
            ImVec2 pos = ImGui::GetCursorScreenPos();

            ImGui::PushID(i + 10);
            ImGui::InvisibleButton("##subTab", ImVec2(60.0f, subBarHeight));
            if (ImGui::IsItemClicked())
                *activeSub = i;
            ImGui::PopID();

            ImU32 col = isActive
                ? IM_COL32(0xff, 0xff, 0xff, 0xFF)
                : IM_COL32(0xe8, 0xe8, 0xe8, 0xFF);
            dl->AddText(ImVec2(pos.x, pos.y + (subBarHeight - ImGui::CalcTextSize(subTabNames[i]).y) * 0.5f),
                        col, subTabNames[i]);

            ImGui::SetCursorScreenPos(ImVec2(pos.x + 60.0f + 20.0f, pos.y));
        }
    }

    // ==================================================================
    // PAGE BODY  (padding matches React page-class variants)
    // ==================================================================
    float bodyTopPad = 8.0f;  // default
    if (state.activeTab == TAB_AIMING)       bodyTopPad = 6.0f;  // page-aimbot
    else if (state.activeTab == TAB_VISUALS) bodyTopPad = 0.0f;  // page-visuals

    float bodyY = contentBandY + subBarHeight + bodyTopPad;
    float bodyH = MaxFloat(0.0f, contentMax.y - bodyY);

    // Begin a child region for scrollable content.
    ImGui::SetCursorScreenPos(ImVec2(winPos.x, bodyY));
    ImGui::BeginChild("PageBody", ImVec2(winW, bodyH), false,
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration);

    // Apply the page-body padding (8px 9px 10px)
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    ImGui::Indent(9.0f);

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

    ImGui::Unindent(9.0f);
    ImGui::EndChild();

    dl->AddRect(shellMin, shellMax, IM_COL32(0x07, 0x08, 0x0a, 0xFF), 0.0f, 0, 2.0f);
    dl->AddRect(ImVec2(shellMin.x + 2.0f, shellMin.y + 2.0f),
                ImVec2(shellMax.x - 2.0f, shellMax.y - 2.0f),
                IM_COL32(0x30, 0x32, 0x3a, 0xFF));

    ImGui::End();
}
