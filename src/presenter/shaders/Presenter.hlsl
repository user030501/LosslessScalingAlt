// Minimal fullscreen-triangle blit: samples the pipeline's final output
// texture (whatever ScalingEngine/FrameGenEngine last produced) into
// whatever viewport OutputPresenter has set on the backbuffer (a letterboxed/
// pillarboxed sub-rect when aspect ratio is preserved). No compute shader
// here -- this is a standard graphics-pipeline blit, the cheapest way to
// get a texture onto a swapchain backbuffer (one draw call, no CPU
// readback, no intermediate copy).

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    // Classic 3-vertex fullscreen triangle: covers the whole clip-space
    // square (and then some, clipped away), needs no vertex/index buffer.
    float2 pos = float2((id == 2) ? 3.0f : -1.0f, (id == 1) ? 3.0f : -1.0f);
    o.pos = float4(pos, 0.0f, 1.0f);
    o.uv = float2((pos.x + 1.0f) * 0.5f, 1.0f - (pos.y + 1.0f) * 0.5f);
    return o;
}

Texture2D g_tex : register(t0);
SamplerState g_sampler : register(s0);

float4 PSMain(VSOut input) : SV_Target {
    return g_tex.Sample(g_sampler, input.uv);
}
