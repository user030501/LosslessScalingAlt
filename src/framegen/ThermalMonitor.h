#pragma once
#include <wrl/client.h>
#include <cstdint>
#include <deque>

struct IWbemLocator;
struct IWbemServices;

namespace lsalt {

// Phase 3 thermal-aware signal for DynamicPerformanceManager.
//
// Real CPU temperature is attempted, best-effort, via WMI's
// MSAcpi_ThermalZoneTemperature (root\WMI namespace). This is a genuine
// hardware reading when it works, but on many consumer/OEM laptops --
// including ones this app targets -- the firmware either doesn't expose
// an ACPI thermal zone to Windows at all, or restricts it; that is a
// normal, expected outcome here, not a bug, and is reported honestly as
// "unavailable" rather than guessed at.
//
// GPU temperature has no portable, vendor-neutral D3D11/DXGI query at
// all -- real GPU temperature requires NVAPI (NVIDIA) or ADL (AMD), vendor
// SDKs this project does not depend on and will not silently fake a
// number for. ThermalSample::gpuTempValid is therefore always false; this
// is a documented, permanent limitation, not a TODO.
//
// When temperature is unavailable, the brief's own explicitly-permitted
// fallback applies: a "thermal pressure" signal derived purely from
// sustained GPU/CPU utilization (real PDH/GetSystemTimes values the app
// already measures) and frame-time trends, standing in for "is this
// system under sustained load that would eventually heat-soak it" without
// claiming an actual temperature.
struct ThermalSample {
    double cpuTempCelsius = 0.0;
    bool cpuTempValid = false;
    bool gpuTempValid = false; // always false -- see class comment above

    double utilizationPressure = 0.0; // 0..1, from real CPU/GPU utilization
    bool sustainedHighLoad = false;   // utilizationPressure stayed >= threshold for the trend window
};

class ThermalMonitor {
public:
    ThermalMonitor();
    ~ThermalMonitor();

    // Call once per Performance Monitor sample (not necessarily every UI
    // frame) with the latest real CPU/GPU utilization percentages (0..100).
    // WMI temperature is internally throttled to ~1 Hz regardless of call
    // frequency -- temperature doesn't change fast enough to justify
    // polling it every frame, and doing so would itself add CPU overhead,
    // which Phase 3 is supposed to be reducing, not adding.
    ThermalSample Sample(double cpuPercent, double gpuPercent, bool gpuPercentValid);

    bool WmiAvailable() const { return m_wmiAvailable; }

private:
    bool EnsureWmiConnection();
    bool QueryWmiCpuTemperatureOnce(double& outCelsius);

    Microsoft::WRL::ComPtr<IWbemLocator> m_locator;
    Microsoft::WRL::ComPtr<IWbemServices> m_services;
    bool m_comInitializedHere = false;
    bool m_wmiProbed = false;
    bool m_wmiAvailable = false;

    double m_lastCpuTempCelsius = 0.0;
    bool m_lastCpuTempValid = false;
    double m_lastWmiPollSeconds = -1.0;

    std::deque<double> m_loadHistory;
    static constexpr size_t kTrendWindowSamples = 30;
    static constexpr double kHighLoadThreshold = 0.85;
};

} // namespace lsalt
