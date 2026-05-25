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
// Overlay -- two-layer DX11 overlay:
// 1) a full-screen black click-through canvas for ESP draw lists
// 2) a separate topmost borderless tool window for the interactive ImGui menu
// -----------------------------------------------------------------------

class Overlay {
public:
    bool Initialize(const wchar_t* overlayTitle);
    void Run(std::function<void()> renderCallback);
    void Shutdown();

    HWND GetOverlayWindow() const { return m_canvasHWnd; }
    HWND GetMenuWindow() const { return m_menuHWnd; }

private:
    HWND                    m_canvasHWnd = nullptr;
    HWND                    m_menuHWnd = nullptr;
    ID3D11Device*           m_pDevice = nullptr;
    ID3D11DeviceContext*    m_pContext = nullptr;
    IDXGISwapChain*         m_canvasSwapChain = nullptr;
    IDXGISwapChain*         m_menuSwapChain = nullptr;
    ID3D11RenderTargetView* m_canvasRenderTargetView = nullptr;
    ID3D11RenderTargetView* m_menuRenderTargetView = nullptr;
    ImGuiContext*           m_canvasContext = nullptr;
    ImGuiContext*           m_menuContext = nullptr;
    UINT                    m_canvasWidth = 0;
    UINT                    m_canvasHeight = 0;
    UINT                    m_menuWidth = 0;
    UINT                    m_menuHeight = 0;

    bool RegisterWindowClasses(HINSTANCE instance);
    bool CreateWindows(const wchar_t* overlayTitle, HINSTANCE instance);
    bool CreateDX11();
    void CleanupDX11();
    bool CreateSwapChainForWindow(HWND hWnd, UINT width, UINT height, IDXGISwapChain** swapChain);
    bool CreateRenderTarget(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView);
    void ReleaseRenderTarget(ID3D11RenderTargetView** renderTargetView);
    bool ResizeSwapChain(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView,
                         UINT width, UINT height);
    bool InitializeImGuiContext(ImGuiContext** context, HWND hWnd);
    void ShutdownImGuiContext(ImGuiContext*& context);
    void UpdateWindowVisibility();
    void UpdateSwapChainSizes();
    void RenderCanvas(std::function<void()> renderCallback);
    void RenderMenu();
};

// Convenience global overlay instance
inline Overlay g_Overlay;
