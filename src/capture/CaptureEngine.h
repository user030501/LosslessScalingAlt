#pragma once
#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace lsalt {

// Interface the rest of the app (ScalingEngine, FrameGenEngine, UI)
// programs against. CaptureEngineWGC below is the first concrete
// implementation, using the Windows.Graphics.Capture API (the same OS
// feature behind the Xbox Game Bar / Windows screen-recording/Teams
// screen-share): it captures a selected top-level window's presented
// frames into a real ID3D11Texture2D, without reading the target
// process' memory, injecting any code into it, or touching anti-cheat in
// any way -- it's the same capability any screen-recording app uses.
//
// Planned-but-real responsibilities:
//  - Capture a user-selected window (not auto-targeted by executable name
//    match alone -- the user picks from GameDetector's real running-window
//    list in the Game Library panel) into a shared GPU texture.
//  - Hand that texture to the ScalingEngine/FrameGenEngine pipeline.
//  - Report capture health (window closed, capture failed, device lost).
class ICaptureEngine {
public:
    virtual ~ICaptureEngine() = default;

    // Starts capturing the given top-level window. Returns false (with a
    // reason in LastError()) if Windows Graphics Capture isn't supported
    // on this OS build, or the window can't be captured.
    virtual bool StartCaptureWindow(HWND targetWindow) = 0;
    virtual void StopCapture() = 0;
    virtual bool IsCapturing() const = 0;

    // The most recently captured frame, or null if nothing has arrived
    // yet (capture just started) or capture isn't active. Safe to call
    // every render frame; internally returns whatever the last frame-
    // arrived callback copied, never blocks waiting for a new one.
    virtual ID3D11ShaderResourceView* LatestFrameSRV() = 0;
    virtual void GetFrameSize(uint32_t& width, uint32_t& height) const = 0;

    virtual std::string LastError() const = 0;
    virtual std::string TargetWindowTitle() const = 0;
};

} // namespace lsalt
