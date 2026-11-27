#pragma once
#include <cstdint>
#include "../core/Types.h"

namespace lsalt {

struct ResolutionRequest {
    uint32_t inputWidth = 0;
    uint32_t inputHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t scalePercent = 100;
    AspectMode aspectMode = AspectMode::Fit;
};

struct ResolutionResult {
    uint32_t inputWidth = 0;
    uint32_t inputHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    uint32_t viewportX = 0;
    uint32_t viewportY = 0;
    uint32_t viewportWidth = 0;
    uint32_t viewportHeight = 0;
    uint32_t integerFactor = 1;
    bool valid = false;
};

class ResolutionManager {
public:
    static ResolutionResult Resolve(const ResolutionRequest& request);
};

} // namespace lsalt
