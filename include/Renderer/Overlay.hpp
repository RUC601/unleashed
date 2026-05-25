#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <functional>

#include "Renderer.hpp"

// -----------------------------------------------------------------------
// Overlay -- DX11-based resizable window for DMA secondary display.
// All rendering goes through ImGui draw lists.
// -----------------------------------------------------------------------

class Overlay {
public:
    bool Initialize(const wchar_t* overlayTitle);
    void Run(std::function<void()> renderCallback);
    void Shutdown();

    HWND GetOverlayWindow() const { return m_hWnd; }

private:
    HWND                    m_hWnd = nullptr;
    ID3D11Device*           m_pDevice = nullptr;
    ID3D11DeviceContext*    m_pContext = nullptr;
    IDXGISwapChain*         m_pSwapChain = nullptr;
    ID3D11RenderTargetView* m_pRenderTargetView = nullptr;
    UINT                    m_clientWidth = 0;
    UINT                    m_clientHeight = 0;

    bool CreateDX11();
    void CleanupDX11();
    void ResizeBuffers();
    bool ResizeSwapChain(UINT width, UINT height);
};

// Convenience global overlay instance
inline Overlay g_Overlay;
