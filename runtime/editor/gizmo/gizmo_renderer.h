/// Editor Gizmo Renderer — Transform gizmo visualization for editor mode.
///
/// Renders translate, rotate, and scale gizmo handles at the selected entity's
/// world position. The gizmo maintains a constant screen-space size (~120px)
/// regardless of camera distance. Supports axis highlighting for hover/drag states.
///
/// # Geometry
/// - **Translate**: 3 axis arrows (cones + lines) + 3 plane handles + center cube.
/// - **Rotate**: 3 axis circles (64-segment line rings in XY/XZ/YZ planes).
/// - **Scale**: 3 axis lines + 3 cubes at tips + center cube for uniform scale.
///
/// All geometry is pre-baked at Init time with per-vertex axis colors:
/// X = Red (1,0,0), Y = Green (0,1,0), Z = Blue (0,0,1).
///
/// # Rendering
/// Uses two pipelines: LineList (circles, axis lines) and TriangleList (cones, cubes).
/// Both use depth-read/write-disabled + alpha blending to render on top of scene geometry.
/// Reversed-Z depth test (GreaterEqual) ensures correct depth ordering.
///
/// # Usage
/// ```cpp
/// GizmoRenderer gizmo;
/// gizmo.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float);
///
/// // In render loop (editor mode, entity selected):
/// gizmo.Draw(cmd, cbRing, camera, entityPos, entityRot,
///            GizmoMode::Translate, GizmoSpace::World);
///
/// gizmo.Shutdown();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. All calls must be from the render thread.

#pragma once

#include "mat4.h"
#include "log.h"
#include "rhi_types.h"

#include <cstdint>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(Gizmo);

class IRHIDevice;
class IRHICommandList;
class ShaderPipeline;
class CBRingAllocator;
class Camera;

/// Gizmo manipulation mode.
enum class GizmoMode : uint8_t;

/// Gizmo coordinate space.
enum class GizmoSpace : uint8_t;

/// Renders transform gizmo handles at a world position.
///
/// Pre-bakes all gizmo geometry (arrows, circles, cubes) into GPU vertex/index
/// buffers at Init time. Draw() computes a constant screen-space scale factor
/// and renders the appropriate geometry set for the current mode.
///
/// # Ownership
/// Owns all GPU resources (vertex buffers, index buffers, pipeline states).
/// Does NOT own the `IRHIDevice` — it must outlive this renderer.
class GizmoRenderer {
public:
    /// Vertex format for gizmo geometry: position + per-vertex color.
    /// Public so that the static geometry builder helpers can use it.
    struct GizmoVertex {
        float pos[3];
        float color[3];
    };

    /// Initializes GPU resources: geometry buffers, shaders, and pipelines.
    ///
    /// Compiles gizmo shaders via the provided ShaderPipeline and creates both
    /// line and triangle pipeline states. Pre-bakes all geometry into GPU buffers.
    ///
    /// # Parameters
    /// - `device` — RHI device for resource creation.
    /// - `pipeline` — Shader pipeline for compiling gizmo shaders.
    /// - `rtvFormat` — Render target format (must match swapchain/backbuffer).
    /// - `dsvFormat` — Depth-stencil format (must match depth buffer).
    ///
    /// # Returns
    /// `true` on success, `false` if shader compilation or resource creation fails.
    [[nodiscard]] bool Init(IRHIDevice* device, ShaderPipeline& pipeline,
                            RHIFormat rtvFormat, RHIFormat dsvFormat);

    /// Releases all GPU resources.
    void Shutdown();

    /// Returns true when Init() has completed successfully.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    /// Draws the gizmo at worldPos with the current mode.
    ///
    /// Computes constant screen-space scale from camera distance, uploads
    /// a GizmoCB per axis draw, and renders the geometry for the active mode.
    ///
    /// Must be called between BeginRenderPass/EndRenderPass with a render
    /// target and depth buffer already bound.
    ///
    /// # Parameters
    /// - `cmdList` — Active command list with render target bound.
    /// - `cbRing` — CB ring allocator for uploading gizmo constant buffers.
    /// - `camera` — Camera for view-projection and distance calculation.
    /// - `worldPos` — World-space position of the gizmo center.
    /// - `entityRotation` — Entity rotation (used in Local space mode).
    /// - `mode` — Which gizmo to draw (Translate/Rotate/Scale).
    /// - `space` — Coordinate space for axis orientation (World/Local).
    /// - `hoveredAxis` — Axis index highlighted on hover (-1=none, 0=X, 1=Y,
    ///   2=Z, 3=XY, 4=XZ, 5=YZ, 6=all).
    /// - `activeAxis` — Axis index being dragged (-1=none, same encoding).
    void Draw(IRHICommandList* cmdList, CBRingAllocator& cbRing,
              const Camera& camera,
              const Vec3& worldPos, const Quat& entityRotation,
              GizmoMode mode, GizmoSpace space,
              int hoveredAxis = -1,
              int activeAxis = -1);

private:
    /// Constant buffer layout matching gizmo_vs.hlsl cbuffer GizmoCB : register(b1).
    /// Bound at root slot 1 (kSlotPerFrame) since gizmo draws don't need PerFrameCB.
    struct GizmoCB {
        Mat4     viewProj;          ///< Combined view-projection matrix.
        float    gizmoWorldPos[3];  ///< Gizmo center in world space.
        float    screenScale;       ///< Scale factor for constant screen-space size.
        float    axisColor[3];      ///< Current axis color (RGB).
        float    alpha;             ///< Opacity (1.0 normal, 0.5 for inactive axes during drag).
        uint32_t vertexBufferSrv;   ///< SRV index for structured vertex buffer.
        uint32_t _pad0, _pad1, _pad2; ///< Padding to 16-byte alignment.
    };

    // ── Geometry build helpers ───────────────────────────────────────────
    void BuildTranslateGeometry();
    void BuildRotateGeometry();
    void BuildScaleGeometry();

    // ── Per-axis draw helpers ────────────────────────────────────────────
    void DrawAxisSubset(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                        const GizmoCB& baseCB,
                        int axisIndex, int hoveredAxis, int activeAxis,
                        const float axisColor[3],
                        uint32_t srvIndex, uint32_t vertexOffset, uint32_t vertexCount,
                        bool isTriangles);

    void DrawAxisSubsetIndexed(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                               const GizmoCB& baseCB,
                               int axisIndex, int hoveredAxis, int activeAxis,
                               const float axisColor[3],
                               uint32_t srvIndex, RHIBufferHandle ib,
                               uint32_t indexOffset, uint32_t indexCount,
                               uint32_t vertexOffset);

    // ── GPU Resources ────────────────────────────────────────────────────

    /// Translate geometry: lines (axis stems) + triangles (cones, plane handles, center).
    RHIBufferHandle m_translateLineVB{};       ///< Structured buffer for line vertices.
    uint32_t m_translateLineSrv = UINT32_MAX;  ///< SRV index for line vertex structured buffer.
    uint32_t m_translateLineVertCount = 0;

    RHIBufferHandle m_translateTriVB{};        ///< Structured buffer for triangle vertices.
    uint32_t m_translateTriSrv = UINT32_MAX;   ///< SRV index for triangle vertex structured buffer.
    RHIBufferHandle m_translateTriIB{};        ///< Index buffer for triangle geometry.
    uint32_t m_translateTriIndexCount = 0;

    /// Per-axis triangle ranges (indexOffset, indexCount, vertexOffset) for selective drawing.
    struct AxisRange {
        uint32_t indexOffset = 0;
        uint32_t indexCount = 0;
        uint32_t vertexOffset = 0;
    };
    AxisRange m_translateConeRange[3];         ///< X, Y, Z cone ranges.
    AxisRange m_translatePlaneRange[3];        ///< XY, XZ, YZ plane handle ranges.
    AxisRange m_translateCenterRange;          ///< Center cube range.

    /// Rotate geometry: line vertices for 3 axis circles.
    RHIBufferHandle m_rotateLineVB{};
    uint32_t m_rotateLineSrv = UINT32_MAX;     ///< SRV index for rotate vertex structured buffer.
    uint32_t m_rotateLineVertCount = 0;
    uint32_t m_rotateCircleVertOffset[3];     ///< Per-axis start offset in vertex buffer.
    uint32_t m_rotateCircleVertCount[3];      ///< Per-axis vertex count.

    /// Scale geometry: same layout as translate but cubes instead of cones.
    RHIBufferHandle m_scaleLineVB{};
    uint32_t m_scaleLineSrv = UINT32_MAX;      ///< SRV index for scale line vertex structured buffer.
    uint32_t m_scaleLineVertCount = 0;

    RHIBufferHandle m_scaleTriVB{};
    uint32_t m_scaleTriSrv = UINT32_MAX;       ///< SRV index for scale triangle vertex structured buffer.
    RHIBufferHandle m_scaleTriIB{};
    uint32_t m_scaleTriIndexCount = 0;
    AxisRange m_scaleCubeRange[3];            ///< X, Y, Z tip cube ranges.
    AxisRange m_scaleCenterRange;             ///< Center uniform-scale cube range.

    // ── Pipeline States ──────────────────────────────────────────────────

    RHIPipelineHandle m_linePipeline{};        ///< LineList topology, depth read, no write, alpha blend.
    RHIPipelineHandle m_trianglePipeline{};    ///< TriangleList topology, same depth/blend settings.
    RHIShaderHandle m_vs{};
    RHIShaderHandle m_ps{};

    // ── State ────────────────────────────────────────────────────────────

    IRHIDevice* m_device = nullptr;
    bool m_initialized = false;

    /// Constant screen-space gizmo size in pixels.
    static constexpr float kTargetScreenPx = 120.0f;
};

} // namespace uz
