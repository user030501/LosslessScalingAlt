#pragma once
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <memory>
#include <string>
#include <vector>

#include "core/ConfigManager.h"
#include "profiles/ProfileManager.h"
#include "gpu/GpuDeviceManager.h"
#include "monitor/PerfMonitor.h"
#include "profiles/GameProfile.h"
#include "scaling/ScalingEngine.h"
#include "scaling/ScalingDiagnostics.h"
#include "capture/CaptureEngineWGC.h"
#include "presenter/OutputPresenter.h"
#include "capture/CaptureDiagnostics.h"
#include "framegen/FrameGenEngine.h"
#include "framegen/TestPatternSource.h"
#include "framegen/DiagnosticTests.h"
#include "framegen/ThermalMonitor.h"
#include "framegen/DynamicPerformanceManager.h"
#include "profiles/GameDetector.h"
#include "profiles/GameSessionController.h"
#include "profiles/PerformanceHistory.h"
#include "profiles/BenchmarkHistory.h"
#include "benchmark/BenchmarkRunner.h"
#include "profiles/ProfileDiagnostics.h"

namespace lsalt {

// Everything the UI panels need, gathered in one place so each panel
// function takes a single AppState& instead of a long parameter list.
struct AppState {
    ConfigManager config;
    ProfileManager profiles;
    GpuDeviceManager gpu;
    std::unique_ptr<PerfMonitor> perf;

    SystemStats lastStats;

    int selectedProfileIndex = 0;
    bool scalingActive = false; // UI-only flag; Start/Stop do not yet drive a real engine (Phase 2/3/4)
    std::string statusMessage = "Idle. Capture/Scaling/Frame-Generation engines are not implemented yet (Phase 2-4).";

    // Scratch buffer used by the "add/edit profile" form in ProfilesPanel.
    GameProfile profileEditBuffer;
    bool isAddingNewProfile = false;

    int activePanel = 0; // index into the left-nav sections

    // Phase 4: a real Frame Generation Engine, exercised against a
    // synthetic test-pattern source since the Capture Engine (Phase 2)
    // doesn't exist yet. See framegen/TestPatternSource.h.
    std::unique_ptr<FrameGenEngineD3D11> frameGenEngine;
    std::unique_ptr<TestPatternSource> testPatternSource;
    bool frameGenLiveEnabled = false;

    // Phase 5: a real Image Scaling Engine (compute-shader Nearest/
    // Bilinear/Bicubic/Lanczos/Integer Scale with real aspect-ratio
    // letterboxing). Exercised against the same synthetic animated source
    // as Frame Generation until the Capture Engine exists.
    std::unique_ptr<ScalingEngineD3D11> scalingEngine;
    bool scalingLiveEnabled = false;

    // Phase 5: real Windows Graphics Capture. When IsCapturing() is true,
    // App::RenderFrame feeds the real captured window's frames through
    // ScalingEngine/FrameGenEngine INSTEAD of the synthetic
    // TestPatternSource (the two share the same engine instances and
    // would otherwise fight over them -- see App::UpdateRealCapturePipeline).
    std::unique_ptr<CaptureEngineWGC> captureEngine;
    float captureScalePercent = 150.0f; // uniform upscale applied to the real captured window

    // Phase 6: a real, dedicated output window showing the pipeline's
    // final processed frame. See OutputPresenter's class comment for the
    // feedback-loop safety rationale (why it's excluded from capture targets).
    std::unique_ptr<OutputPresenter> presenter;
    HINSTANCE hInstance = nullptr; // needed by UI code that calls presenter->Open()
    std::vector<DiagnosticResult> lastDiagnosticsResults;

    // Phase 3: thermal-aware signal + closed-loop quality auto-adjustment.
    // See ThermalMonitor.h / DynamicPerformanceManager.h for what is and
    // isn't real here (CPU temperature is best-effort; GPU temperature is
    // never available; both have a utilization/trend-based fallback).
    std::unique_ptr<ThermalMonitor> thermalMonitor;
    std::unique_ptr<DynamicPerformanceManager> dynamicPerfManager;
    ThermalSample lastThermalSample;
    std::string lastAutoPerfReason; // most recent automatic change, for the UI

    // Phase 4: real process-based game detection, automatic profile
    // switching, and performance session history. See GameDetector.h /
    // GameSessionController.h / PerformanceHistory.h for exactly what is
    // and isn't real here (detection and switching are genuinely real;
    // "applying a profile" only ever changes this app's own settings,
    // never a detected game's actual rendering -- Capture Engine is still
    // an unimplemented stub).
    GameDetector gameDetector;
    GameSessionController gameSession;
    PerformanceHistory perfHistory;
    BenchmarkHistory benchmarkHistory;
    // Phase 11: Benchmark runner instance for UI-driven performance measurement.
    BenchmarkRunner benchmarkRunner;
    std::vector<DetectedProcess> lastGameScan;
    uint64_t deviceRecoveryCount = 0;
};

// Owns the Win32 window, D3D11 swapchain and the ImGui frame lifecycle.
// This is the "Phase 1 shell": a real, running, resizable application
// window with a working dark-themed UI, real GPU enumeration and real
// system performance counters. It does not yet capture or process any
// game -- that is Phase 2 onward.
class App {
public:
    bool Initialize(HINSTANCE hInstance);
    int Run();
    // Headlessly runs DiagnosticTests' 10 scenarios against the live
    // FrameGenEngine + TestPatternSource and logs a pass/fail report.
    // Returns 0 if every scenario passed, 1 otherwise (or if the engine
    // isn't available on this GPU/driver). Does not enter the message loop.
    int RunSelfTest();
    void Shutdown();

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool CreateAppWindow(HINSTANCE hInstance);
    bool CreateSwapChain();
    void ResizeSwapChain(UINT width, UINT height);
    void RenderFrame();

    // Phase 3 stability: real D3D11 device-loss recovery. Triggered when
    // Present() reports DXGI_ERROR_DEVICE_REMOVED/RESET (a real, recoverable
    // failure mode on the laptop GPUs this targets, under thermal/driver
    // stress). Tears down and recreates the device, swap chain, UI render
    // backend and Frame Generation Engine; returns false if recovery itself
    // fails, in which case the app shuts down rather than running on a
    // broken device. Live frame generation is deliberately left disabled
    // after a successful recovery so the user re-enables it knowingly.
    bool AttemptDeviceLossRecovery();
    void InitializeScalingEngine();
    void InitializePresenter();
    void UpdateRealCapturePipeline();
    void RefreshCaptureExclusionList();
    // Shared tail end of both the real-capture and synthetic-demo paths:
    // shows `srv` in the output window if it's open, and ties real
    // "displayed" FPS measurement to whichever Present() call actually
    // represents a frame the user saw (the Presenter's, if open; the
    // main UI window's otherwise -- see FrameGenTypes.h's comment on
    // displayedFps).
    void PresentToOutputWindowIfOpen(ID3D11ShaderResourceView* srv, uint32_t width, uint32_t height);

    static constexpr uint32_t kTestSourceWidth = 960;
    static constexpr uint32_t kTestSourceHeight = 540;
    // Scaling demo target: a real upscale (not a 1:1 passthrough) so
    // "Actual image upscaling" is genuinely exercised, independent of any
    // profile's scalePercent (which governs capture-resolution reduction,
    // a concept that needs the Capture Engine to mean anything real yet).
    static constexpr uint32_t kScalingDstWidth = 1920;
    static constexpr uint32_t kScalingDstHeight = 1080;

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;

    AppState m_state;
    bool m_running = false;
    LARGE_INTEGER m_qpcFrequency{};
    LARGE_INTEGER m_lastFrameQpc{};

    // Phase 4: process detection runs on its own real-time (not frame-rate)
    // cadence -- enumerating processes every rendered frame would add real,
    // pointless CPU overhead for information that doesn't change that fast.
    double m_lastDetectionCheckSeconds = -1000.0;
    static constexpr double kDetectionIntervalSeconds = 1.5;
    bool m_perfHistorySessionWasActive = false;

    // Phase 5: real-capture frame pairing state (see UpdateRealCapturePipeline).
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_capturePrevTex;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_capturePrevSrv;
    uint32_t m_capturePrevWidth = 0, m_capturePrevHeight = 0;
    uint32_t m_captureScalingSrcW = 0, m_captureScalingSrcH = 0;
    uint32_t m_captureScalingDstW = 0, m_captureScalingDstH = 0;
    bool m_wasCapturingReal = false;
    bool m_presenterWasOpen = false;
};

} // namespace lsalt
