#pragma once
#include <cstdint>
#include <string>

namespace lsalt {

// ============================================================================
// PHASE 2 (NOT YET IMPLEMENTED)
// ============================================================================
// This interface defines the contract the rest of the app (ScalingEngine,
// FrameGenEngine, UI) will use once implemented. It intentionally has no
// .cpp file yet: do not mark any UI control as "working capture" until a
// concrete implementation (e.g. CaptureEngineWGC using the Windows.Graphics.
// Capture API, or CaptureEngineDDA using IDXGIOutputDuplication) exists and
// is wired into App.cpp.
//
// Planned responsibilities:
//  - Find a target window/process by executable name (ProfileManager match).
//  - Capture its presented frames into a shared GPU texture (ID3D11Texture2D)
//    without touching the target process' memory or code (no injection).
//  - Hand that texture to the ScalingEngine/FrameGenEngine pipeline.
//  - Report capture health (lost device, window closed/minimized, etc).
// ============================================================================
class ICaptureEngine {
public:
    virtual ~ICaptureEngine() = default;

    // Returns false: no concrete capture backend exists yet (Phase 2).
    virtual bool StartCapture(uint32_t processId) = 0;
    virtual void StopCapture() = 0;
    virtual bool IsCapturing() const = 0;
};

} // namespace lsalt
