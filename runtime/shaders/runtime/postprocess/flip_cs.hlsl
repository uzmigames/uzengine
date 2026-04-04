/// UzEngine FLIP Perceptual Image Quality Metric — Compute Shader
///
/// @ref Andersson et al. 2020 "FLIP: A Difference Evaluator for Alternating Images"
/// @ref High Performance Graphics 2020 — Pontus Andersson, Jim Nilsson,
///      Tomas Akenine-Moller, Magnus Oskarsson, Kalle Astrom, Mark D. Fairchild
///
/// Computes per-pixel perceptual error between a reference and test image.
/// Pipeline: sRGB→linear→YCxCz→CSF filter→CIELab→Hunt adjust→HyAB color diff
///          + Gaussian-weighted feature detection → redistributed FLIP score.
///
/// Output: RGBA16F heatmap (magma colormap) + atomic mean/max accumulation.
///
/// Root constants at b0: { refSrvIdx, testSrvIdx, outputUavIdx, statsBufUav,
///                         width, height, monitorWidthPx, monitorDistM_bits }

#include "../common/constants.hlsli"

cbuffer RootConstants : register(b0) {
#ifdef UZ_BINDLESS
    uint g_refSrvIdx;
    uint g_testSrvIdx;
    uint g_outputUavIdx;
    uint g_statsUavIdx;
#endif
    uint g_width;
    uint g_height;
    uint g_monitorWidthPx;    ///< Monitor width in pixels (default 3840).
    uint g_monitorDistBits;   ///< Monitor distance in meters, as float bits (default 0.7).
};

SamplerState linearSampler : register(s0);

#ifndef UZ_BINDLESS
Texture2D<float4>      t_ref    : register(t0);
Texture2D<float4>      t_test   : register(t1);
RWTexture2D<float4>    u_output : register(u0);
RWByteAddressBuffer    u_stats  : register(u1);
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Color Space Transforms
// @ref Andersson 2020 — sRGB → linear → XYZ → YCxCz → CIELab pipeline
// ─────────────────────────────────────────────────────────────────────────────

/// @ref IEC 61966-2-1 — sRGB EOTF
float sRGBToLinearChannel(float s) {
    return (s <= 0.04045) ? s / 12.92 : pow((s + 0.055) / 1.055, 2.4);
}
float3 sRGBToLinear(float3 srgb) {
    return float3(sRGBToLinearChannel(srgb.x),
                  sRGBToLinearChannel(srgb.y),
                  sRGBToLinearChannel(srgb.z));
}

/// @ref sRGB to CIE XYZ (D65) — BT.709 primaries
float3 linearRGBToXYZ(float3 c) {
    return float3(
        0.4124564 * c.r + 0.3575761 * c.g + 0.1804375 * c.b,
        0.2126729 * c.r + 0.7151522 * c.g + 0.0721750 * c.b,
        0.0193339 * c.r + 0.1191920 * c.g + 0.9503041 * c.b);
}

/// @ref CIE XYZ to YCxCz (Preucil 1953, used by FLIP)
float3 XYZToYCxCz(float3 xyz) {
    float3 D65 = float3(0.950456, 1.0, 1.089058);
    float3 r = xyz / D65;
    float3 f;
    f.x = (r.x > 0.008856) ? pow(r.x, 1.0/3.0) : 7.787 * r.x + 16.0/116.0;
    f.y = (r.y > 0.008856) ? pow(r.y, 1.0/3.0) : 7.787 * r.y + 16.0/116.0;
    f.z = (r.z > 0.008856) ? pow(r.z, 1.0/3.0) : 7.787 * r.z + 16.0/116.0;
    return float3(116.0 * f.y - 16.0, 500.0 * (f.x - f.y), 200.0 * (f.y - f.z));
}

float3 linearRGBToYCxCz(float3 c) { return XYZToYCxCz(linearRGBToXYZ(c)); }

/// @ref CIE XYZ to CIELAB
float3 XYZToCIELab(float3 xyz) {
    float3 D65 = float3(0.950456, 1.0, 1.089058);
    float3 r = xyz / D65;
    float3 f;
    f.x = (r.x > 0.008856) ? pow(r.x, 1.0/3.0) : 7.787 * r.x + 16.0/116.0;
    f.y = (r.y > 0.008856) ? pow(r.y, 1.0/3.0) : 7.787 * r.y + 16.0/116.0;
    f.z = (r.z > 0.008856) ? pow(r.z, 1.0/3.0) : 7.787 * r.z + 16.0/116.0;
    return float3(116.0 * f.y - 16.0, 500.0 * (f.x - f.y), 200.0 * (f.y - f.z));
}
float3 linearRGBToCIELab(float3 c) { return XYZToCIELab(linearRGBToXYZ(c)); }

/// Inverse: YCxCz → XYZ → linear RGB (for CSF filtered color back-conversion)
float3 YCxCzToXYZ(float3 lab) {
    float3 D65 = float3(0.950456, 1.0, 1.089058);
    float fy = (lab.x + 16.0) / 116.0;
    float fx = lab.y / 500.0 + fy;
    float fz = fy - lab.z / 200.0;
    float3 xyz;
    xyz.x = (fx > 0.206893) ? fx*fx*fx : (fx - 16.0/116.0) / 7.787;
    xyz.y = (fy > 0.206893) ? fy*fy*fy : (fy - 16.0/116.0) / 7.787;
    xyz.z = (fz > 0.206893) ? fz*fz*fz : (fz - 16.0/116.0) / 7.787;
    return xyz * D65;
}
float3 XYZToLinearRGB(float3 xyz) {
    return float3(
         3.2404542 * xyz.x - 1.5371385 * xyz.y - 0.4985314 * xyz.z,
        -0.9692660 * xyz.x + 1.8760108 * xyz.y + 0.0415560 * xyz.z,
         0.0556434 * xyz.x - 0.2040259 * xyz.y + 1.0572252 * xyz.z);
}
float3 YCxCzToLinearRGB(float3 lab) { return XYZToLinearRGB(YCxCzToXYZ(lab)); }

// ─────────────────────────────────────────────────────────────────────────────
// FLIP Core Functions
// @ref Andersson 2020 — HyAB, Hunt adjustment, CSF, feature detection
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Andersson 2020 Eq. 4 — HyAB color distance
/// @constants splits L* (achromatic) from a*b* (chromatic)
float HyAB(float3 a, float3 b) {
    float3 diff = a - b;
    return abs(diff.x) + length(diff.yz);
}

/// @ref Andersson 2020 — Hunt effect adjustment (chromatic adaptation)
/// @constants huntScale = 0.01
float3 HuntAdjust(float3 lab) {
    float h = 0.01 * lab.x;
    return float3(lab.x, h * lab.y, h * lab.z);
}

/// @ref Andersson 2020 — CSF spatial filter weight (sum of two Gaussians)
/// @constants abA = {1, 0, 0.0047, 1e-5}, abRG = {1, 0, 0.0053, 1e-5}, abBY = {34.1, 13.5, 0.04, 0.025}
float calculateWeight(float dist2, float4 ab) {
    float b1Inv = 1.0 / ab.z;
    float b2Inv = 1.0 / ab.w;
    return ab.x * sqrt(PI * b1Inv) * exp(b1Inv * dist2) + ab.y * sqrt(PI * b2Inv) * exp(b2Inv * dist2);
}

/// @ref Andersson 2020 — error redistribution with perceptual cutoff
/// @constants qc=0.7, pc=0.4, pt=0.95, qf=0.5
float redistributeErrors(float colorDiff, float featureDiff, float maxDist) {
    const float qc = 0.7;
    const float pc = 0.4;
    const float pt = 0.95;
    float error = pow(colorDiff, qc);
    float cutoff = pc * maxDist;
    if (error < cutoff)
        error *= (pt / cutoff);
    else
        error = pt + ((error - cutoff) / (maxDist - cutoff)) * (1.0 - pt);
    error = pow(error, 1.0 - featureDiff);
    return error;
}

// ─────────────────────────────────────────────────────────────────────────────
// Magma colormap (16 samples, interpolated)
// @ref matplotlib magma — perceptually uniform sequential colormap
// ─────────────────────────────────────────────────────────────────────────────

static const float3 kMagma[16] = {
    float3(0.001, 0.000, 0.014), float3(0.035, 0.013, 0.131),
    float3(0.106, 0.027, 0.263), float3(0.196, 0.040, 0.367),
    float3(0.290, 0.063, 0.427), float3(0.383, 0.087, 0.462),
    float3(0.478, 0.105, 0.475), float3(0.575, 0.117, 0.468),
    float3(0.672, 0.130, 0.437), float3(0.764, 0.161, 0.380),
    float3(0.845, 0.215, 0.303), float3(0.912, 0.296, 0.218),
    float3(0.959, 0.397, 0.140), float3(0.982, 0.530, 0.085),
    float3(0.988, 0.691, 0.165), float3(0.987, 0.991, 0.750)
};

float3 magmaColor(float t) {
    t = saturate(t) * 15.0;
    int i = min((int)t, 14);
    return lerp(kMagma[i], kMagma[i+1], t - float(i));
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Compute Shader
// ─────────────────────────────────────────────────────────────────────────────

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= g_width || DTid.y >= g_height) return;

    uint2 pixel = DTid.xy;

#ifdef UZ_BINDLESS
    Texture2D<float4> refTex  = ResourceDescriptorHeap[g_refSrvIdx];
    Texture2D<float4> testTex = ResourceDescriptorHeap[g_testSrvIdx];
    RWTexture2D<float4> output = ResourceDescriptorHeap[g_outputUavIdx];
    RWByteAddressBuffer stats  = ResourceDescriptorHeap[g_statsUavIdx];
#else
    Texture2D<float4> refTex  = t_ref;
    Texture2D<float4> testTex = t_test;
    RWTexture2D<float4> output = u_output;
    RWByteAddressBuffer stats  = u_stats;
#endif

    // Pixels per degree
    float monitorDist = asfloat(g_monitorDistBits);
    if (monitorDist <= 0.0) monitorDist = 0.7;
    float monitorWidthM = 0.7; // ~27" monitor
    float pixPerDeg = monitorDist * (float(g_monitorWidthPx) / monitorWidthM) * (PI / 180.0);
    float dx = 1.0 / pixPerDeg;

    // CSF filter constants
    /// @ref Andersson 2020 Table 1 — CSF Gaussian parameters
    const float4 abA  = float4(1.0, 0.0, 0.0047, 1e-5);
    const float4 abRG = float4(1.0, 0.0, 0.0053, 1e-5);
    const float4 abBY = float4(34.1, 13.5, 0.04, 0.025);

    // Feature detection sigma
    /// @ref Andersson 2020 — gw = 0.082
    float sigmaF = 0.5 * 0.082 * pixPerDeg;
    float sigmaF2 = sigmaF * sigmaF;

    // Kernel radius
    int radius = (int)ceil(3.0 * sqrt(0.04 / (2.0 * PI * PI)) * pixPerDeg);
    radius = min(radius, 16); // cap for perf

    // Accumulate CSF filtered color + feature gradients
    float3 csfSum = float3(0, 0, 0);
    float3 refColorSum = float3(0, 0, 0);
    float3 testColorSum = float3(0, 0, 0);
    float2 refEdgeGrad = float2(0, 0);
    float2 testEdgeGrad = float2(0, 0);
    float2 refPointGrad = float2(0, 0);
    float2 testPointGrad = float2(0, 0);
    float posKernSum = 0, negKernSum = 0, edgeKernSum = 0;

    // Pre-pass: compute kernel sums for normalization
    for (int py = -radius; py <= radius; ++py) {
        for (int px = -radius; px <= radius; ++px) {
            float g = exp(-(px*px + py*py) / (2.0 * sigmaF2));
            float2 pw = (float2(px*px, py*py) / sigmaF2 - 1.0) * g;
            posKernSum += max(pw.x, 0.0);
            negKernSum += max(-pw.x, 0.0);
            edgeKernSum += max(-float(px) * g, 0.0);
        }
    }
    if (posKernSum == 0) posKernSum = 1;
    if (negKernSum == 0) negKernSum = 1;
    if (edgeKernSum == 0) edgeKernSum = 1;

    // Main kernel loop
    for (int ky = -radius; ky <= radius; ++ky) {
        for (int kx = -radius; kx <= radius; ++kx) {
            int2 np = clamp(int2(pixel) + int2(kx, ky), int2(0,0), int2(g_width-1, g_height-1));

            float3 refLin  = sRGBToLinear(refTex.Load(int3(np, 0)).rgb);
            float3 testLin = sRGBToLinear(testTex.Load(int3(np, 0)).rgb);
            float3 refYCxCz  = linearRGBToYCxCz(refLin);
            float3 testYCxCz = linearRGBToYCxCz(testLin);

            // Color pipeline — CSF spatial filter
            float2 p = float2(kx, ky) * dx;
            float dist2 = -(p.x*p.x + p.y*p.y) * PI * PI;
            float3 w = float3(calculateWeight(dist2, abA),
                              calculateWeight(dist2, abRG),
                              calculateWeight(dist2, abBY));
            csfSum += w;
            refColorSum  += w * refYCxCz;
            testColorSum += w * testYCxCz;

            // Feature pipeline — Gaussian-weighted edge/point detection
            float g = exp(-(kx*kx + ky*ky) / (2.0 * sigmaF2));
            float2 pw = (float2(kx*kx, ky*ky) / sigmaF2 - 1.0) * g;
            float2 pn = 1.0 / float2(pw.x >= 0 ? posKernSum : negKernSum,
                                      pw.y >= 0 ? posKernSum : negKernSum);
            float2 ew = -float2(kx, ky) * g;
            float2 en = 1.0 / float2(edgeKernSum, edgeKernSum);

            float refLum  = (refYCxCz.x + 16.0) / 116.0;
            float testLum = (testYCxCz.x + 16.0) / 116.0;
            refPointGrad  += refLum * pw * pn;
            refEdgeGrad   += refLum * ew * en;
            testPointGrad += testLum * pw * pn;
            testEdgeGrad  += testLum * ew * en;
        }
    }

    // Color difference
    float3 filtRef  = clamp(YCxCzToLinearRGB(refColorSum / csfSum), 0, 1);
    float3 filtTest = clamp(YCxCzToLinearRGB(testColorSum / csfSum), 0, 1);
    float colorDiff = HyAB(HuntAdjust(linearRGBToCIELab(filtRef)),
                           HuntAdjust(linearRGBToCIELab(filtTest)));

    // Feature difference
    float edgeDiff  = abs(length(refEdgeGrad) - length(testEdgeGrad));
    float pointDiff = abs(length(refPointGrad) - length(testPointGrad));
    float featureDiff = pow(max(pointDiff, edgeDiff) * 0.70710678 /*1/sqrt(2)*/, 0.5);

    // Max perceptual distance (green vs blue after Hunt adjustment)
    float maxDist = pow(HyAB(HuntAdjust(linearRGBToCIELab(float3(0, 1, 0))),
                             HuntAdjust(linearRGBToCIELab(float3(0, 0, 1)))), 0.7);

    // Final FLIP score
    float flipScore = redistributeErrors(colorDiff, featureDiff, maxDist);
    flipScore = saturate(flipScore);

    // Write heatmap (magma colormap)
    output[pixel] = float4(magmaColor(flipScore), flipScore);

    // Atomic accumulate stats: [0]=sum, [4]=max, [8]=count
    uint scoreBits = asuint(flipScore);
    stats.InterlockedAdd(0, scoreBits);  // sum (approximate — float add via uint)
    stats.InterlockedMax(4, scoreBits);  // max (works for positive floats via uint compare)
    stats.InterlockedAdd(8, 1u);         // pixel count
}
