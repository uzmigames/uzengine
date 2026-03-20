/// FXAA 3.11 (Fast Approximate Anti-Aliasing) — Timothy Lottes / NVIDIA
///
/// @ref Lottes 2009 "Fast Approximate Anti-Aliasing (FXAA)" (NVIDIA white paper)
/// @ref Lottes 2011 "FXAA 3.11" — quality preset 29, PC path
/// @constants fxaaQualitySubpix=0.75 (PostProcessAA.cpp:94)
/// @constants fxaaQualityEdgeThreshold=0.166 (PostProcessAA.cpp:95)
/// @constants fxaaQualityEdgeThresholdMin=0.0833 (PostProcessAA.cpp:96)
/// @constants FxaaLuma weights=(0.299, 0.587, 0.114) (Fxaa3_11.ush:713)
/// @constants Preset 29 step count=12 (Fxaa3_11.ush:575)
/// @constants Preset 29 steps: P0=1.0 P1=1.5 P2-P9=2.0 P10=4.0 P11=8.0 (Fxaa3_11.ush:576-588)
/// @deviations NONE
///
/// Full FXAA 3.11 Quality PC algorithm with preset 29 (reference "high quality" default).
/// The algorithm has 3 phases:
///   1. Local contrast check — early exit if no edge detected
///   2. Edge direction + sub-pixel aliasing factor computation
///   3. Edge-endpoint search (12 steps each direction) to find edge span
///
/// Runs after tonemapping on LDR scene color. Uses FXAA_GREEN_AS_LUMA=0 mode
/// (computes luma via BT.601 dot product, matching reference Fxaa3_11.ush:713).
///
/// Quality preset 29 searches up to 29 pixels along each edge (12 steps with
/// increasing stride: 1.0, 1.5, 2.0*8, 4.0, 8.0). This matches reference's
/// FXAA_PRESET=4 (the default high-quality PC setting).

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

/// Bound at root slot 2 (kSlotPerObject / register b2) via SetRootCBV.
/// Per-frame FXAA parameters.
cbuffer FXAAParams : register(b2) {
    /// Reciprocal of viewport width: 1.0 / screenWidthInPixels.
    float invScreenWidth;

    /// Reciprocal of viewport height: 1.0 / screenHeightInPixels.
    float invScreenHeight;

    /// Edge detection threshold. Controls minimum local contrast to apply AA.
    /// @ref Lottes 2011 "FXAA 3.11" —819 — 0.166 default, range [0.063, 0.333]
    float edgeThreshold;

    /// Unused in FXAA 3.11 — kept for C++ struct compatibility.
    float blurStrength;
};

// ─────────────────────────────────────────────────────────────────────────────
// Textures & Samplers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
/// Root constants at b0 — SRV heap index for the input scene color texture.
cbuffer RootConstants : register(b0) {
    uint inputTextureIndex;   ///< SRV descriptor heap index for LDR scene color.
    uint _rcPad0;
    uint _rcPad1;
    uint _rcPad2;
};
#else
/// Scene color input (LDR, after tone mapping and bloom).
Texture2D<float4> sceneColor : register(t0);
#endif

/// Bilinear sampler for FXAA sub-pixel sampling.
/// FXAA 3.11 requires bilinear filtering (Fxaa3_11.ush:95).
SamplerState samplerPoint : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Input & Output Structures
// ─────────────────────────────────────────────────────────────────────────────

/// Pixel shader input — fullscreen triangle interpolants.
struct PSInput {
    float4 positionCS : SV_Position;  ///< Clip-space position.
    float2 texcoord   : TEXCOORD0;    ///< Screen UV in [0, 1].
};

// ─────────────────────────────────────────────────────────────────────────────
// FXAA Quality Preset 29 — Search Step Sizes
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Lottes 2011 "FXAA 3.11" —574-588 — FXAA_QUALITY__PRESET 29
/// 12 search steps. Total reach per direction = sum(P0..P11) = 1+1.5+2*8+4+8 = 30.5 texels.
/// FXAA_QUALITY__PS = 12
/// P0=1.0, P1=1.5, P2=2.0, P3=2.0, P4=2.0, P5=2.0,
/// P6=2.0, P7=2.0, P8=2.0, P9=2.0, P10=4.0, P11=8.0

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Texture Sampling
// ─────────────────────────────────────────────────────────────────────────────

/// Sample the input texture at the given UV coordinates using SampleLevel(0).
/// @ref Lottes 2011 "FXAA 3.11" —695-696 — FxaaTexTop macro (FXAA_HLSL_5 path)
float4 FxaaTexTop(float2 uv) {
#ifdef UZ_BINDLESS
    Texture2D<float4> inputTex = ResourceDescriptorHeap[inputTextureIndex];
    return inputTex.SampleLevel(samplerPoint, uv, 0.0);
#else
    return sceneColor.SampleLevel(samplerPoint, uv, 0.0);
#endif
}

/// Sample the input texture at posM + integer texel offset * rcpFrame.
/// @ref Lottes 2011 "FXAA 3.11" —696 — FxaaTexOff macro (FXAA_HLSL_5 path)
/// Note: the reference engine's FxaaTexOff uses SampleLevel with int2 offset parameter, but
/// since we don't have the UVMinMax clamp, we compute the offset manually.
/// This is functionally identical for non-edge-of-viewport pixels.
float4 FxaaTexOff(float2 posM, int2 off, float2 rcpFrame) {
    return FxaaTexTop(posM + float2(off) * rcpFrame);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: Luminance
// ─────────────────────────────────────────────────────────────────────────────

/// Compute luminance from RGB using BT.601 luma weights.
/// @ref Lottes 2011 "FXAA 3.11" —711-714 — FxaaLuma (FXAA_GREEN_AS_LUMA == 0, #if 1 path)
/// uses dot(rgb, float3(0.299, 0.587, 0.114)) for R10G10B2A2 compatibility.
float FxaaLuma(float4 rgba) {
    return dot(rgba.rgb, float3(0.299f, 0.587f, 0.114f));
}

// ─────────────────────────────────────────────────────────────────────────────
// FXAA 3.11 Quality — Main Pixel Shader
// ─────────────────────────────────────────────────────────────────────────────

/// Full FXAA 3.11 Quality PC algorithm, preset 29 (12 search steps).
///
/// @ref Lottes 2011 "FXAA 3.11" —735-1252 — FxaaPixelShader (FXAA_PC == 1)
/// @ref Lottes 2011 — default params:94-96 — default quality parameters
///
/// The algorithm:
///   1. Sample center + 4 cardinal neighbors, compute luma, check local contrast.
///      If contrast < max(edgeThresholdMin, rangeMax * edgeThreshold), early exit.
///   2. Sample 4 corner neighbors. Determine horizontal vs vertical edge.
///      Compute sub-pixel aliasing blend factor via smoothstep-like curve.
///   3. Walk along the edge in both directions (12 steps per direction, preset 29).
///      Find where the edge ends by comparing endpoint luma to the edge's average.
///   4. Compute final pixel offset from the closer endpoint and sub-pixel factor.
///      Sample at the offset position for the anti-aliased result.
float4 main(PSInput input) : SV_Target {
    float2 posM = input.texcoord;
    float2 rcpFrame = float2(invScreenWidth, invScreenHeight);

    // ── Phase 1: Local contrast check (early exit) ──────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —923-953

    // Sample center pixel
    float4 rgbyM = FxaaTexTop(posM);
    float lumaM = FxaaLuma(rgbyM);

    // Sample 4 cardinal neighbors (S, E, N, W)
    // @ref Lottes 2011 "FXAA 3.11" —929-932 — FXAA_GATHER4_ALPHA == 0 path
    float lumaS = FxaaLuma(FxaaTexOff(posM, int2( 0,  1), rcpFrame));
    float lumaE = FxaaLuma(FxaaTexOff(posM, int2( 1,  0), rcpFrame));
    float lumaN = FxaaLuma(FxaaTexOff(posM, int2( 0, -1), rcpFrame));
    float lumaW = FxaaLuma(FxaaTexOff(posM, int2(-1,  0), rcpFrame));

    // Compute local contrast range
    // @ref Lottes 2011 "FXAA 3.11" —935-946
    float maxSM = max(lumaS, lumaM);
    float minSM = min(lumaS, lumaM);
    float maxESM = max(lumaE, maxSM);
    float minESM = min(lumaE, minSM);
    float maxWN = max(lumaN, lumaW);
    float minWN = min(lumaN, lumaW);
    float rangeMax = max(maxWN, maxESM);
    float rangeMin = min(minWN, minESM);
    float rangeMaxScaled = rangeMax * edgeThreshold;
    float range = rangeMax - rangeMin;
    // edgeThreshold acts as fxaaQualityEdgeThreshold (0.166 default from PostProcessAA.cpp:95)
    // FXAA_EDGE_THRESHOLD_MIN = 0.0833 (hardcoded, from PostProcessAA.cpp:96, Fxaa3_11.ush:828)
    float rangeMaxClamped = max(0.0833f, rangeMaxScaled);

    // Early exit: no edge detected — return original color unchanged
    // @ref Lottes 2011 "FXAA 3.11" —948-952
    if (range < rangeMaxClamped) {
        return rgbyM;
    }

    // ── Phase 2: Edge direction + sub-pixel aliasing ────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —955-1028

    // Sample 4 corner neighbors (only fetched if we passed the early exit)
    // @ref Lottes 2011 "FXAA 3.11" —956-959 — FXAA_GATHER4_ALPHA == 0 path
    float lumaNW = FxaaLuma(FxaaTexOff(posM, int2(-1, -1), rcpFrame));
    float lumaSE = FxaaLuma(FxaaTexOff(posM, int2( 1,  1), rcpFrame));
    float lumaNE = FxaaLuma(FxaaTexOff(posM, int2( 1, -1), rcpFrame));
    float lumaSW = FxaaLuma(FxaaTexOff(posM, int2(-1,  1), rcpFrame));

    // Compute edge filter weights — horizontal vs vertical
    // @ref Lottes 2011 "FXAA 3.11" —965-984
    float lumaNS = lumaN + lumaS;
    float lumaWE = lumaW + lumaE;
    float subpixRcpRange = 1.0f / range;
    float subpixNSWE = lumaNS + lumaWE;
    float edgeHorz1 = (-2.0f * lumaM) + lumaNS;
    float edgeVert1 = (-2.0f * lumaM) + lumaWE;

    float lumaNESE = lumaNE + lumaSE;
    float lumaNWNE = lumaNW + lumaNE;
    float edgeHorz2 = (-2.0f * lumaE) + lumaNESE;
    float edgeVert2 = (-2.0f * lumaN) + lumaNWNE;

    float lumaNWSW = lumaNW + lumaSW;
    float lumaSWSE = lumaSW + lumaSE;
    float edgeHorz4 = (abs(edgeHorz1) * 2.0f) + abs(edgeHorz2);
    float edgeVert4 = (abs(edgeVert1) * 2.0f) + abs(edgeVert2);
    float edgeHorz3 = (-2.0f * lumaW) + lumaNWSW;
    float edgeVert3 = (-2.0f * lumaS) + lumaSWSE;
    float edgeHorz = abs(edgeHorz3) + edgeHorz4;
    float edgeVert = abs(edgeVert3) + edgeVert4;

    // Determine span direction (horizontal edge = blend vertically, and vice versa)
    // @ref Lottes 2011 "FXAA 3.11" —986-994
    float subpixNWSWNESE = lumaNWSW + lumaNESE;
    float lengthSign = rcpFrame.x;
    bool horzSpan = edgeHorz >= edgeVert;
    float subpixA = subpixNSWE * 2.0f + subpixNWSWNESE;

    // Re-assign lumaN/lumaS based on edge direction
    // For vertical edges, the "north" and "south" become "west" and "east"
    // @ref Lottes 2011 "FXAA 3.11" —991-994
    if (!horzSpan) lumaN = lumaW;
    if (!horzSpan) lumaS = lumaE;
    if (horzSpan)  lengthSign = rcpFrame.y;

    // Sub-pixel blend factor: average of all 8 neighbors vs center
    // @ref Lottes 2011 "FXAA 3.11" —994
    float subpixB = (subpixA * (1.0f / 12.0f)) - lumaM;

    // Determine which side of the edge has higher contrast
    // @ref Lottes 2011 "FXAA 3.11" —996-1003
    float gradientN = lumaN - lumaM;
    float gradientS = lumaS - lumaM;
    float lumaNN = lumaN + lumaM;
    float lumaSS = lumaS + lumaM;
    bool pairN = abs(gradientN) >= abs(gradientS);
    float gradient = max(abs(gradientN), abs(gradientS));
    if (pairN) lengthSign = -lengthSign;
    float subpixC = saturate(abs(subpixB) * subpixRcpRange);

    // Set up search starting position (half-texel offset perpendicular to edge)
    // @ref Lottes 2011 "FXAA 3.11" —1005-1012
    float2 posB;
    posB.x = posM.x;
    posB.y = posM.y;
    float2 offNP;
    offNP.x = (!horzSpan) ? 0.0f : rcpFrame.x;
    offNP.y = ( horzSpan) ? 0.0f : rcpFrame.y;
    if (!horzSpan) posB.x += lengthSign * 0.5f;
    if ( horzSpan) posB.y += lengthSign * 0.5f;

    // Initialize search positions (first step = P0 = 1.0 texel)
    // @ref Lottes 2011 "FXAA 3.11" —1014-1023
    float2 posN;
    posN.x = posB.x - offNP.x * 1.0f;  // FXAA_QUALITY__P0 = 1.0
    posN.y = posB.y - offNP.y * 1.0f;
    float2 posP;
    posP.x = posB.x + offNP.x * 1.0f;
    posP.y = posB.y + offNP.y * 1.0f;

    // Sub-pixel smoothstep: subpixD = (-2*subpixC + 3) * subpixC^2
    // @ref Lottes 2011 "FXAA 3.11" —1020-1022
    float subpixD = ((-2.0f) * subpixC) + 3.0f;
    float subpixE = subpixC * subpixC;
    float subpixF = subpixD * subpixE;

    // Sample luma at initial search endpoints
    float lumaEndN = FxaaLuma(FxaaTexTop(posN));
    float lumaEndP = FxaaLuma(FxaaTexTop(posP));

    // ── Phase 3: Edge-endpoint search (12 steps, preset 29) ─────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1025-1224
    // Unrolled from nested #if FXAA_QUALITY__PS > N blocks.
    // Each step: check if endpoint found, advance position by step size.

    // Reference luma for endpoint comparison
    // @ref Lottes 2011 "FXAA 3.11" —1025-1029
    if (!pairN) lumaNN = lumaSS;
    float gradientScaled = gradient * 1.0f / 4.0f;
    float lumaMM = lumaM - lumaNN * 0.5f;
    bool lumaMLTZero = lumaMM < 0.0f;

    // Subtract the edge's average luma from endpoint samples
    // @ref Lottes 2011 "FXAA 3.11" —1031-1032
    lumaEndN -= lumaNN * 0.5f;
    lumaEndP -= lumaNN * 0.5f;

    // Check if endpoints already found at step 0
    bool doneN = abs(lumaEndN) >= gradientScaled;
    bool doneP = abs(lumaEndP) >= gradientScaled;

    // ── Step 1 (P1 = 1.5) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1035-1039
    if (!doneN) posN.x -= offNP.x * 1.5f;
    if (!doneN) posN.y -= offNP.y * 1.5f;
    bool doneNP = (!doneN) || (!doneP);
    if (!doneP) posP.x += offNP.x * 1.5f;
    if (!doneP) posP.y += offNP.y * 1.5f;

    // ── Step 2 (P2 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1041-1052
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 3 (P3 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1055-1066
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 4 (P4 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1069-1080
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 5 (P5 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1083-1094
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 6 (P6 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1097-1108
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 7 (P7 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1111-1122
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 8 (P8 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1125-1136
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 9 (P9 = 2.0) ───────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1139-1150
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 2.0f;
        if (!doneN) posN.y -= offNP.y * 2.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 2.0f;
        if (!doneP) posP.y += offNP.y * 2.0f;

    // ── Step 10 (P10 = 4.0) ─────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1153-1164
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 4.0f;
        if (!doneN) posN.y -= offNP.y * 4.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 4.0f;
        if (!doneP) posP.y += offNP.y * 4.0f;

    // ── Step 11 (P11 = 8.0) ─────────────────────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1167-1178
    if (doneNP) {
        if (!doneN) lumaEndN = FxaaLuma(FxaaTexTop(posN.xy));
        if (!doneP) lumaEndP = FxaaLuma(FxaaTexTop(posP.xy));
        if (!doneN) lumaEndN = lumaEndN - lumaNN * 0.5f;
        if (!doneP) lumaEndP = lumaEndP - lumaNN * 0.5f;
        doneN = abs(lumaEndN) >= gradientScaled;
        doneP = abs(lumaEndP) >= gradientScaled;
        if (!doneN) posN.x -= offNP.x * 8.0f;
        if (!doneN) posN.y -= offNP.y * 8.0f;
        doneNP = (!doneN) || (!doneP);
        if (!doneP) posP.x += offNP.x * 8.0f;
        if (!doneP) posP.y += offNP.y * 8.0f;

    // Close all nested if(doneNP) blocks (steps 11 through 2)
    }}}}}}}}}}

    // ── Phase 4: Compute final pixel offset ─────────────────────────────────
    // @ref Lottes 2011 "FXAA 3.11" —1226-1251

    // Compute distance from center pixel to each endpoint
    // @ref Lottes 2011 "FXAA 3.11" —1226-1229
    float dstN = posM.x - posN.x;
    float dstP = posP.x - posM.x;
    if (!horzSpan) dstN = posM.y - posN.y;
    if (!horzSpan) dstP = posP.y - posM.y;

    // Determine if each endpoint was a "good" span (luma changed in the expected direction)
    // @ref Lottes 2011 "FXAA 3.11" —1231-1234
    bool goodSpanN = (lumaEndN < 0.0f) != lumaMLTZero;
    float spanLength = (dstP + dstN);
    bool goodSpanP = (lumaEndP < 0.0f) != lumaMLTZero;
    float spanLengthRcp = 1.0f / spanLength;

    // Choose the closer endpoint direction
    // @ref Lottes 2011 "FXAA 3.11" —1236-1241
    bool directionN = dstN < dstP;
    float dstMin = min(dstN, dstP);
    bool goodSpan = directionN ? goodSpanN : goodSpanP;
    // Sub-pixel quality: subpixG = subpixF^2, subpixH = subpixG * fxaaQualitySubpix
    // fxaaQualitySubpix = 0.75 (reference PostProcessAA.cpp:94)
    float subpixG = subpixF * subpixF;
    float pixelOffset = (dstMin * (-spanLengthRcp)) + 0.5f;
    float subpixH = subpixG * 0.75f;

    // Final offset: use edge offset only if the span was "good", otherwise 0
    // Then take the max of edge offset and sub-pixel offset
    // @ref Lottes 2011 "FXAA 3.11" —1243-1244
    float pixelOffsetGood = goodSpan ? pixelOffset : 0.0f;
    float pixelOffsetSubpix = max(pixelOffsetGood, subpixH);

    // Apply the offset perpendicular to the edge and sample the final color
    // @ref Lottes 2011 "FXAA 3.11" —1245-1248
    if (!horzSpan) posM.x += pixelOffsetSubpix * lengthSign;
    if ( horzSpan) posM.y += pixelOffsetSubpix * lengthSign;

    return FxaaTexTop(posM);
}
