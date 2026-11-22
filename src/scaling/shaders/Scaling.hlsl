// Real image-scaling compute shaders: Nearest Neighbor, Bilinear (hardware
// linear filtering), Bicubic (Keys' cubic convolution, A=-0.5 -- the same
// public-domain kernel used by most "bicubic" image resampling), Lanczos
// (windowed sinc, a=2) and Integer Scale (exact pixel replication at the
// largest whole-number factor that fits, centered and letterboxed).
//
// All entry points write the FULL destination texture: pixels outside the
// (possibly letterboxed, for aspect-ratio preservation) content viewport
// are written as black rather than left undefined, so preserving aspect
// ratio never leaves garbage in the border.

cbuffer ScalingParams : register(b0) {
    uint2 g_dstSize;
    uint2 g_srcSize;
    uint2 g_viewportOrigin; // top-left of the content area within g_dstSize
    uint2 g_viewportSize;   // size of that content area (== g_dstSize if not letterboxed)
    uint g_integerFactor;   // used only by CSIntegerScale
    uint3 g_padding;
};

Texture2D<float4> g_src : register(t0);
RWTexture2D<unorm float4> g_dst : register(u0);
SamplerState g_linearClamp : register(s0);

bool InViewport(uint2 id) {
    return id.x >= g_viewportOrigin.x && id.x < g_viewportOrigin.x + g_viewportSize.x &&
           id.y >= g_viewportOrigin.y && id.y < g_viewportOrigin.y + g_viewportSize.y;
}

float2 ViewportUV(uint2 id) {
    float2 local = float2(id - g_viewportOrigin) + 0.5f;
    return local / float2(g_viewportSize);
}

[numthreads(8, 8, 1)]
void CSNearest(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstSize.x || id.y >= g_dstSize.y) return;
    if (!InViewport(id.xy)) { g_dst[id.xy] = float4(0, 0, 0, 1); return; }

    float2 uv = ViewportUV(id.xy);
    uint2 srcPixel = min(uint2(uv * float2(g_srcSize)), g_srcSize - uint2(1, 1));
    g_dst[id.xy] = g_src[srcPixel];
}

[numthreads(8, 8, 1)]
void CSBilinear(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstSize.x || id.y >= g_dstSize.y) return;
    if (!InViewport(id.xy)) { g_dst[id.xy] = float4(0, 0, 0, 1); return; }

    float2 uv = ViewportUV(id.xy);
    g_dst[id.xy] = g_src.SampleLevel(g_linearClamp, uv, 0);
}

// Keys' cubic convolution kernel, A = -0.5 (the standard "bicubic" used by
// most image editors/resamplers; public-domain math, not vendor IP).
float CubicWeight(float x) {
    const float a = -0.5f;
    float ax = abs(x);
    if (ax < 1.0f) return (a + 2.0f) * ax * ax * ax - (a + 3.0f) * ax * ax + 1.0f;
    if (ax < 2.0f) return a * ax * ax * ax - 5.0f * a * ax * ax + 8.0f * a * ax - 4.0f * a;
    return 0.0f;
}

float4 SampleWeighted16Tap(float2 uv, bool lanczos) {
    float2 srcSizeF = float2(g_srcSize);
    float2 texelPos = uv * srcSizeF - 0.5f;
    float2 base = floor(texelPos);
    float2 frac = texelPos - base;

    float4 sum = float4(0, 0, 0, 0);
    float wsum = 0.0f;
    for (int dy = -1; dy <= 2; ++dy) {
        const float offY = float(dy) - frac.y;
        float wy;
        if (lanczos) {
            const float a = 2.0f;
            if (abs(offY) >= a) { wy = 0.0f; }
            else {
                const float px = 3.14159265f * offY;
                const float sincY = (abs(offY) < 1e-5f) ? 1.0f : (sin(px) / px);
                const float px2 = 3.14159265f * (offY / a);
                const float sincY2 = (abs(offY) < 1e-5f) ? 1.0f : (sin(px2) / px2);
                wy = sincY * sincY2;
            }
        } else {
            wy = CubicWeight(offY);
        }

        for (int dx = -1; dx <= 2; ++dx) {
            const float offX = float(dx) - frac.x;
            float wx;
            if (lanczos) {
                const float a = 2.0f;
                if (abs(offX) >= a) { wx = 0.0f; }
                else {
                    const float px = 3.14159265f * offX;
                    const float sincX = (abs(offX) < 1e-5f) ? 1.0f : (sin(px) / px);
                    const float px2 = 3.14159265f * (offX / a);
                    const float sincX2 = (abs(offX) < 1e-5f) ? 1.0f : (sin(px2) / px2);
                    wx = sincX * sincX2;
                }
            } else {
                wx = CubicWeight(offX);
            }

            const float w = wx * wy;
            int2 samplePixel = int2(base) + int2(dx, dy);
            samplePixel = clamp(samplePixel, int2(0, 0), int2(g_srcSize) - int2(1, 1));
            sum += g_src.Load(int3(samplePixel, 0)) * w;
            wsum += w;
        }
    }
    return sum / max(wsum, 0.0001f);
}

[numthreads(8, 8, 1)]
void CSBicubic(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstSize.x || id.y >= g_dstSize.y) return;
    if (!InViewport(id.xy)) { g_dst[id.xy] = float4(0, 0, 0, 1); return; }
    g_dst[id.xy] = SampleWeighted16Tap(ViewportUV(id.xy), false);
}

[numthreads(8, 8, 1)]
void CSLanczos(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstSize.x || id.y >= g_dstSize.y) return;
    if (!InViewport(id.xy)) { g_dst[id.xy] = float4(0, 0, 0, 1); return; }
    g_dst[id.xy] = SampleWeighted16Tap(ViewportUV(id.xy), true);
}

// Exact pixel replication at g_integerFactor:1 -- the viewport is always
// exactly g_srcSize * g_integerFactor, computed CPU-side, so this never
// needs to filter or guess: every destination pixel maps to exactly one
// source pixel.
[numthreads(8, 8, 1)]
void CSIntegerScale(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_dstSize.x || id.y >= g_dstSize.y) return;
    if (!InViewport(id.xy)) { g_dst[id.xy] = float4(0, 0, 0, 1); return; }

    uint2 local = id.xy - g_viewportOrigin;
    uint2 srcPixel = min(local / max(g_integerFactor, 1u), g_srcSize - uint2(1, 1));
    g_dst[id.xy] = g_src[srcPixel];
}
