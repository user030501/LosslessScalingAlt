#pragma once
#include <string>

#include "FrameGenTypes.h"
#include "ThermalMonitor.h"

namespace lsalt {

class FrameGenEngineD3D11;
class ScalingEngineD3D11;

// Phase 3: closed-loop automatic quality-mode adjustment.
//
// The brief's three "dynamic performance profiles" (Ultra Performance /
// Balanced / High Quality) map directly onto the FrameGen engine's
// existing FrameGenQualityMode (Performance / Balanced / Quality -- see
// FrameGenTypes.h): that is already the knob that trades motion-estimation
// accuracy for GPU/VRAM cost, so this manager drives that knob rather than
// inventing a second, parallel one.
//
// Every signal behind a decision is real and already measured elsewhere
// in this codebase -- GPU dispatch time vs. the frame budget implied by
// the detected display refresh rate (FramePacing, via FrameGenStats),
// GPU/CPU utilization (PerfMonitor's real PDH/GetSystemTimes counters),
// and ThermalMonitor's utilization-pressure/sustained-load fallback.
// Nothing here is simulated. No automatic change happens silently: each
// one is logged and the reason is surfaced back through
// FrameGenStats::autoAdjustReason.
//
// This never changes the game's rendering resolution -- only the FrameGen
// quality mode -- and a manual SetQualityMode() call from the UI always
// takes precedence; the UI is expected to turn AutoEnabled() off when the
// user manually picks a quality mode, so automatic and manual control
// never fight each other silently.
class DynamicPerformanceManager {
public:
    void SetAutoEnabled(bool enabled);
    bool AutoEnabled() const { return m_autoEnabled; }

    // Call once per Performance Monitor sample (not necessarily every UI
    // frame). May call engine.SetQualityMode() if auto mode decides a
    // change is warranted. Returns a human-readable reason if it just
    // changed something, otherwise an empty string.
    std::string Update(FrameGenEngineD3D11& engine, const FrameGenStats& fgStats,
                        const ThermalSample& thermal, double gpuUtilPercent, bool gpuUtilValid);
    std::string Update(FrameGenEngineD3D11& engine, ScalingEngineD3D11& scaling,
                       const FrameGenStats& fgStats, const ThermalSample& thermal,
                       double gpuUtilPercent, bool gpuUtilValid);
    bool AdaptiveScalingEnabled() const { return m_adaptiveScalingEnabled; }
    void SetAdaptiveScalingEnabled(bool enabled);
    void RestoreScaling(ScalingEngineD3D11& scaling);
    const std::string& LastAdaptiveScalingReason() const { return m_lastScalingReason; }

private:
    bool m_autoEnabled = false;
    int m_consecutiveOverloadSamples = 0;
    int m_consecutiveHeadroomSamples = 0;
    bool m_adaptiveScalingEnabled = false;
    bool m_scalingBaselineValid = false;
    int m_scalingCooldownSamples = 0;
    int m_consecutiveScalingOverloadSamples = 0;
    int m_consecutiveScalingHeadroomSamples = 0;
    ScalingAlgorithm m_baselineAlgorithm = ScalingAlgorithm::Bilinear;
    SharpenLevel m_baselineSharpen = SharpenLevel::Off;
    ScalingQualityPreset m_baselineQuality = ScalingQualityPreset::Balanced;
    ScalingQualityPreset m_currentAdaptiveScaling = ScalingQualityPreset::Balanced;
    std::string m_lastScalingReason;

    // A few seconds of sustained overload before stepping down (avoid
    // reacting to a single noisy spike); much longer of sustained healthy
    // margin before stepping back up (avoid oscillating at the boundary).
    static constexpr int kStepDownSamples = 5;
    static constexpr int kStepUpSamples = 90;

    static constexpr double kGpuUtilOverloadPercent = 90.0;
    static constexpr double kGpuTimeBudgetFraction = 0.80; // totalGpuMs vs. one display-refresh frame budget
};

} // namespace lsalt
