/// Editor Gizmo Renderer — Implementation
///
/// Builds all gizmo geometry at Init time (translate arrows, rotate circles,
/// scale cubes) and renders the appropriate set per frame with constant
/// screen-space sizing. Coordinate system: Z-up, X-forward, left-handed.

#include "gizmo_renderer.h"

#include "camera.h"
#include "cb_ring_allocator.h"
#include "editor_input_handler.h"
#include "engine_shaders.h"
#include "log.h"
#include "rhi.h"
#include "root_slots.h"
#include "shader_pipeline.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace uz {

UZ_DEFINE_LOG_CATEGORY(Gizmo, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Number of segments for rotate circle geometry. 64 gives smooth circles.
static constexpr uint32_t kCircleSegments = 64;

/// Length of axis stems in unit gizmo space.
static constexpr float kAxisLength = 0.8f;

/// Cone height for translate arrow tips.
static constexpr float kConeHeight = 0.2f;

/// Cone base radius for translate arrow tips.
static constexpr float kConeRadius = 0.06f;

/// Number of segments for cone base circle.
static constexpr uint32_t kConeSegments = 8;

/// Size of the tip cubes for scale mode.
static constexpr float kCubeHalfSize = 0.04f;

/// Center cube half-size for uniform scale/translate.
static constexpr float kCenterCubeHalfSize = 0.06f;

/// Position along each axis pair for the plane translate handles.
static constexpr float kPlaneHandleOffset = 0.3f;

/// Size of the plane translate handle rectangles.
static constexpr float kPlaneHandleSize = 0.15f;

/// Circle radius for rotate gizmo.
static constexpr float kCircleRadius = 0.85f;

/// Axis colors: X=Red, Y=Green, Z=Blue.
static constexpr float kAxisColors[3][3] = {
    {1.0f, 0.0f, 0.0f},  // X: Red
    {0.0f, 1.0f, 0.0f},  // Y: Green
    {0.0f, 0.0f, 1.0f},  // Z: Blue
};

/// Highlight color when hovering over an axis.
static constexpr float kHoverColor[3] = {1.0f, 1.0f, 0.0f};  // Yellow

/// Active (dragging) color.
static constexpr float kActiveColor[3] = {1.0f, 1.0f, 1.0f}; // White

/// Center handle color (lighter gray).
static constexpr float kCenterColor[3] = {0.8f, 0.8f, 0.8f};

static constexpr float kPi = 3.14159265358979323846f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — cube geometry generation
// ─────────────────────────────────────────────────────────────────────────────

/// Appends a unit cube (12 triangles = 36 indices, 8 vertices) centered at
/// `center` with half-extents `h` and vertex color `col`. Returns the base
/// vertex index so the caller can compute index offsets.
static uint32_t AppendCube(std::vector<GizmoRenderer::GizmoVertex>& verts,
                           std::vector<uint16_t>& indices,
                           float cx, float cy, float cz,
                           float hx, float hy, float hz,
                           const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    // 8 corner vertices of the cube.
    const float corners[8][3] = {
        {cx - hx, cy - hy, cz - hz}, // 0: ---
        {cx + hx, cy - hy, cz - hz}, // 1: +--
        {cx + hx, cy + hy, cz - hz}, // 2: ++-
        {cx - hx, cy + hy, cz - hz}, // 3: -+-
        {cx - hx, cy - hy, cz + hz}, // 4: --+
        {cx + hx, cy - hy, cz + hz}, // 5: +-+
        {cx + hx, cy + hy, cz + hz}, // 6: +++
        {cx - hx, cy + hy, cz + hz}, // 7: -++
    };
    for (int i = 0; i < 8; ++i) {
        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = corners[i][0];
        v.pos[1] = corners[i][1];
        v.pos[2] = corners[i][2];
        v.color[0] = col[0];
        v.color[1] = col[1];
        v.color[2] = col[2];
        verts.push_back(v);
    }

    // 12 triangles (6 faces * 2 tris each). CCW winding (frontCounterClockwise = true).
    static constexpr uint16_t cubeIdx[36] = {
        // -Z face
        0, 1, 2, 0, 2, 3,
        // +Z face
        4, 6, 5, 4, 7, 6,
        // -Y face
        0, 5, 1, 0, 4, 5,
        // +Y face
        2, 7, 3, 2, 6, 7,
        // -X face
        0, 7, 4, 0, 3, 7,
        // +X face
        1, 6, 2, 1, 5, 6,
    };
    for (int i = 0; i < 36; ++i) {
        indices.push_back(static_cast<uint16_t>(base + cubeIdx[i]));
    }

    return base;
}

/// Appends a cone pointing along the given axis direction. The cone base is
/// centered at `baseCenter` and the tip is at `baseCenter + axis * height`.
static void AppendCone(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       float baseX, float baseY, float baseZ,
                       int axisIdx,
                       float height, float radius, uint32_t segments,
                       const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    // Tip vertex: base + height along axis.
    GizmoRenderer::GizmoVertex tip{};
    tip.pos[0] = baseX;
    tip.pos[1] = baseY;
    tip.pos[2] = baseZ;
    tip.pos[axisIdx] += height;
    tip.color[0] = col[0];
    tip.color[1] = col[1];
    tip.color[2] = col[2];
    verts.push_back(tip);

    // Center of base (for base cap triangulation).
    GizmoRenderer::GizmoVertex center{};
    center.pos[0] = baseX;
    center.pos[1] = baseY;
    center.pos[2] = baseZ;
    center.color[0] = col[0];
    center.color[1] = col[1];
    center.color[2] = col[2];
    verts.push_back(center);

    // Determine which two axes form the base circle plane.
    // axisIdx=0 (X) → circle in Y,Z; axisIdx=1 (Y) → circle in X,Z; axisIdx=2 (Z) → circle in X,Y.
    const int a1 = (axisIdx + 1) % 3;
    const int a2 = (axisIdx + 2) % 3;

    // Base circle vertices.
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = baseX;
        v.pos[1] = baseY;
        v.pos[2] = baseZ;
        v.pos[a1] += radius * std::cos(angle);
        v.pos[a2] += radius * std::sin(angle);
        v.color[0] = col[0];
        v.color[1] = col[1];
        v.color[2] = col[2];
        verts.push_back(v);
    }

    // Side triangles: tip → circleVert[i] → circleVert[i+1].
    const uint32_t tipIdx = base;
    const uint32_t centerIdx = base + 1;
    const uint32_t circleBase = base + 2;

    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t curr = circleBase + i;
        const uint32_t next = circleBase + ((i + 1) % segments);

        // Side triangle (CCW when viewed from outside).
        indices.push_back(static_cast<uint16_t>(tipIdx));
        indices.push_back(static_cast<uint16_t>(curr));
        indices.push_back(static_cast<uint16_t>(next));

        // Base cap triangle (CCW when viewed from below).
        indices.push_back(static_cast<uint16_t>(centerIdx));
        indices.push_back(static_cast<uint16_t>(next));
        indices.push_back(static_cast<uint16_t>(curr));
    }
}

/// Appends a quad (2 triangles) defined by 4 corner positions.
static void AppendQuad(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       const float p0[3], const float p1[3],
                       const float p2[3], const float p3[3],
                       const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    auto pushVert = [&](const float p[3]) {
        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    };
    pushVert(p0);
    pushVert(p1);
    pushVert(p2);
    pushVert(p3);

    // Two CCW triangles: p0-p2-p1, p0-p3-p2.
    indices.push_back(static_cast<uint16_t>(base));
    indices.push_back(static_cast<uint16_t>(base + 2));
    indices.push_back(static_cast<uint16_t>(base + 1));
    indices.push_back(static_cast<uint16_t>(base));
    indices.push_back(static_cast<uint16_t>(base + 3));
    indices.push_back(static_cast<uint16_t>(base + 2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry builders
// ─────────────────────────────────────────────────────────────────────────────

void GizmoRenderer::BuildTranslateGeometry() {
    // ── Line geometry: 3 axis stems ────────────────────────────────────

    std::vector<GizmoVertex> lineVerts;
    lineVerts.reserve(6);

    // Each axis: line from origin to (axisLength) along that axis.
    for (int axis = 0; axis < 3; ++axis) {
        GizmoVertex v0{};
        v0.color[0] = kAxisColors[axis][0];
        v0.color[1] = kAxisColors[axis][1];
        v0.color[2] = kAxisColors[axis][2];

        GizmoVertex v1 = v0;
        v1.pos[axis] = kAxisLength;

        lineVerts.push_back(v0);
        lineVerts.push_back(v1);
    }

    m_translateLineVertCount = static_cast<uint32_t>(lineVerts.size());

    RHIBufferDesc lineVbDesc{};
    lineVbDesc.sizeBytes = lineVerts.size() * sizeof(GizmoVertex);
    lineVbDesc.stride = sizeof(GizmoVertex);
    lineVbDesc.usage = RHIBufferUsage::StructuredBuffer;
    lineVbDesc.memory = RHIMemoryType::Upload;
    lineVbDesc.debugName = "GizmoTranslateLineVB";
    m_translateLineVB = m_device->CreateBuffer(lineVbDesc);

    void* mapped = m_device->MapBuffer(m_translateLineVB);
    std::memcpy(mapped, lineVerts.data(), lineVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_translateLineVB);
    m_translateLineSrv = m_device->GetBufferSRVIndex(m_translateLineVB);

    // ── Triangle geometry: 3 cones + 3 plane handles + center cube ───

    std::vector<GizmoVertex> triVerts;
    std::vector<uint16_t> triIndices;
    triVerts.reserve(256);
    triIndices.reserve(512);

    // 3 arrow cones at the tip of each axis.
    for (int axis = 0; axis < 3; ++axis) {
        m_translateConeRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_translateConeRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        float basePos[3] = {0.0f, 0.0f, 0.0f};
        basePos[axis] = kAxisLength;

        AppendCone(triVerts, triIndices,
                   basePos[0], basePos[1], basePos[2],
                   axis, kConeHeight, kConeRadius, kConeSegments,
                   kAxisColors[axis]);

        m_translateConeRange[axis].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_translateConeRange[axis].indexOffset;
    }

    // 3 plane handles: small rectangles at the intersection of two axes.
    // Plane 0 (XY): at (offset, offset, 0) in XY plane.
    // Plane 1 (XZ): at (offset, 0, offset) in XZ plane.
    // Plane 2 (YZ): at (0, offset, offset) in YZ plane.
    {
        // XY plane handle (axis pair 0,1) — colored blue-ish (mix of axes).
        const float hs = kPlaneHandleSize;
        const float off = kPlaneHandleOffset;
        const float xyCol[3] = {0.0f, 0.0f, 1.0f}; // Blue for Z-normal plane.

        m_translatePlaneRange[0].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_translatePlaneRange[0].indexOffset = static_cast<uint32_t>(triIndices.size());
        const float xyP0[3] = {off,      off,      0.0f};
        const float xyP1[3] = {off + hs, off,      0.0f};
        const float xyP2[3] = {off + hs, off + hs, 0.0f};
        const float xyP3[3] = {off,      off + hs, 0.0f};
        AppendQuad(triVerts, triIndices, xyP0, xyP1, xyP2, xyP3, xyCol);
        m_translatePlaneRange[0].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[0].indexOffset;

        // XZ plane handle — colored green (Y-normal plane).
        const float xzCol[3] = {0.0f, 1.0f, 0.0f};
        m_translatePlaneRange[1].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_translatePlaneRange[1].indexOffset = static_cast<uint32_t>(triIndices.size());
        const float xzP0[3] = {off,      0.0f, off};
        const float xzP1[3] = {off + hs, 0.0f, off};
        const float xzP2[3] = {off + hs, 0.0f, off + hs};
        const float xzP3[3] = {off,      0.0f, off + hs};
        AppendQuad(triVerts, triIndices, xzP0, xzP1, xzP2, xzP3, xzCol);
        m_translatePlaneRange[1].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[1].indexOffset;

        // YZ plane handle — colored red (X-normal plane).
        const float yzCol[3] = {1.0f, 0.0f, 0.0f};
        m_translatePlaneRange[2].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_translatePlaneRange[2].indexOffset = static_cast<uint32_t>(triIndices.size());
        const float yzP0[3] = {0.0f, off,      off};
        const float yzP1[3] = {0.0f, off + hs, off};
        const float yzP2[3] = {0.0f, off + hs, off + hs};
        const float yzP3[3] = {0.0f, off,      off + hs};
        AppendQuad(triVerts, triIndices, yzP0, yzP1, yzP2, yzP3, yzCol);
        m_translatePlaneRange[2].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[2].indexOffset;
    }

    // Center cube for screen-space translation.
    m_translateCenterRange.vertexOffset = static_cast<uint32_t>(triVerts.size());
    m_translateCenterRange.indexOffset = static_cast<uint32_t>(triIndices.size());
    AppendCube(triVerts, triIndices,
               0.0f, 0.0f, 0.0f,
               kCenterCubeHalfSize, kCenterCubeHalfSize, kCenterCubeHalfSize,
               kCenterColor);
    m_translateCenterRange.indexCount =
        static_cast<uint32_t>(triIndices.size()) - m_translateCenterRange.indexOffset;

    m_translateTriIndexCount = static_cast<uint32_t>(triIndices.size());

    // Upload triangle VB as structured buffer for SV_VertexID fetch.
    RHIBufferDesc triVbDesc{};
    triVbDesc.sizeBytes = triVerts.size() * sizeof(GizmoVertex);
    triVbDesc.stride = sizeof(GizmoVertex);
    triVbDesc.usage = RHIBufferUsage::StructuredBuffer;
    triVbDesc.memory = RHIMemoryType::Upload;
    triVbDesc.debugName = "GizmoTranslateTriVB";
    m_translateTriVB = m_device->CreateBuffer(triVbDesc);

    void* triMapped = m_device->MapBuffer(m_translateTriVB);
    std::memcpy(triMapped, triVerts.data(), triVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_translateTriVB);
    m_translateTriSrv = m_device->GetBufferSRVIndex(m_translateTriVB);

    // Upload triangle IB.
    RHIBufferDesc triIbDesc{};
    triIbDesc.sizeBytes = triIndices.size() * sizeof(uint16_t);
    triIbDesc.stride = sizeof(uint16_t);
    triIbDesc.usage = RHIBufferUsage::IndexBuffer;
    triIbDesc.memory = RHIMemoryType::Upload;
    triIbDesc.debugName = "GizmoTranslateTriIB";
    m_translateTriIB = m_device->CreateBuffer(triIbDesc);

    void* ibMapped = m_device->MapBuffer(m_translateTriIB);
    std::memcpy(ibMapped, triIndices.data(), triIndices.size() * sizeof(uint16_t));
    m_device->UnmapBuffer(m_translateTriIB);
}

void GizmoRenderer::BuildRotateGeometry() {
    std::vector<GizmoVertex> lineVerts;
    lineVerts.reserve(kCircleSegments * 2 * 3); // 2 verts per segment * 3 axes

    for (int axis = 0; axis < 3; ++axis) {
        m_rotateCircleVertOffset[axis] = static_cast<uint32_t>(lineVerts.size());

        // Determine the two axes that form the circle plane.
        // axis=0 (X): circle in YZ plane.
        // axis=1 (Y): circle in XZ plane.
        // axis=2 (Z): circle in XY plane.
        const int a1 = (axis + 1) % 3;
        const int a2 = (axis + 2) % 3;

        for (uint32_t i = 0; i < kCircleSegments; ++i) {
            const float angle0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(kCircleSegments);
            const float angle1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(kCircleSegments);

            GizmoVertex v0{};
            v0.pos[a1] = kCircleRadius * std::cos(angle0);
            v0.pos[a2] = kCircleRadius * std::sin(angle0);
            v0.color[0] = kAxisColors[axis][0];
            v0.color[1] = kAxisColors[axis][1];
            v0.color[2] = kAxisColors[axis][2];

            GizmoVertex v1{};
            v1.pos[a1] = kCircleRadius * std::cos(angle1);
            v1.pos[a2] = kCircleRadius * std::sin(angle1);
            v1.color[0] = kAxisColors[axis][0];
            v1.color[1] = kAxisColors[axis][1];
            v1.color[2] = kAxisColors[axis][2];

            lineVerts.push_back(v0);
            lineVerts.push_back(v1);
        }

        m_rotateCircleVertCount[axis] =
            static_cast<uint32_t>(lineVerts.size()) - m_rotateCircleVertOffset[axis];
    }

    m_rotateLineVertCount = static_cast<uint32_t>(lineVerts.size());

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = lineVerts.size() * sizeof(GizmoVertex);
    vbDesc.stride = sizeof(GizmoVertex);
    vbDesc.usage = RHIBufferUsage::StructuredBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "GizmoRotateLineVB";
    m_rotateLineVB = m_device->CreateBuffer(vbDesc);

    void* mapped = m_device->MapBuffer(m_rotateLineVB);
    std::memcpy(mapped, lineVerts.data(), lineVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_rotateLineVB);
    m_rotateLineSrv = m_device->GetBufferSRVIndex(m_rotateLineVB);
}

void GizmoRenderer::BuildScaleGeometry() {
    // ── Line geometry: 3 axis stems (same as translate) ────────────────

    std::vector<GizmoVertex> lineVerts;
    lineVerts.reserve(6);

    for (int axis = 0; axis < 3; ++axis) {
        GizmoVertex v0{};
        v0.color[0] = kAxisColors[axis][0];
        v0.color[1] = kAxisColors[axis][1];
        v0.color[2] = kAxisColors[axis][2];

        GizmoVertex v1 = v0;
        v1.pos[axis] = kAxisLength;

        lineVerts.push_back(v0);
        lineVerts.push_back(v1);
    }

    m_scaleLineVertCount = static_cast<uint32_t>(lineVerts.size());

    RHIBufferDesc lineVbDesc{};
    lineVbDesc.sizeBytes = lineVerts.size() * sizeof(GizmoVertex);
    lineVbDesc.stride = sizeof(GizmoVertex);
    lineVbDesc.usage = RHIBufferUsage::StructuredBuffer;
    lineVbDesc.memory = RHIMemoryType::Upload;
    lineVbDesc.debugName = "GizmoScaleLineVB";
    m_scaleLineVB = m_device->CreateBuffer(lineVbDesc);

    void* lineMapped = m_device->MapBuffer(m_scaleLineVB);
    std::memcpy(lineMapped, lineVerts.data(), lineVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_scaleLineVB);
    m_scaleLineSrv = m_device->GetBufferSRVIndex(m_scaleLineVB);

    // ── Triangle geometry: 3 tip cubes + center cube ──────────────────

    std::vector<GizmoVertex> triVerts;
    std::vector<uint16_t> triIndices;
    triVerts.reserve(128);
    triIndices.reserve(256);

    for (int axis = 0; axis < 3; ++axis) {
        m_scaleCubeRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_scaleCubeRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        float center[3] = {0.0f, 0.0f, 0.0f};
        center[axis] = kAxisLength;
        cx = center[0]; cy = center[1]; cz = center[2];

        AppendCube(triVerts, triIndices,
                   cx, cy, cz,
                   kCubeHalfSize, kCubeHalfSize, kCubeHalfSize,
                   kAxisColors[axis]);

        m_scaleCubeRange[axis].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_scaleCubeRange[axis].indexOffset;
    }

    // Center cube for uniform scale.
    m_scaleCenterRange.vertexOffset = static_cast<uint32_t>(triVerts.size());
    m_scaleCenterRange.indexOffset = static_cast<uint32_t>(triIndices.size());
    AppendCube(triVerts, triIndices,
               0.0f, 0.0f, 0.0f,
               kCenterCubeHalfSize, kCenterCubeHalfSize, kCenterCubeHalfSize,
               kCenterColor);
    m_scaleCenterRange.indexCount =
        static_cast<uint32_t>(triIndices.size()) - m_scaleCenterRange.indexOffset;

    m_scaleTriIndexCount = static_cast<uint32_t>(triIndices.size());

    RHIBufferDesc triVbDesc{};
    triVbDesc.sizeBytes = triVerts.size() * sizeof(GizmoVertex);
    triVbDesc.stride = sizeof(GizmoVertex);
    triVbDesc.usage = RHIBufferUsage::StructuredBuffer;
    triVbDesc.memory = RHIMemoryType::Upload;
    triVbDesc.debugName = "GizmoScaleTriVB";
    m_scaleTriVB = m_device->CreateBuffer(triVbDesc);

    void* triMapped = m_device->MapBuffer(m_scaleTriVB);
    std::memcpy(triMapped, triVerts.data(), triVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_scaleTriVB);
    m_scaleTriSrv = m_device->GetBufferSRVIndex(m_scaleTriVB);

    RHIBufferDesc triIbDesc{};
    triIbDesc.sizeBytes = triIndices.size() * sizeof(uint16_t);
    triIbDesc.stride = sizeof(uint16_t);
    triIbDesc.usage = RHIBufferUsage::IndexBuffer;
    triIbDesc.memory = RHIMemoryType::Upload;
    triIbDesc.debugName = "GizmoScaleTriIB";
    m_scaleTriIB = m_device->CreateBuffer(triIbDesc);

    void* ibMapped = m_device->MapBuffer(m_scaleTriIB);
    std::memcpy(ibMapped, triIndices.data(), triIndices.size() * sizeof(uint16_t));
    m_device->UnmapBuffer(m_scaleTriIB);
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool GizmoRenderer::Init(IRHIDevice* device, ShaderPipeline& pipeline,
                          RHIFormat rtvFormat, RHIFormat dsvFormat) {
    m_device = device;

    // ── Compile shaders ──────────────────────────────────────────────────

    auto vsBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GizmoVS),
        EngineShaders::GetSource(EngineShader::GizmoVS));
    if (vsBytecode.empty()) {
        UZ_LOG(Gizmo, Error, "Failed to compile gizmo vertex shader");
        return false;
    }

    auto psBytecode = pipeline.LoadBytecodeFromSource(
        EngineShaders::GetName(EngineShader::GizmoPS),
        EngineShaders::GetSource(EngineShader::GizmoPS));
    if (psBytecode.empty()) {
        UZ_LOG(Gizmo, Error, "Failed to compile gizmo pixel shader");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.debugName = "gizmo_vs";
    m_vs = device->CreateShader(vsDesc);

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.debugName = "gizmo_ps";
    m_ps = device->CreateShader(psDesc);

    // ── No input layout — gizmo uses SV_VertexID + structured buffer ──
    // The bindless root signature does not support IA input layouts.
    // Gizmo vertices are uploaded to a structured buffer and indexed via SV_VertexID.

    // ── Line pipeline (LineList) ─────────────────────────────────────

    RHIGraphicsPipelineDesc lineDesc{};
    lineDesc.vertexShader = m_vs;
    lineDesc.pixelShader = m_ps;
    lineDesc.topology = RHIPrimitiveTopology::LineList;
    lineDesc.renderTargetFormats[0] = rtvFormat;
    lineDesc.renderTargetCount = 1;
    lineDesc.depthStencilFormat = dsvFormat;

    // Depth: test only (read), no write — gizmo renders on top of scene.
    lineDesc.depthStencil.depthEnable = true;
    lineDesc.depthStencil.depthWrite = false;
    lineDesc.depthStencil.depthFunc = RHICompareFunc::GreaterEqual; // Reversed-Z

    // Rasterizer: no culling for lines.
    lineDesc.rasterizer.cullMode = RHICullMode::None;

    // Alpha blending: srcAlpha * src + (1-srcAlpha) * dst.
    lineDesc.renderTargetBlend[0].blendEnable = true;
    lineDesc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
    lineDesc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
    lineDesc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
    lineDesc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
    lineDesc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
    lineDesc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

    // No input layout — vertices via SV_VertexID + structured buffer.
    lineDesc.inputElements = nullptr;
    lineDesc.inputElementCount = 0;
    lineDesc.vertexStride = 0;

    lineDesc.debugName = "GizmoLinePipeline";

    m_linePipeline = device->CreateGraphicsPipeline(lineDesc);
    if (!m_linePipeline.IsValid()) {
        UZ_LOG(Gizmo, Error, "Failed to create gizmo line pipeline");
        return false;
    }

    // ── Triangle pipeline (TriangleList) ─────────────────────────────

    RHIGraphicsPipelineDesc triDesc = lineDesc; // Copy all shared state.
    triDesc.topology = RHIPrimitiveTopology::TriangleList;
    triDesc.rasterizer.cullMode = RHICullMode::None; // No backface culling for small gizmo geometry.
    triDesc.debugName = "GizmoTrianglePipeline";

    m_trianglePipeline = device->CreateGraphicsPipeline(triDesc);
    if (!m_trianglePipeline.IsValid()) {
        UZ_LOG(Gizmo, Error, "Failed to create gizmo triangle pipeline");
        return false;
    }

    // ── Build geometry ───────────────────────────────────────────────────

    BuildTranslateGeometry();
    BuildRotateGeometry();
    BuildScaleGeometry();

    m_initialized = true;
    UZ_LOG(Gizmo, Info, "Gizmo renderer initialized");
    return true;
}

void GizmoRenderer::Shutdown() {
    if (m_device) {
        // Destroy all GPU resources.
        if (m_translateLineVB.IsValid()) m_device->DestroyBuffer(m_translateLineVB);
        if (m_translateTriVB.IsValid())  m_device->DestroyBuffer(m_translateTriVB);
        if (m_translateTriIB.IsValid())  m_device->DestroyBuffer(m_translateTriIB);
        if (m_rotateLineVB.IsValid())    m_device->DestroyBuffer(m_rotateLineVB);
        if (m_scaleLineVB.IsValid())     m_device->DestroyBuffer(m_scaleLineVB);
        if (m_scaleTriVB.IsValid())      m_device->DestroyBuffer(m_scaleTriVB);
        if (m_scaleTriIB.IsValid())      m_device->DestroyBuffer(m_scaleTriIB);
    }

    m_translateLineVB = {};
    m_translateTriVB = {};
    m_translateTriIB = {};
    m_rotateLineVB = {};
    m_scaleLineVB = {};
    m_scaleTriVB = {};
    m_scaleTriIB = {};
    m_translateLineSrv = UINT32_MAX;
    m_translateTriSrv = UINT32_MAX;
    m_rotateLineSrv = UINT32_MAX;
    m_scaleLineSrv = UINT32_MAX;
    m_scaleTriSrv = UINT32_MAX;
    m_device = nullptr;
    m_initialized = false;
    UZ_LOG(Gizmo, Info, "Gizmo renderer shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-axis draw helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Determines the draw color and alpha for an axis based on hover/active state.
static void ResolveAxisAppearance(int axisIndex, int hoveredAxis, int activeAxis,
                                   const float defaultColor[3],
                                   float outColor[3], float& outAlpha) {
    // Default: full opacity, default axis color.
    outColor[0] = defaultColor[0];
    outColor[1] = defaultColor[1];
    outColor[2] = defaultColor[2];
    outAlpha = 1.0f;

    if (activeAxis >= 0) {
        // Something is being dragged.
        if (axisIndex == activeAxis) {
            // This is the active axis — white.
            outColor[0] = kActiveColor[0];
            outColor[1] = kActiveColor[1];
            outColor[2] = kActiveColor[2];
            outAlpha = 1.0f;
        } else {
            // Not the active axis — dim.
            outAlpha = 0.3f;
        }
    } else if (hoveredAxis >= 0 && axisIndex == hoveredAxis) {
        // Hovered, not dragging — yellow.
        outColor[0] = kHoverColor[0];
        outColor[1] = kHoverColor[1];
        outColor[2] = kHoverColor[2];
        outAlpha = 1.0f;
    }
}

void GizmoRenderer::DrawAxisSubset(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                                    const GizmoCB& baseCB,
                                    int axisIndex, int hoveredAxis, int activeAxis,
                                    const float axisColor[3],
                                    uint32_t srvIndex, uint32_t vertexOffset, uint32_t vertexCount,
                                    bool isTriangles) {
    GizmoCB cb = baseCB;
    cb.vertexBufferSrv = srvIndex;
    ResolveAxisAppearance(axisIndex, hoveredAxis, activeAxis, axisColor,
                          cb.axisColor, cb.alpha);

    CBAllocation alloc = cbRing.Allocate(sizeof(GizmoCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(GizmoCB));

    cmdList->SetPipeline(isTriangles ? m_trianglePipeline : m_linePipeline);
    cmdList->SetRootCBV(kSlotPerFrame, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr, sizeof(GizmoCB));
    // No SetVertexBuffer — vertices are fetched via SV_VertexID from structured buffer.
    cmdList->Draw(vertexCount, 1, vertexOffset, 0);
}

void GizmoRenderer::DrawAxisSubsetIndexed(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                                           const GizmoCB& baseCB,
                                           int axisIndex, int hoveredAxis, int activeAxis,
                                           const float axisColor[3],
                                           uint32_t srvIndex, RHIBufferHandle ib,
                                           uint32_t indexOffset, uint32_t indexCount,
                                           uint32_t vertexOffset) {
    GizmoCB cb = baseCB;
    cb.vertexBufferSrv = srvIndex;
    ResolveAxisAppearance(axisIndex, hoveredAxis, activeAxis, axisColor,
                          cb.axisColor, cb.alpha);

    CBAllocation alloc = cbRing.Allocate(sizeof(GizmoCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(GizmoCB));

    cmdList->SetPipeline(m_trianglePipeline);
    cmdList->SetRootCBV(kSlotPerFrame, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr, sizeof(GizmoCB));
    // No SetVertexBuffer — vertices are fetched via SV_VertexID from structured buffer.
    cmdList->SetIndexBuffer(ib, RHIIndexFormat::Uint16);
    cmdList->DrawIndexed(indexCount, 1, indexOffset, static_cast<int32_t>(vertexOffset), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Draw
// ─────────────────────────────────────────────────────────────────────────────

void GizmoRenderer::Draw(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                          const Camera& camera,
                          const Vec3& worldPos, const Quat& /*entityRotation*/,
                          GizmoMode mode, GizmoSpace /*space*/,
                          int hoveredAxis,
                          int activeAxis) {
    if (!m_initialized) return;

    // ── Compute constant screen-space scale ──────────────────────────

    const Vec3 camPos = camera.GetPosition();
    const float dx = worldPos.x - camPos.x;
    const float dy = worldPos.y - camPos.y;
    const float dz = worldPos.z - camPos.z;
    const float distToCamera = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Prevent division by zero when camera is at the gizmo position.
    if (distToCamera < 0.001f) return;

    const float tanHalfFov = std::tan(camera.GetFovY() * 0.5f);

    // Scale so the gizmo occupies ~kTargetScreenPx on screen regardless of distance.
    // The formula: screenScale = dist * tan(fov/2) * (targetPx / viewportHeight).
    // Since we don't have viewport height here, use a fraction relative to projection.
    // The projection's vertical extent at distance d is: 2 * d * tan(fov/2).
    // To occupy targetPx out of viewportHeight pixels, we need:
    //   worldSize = 2 * d * tan(fov/2) * (targetPx / viewportHeight)
    // We approximate viewportHeight as the half-height in NDC = 1.0, giving:
    //   screenScale = distToCamera * tanHalfFov * (targetPx / viewportHeight).
    // Without viewport height, we use a fixed ratio. The projection matrix already
    // encodes viewport height via aspect ratio. Using 720p as reference baseline:
    const float screenScale = distToCamera * tanHalfFov * (kTargetScreenPx / 720.0f);

    // ── Prepare base constant buffer ────────────────────────────────

    GizmoCB baseCB{};
    baseCB.viewProj = camera.GetViewProjection();
    baseCB.gizmoWorldPos[0] = worldPos.x;
    baseCB.gizmoWorldPos[1] = worldPos.y;
    baseCB.gizmoWorldPos[2] = worldPos.z;
    baseCB.screenScale = screenScale;
    baseCB.alpha = 1.0f;
    baseCB.vertexBufferSrv = UINT32_MAX; // Overridden per draw call.
    baseCB._pad0 = 0;
    baseCB._pad1 = 0;
    baseCB._pad2 = 0;

    // ── Draw based on mode ──────────────────────────────────────────

    switch (mode) {
    case GizmoMode::Translate: {
        // Axis lines: 2 verts per axis, 3 axes, drawn as LineList.
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubset(cmdList, cbRing, baseCB,
                           axis, hoveredAxis, activeAxis,
                           kAxisColors[axis],
                           m_translateLineSrv,
                           axis * 2, 2,
                           false);
        }

        // Arrow cones (triangles).
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_translateTriSrv, m_translateTriIB,
                                  m_translateConeRange[axis].indexOffset,
                                  m_translateConeRange[axis].indexCount,
                                  0); // vertexOffset=0 since indices are absolute.
        }

        // Plane handles (triangles). Axis encoding: 3=XY, 4=XZ, 5=YZ.
        const float planeColors[3][3] = {
            {0.0f, 0.0f, 1.0f}, // XY plane → Blue
            {0.0f, 1.0f, 0.0f}, // XZ plane → Green
            {1.0f, 0.0f, 0.0f}, // YZ plane → Red
        };
        for (int plane = 0; plane < 3; ++plane) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  3 + plane, hoveredAxis, activeAxis,
                                  planeColors[plane],
                                  m_translateTriSrv, m_translateTriIB,
                                  m_translatePlaneRange[plane].indexOffset,
                                  m_translatePlaneRange[plane].indexCount,
                                  0);
        }

        // Center cube (axis index 6 = all).
        DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                              6, hoveredAxis, activeAxis,
                              kCenterColor,
                              m_translateTriSrv, m_translateTriIB,
                              m_translateCenterRange.indexOffset,
                              m_translateCenterRange.indexCount,
                              0);
        break;
    }

    case GizmoMode::Rotate: {
        // 3 axis circles drawn as line lists.
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubset(cmdList, cbRing, baseCB,
                           axis, hoveredAxis, activeAxis,
                           kAxisColors[axis],
                           m_rotateLineSrv,
                           m_rotateCircleVertOffset[axis],
                           m_rotateCircleVertCount[axis],
                           false);
        }
        break;
    }

    case GizmoMode::Scale: {
        // Axis lines.
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubset(cmdList, cbRing, baseCB,
                           axis, hoveredAxis, activeAxis,
                           kAxisColors[axis],
                           m_scaleLineSrv,
                           axis * 2, 2,
                           false);
        }

        // Tip cubes.
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_scaleTriSrv, m_scaleTriIB,
                                  m_scaleCubeRange[axis].indexOffset,
                                  m_scaleCubeRange[axis].indexCount,
                                  0);
        }

        // Center cube (uniform scale).
        DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                              6, hoveredAxis, activeAxis,
                              kCenterColor,
                              m_scaleTriSrv, m_scaleTriIB,
                              m_scaleCenterRange.indexOffset,
                              m_scaleCenterRange.indexCount,
                              0);
        break;
    }
    }
}

} // namespace uz
