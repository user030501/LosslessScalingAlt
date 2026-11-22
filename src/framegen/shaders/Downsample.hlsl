// Luma extraction + mip-pyramid downsampling used by MotionEstimator to
// build the coarse-to-fine image pyramid that block matching searches over.

cbuffer DownsampleParams : register(b0) {
    uint2 g_outputSize;
    uint2 g_inputSize;
};

Texture2D<float4> g_colorInput : register(t0);
RWTexture2D<float> g_lumaOutput : register(u0);

[numthreads(8, 8, 1)]
void CSLumaExtract(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_outputSize.x || id.y >= g_outputSize.y) return;
    float4 c = g_colorInput[id.xy];
    // Standard BT.601 luma weights.
    float luma = dot(c.rgb, float3(0.299f, 0.587f, 0.114f));
    g_lumaOutput[id.xy] = luma;
}

Texture2D<float> g_lumaInput : register(t1);

[numthreads(8, 8, 1)]
void CSDownsample2x(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_outputSize.x || id.y >= g_outputSize.y) return;

    uint2 src = id.xy * 2;
    uint2 maxSrc = g_inputSize - uint2(1, 1);
    uint2 s00 = min(src, maxSrc);
    uint2 s10 = min(src + uint2(1, 0), maxSrc);
    uint2 s01 = min(src + uint2(0, 1), maxSrc);
    uint2 s11 = min(src + uint2(1, 1), maxSrc);

    float avg = (g_lumaInput[s00] + g_lumaInput[s10] + g_lumaInput[s01] + g_lumaInput[s11]) * 0.25f;
    g_lumaOutput[id.xy] = avg;
}
