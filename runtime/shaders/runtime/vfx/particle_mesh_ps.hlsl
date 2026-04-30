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
// Constant Buffer (b0) — shared with VS, 16 DWORDs (root-constants ceiling).
// ─────────────────────────────────────────────────────────────────────────────
//
// Layout MUST mirror the VS declaration in particle_mesh_vs.hlsl so a single
// `SetRootConstants` push covers both stages. PS only consumes
// `g_instanceColorsBufferIdx`; the transforms index is unused here but kept
// so the offsets match.
cbuffer VFXMeshRenderConstants : register(b0) {
    uint     g_instanceTransformsBufferIdx;
    uint     g_instanceColorsBufferIdx;
    uint     _meshCBpad[14];
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b1) — engine-global PerFrameCB
// ─────────────────────────────────────────────────────────────────────────────
//
// MUST match `runtime/engine/renderer/core/params/per_frame_params.h` byte-for-byte
// (mirrors `particle_mesh_vs.hlsl:58`). The bindless root signature pins b1 as
// a CBV pointing at PerFrameCB; the previous local cbuffer `VFXMeshLightConstants`
// at the same register collided with that binding and caused the PS to read
// PerFrameCB fields as garbage light data — making lit mesh particles render
// pure black even when alive instances were dispatched.
//
// @ref phase0_vfx-mesh-particle-visibility-debug
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
    float4   lightDir;          ///< xyz = direction toward light (matches VS).
    float4   lightColor;        ///< xyz = color * intensity.
    float4   ambientColor;      ///< xyz = ambient radiance.
    row_major float4x4 invViewProj;
    float    nearPlane;
    float    farPlane;
    uint     _perFramePad0;
    uint     _perFramePad1;
    uint     _perFramePad2;
    uint     _perFramePad3;
    float    _perFramePad4;
    float    _perFramePad5;
    float    _perFramePad6;
    float    _perFramePad7;
    float    exposure;
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
    float4 instanceColor = LoadInstanceColor(input.instanceID);

    // Normalize the interpolated world normal.
    float3 N = normalize(input.worldNormal);

    // Simple half-Lambert N-dot-L for soft particle lighting.
    // Half-Lambert wraps the lighting to avoid completely dark backsides,
    // which looks better for small particle meshes viewed from all angles.
    // PerFrameCB.lightDir.xyz / lightColor.xyz / ambientColor.xyz come from
    // the engine-global PerFrameCB (matches particle_mesh_vs.hlsl b1 binding).
    float NdotL = dot(N, lightDir.xyz);
    float halfLambert = NdotL * 0.5f + 0.5f;

    // Combine directional lighting with ambient.
    float3 lighting = lightColor.xyz * halfLambert + ambientColor.xyz;

    // Final color: instance tint * lighting.
    float3 finalColor = instanceColor.rgb * lighting;

    return float4(finalColor, instanceColor.a);
}
