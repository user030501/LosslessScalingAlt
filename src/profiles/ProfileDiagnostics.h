#pragma once
#include <vector>
#include "../framegen/DiagnosticTests.h" // reuses DiagnosticResult {name, passed, detail}

namespace lsalt {

class ProfileManager;
class FrameGenEngineD3D11;

// Phase 4 self-test: exercises game detection, profile CRUD, automatic
// profile switching + restoration, performance-history accounting, CSV
// export, and cross-instance persistence against the REAL on-disk
// profiles.json/sessions.json -- using obviously-named temporary entries
// that are removed again where that's possible (one case, noted in its
// own result text, deliberately leaves a clearly-labeled "(Self-Test)"
// history record rather than silently editing history behind the scenes).
class ProfileDiagnostics {
public:
    static std::vector<DiagnosticResult> RunAll(ProfileManager& profiles, FrameGenEngineD3D11& engine);
};

} // namespace lsalt
