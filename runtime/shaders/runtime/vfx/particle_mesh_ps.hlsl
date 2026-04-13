/// VFX Mesh Particle Pixel Shader
///
/// Reads per-instance color from the instance color buffer and applies basic
/// N-dot-L directional lighting. Each particle instance has its own RGBA color
/// written by particle_mesh_instance_cs.hlsl.
///
/// # Input
/// Interpolated world position, world normal, UV, and instance ID from the VS.
///
/// # Output
/// Final RGBA color to the render target.
///
/// # Register Layout (SM5 fallback)
/// - t1: StructuredBuffer<float4>  instanceColors
/// - b0: cbuffer with light direction and ambient color
///
/// @ref standard per-instance tinting with instanced mesh rendering

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b1) — per-frame lighting data
// ─────────────────────────────────────────────────────────────────────────────

cbuffer VFXMeshLightConstants : register(b1) {
    float3 lightDirection;       ///< Normalized world-space light direction (towards light).
    float  _pad0;
    float3 lightColor;           ///< Light color (linear RGB, pre-multiplied by intensity).
    float  _pad1;
    float3 ambientColor;         ///< Ambient light color (linear RGB).
    float  _pad2;
};

// ─────────────────────────────────────────────────────────────────────────────
// Instance Color Buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Per-instance RGBA colors, one float4 per alive particle.
/// Written by particle_mesh_instance_cs.hlsl, indexed by SV_InstanceID.
StructuredBuffer<float4> g_instanceColors : register(t1);

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Input
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolated data from the vertex shader.
struct PSInput {
    float4 clipPos     : SV_Position;
    float3 worldPos    : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 uv          : TEXCOORD2;
    uint   instanceID  : TEXCOORD3;
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Computes final color from per-instance tint and simple directional lighting.
///
/// The per-instance color (from the particle color SoA buffer, written by the
/// instance CS) is multiplied with the lighting result. Alpha is preserved from
/// the instance color for translucent particle support.
float4 main(PSInput input) : SV_Target
{
    // Read per-instance color (RGBA).
    float4 instanceColor = g_instanceColors[input.instanceID];

    // Normalize the interpolated world normal.
    float3 N = normalize(input.worldNormal);

    // Simple half-Lambert N-dot-L for soft particle lighting.
    // Half-Lambert wraps the lighting to avoid completely dark backsides,
    // which looks better for small particle meshes viewed from all angles.
    float NdotL = dot(N, lightDirection);
    float halfLambert = NdotL * 0.5f + 0.5f;

    // Combine directional lighting with ambient.
    float3 lighting = lightColor * halfLambert + ambientColor;

    // Final color: instance tint * lighting.
    float3 finalColor = instanceColor.rgb * lighting;

    return float4(finalColor, instanceColor.a);
}
