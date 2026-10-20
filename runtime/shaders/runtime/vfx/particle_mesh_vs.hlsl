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
/// Per-instance: float4x4 world transform from a StructuredBuffer<float4x4>
/// indexed by SV_InstanceID. SM6.6 / `UZ_BINDLESS` reads the buffer via
/// `ResourceDescriptorHeap[g_instanceTransformsBufferIdx]` (the engine's
/// global root signature has no SRV table at t0); SM5 fallback keeps the
/// `register(t0)` binding for platforms that lack DXR-class shader model.
///
/// # Output
/// Clip-space position, world-space position and normal for lighting, UV,
/// and the instance index for the PS to read per-instance color.
///
/// # Register Layout (SM5 fallback)
/// - t0: StructuredBuffer<float4x4>  instanceTransforms
/// - b0: cbuffer with viewProjection matrix + bindless descriptor indices
///
/// @ref instanced rendering with structured buffer instance data

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b0) — root constants (16 DWORDs / 64 bytes max)
// ─────────────────────────────────────────────────────────────────────────────
//
// The bindless root signature pins b0 to `RootConstants(num32BitConstants=16)`
// = 64 bytes total. We can ONLY put 16 DWORDs here. viewProjection (64 bytes
// alone) used to fit, but adding the bindless indices needed today would
// overflow the root signature. Instead, viewProjection is read from
// PerFrameCB at b1 (which the engine binds globally each frame), and b0
// carries only the two per-emitter bindless descriptor indices.
//
// Layout MUST stay byte-identical to `ParticleMeshPassConstants` in
// `particle_mesh_render_pass.h` (CPU mirror, 64 bytes).
cbuffer VFXMeshRenderConstants : register(b0) {
    uint     g_instanceTransformsBufferIdx; ///<  0  Bindless SRV — instance xforms.
    uint     g_instanceColorsBufferIdx;     ///<  4  Bindless SRV — instance colors.
    // Reserved DWORDs — keeps the layout 16-DWORDs to hit the root-constants
    // ceiling exactly. Future per-batch fields land here without touching the
    // root signature.
    uint2    _meshCBpad0;                   ///<  8..15 (finish the first row).
    uint4    _meshCBpad[3];                 ///< 16..63 (one vector per cbuffer row).
};

// ─────────────────────────────────────────────────────────────────────────────
// Constant Buffer (b1) — engine-global PerFrameCB (read viewProj only).
// ─────────────────────────────────────────────────────────────────────────────
//
// PerFrameCB layout is owned by `engine/renderer/core/params/per_frame_params.h`.
// We declare the FULL struct (matching `particle_billboard_vs.hlsl`) so DXC's
// reflection metadata mirrors the engine-global binding exactly — partial
// declarations have been observed to cause PSO E_INVALIDARG when the root
// signature validation runs.
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;        ///< View * Projection matrix.
    float4   cameraPos;                 ///< xyz = camera world position, w = unused.
    float4   timeParams;                ///< x = totalTime, y = deltaTime, z = frameCount.
    float4   lightDir;                  ///< xyz = direction toward light, w = unused.
    float4   lightColor;                ///< xyz = color * intensity, w = unused.
    float4   ambientColor;              ///< xyz = ambient radiance, w = unused.
    row_major float4x4 invViewProj;     ///< Inverse view-projection.
    float    nearPlane;                 ///< Camera near plane.
    float    farPlane;                  ///< Camera far plane.
    uint     _perFramePad0;
    uint     _perFramePad1;
    uint     _perFramePad2;
    uint     _perFramePad3;
    float    _perFramePad4;
    float    _perFramePad5;
    float    _perFramePad6;
    float    _perFramePad7;
    float    skyLightMinAmbient;         ///< Byte 248 in the CPU PerFrameCB.
    float    exposure;                  ///< Auto-exposure multiplier.
};

// ─────────────────────────────────────────────────────────────────────────────
// Instance Transform Buffer
// ─────────────────────────────────────────────────────────────────────────────

#ifndef UZ_BINDLESS
/// SM5 fallback: per-instance world transforms via `register(t0)`. Modern
/// builds (SM6.6+) take the bindless path — see `LoadInstanceTransform`.
StructuredBuffer<float4x4> g_instanceTransforms : register(t0);
#endif

/// Loads the per-instance world transform for `instanceID`.
///
/// # Bindless (SM6.6+)
/// Reads `ResourceDescriptorHeap[g_instanceTransformsBufferIdx]` cast to
/// `StructuredBuffer<float4x4>`, then indexes by `instanceID`. The CPU sets
/// `g_instanceTransformsBufferIdx` to the SRV index of the instance buffer
/// produced by `particle_mesh_instance_cs.hlsl` for this emitter.
///
/// # SM5 fallback
/// Uses the explicitly-bound `register(t0)` SRV.
float4x4 LoadInstanceTransform(uint instanceID)
{
#ifdef UZ_BINDLESS
    StructuredBuffer<float4x4> instanceBuf =
        ResourceDescriptorHeap[g_instanceTransformsBufferIdx];
    return instanceBuf[instanceID];
#else
    return g_instanceTransforms[instanceID];
#endif
}

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
    float4x4 world = LoadInstanceTransform(input.instanceID);

    // Transform position: local -> world -> clip.
    float4 worldPos = mul(float4(input.position, 1.0f), world);
    output.clipPos = mul(worldPos, viewProj);
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
