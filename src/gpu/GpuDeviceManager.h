#pragma once
#include <vector>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include "../core/Types.h"

namespace lsalt {

// Enumerates real physical GPUs via DXGI (so the "GPU Settings" panel shows
// this machine's actual NVIDIA MX150 + Intel UHD 620, not placeholders) and
// owns the D3D11 device/context used to render the app's own UI. The
// Scaling/FrameGen engines (Phase 3/4) will request their own device on the
// adapter the user picked here, via CreateDeviceOnAdapter().
class GpuDeviceManager {
public:
    bool EnumerateAdapters();
    const std::vector<GpuAdapterInfo>& Adapters() const { return m_adapters; }

    // Creates the D3D11 device + immediate context used for the UI swapchain.
    // preferredAdapterIndex of -1 means "automatically pick the most
    // suitable compatible adapter" (see SelectBestAdapterIndex) rather than
    // leaving the choice to DXGI's implementation-defined default, which on
    // a hybrid-graphics laptop is not guaranteed to be the more capable GPU.
    bool CreateDeviceOnAdapter(int preferredAdapterIndex);

    // Scores every enumerated adapter by real, measured capability
    // (compute shader + typed UAV store support, then dedicated VRAM as a
    // tie-breaker) and returns the index of the best one, or -1 if none
    // support what this app's compute pipeline needs. Exposed so the GPU
    // Settings panel can show which adapter "Auto" would actually pick.
    int SelectBestAdapterIndex() const;

    Microsoft::WRL::ComPtr<ID3D11Device> Device() const { return m_device; }
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> Context() const { return m_context; }
    Microsoft::WRL::ComPtr<IDXGIFactory2> Factory() const { return m_factory; }

    int ActiveAdapterIndex() const { return m_activeAdapterIndex; }

    // Real, not estimated: queries IDXGIAdapter3::QueryVideoMemoryInfo when
    // available. Returns false if the OS/driver doesn't expose it.
    bool QueryVideoMemoryUsage(int adapterIndex, uint64_t& outUsedBytes, uint64_t& outBudgetBytes) const;

private:
    Microsoft::WRL::ComPtr<IDXGIFactory2> m_factory;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    std::vector<GpuAdapterInfo> m_adapters;
    std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> m_rawAdapters;
    int m_activeAdapterIndex = -1;
};

} // namespace lsalt
