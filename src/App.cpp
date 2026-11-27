#include "App.h"
#include "core/Logger.h"
#include "ui/UIManager.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <algorithm>
#include <cmath>

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
    m_state.hInstance = hInstance;
    Logger::Instance().SetLevel(m_state.config.Get().logLevel);
    Logger::Instance().Info("App", "Starting LSAlt Phase 1 shell.");

    m_state.gameSession.SetAutoSwitchEnabled(m_state.config.Get().autoProfileSwitchingEnabled);

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

    // Frame Generation Engine + its synthetic test source, for controlled
    // testing. Real game frames now come from CaptureEngineWGC (see below)
    // when the user starts a real capture from the Game Library tab --
    // see UpdateRealCapturePipeline for how the two sources are arbitrated.
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

    InitializeScalingEngine();

    m_state.captureEngine = std::make_unique<CaptureEngineWGC>();
    if (!m_state.captureEngine->Initialize(m_state.gpu.Device().Get())) {
        Logger::Instance().Warn("App", "CaptureEngine failed to initialize: " + m_state.captureEngine->LastError());
    } else if (!CaptureEngineWGC::IsSupportedOnThisSystem()) {
        Logger::Instance().Warn("App", "Windows Graphics Capture is not supported on this OS build.");
    }

    InitializePresenter();
    if (m_state.frameGenEngine) {
        m_state.presenter->SetRefreshRate(m_state.frameGenEngine->DetectedRefreshHz());
    }
    RefreshCaptureExclusionList();

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
    const double nowSeconds = static_cast<double>(now.QuadPart) / static_cast<double>(m_qpcFrequency.QuadPart);
    m_lastFrameQpc = now;

    // Phase 4: real process detection on its own ~1.5s cadence, not every
    // rendered frame -- "is a game running" doesn't change at frame rate,
    // and scanning every frame would be pointless CPU overhead.
    if (nowSeconds - m_lastDetectionCheckSeconds >= kDetectionIntervalSeconds) {
        m_lastDetectionCheckSeconds = nowSeconds;
        m_state.lastGameScan = m_state.gameDetector.Scan();
        m_state.gameSession.Update(m_state.lastGameScan, m_state.profiles, m_state.frameGenEngine.get(),
                                     m_state.selectedProfileIndex, m_state.statusMessage, m_state.frameGenLiveEnabled);
    }

    // Real capture (when active) takes over the Scaling/Frame Generation
    // pipeline from the synthetic demo entirely -- they share the same
    // engine instances, so driving both at once would just have them
    // overwrite each other's output every tick. See
    // UpdateRealCapturePipeline's own comment.
    const bool capturingReal = m_state.captureEngine && m_state.captureEngine->IsCapturing();
    if (capturingReal != m_wasCapturingReal && !capturingReal) {
        // Capture just stopped: drop stale frame-pairing state rather than
        // risk pairing a leftover frame from before a resolution/content
        // change with a stale one.
        m_capturePrevTex.Reset();
        m_capturePrevSrv.Reset();
    }
    m_wasCapturingReal = capturingReal;

    if (capturingReal) {
        UpdateRealCapturePipeline();
    } else {
        const bool anySyntheticDemoActive = (m_state.frameGenLiveEnabled || m_state.scalingLiveEnabled) &&
                                             m_state.testPatternSource;
        bool newSourceFrameThisTick = false;
        if (anySyntheticDemoActive) {
            newSourceFrameThisTick = m_state.testPatternSource->Tick(m_state.gpu.Context().Get(), dt);
        }

        ID3D11ShaderResourceView* finalSrv = nullptr;
        uint32_t finalW = 0, finalH = 0;

        if (m_state.scalingLiveEnabled && m_state.scalingEngine && m_state.testPatternSource) {
            finalSrv = m_state.scalingEngine->Scale(m_state.gpu.Context().Get(), m_state.testPatternSource->FrameBSRV());
            finalW = kScalingDstWidth;
            finalH = kScalingDstHeight;
        }

        if (m_state.frameGenLiveEnabled && m_state.frameGenEngine && m_state.testPatternSource) {
            if (newSourceFrameThisTick) {
                if (ID3D11ShaderResourceView* generated = m_state.frameGenEngine->GenerateInterpolatedFrame(
                        m_state.testPatternSource->FrameASRV(), m_state.testPatternSource->FrameBSRV())) {
                    finalSrv = generated;
                    finalW = kTestSourceWidth;
                    finalH = kTestSourceHeight;
                }
            }

            if (m_state.dynamicPerfManager && m_state.frameGenEngine) {
                const std::string reason = m_state.scalingEngine
                    ? m_state.dynamicPerfManager->Update(
                        *m_state.frameGenEngine, *m_state.scalingEngine,
                        m_state.frameGenEngine->GetStats(), m_state.lastThermalSample,
                        m_state.lastStats.gpuUtilPercent, m_state.lastStats.gpuUtilValid)
                    : m_state.dynamicPerfManager->Update(
                        *m_state.frameGenEngine, m_state.frameGenEngine->GetStats(),
                        m_state.lastThermalSample, m_state.lastStats.gpuUtilPercent,
                        m_state.lastStats.gpuUtilValid);
                if (!reason.empty()) m_state.lastAutoPerfReason = reason;
            }
        }

        if (finalSrv) PresentToOutputWindowIfOpen(finalSrv, finalW, finalH);
    }

    // Phase 4/5: performance session history. A session spans however long
    // Live Engine stays on (that's what actually produces real
    // FrameGenStats today), labeled with the real captured window's title
    // when real capture is active, else whatever game is auto-detected,
    // else clearly marked as the synthetic source.
    if (m_state.frameGenLiveEnabled && !m_perfHistorySessionWasActive) {
        std::string label = "(Synthetic Test Source)";
        if (capturingReal) {
            label = "[Captured] " + m_state.captureEngine->TargetWindowTitle();
        } else if (m_state.gameSession.HasActiveGame()) {
            label = m_state.gameSession.ActiveGameProfileName();
        }
        m_state.perfHistory.BeginSession(label);
        m_perfHistorySessionWasActive = true;
    } else if (!m_state.frameGenLiveEnabled && m_perfHistorySessionWasActive) {
        m_state.perfHistory.EndSession();
        m_perfHistorySessionWasActive = false;
    }

    if (m_state.perfHistory.SessionActive() && m_state.frameGenEngine) {
        const FrameGenStats fgStats = m_state.frameGenEngine->GetStats();
        m_state.perfHistory.RecordSample(fgStats.sourceFps, fgStats.displayedFps, fgStats.lastFrameTimeMs,
                                           m_state.lastStats.vramUsedBytes, m_state.lastStats.gpuUtilPercent,
                                           m_state.lastStats.gpuUtilValid, fgStats.droppedFrames);
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
    } else if (SUCCEEDED(presentHr) && m_state.frameGenLiveEnabled && m_state.frameGenEngine &&
               !(m_state.presenter && m_state.presenter->IsOpen())) {
        // Phase 5 FPS-measurement fix: this is a real, independent
        // presentation event (its own swapchain Present) -- see
        // FrameGenTypes.h's comment on displayedFps for exactly what this
        // is a stand-in for and what it isn't. Once a real Presenter
        // window is open (Phase 6), ITS Present is the more meaningful
        // "frame actually shown" event, so this one steps aside for it --
        // see PresentToOutputWindowIfOpen.
        m_state.frameGenEngine->RecordPresentedFrame();
    }

    // Phase 6: the presenter's open/closed state can change from UI code
    // (Game Library tab) between ticks -- keep the capture exclusion list
    // in sync either direction, so its window is excluded as soon as it
    // exists and stops being excluded (no stale HWND) once it's gone.
    const bool presenterOpenNow = m_state.presenter && m_state.presenter->IsOpen();
    if (presenterOpenNow != m_presenterWasOpen) {
        RefreshCaptureExclusionList();
    }
    m_presenterWasOpen = presenterOpenNow;
}

void App::InitializeScalingEngine() {
    m_state.scalingEngine = std::make_unique<ScalingEngineD3D11>();
    m_state.scalingEngine->SetVramQuery([this](uint64_t& used, uint64_t& budget) {
        return m_state.gpu.QueryVideoMemoryUsage(m_state.gpu.ActiveAdapterIndex(), used, budget);
    });

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir = dir.substr(0, slash);

    if (m_state.scalingEngine->Initialize(m_state.gpu.Device().Get(), dir + L"\\shaders\\scaling")) {
        m_state.scalingEngine->Resize(kTestSourceWidth, kTestSourceHeight, kScalingDstWidth, kScalingDstHeight);
    } else {
        Logger::Instance().Warn("App", "ScalingEngine failed to initialize (shader compile failure?).");
    }
}

void App::InitializePresenter() {
    m_state.presenter = std::make_unique<OutputPresenter>();

    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir(exePath);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir = dir.substr(0, slash);

    if (!m_state.presenter->Initialize(m_state.gpu.Device().Get(), m_state.gpu.Context().Get(), dir + L"\\shaders\\presenter")) {
        Logger::Instance().Warn("App", "OutputPresenter failed to initialize (shader compile failure?).");
    }
}

void App::RefreshCaptureExclusionList() {
    if (!m_state.captureEngine) return;
    // Deliberately NOT excluding this app's own main window: the self-
    // capture self-test (CaptureDiagnostics) legitimately captures it, and
    // the main window's small Game Library preview thumbnail is a much
    // lower-severity loop risk than the Presenter (whose entire purpose is
    // showing the full-resolution continuous processed output -- that's
    // the one that actually must never be a valid capture target).
    std::vector<HWND> excluded;
    if (m_state.presenter && m_state.presenter->IsOpen()) excluded.push_back(m_state.presenter->WindowHandle());
    m_state.captureEngine->SetExcludedWindows(std::move(excluded));
}

void App::PresentToOutputWindowIfOpen(ID3D11ShaderResourceView* srv, uint32_t width, uint32_t height) {
    if (!m_state.presenter || !m_state.presenter->IsOpen()) return;
    const bool presented = m_state.presenter->PresentFrame(srv, width, height);
    if (presented && m_state.frameGenLiveEnabled && m_state.frameGenEngine) {
        m_state.frameGenEngine->RecordPresentedFrame();
    }
}

void App::UpdateRealCapturePipeline() {
    ID3D11ShaderResourceView* captured = m_state.captureEngine->LatestFrameSRV();
    if (!captured || !m_state.scalingEngine) return;

    uint32_t srcW = 0, srcH = 0;
    m_state.captureEngine->GetFrameSize(srcW, srcH);
    if (srcW == 0 || srcH == 0) return;

    const uint32_t dstW = std::max(1u, static_cast<uint32_t>(std::lround(srcW * m_state.captureScalePercent / 100.0f)));
    const uint32_t dstH = std::max(1u, static_cast<uint32_t>(std::lround(srcH * m_state.captureScalePercent / 100.0f)));

    if (srcW != m_captureScalingSrcW || srcH != m_captureScalingSrcH ||
        dstW != m_captureScalingDstW || dstH != m_captureScalingDstH) {
        m_state.scalingEngine->Resize(srcW, srcH, dstW, dstH);
        m_captureScalingSrcW = srcW;
        m_captureScalingSrcH = srcH;
        m_captureScalingDstW = dstW;
        m_captureScalingDstH = dstH;
        // A real size change invalidates any pending "previous frame" --
        // pairing it with a differently-sized new frame would be wrong.
        m_capturePrevTex.Reset();
        m_capturePrevSrv.Reset();
    }

    ID3D11ShaderResourceView* scaledSrv = m_state.scalingEngine->Scale(m_state.gpu.Context().Get(), captured);
    if (!scaledSrv) return;

    ID3D11ShaderResourceView* finalSrv = scaledSrv; // what actually goes to the output window
    if (m_state.frameGenLiveEnabled && m_state.frameGenEngine && m_capturePrevSrv) {
        m_state.frameGenEngine->Resize(dstW, dstH);
        if (ID3D11ShaderResourceView* generated =
                m_state.frameGenEngine->GenerateInterpolatedFrame(m_capturePrevSrv.Get(), scaledSrv)) {
            finalSrv = generated;
        }
    }
    PresentToOutputWindowIfOpen(finalSrv, dstW, dstH);

    // Keep this tick's scaled output as "previous" for the next tick's pairing.
    if (!m_capturePrevTex || m_capturePrevWidth != dstW || m_capturePrevHeight != dstH) {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = dstW;
        desc.Height = dstH;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(m_state.gpu.Device()->CreateTexture2D(&desc, nullptr, m_capturePrevTex.ReleaseAndGetAddressOf()))) return;
        if (FAILED(m_state.gpu.Device()->CreateShaderResourceView(m_capturePrevTex.Get(), nullptr, m_capturePrevSrv.ReleaseAndGetAddressOf()))) return;
        m_capturePrevWidth = dstW;
        m_capturePrevHeight = dstH;
    }
    m_state.gpu.Context()->CopyResource(m_capturePrevTex.Get(), m_state.scalingEngine->OutputTexture());
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
    m_state.scalingEngine.reset();
    if (m_state.captureEngine) m_state.captureEngine->StopCapture();
    m_state.captureEngine.reset();
    if (m_state.presenter) m_state.presenter->Close();
    m_state.presenter.reset();
    m_presenterWasOpen = false;
    m_capturePrevTex.Reset();
    m_capturePrevSrv.Reset();

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

    InitializeScalingEngine();

    m_state.captureEngine = std::make_unique<CaptureEngineWGC>();
    if (!m_state.captureEngine->Initialize(m_state.gpu.Device().Get())) {
        Logger::Instance().Warn("App", "CaptureEngine failed to reinitialize after recovery: " + m_state.captureEngine->LastError());
    }

    InitializePresenter();
    if (m_state.frameGenEngine) {
        m_state.presenter->SetRefreshRate(m_state.frameGenEngine->DetectedRefreshHz());
    }
    RefreshCaptureExclusionList();

    // Require the user to deliberately re-enable live generation/scaling/
    // capture after a recovery rather than silently resuming it on a
    // just-recreated device.
    m_state.frameGenLiveEnabled = false;
    m_state.scalingLiveEnabled = false;
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
    auto results = DiagnosticTests::RunAll(*m_state.frameGenEngine, *m_state.testPatternSource,
                                            m_state.gpu.Device().Get(), m_state.gpu.Context().Get());

    const auto profileResults = ProfileDiagnostics::RunAll(m_state.profiles, *m_state.frameGenEngine);
    results.insert(results.end(), profileResults.begin(), profileResults.end());

    const auto scalingResults = ScalingDiagnostics::RunAll(m_state.gpu.Device().Get(), m_state.gpu.Context().Get(),
                                                             *m_state.frameGenEngine, *m_state.testPatternSource);
    results.insert(results.end(), scalingResults.begin(), scalingResults.end());

    if (m_state.captureEngine && m_state.presenter) {
        const auto captureResults = CaptureDiagnostics::RunAll(*m_state.captureEngine, m_hwnd,
                                                                 m_state.gpu.Device().Get(), m_state.gpu.Context().Get(),
                                                                 *m_state.presenter, m_state.hInstance);
        results.insert(results.end(), captureResults.begin(), captureResults.end());
    }

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
    if (m_perfHistorySessionWasActive) {
        m_state.perfHistory.EndSession();
        m_perfHistorySessionWasActive = false;
    }

    if (m_state.captureEngine) m_state.captureEngine->StopCapture();
    if (m_state.presenter) m_state.presenter->Close();

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
