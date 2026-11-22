// Hierarchical block-matching motion estimation. Dispatched once per
// pyramid level, coarsest first; each level refines the previous level's
// (upsampled) motion vector field with a small local search, which is what
// makes the search windows below tractable (true exhaustive search would
// only be affordable at the coarsest, smallest level).
//
// Phase 3 GPU optimization: the original version of this shader used
// [numthreads(1,1,1)] -- one GPU thread doing the *entire* exhaustive
// search (every candidate offset, every pixel in the block) serially. That
// wastes almost all of a SIMD wave on both the Intel UHD 620 and the
// NVIDIA MX150 (a wave/warp runs 16-32 lanes per clock; one active thread
// out of those occupies one lane and leaves the rest idle). There are
// three quality presets, each with a fixed, known search radius
// (Performance=4, Balanced=6, Quality=8 -- see FrameGenTypes.h), so each
// gets its own entry point whose thread-group dimensions exactly match its
// candidate window: one thread per candidate offset, computing that
// candidate's full-block SAD cost in parallel, followed by a group-shared
// reduction to find the minimum. Every thread in the group does useful
// work; none are wasted. The candidate enumeration order (dy ascending,
// then dx ascending) and the reduction's "strictly less than" comparison
// are kept identical to the original serial version so results -- not
// just performance -- are unchanged; MotionEstimator picks the matching
// variant for the active quality mode's search radius.

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

// Shared by all three variants below: computes the initial (predicted)
// motion guess for a block from the coarser pyramid level, if any.
int2 PredictedGuess(uint2 blockCoord) {
    int2 initGuess = int2(0, 0);
    if (g_hasPrevLevel != 0 && g_prevNumBlocks.x > 0 && g_prevNumBlocks.y > 0) {
        uint2 prevCoord = min(blockCoord / 2, g_prevNumBlocks - uint2(1, 1));
        float2 prevMV = g_prevLevelMV[prevCoord];
        initGuess = int2(round(prevMV * g_prevLevelScale));
    }
    return initGuess;
}

// One group = one block. Each thread evaluates exactly one candidate
// offset (dx, dy) in [-R, R]^2 relative to the predicted guess, computing
// the full block SAD cost for that candidate, then a single designated
// thread scans the group-shared results to pick the minimum -- same
// linear scan order the original single-threaded version used, so ties
// resolve identically.
#define BLOCKMATCH_VARIANT(NAME, R, GROUP_DIM, GROUP_SIZE)                                             \
groupshared float g_cost_##NAME[GROUP_SIZE];                                                           \
groupshared int2 g_offset_##NAME[GROUP_SIZE];                                                          \
[numthreads(GROUP_DIM, GROUP_DIM, 1)]                                                                  \
void NAME(uint3 groupId : SV_GroupID, uint3 GTid : SV_GroupThreadID) {                                 \
    uint2 blockCoord = groupId.xy;                                                                     \
    uint flatIdx = GTid.y * GROUP_DIM + GTid.x;                                                        \
    if (blockCoord.x >= g_numBlocks.x || blockCoord.y >= g_numBlocks.y) {                              \
        g_cost_##NAME[flatIdx] = 3.402823e38f;                                                         \
        g_offset_##NAME[flatIdx] = int2(0, 0);                                                         \
        GroupMemoryBarrierWithGroupSync();                                                             \
        return;                                                                                        \
    }                                                                                                  \
                                                                                                         \
    int2 origin = int2(blockCoord * g_blockSize);                                                      \
    int2 initGuess = PredictedGuess(blockCoord);                                                       \
    int2 maxPixel = int2(g_levelSize) - int2(1, 1);                                                    \
    int blockSize = int(g_blockSize);                                                                  \
    int searchR = min(int(g_searchRadius), R);                                                         \
                                                                                                         \
    int dx = int(GTid.x) - R;                                                                          \
    int dy = int(GTid.y) - R;                                                                          \
                                                                                                         \
    float cost = 3.402823e38f;                                                                         \
    int2 candidate = initGuess + int2(dx, dy);                                                         \
    if (abs(dx) <= searchR && abs(dy) <= searchR) {                                                    \
        cost = 0.0f;                                                                                   \
        for (int by = 0; by < blockSize; ++by) {                                                       \
            for (int bx = 0; bx < blockSize; ++bx) {                                                   \
                int2 pa = clamp(origin + int2(bx, by), int2(0, 0), maxPixel);                          \
                int2 pb = clamp(pa + candidate, int2(0, 0), maxPixel);                                 \
                cost += abs(g_lumaA[uint2(pa)] - g_lumaB[uint2(pb)]);                                   \
            }                                                                                          \
        }                                                                                              \
    }                                                                                                   \
                                                                                                         \
    g_cost_##NAME[flatIdx] = cost;                                                                     \
    g_offset_##NAME[flatIdx] = candidate;                                                              \
    GroupMemoryBarrierWithGroupSync();                                                                 \
                                                                                                         \
    if (flatIdx == 0) {                                                                                \
        float bestCost = g_cost_##NAME[0];                                                             \
        int2 bestOffset = g_offset_##NAME[0];                                                          \
        for (uint i = 1; i < GROUP_SIZE; ++i) {                                                        \
            if (g_cost_##NAME[i] < bestCost) {                                                         \
                bestCost = g_cost_##NAME[i];                                                           \
                bestOffset = g_offset_##NAME[i];                                                       \
            }                                                                                          \
        }                                                                                              \
        g_mvOutput[blockCoord] = float2(bestOffset);                                                   \
    }                                                                                                   \
}

// Performance preset: searchRadius = 4 -> 9x9 = 81 candidates/threads.
BLOCKMATCH_VARIANT(CSBlockMatchR4, 4, 9, 81)
// Balanced preset: searchRadius = 6 -> 13x13 = 169 candidates/threads.
BLOCKMATCH_VARIANT(CSBlockMatchR6, 6, 13, 169)
// Quality preset: searchRadius = 8 -> 17x17 = 289 candidates/threads.
BLOCKMATCH_VARIANT(CSBlockMatchR8, 8, 17, 289)
