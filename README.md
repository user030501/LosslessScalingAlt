# LSAlt (working title)

An original, independently implemented Windows desktop application that
upscales game output and generates intermediate frames to improve perceived
smoothness - inspired by the *category* of tool that Lossless Scaling
popularized, built from scratch with its own architecture, code and UI.
Not affiliated with, derived from, or endorsed by that or any other
commercial product; no proprietary source, binaries, icons or branding are
used here.

Targets entry-level/low-VRAM gaming laptops first (dev/reference hardware:
Intel Core i5-8250U, NVIDIA GeForce MX150 2GB, Intel UHD Graphics 620,
16GB RAM).

## Status: Phase 1 of 7

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full module map
and phase plan, and [`docs/BUILD.md`](docs/BUILD.md) to build and run it.

Phase 1 delivers a real, running application: dark-themed desktop UI (Home,
Scaling, Frame Generation, Game Profiles, Performance Monitor, GPU Settings,
Advanced Settings, About), JSON-persisted config and game profiles, real
DXGI GPU enumeration, and real CPU/RAM/VRAM telemetry. It does not yet
capture, scale or frame-generate any game - those are Phases 2-4, each
landing as a real, testable implementation rather than a placeholder.

## License / originality note

All code in this repository is original. No code, assets, or branding from
Lossless Scaling or any other commercial application is included.
