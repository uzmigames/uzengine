/// Pixel shader for instanced mesh particle rendering.
///
/// Receives per-instance color and world position from the mesh particle vertex
/// shader and outputs an unlit HDR color to SV_Target0. This shader is the
/// opaque/unlit path for mesh particles; GBuffer integration (deferred PBR
/// shading, normal mapping, roughness) can be added as a separate variant.
///
/// # Shading Model
/// Unlit: the particle color is emitted directly without any lighting
/// calculation. This is correct for emissive VFX (sparks, fire chunks, magic
/// projectiles) where the artist controls brightness via the color/emissive
/// curve in the particle system. For lit mesh particles, a future
/// PARTICLE_MESH_LIT variant will write to the GBuffer instead.
///
/// # Alpha Handling
/// For opaque mesh particles, alpha is expected to be 1.0 (the PSO has no
/// blend state). When alpha-tested rendering is needed (e.g., foliage debris),
/// an ALPHA_TEST variant can add a clip() call against a threshold.
///
/// # Root Signature
/// - b0: ParticleMeshPassCB (same as VS -- not read by this PS)
/// - No textures bound in the base unlit path
/// - No samplers required
///
/// @ref "GPU-Based Particle Simulation and Rendering" -- instanced mesh
///      particles with pre-computed transforms
/// @constants NONE
/// @deviations NONE

// ─────────────────────────────────────────────────────────────────────────────
// Input Structure
// ─────────────────────────────────────────────────────────────────────────────

/// Interpolants from the mesh particle vertex shader.
/// Semantics must match the VSOutput struct in particle_mesh_vs.hlsl exactly.
struct PSInput {
    float4 position   : SV_Position; ///< Screen-space position (not read in PS).
    float3 worldPos   : WORLD_POS;   ///< World-space position (available for future fog/lighting).
    float4 color      : COLOR;       ///< Per-instance particle color (RGBA), vertex-interpolated.
    uint   instanceId : INSTANCE_ID; ///< Instance index (available for debug coloring).
};

// ─────────────────────────────────────────────────────────────────────────────
// Pixel Shader Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Outputs the per-instance particle color as an unlit HDR value.
///
/// The color was fetched per-instance in the vertex shader from the particle
/// color StructuredBuffer and arrives here interpolated across the triangle
/// (constant per-instance since all vertices of the same instance share the
/// same color value).
///
/// # Parameters
/// - input: interpolated vertex data from particle_mesh_vs.
///
/// # Returns
/// float4 RGBA written to SV_Target0 (HDR render target).
float4 main(PSInput input) : SV_Target0 {
    // ── Unlit output ────────────────────────────────────────────────────────
    // Direct emission of the particle color. No lighting, no tone mapping --
    // the post-process pipeline handles HDR resolve and exposure downstream.
    //
    // For opaque mesh particles the alpha channel should be 1.0 (set by the
    // spawn/update shader). The PSO blend state is opaque (no SrcAlpha blend).
    return input.color;
}
