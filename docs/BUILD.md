# Build Instructions

## Prerequisites

- Windows 10/11 x64
- Visual Studio 2022 Build Tools (or full VS 2022) with the
  "Desktop development with C++" workload, including the Windows 10/11 SDK
- CMake >= 3.21
- Git

On this dev machine these were installed via:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621 --includeRecommended"
winget install --id Kitware.CMake -e
```

## First-time setup

```powershell
cd LosslessScalingAlt
.\scripts\bootstrap.ps1     # clones + bootstraps vcpkg into .\vcpkg
```

## Configure & build

Using the CMake preset (recommended):

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

The first configure will take a while: vcpkg builds Dear ImGui and
nlohmann-json from source for `x64-windows`.

The resulting executable is at:

```
build\windows-x64\Debug\LSAlt.exe
```

(or `build\windows-x64\Release\LSAlt.exe` for `--preset windows-x64-release`).

## Running

Just launch `LSAlt.exe` - no installer, no elevation required. On first run
it creates:

- `%LOCALAPPDATA%\LSAlt\config.json` - app-wide settings
- `%LOCALAPPDATA%\LSAlt\profiles.json` - game profiles (seeded with
  Valorant / GTA V / ROS Legacy / Roblox samples)
- `%LOCALAPPDATA%\LSAlt\logs\lsalt.log` - log file

## What to expect in this build

- A real, resizable, dark-themed window with 9 navigable sections.
- Real GPU list (your NVIDIA MX150 and Intel UHD 620 should both show up
  under "GPU Settings", with real capability flags and an auto-selection
  explanation), real CPU/RAM/VRAM/GPU-utilization/CPU-temperature graphs
  under "Performance Monitor".
- Game profile CRUD that persists to disk, with a working "Browse..." file
  picker for manual executable selection, plus real process-based game
  detection and automatic profile switching under "Game Library".
- Real image scaling (Nearest/Bilinear/Bicubic/Lanczos/Integer Scale) and
  real motion-compensated frame generation (2x), each runnable live
  against either a synthetic test source (for controlled testing) or a
  real captured window (Game Library tab → "Capture").
- "Start Scaling" on Home is a UI-only status flag (it says so) — the real
  controls for capture/scaling/frame-generation live on their own tabs.
- Run `LSAlt.exe --selftest` to run the full automated diagnostic suite
  (27 scenarios) headlessly; it exits 0 if everything passes.

See [`ARCHITECTURE.md`](ARCHITECTURE.md) for what's implemented versus
known limitations, and [`INSTALLATION.md`](INSTALLATION.md) for portable/
installer packaging.
