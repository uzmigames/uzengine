#pragma once

/// VFX Particle Mesh Render Pass -- Render graph graphics pass for instanced 3D mesh particles.
///
/// Registers a graphics pass that draws alive particle instances as 3D meshes
/// using DrawIndexedInstancedIndirect. This replaces the billboard sprite path
/// for emitters that use MeshRenderModule: each particle is a full mesh instance
/// rather than a camera-facing quad.
///
/// # Rendering Strategy
/// A preceding compute dispatch (particle_mesh_instance_cs.hlsl) fills a
/// RWStructuredBuffer<float4x4> with one instance transform per alive particle
/// and writes a DrawIndexedInstancedIndirect argument buffer. This pass binds
/// those buffers as SRVs/indirect args and issues a single indirect draw per
/// emitter batch, keeping draw-call count O(1) regardless of particle count.
///
/// # Transparency vs. Opaque
/// `ParticleMeshPassDesc::isTransparent` controls blend state selection:
/// - false (default): rendered in the opaque pass with depth writes enabled.
///   Suitable for solid debris chunks that should write depth correctly.
/// - true: rendered in the transparent phase with alpha blend and depth writes
///   disabled. Suitable for ghostly shards or translucent crystal fragments.
///
/// # Render Graph Resource Declarations
/// - `depthTarget`:  `DepthStencilRead`  when transparent, `DepthStencilWrite` when opaque.
/// - `colorTarget`:  `RenderTarget`       (write, always).
/// Instance and indirect args buffers are external persistent resources not
/// tracked by the render graph -- the caller is responsible for issuing the
/// UAV barrier after the instance CS and before this pass.
///
/// # Root Signature Slot Layout
/// Matches the engine's global root signature:
/// - Slot 0 (b0): Per-pass root constants (`ParticleMeshPassConstants`,
///   16 DWORDs) — carries the two bindless SRV indices for the instance
///   transforms + instance colors buffers. View-projection rides at b1
///   in `PerFrameCB`, inherited from the engine's global per-frame state.
///
/// SM6.6 bindless: SRV/UAV resources are accessed via
/// `ResourceDescriptorHeap[idx]` using indices from the b0 root constants.
/// No SRV descriptor table is bound at slot 5 — that slot was retired in
/// the RC2 bindless migration. The instance transforms SRV is reached as
/// `ResourceDescriptorHeap[instanceTransformsBufferIdx]` inside the VS.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the render thread only.

#include "cb_ring_allocator.h"
#include "render_graph_types.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz::rg {
class RgBuilder;
} // namespace uz::rg

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ParticleMeshPassConstants -- CPU-side mirror of the mesh instance shader CB
// ─────────────────────────────────────────────────────────────────────────────

/// CPU-side mirror of the mesh instance vertex/pixel shaders' per-pass
/// constant buffer (b0). Both shaders read from the same b0 binding so a
/// single root-constants push covers both stages.
///
/// The bindless root signature pins b0 to
/// `RootConstants(num32BitConstants=16)` = 64 bytes max. View-projection
/// no longer lives here — it is read from PerFrameCB at b1 (engine-global)
/// instead. Only the two per-emitter bindless descriptor indices ride this
/// slot; the rest is reserved padding so future per-batch fields land
/// without touching the root signature.
///
/// Must be byte-identical to the HLSL `cbuffer VFXMeshRenderConstants`
/// declarations in `particle_mesh_vs.hlsl` AND `particle_mesh_ps.hlsl`.
///
/// Total size: 64 bytes (16 DWORDs, exact root-constants ceiling).
struct ParticleMeshPassConstants {
    uint32_t instanceTransformsBufferIdx = 0xFFFFFFFFu; ///<  0  bindless SRV — float4x4 buffer.
    uint32_t instanceColorsBufferIdx     = 0xFFFFFFFFu; ///<  4  bindless SRV — float4  buffer.
    uint32_t _pad[14] = {};                              ///<  8..63 reserved.
};

static_assert(sizeof(ParticleMeshPassConstants) == 64,
              "ParticleMeshPassConstants must be exactly 64 bytes (16 DWORDs) "
              "to match the bindless root-constants slot at b0");

// ─────────────────────────────────────────────────────────────────────────────
// ParticleMeshBatchItem -- one instanced mesh draw batch
// ─────────────────────────────────────────────────────────────────────────────

/// Per-emitter batch data for the particle mesh render pass.
///
/// Each item corresponds to one active emitter using MeshRenderModule. All
/// instances for the emitter are drawn with a single DrawIndexedInstancedIndirect
/// call, consuming the indirect args buffer produced by the instance CS.
///
/// # Ownership
/// Does NOT own any resources. The caller must ensure all handles remain valid
/// until `rg::RgBuilder::Execute()` completes.
struct ParticleMeshBatchItem {
    // ── Pipeline ────────────────────────────────────────────────────────────

    /// Graphics PSO for the instanced mesh particle shader (VS + PS).
    ///
    /// For opaque items: depth writes enabled, no blending.
    /// For transparent items: depth writes disabled, alpha blending.
    /// Typically one shared PSO per transparency category across all emitters.
    RHIPipelineHandle pipeline;

    // ── Mesh geometry ────────────────────────────────────────────────────────

    /// Vertex buffer handle for the mesh geometry.
    ///
    /// Bound via `SetVertexBuffer(0, ...)`. Must contain POSITION (+ normals,
    /// UVs depending on the material).
    RHIBufferHandle meshVertexBuffer;

    /// Index buffer handle for the mesh geometry.
    ///
    /// Bound via `SetIndexBuffer(...)`. Index format is uint16 for meshes with
    /// <= 65 535 vertices, uint32 otherwise.
    RHIBufferHandle meshIndexBuffer;

    /// Format of the mesh index buffer.
    RHIIndexFormat meshIndexFormat = RHIIndexFormat::Uint16;

    // ── Bindless SRV descriptor indices ─────────────────────────────────────

    /// SRV descriptor heap index for the RWStructuredBuffer<float4x4> instance
    /// transform buffer written by particle_mesh_instance_cs.hlsl.
    ///
    /// Bound at the start of the SRV descriptor table (t0 in the shader).
    uint32_t instanceBufferSrvIndex = UINT32_MAX;

    /// SRV descriptor heap index for any additional material textures
    /// (albedo, normal map, roughness, etc.) bound starting at t1.
    ///
    /// `UINT32_MAX` means the material has no bindless textures (uses
    /// push-constants or hard-coded samplers only).
    uint32_t materialTexturesSrvIndex = UINT32_MAX;

    // ── Indirect draw arguments ──────────────────────────────────────────────

    /// Buffer holding `DrawIndexedInstancedIndirectArgs` (5 x uint32 = 20 bytes)
    /// written by the preceding instance CS dispatch.
    ///
    /// Must be in `RHIResourceState::IndirectArgument` state when this pass
    /// executes. The render graph emits the UAV→IndirectArgument barrier
    /// automatically when `indirectArgsBufferRg` below is declared as a
    /// `pb.Read(..., IndirectArgument)` dependency in the pass setup.
    RHIBufferHandle indirectArgsBuffer;

    /// Render graph resource id for the same `indirectArgsBuffer` above —
    /// used by the pass to declare a `Read(..., IndirectArgument)`
    /// dependency so the auto-barrier scheduler emits the
    /// `UnorderedAccess → IndirectArgument` transition between the
    /// preceding mesh-instance CS write and this pass's indirect draw.
    /// MUST be set when `indirectArgsBuffer` is set; otherwise the draw
    /// executes against a buffer still in `UnorderedAccess` state and
    /// triggers a debug-layer error.
    rg::RgResourceId indirectArgsBufferRg;

    /// Render graph resource id for the per-instance transform buffer
    /// (`gpuBuf.meshInstanceTransforms`) written by the preceding mesh
    /// instance CS as `UnorderedAccess`. The vertex shader reads it via
    /// `ResourceDescriptorHeap[g_instanceTransformsBufferIdx]` (bindless
    /// SRV path) so it MUST be in `ShaderResource` state when this pass
    /// executes. Without a `pb.Read(..., ShaderResource)` declaration the
    /// render graph leaves the buffer in `UnorderedAccess`, the bindless
    /// SRV read returns garbage / NaN values, and the resulting clip-space
    /// triangles span the entire viewport — visually blanking the HDR
    /// composite to black with normal lighting (or to the PS constant when
    /// the PS is hard-coded for diagnostics).
    /// @ref phase0_vfx-mesh-particle-visibility-debug
    rg::RgResourceId instanceTransformsBufferRg;

    /// Render graph resource id for the per-instance color buffer
    /// (`gpuBuf.meshInstanceColors`) written by the same mesh instance
    /// CS. The pixel shader reads per-instance RGBA via
    /// `ResourceDescriptorHeap[g_instanceColorsBufferIdx]` so it ALSO
    /// needs an explicit `Read(..., ShaderResource)` for the same reason
    /// the transforms buffer does.
    /// @ref phase0_vfx-mesh-particle-visibility-debug
    rg::RgResourceId instanceColorsBufferRg;

    // ── Per-pass constant buffer ─────────────────────────────────────────────

    /// Pre-filled CB allocation for the per-pass shader constants (b0).
    ///
    /// Contains the view-projection matrix. Filled by the caller before
    /// passing the item to `AddParticleMeshRenderPass`.
    CBAllocation passCB = {};
};

// ─────────────────────────────────────────────────────────────────────────────
// ParticleMeshPassDesc -- full pass configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the particle mesh render graph pass.
///
/// Populated by the frame setup code before calling `AddParticleMeshRenderPass`.
/// The pass does NOT take ownership of any pointed-to objects -- the caller must
/// ensure all handles and allocations remain valid until
/// `rg::RgBuilder::Execute()` completes.
///
/// # Thread Safety
/// NOT thread-safe. Must be populated from the render thread only.
struct ParticleMeshPassDesc {
    // ── Render Targets ────────────────────────────────────────────────────────

    /// The HDR color target to draw mesh particles into (render graph resource ID).
    ///
    /// Declared as `RHIResourceState::RenderTarget` by the pass — both as a
    /// `Read` (so the DAG schedules this pass after lighting/sky writes the
    /// HDR buffer) and a `Write` (so subsequent post-process passes see the
    /// updated state).
    rg::RgResourceId colorTarget;

    /// The depth buffer (render graph resource ID).
    ///
    /// Declared as `RHIResourceState::DepthStencilWrite` for opaque batches
    /// or `RHIResourceState::DepthStencilRead` for transparent batches based
    /// on `isTransparent`.
    rg::RgResourceId depthTarget;

    // ── Physical RHI handles for `BeginRenderPass` ────────────────────────────
    //
    // The execute callback must call `cmd->BeginRenderPass(...)` so the DX12
    // backend issues `OMSetRenderTargets` against the HDR composite slot.
    // Without these handles the indirect draw would land on whatever RTV/DSV
    // were bound by the previous pass (typically GBuffer slots — wrong format
    // for HDR composite). This was the root cause of the RC3 disable.

    /// Physical texture handle for the HDR color target (for `BeginRenderPass`).
    /// MUST refer to the same physical resource backing `colorTarget` above.
    RHITextureHandle colorTexture;

    /// Physical texture handle for the depth-stencil target (for `BeginRenderPass`).
    /// MUST refer to the same physical resource backing `depthTarget` above.
    RHITextureHandle depthTexture;

    /// Render-resolution viewport width for `SetViewport` / `SetScissor`.
    /// Pass the renderer's internal render resolution (post-DLSS upsample
    /// input), NOT the swapchain backbuffer size — the HDR composite is
    /// authored at render resolution.
    uint32_t renderWidth = 0;

    /// Render-resolution viewport height for `SetViewport` / `SetScissor`.
    uint32_t renderHeight = 0;

    // ── Rendering mode ────────────────────────────────────────────────────────

    /// When true, the pass blends particles with alpha and disables depth writes.
    ///
    /// Set this to false (default) for solid mesh debris. Set to true for
    /// translucent crystals, ice shards, or any partially transparent mesh.
    /// Both categories use the same pass function; the caller chooses the PSO
    /// that matches this flag.
    bool isTransparent = false;

    // ── Per-emitter batch items ───────────────────────────────────────────────

    /// Array of active emitter batches to render this frame.
    ///
    /// Each item carries its own PSO, mesh, instance buffer, and indirect args.
    /// Items with an invalid `pipeline` are skipped silently.
    std::vector<ParticleMeshBatchItem> batches;
};

// ─────────────────────────────────────────────────────────────────────────────
// AddParticleMeshRenderPass
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the instanced particle mesh rendering pass in the render graph.
///
/// For each batch in `desc.batches` with a valid pipeline:
/// 1. Binds the instanced mesh graphics PSO.
/// 2. Sets the per-pass constant buffer (view-projection matrix, b0).
/// 3. Binds the SRV descriptor table for the instance transform buffer (t0)
///    and optional material textures (t1+).
/// 4. Binds the mesh vertex and index buffers.
/// 5. Issues one `DrawIndexedIndirect` call using the pre-filled indirect
///    args buffer (written by the preceding instance CS dispatch).
///
/// Opaque batches (`isTransparent == false`) run before the transparent
/// phase and declare the depth target as `DepthStencilWrite`. Transparent
/// batches declare it as `DepthStencilRead` (no depth writes).
///
/// # Arguments
/// * `builder` -- Render graph builder used to declare the pass.
/// * `desc`    -- Pass configuration (render targets, transparency flag,
///                per-batch draw data).
///
/// # Side Effects
/// - Logs `VFX/Info` on first execution with the submitted batch count.
///
/// # Preconditions
/// - All `indirectArgsBuffer` handles in `desc.batches` must be in
///   `RHIResourceState::IndirectArgument` state when this pass executes.
///   The caller inserts the UAV barrier after the instance CS dispatch.
/// - At least one batch must have a valid `pipeline` handle for any work
///   to be submitted. An empty `batches` array is silently a no-op.
///
/// # Thread Safety
/// Must be called from the render thread during frame preparation.
void AddParticleMeshRenderPass(rg::RgBuilder& builder,
                               const ParticleMeshPassDesc& desc);

} // namespace uz
