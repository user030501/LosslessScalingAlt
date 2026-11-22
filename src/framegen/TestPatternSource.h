#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <vector>
#include <cstdint>

// A synthetic, fully-deterministic animated frame source used ONLY because
// the real game Capture Engine (capture/CaptureEngine.h, ICaptureEngine) is
// still an unimplemented stub -- there is nothing else to feed the Frame
// Generation Engine with yet. This is NOT a capture implementation and
// must never be presented as one: it draws procedural test patterns with
// exactly-known motion so the engine (and its diagnostics) can be
// validated end-to-end before a real capture backend exists.

namespace lsalt {

enum class TestScenario : int {
    Static = 0,
    SlowPan,
    FastPan,
    SlowObject,
    FastObject,
    SceneCut,
    ComplexMotion,
    Occlusion,
    Count
};

const char* ToString(TestScenario s);

struct TestSceneMarker {
    bool valid = false;
    float x = 0.0f, y = 0.0f; // expected position of the tracked marker, in pixels
};

class TestPatternSource {
public:
    bool Initialize(ID3D11Device* device, uint32_t width, uint32_t height);
    bool Resize(ID3D11Device* device, uint32_t width, uint32_t height);

    void SetScenario(TestScenario scenario) { m_scenario = scenario; }
    TestScenario Scenario() const { return m_scenario; }
    void SetSourceFps(double fps) { m_sourceFps = fps > 0.1 ? fps : 0.1; }
    double SourceFps() const { return m_sourceFps; }

    // Advances simulated time by realDtSeconds (wall-clock time elapsed).
    // Returns true if a new source frame pair is now ready (i.e. one
    // simulated source-frame interval has elapsed): frame B becomes the new
    // frame A, and a freshly rendered frame B replaces it.
    bool Tick(ID3D11DeviceContext* context, double realDtSeconds);

    // Forces an immediate frame pair at explicit simulated times, bypassing
    // the fps-paced Tick(). Used by DiagnosticTests for repeatable, exact
    // ground truth.
    void RenderExplicit(ID3D11DeviceContext* context, double timeA, double timeB);

    ID3D11ShaderResourceView* FrameASRV() const { return m_srvA.Get(); }
    ID3D11ShaderResourceView* FrameBSRV() const { return m_srvB.Get(); }

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

    // Where the scenario's tracked marker (moving object/stripe) is
    // expected to be at a given simulated time, for diagnostics. Not
    // meaningful for scenarios without a single tracked marker (SceneCut,
    // ComplexMotion has multiple -- see TrackedMarkers()).
    TestSceneMarker MarkerAt(double time) const;
    std::vector<TestSceneMarker> MarkersAt(TestScenario scenario, double time) const;

private:
    void Render(TestScenario scenario, double time, std::vector<uint8_t>& outRgba) const;
    void Upload(ID3D11DeviceContext* context, ID3D11Texture2D* tex, const std::vector<uint8_t>& rgba);

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_texA, m_texB;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srvA, m_srvB;

    uint32_t m_width = 0, m_height = 0;
    TestScenario m_scenario = TestScenario::SlowObject;
    double m_sourceFps = 30.0;
    double m_simTime = 0.0;
    double m_accumulator = 0.0;
    double m_lastFrameATime = 0.0;
    double m_lastFrameBTime = 0.0;
};

} // namespace lsalt
