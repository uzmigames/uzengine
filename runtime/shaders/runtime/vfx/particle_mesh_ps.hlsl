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
/// - b0: cbuffer mirroring `VFXMeshRenderConstants` (viewProjection +
///       bindless descriptor indices). The PS only consumes
///       `g_instanceColorsBufferIdx`; the matrix and the second index are
///       still read by the VS bound to the same PSO.
/// - b1: cbuffer with light direction and ambient color
///
/// @ref standard per-instance tinting with instanced mesh rendering

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) — shared with VS, same layout as
// `ParticleMeshPassConstants` on the CPU.
// ─────────────────────────────────────────────────────────────────────────────
//
// Mirrors the VS declaration so the PS can read the bindless SRV index for
// the instance color buffer without needing a separate root parameter.
cbuffer VFXMeshRenderConstants : register(b0) {
    float4x4 viewProjection;
    uint     g_instanceTransformsBufferIdx;
    uint     g_instanceColorsBufferIdx;
    uint     _meshCBpad0;
    uint     _meshCBpad1;
};

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

#ifndef UZ_BINDLESS
/// SM5 fallback — per-instance RGBA colors via `register(t1)`.
StructuredBuffer<float4> g_instanceColors : register(t1);
#endif

/// Loads the per-instance RGBA color for `instanceID`.
///
/// # Bindless (SM6.6+)
/// Reads `ResourceDescriptorHeap[g_instanceColorsBufferIdx]` cast to
/// `StructuredBuffer<float4>`. The CPU sets the index after the instance
/// CS dispatch.
///
/// # SM5 fallback
/// Uses the explicitly-bound `register(t1)` SRV.
float4 LoadInstanceColor(uint instanceID)
{
#ifdef UZ_BINDLESS
    StructuredBuffer<float4> instanceColors =
        ResourceDescriptorHeap[g_instanceColorsBufferIdx];
    return instanceColors[instanceID];
#else
    return g_instanceColors[instanceID];
#endif
}

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
    float4 instanceColor = LoadInstanceColor(input.instanceID);

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
