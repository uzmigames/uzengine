/// Editor Outline Pass — Stencil-based selection outline for editor mode.
///
/// Renders a colored outline around the selected entity using a two-pass
/// technique:
/// 1. **Mask pass**: Re-renders the selected mesh to an R8Unorm mask RT,
///    writing 1.0 where the mesh is visible (depth tested against GBuffer depth).
/// 2. **Outline detect pass**: Fullscreen pass reads the mask, applies a 3x3
///    edge detection kernel, and outputs colored outline pixels with alpha blend.
///
/// # Usage
/// ```cpp
/// OutlinePass outline;
/// outline.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float);
///
/// // In render loop (after GBuffer, before present):
/// outline.Draw(cmd, cbRing, camera, drawList, meshes, selectedEntityId,
///              viewportWidth, viewportHeight, depthTexture, backbufferTexture);
///
/// outline.Shutdown();
/// ```

#pragma once

#include "cb_ring_allocator.h"
#include "mat4.h"
#include "log.h"
#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(OutlinePass);

class IRHIDevice;
class IRHICommandList;
class ShaderPipeline;
class CBRingAllocator;
class Camera;
struct DrawCommand;
struct GPUMesh;

/// Renders a colored outline around a selected entity using mask + edge detection.
///
/// The pass operates in two stages (mask + detect), both using the shared root
/// signature. The mask pass writes to an R8Unorm intermediate texture, and the
/// detect pass reads it via a bindless SRV index passed through root constants.
class OutlinePass {
public:
    /// Initializes GPU resources: mask texture, shaders, and pipelines.
    ///
    /// # Parameters
    /// - `device` — RHI device for resource creation.
    /// - `pipeline` — Shader pipeline for compiling outline shaders.
    /// - `rtvFormat` — Render target format of the backbuffer (e.g., RGBA8Unorm).
    /// - `dsvFormat` — Depth-stencil format (e.g., D32Float).
    /// - `width` — Viewport width in pixels.
    /// - `height` — Viewport height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` on failure (error logged).
    bool Init(IRHIDevice* device, ShaderPipeline& pipeline,
              RHIFormat rtvFormat, RHIFormat dsvFormat,
              uint32_t width, uint32_t height);

    /// Releases all GPU resources.
    void Shutdown();

    /// Recreates the mask texture and SRV for the new viewport size.
    void Resize(uint32_t width, uint32_t height);

    /// Draws the selection outline for the entity matching the given entity ID.
    ///
    /// Searches the draw list for commands matching `selectedEntityId`, renders
    /// them to the mask texture, then runs the fullscreen edge detection pass.
    ///
    /// # Parameters
    /// - `cmdList` — Active command list (between Begin/End).
    /// - `cbRing` — CB ring allocator for uploading per-draw constants.
    /// - `camera` — Camera for view-projection matrix.
    /// - `drawList` — Current frame's draw list (searched for matching entityId).
    /// - `meshes` — GPU mesh array indexed by DrawCommand::meshId.
    /// - `selectedEntityId` — ECS entity ID to highlight. UINT32_MAX = no selection.
    /// - `viewportWidth` — Display resolution width.
    /// - `viewportHeight` — Display resolution height.
    /// - `depthTexture` — Scene depth texture for depth-tested mask rendering.
    /// - `backbuffer` — Backbuffer texture to composite the outline onto.
    /// - `bindless` — True if SM6.6 bindless is active.
    void Draw(IRHICommandList* cmdList,
              CBRingAllocator& cbRing,
              const Camera& camera,
              const std::vector<DrawCommand>& drawList,
              const std::vector<GPUMesh>& meshes,
              uint32_t selectedEntityId,
              float viewportWidth, float viewportHeight,
              RHITextureHandle depthTexture,
              RHITextureHandle backbuffer,
              bool bindless,
              const CBAllocation& perFrameAlloc);

    /// Sets the outline color (linear RGB). Default: #FF8C00 (dark orange).
    void SetColor(float r, float g, float b) {
        m_colorR = r; m_colorG = g; m_colorB = b;
    }

    /// Sets the outline thickness in pixels. Default: 2.0.
    void SetThickness(float px) { m_thickness = px; }

private:
    IRHIDevice* m_device = nullptr;

    // ── Shaders ─────────────────────────────────────────────────────────
    RHIShaderHandle m_maskVs{};           ///< mesh_vs.hlsl
    RHIShaderHandle m_maskPs{};           ///< outline_mask_ps.hlsl
    RHIShaderHandle m_detectVs{};         ///< fullscreen_vs.hlsl
    RHIShaderHandle m_detectPs{};         ///< outline_detect_ps.hlsl

    // ── Pipelines ───────────────────────────────────────────────────────
    RHIPipelineHandle m_maskPipeline{};   ///< Mesh -> R8Unorm mask, depth test, no blend.
    RHIPipelineHandle m_detectPipeline{}; ///< Fullscreen -> backbuffer, alpha blend, no depth.

    // ── Mask texture ────────────────────────────────────────────────────
    RHITextureHandle m_maskTexture{};     ///< R8Unorm mask RT at viewport resolution.
    uint32_t m_maskSrvIndex = UINT32_MAX; ///< Bindless SRV index for the mask texture.
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // ── Outline parameters ──────────────────────────────────────────────
    float m_colorR = 1.0f;               ///< Default: #FF8C00 orange R.
    float m_colorG = 0.549f;             ///< Default: #FF8C00 orange G.
    float m_colorB = 0.0f;               ///< Default: #FF8C00 orange B.
    float m_thickness = 2.0f;            ///< Outline width in pixels.

    /// Helper: create or recreate the mask texture at the current m_width/m_height.
    void CreateMaskTexture();
};

} // namespace uz
