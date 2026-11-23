#include "ShaderCompiler.h"
#include "../core/Logger.h"

#include <d3dcompiler.h>
#include <fstream>
#include <vector>
#include <sys/stat.h>

namespace lsalt {

namespace {
std::string NarrowFromWide(const std::wstring& w) {
    return std::string(w.begin(), w.end());
}

std::wstring CachePathFor(const std::wstring& hlslPath, const std::string& entryPoint) {
    return hlslPath + L"." + std::wstring(entryPoint.begin(), entryPoint.end()) + L".cso";
}

bool FileNewerThan(const std::wstring& a, const std::wstring& b) {
    struct _stat statA{}, statB{};
    if (_wstat(a.c_str(), &statA) != 0) return false;
    if (_wstat(b.c_str(), &statB) != 0) return true; // b doesn't exist -> a is "newer"
    return statA.st_mtime > statB.st_mtime;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamsize size = f.tellg();
    if (size <= 0) return false;
    out.resize(static_cast<size_t>(size));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), size);
    return f.good() || f.eof();
}

void WriteFileBytes(const std::wstring& path, const void* data, size_t size) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return;
    f.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
}
} // namespace

namespace {
// Shared by all three CompileXShader entry points below: finds/validates a
// cached .cso, or compiles fresh and writes the cache. `target` is the
// HLSL shader-model profile string, e.g. "cs_5_0", "vs_5_0", "ps_5_0".
bool CompileOrLoadCachedBytecode(const std::wstring& hlslPath, const std::string& entryPoint,
                                   const char* target, std::vector<uint8_t>& bytecode) {
    const std::wstring cachePath = CachePathFor(hlslPath, entryPoint);
    bytecode.clear();

    bool haveCached = false;
    if (ReadFileBytes(cachePath, bytecode)) {
        struct _stat srcStat{}, cacheStat{};
        if (_wstat(hlslPath.c_str(), &srcStat) == 0 && _wstat(cachePath.c_str(), &cacheStat) == 0 &&
            cacheStat.st_mtime >= srcStat.st_mtime) {
            haveCached = true;
        }
    }
    if (haveCached) return true;

    bytecode.clear();
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3DCompileFromFile(
        hlslPath.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(), target, flags, 0, blob.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        std::string msg = "Failed to compile " + NarrowFromWide(hlslPath) + " [" + entryPoint + "], hr=" +
                           std::to_string(hr);
        if (errorBlob) {
            msg += " : ";
            msg.append(reinterpret_cast<const char*>(errorBlob->GetBufferPointer()), errorBlob->GetBufferSize());
        }
        Logger::Instance().Error("ShaderCompiler", msg);
        return false;
    }

    bytecode.assign(reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()),
                     reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
    WriteFileBytes(cachePath, bytecode.data(), bytecode.size());
    Logger::Instance().Info("ShaderCompiler", "Compiled " + NarrowFromWide(hlslPath) + " [" + entryPoint + "]");
    return true;
}
} // namespace

Microsoft::WRL::ComPtr<ID3D11ComputeShader> ShaderCompiler::CompileComputeShader(
    ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint) {
    std::vector<uint8_t> bytecode;
    if (!CompileOrLoadCachedBytecode(hlslPath, entryPoint, "cs_5_0", bytecode)) return nullptr;

    Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader;
    HRESULT hr = device->CreateComputeShader(bytecode.data(), bytecode.size(), nullptr, shader.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Instance().Error("ShaderCompiler",
            "CreateComputeShader failed for " + NarrowFromWide(hlslPath) + " [" + entryPoint + "], hr=" + std::to_string(hr));
        return nullptr;
    }
    return shader;
}

Microsoft::WRL::ComPtr<ID3D11VertexShader> ShaderCompiler::CompileVertexShader(
    ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint) {
    std::vector<uint8_t> bytecode;
    if (!CompileOrLoadCachedBytecode(hlslPath, entryPoint, "vs_5_0", bytecode)) return nullptr;

    Microsoft::WRL::ComPtr<ID3D11VertexShader> shader;
    HRESULT hr = device->CreateVertexShader(bytecode.data(), bytecode.size(), nullptr, shader.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Instance().Error("ShaderCompiler",
            "CreateVertexShader failed for " + NarrowFromWide(hlslPath) + " [" + entryPoint + "], hr=" + std::to_string(hr));
        return nullptr;
    }
    return shader;
}

Microsoft::WRL::ComPtr<ID3D11PixelShader> ShaderCompiler::CompilePixelShader(
    ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint) {
    std::vector<uint8_t> bytecode;
    if (!CompileOrLoadCachedBytecode(hlslPath, entryPoint, "ps_5_0", bytecode)) return nullptr;

    Microsoft::WRL::ComPtr<ID3D11PixelShader> shader;
    HRESULT hr = device->CreatePixelShader(bytecode.data(), bytecode.size(), nullptr, shader.GetAddressOf());
    if (FAILED(hr)) {
        Logger::Instance().Error("ShaderCompiler",
            "CreatePixelShader failed for " + NarrowFromWide(hlslPath) + " [" + entryPoint + "], hr=" + std::to_string(hr));
        return nullptr;
    }
    return shader;
}

} // namespace lsalt
