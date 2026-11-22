// Hierarchical block-matching motion estimation. Dispatched once per
// pyramid level, coarsest first; each level refines the previous level's
// (upsampled) motion vector field with a small local search, which is what
// makes the search windows below tractable (true exhaustive search would
// only be affordable at the coarsest, smallest level).
//
// One thread == one block. This is not the most GPU-occupancy-efficient
// layout (a production implementation would parallelize the SAD reduction
// within a block using group-shared memory) but it is simple, correct, and
// fast enough at the block sizes used here for a demonstration pipeline on
// the targeted integrated/entry-level discrete GPUs. See docs/FRAMEGEN.md
// for the measured cost.

cbuffer MotionEstimationParams : register(b0) {
    uint2 g_levelSize;       // luma texture dimensions at this pyramid level
    uint2 g_numBlocks;       // block grid dimensions at this level
    uint2 g_prevNumBlocks;   // block grid dimensions at the coarser level (0 if none)
    uint g_blockSize;
    uint g_searchRadius;
    uint g_hasPrevLevel;
    uint g_padding0;
    float2 g_prevLevelScale; // multiply coarser-level vectors by this (pyramid factor, 2.0)
    float2 g_padding1;
};

Texture2D<float> g_lumaA : register(t0);
Texture2D<float> g_lumaB : register(t1);
Texture2D<float2> g_prevLevelMV : register(t2);
RWTexture2D<float2> g_mvOutput : register(u0);

[numthreads(1, 1, 1)]
void CSBlockMatch(uint3 id : SV_DispatchThreadID) {
    if (id.x >= g_numBlocks.x || id.y >= g_numBlocks.y) return;

    uint2 blockCoord = id.xy;
    int2 origin = int2(blockCoord * g_blockSize);

    int2 initGuess = int2(0, 0);
    if (g_hasPrevLevel != 0 && g_prevNumBlocks.x > 0 && g_prevNumBlocks.y > 0) {
        uint2 prevCoord = min(blockCoord / 2, g_prevNumBlocks - uint2(1, 1));
        float2 prevMV = g_prevLevelMV[prevCoord];
        initGuess = int2(round(prevMV * g_prevLevelScale));
    }

    int2 maxPixel = int2(g_levelSize) - int2(1, 1);
    int R = int(g_searchRadius);
    int blockSize = int(g_blockSize);

    float bestCost = 3.402823e38f;
    int2 bestOffset = initGuess;

    for (int dy = -R; dy <= R; ++dy) {
        for (int dx = -R; dx <= R; ++dx) {
            int2 candidate = initGuess + int2(dx, dy);
            float cost = 0.0f;

            for (int by = 0; by < blockSize; ++by) {
                for (int bx = 0; bx < blockSize; ++bx) {
                    int2 pa = clamp(origin + int2(bx, by), int2(0, 0), maxPixel);
                    int2 pb = clamp(pa + candidate, int2(0, 0), maxPixel);
                    cost += abs(g_lumaA[uint2(pa)] - g_lumaB[uint2(pb)]);
                }
            }

            if (cost < bestCost) {
                bestCost = cost;
                bestOffset = candidate;
            }
        }
    }

    g_mvOutput[blockCoord] = float2(bestOffset);
}
