#pragma once
#include "CaptureEngine.h"

#include <wrl/client.h>
#include <mutex>
#include <memory>

namespace lsalt {

// Real Windows Graphics Capture (WGC) implementation. Uses the C++/WinRT
// projection (header-only, ships in the Windows SDK) rather than hand-
// rolled raw-ABI COM calls, for the same reason the rest of this codebase
// prefers correctness-by-construction: WGC's object lifetime and event-
// registration rules are easy to get subtly wrong by hand.
//
// What this does and doesn't do:
//  - Captures a real, user-selected top-level window's presented frames
//    via GraphicsCaptureItem/Direct3D11CaptureFramePool/
//    GraphicsCaptureSession -- the identical OS mechanism behind Xbox Game
//    Bar capture, OBS's "Windows Graphics Capture" source, and Teams
//    screen share. No process memory is read, no code is injected, no
//    anti-cheat system is touched or bypassed.
//  - Frames arrive on a worker thread (FrameArrived fires off the UI
//    thread); the most recent one is copied into a texture this class
//    owns, guarded by a mutex, so the render thread's LatestFrameSRV()
//    call is always safe and never blocks on the capture thread.
//  - Handles the target window resizing (recreates the frame pool at the
//    new size) and the target window closing (Closed event stops capture
//    and surfaces that via LastError()/IsCapturing()).
//  - Does NOT survive the owning ID3D11Device being lost -- like
//    FrameGenEngine/ScalingEngine/TestPatternSource, it is torn down and
//    must be explicitly restarted after App::AttemptDeviceLossRecovery.
class CaptureEngineWGC : public ICaptureEngine {
public:
    CaptureEngineWGC();
    ~CaptureEngineWGC() override;

    // Must be called once before StartCaptureWindow, with the same D3D11
    // device the rest of the app's pipeline uses (captured frames are
    // real textures on this device -- no copy to a second device/API).
    bool Initialize(ID3D11Device* device);

    bool StartCaptureWindow(HWND targetWindow) override;
    void StopCapture() override;
    bool IsCapturing() const override;

    ID3D11ShaderResourceView* LatestFrameSRV() override;
    void GetFrameSize(uint32_t& width, uint32_t& height) const override;

    std::string LastError() const override;
    std::string TargetWindowTitle() const override;

    // True once on this OS build (checked once, cached) -- exposed so the
    // UI can explain *why* capture controls are disabled rather than just
    // having them silently fail.
    static bool IsSupportedOnThisSystem();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace lsalt
