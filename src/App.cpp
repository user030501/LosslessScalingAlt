#include "App.h"
#include "core/Logger.h"
#include "ui/UIManager.h"

#include <imgui.h>
#include <imgui_impl_win32.h>

// imgui_impl_win32.h intentionally comments this declaration out (to avoid
// dragging a <windows.h> dependency into the backend header) and asks the
// application to copy it in -- see that header's comment above the #if 0.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

    // Phase 4: Frame Generation Engine + its synthetic test source. Real
    // game frames still require the Capture Engine (Phase 2), which is an
    // unimplemented stub (capture/CaptureEngine.h) -- see docs/ARCHITECTURE.md.
    m_state.frameGenEngine = std::make_unique<FrameGenEngineD3D11>();
    m_state.frameGenEngine->SetVramQuery([this](uint64_t& used, uint64_t& budget) {
        return m_state.gpu.QueryVideoMemoryUsage(m_state.gpu.ActiveAdapterIndex(), used, budget);
    });
    if (m_state.frameGenEngine->Initialize(m_state.gpu.Device().Get(), m_state.gpu.Context().Get(), m_hwnd)) {
        m_state.frameGenEngine->SetQualityMode(m_state.config.Get().defaultFrameGenQuality);
        m_state.frameGenEngine->SetMultiplier(FrameGenMultiplier::X2);
        m_state.frameGenEngine->Resize(kTestSourceWidth, kTestSourceHeight);
    } else {
        Logger::Instance().Warn("App", "FrameGenEngine unsupported on this GPU/driver: " +
                                            m_state.frameGenEngine->Capabilities().unsupportedReason);
    }

    m_state.testPatternSource = std::make_unique<TestPatternSource>();
    m_state.testPatternSource->Initialize(m_state.gpu.Device().Get(), kTestSourceWidth, kTestSourceHeight);

    m_state.thermalMonitor = std::make_unique<ThermalMonitor>();
    m_state.dynamicPerfManager = std::make_unique<DynamicPerformanceManager>();

    QueryPerformanceFrequency(&m_qpcFrequency);
    QueryPerformanceCounter(&m_lastFrameQpc);

    m_running = true;
    return true;
}

void App::RenderFrame() {
    m_state.lastStats = m_state.perf->Sample();

    if (m_state.thermalMonitor) {
        m_state.lastThermalSample = m_state.thermalMonitor->Sample(
            m_state.lastStats.systemCpuPercent, m_state.lastStats.gpuUtilPercent, m_state.lastStats.gpuUtilValid);
    }

    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double dt = static_cast<double>(now.QuadPart - m_lastFrameQpc.QuadPart) / static_cast<double>(m_qpcFrequency.QuadPart);
    m_lastFrameQpc = now;

    if (m_state.frameGenLiveEnabled && m_state.frameGenEngine && m_state.testPatternSource) {
        if (m_state.testPatternSource->Tick(m_state.gpu.Context().Get(), dt)) {
            m_state.frameGenEngine->GenerateInterpolatedFrame(
                m_state.testPatternSource->FrameASRV(), m_state.testPatternSource->FrameBSRV());
        }

        if (m_state.dynamicPerfManager && m_state.frameGenEngine) {
            const std::string reason = m_state.dynamicPerfManager->Update(
                *m_state.frameGenEngine, m_state.frameGenEngine->GetStats(), m_state.lastThermalSample,
                m_state.lastStats.gpuUtilPercent, m_state.lastStats.gpuUtilValid);
            if (!reason.empty()) m_state.lastAutoPerfReason = reason;
        }
    }

    g_uiManager.NewFrame();
    g_uiManager.Draw(m_state);

    const float clearColor[4] = {0.06f, 0.06f, 0.07f, 1.0f};
    m_state.gpu.Context()->OMSetRenderTargets(1, m_rtv.GetAddressOf(), nullptr);
    m_state.gpu.Context()->ClearRenderTargetView(m_rtv.Get(), clearColor);

    g_uiManager.Render();

    const HRESULT presentHr = m_swapChain->Present(1, 0);
    if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET) {
        AttemptDeviceLossRecovery();
    }
}

bool App::AttemptDeviceLossRecovery() {
    const HRESULT removedReason = m_state.gpu.Device() ? m_state.gpu.Device()->GetDeviceRemovedReason() : E_FAIL;
    Logger::Instance().Error("App", "D3D11 device lost (removed reason hr=" + std::to_string(removedReason) +
                                         "). Attempting recovery...");
    m_state.statusMessage = "GPU device lost (driver crash/reset). Attempting recovery...";

    // Everything holding a reference to the old device/context/swapchain
    // must be released before a new device can be created cleanly.
    g_uiManager.Shutdown();
    m_rtv.Reset();
    m_swapChain.Reset();
    m_state.frameGenEngine.reset();
    m_state.testPatternSource.reset();

    const int preferredAdapter = m_state.config.Get().preferredAdapterIndex;
    if (!m_state.gpu.EnumerateAdapters() || !m_state.gpu.CreateDeviceOnAdapter(preferredAdapter)) {
        Logger::Instance().Error("App", "Device-loss recovery failed: could not recreate the D3D11 device.");
        m_state.statusMessage = "GPU device lost and recovery failed. Please restart the app.";
        m_running = false;
        return false;
    }

    if (!CreateSwapChain()) {
        Logger::Instance().Error("App", "Device-loss recovery failed: could not recreate the swap chain.");
        m_state.statusMessage = "GPU device lost and recovery failed. Please restart the app.";
        m_running = false;
        return false;
    }

    if (!g_uiManager.Initialize(m_hwnd, m_state.gpu.Device().Get(), m_state.gpu.Context().Get())) {
        Logger::Instance().Error("App", "Device-loss recovery failed: could not reinitialize the UI renderer.");
        m_state.statusMessage = "GPU device lost and recovery failed. Please restart the app.";
        m_running = false;
        return false;
    }

    m_state.frameGenEngine = std::make_unique<FrameGenEngineD3D11>();
    m_state.frameGenEngine->SetVramQuery([this](uint64_t& used, uint64_t& budget) {
        return m_state.gpu.QueryVideoMemoryUsage(m_state.gpu.ActiveAdapterIndex(), used, budget);
    });
    if (m_state.frameGenEngine->Initialize(m_state.gpu.Device().Get(), m_state.gpu.Context().Get(), m_hwnd)) {
        m_state.frameGenEngine->SetQualityMode(m_state.config.Get().defaultFrameGenQuality);
        m_state.frameGenEngine->SetMultiplier(FrameGenMultiplier::X2);
        m_state.frameGenEngine->Resize(kTestSourceWidth, kTestSourceHeight);
    } else {
        Logger::Instance().Warn("App", "FrameGenEngine unsupported on the recreated device/driver: " +
                                            m_state.frameGenEngine->Capabilities().unsupportedReason);
    }

    m_state.testPatternSource = std::make_unique<TestPatternSource>();
    m_state.testPatternSource->Initialize(m_state.gpu.Device().Get(), kTestSourceWidth, kTestSourceHeight);

    // Require the user to deliberately re-enable live generation after a
    // recovery rather than silently resuming it on a just-recreated device.
    m_state.frameGenLiveEnabled = false;
    if (m_state.frameGenEngine) m_state.frameGenEngine->SetEnabled(false);

    Logger::Instance().Info("App", "Device-loss recovery succeeded: GPU device, swap chain, UI renderer and "
                                     "Frame Generation Engine were recreated.");
    m_state.statusMessage = "Recovered from a GPU device loss. Frame generation was disabled as a precaution -- "
                              "re-enable it from the Frame Generation tab.";
    return true;
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

int App::RunSelfTest() {
    if (!m_state.frameGenEngine || !m_state.testPatternSource) {
        Logger::Instance().Error("SelfTest", "Frame Generation Engine or test source not constructed.");
        return 1;
    }
    if (!m_state.frameGenEngine->Capabilities().computeShadersSupported) {
        Logger::Instance().Error("SelfTest", "Unsupported: " + m_state.frameGenEngine->Capabilities().unsupportedReason);
        return 1;
    }

    m_state.frameGenEngine->SetEnabled(true);
    const auto results = DiagnosticTests::RunAll(*m_state.frameGenEngine, *m_state.testPatternSource,
                                                  m_state.gpu.Device().Get(), m_state.gpu.Context().Get());

    bool allPassed = true;
    for (const auto& r : results) {
        Logger::Instance().Log(r.passed ? LogLevel::Info : LogLevel::Error, "SelfTest",
                                 (r.passed ? "PASS " : "FAIL ") + r.name + " -- " + r.detail);
        if (!r.passed) allPassed = false;
    }
    Logger::Instance().Info("SelfTest", allPassed ? "All scenarios passed." : "One or more scenarios FAILED.");
    return allPassed ? 0 : 1;
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
