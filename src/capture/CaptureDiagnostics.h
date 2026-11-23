#pragma once
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include "../framegen/DiagnosticTests.h" // reuses DiagnosticResult {name, passed, detail}

namespace lsalt {

class CaptureEngineWGC;
class OutputPresenter;

// Real, automated, fully reproducible tests of the capture + presenter
// pipeline:
//  - captures THIS APPLICATION'S OWN main window (a real, currently-
//    running top-level window -- no second external game needed, the
//    same idea as GameDetector's self-detection test) via the real
//    Windows Graphics Capture path, and verifies a real frame actually
//    arrives at the real window size with real readable pixel data;
//  - exercises the feedback-loop-prevention safety check (refusing to
//    capture an excluded window) and invalid-target handling, both of
//    which are deterministic and real regardless of whether WGC itself
//    is supported in this environment;
//  - opens a real OutputPresenter window, presents a real frame to it,
//    resizes it, toggles fullscreen, and closes it.
// These are hardware/OS integration tests, not synthetic-input tests:
// nothing about the capture or presenter path itself is faked or mocked.
class CaptureDiagnostics {
public:
    static std::vector<DiagnosticResult> RunAll(CaptureEngineWGC& captureEngine, HWND selfWindow,
                                                 ID3D11Device* device, ID3D11DeviceContext* context,
                                                 OutputPresenter& presenter, HINSTANCE hInstance);
};

} // namespace lsalt
