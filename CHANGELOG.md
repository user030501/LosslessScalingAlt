# Changelog

All notable changes to this project are documented here. Versions are
development milestones, not public releases.

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
