/// @ref Greene et al. 1993 "Hierarchical Z-Buffer Visibility" — hierarchical Z-buffer mip-chain construction
/// @ref Wihlidal 2015 "Optimizing the Graphics Pipeline with Compute" — Hi-Z pyramid generation
/// @constants GROUP_TILE_SIZE=8 (standard 8x8 thread group for 2x downsample), gather offset -0.25 (bilinear gather texel offset)
/// @deviations Single-mip dispatch (no LDS multi-mip batch); uses Load instead of
///             GatherRed (explicit 4-tap for clarity and odd-dimension clamping);
///             no ClosestHZB output (only furthest/conservative needed for culling).
///
/// Hierarchical Z-Buffer (Hi-Z) pyramid generation compute shader.
///
/// Downsamples a depth buffer by 2x per dispatch, producing one mip level each
/// invocation. Each output texel stores the minimum of its corresponding 2x2 input
/// texels, creating a conservative depth hierarchy.
///
/// With reversed-Z (near=1.0, far=0.0), min() yields the CLOSEST depth value.
/// This is conservative for occlusion culling: if the closest geometry in a tile
/// is behind the occluder, everything in that tile is occluded.
///
/// Used by: SSAO (depth-aware blur), SSR (Hi-Z tracing), contact shadows,
///          GPU occlusion culling (two-phase HZB test).
///
/// # Inputs
/// - `g_InputDepth` (t0): Source depth mip level (SRV, R32_FLOAT).
/// - Root constants (b0): output dimensions + input dimensions for clamping.
///
/// # Outputs
/// - `g_OutputDepth` (u0): Destination mip level at half resolution (UAV, R32_FLOAT).
///
/// # Thread Group
/// [8, 8, 1] -- one thread per output texel. Dispatch ceil(outW/8) x ceil(outH/8).
///
/// # Dispatch Pattern (CPU side)
/// For a 1920x1080 depth buffer with N mip levels:
///   mip 0 = full res (source depth from depth prepass, not written by this shader)
///   mip 1 = 960x540:  dispatch(120, 68, 1), input=mip0, output=mip1
///   mip 2 = 480x270:  dispatch(60, 34, 1),  input=mip1, output=mip2
///   ...and so on until 1x1.
/// Each dispatch requires a UAV barrier between mip levels.

// ---------------------------------------------------------------------------
// Root Constants
// ---------------------------------------------------------------------------

/// Per-dispatch constants uploaded via root constants (32-bit root CBV at b0).
/// @ref HZB construction —13-16 — DispatchThreadIdToBufferUV, InvSize, InputViewportMaxBound
/// passes UV-space transforms; we pass integer dimensions directly since we
/// use Load() instead of SampleLevel(), avoiding UV coordinate arithmetic.
#ifdef UZ_BINDLESS
cbuffer HiZConstants : register(b0)
{
    uint2 g_OutputSize;      // Dimensions of the output mip level in texels
    uint2 g_InputSize;       // Dimensions of the input mip level in texels (for clamping)
    uint  g_InputSrvIndex;   // SRV index into ResourceDescriptorHeap for input depth
    uint  _pad0;
    uint  _pad1;
    uint  _pad2;
};
#else
cbuffer HiZConstants : register(b0)
{
    uint2 g_OutputSize;  // Dimensions of the output mip level in texels
    uint2 g_InputSize;   // Dimensions of the input mip level in texels (for clamping)
};
#endif

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

#ifdef UZ_BINDLESS
// Bindless: input depth accessed via ResourceDescriptorHeap[g_InputSrvIndex]
#else
/// Source depth mip -- bound as SRV via descriptor table (SM5 path).
Texture2D<float>    g_InputDepth  : register(t0);
#endif

/// Destination mip -- half the resolution of g_InputDepth in each dimension.
RWTexture2D<float>  g_OutputDepth : register(u0);

// ---------------------------------------------------------------------------
// Hi-Z Downsample Kernel
// ---------------------------------------------------------------------------

/// @ref HZB construction —183-231 — HZBBuildCS (single-mip path, DIM_MIP_LEVEL_COUNT==1)
/// uses Gather4() which samples a 2x2 neighborhood via hardware gather.
/// We use explicit Load() with 4 taps for two reasons:
///   1. Handles odd-dimension mips correctly by clamping each tap independently.
///   2. Input is a UAV re-bound as SRV with no sampler, so gather is unavailable
///      when the resource was just a UAV in the previous dispatch.
///
/// The reduction operator is min() for all 4 samples.
/// @ref HZB construction —211 — MinDeviceZ = min(min3(DeviceZ.x, DeviceZ.y, DeviceZ.z), DeviceZ.w)
/// names this "FurthestDeviceZ" because in standard Z mapping (near=0, far=1),
/// the furthest visible depth is the smallest number. In our reversed-Z setup
/// (near=1, far=0), min() gives the CLOSEST depth -- which is the conservative
/// choice for occlusion culling (if the closest depth in a tile is behind the
/// occluder, everything in the tile is hidden).
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Early-out for threads beyond the output dimensions. This handles
    // non-power-of-two mip levels where the dispatch grid is rounded up.
    if (dtid.x >= g_OutputSize.x || dtid.y >= g_OutputSize.y)
        return;

    // Map output texel to the top-left corner of the corresponding 2x2 block
    // in the input mip.
    uint2 srcBase = dtid.xy * 2;

    // Compute clamped coordinates for all 4 taps. When the input dimension is
    // odd (e.g., 961 wide for a 1920 source), the rightmost output texel's 2x2
    // block extends one pixel beyond the input. Clamping to (inputSize - 1)
    // duplicates the edge texel, which is conservative (it cannot introduce a
    // depth value that doesn't exist in the source).
    /// @ref HZB construction —28 — "min(..., InputViewportMaxBound) because we don't
    ///   want to sample outside of the viewport when the view size has odd dimensions"
    uint2 maxCoord = g_InputSize - uint2(1, 1);

    uint2 c00 = min(srcBase,                    maxCoord);
    uint2 c10 = min(srcBase + uint2(1, 0),      maxCoord);
    uint2 c01 = min(srcBase + uint2(0, 1),      maxCoord);
    uint2 c11 = min(srcBase + uint2(1, 1),      maxCoord);

    // Load all 4 depth samples. Using Load (integer addressing, mip 0) avoids
    // any filtering -- we want exact texel values for the conservative reduction.
#ifdef UZ_BINDLESS
    Texture2D<float> inputDepth = ResourceDescriptorHeap[g_InputSrvIndex];
    float z00 = inputDepth.Load(int3(c00, 0));
    float z10 = inputDepth.Load(int3(c10, 0));
    float z01 = inputDepth.Load(int3(c01, 0));
    float z11 = inputDepth.Load(int3(c11, 0));
#else
    float z00 = g_InputDepth.Load(int3(c00, 0));
    float z10 = g_InputDepth.Load(int3(c10, 0));
    float z01 = g_InputDepth.Load(int3(c01, 0));
    float z11 = g_InputDepth.Load(int3(c11, 0));
#endif

    // Conservative reduction: min of all 4 samples.
    // Reversed-Z: min = closest to camera = most conservative for occlusion.
    // @ref HZB mip downsample —211 — min(min3(x,y,z), w)
    float minZ = min(min(z00, z10), min(z01, z11));

    g_OutputDepth[dtid.xy] = minZ;
}
