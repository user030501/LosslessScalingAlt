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
    double generatedFps = 0.0;       // output rate including interpolated frames
    double displayedFps = 0.0;       // rate frames were actually consumed by a presenter
    double lastFrameTimeMs = 0.0;
    double motionEstimationGpuMs = 0.0;
    double interpolationGpuMs = 0.0;
    double totalGpuMs = 0.0;
    uint64_t framesGenerated = 0;
    uint64_t droppedFrames = 0;
    uint64_t queueDepth = 0;
    bool engineActive = false;
    std::string statusText;
};

} // namespace lsalt
