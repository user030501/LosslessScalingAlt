#include "ResolutionManager.h"
#include <algorithm>
#include <cmath>

namespace lsalt {

ResolutionResult ResolutionManager::Resolve(const ResolutionRequest& request) {
    ResolutionResult result;
    result.inputWidth = request.inputWidth;
    result.inputHeight = request.inputHeight;

    if (request.inputWidth == 0 || request.inputHeight == 0) return result;

    uint64_t outW = request.outputWidth;
    uint64_t outH = request.outputHeight;
    if (outW == 0 || outH == 0) {
        const uint32_t percent = std::clamp(request.scalePercent, 25u, 200u);
        outW = (static_cast<uint64_t>(request.inputWidth) * percent + 50u) / 100u;
        outH = (static_cast<uint64_t>(request.inputHeight) * percent + 50u) / 100u;
    }

    const uint64_t maxDimension = 16384;
    outW = std::clamp(outW, 1ull, maxDimension);
    outH = std::clamp(outH, 1ull, maxDimension);
    result.outputWidth = static_cast<uint32_t>(outW);
    result.outputHeight = static_cast<uint32_t>(outH);

    if (request.aspectMode == AspectMode::Stretch) {
        result.viewportWidth = result.outputWidth;
        result.viewportHeight = result.outputHeight;
        result.integerFactor = 1;
    } else if (request.aspectMode == AspectMode::IntegerScale) {
        const uint64_t factorX = result.outputWidth / request.inputWidth;
        const uint64_t factorY = result.outputHeight / request.inputHeight;
        result.integerFactor = static_cast<uint32_t>(std::max<uint64_t>(1, std::min(factorX, factorY)));
        result.viewportWidth = static_cast<uint32_t>(std::min<uint64_t>(
            result.outputWidth, request.inputWidth * result.integerFactor));
        result.viewportHeight = static_cast<uint32_t>(std::min<uint64_t>(
            result.outputHeight, request.inputHeight * result.integerFactor));
    } else {
        const double scale = std::min(
            static_cast<double>(result.outputWidth) / request.inputWidth,
            static_cast<double>(result.outputHeight) / request.inputHeight);
        result.viewportWidth = static_cast<uint32_t>(std::lround(request.inputWidth * scale));
        result.viewportHeight = static_cast<uint32_t>(std::lround(request.inputHeight * scale));
        result.viewportWidth = std::min(result.viewportWidth, result.outputWidth);
        result.viewportHeight = std::min(result.viewportHeight, result.outputHeight);
        result.integerFactor = 1;
    }

    result.viewportX = (result.outputWidth - result.viewportWidth) / 2u;
    result.viewportY = (result.outputHeight - result.viewportHeight) / 2u;
    result.valid = true;
    return result;
}

} // namespace lsalt
