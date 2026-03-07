// sky.ps.hlsl -- UE5-style atmospheric sky with Rayleigh/Mie approximation
//
// Reconstructs world-space ray direction from NDC + camera basis vectors.
// Sky colors are configurable per-scene via SkyColorsCB (b0).
// Colors are in linear space; gamma correction applied at the end.
//
// Root constants:
//   b0: SkyColorsCB  -- 4 sky gradient colors + fog density (16 DWORDs)
//   b3: SkyCB        -- camera basis vectors + sun direction (16 DWORDs)

// Sky gradient colors -- set from SkyAtmosphere scene component.
cbuffer SkyColorsCB : register(b0) {
    float3 skyZenith;   float _sc0;  // deep blue zenith (straight up)
    float3 skyHorizon;  float _sc1;  // bright sky-blue at horizon
    float3 skyHaze;     float _sc2;  // warm atmospheric haze below horizon
    float3 skyGround;   float fogDensity;  // ground color / height fog density
};

cbuffer SkyCB : register(b3) {
    float3 camForward;  float tanHalfFovY;
    float3 camRight;    float aspect;
    float3 camUp;       float _pad;
    float3 sunDir;      float sunIntensity;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 ndc      : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_TARGET {
    // Reconstruct world-space ray direction from NDC + perspective camera vectors.
    float3 worldDir = normalize(
        camForward
        + camRight * (input.ndc.x * aspect * tanHalfFovY)
        + camUp    * (input.ndc.y * tanHalfFovY)
    );

    float elevation = worldDir.y;  // -1 (down) to +1 (up)

    // === Upper hemisphere: horizon -> zenith ===
    // UE5-style: strong blue saturation with smooth power curve
    float3 color;
    if (elevation >= 0.0) {
        // Cubic blend gives deep blue at zenith, bright at horizon (like UE5 SkyAtmosphere)
        float u = saturate(elevation);
        float blend = 1.0 - pow(1.0 - u, 3.0);
        color = lerp(skyHorizon, skyZenith, blend);

        // Rayleigh-inspired: boost blue channel slightly toward zenith
        color.b += u * u * 0.08;
    } else {
        // Below horizon: haze -> ground
        float d = saturate(-elevation);
        if (d < 0.15) {
            // Narrow haze band just below horizon
            float u = d / 0.15;
            color = lerp(skyHorizon, skyHaze, u * u);
        } else {
            float u = saturate((d - 0.15) / 0.85);
            color = lerp(skyHaze, skyGround, u);
        }
    }

    // === Mie scattering halo around sun (warm glow at horizon) ===
    float3 sunDirN = normalize(-sunDir);
    float sunCosAngle = dot(worldDir, sunDirN);

    // Horizon glow: warm orange scattered light, strongest at low sun angles
    float horizonFactor = exp(-abs(elevation) * 6.0);
    float miePhaseFn = pow(saturate(sunCosAngle * 0.5 + 0.5), 8.0);
    float3 mieColor = float3(1.0, 0.75, 0.42);
    color += mieColor * horizonFactor * miePhaseFn * 0.15 * sunIntensity;

    // Subtle overall horizon brightening (atmospheric path length effect)
    color += skyHorizon * horizonFactor * horizonFactor * 0.06;

    // === Sun disk + corona ===
    float sunDisk   = smoothstep(0.9997, 0.99995, sunCosAngle);
    float sunCorona = exp(-max(0.0, 1.0 - sunCosAngle) * 120.0) * 0.25;
    float sunBloom  = exp(-max(0.0, 1.0 - sunCosAngle) * 30.0) * 0.08;
    float3 sunColor = float3(1.3, 1.1, 0.85) * sunIntensity;
    color += sunDisk * sunColor * 5.0;
    color += sunCorona * sunColor;
    color += sunBloom * float3(1.0, 0.85, 0.6) * sunIntensity;

    // Output linear HDR — tonemap + gamma applied in composite post-process pass.
    return float4(max(color, 0.0), 1.0);
}
