// Motion-compensated frame interpolation at t=0.5. For each output pixel:
//   - forward-warp Frame A toward t=0.5 using half the A->B vector,
//   - backward-warp Frame B toward t=0.5 using half the B->A vector,
//   - blend the two warped samples,
//   - and where the occlusion mask flags the block as inconsistent, fall
//     back to a plain (unwarped) cross-dissolve of A and B instead -- never
//     frame duplication.
// Motion vectors and the occlusion mask are sampled with a bilinear filter
// across the block grid so the warp is smooth rather than blocky.

cbuffer InterpolationParams : register(b0) {
    uint2 g_outputSize;
    uint2 g_numBlocks;
    uint g_blockSize;
    uint g_padding0;
    uint2 g_padding1;
};

Texture2D<float4> g_frameA : register(t0);
Texture2D<float4> g_frameB : register(t1);
Texture2D<float2> g_fwdMV : register(t2); // A -> B, block-grid resolution
Texture2D<float2> g_bwdMV : register(t3); // B -> A, block-grid resolution
Texture2D<float> g_occlusion : register(t4); // block-grid resolution
RWTexture2D<unorm float4> g_output : register(u0);

SamplerState g_linearClamp : register(s0);

[numthreads(8, 8, 1)]
void CSInterpolate(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_outputSize.x || id.y >= g_outputSize.y) return;

    float2 outSize = float2(g_outputSize);
    float2 pixel = float2(id.xy) + 0.5f;
    float2 blockGridExtent = float2(g_numBlocks) * float(g_blockSize);
    float2 blockUV = pixel / max(blockGridExtent, float2(1.0f, 1.0f));

    float2 fwd = g_fwdMV.SampleLevel(g_linearClamp, blockUV, 0).xy;
    float2 bwd = g_bwdMV.SampleLevel(g_linearClamp, blockUV, 0).xy;
    float occ = g_occlusion.SampleLevel(g_linearClamp, blockUV, 0).x;

    float2 uvA = saturate((pixel - 0.5f * fwd) / outSize);
    float2 uvB = saturate((pixel + 0.5f * bwd) / outSize);
    float2 uvPlain = pixel / outSize;

    float4 colorFromA = g_frameA.SampleLevel(g_linearClamp, uvA, 0);
    float4 colorFromB = g_frameB.SampleLevel(g_linearClamp, uvB, 0);
    float4 warped = 0.5f * colorFromA + 0.5f * colorFromB;

    float4 plainA = g_frameA.SampleLevel(g_linearClamp, uvPlain, 0);
    float4 plainB = g_frameB.SampleLevel(g_linearClamp, uvPlain, 0);
    float4 blended = 0.5f * plainA + 0.5f * plainB;

    g_output[id.xy] = lerp(warped, blended, occ);
}
