# Hardware Compatibility

Every number in this document was measured on the actual development
machine by running `LSAlt.exe --selftest` and reading the resulting log
and in-app Performance Monitor panel. Nothing here is an estimate or a
benchmark run on different hardware.

## Reference / tested hardware

| Component | Model | Role |
|---|---|---|
| CPU | Intel Core i5-8250U @ 1.60GHz | Verified (real registry-read model string, real utilization) |
| Integrated GPU | Intel UHD Graphics 620 | Verified (enumerated, compute-shader + typed-UAV-store capable) |
| Discrete GPU | NVIDIA GeForce MX150 (2GB) | Verified (enumerated, auto-selected as the more capable adapter, ran all engines) |
| RAM | 16GB | Verified |
| OS | Windows 11 (build 10.0.26200) | Verified |

## What was actually verified, and how

All results below come from the automated self-test suite
(`LSAlt.exe --selftest`), which is 27/27 passing as of this writing, run
against the NVIDIA GeForce MX150 (auto-selected by the capability-scored
adapter selection over the Intel UHD 620).

| Area | Verified | Notes |
|---|---|---|
| GPU enumeration/selection | Yes | Both adapters enumerated with real capability flags; MX150 auto-selected (more VRAM, both compute-capable) |
| Frame Generation (2x) | Yes | 14 scenarios, real GPU motion estimation + interpolation + readback, against a synthetic deterministic source (no real game capture pipeline existed when this was built) |
| Image Scaling | Yes | 6 scenarios with exact pixel-level ground truth (not approximate) for Nearest/Integer-Scale exactness and letterboxing math, plus real pipeline composition with Frame Generation |
| GPU optimization (VRAM guard, auto-select, device-loss recovery) | Yes | Including a real, interesting finding: this hybrid-graphics laptop's DXGI-reported VRAM budget for the MX150 starts very low (~1MB) immediately after the adapter wakes from idle, then climbs to its real ~1.9GB within moments — the VRAM guard reacted correctly to that real (if initially misleading) number |
| Game detection | Yes | Real process enumeration finds this app's own running process deterministically |
| Profile switching | Yes | Synthetic process-scan injection (no second real game needed) exercising the real switching/restoration code paths |
| Performance history / CSV export | Yes | Real file I/O, round-tripped |
| FPS independence | Yes | Confirmed interactively: Source=30, Generated=30, Displayed=60 (not a clean multiple — tied to this app's own real present cadence hitting every other vblank at a detected 120Hz refresh rate) |
| CPU temperature | Yes | Real reading via WMI `MSAcpi_ThermalZoneTemperature` works on this machine |
| GPU temperature | **Not available** | Requires vendor-specific NVIDIA NVAPI / AMD ADL, which this app intentionally does not depend on. Always reported as N/A, never estimated. |
| **Real window capture (Windows Graphics Capture)** | **Not verified end-to-end** | See below |

## Known limitation: Windows Graphics Capture not verified end-to-end on the dev machine

The Capture Engine (`CaptureEngineWGC`, using the real Windows Graphics
Capture API) **compiles and links successfully**, and its self-test
(`27. Real Window Capture (self)`) **correctly detects and reports** that
WGC is unsupported in the current environment, rather than crashing or
fabricating a successful capture.

Investigation traced the actual cause: this specific development machine
has several Windows OS services disabled (apparently by a prior
"debloating"/privacy script) that Windows Graphics Capture's activation
path depends on:

- `CDPSvc` (Connected Devices Platform Service)
- `ncbservice` (Network Connection Broker) — a dependency of the above
- `CDPUserSvc` (the per-user companion service) — its per-session instance
  requires a logoff/logon cycle to pick up a re-enabled template
  configuration, which was not performed
- The Windows Event Log service itself is also disabled on this machine,
  which is unusual even for a debloated system

The first two were re-enabled (with explicit approval) to narrow down the
cause; the investigation was stopped at the `CDPUserSvc`/Event Log layer
rather than continuing to widen changes to this machine's OS configuration,
since that goes beyond verifying this application's code.

**What this means:** the implementation is real and believed correct (it
follows the same interop pattern as Microsoft's own public WGC samples,
and the surrounding code — activation, D3D11 interop, frame pool handling,
resize/close handling — compiled without a single error on the first
attempt), but it has not been exercised against an actual running game's
window on real hardware. On a typical Windows 10/11 installation (where
these services are enabled by default), it is expected to work; this has
not been confirmed.

**For a user on a normal (non-debloated) Windows installation:** Windows
Graphics Capture should simply work. If capture reports "not supported,"
check that the "Connected Devices Platform Service" is not disabled in
`services.msc`.

## Not implemented (by design, not oversight)

- 3x/4x frame generation (architecture placeholders only).
- FSR1 (AMD FidelityFX Super Resolution 1.0) and NVIDIA Image Scaling —
  porting a vendor reference shader implementation was judged out of
  scope; both fall back to Bilinear with a logged warning rather than
  silently doing nothing.
- GPU temperature reporting (requires NVAPI/ADL).
