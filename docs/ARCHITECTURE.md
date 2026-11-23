# Architecture

An original Windows desktop application for upscaling and frame-generating
game output, built in independent phases. It does not use or derive from any
proprietary source, binaries, icons or branding of any existing product.

## Module map

| Module                         | Path                    | Status |
|---------------------------------|--------------------------|--------|
| Desktop UI                     | `src/ui/`                | Implemented (ImGui, dark theme, 9 panels) |
| Configuration Manager          | `src/core/ConfigManager.*`| Implemented (JSON, `%LOCALAPPDATA%\LSAlt`, backup-before-overwrite) |
| Game Profile Manager + Detection | `src/profiles/`         | Implemented (JSON persistence, 4 built-in profiles, real process detection, automatic profile switching, performance session history) |
| GPU Processing Engine (device/enum) | `src/gpu/`           | Implemented (real DXGI adapter enumeration scored by capability, D3D11 device, device-loss recovery) |
| Performance Monitoring Engine  | `src/monitor/`           | Implemented (real CPU/RAM/VRAM/GPU-util/CPU-temp via Win32/DXGI/PDH/WMI/registry) |
| Game Capture Engine            | `src/capture/`           | Implemented (`CaptureEngineWGC`, real Windows Graphics Capture). Compiles, links, and runs correctly (detects non-support rather than faking it); end-to-end verification on the dev machine is blocked by that machine's OS services being disabled -- see `HARDWARE_COMPATIBILITY.md` |
| Image Scaling Engine           | `src/scaling/`           | Implemented (`ScalingEngineD3D11`: Nearest/Bilinear/Bicubic/Lanczos/Integer Scale, real aspect-ratio letterboxing). FSR1/NIS fall back to Bilinear (not implemented -- porting vendor reference shaders is out of scope) |
| Frame Generation Engine        | `src/framegen/`          | Implemented (`FrameGenEngineD3D11`: block-matching motion estimation, forward/backward warping, occlusion-aware blending, VRAM-aware quality guard, thermal/dynamic-performance auto-adjust). 3x/4x remain architecture placeholders only |
| Output Presenter                | `src/presenter/`         | Implemented (`OutputPresenter`: dedicated borderless-resizable window, own D3D11 swapchain, one-draw-call fullscreen-triangle blit, fullscreen toggle, feedback-loop-safe) |

Each engine module is behind an interface (`I*Engine`) so a concrete
implementation can be swapped in without touching the UI or the modules
around it. Scaling and Frame Generation are proven against a synthetic,
deterministic test-pattern source (for repeatable automated testing) AND
against real captured window content when Capture is active -- see
`App::UpdateRealCapturePipeline` for exactly how the two are arbitrated
(real capture takes priority over the synthetic demo whenever it's active).

## Startup data flow

```
WinMain -> App::Initialize
             |-- GpuDeviceManager::EnumerateAdapters()   (real DXGI adapters, capability-scored)
             |-- GpuDeviceManager::CreateDeviceOnAdapter (D3D11 device for the UI's own swapchain)
             |-- PerfMonitor(&gpu)                        (CPU/RAM/VRAM/GPU-util sampling)
             |-- FrameGenEngineD3D11::Initialize + InitializeScalingEngine + CaptureEngineWGC::Initialize
             |-- CreateAppWindow / CreateSwapChain
             '-- UIManager::Initialize (ImGui + Win32/D3D11 backends, dark theme)

App::Run (message loop)
  -> RenderFrame per frame:
       PerfMonitor::Sample() / ThermalMonitor::Sample() -> AppState.lastStats
       GameDetector::Scan() + GameSessionController::Update() (every ~1.5s)
       if real capture active: UpdateRealCapturePipeline()
       else: synthetic TestPatternSource demo path (if either Live toggle is on)
       UIManager::Draw(AppState) -> left nav + active panel
       Present -> FrameGenEngineD3D11::RecordPresentedFrame() (real displayedFps measurement)
```

`AppState` (in `App.h`) is the one object every UI panel function receives;
it bundles every manager/engine (`ConfigManager`, `ProfileManager`,
`GpuDeviceManager`, `PerfMonitor`, `ScalingEngineD3D11`, `FrameGenEngineD3D11`,
`CaptureEngineWGC`, `GameSessionController`, `PerformanceHistory`) and
UI-only scratch state.

## Real pipeline data flow

```
CaptureEngineWGC  : user-selected real window -> ID3D11Texture2D (Windows
                     Graphics Capture, async FrameArrived callback)
ScalingEngineD3D11: captured texture -> scaled texture (real compute-shader
                     Nearest/Bilinear/Bicubic/Lanczos/Integer Scale pass,
                     matching the selected ScalingAlgorithm + aspect ratio)
FrameGenEngineD3D11: pair of scaled frames -> 1 interpolated frame
                     (motion-vector-based warp+blend, not duplication)
```

App::UpdateRealCapturePipeline pairs consecutive scaled real frames (A, B)
each tick and feeds them to FrameGenEngineD3D11 exactly like the synthetic
demo does -- the same engine code path either way. There is not yet a
dedicated presenter window that displays the final result over/in place of
the captured game; the live preview in the Game Library panel is the
current way to see it.

## Why these technology choices

- **C++17 + D3D11 first, D3D12 as a later `RenderApi` option**: D3D11 is
  simpler to get a correct, stable capture/scale pipeline running on the
  MX150/UHD 620 class of hardware; the `RenderApi` enum and engine
  interfaces are designed so D3D12 can be added without a rewrite.
- **Dear ImGui** for the UI: immediate-mode, trivially themeable, very low
  CPU/GPU overhead compared to a full XAML/WinUI stack — important on an
  i5-8250U + MX150 laptop where the app's own UI shouldn't compete with the
  game for resources.
- **nlohmann-json** for config/profile persistence: header-friendly, no code
  generation step, easy to diff in source control.
- **vcpkg manifest mode**: pins dependency versions per-project without a
  global install.
