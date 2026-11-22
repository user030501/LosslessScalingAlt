#include "DynamicPerformanceManager.h"
#include "FrameGenEngine.h"
#include "../core/Logger.h"

namespace lsalt {

void DynamicPerformanceManager::SetAutoEnabled(bool enabled) {
    if (enabled == m_autoEnabled) return;
    m_autoEnabled = enabled;
    m_consecutiveOverloadSamples = 0;
    m_consecutiveHeadroomSamples = 0;
    Logger::Instance().Info("DynamicPerformanceManager",
        enabled ? "Automatic performance management enabled." : "Automatic performance management disabled.");
}

namespace {
FrameGenQualityMode StepDown(FrameGenQualityMode m) {
    if (m == FrameGenQualityMode::Quality) return FrameGenQualityMode::Balanced;
    return FrameGenQualityMode::Performance;
}
FrameGenQualityMode StepUp(FrameGenQualityMode m) {
    if (m == FrameGenQualityMode::Performance) return FrameGenQualityMode::Balanced;
    return FrameGenQualityMode::Quality;
}
} // namespace

std::string DynamicPerformanceManager::Update(FrameGenEngineD3D11& engine, const FrameGenStats& fgStats,
                                                const ThermalSample& thermal, double gpuUtilPercent, bool gpuUtilValid) {
    if (!m_autoEnabled || !engine.IsEnabled()) {
        m_consecutiveOverloadSamples = 0;
        m_consecutiveHeadroomSamples = 0;
        return {};
    }

    bool overloaded = false;
    if (gpuUtilValid && gpuUtilPercent >= kGpuUtilOverloadPercent) overloaded = true;

    const double refreshHz = engine.DetectedRefreshHz();
    if (refreshHz > 1.0) {
        const double frameBudgetMs = 1000.0 / refreshHz;
        if (fgStats.totalGpuMs >= frameBudgetMs * kGpuTimeBudgetFraction) overloaded = true;
    }

    if (thermal.sustainedHighLoad) overloaded = true;

    const FrameGenQualityMode current = engine.QualityMode();

    if (overloaded) {
        m_consecutiveHeadroomSamples = 0;
        ++m_consecutiveOverloadSamples;
        if (m_consecutiveOverloadSamples >= kStepDownSamples && current != FrameGenQualityMode::Performance) {
            const FrameGenQualityMode next = StepDown(current);
            engine.SetQualityMode(next);
            m_consecutiveOverloadSamples = 0;
            const std::string reason = "Auto-stepped down from " + std::string(ToString(current)) + " to " +
                                        std::string(ToString(next)) + " quality: sustained GPU overload.";
            Logger::Instance().Warn("DynamicPerformanceManager", reason);
            return reason;
        }
        return {};
    }

    // Not overloaded this sample: count toward stepping back up, but reset
    // the overload counter too (a single clean sample shouldn't erase a
    // building overload streak the next time it does spike -- only a
    // *healthy* streak should, which this branch already represents since
    // we only get here when overloaded == false).
    m_consecutiveOverloadSamples = 0;
    if (current == FrameGenQualityMode::Quality) {
        m_consecutiveHeadroomSamples = 0;
        return {};
    }

    ++m_consecutiveHeadroomSamples;
    if (m_consecutiveHeadroomSamples >= kStepUpSamples) {
        const FrameGenQualityMode next = StepUp(current);
        engine.SetQualityMode(next);
        m_consecutiveHeadroomSamples = 0;
        const std::string reason = "Auto-stepped up from " + std::string(ToString(current)) + " to " +
                                    std::string(ToString(next)) + " quality: sustained headroom.";
        Logger::Instance().Info("DynamicPerformanceManager", reason);
        return reason;
    }
    return {};
}

} // namespace lsalt
