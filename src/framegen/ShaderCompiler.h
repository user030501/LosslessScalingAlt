#pragma once
#include <wrl/client.h>
#include <d3d11.h>
#include <string>

namespace lsalt {

// Compiles HLSL compute shaders at runtime via D3DCompiler and caches the
// resulting bytecode next to the source file (<name>.cso) so subsequent
// launches skip recompilation unless the .hlsl source changed. Every
// failure is logged with the full D3DCompile error blob -- shaders never
// silently fail to load.
class ShaderCompiler {
public:
    // hlslPath: path to the .hlsl source file.
    // entryPoint: e.g. "CSMain".
    // Returns a created compute shader, or null on failure (logged).
    static Microsoft::WRL::ComPtr<ID3D11ComputeShader> CompileComputeShader(
        ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint);

    // Same caching/compilation machinery, for the graphics pipeline
    // (OutputPresenter's blit shader). No input layout is produced/needed:
    // both are meant for a fullscreen-triangle technique driven purely by
    // SV_VertexID, with no application-defined per-vertex input.
    static Microsoft::WRL::ComPtr<ID3D11VertexShader> CompileVertexShader(
        ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint);
    static Microsoft::WRL::ComPtr<ID3D11PixelShader> CompilePixelShader(
        ID3D11Device* device, const std::wstring& hlslPath, const std::string& entryPoint);
};

} // namespace lsalt
