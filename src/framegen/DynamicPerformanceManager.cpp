#include "DynamicPerformanceManager.h"
#include "FrameGenEngine.h"
#include "../scaling/ScalingEngine.h"
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

void DynamicPerformanceManager::SetAdaptiveScalingEnabled(bool enabled) {
    m_adaptiveScalingEnabled = enabled;
    m_scalingCooldownSamples = 0;
    m_consecutiveScalingOverloadSamples = 0;
    m_consecutiveScalingHeadroomSamples = 0;
    m_lastScalingReason = enabled
        ? "Adaptive scaling enabled; scaling is unchanged until sustained overload is measured."
        : "Adaptive scaling disabled.";
}

void DynamicPerformanceManager::RestoreScaling(ScalingEngineD3D11& scaling) {
    if (!m_scalingBaselineValid) return;
    scaling.SetQualityPreset(m_baselineQuality);
    scaling.SetSharpenLevel(m_baselineSharpen);
    scaling.SetAlgorithm(m_baselineAlgorithm);
    m_currentAdaptiveScaling = m_baselineQuality;
    m_scalingBaselineValid = false;
    m_lastScalingReason = "Adaptive scaling disabled; original scaling settings restored.";
}

namespace {
FrameGenQualityMode StepDown(FrameGenQualityMode mode) {
    return mode == FrameGenQualityMode::Quality
        ? FrameGenQualityMode::Balanced
        : FrameGenQualityMode::Performance;
}

FrameGenQualityMode StepUp(FrameGenQualityMode mode) {
    return mode == FrameGenQualityMode::Performance
        ? FrameGenQualityMode::Balanced
        : FrameGenQualityMode::Quality;
}
} // namespace

std::string DynamicPerformanceManager::Update(FrameGenEngineD3D11& engine,
                                                const FrameGenStats& fgStats,
                                                const ThermalSample& thermal,
                                                double gpuUtilPercent,
                                                bool gpuUtilValid) {
    if (!m_autoEnabled || !engine.IsEnabled()) {
        m_consecutiveOverloadSamples = 0;
        m_consecutiveHeadroomSamples = 0;
        return {};
    }

    bool overloaded = thermal.sustainedHighLoad;
    if (gpuUtilValid && gpuUtilPercent >= kGpuUtilOverloadPercent) overloaded = true;

    const double refreshHz = engine.DetectedRefreshHz();
    if (refreshHz > 1.0 &&
        fgStats.totalGpuMs >= (1000.0 / refreshHz) * kGpuTimeBudgetFraction) overloaded = true;

    const FrameGenQualityMode current = engine.QualityMode();
    if (overloaded) {
        m_consecutiveHeadroomSamples = 0;
        ++m_consecutiveOverloadSamples;
        if (m_consecutiveOverloadSamples >= kStepDownSamples &&
            current != FrameGenQualityMode::Performance) {
            const FrameGenQualityMode next = StepDown(current);
            engine.SetQualityMode(next);
            m_consecutiveOverloadSamples = 0;
            const std::string reason = "Auto-stepped down from " + std::string(ToString(current)) +
                " to " + std::string(ToString(next)) +
                " quality: sustained GPU overload.";
            Logger::Instance().Warn("DynamicPerformanceManager", reason);
            return reason;
        }
        return {};
    }

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
        const std::string reason = "Auto-stepped up from " + std::string(ToString(current)) +
            " to " + std::string(ToString(next)) +
            " quality: sustained headroom.";
        Logger::Instance().Info("DynamicPerformanceManager", reason);
        return reason;
    }
    return {};
}

std::string DynamicPerformanceManager::Update(FrameGenEngineD3D11& engine,
                                                ScalingEngineD3D11& scaling,
                                                const FrameGenStats& fgStats,
                                                const ThermalSample& thermal,
                                                double gpuUtilPercent,
                                                bool gpuUtilValid) {
    const std::string frameGenReason =
        Update(engine, fgStats, thermal, gpuUtilPercent, gpuUtilValid);

    if (!m_adaptiveScalingEnabled) {
        m_scalingBaselineValid = false;
        m_consecutiveScalingOverloadSamples = 0;
        m_consecutiveScalingHeadroomSamples = 0;
        return frameGenReason;
    }

    if (!m_scalingBaselineValid) {
        m_baselineAlgorithm = scaling.Algorithm();
        m_baselineSharpen = scaling.Sharpen();
        m_baselineQuality = scaling.QualityPreset();
        m_currentAdaptiveScaling = m_baselineQuality;
        m_scalingBaselineValid = true;
    }

    if (m_scalingCooldownSamples > 0) --m_scalingCooldownSamples;

    bool overloaded = thermal.sustainedHighLoad;
    if (gpuUtilValid && gpuUtilPercent >= kGpuUtilOverloadPercent) overloaded = true;
    const double refreshHz = engine.DetectedRefreshHz();
    if (refreshHz > 1.0 &&
        fgStats.totalGpuMs >= (1000.0 / refreshHz) * kGpuTimeBudgetFraction) overloaded = true;
    if (fgStats.frameTimeVarianceMs > 4.0 && fgStats.droppedFrames > 0) overloaded = true;

    if (overloaded) {
        m_consecutiveScalingHeadroomSamples = 0;
        ++m_consecutiveScalingOverloadSamples;
        if (m_scalingCooldownSamples == 0 &&
            m_consecutiveScalingOverloadSamples >= kStepDownSamples) {
            ScalingQualityPreset next = m_currentAdaptiveScaling;
            SharpenLevel nextSharpen = scaling.Sharpen();
            ScalingAlgorithm nextAlgorithm = scaling.Algorithm();

            if (nextSharpen == SharpenLevel::High) nextSharpen = SharpenLevel::Medium;
            else if (nextSharpen == SharpenLevel::Medium) nextSharpen = SharpenLevel::Low;
            else if (nextSharpen == SharpenLevel::Low) nextSharpen = SharpenLevel::Off;

            if (nextAlgorithm == ScalingAlgorithm::Lanczos) nextAlgorithm = ScalingAlgorithm::Bicubic;
            else if (nextAlgorithm == ScalingAlgorithm::Bicubic) nextAlgorithm = ScalingAlgorithm::Bilinear;

            if (m_currentAdaptiveScaling == ScalingQualityPreset::Quality) next = ScalingQualityPreset::Balanced;
            else if (m_currentAdaptiveScaling == ScalingQualityPreset::Balanced) next = ScalingQualityPreset::Performance;

            if (next != m_currentAdaptiveScaling ||
                nextSharpen != scaling.Sharpen() ||
                nextAlgorithm != scaling.Algorithm()) {
                scaling.SetQualityPreset(next);
                scaling.SetSharpenLevel(nextSharpen);
                scaling.SetAlgorithm(nextAlgorithm);
                m_currentAdaptiveScaling = next;
                m_scalingCooldownSamples = 30;
                m_consecutiveScalingOverloadSamples = 0;
                m_lastScalingReason =
                    "Adaptive scaling reduced to " + std::string(ToString(next)) +
                    ": sustained GPU/load instability; no resolution changed.";
                Logger::Instance().Warn("DynamicPerformanceManager", m_lastScalingReason);
                return m_lastScalingReason;
            }

            m_lastScalingReason =
                "Adaptive scaling reached Performance; Frame Generation disable is recommended "
                "if delivery remains unstable.";
            Logger::Instance().Warn("DynamicPerformanceManager", m_lastScalingReason);
            return m_lastScalingReason;
        }
        return frameGenReason;
    }

    m_consecutiveScalingOverloadSamples = 0;
    ++m_consecutiveScalingHeadroomSamples;
    if (m_scalingCooldownSamples == 0 &&
        m_consecutiveScalingHeadroomSamples >= kStepUpSamples &&
        (m_currentAdaptiveScaling != m_baselineQuality ||
         scaling.Sharpen() != m_baselineSharpen ||
         scaling.Algorithm() != m_baselineAlgorithm)) {
        ScalingAlgorithm restoredAlgorithm = scaling.Algorithm();
        if (restoredAlgorithm == ScalingAlgorithm::Bilinear &&
            m_baselineAlgorithm == ScalingAlgorithm::Lanczos) restoredAlgorithm = ScalingAlgorithm::Bicubic;
        else if (restoredAlgorithm == ScalingAlgorithm::Bicubic &&
                 m_baselineAlgorithm == ScalingAlgorithm::Lanczos) restoredAlgorithm = ScalingAlgorithm::Lanczos;

        SharpenLevel restoredSharpen = scaling.Sharpen();
        if (restoredSharpen == SharpenLevel::Off &&
            m_baselineSharpen != SharpenLevel::Off) restoredSharpen = SharpenLevel::Low;
        else if (restoredSharpen == SharpenLevel::Low &&
                 m_baselineSharpen == SharpenLevel::Medium) restoredSharpen = SharpenLevel::Medium;
        else if (restoredSharpen == SharpenLevel::Medium &&
                 m_baselineSharpen == SharpenLevel::High) restoredSharpen = SharpenLevel::High;

        ScalingQualityPreset restoredQuality = m_currentAdaptiveScaling;
        if (restoredQuality == ScalingQualityPreset::Performance) restoredQuality = ScalingQualityPreset::Balanced;
        else if (restoredQuality == ScalingQualityPreset::Balanced &&
                 m_baselineQuality == ScalingQualityPreset::Quality) restoredQuality = ScalingQualityPreset::Quality;

        scaling.SetQualityPreset(restoredQuality);
        scaling.SetSharpenLevel(restoredSharpen);
        scaling.SetAlgorithm(restoredAlgorithm);
        m_currentAdaptiveScaling = restoredQuality;
        m_scalingCooldownSamples = 60;
        m_consecutiveScalingHeadroomSamples = 0;
        m_lastScalingReason =
            "Adaptive scaling restored one quality step: sustained headroom, no resolution changed.";
        Logger::Instance().Info("DynamicPerformanceManager", m_lastScalingReason);
        return m_lastScalingReason;
    }

    return frameGenReason;
}

} // namespace lsalt
