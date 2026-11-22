#include "App.h"
#include "core/Logger.h"
#include "ui/UIManager.h"

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

namespace lsalt {

namespace {
UIManager g_uiManager;
App* g_appForWndProc = nullptr;

LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_appForWndProc) return g_appForWndProc->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
} // namespace

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_SIZE:
            if (m_swapChain && wParam != SIZE_MINIMIZED) {
                ResizeSwapChain(LOWORD(lParam), HIWORD(lParam));
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool App::CreateAppWindow(HINSTANCE hInstance) {
    const wchar_t* kClassName = L"LSAltMainWindow";

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProcThunk;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    AppConfig& cfg = m_state.config.Get();
    const int x = (cfg.windowX >= 0) ? cfg.windowX : CW_USEDEFAULT;
    const int y = (cfg.windowY >= 0) ? cfg.windowY : CW_USEDEFAULT;

    g_appForWndProc = this;

    m_hwnd = CreateWindowExW(
        0, kClassName, L"LSAlt (Phase 1)", WS_OVERLAPPEDWINDOW,
        x, y, cfg.windowWidth, cfg.windowHeight,
        nullptr, nullptr, hInstance, nullptr);

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);
    return true;
}

bool App::CreateSwapChain() {
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.BufferCount = 2;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.SampleDesc.Count = 1;

    HRESULT hr = m_state.gpu.Factory()->CreateSwapChainForHwnd(
        m_state.gpu.Device().Get(), m_hwnd, &desc, nullptr, nullptr, m_swapChain.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
        Logger::Instance().Error("App", "CreateSwapChainForHwnd failed, hr=" + std::to_string(hr));
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    m_state.gpu.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf());

    return true;
}

void App::ResizeSwapChain(UINT width, UINT height) {
    if (width == 0 || height == 0) return;
    m_rtv.Reset();
    m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(backBuffer.GetAddressOf()));
    m_state.gpu.Device()->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.ReleaseAndGetAddressOf());
}

bool App::Initialize(HINSTANCE hInstance) {
    Logger::Instance().SetLevel(m_state.config.Get().logLevel);
    Logger::Instance().Info("App", "Starting LSAlt Phase 1 shell.");

    if (!m_state.gpu.EnumerateAdapters()) {
        Logger::Instance().Error("App", "No DXGI adapters found.");
        return false;
    }

    if (!m_state.gpu.CreateDeviceOnAdapter(m_state.config.Get().preferredAdapterIndex)) {
        return false;
    }

    m_state.perf = std::make_unique<PerfMonitor>(&m_state.gpu);

    if (!CreateAppWindow(hInstance)) return false;
    if (!CreateSwapChain()) return false;

    if (!g_uiManager.Initialize(m_hwnd, m_state.gpu.Device().Get(), m_state.gpu.Context().Get())) {
        Logger::Instance().Error("App", "UIManager::Initialize failed.");
        return false;
    }

    m_running = true;
    return true;
}

void App::RenderFrame() {
    m_state.lastStats = m_state.perf->Sample();

    g_uiManager.NewFrame();
    g_uiManager.Draw(m_state);

    const float clearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};
    m_state.gpu.Context()->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_state.gpu.Context()->ClearRenderTargetView(m_rtv.Get(), clearColor);

    g_uiManager.Render();

    m_swapChain->Present(1, 0);
}

int App::Run() {
    MSG msg{};
    while (m_running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                m_running = false;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!m_running) break;
        RenderFrame();
    }
    return static_cast<int>(msg.wParam);
}

void App::Shutdown() {
    g_uiManager.Shutdown();

    AppConfig& cfg = m_state.config.Get();
    if (m_hwnd) {
        RECT rect{};
        GetWindowRect(m_hwnd, &rect);
        cfg.windowX = rect.left;
        cfg.windowY = rect.top;
        cfg.windowWidth = rect.right - rect.left;
        cfg.windowHeight = rect.bottom - rect.top;
    }
    m_state.config.Save();

    g_appForWndProc = nullptr;
    Logger::Instance().Info("App", "Shutdown complete.");
}

} // namespace lsalt
