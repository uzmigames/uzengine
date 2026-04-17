#include "vfx_advection_field_di.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────

bool VfxAdvectionFieldConfig::IsEnabled() const noexcept {
    if (fieldSrvIndex == 0xFFFFFFFFu) return false;
    // Non-degenerate AABB: any axis with min >= max is invalid.
    if (worldMax.x <= worldMin.x) return false;
    if (worldMax.y <= worldMin.y) return false;
    if (worldMax.z <= worldMin.z) return false;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// World ↔ field mapping
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxAdvectionWorldToField(const VfxAdvectionFieldConfig& cfg,
                                const Vec3& worldPos) noexcept {
    const float dx = cfg.worldMax.x - cfg.worldMin.x;
    const float dy = cfg.worldMax.y - cfg.worldMin.y;
    const float dz = cfg.worldMax.z - cfg.worldMin.z;
    // Degenerate AABB — any axis 0 produces UV coords outside [0,1], which
    // the inside test rejects.
    const float invX = (dx > 1e-6f) ? 1.0f / dx : 0.0f;
    const float invY = (dy > 1e-6f) ? 1.0f / dy : 0.0f;
    const float invZ = (dz > 1e-6f) ? 1.0f / dz : 0.0f;
    return Vec3{
        (worldPos.x - cfg.worldMin.x) * invX,
        (worldPos.y - cfg.worldMin.y) * invY,
        (worldPos.z - cfg.worldMin.z) * invZ,
    };
}

bool VfxAdvectionIsInside(const Vec3& uvw) noexcept {
    return uvw.x >= 0.0f && uvw.x <= 1.0f &&
           uvw.y >= 0.0f && uvw.y <= 1.0f &&
           uvw.z >= 0.0f && uvw.z <= 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tightness blend
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxAdvectionBlendVelocity(const Vec3& currentVel, const Vec3& advectedVel,
                                  bool insideField, float tightness) noexcept {
    if (!insideField) return currentVel;
    const float t = std::clamp(tightness, 0.0f, 1.0f);
    return Vec3{
        currentVel.x + (advectedVel.x - currentVel.x) * t,
        currentVel.y + (advectedVel.y - currentVel.y) * t,
        currentVel.z + (advectedVel.z - currentVel.z) * t,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet
// ─────────────────────────────────────────────────────────────────────────────

const char* VfxAdvectionEmitHLSLSnippet() noexcept {
    // Emitted once per emitter permutation that uses advection. The CB
    // fields (fieldSrvIdx, worldMin, worldMax, tightness) are declared
    // elsewhere in the composed shader header — this snippet only adds
    // the sampler function + the blend call at the dispatch site.
    static const char* kSnippet =
        "float3 SampleAdvection(float3 worldPos) {\n"
        "    if (advectionFieldSrvIdx == 0xFFFFFFFFu) return float3(0, 0, 0);\n"
        "    const float3 extent = advectionWorldMax - advectionWorldMin;\n"
        "    const float3 uvw = (worldPos - advectionWorldMin) / max(extent, float3(1e-6, 1e-6, 1e-6));\n"
        "    if (any(uvw < 0.0) || any(uvw > 1.0)) return float3(0, 0, 0);\n"
        "    Texture3D<float3> field = ResourceDescriptorHeap[advectionFieldSrvIdx];\n"
        "    return field.SampleLevel(g_trilinearClamp, uvw, 0);\n"
        "}\n"
        "\n"
        "// Update-CS dispatch site:\n"
        "//   float3 advected = SampleAdvection(particle.position);\n"
        "//   float  t        = clamp(advectionTightness, 0.0, 1.0);\n"
        "//   particle.velocity = lerp(particle.velocity, advected, t);\n";
    return kSnippet;
}

} // namespace uz
