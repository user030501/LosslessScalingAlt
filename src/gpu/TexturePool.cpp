#include "TexturePool.h"
#include "../core/Logger.h"

#include <algorithm>

namespace lsalt {

namespace {
uint64_t BytesPerPixel(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_R32_FLOAT:
            return 4;
        case DXGI_FORMAT_R16G16_FLOAT:
            return 4;
        case DXGI_FORMAT_R16_FLOAT:
            return 2;
        default:
            return 4; // reasonable estimate default -- this is a UI/diagnostic estimate, not a hard guarantee
    }
}
}

bool TexturePool::Acquire(ID3D11Device* device, uint32_t width, uint32_t height, DXGI_FORMAT format, UINT bindFlags,
                           Microsoft::WRL::ComPtr<ID3D11Texture2D>& outTex,
                           Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSrv,
                           Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>& outUav, bool* outReused) {
    for (auto it = m_pooled.begin(); it != m_pooled.end(); ++it) {
        if (it->width == width && it->height == height && it->format == format && it->bindFlags == bindFlags) {
            outTex = it->tex;
            outSrv = it->srv;
            outUav = it->uav;
            m_pooled.erase(it);
            ++m_totalReuses;
            if (outReused) *outReused = true;
            return true;
        }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = std::max(width, 1u);
    desc.Height = std::max(height, 1u);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = bindFlags;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, tex.GetAddressOf()))) {
        ++m_totalAllocationFailures;
        Logger::Instance().Error("TexturePool", "CreateTexture2D failed (out of memory, or an invalid/too-large "
                                                   "request -- see the caller's own resolution clamping).");
        return false;
    }
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    if (bindFlags & D3D11_BIND_SHADER_RESOURCE) {
        if (FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) return false;
    }
    Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
    if (bindFlags & D3D11_BIND_UNORDERED_ACCESS) {
        if (FAILED(device->CreateUnorderedAccessView(tex.Get(), nullptr, uav.GetAddressOf()))) return false;
    }

    outTex = tex;
    outSrv = srv;
    outUav = uav;
    ++m_totalAllocations;
    if (outReused) *outReused = false;
    return true;
}

void TexturePool::Release(Microsoft::WRL::ComPtr<ID3D11Texture2D> tex, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv,
                            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav, uint32_t width, uint32_t height,
                            DXGI_FORMAT format, UINT bindFlags) {
    if (!tex) return;

    Entry e;
    e.tex = std::move(tex);
    e.srv = std::move(srv);
    e.uav = std::move(uav);
    e.width = width;
    e.height = height;
    e.format = format;
    e.bindFlags = bindFlags;
    e.bytes = static_cast<uint64_t>(width) * height * BytesPerPixel(format);

    m_pooled.push_back(std::move(e));
    // Bounded capacity: evict the oldest entry rather than let the pool
    // (and the real GPU memory it holds) grow without limit.
    while (m_pooled.size() > m_capacity) {
        m_pooled.erase(m_pooled.begin());
    }
}

void TexturePool::Clear() { m_pooled.clear(); }

void TexturePool::SetCapacity(size_t capacity) {
    m_capacity = capacity;
    while (m_pooled.size() > m_capacity) m_pooled.erase(m_pooled.begin());
}

void TexturePool::TrimToBytes(uint64_t maxBytes) {
    while (!m_pooled.empty() && EstimatedBytesInPool() > maxBytes) {
        m_pooled.erase(m_pooled.begin());
    }
}

uint64_t TexturePool::EstimatedBytesInPool() const {
    uint64_t total = 0;
    for (const auto& e : m_pooled) total += e.bytes;
    return total;
}

} // namespace lsalt
