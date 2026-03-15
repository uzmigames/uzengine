/// Height fog pixel shader — exponential height fog via alpha blending.
///
/// Reads scene depth from the G-Buffer, reconstructs world position,
/// computes exponential height fog density along the view ray, and
/// outputs fog color with fog opacity as alpha. The PSO uses SrcAlpha /
/// InvSrcAlpha blending to composite fog over the existing backbuffer.
///
/// Supports:
/// - Primary exponential height fog layer
/// - Second fog layer (independent density/height/falloff)
/// - Directional inscattering (sun-facing fog glow)
/// - Fog cutoff distance
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, lightDir
/// - HeightFogCB at b2: fog parameters (two layers + inscattering)
/// - Depth texture (SRV at t0 / bindless)
///
/// # Output
/// float4(fogColor.rgb, fogFactor) — alpha-blended over the backbuffer by the PSO.

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffers
// ─────────────────────────────────────────────────────────────────────────────

#ifdef UZ_BINDLESS
struct FogIndices {
    uint depthIdx;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};
ConstantBuffer<FogIndices> fogIndices : register(b0);
#endif

cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;       // xyz = direction toward the sun
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _framePad[10]; // exposure is at offset 248, but fog pass doesn't need it
};

cbuffer HeightFogCB : register(b2) {
    // Primary layer
    float4 fogColor;
    float  fogDensity;
    float  fogHeightFalloff;
    float  fogBaseHeight;
    float  fogMaxOpacity;
    float  fogStartDistance;
    float  fogCutoffDistance;       // 0 = unlimited
    float  _fogPad0[2];

    // Second layer
    float  secondFogDensity;       // 0 = disabled
    float  secondFogHeightFalloff;
    float  secondFogBaseHeight;
    float  _fogPad1;

    // Directional inscattering
    float4 directionalInscatteringColor;  // xyz = sun glow color
    float  directionalInscatteringExponent;
    float  directionalInscatteringStartDistance;
    float  _fogPad2[2];
};

#ifndef UZ_BINDLESS
Texture2D<float> sceneDepth : register(t0);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Utility Functions
// ─────────────────────────────────────────────────────────────────────────────

/// Reconstructs world-space position from screen UV and hardware depth.
float3 ReconstructWorldPosition(float2 uv, float depth) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

/// Computes exponential height fog for a single layer.
/// Uses Z as the height axis (Z-up world).
///
/// # Returns
/// Raw fog amount (before 1-exp and opacity clamping).
float ComputeFogLayer(float3 camPos, float3 worldPos, float dist,
                       float density, float falloff, float baseH)
{
    float camHeight  = camPos.z - baseH;
    float fragHeight = worldPos.z - baseH;
    float heightDiff = fragHeight - camHeight;

    float fogAmount;
    if (abs(falloff * heightDiff) > 0.001f) {
        // Analytical integration of exp(-falloff * h) along the ray
        float expCam  = exp(-falloff * camHeight);
        float expFrag = exp(-falloff * fragHeight);
        fogAmount = density * dist * (expCam - expFrag) / (falloff * heightDiff);
    } else {
        // Nearly horizontal ray — use density at camera height
        fogAmount = density * dist * exp(-falloff * camHeight);
    }
    return max(fogAmount, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

float4 main(FSInput input) : SV_Target {
    float2 uv = input.texcoord;

    // ── Read depth ──
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex = ResourceDescriptorHeap[fogIndices.depthIdx];
    float depth = depthTex.Sample(linearSampler, uv).r;
#else
    float depth = sceneDepth.Sample(linearSampler, uv).r;
#endif

    // Skip sky pixels — fog does not affect the sky
    if (depth >= 0.9999f)
        discard;

    // ── Reconstruct world position ──
    float3 worldPos = ReconstructWorldPosition(uv, depth);
    float3 camPos3  = cameraPos.xyz;
    float3 viewVec  = worldPos - camPos3;
    float  dist     = length(viewVec);

    // No fog before start distance
    if (dist < fogStartDistance)
        discard;

    // No fog beyond cutoff distance (0 = unlimited)
    if (fogCutoffDistance > 0.0f && dist > fogCutoffDistance)
        discard;

    float effectiveDistance = dist - fogStartDistance;

    // ── Primary fog layer ──
    float fogAmount = ComputeFogLayer(camPos3, worldPos, effectiveDistance,
                                       fogDensity, fogHeightFalloff, fogBaseHeight);

    // ── Second fog layer (additive) ──
    if (secondFogDensity > 0.0f) {
        fogAmount += ComputeFogLayer(camPos3, worldPos, effectiveDistance,
                                      secondFogDensity, secondFogHeightFalloff, secondFogBaseHeight);
    }

    // Convert accumulated optical depth to fog factor
    float fog = 1.0f - exp(-fogAmount);
    fog = min(fog, fogMaxOpacity);

    if (fog < 0.001f)
        discard;

    // ── Directional inscattering (sun-facing fog glow) ──
    float3 finalFogColor = fogColor.xyz;

    if (directionalInscatteringExponent > 0.0f && dist > directionalInscatteringStartDistance) {
        float3 viewDir = viewVec / dist;
        float3 sunDir  = normalize(lightDir.xyz);
        float  cosAngle = saturate(dot(viewDir, sunDir));
        float  inscatterFactor = pow(cosAngle, directionalInscatteringExponent);

        // Blend inscattering color toward sun direction
        finalFogColor = lerp(finalFogColor, directionalInscatteringColor.xyz, inscatterFactor);
    }

    return float4(finalFogColor, fog);
}
