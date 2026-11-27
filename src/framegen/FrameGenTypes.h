#pragma once
#include <cstdint>
#include <string>
#include "../core/Types.h"

// Shared types for the Frame Generation Engine (Phase 4 in the project's own
// phase numbering -- see FrameGenEngine.h). These describe the concrete
// motion-compensated interpolation pipeline: block-matching motion
// estimation, forward/backward warping and occlusion-aware blending.

namespace lsalt {

// Per-quality-mode knobs for the motion estimation / interpolation pipeline.
// Larger blocks and fewer pyramid levels trade accuracy for throughput.
struct FrameGenQualityParams {
    int blockSize = 16;        // motion vector granularity, in pixels
    int pyramidLevels = 2;     // coarse-to-fine levels (1 = no pyramid)
    int searchRadius = 6;      // candidate search window radius, in pixels, at each level

    static FrameGenQualityParams For(FrameGenQualityMode mode) {
        switch (mode) {
            case FrameGenQualityMode::Quality:
                return FrameGenQualityParams{8, 3, 8};
            case FrameGenQualityMode::Performance:
                return FrameGenQualityParams{32, 1, 4};
            case FrameGenQualityMode::Balanced:
            default:
                return FrameGenQualityParams{16, 2, 6};
        }
    }
};

// What this GPU/driver combination can actually run. Populated by querying
// real D3D11 feature support at Initialize() time -- never assumed.
struct FrameGenCapabilities {
    bool computeShadersSupported = false;   // feature level >= 11_0
    bool typedUavLoadStoreSupported = false; // needed for RG16_FLOAT UAV read-modify
    std::string adapterDescription;
    std::string unsupportedReason; // empty if supported
};

struct FrameGenStats {
    double sourceFps = 0.0;          // rate real frame pairs are arriving at
    double generatedFps = 0.0;       // rate of successfully-produced interpolated frames

    // Phase 5: an independent measurement, not generatedFps*3 (that was
    // the bug this field used to have). Driven by
    // FrameGenEngineD3D11::RecordPresentedFrame(), called once per real
    // IDXGISwapChain::Present() call in App::RenderFrame while Live Engine
    // is active. Read literally: this is how often THIS APPLICATION'S OWN
    // window presents while generation is running, not a captured game's
    // displayed-frame rate -- there is no real per-game presenter yet
    // (that needs the Capture Engine). It is still a real, independently
    // measured number, not a derived/estimated one.
    double displayedFps = 0.0;
    double lastFrameTimeMs = 0.0;
    double motionEstimationGpuMs = 0.0;
    double interpolationGpuMs = 0.0;
    double totalGpuMs = 0.0;
    uint64_t framesGenerated = 0;
    uint64_t droppedFrames = 0;
    uint64_t queueDepth = 0;
    uint64_t duplicateFrames = 0;
    uint64_t lateFrames = 0;
    double onePercentLowFrameTimeMs = 0.0;
    double frameTimeVarianceMs = 0.0;
    double estimatedLatencyMs = 0.0;
    double cpuProcessingMs = 0.0;
    uint64_t estimatedOwnedTextureBytes = 0;
    double gpuMemoryPressurePercent = 0.0;
    double scalingGpuMs = -1.0;
    double scalingCpuSubmissionMs = -1.0;
    double sharpeningGpuMs = -1.0;
    double presenterGpuMs = -1.0;
    double presenterCpuSubmissionMs = -1.0;

    // Phase 4: running avg/min/max of displayedFps since the engine was
    // last (re)enabled -- real incremental statistics over the same real
    // per-sample displayedFps value above, not a separate estimate.
    double avgDisplayedFpsRunning = 0.0;
    double minDisplayedFpsRunning = 0.0;
    double maxDisplayedFpsRunning = 0.0;
    bool engineActive = false;
    std::string statusText;

    // Phase 3: non-empty when the engine or DynamicPerformanceManager has
    // automatically changed the quality mode since the last time this was
    // read (VRAM headroom, sustained GPU overload, etc.) -- surfaced in the
    // UI so an automatic change is never silent.
    std::string autoAdjustReason;
};

} // namespace lsalt
