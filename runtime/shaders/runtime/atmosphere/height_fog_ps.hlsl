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
    // @source UE5 FogRendering.cpp:257 — ExponentialFogParameters.z
    float  maxObserverHeight;  // Clamped max observer Z for shader-side height compensation
    // @source UE5 FogRendering.cpp:261 — ExponentialFogParameters3.x
    float  rawDensity;         // Raw (un-precomputed) density for start distance recalculation

    // Second layer
    // @source UE5 HeightFogCommon.ush:60 — ExponentialFogParameters2
    // .x = precomputed RayOriginTermsSecond (density * exp2(-falloff*(camZ-baseH)))
    // .y = secondFogHeightFalloff
    // .z = secondFogDensity (raw, for start distance recalc)
    // .w = secondFogBaseHeight (for start distance recalc)
    float  secondFogDensity;         // PRE-COMPUTED: secondDensity * exp2(-secondFalloff*(camZ-secondBaseH))
    float  secondFogHeightFalloff;
    float  secondFogRawDensity;      // Raw (un-precomputed) density for start distance recalculation
    float  secondFogBaseHeight;

    // Directional inscattering
    float4 directionalInscatteringColor;
    float  directionalInscatteringExponent;
    float  directionalInscatteringStartDistance;
    float  _fogPad2[2];

    /// @source UE5 HeightFogCommon.ush:174-180 — Sky atmosphere ambient injection
    /// @source UE5 FogRendering.cpp:85 — SkyAtmosphereAmbientContributionColorScale
    float4 skyAtmContribColorScale;    // xyz = SkyAtmosphereAmbientContributionColorScale (default white)
    float  heightFogContribution;      // SkyAtmosphere.HeightFogContribution (0-1, default 1.0)
    float3 skyLightColor;             // SkyLight diffuse color (from SH L0 or cubemap average)

    /// @source UE5 SceneRendering.cpp:1451 — AtmosphereLightIlluminanceOnGroundPostTransmittance
    /// Sun color * intensity * atmospheric transmittance at ground level.
    float  sunIllumPostTransR;        // Post-transmittance sun illuminance R
    float  sunIllumPostTransG;        // Post-transmittance sun illuminance G
    float  sunIllumPostTransB;        // Post-transmittance sun illuminance B
    float  sunIllumPostTransA;        // 1.0 = has atmosphere sun, 0.0 = none

    /// @source UE5 HeightFogCommon.ush:119 — InscatteringLightDirection
    float  sunDirX;                   // Sun direction X (world-space, toward sun)
    float  sunDirY;                   // Sun direction Y
    float  sunDirZ;                   // Sun direction Z
    float  _sunPad;
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
    // Reversed-Z: sky pixels have depth ≈ 0.0 (far plane).
    // For sky pixels, use a far-away point along the view ray for fog integration.
    float3 camPos3 = cameraPos.xyz;
    float3 worldPos;
    float3 viewVec;
    float  dist;

    bool isSky = (depth <= 0.0001f);

    if (isSky) {
        // Sky pixel: project ray to a very far distance for fog integration.
        // Use depth near 1.0 (near plane in reversed-Z) to get a valid world position,
        // then extend the ray direction to a far distance.
        float3 farWorldPos = ReconstructWorldPosition(uv, 0.0002f);
        float3 rayDir = normalize(farWorldPos - camPos3);
        /// @source UE5 HeightFogPixelShader.usf — uses camera far plane distance for sky fog ray length
        float  skyDist = farPlane;
        worldPos = camPos3 + rayDir * skyDist;
        viewVec  = worldPos - camPos3;
        dist     = skyDist;
    } else {
        worldPos = ReconstructWorldPosition(uv, depth);
        viewVec  = worldPos - camPos3;
        dist     = length(viewVec);
    }

    // ── Observer height clamp (UE5 parity) ──
    /// @source UE5 HeightFogCommon.ush:237-239 — Clamp observer Z and compensate view vector
    float3 observerOrigin = float3(camPos3.xy, min(camPos3.z, maxObserverHeight));
    viewVec.z += camPos3.z - observerOrigin.z; // Compensate for clamped observer height
    float rayLength = length(viewVec);
    float rayDirZ   = viewVec.z;

    /// @source UE5 HeightFogCommon.ush:248-249 — RayOriginTerms and RayOriginTermsSecond
    float rayOriginTerms = fogDensity;  // Already pre-computed on CPU
    float rayOriginTermsSecond = secondFogDensity; // Pre-computed on CPU for second layer

    /// @source UE5 HeightFogCommon.ush:272-290 — Start distance exclusion
    /// UE5 recalculates BOTH layers' RayOriginTerms from raw density at the
    /// exclusion intersection height, not by ratio from precomputed terms.
    /// @source UE5 HeightFogCommon.ush:270-290 — Start distance exclusion
    float excludeDist = max(0.0f, fogStartDistance);
    if (excludeDist > 0.0f) {
        if (rayLength <= excludeDist) {
            discard;
        }
        float excludeTime = excludeDist / rayLength;
        float excludeZ    = observerOrigin.z + excludeTime * viewVec.z;

        rayLength = (1.0f - excludeTime) * rayLength;
        rayDirZ   = viewVec.z - excludeTime * viewVec.z;

        /// @source UE5 HeightFogCommon.ush:285 — Recalc layer 1 from raw density at exclusion Z
        float exponent1 = max(-127.0f, fogHeightFalloff * (excludeZ - fogBaseHeight));
        rayOriginTerms = rawDensity * exp2(-exponent1);

        /// @source UE5 HeightFogCommon.ush:288 — Recalc layer 2 from raw density at exclusion Z
        if (secondFogRawDensity > 0.0f) {
            float exponent2 = max(-127.0f, secondFogHeightFalloff * (excludeZ - secondFogBaseHeight));
            rayOriginTermsSecond = secondFogRawDensity * exp2(-exponent2);
        }
    }

    // ── Line integral (UE5 CalculateLineIntegralShared) ──
    /// @source UE5 HeightFogCommon.ush:294 — Layer 1 line integral
    float lineIntegral = CalculateLineIntegralShared(fogHeightFalloff, rayDirZ, rayOriginTerms);

    /// @source UE5 HeightFogCommon.ush:296 — Layer 2 additive combination
    /// @constants PERMUTATION_SUPPORT_FOG_SECOND_TERM = 1 (HeightFogCommon.ush:30)
    /// @deviations NONE — guarded by density > 0 check instead of permutation define
    if (secondFogRawDensity > 0.0f) {
        lineIntegral += CalculateLineIntegralShared(secondFogHeightFalloff, rayDirZ, rayOriginTermsSecond);
    }

    /// @source UE5 HeightFogCommon.ush:299 — ExponentialHeightLineIntegral = shared * RayLength
    float fogAmount = lineIntegral * rayLength;

    /// @source UE5 HeightFogCommon.ush:362 — ExpFogFactor = max(saturate(exp2(-integral)), MinFogOpacity)
    /// @constants MinFogOpacity = ExponentialFogColorParameter.w (HeightFogCommon.ush:230)
    /// @deviations NONE
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

    /// @source UE5 HeightFogCommon.ush:174-180 — Sky atmosphere ambient injection
    /// When SkyAtmosphere is active, add sky color to fog inscattering.
    /// This is what creates the blue/white horizon haze in UE5's Basic scene.
    /// Inscattering += ColorScale * Contribution * SkyLightColor
    inscatterColor += skyAtmContribColorScale.xyz
                    * heightFogContribution
                    * skyLightColor;

    // ── Directional inscattering with atmosphere transmittance ──
    /// @source UE5 HeightFogCommon.ush:319 — DirectionalInscatteringColor
    ///   + SkyAtmosphereHeightFogContribution * AtmosphereLightIlluminanceOnGroundPostTransmittance
    /// The post-transmittance sun illuminance tints directional inscattering with
    /// the atmosphere's color filtering (Rayleigh blue shift at sunset, warm at noon).
    {
        // Build the directional inscattering base color.
        // UE5 adds the atmosphere-filtered sun illuminance to the artistic inscattering color.
        float3 dirInscatterColor = directionalInscatteringColor.xyz;
        if (sunIllumPostTransA > 0.0f) {
            dirInscatterColor += heightFogContribution
                * float3(sunIllumPostTransR, sunIllumPostTransG, sunIllumPostTransB);
        }

        // Compute directional phase and fog factor.
        float3 viewDir = viewVec / max(dist, 0.001f);
        float3 sunDirection = float3(sunDirX, sunDirY, sunDirZ);

        /// @source UE5 HeightFogCommon.ush:323-330 — directional inscattering phase
        /// UE5 uses pow(saturate(dot(ViewDir, LightDir)), Exponent) * UniformPhaseFunction.
        /// @constants UniformPhaseFunction = 1 / (4 * PI) (HeightFogCommon.ush:324)
        float cosAngle = saturate(dot(viewDir, sunDirection));
        float dirExponent = max(directionalInscatteringExponent, 0.001f);
        float phase = pow(cosAngle, dirExponent);

        // UE5: directional inscattering uses a separate line integral
        // with its own start distance
        float dirFogAmount = lineIntegral * max(rayLength - directionalInscatteringStartDistance, 0.0f);
        float dirFogFactor = saturate(exp2(-dirFogAmount));
        float3 dirInscatter = dirInscatterColor * phase * (1.0f - dirFogFactor);

        // With SrcAlpha/InvSrcAlpha blending:
        //   result = src.rgb * src.a + dst.rgb * (1 - src.a)
        //          = (inscatter + dirInscatter/fogOpacity) * fogOpacity + scene * fogFactor
        // So pack directional inscattering into the color and use fogOpacity as alpha
        float3 totalInscatter = inscatterColor + dirInscatter / max(fogOpacity, 0.001f);
        return float4(totalInscatter, fogOpacity);
    }
}
