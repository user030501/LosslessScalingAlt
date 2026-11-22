#pragma once
#include "../core/Types.h"

namespace lsalt {

// ============================================================================
// PHASE 3 (NOT YET IMPLEMENTED)
// ============================================================================
// Will own the HLSL pixel shaders for each ScalingAlgorithm (nearest,
// bilinear, bicubic, Lanczos, integer scale as straightforward shaders;
// FSR1/NIS as ports of their public, permissively-licensed reference HLSL)
// and run them against the texture ICaptureEngine produces.
// ============================================================================
class IScalingEngine {
public:
    virtual ~IScalingEngine() = default;
    virtual bool Initialize(ScalingAlgorithm algorithm) = 0;
    virtual void SetAlgorithm(ScalingAlgorithm algorithm) = 0;
    virtual void SetScalePercent(int percent) = 0;
    // Scales srcTexture into dstTexture. Returns false until implemented.
    virtual bool Scale(void* srcTexture, void* dstTexture) = 0;
};

} // namespace lsalt
