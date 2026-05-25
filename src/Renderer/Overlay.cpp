#include "Renderer/Overlay.hpp"
#include "Features/UI.hpp"
#include "Utils/Config.hpp"
#include "resource.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

// Forward declare the ImGui WndProc handler (exported by imgui_impl_win32)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// =====================================================================
// File-scope WndProc for the overlay window
// =====================================================================

namespace {

constexpr int kDefaultClientWidth = 650;
constexpr int kDefaultClientHeight = 550;

RECT AdjustedWindowRectForClient(int clientWidth, int clientHeight) {
    RECT rect = { 0, 0, clientWidth, clientHeight };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
    return rect;
}

void ApplyMinimumTrackSize(LPARAM lParam) {
    MINMAXINFO* minMax = reinterpret_cast<MINMAXINFO*>(lParam);
    if (!minMax)
        return;

    RECT rect = AdjustedWindowRectForClient(kDefaultClientWidth, kDefaultClientHeight);
    minMax->ptMinTrackSize.x = rect.right - rect.left;
    minMax->ptMinTrackSize.y = rect.bottom - rect.top;
}

bool IsMenuTogglePressed() {
    const int key = OW::Config::MenuToggleKey;
    return key > 0 && (GetAsyncKeyState(key) & 0x0001) != 0;
}

} // namespace

static LRESULT WINAPI OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
        case WM_GETMINMAXINFO: {
            ApplyMinimumTrackSize(lParam);
            return 0;
        }
        case WM_SYSCOMMAND: {
            if ((wParam & 0xFFF0) == SC_KEYMENU)
                return 0;   // Disable ALT menu
            break;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// =====================================================================
// Overlay implementation
// =====================================================================

bool Overlay::Initialize(const wchar_t* overlayTitle) {
    // 1. Register overlay window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = OverlayWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"UnleashedOverlayDX11";
    wc.hIcon         = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_UNLEASHED));
    wc.hIconSm       = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_UNLEASHED));

    RegisterClassExW(&wc);

    // 2. Create a normal topmost tool window with a 650x550 client area.
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT windowRect = AdjustedWindowRectForClient(kDefaultClientWidth, kDefaultClientHeight);
    const int windowW = windowRect.right - windowRect.left;
    const int windowH = windowRect.bottom - windowRect.top;

    RECT workArea = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int workW = workArea.right - workArea.left;
    const int workH = workArea.bottom - workArea.top;
    const int windowX = workArea.left + (workW - windowW) / 2;
    const int windowY = workArea.top + (workH - windowH) / 2;

    // 3. Create the overlay display window.
    m_hWnd = CreateWindowExW(
        exStyle,
        wc.lpszClassName,
        overlayTitle,
        style | WS_VISIBLE,
        windowX, windowY, windowW, windowH,
        nullptr,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (!m_hWnd)
        return false;

    // Show the window and force it to the top of the Z-order
    ShowWindow(m_hWnd, SW_SHOW);
    UpdateWindow(m_hWnd);
    SetWindowPos(m_hWnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    // 4. Initialise D3D11
    if (!CreateDX11())
        return false;

    // 5. Initialise ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    // Style
    ImGui::StyleColorsDark();

    // Backends
    if (!ImGui_ImplWin32_Init(m_hWnd))
        return false;
    if (!ImGui_ImplDX11_Init(m_pDevice, m_pContext))
        return false;

    // Initialise the renderer helper
    Render::Initialize(m_pDevice, m_pContext);
    Render::InitIcons(m_pDevice);
    UI::InitializeResources(m_pDevice);

    return true;
}

void Overlay::Run(std::function<void()> renderCallback) {
    if (!m_hWnd || !m_pSwapChain)
        return;

    // Message loop
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

        // Exit if target or overlay window was destroyed
        if (!IsWindow(m_hWnd))
            break;

        if (IsMenuTogglePressed())
            OW::Config::Menu = !OW::Config::Menu;

        RECT clientRect = {};
        if (GetClientRect(m_hWnd, &clientRect)) {
            const UINT width = static_cast<UINT>(clientRect.right - clientRect.left);
            const UINT height = static_cast<UINT>(clientRect.bottom - clientRect.top);
            if (width > 0 && height > 0 &&
                (width != m_clientWidth || height != m_clientHeight)) {
                ResizeSwapChain(width, height);
            }
        }

        if (!m_pRenderTargetView)
            continue;

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // User-defined rendering
        if (renderCallback)
            renderCallback();

        // Render ImGui draw data
        ImGui::Render();

        // Clear to opaque black and present
        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_pContext->OMSetRenderTargets(1, &m_pRenderTargetView, nullptr);
        m_pContext->ClearRenderTargetView(m_pRenderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        m_pSwapChain->Present(1, 0);   // V-Sync on
    }

    Shutdown();
}

void Overlay::Shutdown() {
    UI::ShutdownResources();
    Render::ShutdownIcons();

    // ImGui shutdown
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    // D3D11 cleanup
    CleanupDX11();

    // Window cleanup
    if (m_hWnd) {
        DestroyWindow(m_hWnd);
        m_hWnd = nullptr;
    }

    // Unregister window class (optional -- process exit will handle it)
    UnregisterClassW(L"UnleashedOverlayDX11", GetModuleHandleW(nullptr));
}

// =====================================================================
// Private helpers
// =====================================================================

bool Overlay::CreateDX11() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Width                   = 0;   // auto from window
    sd.BufferDesc.Height                  = 0;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = m_hWnd;
    sd.SampleDesc.Count                   = 1;
    sd.SampleDesc.Quality                 = 0;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    // createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;

    D3D_FEATURE_LEVEL featureLevel;
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
        &sd,
        &m_pSwapChain,
        &m_pDevice,
        &featureLevel,
        &m_pContext
    );

    if (FAILED(hr))
        return false;

    // Create the render-target view
    ResizeBuffers();
    RECT clientRect = {};
    if (GetClientRect(m_hWnd, &clientRect)) {
        m_clientWidth = static_cast<UINT>(clientRect.right - clientRect.left);
        m_clientHeight = static_cast<UINT>(clientRect.bottom - clientRect.top);
    }
    return true;
}

void Overlay::CleanupDX11() {
    if (m_pRenderTargetView) {
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }
    if (m_pSwapChain) {
        m_pSwapChain->Release();
        m_pSwapChain = nullptr;
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

void Overlay::ResizeBuffers() {
    if (m_pRenderTargetView) {
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }

    ID3D11Texture2D* pBackBuffer = nullptr;
    if (m_pSwapChain) {
        m_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer) {
            m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pRenderTargetView);
            pBackBuffer->Release();
        }
    }
}

bool Overlay::ResizeSwapChain(UINT width, UINT height) {
    if (!m_pSwapChain || width == 0 || height == 0)
        return false;

    if (m_pContext)
        m_pContext->OMSetRenderTargets(0, nullptr, nullptr);

    if (m_pRenderTargetView) {
        m_pRenderTargetView->Release();
        m_pRenderTargetView = nullptr;
    }

    const HRESULT hr = m_pSwapChain->ResizeBuffers(
        0,
        width,
        height,
        DXGI_FORMAT_UNKNOWN,
        0
    );
    if (FAILED(hr))
        return false;

    ResizeBuffers();
    m_clientWidth = width;
    m_clientHeight = height;
    return m_pRenderTargetView != nullptr;
}
