#pragma once
#include <d3d11.h>
#include <string>
#include <vector>

namespace lsalt {

class FrameGenEngineD3D11;
class TestPatternSource;

struct DiagnosticResult {
    std::string name;
    bool passed = false;
    std::string detail;
};

// Runs the 10 required validation scenarios (static image, slow/fast
// moving object, camera panning, scene transitions, complex motion,
// occlusion/disocclusion, low-fps input, GPU-saturation, and resize)
// against a live FrameGenEngineD3D11 + TestPatternSource pair, reading
// results back from the GPU so every pass/fail is backed by measured
// pixel data -- never a guess. Mutates the engine/source's scenario,
// size and timing state; callers that care should restore it afterward.
class DiagnosticTests {
public:
    static std::vector<DiagnosticResult> RunAll(FrameGenEngineD3D11& engine, TestPatternSource& source,
                                                 ID3D11Device* device, ID3D11DeviceContext* context);
};

} // namespace lsalt
