/// Editor Gizmo Renderer — Implementation
///
/// Builds all gizmo geometry at Init time as solid 3D TriangleList meshes
/// (cylinders, cones, rings, cubes) with per-vertex normals for N-dot-L shading.
/// Coordinate system: Z-up, X-forward, left-handed.

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
// Constants — UE5 parity values
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kPi = 3.14159265358979323846f;

/// Number of segments for rotate ring geometry. 64 gives smooth circles.
static constexpr uint32_t kCircleSegments = 64;

/// Length of axis stems in unit gizmo space.
static constexpr float kAxisLength = 0.70f;

/// Cylinder radius for axis stems.
static constexpr float kStemRadius = 0.015f;

/// Number of sides for cylinder stem geometry.
static constexpr uint32_t kStemSegments = 12;

/// Cone height for translate arrow tips.
static constexpr float kConeHeight = 0.22f;

/// Cone base radius for translate arrow tips.
static constexpr float kConeRadius = 0.07f;

/// Number of segments for cone base circle.
static constexpr uint32_t kConeSegments = 16;

/// Size of the tip cubes for scale mode.
static constexpr float kCubeHalfSize = 0.06f;

/// Center cube half-size for uniform scale/translate.
static constexpr float kCenterCubeHalfSize = 0.08f;

/// Position along each axis pair for the plane translate handles.
static constexpr float kPlaneHandleOffset = 0.55f;

/// Size of the plane translate handle rectangles.
static constexpr float kPlaneHandleSize = 0.15f;

/// Circle radius for rotate gizmo — matches axis length.
static constexpr float kCircleRadius = 0.70f;

/// Torus minor radius for thick rotate rings.
static constexpr float kCircleThickness = 0.025f;

/// Axis colors: UE5 muted tones (linear RGB).
static constexpr float kAxisColors[3][3] = {
    {0.594f, 0.0197f, 0.0f},   // X: Muted red
    {0.1349f, 0.3959f, 0.0f},  // Y: Muted green
    {0.0251f, 0.207f, 0.85f},  // Z: Deep blue
};

/// Highlight color when hovering over an axis — yellow.
static constexpr float kHoverColor[3] = {1.0f, 1.0f, 0.0f};

/// Active (dragging) color — yellow (same as hover per UE5).
static constexpr float kActiveColor[3] = {1.0f, 1.0f, 0.0f};

/// Center handle color — light grey.
static constexpr float kCenterColor[3] = {0.765f, 0.765f, 0.765f};

/// Normal alpha for non-hovered axes — UE5 gizmos are slightly transparent.
static constexpr float kNormalAlpha = 0.8f;

/// Inactive alpha when another axis is being dragged.
static constexpr float kInactiveAlpha = 0.3f;

/// Plane handle normal alpha.
static constexpr float kPlaneNormalAlpha = 0.6f;

/// Plane handle inactive alpha when another axis is being dragged.
static constexpr float kPlaneInactiveAlpha = 0.15f;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — geometry generation (all produce normals)
// ─────────────────────────────────────────────────────────────────────────────

/// Appends a cube (12 triangles, 24 vertices with face normals) centered at
/// `center` with half-extents (hx, hy, hz) and vertex color `col`.
static void AppendCube(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       float cx, float cy, float cz,
                       float hx, float hy, float hz,
                       const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    // 6 faces, each with 4 vertices (unique normals per face) = 24 vertices.
    // Face data: {normal, corner offsets for 4 verts}.
    struct FaceDef {
        float nx, ny, nz;
        float v[4][3]; // 4 corner positions as offsets from center
    };

    const FaceDef faces[6] = {
        // +X face
        { 1, 0, 0, {{ hx,-hy,-hz}, { hx, hy,-hz}, { hx, hy, hz}, { hx,-hy, hz}} },
        // -X face
        {-1, 0, 0, {{-hx, hy,-hz}, {-hx,-hy,-hz}, {-hx,-hy, hz}, {-hx, hy, hz}} },
        // +Y face
        { 0, 1, 0, {{ hx, hy,-hz}, {-hx, hy,-hz}, {-hx, hy, hz}, { hx, hy, hz}} },
        // -Y face
        { 0,-1, 0, {{-hx,-hy,-hz}, { hx,-hy,-hz}, { hx,-hy, hz}, {-hx,-hy, hz}} },
        // +Z face
        { 0, 0, 1, {{-hx,-hy, hz}, { hx,-hy, hz}, { hx, hy, hz}, {-hx, hy, hz}} },
        // -Z face
        { 0, 0,-1, {{ hx,-hy,-hz}, {-hx,-hy,-hz}, {-hx, hy,-hz}, { hx, hy,-hz}} },
    };

    for (int f = 0; f < 6; ++f) {
        const auto& face = faces[f];
        for (int v = 0; v < 4; ++v) {
            GizmoRenderer::GizmoVertex gv{};
            gv.pos[0] = cx + face.v[v][0];
            gv.pos[1] = cy + face.v[v][1];
            gv.pos[2] = cz + face.v[v][2];
            gv.normal[0] = face.nx;
            gv.normal[1] = face.ny;
            gv.normal[2] = face.nz;
            gv.color[0] = col[0];
            gv.color[1] = col[1];
            gv.color[2] = col[2];
            verts.push_back(gv);
        }
        // Two CCW triangles per face: 0-1-2, 0-2-3
        const uint32_t fb = base + static_cast<uint32_t>(f) * 4;
        indices.push_back(static_cast<uint16_t>(fb + 0));
        indices.push_back(static_cast<uint16_t>(fb + 1));
        indices.push_back(static_cast<uint16_t>(fb + 2));
        indices.push_back(static_cast<uint16_t>(fb + 0));
        indices.push_back(static_cast<uint16_t>(fb + 2));
        indices.push_back(static_cast<uint16_t>(fb + 3));
    }
}

/// Appends a cone pointing along the given axis direction with per-vertex normals.
/// The cone base center is at (baseX, baseY, baseZ) and the tip extends along `axisIdx`.
static void AppendCone(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       float baseX, float baseY, float baseZ,
                       int axisIdx,
                       float height, float radius, uint32_t segments,
                       const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    // Determine the two axes that form the base circle plane.
    const int a1 = (axisIdx + 1) % 3;
    const int a2 = (axisIdx + 2) % 3;

    // The slope angle determines the normal's axial component.
    // For a cone: normal = normalize(radialDir * height + axisDir * radius)
    // The axial component is: radius / sqrt(radius^2 + height^2)
    // The radial component is: height / sqrt(radius^2 + height^2)
    const float slant = std::sqrt(radius * radius + height * height);
    const float nRadial = height / slant;  // radial normal scale
    const float nAxial = radius / slant;   // axial normal scale (negative for pointing outward)

    // Side vertices: each segment needs unique tip + base vertex for correct normals.
    // Total side verts: segments * 3 (tip + 2 base per triangle, but we can share base ring).
    // We use: segments ring verts + segments tip verts for sides, plus center + ring for base cap.

    // Base ring vertices (for sides — normals point outward along cone surface).
    const uint32_t ringBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = baseX; v.pos[1] = baseY; v.pos[2] = baseZ;
        v.pos[a1] += radius * cosA;
        v.pos[a2] += radius * sinA;

        // Normal: outward radial + upward along axis (cone slope).
        v.normal[a1] = nRadial * cosA;
        v.normal[a2] = nRadial * sinA;
        v.normal[axisIdx] = nAxial;

        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }

    // Tip vertices — one per segment so each side triangle has the correct interpolated normal.
    const uint32_t tipBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        // Average normal between current and next segment.
        const float angle0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float angle1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segments);
        const float midCos = std::cos((angle0 + angle1) * 0.5f);
        const float midSin = std::sin((angle0 + angle1) * 0.5f);

        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = baseX; v.pos[1] = baseY; v.pos[2] = baseZ;
        v.pos[axisIdx] += height;

        v.normal[a1] = nRadial * midCos;
        v.normal[a2] = nRadial * midSin;
        v.normal[axisIdx] = nAxial;

        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }

    // Side triangles: tip[i] → ring[i] → ring[i+1].
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1) % segments;
        indices.push_back(static_cast<uint16_t>(tipBase + i));
        indices.push_back(static_cast<uint16_t>(ringBase + i));
        indices.push_back(static_cast<uint16_t>(ringBase + next));
    }

    // Base cap: center vertex + ring vertices with downward normals.
    const uint32_t capCenter = static_cast<uint32_t>(verts.size());
    {
        GizmoRenderer::GizmoVertex cv{};
        cv.pos[0] = baseX; cv.pos[1] = baseY; cv.pos[2] = baseZ;
        cv.normal[axisIdx] = -1.0f; // Normal points away from tip
        cv.color[0] = col[0]; cv.color[1] = col[1]; cv.color[2] = col[2];
        verts.push_back(cv);
    }

    const uint32_t capRingBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = baseX; v.pos[1] = baseY; v.pos[2] = baseZ;
        v.pos[a1] += radius * std::cos(angle);
        v.pos[a2] += radius * std::sin(angle);
        v.normal[axisIdx] = -1.0f; // Normal points away from tip
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }

    // Base cap triangles: center → next → curr (CCW from below).
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1) % segments;
        indices.push_back(static_cast<uint16_t>(capCenter));
        indices.push_back(static_cast<uint16_t>(capRingBase + next));
        indices.push_back(static_cast<uint16_t>(capRingBase + i));
    }
}

/// Appends a cylinder along the given axis from t=0 to t=`length` with the given
/// radius and segment count. Generates proper outward-facing normals.
static void AppendCylinder(std::vector<GizmoRenderer::GizmoVertex>& verts,
                           std::vector<uint16_t>& indices,
                           int axisIdx, float length, float radius, uint32_t segments,
                           const float col[3]) {
    const int a1 = (axisIdx + 1) % 3;
    const int a2 = (axisIdx + 2) % 3;

    // Bottom ring (at origin along axis).
    const uint32_t bottomRing = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        GizmoRenderer::GizmoVertex v{};
        v.pos[a1] = radius * cosA;
        v.pos[a2] = radius * sinA;
        v.pos[axisIdx] = 0.0f;
        v.normal[a1] = cosA;
        v.normal[a2] = sinA;
        v.normal[axisIdx] = 0.0f;
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }

    // Top ring (at `length` along axis).
    const uint32_t topRing = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        GizmoRenderer::GizmoVertex v{};
        v.pos[a1] = radius * cosA;
        v.pos[a2] = radius * sinA;
        v.pos[axisIdx] = length;
        v.normal[a1] = cosA;
        v.normal[a2] = sinA;
        v.normal[axisIdx] = 0.0f;
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }

    // Side quads: bottom[i]→top[i]→top[next]→bottom[next].
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1) % segments;
        // Triangle 1: bottom[i] → top[i] → top[next]
        indices.push_back(static_cast<uint16_t>(bottomRing + i));
        indices.push_back(static_cast<uint16_t>(topRing + i));
        indices.push_back(static_cast<uint16_t>(topRing + next));
        // Triangle 2: bottom[i] → top[next] → bottom[next]
        indices.push_back(static_cast<uint16_t>(bottomRing + i));
        indices.push_back(static_cast<uint16_t>(topRing + next));
        indices.push_back(static_cast<uint16_t>(bottomRing + next));
    }

    // Bottom cap.
    const uint32_t bottomCenter = static_cast<uint32_t>(verts.size());
    {
        GizmoRenderer::GizmoVertex cv{};
        cv.pos[axisIdx] = 0.0f;
        cv.normal[axisIdx] = -1.0f;
        cv.color[0] = col[0]; cv.color[1] = col[1]; cv.color[2] = col[2];
        verts.push_back(cv);
    }
    const uint32_t bottomCapRing = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        GizmoRenderer::GizmoVertex v{};
        v.pos[a1] = radius * std::cos(angle);
        v.pos[a2] = radius * std::sin(angle);
        v.pos[axisIdx] = 0.0f;
        v.normal[axisIdx] = -1.0f;
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1) % segments;
        indices.push_back(static_cast<uint16_t>(bottomCenter));
        indices.push_back(static_cast<uint16_t>(bottomCapRing + next));
        indices.push_back(static_cast<uint16_t>(bottomCapRing + i));
    }

    // Top cap.
    const uint32_t topCenter = static_cast<uint32_t>(verts.size());
    {
        GizmoRenderer::GizmoVertex cv{};
        cv.pos[axisIdx] = length;
        cv.normal[axisIdx] = 1.0f;
        cv.color[0] = col[0]; cv.color[1] = col[1]; cv.color[2] = col[2];
        verts.push_back(cv);
    }
    const uint32_t topCapRing = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        GizmoRenderer::GizmoVertex v{};
        v.pos[a1] = radius * std::cos(angle);
        v.pos[a2] = radius * std::sin(angle);
        v.pos[axisIdx] = length;
        v.normal[axisIdx] = 1.0f;
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        verts.push_back(v);
    }
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t next = (i + 1) % segments;
        indices.push_back(static_cast<uint16_t>(topCenter));
        indices.push_back(static_cast<uint16_t>(topCapRing + i));
        indices.push_back(static_cast<uint16_t>(topCapRing + next));
    }
}

/// Appends a flat ring (annulus) for the rotate gizmo. The ring lies in the plane
/// perpendicular to `axisIdx`, centered at origin, with inner radius `rInner` and
/// outer radius `rOuter`. Normals point along the axis.
static void AppendRing(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       int axisIdx, float rInner, float rOuter, uint32_t segments,
                       const float col[3]) {
    const int a1 = (axisIdx + 1) % 3;
    const int a2 = (axisIdx + 2) % 3;

    // Front face (normal = +axis).
    const uint32_t frontBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        // Inner vertex
        GizmoRenderer::GizmoVertex vi{};
        vi.pos[a1] = rInner * cosA;
        vi.pos[a2] = rInner * sinA;
        vi.pos[axisIdx] = 0.0f;
        vi.normal[axisIdx] = 1.0f;
        vi.color[0] = col[0]; vi.color[1] = col[1]; vi.color[2] = col[2];
        verts.push_back(vi);

        // Outer vertex
        GizmoRenderer::GizmoVertex vo{};
        vo.pos[a1] = rOuter * cosA;
        vo.pos[a2] = rOuter * sinA;
        vo.pos[axisIdx] = 0.0f;
        vo.normal[axisIdx] = 1.0f;
        vo.color[0] = col[0]; vo.color[1] = col[1]; vo.color[2] = col[2];
        verts.push_back(vo);
    }

    // Front face triangles.
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t curr = frontBase + i * 2;
        const uint32_t next = frontBase + (i + 1) * 2;
        // inner[i] → outer[i] → outer[i+1]
        indices.push_back(static_cast<uint16_t>(curr));
        indices.push_back(static_cast<uint16_t>(curr + 1));
        indices.push_back(static_cast<uint16_t>(next + 1));
        // inner[i] → outer[i+1] → inner[i+1]
        indices.push_back(static_cast<uint16_t>(curr));
        indices.push_back(static_cast<uint16_t>(next + 1));
        indices.push_back(static_cast<uint16_t>(next));
    }

    // Back face (normal = -axis) — reverse winding for correct face direction.
    const uint32_t backBase = static_cast<uint32_t>(verts.size());
    for (uint32_t i = 0; i <= segments; ++i) {
        const float angle = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segments);
        const float cosA = std::cos(angle);
        const float sinA = std::sin(angle);

        GizmoRenderer::GizmoVertex vi{};
        vi.pos[a1] = rInner * cosA;
        vi.pos[a2] = rInner * sinA;
        vi.pos[axisIdx] = 0.0f;
        vi.normal[axisIdx] = -1.0f;
        vi.color[0] = col[0]; vi.color[1] = col[1]; vi.color[2] = col[2];
        verts.push_back(vi);

        GizmoRenderer::GizmoVertex vo{};
        vo.pos[a1] = rOuter * cosA;
        vo.pos[a2] = rOuter * sinA;
        vo.pos[axisIdx] = 0.0f;
        vo.normal[axisIdx] = -1.0f;
        vo.color[0] = col[0]; vo.color[1] = col[1]; vo.color[2] = col[2];
        verts.push_back(vo);
    }

    // Back face triangles (reversed winding).
    for (uint32_t i = 0; i < segments; ++i) {
        const uint32_t curr = backBase + i * 2;
        const uint32_t next = backBase + (i + 1) * 2;
        // inner[i] → outer[i+1] → outer[i]
        indices.push_back(static_cast<uint16_t>(curr));
        indices.push_back(static_cast<uint16_t>(next + 1));
        indices.push_back(static_cast<uint16_t>(curr + 1));
        // inner[i] → inner[i+1] → outer[i+1]
        indices.push_back(static_cast<uint16_t>(curr));
        indices.push_back(static_cast<uint16_t>(next));
        indices.push_back(static_cast<uint16_t>(next + 1));
    }
}

/// Appends a quad (2 triangles) defined by 4 corner positions with a face normal.
static void AppendQuad(std::vector<GizmoRenderer::GizmoVertex>& verts,
                       std::vector<uint16_t>& indices,
                       const float p0[3], const float p1[3],
                       const float p2[3], const float p3[3],
                       const float normal[3],
                       const float col[3]) {
    const uint32_t base = static_cast<uint32_t>(verts.size());

    auto pushVert = [&](const float p[3]) {
        GizmoRenderer::GizmoVertex v{};
        v.pos[0] = p[0]; v.pos[1] = p[1]; v.pos[2] = p[2];
        v.normal[0] = normal[0]; v.normal[1] = normal[1]; v.normal[2] = normal[2];
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
    std::vector<GizmoVertex> triVerts;
    std::vector<uint16_t> triIndices;
    triVerts.reserve(1024);
    triIndices.reserve(2048);

    // 3 axis cylinder stems.
    for (int axis = 0; axis < 3; ++axis) {
        m_translateStemRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_translateStemRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        AppendCylinder(triVerts, triIndices, axis, kAxisLength, kStemRadius, kStemSegments,
                       kAxisColors[axis]);

        m_translateStemRange[axis].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_translateStemRange[axis].indexOffset;
    }

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
    {
        const float hs = kPlaneHandleSize;
        const float off = kPlaneHandleOffset;

        // XY plane handle — colored blue (Z-normal plane).
        {
            const float xyCol[3] = {kAxisColors[2][0], kAxisColors[2][1], kAxisColors[2][2]};
            const float xyNormal[3] = {0.0f, 0.0f, 1.0f};
            m_translatePlaneRange[0].vertexOffset = static_cast<uint32_t>(triVerts.size());
            m_translatePlaneRange[0].indexOffset = static_cast<uint32_t>(triIndices.size());
            const float xyP0[3] = {off,      off,      0.0f};
            const float xyP1[3] = {off + hs, off,      0.0f};
            const float xyP2[3] = {off + hs, off + hs, 0.0f};
            const float xyP3[3] = {off,      off + hs, 0.0f};
            AppendQuad(triVerts, triIndices, xyP0, xyP1, xyP2, xyP3, xyNormal, xyCol);
            m_translatePlaneRange[0].indexCount =
                static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[0].indexOffset;
        }

        // XZ plane handle — colored green (Y-normal plane).
        {
            const float xzCol[3] = {kAxisColors[1][0], kAxisColors[1][1], kAxisColors[1][2]};
            const float xzNormal[3] = {0.0f, 1.0f, 0.0f};
            m_translatePlaneRange[1].vertexOffset = static_cast<uint32_t>(triVerts.size());
            m_translatePlaneRange[1].indexOffset = static_cast<uint32_t>(triIndices.size());
            const float xzP0[3] = {off,      0.0f, off};
            const float xzP1[3] = {off + hs, 0.0f, off};
            const float xzP2[3] = {off + hs, 0.0f, off + hs};
            const float xzP3[3] = {off,      0.0f, off + hs};
            AppendQuad(triVerts, triIndices, xzP0, xzP1, xzP2, xzP3, xzNormal, xzCol);
            m_translatePlaneRange[1].indexCount =
                static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[1].indexOffset;
        }

        // YZ plane handle — colored red (X-normal plane).
        {
            const float yzCol[3] = {kAxisColors[0][0], kAxisColors[0][1], kAxisColors[0][2]};
            const float yzNormal[3] = {1.0f, 0.0f, 0.0f};
            m_translatePlaneRange[2].vertexOffset = static_cast<uint32_t>(triVerts.size());
            m_translatePlaneRange[2].indexOffset = static_cast<uint32_t>(triIndices.size());
            const float yzP0[3] = {0.0f, off,      off};
            const float yzP1[3] = {0.0f, off + hs, off};
            const float yzP2[3] = {0.0f, off + hs, off + hs};
            const float yzP3[3] = {0.0f, off,      off + hs};
            AppendQuad(triVerts, triIndices, yzP0, yzP1, yzP2, yzP3, yzNormal, yzCol);
            m_translatePlaneRange[2].indexCount =
                static_cast<uint32_t>(triIndices.size()) - m_translatePlaneRange[2].indexOffset;
        }
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
    std::vector<GizmoVertex> triVerts;
    std::vector<uint16_t> triIndices;
    triVerts.reserve(2048);
    triIndices.reserve(4096);

    const float rInner = kCircleRadius - kCircleThickness;
    const float rOuter = kCircleRadius + kCircleThickness;

    for (int axis = 0; axis < 3; ++axis) {
        m_rotateRingRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_rotateRingRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        AppendRing(triVerts, triIndices, axis, rInner, rOuter, kCircleSegments,
                   kAxisColors[axis]);

        m_rotateRingRange[axis].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_rotateRingRange[axis].indexOffset;
    }

    m_rotateTriIndexCount = static_cast<uint32_t>(triIndices.size());

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = triVerts.size() * sizeof(GizmoVertex);
    vbDesc.stride = sizeof(GizmoVertex);
    vbDesc.usage = RHIBufferUsage::StructuredBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "GizmoRotateTriVB";
    m_rotateTriVB = m_device->CreateBuffer(vbDesc);

    void* mapped = m_device->MapBuffer(m_rotateTriVB);
    std::memcpy(mapped, triVerts.data(), triVerts.size() * sizeof(GizmoVertex));
    m_device->UnmapBuffer(m_rotateTriVB);
    m_rotateTriSrv = m_device->GetBufferSRVIndex(m_rotateTriVB);

    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = triIndices.size() * sizeof(uint16_t);
    ibDesc.stride = sizeof(uint16_t);
    ibDesc.usage = RHIBufferUsage::IndexBuffer;
    ibDesc.memory = RHIMemoryType::Upload;
    ibDesc.debugName = "GizmoRotateTriIB";
    m_rotateTriIB = m_device->CreateBuffer(ibDesc);

    void* ibMapped = m_device->MapBuffer(m_rotateTriIB);
    std::memcpy(ibMapped, triIndices.data(), triIndices.size() * sizeof(uint16_t));
    m_device->UnmapBuffer(m_rotateTriIB);
}

void GizmoRenderer::BuildScaleGeometry() {
    std::vector<GizmoVertex> triVerts;
    std::vector<uint16_t> triIndices;
    triVerts.reserve(1024);
    triIndices.reserve(2048);

    // 3 axis cylinder stems (same as translate).
    for (int axis = 0; axis < 3; ++axis) {
        m_scaleStemRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_scaleStemRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        AppendCylinder(triVerts, triIndices, axis, kAxisLength, kStemRadius, kStemSegments,
                       kAxisColors[axis]);

        m_scaleStemRange[axis].indexCount =
            static_cast<uint32_t>(triIndices.size()) - m_scaleStemRange[axis].indexOffset;
    }

    // 3 tip cubes at the end of each axis.
    for (int axis = 0; axis < 3; ++axis) {
        m_scaleCubeRange[axis].vertexOffset = static_cast<uint32_t>(triVerts.size());
        m_scaleCubeRange[axis].indexOffset = static_cast<uint32_t>(triIndices.size());

        float center[3] = {0.0f, 0.0f, 0.0f};
        center[axis] = kAxisLength;

        AppendCube(triVerts, triIndices,
                   center[0], center[1], center[2],
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

    // ── Single triangle pipeline (TriangleList) ─────────────────────────

    RHIGraphicsPipelineDesc triDesc{};
    triDesc.vertexShader = m_vs;
    triDesc.pixelShader = m_ps;
    triDesc.topology = RHIPrimitiveTopology::TriangleList;
    triDesc.renderTargetFormats[0] = rtvFormat;
    triDesc.renderTargetCount = 1;
    triDesc.depthStencilFormat = dsvFormat;

    // Depth disabled: gizmo ALWAYS renders on top of all scene geometry (UE5 behavior).
    triDesc.depthStencil.depthEnable = false;
    triDesc.depthStencil.depthWrite = false;

    // No backface culling for small gizmo geometry — view from any angle.
    triDesc.rasterizer.cullMode = RHICullMode::None;

    // Alpha blending: srcAlpha * src + (1-srcAlpha) * dst.
    triDesc.renderTargetBlend[0].blendEnable = true;
    triDesc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
    triDesc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
    triDesc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
    triDesc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::One;
    triDesc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::Zero;
    triDesc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;

    // No input layout — vertices via SV_VertexID + structured buffer.
    triDesc.inputElements = nullptr;
    triDesc.inputElementCount = 0;
    triDesc.vertexStride = 0;

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
        if (m_translateTriVB.IsValid())  m_device->DestroyBuffer(m_translateTriVB);
        if (m_translateTriIB.IsValid())  m_device->DestroyBuffer(m_translateTriIB);
        if (m_rotateTriVB.IsValid())     m_device->DestroyBuffer(m_rotateTriVB);
        if (m_rotateTriIB.IsValid())     m_device->DestroyBuffer(m_rotateTriIB);
        if (m_scaleTriVB.IsValid())      m_device->DestroyBuffer(m_scaleTriVB);
        if (m_scaleTriIB.IsValid())      m_device->DestroyBuffer(m_scaleTriIB);
    }

    m_translateTriVB = {};
    m_translateTriIB = {};
    m_rotateTriVB = {};
    m_rotateTriIB = {};
    m_scaleTriVB = {};
    m_scaleTriIB = {};
    m_translateTriSrv = UINT32_MAX;
    m_rotateTriSrv = UINT32_MAX;
    m_scaleTriSrv = UINT32_MAX;
    m_device = nullptr;
    m_initialized = false;
    UZ_LOG(Gizmo, Info, "Gizmo renderer shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-axis draw helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Determines the draw color and alpha for an axis based on hover/active state.
/// `isPlane` adjusts alpha values for plane handles (lower opacity).
static void ResolveAxisAppearance(int axisIndex, int hoveredAxis, int activeAxis,
                                   const float defaultColor[3], bool isPlane,
                                   float outColor[3], float& outAlpha) {
    outColor[0] = defaultColor[0];
    outColor[1] = defaultColor[1];
    outColor[2] = defaultColor[2];
    outAlpha = isPlane ? kPlaneNormalAlpha : kNormalAlpha;

    if (activeAxis >= 0) {
        // Something is being dragged.
        if (axisIndex == activeAxis) {
            // This is the active axis — yellow.
            outColor[0] = kActiveColor[0];
            outColor[1] = kActiveColor[1];
            outColor[2] = kActiveColor[2];
            outAlpha = 1.0f;
        } else {
            // Not the active axis — dim.
            outAlpha = isPlane ? kPlaneInactiveAlpha : kInactiveAlpha;
        }
    } else if (hoveredAxis >= 0 && axisIndex == hoveredAxis) {
        // Hovered, not dragging — yellow.
        outColor[0] = kHoverColor[0];
        outColor[1] = kHoverColor[1];
        outColor[2] = kHoverColor[2];
        outAlpha = 1.0f;
    }
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

    // Determine if this is a plane handle for alpha adjustment.
    const bool isPlane = (axisIndex >= 3 && axisIndex <= 5);
    ResolveAxisAppearance(axisIndex, hoveredAxis, activeAxis, axisColor, isPlane,
                          cb.axisColor, cb.alpha);

    CBAllocation alloc = cbRing.Allocate(sizeof(GizmoCB));
    std::memcpy(alloc.cpuPtr, &cb, sizeof(GizmoCB));

    cmdList->SetPipeline(m_trianglePipeline);
    cmdList->SetRootCBV(kSlotPerFrame, alloc.bufferHandle, alloc.offsetBytes, alloc.cpuPtr, sizeof(GizmoCB));
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
    // screenScale = distToCamera * tan(fov/2) * (targetPx / viewportHeight).
    // Using 720p as reference baseline.
    const float screenScale = distToCamera * tanHalfFov * (kTargetScreenPx / 720.0f);

    // ── Prepare base constant buffer ────────────────────────────────

    GizmoCB baseCB{};
    baseCB.viewProj = camera.GetViewProjection();
    baseCB.gizmoWorldPos[0] = worldPos.x;
    baseCB.gizmoWorldPos[1] = worldPos.y;
    baseCB.gizmoWorldPos[2] = worldPos.z;
    baseCB.screenScale = screenScale;
    baseCB.alpha = kNormalAlpha;
    baseCB.vertexBufferSrv = UINT32_MAX;
    baseCB._pad0 = 0;
    baseCB._pad1 = 0;
    baseCB._pad2 = 0;

    // ── Draw based on mode ──────────────────────────────────────────

    switch (mode) {
    case GizmoMode::Translate: {
        // Cylinder stems (triangles).
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_translateTriSrv, m_translateTriIB,
                                  m_translateStemRange[axis].indexOffset,
                                  m_translateStemRange[axis].indexCount,
                                  0);
        }

        // Arrow cones (triangles).
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_translateTriSrv, m_translateTriIB,
                                  m_translateConeRange[axis].indexOffset,
                                  m_translateConeRange[axis].indexCount,
                                  0);
        }

        // Plane handles (triangles). Axis encoding: 3=XY, 4=XZ, 5=YZ.
        const float planeColors[3][3] = {
            {kAxisColors[2][0], kAxisColors[2][1], kAxisColors[2][2]}, // XY plane → Z color
            {kAxisColors[1][0], kAxisColors[1][1], kAxisColors[1][2]}, // XZ plane → Y color
            {kAxisColors[0][0], kAxisColors[0][1], kAxisColors[0][2]}, // YZ plane → X color
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
        // 3 axis thick rings (triangles).
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_rotateTriSrv, m_rotateTriIB,
                                  m_rotateRingRange[axis].indexOffset,
                                  m_rotateRingRange[axis].indexCount,
                                  0);
        }
        break;
    }

    case GizmoMode::Scale: {
        // Cylinder stems.
        for (int axis = 0; axis < 3; ++axis) {
            DrawAxisSubsetIndexed(cmdList, cbRing, baseCB,
                                  axis, hoveredAxis, activeAxis,
                                  kAxisColors[axis],
                                  m_scaleTriSrv, m_scaleTriIB,
                                  m_scaleStemRange[axis].indexOffset,
                                  m_scaleStemRange[axis].indexCount,
                                  0);
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
