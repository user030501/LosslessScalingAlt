#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <cstdint>
#include <vector>

namespace lsalt {

// Phase 7: a small reuse pool for same-shaped D3D11 textures, so toggling
// between a bounded set of resolutions (cycling quality presets, resizing
// a window back and forth across common sizes) doesn't repeatedly
// allocate/free real GPU memory -- CreateTexture2D/driver allocation has
// real cost, especially on entry-level/shared-memory GPUs like the UHD
// 620. Deliberately simple and bounded: not a general-purpose allocator,
// just "don't throw away a texture we'll probably want again soon."
class TexturePool {
public:
    explicit TexturePool(size_t capacity = 4) : m_capacity(capacity) {}

    // Returns a texture+SRV+UAV matching the given shape: reused from the
    // pool if an exact (format, width, height, bind flags) match is
    // sitting in it, freshly allocated otherwise. `outReused`, if given,
    // reports which happened (for diagnostics/tests).
    bool Acquire(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, UINT bindFlags,
                 Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTex,
                 Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSrv,
                 Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& outUav,
                 bool* outReused = nullptr);

    // Stashes a texture in the pool instead of releasing it immediately.
    // The caller must not use these ComPtrs again directly after this
    // call -- re-Acquire a matching shape to get them back.
    void Release(Microsoft::WRL::ComPtr<ID3D11Texture2D> tex, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv,
                 Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav, uint32_t width, uint32_t height,
                 DXGI_FORMAT format, UINT bindFlags);

    // Drops every pooled (not currently acquired) texture -- call on
    // device loss, or to reclaim memory under VRAM pressure.
    void Clear();
    void SetCapacity(size_t capacity);
    void TrimToBytes(uint64_t maxBytes);

    uint64_t EstimatedBytesInPool() const;
    uint64_t TotalAllocationsEver() const { return m_totalAllocations; }
    uint64_t TotalReusesEver() const { return m_totalReuses; }
    size_t PooledCount() const { return m_pooled.size(); }

private:
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
        uint32_t width = 0, height = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        UINT bindFlags = 0;
        uint64_t bytes = 0;
    };
    std::vector<Entry> m_pooled;
    size_t m_capacity;
    uint64_t m_totalAllocations = 0;
    uint64_t m_totalReuses = 0;
};

} // namespace lsalt
