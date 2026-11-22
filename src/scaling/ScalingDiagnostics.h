#pragma once
#include <d3d11.h>
#include <vector>
#include "../framegen/DiagnosticTests.h" // reuses DiagnosticResult {name, passed, detail}

namespace lsalt {

class FrameGenEngineD3D11;
class TestPatternSource;

// Real automated tests for the Image Scaling Engine: algorithm correctness
// against hand-crafted, exact-ground-truth pixel data (Nearest, Integer
// Scale, aspect-ratio letterboxing), plus integration against the same
// animated synthetic source Frame Generation uses -- including feeding
// ScalingEngine's own output directly into FrameGenEngine, proving the two
// real engines compose, not just that each works alone.
class ScalingDiagnostics {
public:
    static std::vector<DiagnosticResult> RunAll(ID3D11Device* device, ID3D11DeviceContext* context,
                                                 FrameGenEngineD3D11& frameGenEngine, TestPatternSource& source);
};

} // namespace lsalt
