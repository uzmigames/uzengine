/// Auto-Exposure Luminance Histogram — Compute Shader
///
/// Builds a 64-bin histogram of log2 luminance across the HDR backbuffer.
/// Each thread group processes a 16x16 tile of pixels. Uses LDS (groupshared)
/// histogram for fast local accumulation, then atomically merges into
/// the global UAV histogram.
///
/// # Inputs
/// - HDR scene color (bindless SRV via `g_HdrSrvIndex`): after lighting, before tonemapping.
/// - Root constants (b0): log2 luminance range, screen dimensions, and HDR SRV index.
///
/// # Outputs
/// - `g_HistogramUAV` (u0): 64 x uint32 histogram, must be cleared to 0
///   before dispatch.
///
/// # Thread Group
/// [16, 16, 1] = 256 threads. Dispatch ceil(width/16) x ceil(height/16) x 1.
///
/// # Algorithm
/// 1. Each thread computes Rec.709 luminance of its pixel.
/// 2. Maps log2(luminance) into a bin index [0, 63] via linear remap.
/// 3. Accumulates into groupshared LDS histogram (InterlockedAdd).
/// 4. After barrier, merges LDS counts into global UAV histogram.
///
/// Pixels with luminance <= 1e-5 (near-black / background) are excluded
/// to prevent the darkest bin from dominating the exposure calculation.

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

cbuffer ExposureParams : register(b0)
{
    float g_HistLogMin;    // Log2 luminance range minimum (default: -8.0)
    float g_HistLogMax;    // Log2 luminance range maximum (default:  4.0)
    uint  g_ScreenWidth;   // HDR backbuffer width in pixels
    uint  g_ScreenHeight;  // HDR backbuffer height in pixels
    uint  g_HdrSrvIndex;   // Bindless SRV index for HDR scene color texture
};

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Global luminance histogram — 64 bins x uint32.
/// Must be cleared to zero before each frame's dispatch.
RWByteAddressBuffer g_HistogramUAV : register(u0);

/// SM5 non-bindless: HDR scene color bound at t0 via SetFragmentTexture(0).
#ifndef UZ_BINDLESS
Texture2D<float4> g_HdrColorTex : register(t0);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

#define NUM_BINS   64
#define GROUP_SIZE 256  // 16 x 16

/// Near-zero luminance threshold — pixels below this are treated as black
/// and excluded from the histogram to avoid polluting exposure.
static const float kLuminanceEpsilon = 1e-5;

// ─────────────────────────────────────────────────────────────────────────────
// LDS (Groupshared) Histogram
// ─────────────────────────────────────────────────────────────────────────────

/// Per-thread-group histogram for fast local accumulation.
/// Avoids global UAV contention by merging only once per group.
groupshared uint g_LocalHistogram[NUM_BINS];

// ─────────────────────────────────────────────────────────────────────────────
// Utilities
// ─────────────────────────────────────────────────────────────────────────────

/// Computes Rec.709 luminance from linear RGB.
///
/// Uses the standard ITU-R BT.709 coefficients.
/// Input must be in linear space (not gamma-encoded).
float Luminance(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

/// Maps a log2 luminance value to a histogram bin index [0, NUM_BINS-1].
///
/// The mapping is linear within [histLogMin, histLogMax].
/// Values outside the range are clamped to the first or last bin.
uint LogLumToBin(float logLum)
{
    float t = saturate((logLum - g_HistLogMin) / (g_HistLogMax - g_HistLogMin));
    return min(uint(t * float(NUM_BINS - 1) + 0.5), NUM_BINS - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(16, 16, 1)]
void main(uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    // Step 1: Clear LDS histogram — each of the first NUM_BINS threads zeros one bin
    if (gi < NUM_BINS)
        g_LocalHistogram[gi] = 0;

    GroupMemoryBarrierWithGroupSync();

    // Step 2: Accumulate this thread's pixel into the LDS histogram
    if (dtid.x < g_ScreenWidth && dtid.y < g_ScreenHeight)
    {
#ifdef UZ_BINDLESS
        Texture2D<float4> hdrColor = ResourceDescriptorHeap[g_HdrSrvIndex];
#else
        Texture2D<float4> hdrColor = g_HdrColorTex;
#endif
        float3 color = hdrColor.Load(int3(dtid.xy, 0)).rgb;
        float lum = Luminance(color);

        // Exclude near-black pixels — they represent background / unlit areas
        // and would otherwise dominate bin 0, pulling exposure too bright
        if (lum > kLuminanceEpsilon)
        {
            float logLum = log2(lum);
            uint bin = LogLumToBin(logLum);
            InterlockedAdd(g_LocalHistogram[bin], 1);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Step 3: Merge LDS histogram into global UAV histogram.
    // Only the first NUM_BINS threads participate — one thread per bin.
    // Skipping bins with zero count avoids unnecessary atomic traffic.
    if (gi < NUM_BINS)
    {
        uint count = g_LocalHistogram[gi];
        if (count > 0)
        {
            g_HistogramUAV.InterlockedAdd(gi * 4, count);
        }
    }
}
