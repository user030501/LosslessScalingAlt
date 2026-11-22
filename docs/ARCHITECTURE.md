# Architecture

An original Windows desktop application for upscaling and frame-generating
game output, built in independent phases. It does not use or derive from any
proprietary source, binaries, icons or branding of any existing product.

## Module map

| Module                         | Path                    | Phase | Status |
|---------------------------------|--------------------------|-------|--------|
| Desktop UI                     | `src/ui/`                | 1     | Implemented (ImGui, dark theme, 8 panels) |
| Configuration Manager          | `src/core/ConfigManager.*`| 1    | Implemented (JSON, `%LOCALAPPDATA%\LSAlt`) |
| Game Profile Manager           | `src/profiles/`          | 1     | Implemented (JSON persistence + 4 sample profiles) |
| GPU Processing Engine (device/enum)| `src/gpu/`           | 1     | Implemented (real DXGI adapter enumeration + D3D11 device) |
| Performance Monitoring Engine  | `src/monitor/`           | 1     | Implemented (real CPU/RAM/VRAM via Win32 + DXGI; game FPS deferred to Phase 2) |
| Game Capture Engine            | `src/capture/`           | 2     | Interface only (`ICaptureEngine`); no implementation |
| Image Scaling Engine           | `src/scaling/`           | 3     | Interface only (`IScalingEngine`); no implementation |
| Frame Generation Engine        | `src/framegen/`          | 4     | Interface only (`IFrameGenEngine`); no implementation |

Each engine module is behind an interface (`I*Engine`) so a concrete
implementation can be swapped in without touching the UI or the modules
around it. The UI already has controls for settings those engines will read
(scaling algorithm, scale %, frame-gen multiplier, etc.) — they persist to
disk now, and take effect once the corresponding engine exists.

## Phase 1 data flow

```
WinMain -> App::Initialize
             |-- GpuDeviceManager::EnumerateAdapters()   (real DXGI adapters: MX150 + UHD 620)
             |-- GpuDeviceManager::CreateDeviceOnAdapter (D3D11 device for the UI's own swapchain)
             |-- PerfMonitor(&gpu)                        (CPU/RAM/VRAM sampling)
             |-- CreateAppWindow / CreateSwapChain
             '-- UIManager::Initialize (ImGui + Win32/D3D11 backends, dark theme)

App::Run (message loop)
  -> RenderFrame per frame:
       PerfMonitor::Sample() -> AppState.lastStats
       UIManager::Draw(AppState) -> left nav + active panel
       Present
```

`AppState` (in `App.h`) is the one object every UI panel function receives;
it bundles `ConfigManager`, `ProfileManager`, `GpuDeviceManager`,
`PerfMonitor` and UI-only scratch state (selected profile, Start/Stop flag).

## Planned data flow once Phase 2-4 land

```
ICaptureEngine   : target window -> ID3D11Texture2D (per frame)
IScalingEngine   : captured texture -> scaled texture (HLSL pass matching
                    the profile's ScalingAlgorithm)
IFrameGenEngine  : pair of scaled frames -> N interpolated frames
                    (motion-vector-based warp+blend, not duplication)
Presenter        : paces and presents the resulting frame sequence into a
                    borderless window placed over/replacing the original
```

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
