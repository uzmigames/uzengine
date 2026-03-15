/// Auto-Exposure Average + Temporal Adaptation — Compute Shader
///
/// Reads the 64-bin luminance histogram produced by the histogram pass,
/// applies percentile filtering (ignoring the darkest and brightest tails),
/// computes the weighted average log2 luminance in the remaining range,
/// clamps to artist-defined brightness bounds, applies temporal adaptation
/// with separate up/down speeds, and outputs the final exposure multiplier.
///
/// # Inputs
/// - Histogram buffer (bindless SRV via `g_HistogramSrvIdx`): 64 x uint32.
/// - Root constants (b0): log2 range, histogram SRV index, brightness clamps,
///   exposure bias, and deltaTime. Adaptation speeds and percentile bounds
///   are hardcoded as shader constants.
///
/// # Outputs
/// - `g_ExposureUAV` (u0): Persistent 2-float buffer:
///   - [0] = currentEV (adapted EV100, read back next frame)
///   - [4] = exposure multiplier (consumed by tonemap shader)
///
/// # Thread Group
/// [1, 1, 1] — single thread. Dispatched as 1x1x1.
///
/// # Algorithm
/// 1. Read all 64 histogram bins and sum total pixel count.
/// 2. Walk bins from dark to bright, applying percentile crop:
///    skip the darkest `lowPercent`% and brightest `(100-highPercent)`%.
/// 3. Compute weighted average log2 luminance of remaining pixels.
/// 4. Convert to linear luminance, clamp to [minBrightness, maxBrightness].
/// 5. Derive target EV100 = log2(avgLum) + 3.0.
/// 6. Exponential temporal adaptation toward target EV (asymmetric speeds).
/// 7. Compute final exposure = exposureBias / (1.2 * 2^currentEV).
/// 8. Store currentEV (for next frame) and exposure (for tonemap).
///
/// # References
/// - UE5 Eye Adaptation (PostProcessEyeAdaptation.usf)
/// - Reinhard et al., "Photographic Tone Reproduction for Digital Images"
/// - ISO 12232:2019 for EV100 ↔ luminance relationship

// ─────────────────────────────────────────────────────────────────────────────
// Root Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants at b0 — 8 DWORDs (matches global root signature limit).
///
/// Rarely-changing parameters (speedDown, lowPercent, highPercent) are
/// hardcoded below to stay within the 8-DWORD root constant budget.
/// Only dynamic per-frame values are passed via root constants.
cbuffer ExposureParams : register(b0)
{
    float g_HistLogMin;        // Log2 luminance range minimum (default: -8.0)
    float g_HistLogMax;        // Log2 luminance range maximum (default:  4.0)
    uint  g_HistogramSrvIdx;   // Bindless SRV index for the histogram buffer
    uint  g_Padding;           // Unused — maintains 8-DWORD alignment
    float g_ExposureBias;      // Artistic EV offset — 1.0 = neutral (default: 1.0)
    float g_MinBrightness;     // Lower luminance clamp in cd/m2 (default: 0.03)
    float g_MaxBrightness;     // Upper luminance clamp in cd/m2 (default: 8.0)
    float g_DeltaTime;         // Frame delta time in seconds
};

/// Hardcoded adaptation parameters — rarely changed at runtime.
/// These match UE5 Eye Adaptation defaults.
static const float g_SpeedUp     = 3.0;   // Adaptation time constant for brightening (seconds)
static const float g_SpeedDown   = 1.0;   // Adaptation time constant for darkening (seconds)
static const float g_LowPercent  = 70.0;  // Dark tail percentile to ignore
static const float g_HighPercent = 98.0;  // Bright tail percentile ceiling

// ─────────────────────────────────────────────────────────────────────────────
// Resources
// ─────────────────────────────────────────────────────────────────────────────

/// Persistent exposure state — survives across frames.
/// Layout: [0] = float currentEV, [4] = float exposureMultiplier.
RWByteAddressBuffer g_ExposureUAV : register(u0);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

#define NUM_BINS 64

/// Minimum adaptation speed denominator to prevent division by zero.
static const float kMinAdaptSpeed = 0.001;

/// Threshold below which the previous EV is considered uninitialized
/// (buffer was cleared to zero). Triggers instant adaptation on first frame.
static const float kUninitializedEVThreshold = 1e-10;

/// EV100 offset: log2(100/12.5) = log2(8) = 3.0
/// Converts from log2(luminance) to EV100 per ISO 12232.
static const float kEV100Offset = 3.0;

/// Normalization constant for the exposure formula.
/// Derived from the ISO 12232 standard: exposure = 1 / (1.2 * 2^EV100).
static const float kExposureNormalization = 1.2;

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(1, 1, 1)]
void main()
{
    // ── Step 1: Read histogram bins and compute total pixel count ────────────

    ByteAddressBuffer histogramSRV = ResourceDescriptorHeap[g_HistogramSrvIdx];

    uint bins[NUM_BINS];
    uint totalPixels = 0;

    [unroll]
    for (uint i = 0; i < NUM_BINS; ++i)
    {
        bins[i] = histogramSRV.Load(i * 4);
        totalPixels += bins[i];
    }

    // No visible pixels — preserve previous exposure (don't adapt to black)
    if (totalPixels == 0)
        return;

    // ── Step 2: Percentile filtering ────────────────────────────────────────
    //
    // Compute the pixel count thresholds for the low and high percentile crops.
    // We ignore the darkest lowPercent% of pixels (shadows, skybox, UI overlays)
    // and the brightest (100-highPercent)% (specular highlights, emissives, sun).
    // This focuses adaptation on the "meat" of the scene luminance distribution.

    uint lowCount  = uint(float(totalPixels) * g_LowPercent  / 100.0);
    uint highCount = uint(float(totalPixels) * g_HighPercent / 100.0);

    // ── Step 3: Weighted average of filtered bins ───────────────────────────

    float weightedLogLum = 0.0;
    uint  pixelsInRange  = 0;
    uint  runningCount   = 0;

    for (uint b = 0; b < NUM_BINS; ++b)
    {
        uint prevCount = runningCount;
        runningCount += bins[b];

        // Determine how many pixels from this bin fall within the
        // [lowCount, highCount] percentile window
        uint binStart = max(prevCount, lowCount);
        uint binEnd   = min(runningCount, highCount);

        if (binEnd > binStart)
        {
            uint contribution = binEnd - binStart;

            // Reconstruct the log2 luminance at the center of this bin.
            // Bin edges are linearly spaced in log2 space.
            float binCenter = g_HistLogMin
                + (float(b) + 0.5) / float(NUM_BINS)
                * (g_HistLogMax - g_HistLogMin);

            weightedLogLum += binCenter * float(contribution);
            pixelsInRange  += contribution;
        }
    }

    // If no pixels survived the percentile filter, preserve previous exposure
    if (pixelsInRange == 0)
        return;

    // ── Step 4: Average luminance + brightness clamp ────────────────────────

    float avgLogLum = weightedLogLum / float(pixelsInRange);

    // Convert from log2 back to linear luminance
    float avgLum = pow(2.0, avgLogLum);

    // Clamp to artist-defined brightness range to prevent extreme adaptation.
    // minBrightness prevents the image from becoming washed-out in dark scenes;
    // maxBrightness prevents it from becoming too dark in bright scenes.
    avgLum = clamp(avgLum, g_MinBrightness, g_MaxBrightness);

    // ── Step 5: Target EV100 ────────────────────────────────────────────────
    //
    // EV100 = log2(L * S / K) where L=luminance, S=100 (ISO), K=12.5
    // Simplifies to: EV100 = log2(L) + log2(100/12.5) = log2(L) + 3.0

    float targetEV = log2(max(avgLum, 1e-5)) + kEV100Offset;

    // ── Step 6: Temporal adaptation ─────────────────────────────────────────
    //
    // Read the previous frame's adapted EV from persistent storage.
    // Apply exponential smoothing with asymmetric time constants:
    // - SpeedUp (slower): eyes adapt slowly when scene gets brighter
    // - SpeedDown (faster): eyes adapt quickly when scene gets darker
    // This matches human visual adaptation behavior.

    float prevEV = asfloat(g_ExposureUAV.Load(0));

    // First-frame initialization: if buffer was cleared to zero,
    // snap instantly to the target instead of slowly adapting from EV=0
    if (abs(prevEV) < kUninitializedEVThreshold)
        prevEV = targetEV;

    // Select adaptation speed based on direction of change
    float adaptSpeed = (targetEV > prevEV) ? g_SpeedUp : g_SpeedDown;

    // Exponential decay factor — frame-rate independent via exp(-dt/tau)
    float alpha = 1.0 - exp(-g_DeltaTime / max(adaptSpeed, kMinAdaptSpeed));
    float currentEV = lerp(prevEV, targetEV, alpha);

    // ── Step 7: Final exposure multiplier ───────────────────────────────────
    //
    // The tonemap shader multiplies HDR color by this value before mapping.
    // Formula: exposure = bias / (1.2 * 2^EV100)
    // The 1.2 factor is the standard normalization from ISO 12232.

    float exposure = g_ExposureBias / (kExposureNormalization * pow(2.0, currentEV));

    // ── Step 8: Write persistent state ──────────────────────────────────────

    g_ExposureUAV.Store(0, asuint(currentEV));  // For next frame's adaptation
    g_ExposureUAV.Store(4, asuint(exposure));    // For tonemap shader to read
}
