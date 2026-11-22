#include "GpuDeviceManager.h"
#include "../core/Logger.h"
#include <d3d11.h>
#include <dxgi1_4.h>

using Microsoft::WRL::ComPtr;

namespace lsalt {

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

        m_adapters.push_back(info);
        m_rawAdapters.push_back(adapter);

        Logger::Instance().Info("GpuDeviceManager",
            "Found adapter: " + std::string(info.description.begin(), info.description.end()) +
            (info.supportsD3D11 ? " [D3D11 OK]" : " [D3D11 unsupported]"));
    }

    return !m_adapters.empty();
}

bool GpuDeviceManager::CreateDeviceOnAdapter(int preferredAdapterIndex) {
    IDXGIAdapter1* adapter = nullptr;
    if (preferredAdapterIndex >= 0 && preferredAdapterIndex < static_cast<int>(m_rawAdapters.size())) {
        adapter = m_rawAdapters[preferredAdapterIndex].Get();
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

    m_activeAdapterIndex = preferredAdapterIndex;
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
