#include "vfx_volumetric.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {
constexpr float kFourPi = 12.566370614359172f; // 4π
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Grid coordinate mapping
// ─────────────────────────────────────────────────────────────────────────────

bool VfxVolumetricWorldToVoxel(const VfxVolumetricConfig& cfg,
                                 const Vec3& worldPos,
                                 uint32_t& outX,
                                 uint32_t& outY,
                                 uint32_t& outZ) noexcept {
    if (cfg.gridResolution == 0u || cfg.halfExtent <= 0.0f) {
        outX = outY = outZ = 0u;
        return false;
    }

    // Normalised grid coordinate in [0, 1]. The grid covers
    // [center - halfExtent, center + halfExtent] per axis.
    const float invExtent = 1.0f / (2.0f * cfg.halfExtent);
    const float nx = ((worldPos.x - cfg.gridCenter.x) + cfg.halfExtent) * invExtent;
    const float ny = ((worldPos.y - cfg.gridCenter.y) + cfg.halfExtent) * invExtent;
    const float nz = ((worldPos.z - cfg.gridCenter.z) + cfg.halfExtent) * invExtent;

    const bool inside = (nx >= 0.0f && nx <= 1.0f)
                      && (ny >= 0.0f && ny <= 1.0f)
                      && (nz >= 0.0f && nz <= 1.0f);

    const float res = static_cast<float>(cfg.gridResolution);
    const float lastIdx = res - 1.0f;
    // floor(n * res) clamped so exact 1.0 still lands in-bounds.
    const float fx = std::clamp(std::floor(nx * res), 0.0f, lastIdx);
    const float fy = std::clamp(std::floor(ny * res), 0.0f, lastIdx);
    const float fz = std::clamp(std::floor(nz * res), 0.0f, lastIdx);

    outX = static_cast<uint32_t>(fx);
    outY = static_cast<uint32_t>(fy);
    outZ = static_cast<uint32_t>(fz);
    return inside;
}

Vec3 VfxVolumetricVoxelToWorld(const VfxVolumetricConfig& cfg,
                                 uint32_t ix, uint32_t iy, uint32_t iz) noexcept {
    if (cfg.gridResolution == 0u) return cfg.gridCenter;
    const float res = static_cast<float>(cfg.gridResolution);
    const float voxelSize = (2.0f * cfg.halfExtent) / res;
    // Center of voxel = min corner + (index + 0.5) * voxelSize.
    const Vec3 minCorner{cfg.gridCenter.x - cfg.halfExtent,
                         cfg.gridCenter.y - cfg.halfExtent,
                         cfg.gridCenter.z - cfg.halfExtent};
    return Vec3{
        minCorner.x + (static_cast<float>(ix) + 0.5f) * voxelSize,
        minCorner.y + (static_cast<float>(iy) + 0.5f) * voxelSize,
        minCorner.z + (static_cast<float>(iz) + 0.5f) * voxelSize,
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Henyey–Greenstein phase function
// ─────────────────────────────────────────────────────────────────────────────

float VfxHenyeyGreenstein(float cosTheta, float g) noexcept {
    // Clamp anisotropy away from the poles to avoid division by zero.
    g = std::clamp(g, -0.9999f, 0.9999f);
    const float g2 = g * g;
    const float numerator   = 1.0f - g2;
    const float denomBase   = 1.0f + g2 - 2.0f * g * cosTheta;
    // Guard against tiny negative values from fp noise near the pole.
    const float denom       = std::max(1e-6f, denomBase);
    return numerator / (kFourPi * std::pow(denom, 1.5f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Beer–Lambert transmittance
// ─────────────────────────────────────────────────────────────────────────────

float VfxBeerLambertTransmittance(float extinctionCoeff,
                                    float density,
                                    float stepDistance) noexcept {
    const float tau = std::max(0.0f, extinctionCoeff) *
                      std::max(0.0f, density) *
                      std::max(0.0f, stepDistance);
    return std::exp(-tau);
}

// ─────────────────────────────────────────────────────────────────────────────
// Single raymarch step contribution
// ─────────────────────────────────────────────────────────────────────────────

Vec3 VfxVolumetricStepContribution(const VfxVolumetricConfig& cfg,
                                     float density,
                                     float cosTheta,
                                     float stepTransmittance,
                                     float accumulatedTransmittance) noexcept {
    const float phase  = VfxHenyeyGreenstein(cosTheta, cfg.hgAnisotropy);
    const float absorp = 1.0f - std::clamp(stepTransmittance, 0.0f, 1.0f);
    const float scale  = phase * density * absorp * accumulatedTransmittance;
    return Vec3{
        cfg.scatteringColor.x * scale,
        cfg.scatteringColor.y * scale,
        cfg.scatteringColor.z * scale,
    };
}

} // namespace uz
