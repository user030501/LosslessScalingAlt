// Forward/backward motion-vector consistency check. A block whose A->B
// vector and the corresponding B->A vector don't cancel out is either
// occluded (visible in only one of the two frames) or sitting on bad
// motion-estimation data; either way, warping it is unsafe and the
// interpolator should fall back to plain cross-dissolve blending there.

cbuffer OcclusionParams : register(b0) {
    uint2 g_numBlocks;
    uint g_blockSize;
    float g_consistencyThreshold; // in pixels; vector-sum error beyond this is "occluded"
};

Texture2D<float2> g_fwdMV : register(t0); // A -> B
Texture2D<float2> g_bwdMV : register(t1); // B -> A
RWTexture2D<float> g_occlusionOut : register(u0); // 0 = consistent, 1 = occluded

[numthreads(8, 8, 1)]
void CSOcclusionMask(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_numBlocks.x || id.y >= g_numBlocks.y) return;

    uint2 c = id.xy;
    float2 vf = g_fwdMV[c];

    int2 targetBlock = int2(c) + int2(round(vf / max(float(g_blockSize), 1.0f)));
    targetBlock = clamp(targetBlock, int2(0, 0), int2(g_numBlocks) - int2(1, 1));
    float2 vb = g_bwdMV[uint2(targetBlock)];

    float err = length(vf + vb);
    float occ = saturate((err - g_consistencyThreshold) / max(g_consistencyThreshold, 0.0001f));
    g_occlusionOut[c] = occ;
}
