#include "Renderer/Renderer.hpp"
#include "Renderer/IconManager.hpp"
#include "Utils/Diagnostics.hpp"

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
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Line);
    }

    // ---- DrawRect ----

    void DrawRect(const Vector2& pos, float width, float height, const Color& color, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        const ImVec2 p = ToCanvas(pos);
        d->AddRect(p, ImVec2(p.x + width * ScaleX(), p.y + height * ScaleY()), color.ToImU32(), 0.0f, 0, thickness * ScaleUniform());
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Rect);
    }

    // ---- DrawFilledRect ----

    void DrawFilledRect(const Vector2& pos, float width, float height, const ImColor& color) {
        ImDrawList* d = DL();
        if (!d) return;
        const ImVec2 p = ToCanvas(pos);
        d->AddRectFilled(p, ImVec2(p.x + width * ScaleX(), p.y + height * ScaleY()), color);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
    }

    // ---- DrawCorneredBox ----
    // Only draws the four corners (each corner is lineW / lineH long).

    void DrawCorneredBox(float x, float y, float w, float h, ImU32 color, float thickness, ImU32 outlineColor) {
        ImDrawList* d = DL();
        if (!d) return;

        x *= ScaleX();
        y *= ScaleY();
        w *= ScaleX();
        h *= ScaleY();
        thickness *= ScaleUniform();

        float lineW = w / 3.0f;
        float lineH = h / 3.0f;

        uint64_t lineCount = 8;
        if (((outlineColor >> IM_COL32_A_SHIFT) & 0xFF) != 0) {
            d->AddLine(ImVec2(x, y), ImVec2(x, y + lineH), outlineColor, 3.0f);
            d->AddLine(ImVec2(x, y), ImVec2(x + lineW, y), outlineColor, 3.0f);
            d->AddLine(ImVec2(x + w - lineW, y), ImVec2(x + w, y), outlineColor, 3.0f);
            d->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + lineH), outlineColor, 3.0f);
            d->AddLine(ImVec2(x, y + h - lineH), ImVec2(x, y + h), outlineColor, 3.0f);
            d->AddLine(ImVec2(x, y + h), ImVec2(x + lineW, y + h), outlineColor, 3.0f);
            d->AddLine(ImVec2(x + w - lineW, y + h), ImVec2(x + w, y + h), outlineColor, 3.0f);
            d->AddLine(ImVec2(x + w, y + h - lineH), ImVec2(x + w, y + h), outlineColor, 3.0f);
            lineCount += 8;
        }

        // Colour corners
        d->AddLine(ImVec2(x, y), ImVec2(x, y + lineH), color, thickness);
        d->AddLine(ImVec2(x, y), ImVec2(x + lineW, y), color, thickness);
        d->AddLine(ImVec2(x + w - lineW, y), ImVec2(x + w, y), color, thickness);
        d->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + lineH), color, thickness);
        d->AddLine(ImVec2(x, y + h - lineH), ImVec2(x, y + h), color, thickness);
        d->AddLine(ImVec2(x, y + h), ImVec2(x + lineW, y + h), color, thickness);
        d->AddLine(ImVec2(x + w - lineW, y + h), ImVec2(x + w, y + h), color, thickness);
        d->AddLine(ImVec2(x + w, y + h - lineH), ImVec2(x + w, y + h), color, thickness);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Line, lineCount);
        Diagnostics::RecordRenderBox(false);
    }

    void DrawFastRectBox(float x, float y, float w, float h, ImU32 color, float thickness, ImU32 fillColor) {
        ImDrawList* d = DL();
        if (!d) return;

        x *= ScaleX();
        y *= ScaleY();
        w *= ScaleX();
        h *= ScaleY();
        thickness *= ScaleUniform();

        const ImVec2 boxMin(x, y);
        const ImVec2 boxMax(x + w, y + h);
        if (((fillColor >> IM_COL32_A_SHIFT) & 0xFF) != 0) {
            d->AddRectFilled(boxMin, boxMax, fillColor);
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
        }
        d->AddRect(boxMin, boxMax, color, 0.0f, 0, thickness);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Rect);
        Diagnostics::RecordRenderBox(true);
    }

    // ---- DrawCircle ----

    void DrawCircle(const Vector2& center, float radius, const Color& color, int segments, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddCircle(ToCanvas(center), radius * ScaleUniform(), color.ToImU32(), segments, thickness * ScaleUniform());
    }

    void DrawEllipse(const Vector2& center, const Vector2& radius, const Color& color,
                     float rotation, int segments, float thickness) {
        ImDrawList* d = DL();
        if (!d) return;
        d->AddEllipse(
            ToCanvas(center),
            ImVec2(radius.X * ScaleX(), radius.Y * ScaleY()),
            color.ToImU32(),
            rotation,
            segments,
            thickness * ScaleUniform());
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Line);
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
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Text);
    }

    void DrawText(const ImVec2& pos, ImU32 color, const char* text, float fontSize) {
        ImDrawList* d = DL();
        if (!d || !text) return;

        const ImVec2 p = ToCanvas(pos);
        const float scaledFontSize = fontSize * ScaleUniform();
        d->AddText(nullptr, scaledFontSize, p, color, text);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Text);
    }

    // ---- DrawString ----
    // Plain text without outline.

    void DrawString(const Vector2& pos, const Color& color, const char* text) {
        ImDrawList* d = DL();
        if (!d || !text) return;
        d->AddText(ToCanvas(pos), color.ToImU32(), text);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Text);
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
    // Compact health + shield strip used below the hero avatar.

    void DrawSeerLikeHealth(float x, float y, int shield, int maxShield, int hp, int maxHp) {
        ImDrawList* d = DL();
        if (!d) return;

        auto positive = [](int value) -> float {
            return value > 0 ? static_cast<float>(value) : 0.0f;
        };
        auto clamp01 = [](float value) -> float {
            if (!std::isfinite(value)) return 0.0f;
            if (value < 0.0f) return 0.0f;
            if (value > 1.0f) return 1.0f;
            return value;
        };

        const float healthCurrent = positive(hp);
        const float healthMax = positive(maxHp);
        const float shieldCurrent = positive(shield);
        const float shieldMax = positive(maxShield);
        const float totalMax = healthMax + shieldMax;
        if (totalMax <= 0.0f)
            return;

        constexpr float barWidth = 82.0f;
        constexpr float barHeight = 6.0f;
        const float left = x - barWidth * 0.5f;
        const float top = y;
        const float sx = ScaleX();
        const float sy = ScaleY();
        const float rounding = 2.0f * ScaleUniform();
        const ImVec2 barMin = ToCanvas(ImVec2(left, top));
        const ImVec2 barMax(barMin.x + barWidth * sx, barMin.y + barHeight * sy);

        d->AddRectFilled(ImVec2(barMin.x - 1.0f * sx, barMin.y - 1.0f * sy),
                         ImVec2(barMax.x + 1.0f * sx, barMax.y + 1.0f * sy),
                         IM_COL32(0x00, 0x00, 0x00, 0xB8), rounding);
        d->AddRectFilled(barMin, barMax, IM_COL32(0x13, 0x16, 0x1C, 0xE8), rounding);
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect, 2);

        float cursor = left;
        auto drawSegment = [&](float current, ImU32 color) {
            const float width = barWidth * clamp01(current / totalMax);
            if (width <= 0.0f)
                return;
            const ImVec2 segMin = ToCanvas(ImVec2(cursor, top));
            const ImVec2 segMax(segMin.x + width * sx, segMin.y + barHeight * sy);
            d->AddRectFilled(segMin, segMax, color, rounding);
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);
            cursor += width;
        };

        drawSegment(healthCurrent, IM_COL32(0xF4, 0xF6, 0xF8, 0xF6));
        drawSegment(shieldCurrent, IM_COL32(0x49, 0xB6, 0xFF, 0xF0));

        const int tickCount = static_cast<int>(totalMax / 25.0f);
        const int cappedTicks = tickCount < 12 ? tickCount : 12;
        for (int tick = 1; tick < cappedTicks; ++tick) {
            const float tickX = left + barWidth * clamp01((tick * 25.0f) / totalMax);
            const ImVec2 tickTop = ToCanvas(ImVec2(tickX, top + 1.0f));
            const ImVec2 tickBottom = ToCanvas(ImVec2(tickX, top + barHeight - 1.0f));
            d->AddLine(tickTop, tickBottom, IM_COL32(0x00, 0x00, 0x00, 0x70), 1.0f * ScaleUniform());
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Line);
        }

        d->AddRect(barMin, barMax, IM_COL32(0xFF, 0xFF, 0xFF, 0x28), rounding, 0, 1.0f * ScaleUniform());
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Rect);
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
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);

            // Tag colour bar on the left edge
            d->AddRectFilled(ImVec2(p.x - scaledHalfW, p.y + scaledFontSize * 0.5f),
                             ImVec2(p.x - scaledHalfW + 5.0f * sx, p.y - scaledFontSize * 0.5f),
                             tagColor);
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);

            // Health bar
            float healthWidth = (maxHp > 0.0f) ? (hp / maxHp) * halfW * 2.0f : 0.0f;
            d->AddRectFilled(ImVec2(p.x - scaledHalfW + 7.0f * sx, p.y + scaledFontSize * 0.5f - 6.0f * sy),
                             ImVec2(p.x - scaledHalfW + (7.0f + healthWidth) * sx, p.y + scaledFontSize * 0.5f - 2.0f * sy),
                             healthColor);
            Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::FilledRect);

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
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Icon);
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
        Diagnostics::RecordRenderPrimitive(Diagnostics::RenderPrimitiveKind::Line);
    }

} // namespace Render
