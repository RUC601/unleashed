#include "Renderer/Overlay.hpp"
#include "Features/UI.hpp"
#include "Utils/Config.hpp"
#include "resource.h"

#include <windowsx.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

constexpr int kDefaultMenuClientWidth = 650;
constexpr int kDefaultMenuClientHeight = 550;
constexpr int kMenuResizeGrip = 8;
constexpr int kMenuDragHeight = 38;
constexpr wchar_t kCanvasClassName[] = L"UnleashedOverlayCanvasDX11";
constexpr wchar_t kMenuClassName[] = L"UnleashedOverlayMenuDX11";

ImGuiContext* g_menuContext = nullptr;

void ApplyMinimumMenuTrackSize(LPARAM lParam) {
    MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
    if (!minMax)
        return;

    minMax->ptMinTrackSize.x = kDefaultMenuClientWidth;
    minMax->ptMinTrackSize.y = kDefaultMenuClientHeight;
}

LRESULT HitTestBorderlessMenu(HWND hWnd, LPARAM lParam) {
    POINT cursor = {
        GET_X_LPARAM(lParam),
        GET_Y_LPARAM(lParam)
    };
    ScreenToClient(hWnd, &cursor);

    RECT rect = {};
    GetClientRect(hWnd, &rect);

    const bool left = cursor.x < kMenuResizeGrip;
    const bool right = cursor.x >= (rect.right - kMenuResizeGrip);
    const bool top = cursor.y < kMenuResizeGrip;
    const bool bottom = cursor.y >= (rect.bottom - kMenuResizeGrip);

    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;

    if (cursor.y < kMenuDragHeight && cursor.x < rect.right - 72)
        return HTCAPTION;

    return HTCLIENT;
}

bool IsMenuTogglePressed() {
    const int key = OW::Config::MenuToggleKey;
    return key > 0 && (GetAsyncKeyState(key) & 0x0001) != 0;
}

void DrawCanvasBackdrop(UINT width, UINT height) {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
        return;

    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float w = displaySize.x > 0.0f ? displaySize.x : static_cast<float>(width);
    const float h = displaySize.y > 0.0f ? displaySize.y : static_cast<float>(height);
    if (w <= 0.0f || h <= 0.0f)
        return;

    // The layered window uses RGB(0,0,0) as a transparency key. RGB(1,1,1)
    // is visually black, but remains a real bottom-layer primitive.
    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(w, h), IM_COL32(1, 1, 1, 255));
}

void ConfigureCanvasFont(ImGuiIO& io) {
    if (io.Fonts->Locked)
        return;

    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 1;
    fontConfig.PixelSnapH = true;
    fontConfig.RasterizerMultiply = 1.0f;

    ImFont* font = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consola.ttf", 13.0f, &fontConfig);
    if (!font)
        font = io.Fonts->AddFontDefault();
    if (font)
        io.FontDefault = font;
}

DXGI_SWAP_CHAIN_DESC MakeSwapChainDesc(HWND hWnd, UINT width, UINT height) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = width;
    desc.BufferDesc.Height = height;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hWnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    return desc;
}

bool RegisterClassIfNeeded(const WNDCLASSEXW& windowClass) {
    if (RegisterClassExW(&windowClass) != 0)
        return true;

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

LRESULT WINAPI CanvasWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT WINAPI MenuWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCHITTEST)
        return HitTestBorderlessMenu(hWnd, lParam);
    if (msg == WM_NCCALCSIZE && wParam)
        return 0;

    ImGuiContext* previousContext = ImGui::GetCurrentContext();
    if (g_menuContext) {
        ImGui::SetCurrentContext(g_menuContext);
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
            ImGui::SetCurrentContext(previousContext);
            return true;
        }
        ImGui::SetCurrentContext(previousContext);
    }

    switch (msg) {
        case WM_GETMINMAXINFO:
            ApplyMinimumMenuTrackSize(lParam);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_SYSCOMMAND:
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;
            break;
        case WM_CLOSE:
            OW::Config::Menu = false;
            ShowWindow(hWnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace

bool Overlay::Initialize(const wchar_t* overlayTitle) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!RegisterWindowClasses(instance))
        return false;

    if (!CreateWindows(overlayTitle, instance))
        return false;

    if (!CreateDX11())
        return false;

    IMGUI_CHECKVERSION();
    if (!InitializeImGuiContext(&m_canvasContext, m_canvasHWnd))
        return false;
    if (!InitializeImGuiContext(&m_menuContext, m_menuHWnd))
        return false;

    g_menuContext = m_menuContext;

    Render::Initialize(m_pDevice, m_pContext);
    Render::InitIcons(m_pDevice);
    UI::InitializeResources(m_pDevice);

    UpdateWindowVisibility();
    return true;
}

void Overlay::Run(std::function<void()> renderCallback) {
    if (!m_canvasHWnd || !m_canvasSwapChain || !m_canvasRenderTargetView)
        return;

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (!running)
            break;

        if (!IsWindow(m_canvasHWnd))
            break;

        if (IsMenuTogglePressed())
            OW::Config::Menu = !OW::Config::Menu;

        UpdateWindowVisibility();
        UpdateSwapChainSizes();

        RenderCanvas(renderCallback);
        if (OW::Config::Menu)
            RenderMenu();
    }

    Shutdown();
}

void Overlay::Shutdown() {
    UI::ShutdownResources();
    Render::ShutdownIcons();

    g_menuContext = nullptr;
    ShutdownImGuiContext(m_menuContext);
    ShutdownImGuiContext(m_canvasContext);

    CleanupDX11();

    if (m_menuHWnd) {
        DestroyWindow(m_menuHWnd);
        m_menuHWnd = nullptr;
    }
    if (m_canvasHWnd) {
        DestroyWindow(m_canvasHWnd);
        m_canvasHWnd = nullptr;
    }

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    UnregisterClassW(kMenuClassName, instance);
    UnregisterClassW(kCanvasClassName, instance);
}

bool Overlay::RegisterWindowClasses(HINSTANCE instance) {
    WNDCLASSEXW canvasClass = {};
    canvasClass.cbSize = sizeof(WNDCLASSEXW);
    canvasClass.style = CS_HREDRAW | CS_VREDRAW;
    canvasClass.lpfnWndProc = CanvasWndProc;
    canvasClass.hInstance = instance;
    canvasClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    canvasClass.hbrBackground = nullptr;
    canvasClass.lpszClassName = kCanvasClassName;
    canvasClass.hIcon = LoadIcon(instance, MAKEINTRESOURCE(IDI_UNLEASHED));
    canvasClass.hIconSm = LoadIcon(instance, MAKEINTRESOURCE(IDI_UNLEASHED));

    WNDCLASSEXW menuClass = canvasClass;
    menuClass.lpfnWndProc = MenuWndProc;
    menuClass.lpszClassName = kMenuClassName;

    return RegisterClassIfNeeded(canvasClass) && RegisterClassIfNeeded(menuClass);
}

bool Overlay::CreateWindows(const wchar_t* overlayTitle, HINSTANCE instance) {
    m_canvasWidth = static_cast<UINT>(GetSystemMetrics(SM_CXSCREEN));
    m_canvasHeight = static_cast<UINT>(GetSystemMetrics(SM_CYSCREEN));
    if (m_canvasWidth == 0 || m_canvasHeight == 0) {
        m_canvasWidth = static_cast<UINT>(OW::Config::manualScreenWidth > 0 ? OW::Config::manualScreenWidth : 1);
        m_canvasHeight = static_cast<UINT>(OW::Config::manualScreenHeight > 0 ? OW::Config::manualScreenHeight : 1);
    }

    m_canvasHWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        kCanvasClassName,
        overlayTitle,
        WS_POPUP | WS_VISIBLE,
        0,
        0,
        static_cast<int>(m_canvasWidth),
        static_cast<int>(m_canvasHeight),
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (!m_canvasHWnd)
        return false;

    // The canvas is cleared to black every frame; color-key that clear color so
    // empty overlay pixels do not cover the target display.
    SetLayeredWindowAttributes(m_canvasHWnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    ShowWindow(m_canvasHWnd, SW_SHOW);
    SetWindowPos(m_canvasHWnd, HWND_TOPMOST, 0, 0,
                 static_cast<int>(m_canvasWidth), static_cast<int>(m_canvasHeight),
                 SWP_SHOWWINDOW);

    const DWORD menuStyle = WS_POPUP | WS_THICKFRAME;
    const DWORD menuExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    const int menuW = kDefaultMenuClientWidth;
    const int menuH = kDefaultMenuClientHeight;

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workW = workArea.right - workArea.left;
    const int workH = workArea.bottom - workArea.top;
    const int menuX = workArea.left + (workW - menuW) / 2;
    const int menuY = workArea.top + (workH - menuH) / 2;

    m_menuHWnd = CreateWindowExW(
        menuExStyle,
        kMenuClassName,
        L"Unleashed Settings",
        menuStyle,
        menuX,
        menuY,
        menuW,
        menuH,
        nullptr,
        nullptr,
        instance,
        nullptr
    );
    if (!m_menuHWnd)
        return false;

    RECT clientRect = {};
    if (GetClientRect(m_menuHWnd, &clientRect)) {
        m_menuWidth = static_cast<UINT>(clientRect.right - clientRect.left);
        m_menuHeight = static_cast<UINT>(clientRect.bottom - clientRect.top);
    } else {
        m_menuWidth = kDefaultMenuClientWidth;
        m_menuHeight = kDefaultMenuClientHeight;
    }

    SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

bool Overlay::CreateDX11() {
    DXGI_SWAP_CHAIN_DESC canvasDesc = MakeSwapChainDesc(m_canvasHWnd, m_canvasWidth, m_canvasHeight);

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL featureLevelArray[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        ARRAYSIZE(featureLevelArray),
        D3D11_SDK_VERSION,
        &canvasDesc,
        &m_canvasSwapChain,
        &m_pDevice,
        &featureLevel,
        &m_pContext
    );
    if (FAILED(hr))
        return false;

    if (!CreateRenderTarget(m_canvasSwapChain, &m_canvasRenderTargetView))
        return false;

    if (!CreateSwapChainForWindow(m_menuHWnd, m_menuWidth, m_menuHeight, &m_menuSwapChain))
        return false;

    return CreateRenderTarget(m_menuSwapChain, &m_menuRenderTargetView);
}

void Overlay::CleanupDX11() {
    ReleaseRenderTarget(&m_menuRenderTargetView);
    ReleaseRenderTarget(&m_canvasRenderTargetView);

    if (m_menuSwapChain) {
        m_menuSwapChain->Release();
        m_menuSwapChain = nullptr;
    }
    if (m_canvasSwapChain) {
        m_canvasSwapChain->Release();
        m_canvasSwapChain = nullptr;
    }
    if (m_pContext) {
        m_pContext->Release();
        m_pContext = nullptr;
    }
    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
}

bool Overlay::CreateSwapChainForWindow(HWND hWnd, UINT width, UINT height, IDXGISwapChain** swapChain) {
    if (!m_pDevice || !hWnd || !swapChain || width == 0 || height == 0)
        return false;

    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;

    HRESULT hr = m_pDevice->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (SUCCEEDED(hr))
        hr = dxgiDevice->GetParent(IID_PPV_ARGS(&adapter));
    if (SUCCEEDED(hr))
        hr = adapter->GetParent(IID_PPV_ARGS(&factory));

    if (FAILED(hr) || !factory) {
        if (factory)
            factory->Release();
        if (adapter)
            adapter->Release();
        if (dxgiDevice)
            dxgiDevice->Release();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc = MakeSwapChainDesc(hWnd, width, height);
    hr = factory->CreateSwapChain(m_pDevice, &desc, swapChain);

    factory->Release();
    adapter->Release();
    dxgiDevice->Release();

    return SUCCEEDED(hr) && *swapChain;
}

bool Overlay::CreateRenderTarget(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView) {
    if (!m_pDevice || !swapChain || !renderTargetView)
        return false;

    ReleaseRenderTarget(renderTargetView);

    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr) || !backBuffer)
        return false;

    hr = m_pDevice->CreateRenderTargetView(backBuffer, nullptr, renderTargetView);
    backBuffer->Release();

    return SUCCEEDED(hr) && *renderTargetView;
}

void Overlay::ReleaseRenderTarget(ID3D11RenderTargetView** renderTargetView) {
    if (renderTargetView && *renderTargetView) {
        (*renderTargetView)->Release();
        *renderTargetView = nullptr;
    }
}

bool Overlay::ResizeSwapChain(IDXGISwapChain* swapChain, ID3D11RenderTargetView** renderTargetView,
                              UINT width, UINT height) {
    if (!swapChain || !renderTargetView || width == 0 || height == 0)
        return false;

    if (m_pContext)
        m_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    ReleaseRenderTarget(renderTargetView);

    HRESULT hr = swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr))
        return false;

    return CreateRenderTarget(swapChain, renderTargetView);
}

bool Overlay::InitializeImGuiContext(ImGuiContext** context, HWND hWnd) {
    if (!context || !hWnd || !m_pDevice || !m_pContext)
        return false;

    *context = ImGui::CreateContext();
    ImGui::SetCurrentContext(*context);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    if (hWnd == m_canvasHWnd)
        ConfigureCanvasFont(io);

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hWnd))
        return false;
    if (!ImGui_ImplDX11_Init(m_pDevice, m_pContext))
        return false;

    return true;
}

void Overlay::ShutdownImGuiContext(ImGuiContext*& context) {
    if (!context)
        return;

    ImGui::SetCurrentContext(context);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext(context);
    context = nullptr;
    ImGui::SetCurrentContext(nullptr);
}

void Overlay::UpdateWindowVisibility() {
    if (!m_menuHWnd)
        return;

    const bool shouldShowMenu = OW::Config::Menu;
    const bool isVisible = IsWindowVisible(m_menuHWnd) != FALSE;

    if (shouldShowMenu && !isVisible) {
        ShowWindow(m_menuHWnd, SW_SHOW);
        SetWindowPos(m_menuHWnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    } else if (!shouldShowMenu && isVisible) {
        ShowWindow(m_menuHWnd, SW_HIDE);
    }
}

void Overlay::UpdateSwapChainSizes() {
    RECT canvasRect = {};
    if (GetClientRect(m_canvasHWnd, &canvasRect)) {
        const UINT width = static_cast<UINT>(canvasRect.right - canvasRect.left);
        const UINT height = static_cast<UINT>(canvasRect.bottom - canvasRect.top);
        if (width > 0 && height > 0 && (width != m_canvasWidth || height != m_canvasHeight)) {
            if (ResizeSwapChain(m_canvasSwapChain, &m_canvasRenderTargetView, width, height)) {
                m_canvasWidth = width;
                m_canvasHeight = height;
            }
        }
    }

    RECT menuRect = {};
    if (GetClientRect(m_menuHWnd, &menuRect)) {
        const UINT width = static_cast<UINT>(menuRect.right - menuRect.left);
        const UINT height = static_cast<UINT>(menuRect.bottom - menuRect.top);
        if (width > 0 && height > 0 && (width != m_menuWidth || height != m_menuHeight)) {
            if (ResizeSwapChain(m_menuSwapChain, &m_menuRenderTargetView, width, height)) {
                m_menuWidth = width;
                m_menuHeight = height;
            }
        }
    }
}

void Overlay::RenderCanvas(std::function<void()> renderCallback) {
    if (!m_canvasContext || !m_canvasRenderTargetView || !m_canvasSwapChain)
        return;

    ImGui::SetCurrentContext(m_canvasContext);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    DrawCanvasBackdrop(m_canvasWidth, m_canvasHeight);

    if (renderCallback)
        renderCallback();

    ImGui::Render();

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_pContext->OMSetRenderTargets(1, &m_canvasRenderTargetView, nullptr);
    m_pContext->ClearRenderTargetView(m_canvasRenderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_canvasSwapChain->Present(0, 0);
}

void Overlay::RenderMenu() {
    if (!m_menuContext || !m_menuRenderTargetView || !m_menuSwapChain || !IsWindowVisible(m_menuHWnd))
        return;

    ImGui::SetCurrentContext(m_menuContext);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UI::Render();

    ImGui::Render();

    const float clearColor[4] = { 0.05f, 0.05f, 0.06f, 1.0f };
    m_pContext->OMSetRenderTargets(1, &m_menuRenderTargetView, nullptr);
    m_pContext->ClearRenderTargetView(m_menuRenderTargetView, clearColor);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_menuSwapChain->Present(1, 0);
}
