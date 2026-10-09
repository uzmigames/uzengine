#include "vfx_scene_sdf_di.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Atlas descriptor
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSceneSDFAtlasDesc::IsBound() const noexcept {
    if (atlasSrvIndex == 0xFFFFFFFFu) return false;
    if (worldMax.x <= worldMin.x || worldMax.y <= worldMin.y || worldMax.z <= worldMin.z)
        return false;
    return resX > 0u && resY > 0u && resZ > 0u;
}

Vec3 VfxSceneSDFVoxelSize(const VfxSceneSDFAtlasDesc& desc) noexcept {
    const float dx = desc.worldMax.x - desc.worldMin.x;
    const float dy = desc.worldMax.y - desc.worldMin.y;
    const float dz = desc.worldMax.z - desc.worldMin.z;
    return Vec3{
        (desc.resX > 0u) ? dx / static_cast<float>(desc.resX) : 0.0f,
        (desc.resY > 0u) ? dy / static_cast<float>(desc.resY) : 0.0f,
        (desc.resZ > 0u) ? dz / static_cast<float>(desc.resZ) : 0.0f,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Analytic SDF primitives
// ─────────────────────────────────────────────────────────────────────────────

float VfxSDFSphere(const Vec3& p, const Vec3& center, float radius) noexcept {
    const float dx = p.x - center.x;
    const float dy = p.y - center.y;
    const float dz = p.z - center.z;
    return std::sqrt(dx*dx + dy*dy + dz*dz) - radius;
}

float VfxSDFBox(const Vec3& p, const Vec3& center, const Vec3& halfExtents) noexcept {
    // Standard Inigo Quilez sdBox:
    //   q = abs(p - center) - halfExtents
    //   outside = length(max(q, 0))
    //   inside  = min(max(q.x, max(q.y, q.z)), 0)
    const float qx = std::fabs(p.x - center.x) - halfExtents.x;
    const float qy = std::fabs(p.y - center.y) - halfExtents.y;
    const float qz = std::fabs(p.z - center.z) - halfExtents.z;
    const float mx = std::max(qx, 0.0f);
    const float my = std::max(qy, 0.0f);
    const float mz = std::max(qz, 0.0f);
    const float outside = std::sqrt(mx*mx + my*my + mz*mz);
    const float inside  = std::min(std::max(qx, std::max(qy, qz)), 0.0f);
    return outside + inside;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision response
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSceneSDFResolveCollision(Vec3& position, Vec3& velocity,
                                    float distance, const Vec3& gradient,
                                    float radius, float restitution) noexcept {
    if (distance >= radius) return false;

    // Normalise gradient. Fall back to world-up when gradient is
    // degenerate (e.g. sampling inside a perfectly flat region).
    const float gLen = std::sqrt(gradient.x * gradient.x +
                                 gradient.y * gradient.y +
                                 gradient.z * gradient.z);
    Vec3 n;
    if (gLen < 1e-6f) {
        n = Vec3{0.0f, 0.0f, 1.0f};
    } else {
        n = Vec3{gradient.x / gLen, gradient.y / gLen, gradient.z / gLen};
    }

    // Push out along the surface normal.
    const float pushOut = radius - distance;
    position.x += n.x * pushOut;
    position.y += n.y * pushOut;
    position.z += n.z * pushOut;

    // Reflect velocity. Standard vMirror = v - 2·(v·n)·n, scaled by
    // restitution: vOut = v - (1 + e)·(v·n)·n. e=0 absorbs (slide along),
    // e=1 perfect reflection.
    const float r     = std::clamp(restitution, 0.0f, 1.0f);
    const float vDotN = velocity.x * n.x + velocity.y * n.y + velocity.z * n.z;
    if (vDotN < 0.0f) {
        const float k = (1.0f + r) * vDotN;
        velocity.x -= k * n.x;
        velocity.y -= k * n.y;
        velocity.z -= k * n.z;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// HLSL snippet
// ─────────────────────────────────────────────────────────────────────────────

const char* VfxSceneSDFEmitHLSLSnippet() noexcept {
    static const char* kSnippet =
        "float SampleSceneSDF(float3 worldPos) {\n"
        "    if (sdfAtlasSrvIdx == 0xFFFFFFFFu) return 1e9;\n"
        "    const float3 extent = sdfWorldMax - sdfWorldMin;\n"
        "    const float3 uvw    = (worldPos - sdfWorldMin) / max(extent, float3(1e-6, 1e-6, 1e-6));\n"
        "    if (any(uvw < 0.0) || any(uvw > 1.0)) return 1e9;\n"
        "    Texture3D<float> atlas = ResourceDescriptorHeap[sdfAtlasSrvIdx];\n"
        "    return atlas.SampleLevel(g_trilinearClamp, uvw, 0);\n"
        "}\n"
        "\n"
        "float3 GradientSceneSDF(float3 worldPos, float eps) {\n"
        "    const float dx = SampleSceneSDF(worldPos + float3(eps, 0, 0)) -\n"
        "                     SampleSceneSDF(worldPos - float3(eps, 0, 0));\n"
        "    const float dy = SampleSceneSDF(worldPos + float3(0, eps, 0)) -\n"
        "                     SampleSceneSDF(worldPos - float3(0, eps, 0));\n"
        "    const float dz = SampleSceneSDF(worldPos + float3(0, 0, eps)) -\n"
        "                     SampleSceneSDF(worldPos - float3(0, 0, eps));\n"
        "    return float3(dx, dy, dz) / (2.0f * eps);\n"
        "}\n";
    return kSnippet;
}

} // namespace uz
