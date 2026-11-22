#pragma once
#include <windows.h>
#include <d3d11.h>
#include <vector>
#include "../framegen/DiagnosticTests.h" // reuses DiagnosticResult {name, passed, detail}

namespace lsalt {

class CaptureEngineWGC;

// Real, automated, fully reproducible test of the capture pipeline: it
// captures THIS APPLICATION'S OWN main window (a real, currently-running
// top-level window -- no second external game needed, the same idea as
// GameDetector's self-detection test) via the real Windows Graphics
// Capture path, and verifies a real frame actually arrives at the real
// window size with real readable pixel data. This is a hardware/OS
// integration test, not a synthetic-input test: nothing about the
// capture path itself is faked or mocked.
class CaptureDiagnostics {
public:
    static std::vector<DiagnosticResult> RunAll(CaptureEngineWGC& captureEngine, HWND selfWindow,
                                                 ID3D11Device* device, ID3D11DeviceContext* context);
};

} // namespace lsalt
