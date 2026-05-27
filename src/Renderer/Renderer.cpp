#include "Renderer/Renderer.hpp"
#include "Renderer/IconManager.hpp"

#include <memory>

namespace OW {
    extern float WX;
    extern float WY;
}

// =====================================================================
// Render namespace implementation
// =====================================================================

namespace Render {

    // ---- Internal state ----

    static ID3D11Device*        g_Device  = nullptr;
    static ID3D11DeviceContext* g_Context = nullptr;
    static std::unique_ptr<IconManager> g_IconManager;

    // ---- Initialisation ----

    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
        if (!device || !context)
            return false;
        g_Device  = device;
        g_Context = context;
        return true;
    }

    bool InitIcons(ID3D11Device* device) {
        if (!device)
            return false;

        g_IconManager = std::make_unique<IconManager>(device);
        return g_IconManager->LoadAll();
    }

    void ShutdownIcons() {
        g_IconManager.reset();
    }

    IconManager* GetIconManager() {
        return g_IconManager.get();
    }

    // ---- Drawing helpers ----

    static ImDrawList* DL() {
        return ImGui::GetForegroundDrawList();
    }

    static ImVec2 CanvasSize() {
        const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
        if (displaySize.x > 0.0f && displaySize.y > 0.0f)
            return displaySize;
        return ImVec2(OW::WX > 0.0f ? OW::WX : 1.0f, OW::WY > 0.0f ? OW::WY : 1.0f);
    }

    static float SourceWidth() {
        return OW::WX > 0.0f ? OW::WX : CanvasSize().x;
    }

    static float SourceHeight() {
        return OW::WY > 0.0f ? OW::WY : CanvasSize().y;
    }

    static float ScaleX() {
        return CanvasSize().x / SourceWidth();
    }

    static float ScaleY() {
        return CanvasSize().y / SourceHeight();
    }

    static float ScaleUniform() {
        const float sx = ScaleX();
        const float sy = ScaleY();
        return (sx + sy) * 0.5f;
    }

    static ImVec2 ToCanvas(const Vector2& point) {
        return ImVec2(point.X * ScaleX(), point.Y * ScaleY());
    }

    static ImVec2 ToCanvas(const ImVec2& point) {
        return ImVec2(point.x * ScaleX(), point.y * ScaleY());
    }

    // ---- DrawLine ----

    void DrawLine(const Vector2& from, const Vector2& to, const Color& color, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddLine(ToCanvas(from), ToCanvas(to), color.ToImU32(), thickness * ScaleUniform());
    }

    // ---- DrawRect ----

    void DrawRect(const Vector2& pos, float width, float height, const Color& color, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        const ImVec2 p = ToCanvas(pos);
        d->AddRect(p, ImVec2(p.x + width * ScaleX(), p.y + height * ScaleY()), color.ToImU32(), 0.0f, 0, thickness * ScaleUniform());
    }

    // ---- DrawFilledRect ----

    void DrawFilledRect(const Vector2& pos, float width, float height, const ImColor& color) {
        ImDrawList* d = DL();
        if (!d) return;
        const ImVec2 p = ToCanvas(pos);
        d->AddRectFilled(p, ImVec2(p.x + width * ScaleX(), p.y + height * ScaleY()), color);
    }

    // ---- DrawCorneredBox ----
    // Only draws the four corners (each corner is lineW / lineH long).

    void DrawCorneredBox(float x, float y, float w, float h, ImU32 color, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;

        x *= ScaleX();
        y *= ScaleY();
        w *= ScaleX();
        h *= ScaleY();
        thickness *= ScaleUniform();

        float lineW = w / 3.0f;
        float lineH = h / 3.0f;

        // Black outline (3px) for visibility against any background
        const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        ImU32 black = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, alpha));
        d->AddLine(ImVec2(x, y), ImVec2(x, y + lineH), black, 3.0f);
        d->AddLine(ImVec2(x, y), ImVec2(x + lineW, y), black, 3.0f);
        d->AddLine(ImVec2(x + w - lineW, y), ImVec2(x + w, y), black, 3.0f);
        d->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + lineH), black, 3.0f);
        d->AddLine(ImVec2(x, y + h - lineH), ImVec2(x, y + h), black, 3.0f);
        d->AddLine(ImVec2(x, y + h), ImVec2(x + lineW, y + h), black, 3.0f);
        d->AddLine(ImVec2(x + w - lineW, y + h), ImVec2(x + w, y + h), black, 3.0f);
        d->AddLine(ImVec2(x + w, y + h - lineH), ImVec2(x + w, y + h), black, 3.0f);

        // Colour corners
        d->AddLine(ImVec2(x, y), ImVec2(x, y + lineH), color, thickness);
        d->AddLine(ImVec2(x, y), ImVec2(x + lineW, y), color, thickness);
        d->AddLine(ImVec2(x + w - lineW, y), ImVec2(x + w, y), color, thickness);
        d->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + lineH), color, thickness);
        d->AddLine(ImVec2(x, y + h - lineH), ImVec2(x, y + h), color, thickness);
        d->AddLine(ImVec2(x, y + h), ImVec2(x + lineW, y + h), color, thickness);
        d->AddLine(ImVec2(x + w - lineW, y + h), ImVec2(x + w, y + h), color, thickness);
        d->AddLine(ImVec2(x + w, y + h - lineH), ImVec2(x + w, y + h), color, thickness);
    }

    // ---- DrawCircle ----

    void DrawCircle(const Vector2& center, float radius, const Color& color, int segments, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddCircle(ToCanvas(center), radius * ScaleUniform(), color.ToImU32(), segments, thickness * ScaleUniform());
    }

    // ---- DrawFilledCircle ----

    void DrawFilledCircle(const Vector2& center, float radius, const Color& color, int segments) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddCircleFilled(ToCanvas(center), radius * ScaleUniform(), color.ToImU32(), segments);
    }

    // ---- DrawStrokeText ----
    // Legacy name kept for callers; draws crisp text without multipass outline.

    void DrawStrokeText(const ImVec2& pos, ImU32 color, const char* text, float fontSize) {
        ImDrawList* d = DL();
        if (!d || !text) return;

        const ImVec2 p = ToCanvas(pos);
        const float scaledFontSize = fontSize * ScaleUniform();
        d->AddText(nullptr, scaledFontSize, p, color, text);
    }

    void DrawText(const ImVec2& pos, ImU32 color, const char* text, float fontSize) {
        ImDrawList* d = DL();
        if (!d || !text) return;

        const ImVec2 p = ToCanvas(pos);
        const float scaledFontSize = fontSize * ScaleUniform();
        d->AddText(nullptr, scaledFontSize, p, color, text);
    }

    // ---- DrawString ----
    // Plain text without outline.

    void DrawString(const Vector2& pos, const Color& color, const char* text) {
        ImDrawList* d = DL();
        if (!d || !text) return;
        d->AddText(ToCanvas(pos), color.ToImU32(), text);
    }

    // ---- DrawHealthBar ----

    void DrawHealthBar(const Vector2& screenPos, float height, float health, float maxHealth, float opacity) {
        if (maxHealth <= 0.0f) return;

        float barWidth  = 3.0f;
        float barHeight = (health / maxHealth) * height;
        if (barHeight < 0.0f) barHeight = 0.0f;
        if (barHeight > height) barHeight = height;

        int alpha = static_cast<int>(opacity * 255.0f + 0.5f);
        if (alpha < 0) alpha = 0;
        if (alpha > 255) alpha = 255;

        // Background outline (black)
        DrawRect(screenPos, barWidth, height, Color(0, 0, 0, alpha), 1.0f);

        // Choose colour based on health percentage
        float ratio = health / maxHealth;
        Color fillColor;
        if      (ratio >= 0.8f) fillColor = Color(10, 255, 10, alpha);   // green
        else if (ratio >= 0.6f) fillColor = Color(255, 255, 10, alpha);  // yellow
        else if (ratio >= 0.4f) fillColor = Color(255, 150, 10, alpha);  // orange
        else if (ratio >  0.0f) fillColor = Color(255, 50, 10, alpha);   // red
        else                    fillColor = Color(0, 0, 0, alpha);       // black

        DrawFilledRect(screenPos, barWidth, barHeight, fillColor.ToImColor());
    }

    // ---- DrawSeerLikeHealth ----
    // Stylised health + shield bar inspired by Apex Legend's Seer tactical.

    static void DrawQuadFilled(ImVec2 p1, ImVec2 p2, ImVec2 p3, ImVec2 p4, ImColor color) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddQuadFilled(ToCanvas(p1), ToCanvas(p2), ToCanvas(p3), ToCanvas(p4), color);
    }

    static void DrawHexagonFilled(const ImVec2& p1, const ImVec2& p2, const ImVec2& p3,
                                   const ImVec2& p4, const ImVec2& p5, const ImVec2& p6, ImColor col) {
        ImDrawList* d = DL();
        if (!d) return;
        ImVec2 pts[6] = {
            ToCanvas(p1), ToCanvas(p2), ToCanvas(p3),
            ToCanvas(p4), ToCanvas(p5), ToCanvas(p6)
        };
    d->AddConvexPolyFilled(pts, 6, col);
    }

    void DrawSeerLikeHealth(float x, float y, int shield, int maxShield, int hp, int maxHp) {
        // Dimensions and layout derived from original implementation
        const int   bgOffset    = 3;
        const int   barWidth    = 158;
        const float shieldStep  = 25.0f;
        const int   shield25    = 30;
        const int   steps       = 5;

        int armorType = 1;
        if      (maxShield == 50)  armorType = 1;
        else if (maxShield == 75)  armorType = 2;
        else if (maxShield == 100) armorType = 3;
        else if (maxShield == 125) armorType = 5;

        // Background hexagon
        ImVec2 bg1(x - barWidth / 2 - bgOffset, y);
        ImVec2 bg2(bg1.x - 10, bg1.y - 16);
        ImVec2 bg3(bg2.x + 5, bg2.y - 7);
        ImVec2 bg4(bg3.x + barWidth + bgOffset, bg3.y);
        ImVec2 bg5(bg4.x + 11, bg4.y + 18);
        ImVec2 bg6(x + barWidth / 2 + bgOffset, y);
        DrawHexagonFilled(bg1, bg2, bg3, bg4, bg5, bg6, ImColor(0, 0, 0, 120));

        // Health bar (white fill)
        ImVec2 h1(bg1.x + 3, bg1.y - 4);
        ImVec2 h2(h1.x - 5, h1.y - 8);
        float healthRatio = (maxHp > 0) ? (float)hp / maxHp : 0.0f;
        ImVec2 h3(h2.x + healthRatio * barWidth, h2.y);
        ImVec2 h4(h1.x + healthRatio * barWidth, h1.y);
        ImVec2 h3m(h2.x + barWidth, h2.y);
        ImVec2 h4m(h1.x + barWidth, h1.y);
        DrawQuadFilled(h1, h2, h3m, h4m, ImColor(10, 10, 30, 60));
        DrawQuadFilled(h1, h2, h3, h4, ImColor(255, 255, 255));

        // Shield colours
        ImColor shieldCracked(97, 97, 97);
        ImColor shieldCol;
        switch (armorType) {
            case 2: shieldCol = ImColor(39, 178, 255);  break;  // blue
            case 3: shieldCol = ImColor(206, 59, 255);  break;  // purple
            case 4: shieldCol = ImColor(255, 255, 79);  break;  // gold
            case 5: shieldCol = ImColor(219, 2, 2);     break;  // red
            default:shieldCol = ImColor(247, 247, 247);  break;  // white
        }

        // Break shield into 25 HP segments
        int segs[5] = { 0, 0, 0, 0, 0 };
        int remaining = shield;
        for (int i = 0; i < 5 && remaining > 0; ++i) {
            segs[i] = (remaining > 25) ? 25 : remaining;
            remaining -= segs[i];
        }

        // Helper lambda to draw a shield segment
        auto drawSegment = [&](int idx, bool filled) {
            if (idx < 0 || idx > 4) return;
            // Predefined vertex offsets per segment
            struct SegVerts { float h2x, h2y, s1x, s1y; };
            static const SegVerts verts[5] = {
                { 0.0f, 0.0f, 0.0f, 0.0f },
                { 32.0f, 0.0f, 2.0f, 0.0f },
                { 64.0f, 0.0f, 2.0f, 0.0f },
                { 96.0f, 0.0f, 2.0f, 0.0f },
                { 128.0f, 0.0f, 2.0f, 0.0f },
            };

            float baseX = h2.x + verts[idx].h2x + (idx > 0 ? verts[idx].s1x : 0.0f);
            float baseY = h2.y + verts[idx].h2y;

            ImVec2 s1(baseX - 1.0f, baseY - 2.0f);
            ImVec2 s2(s1.x - 3.0f, s1.y - 5.0f);
            float sw = (segs[idx] / shieldStep) * shield25;
            ImVec2 s3(s2.x + sw, s2.y);
            ImVec2 s4(s1.x + sw, s1.y);
            ImVec2 s3m(s2.x + shield25, s2.y);
            ImVec2 s4m(s1.x + shield25, s1.y);

            if (filled) {
                if (segs[idx] > 0)
                    DrawQuadFilled(s1, s2, s3, s4, shieldCol);
            } else {
                if (segs[idx] < 25)
                    DrawQuadFilled(s1, s2, s3m, s4m, shieldCracked);
            }
        };

        // Draw all 5 segments
        for (int i = 0; i < 5; ++i) {
            drawSegment(i, true);   // filled portion
            drawSegment(i, false);  // cracked portion
        }
    }

    // ---- DrawInfo ----
    // Panel with tag colour, health bar, and text label.

    void DrawInfo(const ImVec2& pos, ImU32 tagColor, float fontSize, const char* text, float dist, float hp, float maxHp) {
        ImDrawList* d = DL();
        if (!d || !text) return;

        ImVec2 textSize = ImGui::CalcTextSize(text);
        float halfW = textSize.x * 0.5f;
        const ImVec2 p = ToCanvas(pos);
        const float sx = ScaleX();
        const float sy = ScaleY();
        const float scaledFontSize = fontSize * ScaleUniform();
        const float scaledHalfW = halfW * sx;
        const float alpha = static_cast<float>((tagColor >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
        const ImU32 panelColor = ImGui::GetColorU32(ImVec4(0.2f, 0.2f, 0.6f, 0.3f * alpha));
        const ImU32 textColor = ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.6f, alpha));
        const ImU32 healthColor = ImGui::GetColorU32(ImVec4(0.0f, 1.0f, 0.0f, alpha));

        if (dist < 200.0f) {
            // Background panel
            d->AddRectFilled(ImVec2(p.x - scaledHalfW, p.y + scaledFontSize * 0.5f),
                             ImVec2(p.x + scaledHalfW + 35.0f * sx, p.y - scaledFontSize * 0.5f),
                             panelColor);

            // Tag colour bar on the left edge
            d->AddRectFilled(ImVec2(p.x - scaledHalfW, p.y + scaledFontSize * 0.5f),
                             ImVec2(p.x - scaledHalfW + 5.0f * sx, p.y - scaledFontSize * 0.5f),
                             tagColor);

            // Health bar
            float healthWidth = (maxHp > 0.0f) ? (hp / maxHp) * halfW * 2.0f : 0.0f;
            d->AddRectFilled(ImVec2(p.x - scaledHalfW + 7.0f * sx, p.y + scaledFontSize * 0.5f - 6.0f * sy),
                             ImVec2(p.x - scaledHalfW + (7.0f + healthWidth) * sx, p.y + scaledFontSize * 0.5f - 2.0f * sy),
                             healthColor);

            DrawStrokeText(ImVec2(pos.x - halfW + 10.0f, pos.y - fontSize * 0.5f),
                           textColor, text, fontSize);
        } else {
            DrawStrokeText(ImVec2(pos.x - halfW + 10.0f, pos.y - fontSize * 0.5f),
                           textColor, text, fontSize);
        }
    }

    // ---- DrawSKILL ----
    // Renders skill text at the given position with a stroke.

    void DrawSKILL(const ImVec2& pos, const std::string& text) {
        DrawStrokeText(pos, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), text.c_str(), 19.0f);
    }

    void DrawIcon(ID3D11ShaderResourceView* texture, const ImVec2& pos, const ImVec2& size, ImU32 tint) {
        ImDrawList* d = DL();
        if (!d || !texture)
            return;

        d->AddImage(
            reinterpret_cast<ImTextureID>(texture),
            ToCanvas(pos),
            ToCanvas(ImVec2(pos.x + size.x, pos.y + size.y)),
            ImVec2(0.0f, 0.0f),
            ImVec2(1.0f, 1.0f),
            tint
        );
    }

    // ---- RenderLine ----
    // Low-level line using packed ARGB colour.

    void RenderLine(const Vector2& from, const Vector2& to, unsigned int color, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;

        unsigned int a = (color >> 24) & 0xFF;
        unsigned int r = (color >> 16) & 0xFF;
        unsigned int g = (color >> 8) & 0xFF;
        unsigned int b = (color) & 0xFF;

        d->AddLine(ToCanvas(from), ToCanvas(to),
                   ImGui::GetColorU32(ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f)),
                   thickness * ScaleUniform());
    }

} // namespace Render
