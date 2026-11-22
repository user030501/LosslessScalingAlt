#include "ThermalMonitor.h"
#include "../core/Logger.h"

#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <algorithm>

#pragma comment(lib, "wbemuuid.lib")

namespace lsalt {

namespace {
double NowSeconds() {
    static const double freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart) / freq;
}
} // namespace

ThermalMonitor::ThermalMonitor() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_comInitializedHere = SUCCEEDED(hr);
    if (hr == RPC_E_CHANGED_MODE) {
        // COM was already initialized on this thread with a different
        // concurrency model by something else in the process; we don't own
        // it, so we won't uninitialize it either.
        Logger::Instance().Info("ThermalMonitor", "COM already initialized on this thread; reusing it.");
    } else if (FAILED(hr)) {
        Logger::Instance().Warn("ThermalMonitor",
            "CoInitializeEx failed, hr=" + std::to_string(hr) + "; CPU temperature via WMI will be unavailable.");
    }
}

ThermalMonitor::~ThermalMonitor() {
    m_services.Reset();
    m_locator.Reset();
    if (m_comInitializedHere) CoUninitialize();
}

bool ThermalMonitor::EnsureWmiConnection() {
    if (m_services) return true;

    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                   reinterpret_cast<void**>(m_locator.ReleaseAndGetAddressOf()));
    if (FAILED(hr) || !m_locator) return false;

    // MSAcpi_ThermalZoneTemperature lives in ROOT\WMI, not the more common
    // ROOT\CIMV2 -- it will simply not be found if queried against CIMV2.
    hr = m_locator->ConnectServer(_bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr,
                                   m_services.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !m_services) {
        m_locator.Reset();
        return false;
    }

    hr = CoSetProxyBlanket(m_services.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                            RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        m_services.Reset();
        m_locator.Reset();
        return false;
    }

    return true;
}

bool ThermalMonitor::QueryWmiCpuTemperatureOnce(double& outCelsius) {
    if (!EnsureWmiConnection()) return false;

    Microsoft::WRL::ComPtr<IEnumWbemClassObject> enumerator;
    HRESULT hr = m_services->ExecQuery(
        _bstr_t(L"WQL"), _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, enumerator.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !enumerator) return false;

    Microsoft::WRL::ComPtr<IWbemClassObject> obj;
    ULONG returned = 0;
    if (enumerator->Next(2000, 1, obj.ReleaseAndGetAddressOf(), &returned) != S_OK || returned == 0) {
        // Not an error: this is the normal "no ACPI thermal zone exposed"
        // case on many laptops (OEM firmware restriction or simply absent).
        return false;
    }

    VARIANT val;
    VariantInit(&val);
    bool ok = false;
    if (SUCCEEDED(obj->Get(L"CurrentTemperature", 0, &val, nullptr, nullptr)) && val.vt == VT_I4) {
        // ACPI reports this in tenths of a degree Kelvin.
        outCelsius = (static_cast<double>(val.lVal) / 10.0) - 273.15;
        ok = true;
    }
    VariantClear(&val);
    return ok;
}

ThermalSample ThermalMonitor::Sample(double cpuPercent, double gpuPercent, bool gpuPercentValid) {
    ThermalSample sample;

    const double now = NowSeconds();
    const bool firstProbe = !m_wmiProbed;
    if (firstProbe || (now - m_lastWmiPollSeconds) >= 1.0) {
        double celsius = 0.0;
        const bool ok = QueryWmiCpuTemperatureOnce(celsius);
        if (firstProbe) {
            m_wmiProbed = true;
            m_wmiAvailable = ok;
            Logger::Instance().Info("ThermalMonitor", ok
                ? "Real CPU temperature available via WMI MSAcpi_ThermalZoneTemperature."
                : "CPU temperature not available via WMI on this system/firmware/driver; falling back to the "
                  "utilization/frame-time trend as the thermal-pressure signal (GPU temperature is never "
                  "available -- that requires vendor-specific NVAPI/ADL, which this app does not use).");
        }
        m_lastCpuTempValid = ok;
        m_lastCpuTempCelsius = celsius;
        m_lastWmiPollSeconds = now;
        if (!ok) m_wmiAvailable = false; // stop treating it as available if it starts failing mid-run
    }

    sample.cpuTempValid = m_lastCpuTempValid;
    sample.cpuTempCelsius = m_lastCpuTempCelsius;
    sample.gpuTempValid = false;

    const double combined = gpuPercentValid ? std::max(cpuPercent, gpuPercent) : cpuPercent;
    const double pressure = std::min(1.0, std::max(0.0, combined / 100.0));
    m_loadHistory.push_back(pressure);
    while (m_loadHistory.size() > kTrendWindowSamples) m_loadHistory.pop_front();

    sample.utilizationPressure = pressure;
    if (m_loadHistory.size() >= kTrendWindowSamples) {
        sample.sustainedHighLoad = std::all_of(m_loadHistory.begin(), m_loadHistory.end(),
                                                [](double p) { return p >= kHighLoadThreshold; });
    }

    return sample;
}

} // namespace lsalt
