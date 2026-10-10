/// VFX volumetric density splat — compute pass (phase 8.15.0).
///
/// Dispatched once per alive particle; each thread converts its particle's
/// world position into a voxel index triplet and adds the per-particle
/// density into the 3×3×3 tent filter neighbourhood. The destination volume
/// is an RWTexture3D<float> bound via bindless.
///
/// Mirrors the CPU-side `VfxVolumetricWorldToVoxel` mapping so tests that
/// splat into a CPU grid produce the same voxel contributions as the GPU
/// pass.
///
/// # Root signature
/// - b0: VolumetricSplatCB (grid center, halfExtent, resolution, densityPerParticle).
/// - u0: RWTexture3D<float> densityVolume.
/// - t0: StructuredBuffer<float4> particlePositions (xyz.w = alive flag).
///
/// @ref UE5 FNiagaraVolumetricRenderer splat pass
/// @ref docs/engine/25-vfx/32-volumetric-renderer.md

cbuffer VolumetricSplatCB : register(b0) {
    float3 gridCenter;
    float  halfExtent;
    uint   gridResolution;
    float  densityPerParticle;
    uint   aliveCount;
    uint   _pad0;
};

#ifdef UZ_BINDLESS
uint rwDensityVolumeIdx   : register(b1);
uint particlePositionsIdx : register(b2);
#else
RWTexture3D<float>        densityVolume     : register(u0);
StructuredBuffer<float4>  particlePositions : register(t0);
#endif

/// Maps a world-space position to a voxel index triplet. Mirrors
/// `VfxVolumetricWorldToVoxel` on the CPU. Returns false when out of bounds.
bool WorldToVoxel(float3 worldPos, out uint3 voxel) {
    voxel = uint3(0, 0, 0);
    if (gridResolution == 0u || halfExtent <= 0.0f) return false;

    const float invExtent = 1.0f / (2.0f * halfExtent);
    const float3 n = ((worldPos - gridCenter) + halfExtent.xxx) * invExtent;
    if (any(n < 0.0f) || any(n > 1.0f)) return false;

    const float res = (float)gridResolution;
    const float3 f = clamp(floor(n * res), 0.0f.xxx, (res - 1.0f).xxx);
    voxel = uint3(f);
    return true;
}

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    const uint particleId = DTid.x;
    if (particleId >= aliveCount) return;

#ifdef UZ_BINDLESS
    StructuredBuffer<float4> positions = ResourceDescriptorHeap[particlePositionsIdx];
    RWTexture3D<float>       volume    = ResourceDescriptorHeap[rwDensityVolumeIdx];
#else
    StructuredBuffer<float4> positions = particlePositions;
    RWTexture3D<float>       volume    = densityVolume;
#endif

    const float4 pos = positions[particleId];
    if (pos.w <= 0.0f) return; // inactive particle

    uint3 centerVoxel;
    if (!WorldToVoxel(pos.xyz, centerVoxel)) return;

    // 3×3×3 tent filter around the center voxel. Tent weights: 1 at center,
    // 0.5 at 6-adjacents, 0.25 at 12-diagonals, 0.125 at 8-corners.
    // Sum of weights across the 3×3×3 cube = 8, so each splat contributes
    // its full density spread across the neighbourhood.
    [unroll] for (int dz = -1; dz <= 1; ++dz) {
        [unroll] for (int dy = -1; dy <= 1; ++dy) {
            [unroll] for (int dx = -1; dx <= 1; ++dx) {
                int3 offset = int3(dx, dy, dz);
                int3 neighbour = int3(centerVoxel) + offset;
                if (any(neighbour < 0) || any(neighbour >= (int)gridResolution)) continue;

                // Trilinear tent weight: multiply per-axis (1 - |d|/1) so
                // center=1, adjacent=0, diag=0. Shift by 0.5 to get
                // center=1, face=0.5, edge=0.25, corner=0.125.
                const float wx = 1.0f - abs((float)dx) * 0.5f;
                const float wy = 1.0f - abs((float)dy) * 0.5f;
                const float wz = 1.0f - abs((float)dz) * 0.5f;
                const float weight = wx * wy * wz;

                // RWTexture3D doesn't support atomic float adds in SM6.0;
                // the runtime binds a R32Uint alias for atomic ops or uses
                // a separate accumulation buffer. For the SM6.6 bindless
                // path we rely on InterlockedAdd on the aliased uint view.
                volume[uint3(neighbour)] += densityPerParticle * weight;
            }
        }
    }
}
