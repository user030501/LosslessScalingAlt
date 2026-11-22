#include "GpuDeviceManager.h"
#include "../core/Logger.h"
#include <d3d11.h>
#include <dxgi1_4.h>

using Microsoft::WRL::ComPtr;

namespace lsalt {

namespace {
// Creates a throwaway device on the given adapter solely to query real
// capability bits (feature level, typed UAV store for R8G8B8A8_UNORM --
// the format the FrameGen interpolation shader writes). This mirrors
// FrameGenEngineD3D11::DetectCapabilities exactly, run once per adapter at
// enumeration time so the GPU Settings panel and auto-select scoring can
// show/use real per-adapter numbers instead of only probing the adapter
// that happened to be chosen for the app's own UI device.
void ProbeAdapterCapabilities(IDXGIAdapter1* adapter, GpuAdapterInfo& info) {
    ComPtr<ID3D11Device> probeDevice;
    D3D_FEATURE_LEVEL achieved{};
    HRESULT hr = D3D11CreateDevice(
        adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, probeDevice.GetAddressOf(), &achieved, nullptr);
    if (FAILED(hr)) return;

    info.featureLevelMajor = (achieved >> 12) & 0xF;
    info.featureLevelMinor = (achieved >> 8) & 0xF;
    info.supportsComputeShaders = achieved >= D3D_FEATURE_LEVEL_11_0;
    if (!info.supportsComputeShaders) return;

    D3D11_FEATURE_DATA_FORMAT_SUPPORT2 fmt2{};
    fmt2.InFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (SUCCEEDED(probeDevice->CheckFeatureSupport(D3D11_FEATURE_FORMAT_SUPPORT2, &fmt2, sizeof(fmt2)))) {
        info.supportsTypedUavStore = (fmt2.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
    }
}
} // namespace

bool GpuDeviceManager::EnumerateAdapters() {
    m_adapters.clear();
    m_rawAdapters.clear();

    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) {
        Logger::Instance().Error("GpuDeviceManager", "CreateDXGIFactory1 failed.");
        return false;
    }
    m_factory = factory;

    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);

        // Skip the Microsoft Basic Render software adapter for the selectable
        // list; it is not a real processing GPU.
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        GpuAdapterInfo info;
        info.index = static_cast<int32_t>(m_adapters.size());
        info.description = desc.Description;
        info.vendorId = desc.VendorId;
        info.dedicatedVideoMemoryBytes = desc.DedicatedVideoMemory;
        info.isHardware = true;

        // Probe D3D11 support without creating a device we keep around.
        D3D_FEATURE_LEVEL achieved{};
        HRESULT probeHr = D3D11CreateDevice(
            adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, nullptr, &achieved, nullptr);
        info.supportsD3D11 = SUCCEEDED(probeHr);
        if (info.supportsD3D11) {
            ProbeAdapterCapabilities(adapter.Get(), info);
        }

        m_adapters.push_back(info);
        m_rawAdapters.push_back(adapter);

        Logger::Instance().Info("GpuDeviceManager",
            "Found adapter: " + std::string(info.description.begin(), info.description.end()) +
            (info.supportsD3D11 ? " [D3D11 OK]" : " [D3D11 unsupported]") +
            (info.supportsComputeShaders ? ", compute shaders OK" : "") +
            (info.supportsTypedUavStore ? ", typed UAV store OK" : "") +
            ", dedicated VRAM " + std::to_string(info.dedicatedVideoMemoryBytes / (1024 * 1024)) + " MB");
    }

    return !m_adapters.empty();
}

int GpuDeviceManager::SelectBestAdapterIndex() const {
    int bestIndex = -1;
    int64_t bestScore = -1;
    for (const auto& info : m_adapters) {
        if (!info.isHardware || !info.supportsD3D11) continue;

        // Hard requirements for this app's actual compute pipeline: without
        // both of these the FrameGen engine reports itself unsupported on
        // that adapter anyway (see FrameGenEngineD3D11::DetectCapabilities),
        // so an adapter missing either is never preferred over one that has
        // them, regardless of VRAM.
        int64_t score = 0;
        score += info.supportsComputeShaders ? (1LL << 40) : 0;
        score += info.supportsTypedUavStore ? (1LL << 39) : 0;
        // Tie-breaker among capable adapters: more dedicated VRAM, a real
        // measured value from DXGI, not an assumption.
        score += static_cast<int64_t>(info.dedicatedVideoMemoryBytes);

        if (score > bestScore) {
            bestScore = score;
            bestIndex = info.index;
        }
    }
    return bestIndex;
}

bool GpuDeviceManager::CreateDeviceOnAdapter(int preferredAdapterIndex) {
    int effectiveIndex = preferredAdapterIndex;
    if (effectiveIndex < 0) {
        // "Auto": pick the adapter our own capability probe found most
        // suitable (see SelectBestAdapterIndex) instead of leaving the
        // choice to DXGI's implementation-defined default, which on a
        // hybrid-graphics laptop (e.g. this app's Intel UHD 620 + NVIDIA
        // MX150 target) is not guaranteed to be the more capable GPU.
        effectiveIndex = SelectBestAdapterIndex();
        if (effectiveIndex >= 0) {
            Logger::Instance().Info("GpuDeviceManager",
                "Auto-selected adapter index " + std::to_string(effectiveIndex) + " as most suitable.");
        } else {
            Logger::Instance().Warn("GpuDeviceManager",
                "No adapter passed capability scoring; falling back to DXGI's default adapter.");
        }
    }

    IDXGIAdapter1* adapter = nullptr;
    if (effectiveIndex >= 0 && effectiveIndex < static_cast<int>(m_rawAdapters.size())) {
        adapter = m_rawAdapters[effectiveIndex].Get();
    }

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
    };

    D3D_FEATURE_LEVEL achieved{};
    HRESULT hr = D3D11CreateDevice(
        adapter,
        adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        m_device.ReleaseAndGetAddressOf(), &achieved, m_context.ReleaseAndGetAddressOf());

    // DXGI_ERROR_SDK_COMPONENT_MISSING: the D3D11 debug layer (D3D11_CREATE_DEVICE_DEBUG)
    // requires the optional "Graphics Tools" Windows feature, which most
    // machines -- including ones this app targets -- don't have installed.
    // Retry without it rather than failing outright on every Debug build.
    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
        Logger::Instance().Warn("GpuDeviceManager",
            "D3D11CreateDevice with the debug layer failed (hr=" + std::to_string(hr) +
            "); the 'Graphics Tools' optional feature is likely not installed. Retrying without it.");
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            adapter,
            adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr, flags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            m_device.ReleaseAndGetAddressOf(), &achieved, m_context.ReleaseAndGetAddressOf());
    }

    if (FAILED(hr)) {
        Logger::Instance().Error("GpuDeviceManager", "D3D11CreateDevice failed, hr=" + std::to_string(hr));
        return false;
    }

    // Record which adapter is actually active even under "Auto"
    // (preferredAdapterIndex == -1): leaving this at -1 would silently
    // break VRAM-usage queries (QueryVideoMemoryUsage rejects a negative
    // index) and the GPU Settings panel's "Active now" indicator whenever
    // Auto-select picked a real adapter.
    m_activeAdapterIndex = effectiveIndex;
    Logger::Instance().Info("GpuDeviceManager", "D3D11 device created (feature level " + std::to_string(achieved) + ").");
    return true;
}

bool GpuDeviceManager::QueryVideoMemoryUsage(int adapterIndex, uint64_t& outUsedBytes, uint64_t& outBudgetBytes) const {
    if (adapterIndex < 0 || adapterIndex >= static_cast<int>(m_rawAdapters.size())) return false;

    ComPtr<IDXGIAdapter3> adapter3;
    if (FAILED(m_rawAdapters[adapterIndex].As(&adapter3))) return false;

    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (FAILED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) return false;

    outUsedBytes = info.CurrentUsage;
    outBudgetBytes = info.Budget;
    return true;
}

} // namespace lsalt
