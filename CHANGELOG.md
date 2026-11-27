# Changelog

All notable changes to this project are documented here. Versions are
development milestones, not public releases.

## [0.9.0] - Advanced GPU optimization and adaptive performance

### Added
- Bounded texture-pool capacity and byte-budget trimming.
- Pool allocation, reuse, cleanup, and oversized-allocation diagnostics.
- Estimated FrameGen and Scaling texture-memory reporting in the
  Performance Monitor.
- Real DXGI GPU-memory-pressure reporting where `IDXGIAdapter3` is available.
- Opt-in adaptive Scaling quality: algorithm, preset, and sharpening
  step-down, sustained-headroom restoration, cooldown, hysteresis, and
  opt-out restoration of the original settings.
- Benchmark audit mode with warm-up, 30 measured rounds, median, average,
  minimum, maximum, and standard deviation. Benchmarks report
  submission-plus-`Flush` wall time, not actual GPU execution alone.
- Phase 9 tests for pool cleanup, pool trimming, device status, oversized
  allocation failure, adaptive overload/cooldown/restore, and repeatability.

### Audited
- Phase 8 900p measurements can appear faster or slower than 720p due to
  warm-up state, CPU submission, driver scheduling, and wall-clock-only
  measurement. Phase 9 reports statistics across repeated rounds and does
  not treat individual faster results as performance improvements.

## [0.8.0] - Frame pacing, latency, and performance optimization

### Added
- A bounded, deterministic `FrameScheduler` with refresh-rate scheduling,
  QPC timestamps, late-frame detection, dropped-frame accounting,
  duplicate-sequence detection, and a latest-bounded queue.
- Presenter pacing at the detected monitor refresh rate, using latest-frame
  admission and no unbounded SRV queue.
- Phase 8 performance fields for 1% low frame time, frame-time variance,
  duplicate/late frames, CPU processing time, queue depth, and estimated
  end-to-end latency.
- Non-blocking D3D11 timestamp-poll integration for GPU/CPU processing
  measurements and opt-in adaptive Frame Generation quality management.
- Eight deterministic Phase 8 tests plus synthetic pacing benchmarks at
  720p, 900p, 1080p, and 1440p.

### Improved
- Frame-generation source ordering and stale-frame presentation handling
  remain explicit: frame A is the older frame, frame B is the newer frame,
  and each generated result is only handed to the presenter once.
- The presenter and frame-pacing paths avoid CPU readbacks and GPU flushes
  in normal operation; timestamp queries are polled without blocking.

## [0.7.0] - Advanced scaling and resolution management

### Added
- A centralized `ResolutionManager` for custom/automatic input and output
  sizes, fit/stretch/integer placement, letterboxing/pillarboxing, invalid
  dimensions, and D3D11-safe maximum dimensions.
- Sharpening levels (Off/Low/Medium/High), quality/performance presets,
  and resolution/aspect/sharpening controls in the Scaling panel.
- Texture-pool reuse for repeated scaling resolutions and VRAM-budget
  admission/clamping for output allocations.
- Phase 7 profile fields for input/output resolution, quality, sharpening,
  aspect mode, frame generation, and GPU preference, plus backward-
  compatible profile migration and legacy vendor-algorithm migration.
- Synthetic Phase 7 diagnostics for resolution ground truth, invalid sizes,
  integer scaling, sharpening, texture reuse, VRAM budget enforcement,
  and the 640x480 through 2560x1440 benchmark matrix.

### Improved
- Presenter swapchain resize, viewport/scissor handling, fullscreen/DPI
  behavior, and aspect-ratio calculations now use the same resolution
  placement rules as the scaling engine.
- Pixel-center UV sampling and border handling remain GPU-compute based;
  no CPU fallback or vendor-specific FSR1/NIS implementation is claimed.

## [0.6.0] - Real-game integration: Output Presenter, pipeline wiring

### Added
- `OutputPresenter`: a real, dedicated, borderless-but-resizable output
  window with its own D3D11 swapchain, presenting the pipeline's final
  processed frame via a one-draw-call fullscreen-triangle blit (no CPU
  readback). Supports resizing, fullscreen toggle (F11/Esc), aspect-ratio
  preservation, and clean close (Esc/Alt+F4/window X).
- Real capture → scale → frame-generate → **present** pipeline wiring:
  `App::UpdateRealCapturePipeline` now also drives the Output Presenter
  when it's open, with real `displayedFps` measurement tied to the
  Presenter's own `Present()` calls once it's open (falling back to the
  main UI window's otherwise).
- Feedback-loop prevention: `CaptureEngineWGC::SetExcludedWindows` refuses
  to capture a window on the exclusion list (the Presenter's, kept in
  sync automatically); checked before, and independent of, WGC support,
  so it's deterministically testable.
- Minimized/stalled-capture detection: `CaptureEngineWGC::SecondsSinceLastFrame()`,
  surfaced in the UI when a capture stops receiving new frames.
- 4 new automated diagnostic scenarios: real self-capture (already
  present, now alongside), feedback-loop-prevention, invalid-capture-target
  handling, and full Output Presenter lifecycle (open/present/resize/
  fullscreen/close) — self-test is now 30 scenarios.
- Real-world verification: launched an actually-installed copy of Roblox
  and confirmed LSAlt's automatic game detection/profile switching
  against it for real (not a self-test or synthetic substitute).

### Fixed
- `CaptureEngineWGC::IsSupportedOnThisSystem()` was documented as "checked
  once, cached" but was never actually cached — it was being called from
  UI draw code every rendered frame while the Game Library panel was
  visible, causing continuous real WinRT activation-factory overhead and
  log spam. Now a real `static`-initialized cache.

## [0.5.0] - Capture, Scaling, FPS correction, stability, release prep

### Added
- Real Game Capture Engine (`CaptureEngineWGC`) using Windows Graphics
  Capture: captures a user-selected top-level window, handles resize
  (frame pool recreation) and window-close, thread-safe frame handoff.
- Real Image Scaling Engine (`ScalingEngineD3D11`): Nearest Neighbor,
  Bilinear, Bicubic (Keys cubic convolution), Lanczos (a=2), Integer Scale,
  with real aspect-ratio-preserving letterboxing/pillarboxing.
- Real capture -> scale -> frame-generate pipeline composition
  (`App::UpdateRealCapturePipeline`), taking priority over the synthetic
  demo path whenever a real capture is active.
- 7 new automated diagnostic scenarios for Scaling (exact pixel-level
  ground truth for Nearest/Integer Scale/letterboxing, pipeline
  integration, resolution-change robustness, all-algorithm validity) and 1
  for Capture (real self-window capture, end-to-end).
- `docs/HARDWARE_COMPATIBILITY.md`, `docs/INSTALLATION.md`,
  `CHANGELOG.md`, `LICENSE`.
- CMake install rules and Inno Setup installer script; portable build
  packaging instructions.

### Fixed
- **`displayedFps` was calculated as `generatedFps x 3`** (one count each
  for frame A, the generated frame, and frame B) — a derived number
  presented as a measurement. It is now driven by real
  `IDXGISwapChain::Present()` calls (`FrameGenEngineD3D11::
  RecordPresentedFrame`), independently of source/generated FPS. `Frame
  Time` (which was itself derived from the broken `displayedFps`) now
  derives from content production rate instead, so it doesn't silently
  start tracking vsync/UI refresh rate.
- A real dangling-reference bug in `GameSessionController::
  ActiveGameProfileName()` (`std::optional::value_or()` returns by value;
  binding a `const&` to it dangled — caught by MSVC's C4172 warning, not
  by any test, since the UB happened not to corrupt memory in the specific
  cases exercised).
- Automatic profile switching could silently disable a user's manually-
  enabled Live Engine demo (both wrote the same engine "enabled" flag);
  automatic switching now leaves that flag alone whenever the user has
  Live Engine on.
- A stale "Run Diagnostic Tests (10 scenarios)" button label (actually 14
  by that point).

## [0.4.0] - Game Profiles and Advanced Performance Monitoring

### Added
- Real process-based game detection (`GameDetector`) and automatic profile
  switching (`GameSessionController`), explicitly scoped to only ever
  change this app's own settings, never a detected game's rendering.
- Game Library UI panel (cards, detection status, add/edit/remove).
- Performance session history (`PerformanceHistory`) with JSON persistence
  and CSV export.
- Real CPU model (registry), app memory use, avg/min/max FPS, frame-time
  graph in the Performance Monitor panel.
- 6 new automated diagnostic scenarios (self-detection, profile CRUD,
  switching + restoration, history accounting, CSV export, persistence).

## [0.3.0] - GPU Optimization

### Added
- Capability-scored GPU adapter auto-selection (compute shader + typed UAV
  store support, then VRAM).
- VRAM-aware quality-mode guard with automatic downgrade.
- Parallelized motion-estimation compute shader (was `[numthreads(1,1,1)]`
  doing the entire exhaustive search on one GPU thread; now one thread per
  candidate offset with a group-shared reduction — same results, real
  GPU occupancy win).
- Thermal monitoring (best-effort WMI CPU temperature, utilization-based
  fallback signal) and closed-loop dynamic performance management.
- Real D3D11 device-loss recovery; config backup-before-overwrite.
- 4 new automated diagnostic scenarios (720p/1080p, forced low-VRAM
  fallback, soak/leak test).

## [0.2.0] - Frame Generation Engine

### Added
- Real motion-compensated 2x frame interpolation: hierarchical
  block-matching motion estimation, forward/backward warping,
  occlusion-aware blending (never frame duplication).
- Real frame pacing (refresh-rate detection, non-blocking GPU timing).
- Synthetic, deterministic test-pattern source for repeatable automated
  testing (the Capture Engine did not exist yet at this point).
- 10 automated diagnostic scenarios (static/slow/fast motion, panning,
  scene cuts, complex motion, occlusion, low-FPS input, GPU overload,
  resize), run via `LSAlt.exe --selftest`.

## [0.1.0] - Phase 1 shell

### Added
- Dark-themed desktop UI (ImGui), real DXGI GPU enumeration, real
  CPU/RAM/VRAM telemetry, JSON-persisted config and game profiles (4
  built-in: Valorant, Grand Theft Auto V, ROS Legacy, Roblox).
