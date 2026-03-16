/// Height fog pixel shader — UE5 ExponentialHeightFog parity.
///
/// Implements UE5's GetExponentialHeightFog() as a fullscreen post-process.
/// Uses the exact same CalculateLineIntegralShared() formula from
/// HeightFogCommon.ush with pre-computed RayOriginTerms from the CPU.
///
/// Key UE5 behaviors replicated:
/// - Pre-computed RayOriginTerms = FogDensity * exp2(-falloff * (camZ - fogHeight))
/// - CalculateLineIntegralShared with Taylor expansion fallback for near-zero Falloff
/// - Fog affects BOTH geometry AND sky (sky uses far-plane distance for haze)
/// - Directional inscattering (sun glow in fog)
/// - FogCutoffDistance clamp
///
/// # Inputs
/// - PerFrameCB at b1: camera, invViewProj, lightDir
/// - HeightFogCB at b2: pre-computed fog parameters
/// - Depth texture (SRV at t0 / bindless)

struct FSInput {
    float4 positionCS : SV_Position;
    float2 texcoord   : TEXCOORD0;
};

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
    float4   lightDir;
    float4   lightColor;
    float4   ambientColor;
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    float    _framePad[10];
};

cbuffer HeightFogCB : register(b2) {
    // Primary layer
    float4 fogColor;           // xyz = inscattering color (UE5: FogInscatteringColor)
    float  fogDensity;         // PRE-COMPUTED: density * exp2(-falloff * (camZ - fogHeight))
    float  fogHeightFalloff;   // Height falloff coefficient
    float  fogBaseHeight;      // Fog base height (cm)
    float  fogMaxOpacity;      // Max opacity (0-1)
    float  fogStartDistance;   // Start distance (cm)
    float  fogCutoffDistance;  // Cutoff distance (0 = unlimited)
    float  _fogPad0[2];

    // Second layer
    float  secondFogDensity;
    float  secondFogHeightFalloff;
    float  secondFogBaseHeight;
    float  _fogPad1;

    // Directional inscattering
    float4 directionalInscatteringColor;
    float  directionalInscatteringExponent;
    float  directionalInscatteringStartDistance;
    float  _fogPad2[2];
};

#ifndef UZ_BINDLESS
Texture2D<float> sceneDepth : register(t0);
#endif

SamplerState linearSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// UE5 CalculateLineIntegralShared (HeightFogCommon.ush line 192)
// ─────────────────────────────────────────────────────────────────────────────

/// Calculates the line integral of exponential height fog along a ray.
/// This is the EXACT UE5 formula from HeightFogCommon.ush.
///
/// RayOriginTerms = FogDensity * exp2(-FogHeightFalloff * (CamZ - FogHeight))
///   (pre-computed on CPU and passed via fogDensity in the CB)
float CalculateLineIntegralShared(float FogHeightFalloff_val, float RayDirectionZ, float RayOriginTerms_val)
{
    float Falloff = max(-127.0f, FogHeightFalloff_val * RayDirectionZ);
    float LineIntegral = (1.0f - exp2(-Falloff)) / Falloff;
    // Taylor expansion around Falloff=0 to avoid division by zero
    float LineIntegralTaylor = log(2.0f) - (0.5f * log(2.0f) * log(2.0f)) * Falloff;

    return RayOriginTerms_val * (abs(Falloff) > 0.01f ? LineIntegral : LineIntegralTaylor);
}

/// Reconstructs world-space position from screen UV and hardware depth.
float3 ReconstructWorldPosition(float2 uv, float depth) {
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clipPos = float4(ndc, depth, 1.0f);
    float4 worldPos = mul(clipPos, invViewProj);
    return worldPos.xyz / worldPos.w;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main
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

    // ── Reconstruct world position ──
    // For sky pixels (depth >= 0.9999), use a far-away point along the view ray.
    // This allows fog to produce horizon haze on the sky (UE5 behavior).
    float3 camPos3 = cameraPos.xyz;
    float3 worldPos;
    float3 viewVec;
    float  dist;

    bool isSky = (depth >= 0.9999f);

    if (isSky) {
        // Sky pixel: project ray to a very far distance for fog integration.
        // UE5 uses the actual far plane or EndDistance for sky fog.
        float3 farWorldPos = ReconstructWorldPosition(uv, 0.9998f);
        float3 rayDir = normalize(farWorldPos - camPos3);
        float  skyDist = 50000.0f;  // 500m — far enough for visible haze
        worldPos = camPos3 + rayDir * skyDist;
        viewVec  = worldPos - camPos3;
        dist     = skyDist;
    } else {
        worldPos = ReconstructWorldPosition(uv, depth);
        viewVec  = worldPos - camPos3;
        dist     = length(viewVec);
    }

    // ── Start distance exclusion ──
    float rayLength = dist;
    float rayDirZ   = viewVec.z;

    float rayOriginTerms = fogDensity;  // Already pre-computed on CPU

    if (fogStartDistance > 0.0f && dist > fogStartDistance) {
        // UE5: recalculate RayOriginTerms from the exclusion intersection point
        float excludeTime = fogStartDistance / dist;
        float excludeZ    = camPos3.z + excludeTime * viewVec.z;

        rayLength = dist - fogStartDistance;
        rayDirZ   = viewVec.z * (1.0f - excludeTime);

        // Recompute RayOriginTerms at the exclusion intersection height
        float exponent = max(-127.0f, fogHeightFalloff * (excludeZ - fogBaseHeight));
        rayOriginTerms = fogDensity * exp2(-exponent) / max(fogDensity, 1e-10f);
        // Simplified: use raw density at exclusion height
        // fogDensity in CB is already pre-computed, so we need raw density
        // Actually fogDensity = rawDensity * exp2(-falloff*(camZ-baseH))
        // At exclusion point: rawDensity * exp2(-falloff*(exclZ-baseH))
        // So ratio = exp2(-falloff*(exclZ-baseH)) / exp2(-falloff*(camZ-baseH))
        //          = exp2(-falloff*(exclZ-camZ))
        float heightShift = excludeZ - camPos3.z;
        rayOriginTerms = fogDensity * exp2(-fogHeightFalloff * heightShift);
    } else if (fogStartDistance > 0.0f && dist <= fogStartDistance) {
        discard;
    }

    // ── Line integral (UE5 CalculateLineIntegralShared) ──
    float lineIntegral = CalculateLineIntegralShared(fogHeightFalloff, rayDirZ, rayOriginTerms);
    float fogAmount = lineIntegral * rayLength;

    // ── Fog factor (UE5: ExpFogFactor) ──
    float fogFactor = max(saturate(exp2(-fogAmount)), 1.0f - fogMaxOpacity);

    // ── Cutoff distance ──
    if (fogCutoffDistance > 0.0f && dist > fogCutoffDistance) {
        fogFactor = 1.0f;  // No fog beyond cutoff
    }

    float fogOpacity = 1.0f - fogFactor;

    if (fogOpacity < 0.001f)
        discard;

    // ── Inscattering color ──
    float3 inscatterColor = fogColor.xyz;

    // ── Directional inscattering (sun glow) ──
    if (directionalInscatteringExponent > 0.0f) {
        float3 viewDir = viewVec / max(dist, 0.001f);
        float3 sunDir  = normalize(lightDir.xyz);
        float  cosAngle = saturate(dot(viewDir, sunDir));
        float  dirFactor = pow(cosAngle, directionalInscatteringExponent);

        // UE5: directional inscattering uses a separate line integral
        // with its own start distance
        float dirFogAmount = lineIntegral * max(rayLength - directionalInscatteringStartDistance, 0.0f);
        float dirFogFactor = saturate(exp2(-dirFogAmount));
        float3 dirInscatter = directionalInscatteringColor.xyz * dirFactor * (1.0f - dirFogFactor);

        // With SrcAlpha/InvSrcAlpha blending:
        //   result = src.rgb * src.a + dst.rgb * (1 - src.a)
        //          = (inscatter + dirInscatter/fogOpacity) * fogOpacity + scene * fogFactor
        // So pack directional inscattering into the color and use fogOpacity as alpha
        float3 totalInscatter = inscatterColor + dirInscatter / max(fogOpacity, 0.001f);
        return float4(totalInscatter, fogOpacity);
    }

    // No directional inscattering: simple fog
    // With SrcAlpha/InvSrcAlpha: result = inscatterColor * fogOpacity + scene * (1-fogOpacity)
    return float4(inscatterColor, fogOpacity);
}
