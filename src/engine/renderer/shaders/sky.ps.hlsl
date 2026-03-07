// sky.ps.hlsl -- Physically-inspired atmospheric gradient sky
//
// Reconstructs world-space ray direction from NDC + camera basis vectors.
// Sky colors are configurable per-scene via SkyColorsCB (b0).
// Colors are in linear space before gamma correction.
//
// Root constants:
//   b0: SkyColorsCB  — 4 sky gradient colors + fog density (16 DWORDs)
//   b3: SkyCB        — camera basis vectors + sun direction (16 DWORDs)

// Sky gradient colors — set from SkyAtmosphere scene component.
// Defaults match the original hardcoded UE5-inspired palette.
cbuffer SkyColorsCB : register(b0) {
    float3 skyZenith;   float _sc0;  // deep azure zenith (straight up)
    float3 skyHorizon;  float _sc1;  // pale bright sky-blue at horizon
    float3 skyHaze;     float _sc2;  // warm atmospheric haze below horizon
    float3 skyGround;   float fogDensity;  // muted earth / height fog density
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

    // Elevation blend: 0 = straight down, 1 = straight up
    float t = worldDir.y * 0.5 + 0.5;

    // Sky gradient: 4 configurable stops from SkyColorsCB.
    // skyGround → skyHaze → skyHorizon → skyZenith
    float3 color;
    if (t > 0.5) {
        // Upper hemisphere: horizon → zenith (quadratic blend for natural falloff)
        float u  = (t - 0.5) * 2.0;  // 0 at horizon, 1 at zenith
        color = lerp(skyHorizon, skyZenith, u * u);
    } else if (t > 0.40) {
        // Transition band just below horizon: haze → horizon
        float u = (t - 0.40) / 0.10;
        color = lerp(skyHaze, skyHorizon, u * u);
    } else {
        // Lower sky: ground → haze
        float u = t / 0.40;
        color = lerp(skyGround, skyHaze, u);
    }

    // Subtle warm glow right at the horizon (Rayleigh scatter effect)
    float horizonGlow = exp(-abs(worldDir.y) * 10.0);
    color += float3(0.55, 0.40, 0.20) * horizonGlow * horizonGlow * 0.12;

    // Sun disk + soft corona (uses scene directional light direction)
    float sunAlignment = dot(worldDir, normalize(-sunDir));
    float sunDisk   = smoothstep(0.9998, 1.0, sunAlignment);
    float sunCorona = exp(-max(0.0, 1.0 - sunAlignment) * 80.0) * 0.35;
    float3 sunColor = float3(1.20, 1.05, 0.80) * sunIntensity;
    color += sunDisk * sunColor * 4.0;
    color += sunCorona * sunColor;

    // Gamma correction (linear -> sRGB)
    color = pow(max(color, 0.001), 1.0 / 2.2);

    return float4(color, 1.0);
}
