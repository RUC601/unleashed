#pragma once

#include <d3d11.h>
#include <imgui.h>
#include <string>
#include <vector>
#include <cmath>
#include <cstdint>
#include "Utils/Types.hpp"

class IconManager;

// Import shared math types
using OW::Vector2;
using OW::Vector3;
using OW::Rect;
using OW::Matrix;
// Macros from Types.hpp: M_PI, M_RADPI, M_PI_F, RAD2DEG, DEG2RAD

// =====================================================================
// Render Namespace - Drawing Primitives
// =====================================================================

namespace Render {

    struct Color {
        int R, G, B, A;

        Color() : R(0), G(0), B(0), A(255) {}
        Color(int r, int g, int b, int a = 255) : R(r), G(g), B(b), A(a) {}

        // Convert RGBA byte values to ARGB packed DWORD (alpha override)
        unsigned int RGBA2ARGB(int alpha) const {
            return ((alpha & 0xFF) << 24) | ((B & 0xFF)) | ((G & 0xFF) << 8) | ((R & 0xFF) << 16);
        }

        ImU32 ToImU32() const {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(R / 255.0f, G / 255.0f, B / 255.0f, A / 255.0f));
        }

        ImColor ToImColor() const {
            return ImColor(R, G, B, A);
        }
    };

    // Store the D3D11 device and context for later use
    bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    bool InitIcons(ID3D11Device* device);

    void ShutdownIcons();

    IconManager* GetIconManager();

    // ---- Drawing Primitives ----
    // All drawing goes through ImGui foreground draw lists.

    void DrawLine(const Vector2& from, const Vector2& to, const Color& color, float thickness = 1.0f);

    void DrawRect(const Vector2& pos, float width, float height, const Color& color, float thickness = 1.0f);

    void DrawFilledRect(const Vector2& pos, float width, float height, const ImColor& color);

    void DrawCorneredBox(float x, float y, float w, float h, ImU32 color, float thickness = 1.0f);

    void DrawCircle(const Vector2& center, float radius, const Color& color, int segments = 32, float thickness = 1.0f);

    void DrawFilledCircle(const Vector2& center, float radius, const Color& color, int segments = 32);

    void DrawStrokeText(const ImVec2& pos, ImU32 color, const char* text, float fontSize = 15.0f);

    void DrawString(const Vector2& pos, const Color& color, const char* text);

    void DrawHealthBar(const Vector2& screenPos, float height, float health, float maxHealth);

    void DrawSeerLikeHealth(float x, float y, int ult, int maxUlt, int hp, int maxHp);

    void DrawInfo(const ImVec2& pos, ImU32 color, float fontSize, const char* text, float dist, float hp, float maxHp);

    void DrawSKILL(const ImVec2& pos, const std::string& text);

    void DrawIcon(ID3D11ShaderResourceView* texture, const ImVec2& pos, const ImVec2& size, ImU32 tint = IM_COL32_WHITE);

    void RenderLine(const Vector2& from, const Vector2& to, unsigned int color, float thickness = 1.0f);

} // namespace Render
