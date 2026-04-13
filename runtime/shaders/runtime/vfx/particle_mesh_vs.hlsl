/// VFX Mesh Particle Vertex Shader
///
/// Transforms mesh vertices using per-instance float4x4 matrices from the
/// instance transform buffer produced by particle_mesh_instance_cs.hlsl.
/// Each instance renders one alive particle as a full 3D mesh.
///
/// # Input
/// Per-vertex: position (float3), normal (float3), uv (float2) from the
/// mesh vertex buffer (VfxMeshVertex layout, 32 bytes stride).
///
/// Per-instance: float4x4 world transform from StructuredBuffer<float4x4>
/// indexed by SV_InstanceID. The transform includes translation, rotation,
/// and scale baked in by the instance CS.
///
/// # Output
/// Clip-space position, world-space position and normal for lighting, UV,
/// and the instance index for the PS to read per-instance color.
///
/// # Register Layout (SM5 fallback)
/// - t0: StructuredBuffer<float4x4>  instanceTransforms
/// - b0: cbuffer with viewProjection matrix
///
/// @ref instanced rendering with structured buffer instance data

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) — per-frame camera data
// ─────────────────────────────────────────────────────────────────────────────

cbuffer VFXMeshRenderConstants : register(b0) {
    float4x4 viewProjection;     ///< Combined view * projection matrix.
};

// ─────────────────────────────────────────────────────────────────────────────
// Instance Transform Buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Per-instance world transforms, one float4x4 per alive particle.
/// Written by particle_mesh_instance_cs.hlsl, consumed here via SV_InstanceID.
StructuredBuffer<float4x4> g_instanceTransforms : register(t0);

// ─────────────────────────────────────────────────────────────────────────────
// Vertex Input / Output
// ─────────────────────────────────────────────────────────────────────────────

/// Per-vertex input from the mesh vertex buffer.
/// Must match VfxMeshVertex layout: float3 pos, float3 normal, float2 uv.
struct VSInput {
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
    uint   instanceID : SV_InstanceID;
};

/// Interpolated data passed to the pixel shader.
struct VSOutput {
    float4 clipPos     : SV_Position;   ///< Clip-space position for rasterization.
    float3 worldPos    : TEXCOORD0;     ///< World-space position for lighting.
    float3 worldNormal : TEXCOORD1;     ///< World-space normal (unnormalized; PS normalizes).
    float2 uv          : TEXCOORD2;     ///< Texture coordinates.
    uint   instanceID  : TEXCOORD3;     ///< Instance index for PS instance color lookup.
};

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Transforms mesh vertex by per-instance world matrix, then by viewProjection.
///
/// The instance transform already encodes translation, rotation, and scale.
/// Normal is transformed by the upper-left 3x3 of the world matrix (assumes
/// uniform or nearly-uniform scale; for non-uniform scale a proper
/// inverse-transpose would be needed, but VFX particles rarely need that).
VSOutput main(VSInput input)
{
    VSOutput output;

    // Read the per-instance world transform (row-major float4x4).
    float4x4 world = g_instanceTransforms[input.instanceID];

    // Transform position: local -> world -> clip.
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    output.clipPos = mul(worldPos, viewProjection);
    output.worldPos = worldPos.xyz;

    // Transform normal by upper 3x3 (no translation).
    // For uniform scale this is correct; non-uniform scale would require
    // inverse-transpose, but VFX mesh particles use uniform scale per axis.
    float3 worldNormal = mul(input.normal, (float3x3)world);
    output.worldNormal = worldNormal;

    output.uv = input.uv;
    output.instanceID = input.instanceID;

    return output;
}
