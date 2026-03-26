/// VFX Decal Render Pass -- Deferred decal projection into the G-Buffer.
///
/// Renders projected decals as oriented bounding boxes (OBBs) that modify the
/// G-Buffer's albedo, normal, and roughness channels. Each decal is drawn as a
/// unit cube transformed by the decal's world matrix; the pixel shader samples
/// the scene depth to reconstruct world position, tests whether it falls inside
/// the decal volume, and blends the decal material onto the G-Buffer.
///
/// # Rendering Strategy
/// The pass draws the **back faces** of each decal's bounding box (front-face
/// culling). This ensures the decal is visible even when the camera is inside
/// the projection volume. Depth is tested read-only against the scene depth
/// buffer -- no depth writes occur.
///
/// # Blend Modes
/// The PSO uses alpha blending (SrcAlpha / InvSrcAlpha) for the Opaque mode.
/// Additive, Multiply, and Screen modes are selected per-decal via a shader
/// constant (`blendMode` float in the CB), not via separate PSOs.
///
/// # Render Graph Integration
/// The pass is registered between the G-Buffer pass and the lighting pass.
/// It declares read access to the scene depth texture (ShaderResource) and
/// write access to three G-Buffer targets (RenderTarget). The render graph
/// compiler schedules barriers automatically.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#pragma once

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

struct DecalComponent;

} // namespace uz

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DecalConstants -- CPU-side mirror of the per-decal constant buffer
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mirror of the decal pixel shader's constant buffer (register b1).
///
/// Contains the matrices and parameters needed to project a decal from its
/// OBB onto the scene geometry via depth reconstruction. Must be byte-identical
/// to the HLSL cbuffer layout.
///
/// Total size: 192 bytes (256-byte aligned by CBRingAllocator).
struct DecalConstants {
    float worldToDecal[16]  = {}; ///< 0:   World-to-decal-local transform (maps OBB to [-1,1] cube).
    float invViewProj[16]   = {}; ///< 64:  Inverse view-projection matrix for depth reconstruction.
    float normalIntensity   = 0.0f; ///< 128: Strength of the decal's normal map [0, 1].
    float roughnessOverride = 0.0f; ///< 132: Surface roughness override (-1 = no override).
    float fadeAlpha         = 0.0f; ///< 136: Current fade alpha (1 = opaque, 0 = invisible).
    float blendMode         = 0.0f; ///< 140: Blend mode as float (0=Opaque, 1=Additive, 2=Multiply, 3=Screen).
    float screenWidth       = 0.0f; ///< 144: Viewport width in pixels.
    float screenHeight      = 0.0f; ///< 148: Viewport height in pixels.
    float _pad0             = 0.0f; ///< 152: Padding.
    float _pad1             = 0.0f; ///< 156: Padding.
    float viewProjection[16]= {}; ///< 160: View-projection matrix (for clip-space vertex transform).
};

// ─────────────────────────────────────────────────────────────────────────────
// DecalRenderData -- per-decal data for the render pass
// ─────────────────────────────────────────────────────────────────────────────

/// Per-decal rendering data for the decal render pass.
///
/// Contains the pre-computed constant buffer allocation and texture bindings
/// needed to draw one decal as a projected OBB into the G-Buffer.
///
/// # Ownership
/// Does NOT own any resources. The caller must ensure all handles remain
/// valid until `RgBuilder::Execute()` completes.
struct DecalRenderData {
    /// The source decal component (read-only access for skipping inactive decals).
    const DecalComponent* decal = nullptr;

    /// Pre-filled CB allocation for the per-decal shader constants.
    CBAllocation decalCB = {};

    /// Albedo texture for this decal's material.
    /// Bound as an SRV in the pixel shader.
    RHITextureHandle albedoTexture;

    /// Normal map texture for this decal's material.
    /// Bound as an SRV in the pixel shader. Invalid handle = no normal override.
    RHITextureHandle normalTexture;

    /// Roughness map texture for this decal's material (optional).
    /// Invalid handle = use roughnessOverride constant instead.
    RHITextureHandle roughnessTexture;

    // -- Descriptor heap indices for SRV binding (SM5 path) -------------------

    /// SRV index for the albedo texture.
    uint32_t albedoSrvIndex = UINT32_MAX;

    /// SRV index for the normal map texture.
    uint32_t normalSrvIndex = UINT32_MAX;

    /// SRV index for the roughness texture.
    uint32_t roughnessSrvIndex = UINT32_MAX;

    /// SRV index for the scene depth texture.
    uint32_t sceneDepthSrvIndex = UINT32_MAX;
};

// ─────────────────────────────────────────────────────────────────────────────
// DecalPassDesc -- full pass configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the deferred decal render graph pass.
///
/// All fields must be set before calling `AddDecalPass`. The pass does not
/// take ownership of any pointed-to objects -- the caller must ensure they
/// remain valid until `RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct DecalPassDesc {
    // -- Pipeline -------------------------------------------------------------

    /// Graphics PSO for the decal deferred shader (VS + PS).
    /// Must be configured with front-face culling, depth read-only, and alpha blending.
    RHIPipelineHandle decalPipeline;

    // -- Unit cube mesh -------------------------------------------------------

    /// Vertex buffer for the unit cube (8 vertices, float3 POSITION).
    RHIBufferHandle cubeVertexBuffer;

    /// Index buffer for the unit cube (36 indices, uint16).
    RHIBufferHandle cubeIndexBuffer;

    // -- Scene depth for world-space reconstruction ---------------------------

    /// Scene depth texture handle for world-space position reconstruction.
    /// Expected to be in `RHIResourceState::ShaderResource` before this pass
    /// executes (transitioned by the render graph).
    RHITextureHandle sceneDepthTexture;

    // -- G-Buffer targets (read-write) ----------------------------------------

    /// G-Buffer albedo target (render graph resource ID).
    rg::RgResourceId gbufferAlbedo;

    /// G-Buffer normal target (render graph resource ID).
    rg::RgResourceId gbufferNormal;

    /// G-Buffer roughness target (render graph resource ID).
    rg::RgResourceId gbufferRoughness;

    /// Scene depth target for depth testing (render graph resource ID).
    rg::RgResourceId depthTarget;

    // -- Per-decal render data ------------------------------------------------

    /// Array of active decals to render this frame, pre-sorted by the caller
    /// (by sortOrder, then by distance). The pass renders in the given order.
    std::vector<DecalRenderData> decals;
};

// ─────────────────────────────────────────────────────────────────────────────
// AddDecalPass
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the deferred decal rendering pass in the render graph.
///
/// For each decal in `desc.decals`:
/// 1. Skips if the decal component is inactive or fully faded.
/// 2. Binds the decal deferred pipeline.
/// 3. Binds the per-decal constant buffer (worldToDecal, invViewProj,
///    decal parameters, screen dimensions).
/// 4. Binds SRV descriptor table for decal textures and scene depth.
/// 5. Binds the unit cube vertex/index buffers.
/// 6. Issues `DrawIndexed(36)` to render the decal bounding box.
///
/// The pass renders into the G-Buffer after the geometry pass and before
/// the lighting pass. Depth testing is enabled (read-only, reversed-Z
/// GreaterEqual) to reject pixels behind the scene. Depth writes are
/// disabled. Front-face culling ensures the box interior is rasterized.
///
/// # Arguments
/// * `builder` -- Render graph builder.
/// * `desc` -- Pass configuration (pipeline, cube mesh, depth, G-Buffer targets,
///   per-decal data).
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
///
/// # Panics
/// Asserts if `desc.decalPipeline` is invalid or if `desc.cubeVertexBuffer`
/// or `desc.cubeIndexBuffer` are invalid.
void AddDecalPass(rg::RgBuilder& builder, const DecalPassDesc& desc);

// ─────────────────────────────────────────────────────────────────────────────
// Unit Cube Mesh Data -- vertex and index data for the decal bounding box
// ─────────────────────────────────────────────────────────────────────────────

/// Number of vertices in the decal unit cube (8 corners of a [-0.5, 0.5] cube).
static constexpr uint32_t kDecalCubeVertexCount = 8;

/// Number of indices in the decal unit cube (12 triangles * 3 = 36).
static constexpr uint32_t kDecalCubeIndexCount = 36;

/// Returns the unit cube vertex positions (8 vertices, 3 floats each).
///
/// The cube spans [-0.5, 0.5] in all axes. The decal's world matrix scales
/// this to match the decal's halfExtents.
///
/// # Returns
/// Pointer to a static array of 24 floats (8 vertices * 3 components).
[[nodiscard]] const float* GetDecalCubeVertices() noexcept;

/// Returns the unit cube index data (36 indices, uint16).
///
/// Defines 12 triangles (2 per face) with counter-clockwise winding for
/// the default front-face convention. Since the PSO uses front-face culling,
/// the visible triangles are the back faces (box interior).
///
/// # Returns
/// Pointer to a static array of 36 uint16 values.
[[nodiscard]] const uint16_t* GetDecalCubeIndices() noexcept;

} // namespace uz
