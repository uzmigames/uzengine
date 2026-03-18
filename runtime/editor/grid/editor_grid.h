/// Editor Grid Renderer
///
/// Renders an infinite ground grid on the XZ plane (Y=0) using a fullscreen
/// triangle with a procedural shader. The grid fades with distance and supports
/// major/minor line spacing. Used in editor/dev mode for spatial reference.
///
/// # Usage
/// ```cpp
/// EditorGrid grid;
/// grid.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float);
/// // In render loop (after opaque geometry, before post-process):
/// grid.Draw(cmdList, cbRing, camera);
/// // At shutdown:
/// grid.Shutdown();
/// ```

#pragma once

#include "mat4.h"
#include "log.h"
#include "rhi_types.h"

namespace uz {

UZ_DECLARE_LOG_CATEGORY(EditorGrid);

class IRHIDevice;
class IRHICommandList;
class ShaderPipeline;
class CBRingAllocator;
class Camera;

/// Renders an infinite XZ ground plane grid with fading.
///
/// Uses a fullscreen triangle technique — no vertex buffer needed.
/// The vertex shader generates 3 vertices that cover the screen,
/// and the pixel shader intersects the camera ray with the Y=0 plane
/// to compute anti-aliased grid lines procedurally.
class EditorGrid {
public:
    /// Initializes GPU resources (pipeline state, shaders).
    ///
    /// Compiles grid shaders via the provided ShaderPipeline and creates the
    /// graphics pipeline with alpha blending and read-only depth test.
    ///
    /// # Parameters
    /// - `device` — RHI device for resource creation.
    /// - `pipeline` — Shader pipeline for compiling grid shaders (must have `/editor` mounted).
    /// - `rtvFormat` — Render target format (must match swapchain).
    /// - `dsvFormat` — Depth-stencil format (must match depth buffer).
    ///
    /// # Returns
    /// `true` on success, `false` if shader compilation or PSO creation fails.
    bool Init(IRHIDevice* device, ShaderPipeline& pipeline,
              RHIFormat rtvFormat, RHIFormat dsvFormat);

    /// Draws the grid into the current render target.
    ///
    /// Allocates a GridCB from the provided ring buffer, uploads the inverse
    /// ViewProjection matrix and camera position, then draws a fullscreen
    /// triangle with the grid shader.
    ///
    /// Must be called between cmdList->Begin() and cmdList->End(),
    /// with a render target and depth buffer already bound.
    ///
    /// # Parameters
    /// - `cmdList` — Active command list with render target bound.
    /// - `cbRing` — CB ring allocator for uploading the grid constant buffer.
    /// - `camera` — Camera to extract inverse VP and position from.
    /// - `sceneDepthSrv` — Bindless SRV index for scene depth texture.
    ///   Pass UINT32_MAX when hardware depth testing is active (depth buffer attached).
    ///   When != UINT32_MAX, the shader performs manual depth occlusion via
    ///   bindless sampling (used when screen percentage != 100%).
    /// - `displayWidth` / `displayHeight` — Display resolution for pixel-to-UV
    ///   mapping (only used when sceneDepthSrv != UINT32_MAX).
    void Draw(IRHICommandList* cmdList, CBRingAllocator& cbRing, const Camera& camera,
              uint32_t sceneDepthSrv = UINT32_MAX,
              float displayWidth = 0.f, float displayHeight = 0.f);

    /// Releases GPU resources.
    void Shutdown();

    /// Grid line spacing in world units / cm (default: 100 = 1m).
    void SetMinorSpacing(float spacing) { m_minorSpacing = spacing; }

    /// Major grid line interval (every N minor lines, default: 10).
    void SetMajorInterval(uint32_t interval) { m_majorInterval = interval; }

    /// Maximum visible distance for grid fade in cm (default: 10000 = 100m).
    void SetFadeDistance(float dist) { m_fadeDistance = dist; }

private:
    IRHIDevice* m_device = nullptr;
    RHIPipelineHandle m_pipeline{};           ///< With hardware depth test (screen percentage == 100%).
    RHIPipelineHandle m_pipelineNoDepth{};    ///< Without depth attachment (screen percentage != 100%).
    RHIShaderHandle m_vs{};
    RHIShaderHandle m_ps{};

    float m_minorSpacing = 100.0f;    ///< 100 cm = 1 m per minor line
    uint32_t m_majorInterval = 10;    ///< Major line every 10 m
    float m_fadeDistance = 10000.0f;   ///< Fade at 100 m
};

} // namespace uz
