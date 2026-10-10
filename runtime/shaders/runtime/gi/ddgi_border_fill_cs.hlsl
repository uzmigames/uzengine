/// UzEngine DDGI — atlas border fill.
///
/// Both DDGI atlases (irradiance 6x6, depth 16x16 per probe) carry a 1-texel border so that the
/// deferred lighting's bilinear lookup wraps correctly at the octahedral seams. The probe update
/// writes the interior texels only; this pass fills the border from the interior after each update,
/// following the octahedral map's own identifications (Cigolle 2014, the same encode/decode as
/// `ddgi_probe_update_cs.hlsl` and the deferred sampler):
///
///   * an edge texel copies the interior texel in the adjacent column/row with the coordinate ALONG
///     the edge mirrored — crossing the edge u = -1 at v lands on (u = -1, -v) after the fold;
///   * a corner texel copies the diagonally opposite interior corner — all four corners of the
///     square encode the same direction (-z).
///
/// Until 2026-09-18 nothing filled the border and nothing cleared the atlas, so the bilinear tap
/// read uninitialised memory for every normal within half a texel of a tile edge: measured on
/// Sponza as the consumer reading the DDGI path at 0.57-0.72x with identical stored energy
/// (followup-01, H-55). `g_clear = 1` writes zeros to every texel instead, run once at creation so
/// the initial contents are defined; it does not replace the fill.
///
/// @ref Cigolle 2014 "A Survey of Efficient Representations for Independent Unit Vectors"
/// @ref Majercik 2019 "Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields"

cbuffer RootConstants : register(b0) {
    uint g_atlasUav;       ///< Bindless UAV of the atlas being filled.
    uint g_probesPerAxis;  ///< Probe grid side (tiles: probes^2 across, probes down).
    uint g_tileSize;       ///< Interior tile size: 6 (irradiance) or 16 (depth).
    uint g_mode;           ///< 0 = irradiance atlas (float4), 1 = depth atlas (float2).
    uint g_clear;          ///< 1 = write zeros to every texel; 0 = fill the borders from the interior.
};

/// Interior texel (1..N, 1..N inside the tile) that a border texel (lx, ly) mirrors.
uint2 BorderSource(uint lx, uint ly, uint N) {
    const bool onX = (lx == 0u) || (lx == N + 1u);
    const bool onY = (ly == 0u) || (ly == N + 1u);
    if (onX && onY) {
        // Corner: the diagonally opposite interior corner.
        return uint2((lx == 0u) ? N : 1u, (ly == 0u) ? N : 1u);
    }
    if (onX) {
        // Left/right edge: the adjacent interior column, row mirrored along the edge.
        return uint2((lx == 0u) ? 1u : N, N + 1u - ly);
    }
    // Top/bottom edge: the adjacent interior row, column mirrored along the edge.
    return uint2(N + 1u - lx, (ly == 0u) ? 1u : N);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    const uint stride = g_tileSize + 2u;
    const uint atlasW = g_probesPerAxis * g_probesPerAxis * stride;
    const uint atlasH = g_probesPerAxis * stride;
    if (DTid.x >= atlasW || DTid.y >= atlasH) return;

    const uint lx = DTid.x % stride;
    const uint ly = DTid.y % stride;
    const uint N  = g_tileSize;
    const bool isBorder = (lx == 0u) || (lx == N + 1u) || (ly == 0u) || (ly == N + 1u);
    if (g_clear == 0u && !isBorder) return;

    const uint2 dst = DTid.xy;
    const uint2 src = uint2(DTid.x - lx, DTid.y - ly) + BorderSource(lx, ly, N);

    if (g_mode == 0u) {
        RWTexture2D<float4> atlas = ResourceDescriptorHeap[g_atlasUav];
        atlas[dst] = (g_clear != 0u) ? float4(0.0, 0.0, 0.0, 0.0) : atlas[src];
    } else {
        RWTexture2D<float2> atlas = ResourceDescriptorHeap[g_atlasUav];
        atlas[dst] = (g_clear != 0u) ? float2(0.0, 0.0) : atlas[src];
    }
}
