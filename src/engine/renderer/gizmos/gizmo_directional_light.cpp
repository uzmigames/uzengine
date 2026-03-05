// gizmo_directional_light.cpp — Directional light gizmo.
//
// 3 parallel rays with arrowheads pointing DOWN (-Y), Unreal-style.
// Sun symbol sits above the rays. Lines are tripled with slight offsets
// to simulate thicker rendering on D3D12 (which lacks line-width control).

#include "gizmo_directional_light.h"
#include "gizmo_utils.h"
#include "engine/renderer/renderer.h"


namespace uz {

namespace {

// Helper: push a line segment with 3 parallel copies (offsets in X and Z)
// to simulate thicker lines.
inline void pushThickLine(Vertex* verts, uint32_t& idx,
                          Float3 a, Float3 b, const Float3& col) {
    constexpr float kT = 0.008f; // offset for thickness

    // Center line.
    verts[idx++] = {a, col};
    verts[idx++] = {b, col};
    // Offset +X.
    verts[idx++] = {{a.x + kT, a.y, a.z}, col};
    verts[idx++] = {{b.x + kT, b.y, b.z}, col};
    // Offset +Z.
    verts[idx++] = {{a.x, a.y, a.z + kT}, col};
    verts[idx++] = {{b.x, b.y, b.z + kT}, col};
}

} // anonymous namespace

bool GizmoDirectionalLight::init() {
    const Float3 col = {1.0f, 1.0f, 1.0f};

    // 3 rays × 5 line-segments × 3 copies + sun symbol (8 lines × 3 copies)
    // = 45 + 24 = 69 line-segments × 2 verts = 414 max (use 512 for safety).
    Vertex verts[512];
    uint32_t idx = 0;

    // ── Sun symbol at top (Y = +0.55, above the rays) ──────────────────
    const float sunY  = 0.55f;
    const float sr    = 0.15f;  // radius of sun cross
    const float sd    = sr * 0.707f;

    // Cross (+X, -X, +Z, -Z).
    pushThickLine(verts, idx, {-sr, sunY, 0}, { sr, sunY, 0}, col);
    pushThickLine(verts, idx, {0, sunY, -sr}, {0, sunY,  sr}, col);
    // Diagonals.
    pushThickLine(verts, idx, {-sd, sunY, -sd}, { sd, sunY,  sd}, col);
    pushThickLine(verts, idx, {-sd, sunY,  sd}, { sd, sunY, -sd}, col);

    // ── 3 rays pointing downward (-Y) in a triangle pattern ────────────
    const float offsets[][2] = { {0.0f, 0.17f}, {-0.15f, -0.085f}, {0.15f, -0.085f} };
    const float rayTopY    = 0.35f;    // start just below the sun
    const float rayBottomY = -0.45f;   // end well below origin
    const float arrowLen   = 0.15f;
    const float arrowW     = 0.07f;

    for (int r = 0; r < 3; ++r) {
        const float ox = offsets[r][0];  // X offset
        const float oz = offsets[r][1];  // Z offset

        // Main ray (top to bottom, along -Y).
        pushThickLine(verts, idx,
                      {ox, rayTopY,    oz},
                      {ox, rayBottomY, oz}, col);

        // 4-prong arrowhead at the bottom.
        pushThickLine(verts, idx,
                      {ox,          rayBottomY, oz},
                      {ox - arrowW, rayBottomY + arrowLen, oz}, col);
        pushThickLine(verts, idx,
                      {ox,          rayBottomY, oz},
                      {ox + arrowW, rayBottomY + arrowLen, oz}, col);
        pushThickLine(verts, idx,
                      {ox, rayBottomY, oz},
                      {ox, rayBottomY + arrowLen, oz - arrowW}, col);
        pushThickLine(verts, idx,
                      {ox, rayBottomY, oz},
                      {ox, rayBottomY + arrowLen, oz + arrowW}, col);
    }

    vertCount_ = idx;
    vb_ = gizmo_upload_vb(verts, idx, "GizmoDirectionalLight VB");
    return vb_ != RHI_NULL;
}

void GizmoDirectionalLight::draw(RhiCmdList cmd) {
    rhi()->cmd_set_vertex_buffer(cmd, 0, vb_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, vertCount_, 1, 0, 0);
}

void GizmoDirectionalLight::shutdown() {
    if (vb_ != RHI_NULL) { rhi()->buffer_destroy(vb_); vb_ = RHI_NULL; }
}

} // namespace uz
