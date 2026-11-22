#pragma once
#include "../core/Types.h"

namespace lsalt {

// ============================================================================
// PHASE 4 (NOT YET IMPLEMENTED)
// ============================================================================
// Will implement real motion-estimation-based frame interpolation between
// two consecutive captured frames (optical-flow/block-matching motion
// vectors computed on the GPU, then used to warp and blend an intermediate
// frame) -- not naive frame duplication. Architected for 2x first, with
// NextFrameTimings() generalized so 3x/4x can slot in by producing more
// interpolated frames per real pair instead of changing the pipeline shape.
// ============================================================================
class IFrameGenEngine {
public:
    virtual ~IFrameGenEngine() = default;
    virtual bool Initialize(FrameGenMultiplier multiplier, FrameGenQualityMode quality) = 0;
    virtual void SetEnabled(bool enabled) = 0;
    virtual void SetMultiplier(FrameGenMultiplier multiplier) = 0;
    // Given frameA (older) and frameB (newer), fills outInterpolated with
    // (multiplier - 1) in-between frames. Returns false until implemented.
    virtual bool GenerateInterpolatedFrames(void* frameA, void* frameB, void** outInterpolated, int count) = 0;
};

} // namespace lsalt
