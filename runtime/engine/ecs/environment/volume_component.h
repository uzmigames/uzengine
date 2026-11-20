/// UzEngine VolumeComponent — a heterogeneous participating medium for the DXR path tracer.
///
/// An oriented box (the entity's transform times `halfExtents`) filled with a scalar density
/// grid (`VolumeAsset`, see engine/asset/formats/volume_asset.h). Absorption and scattering
/// both scale with the sampled density, so the medium keeps one scattering albedo:
/// ```
/// sigma_a(x) = sigmaA * density(x)      sigma_s(x) = sigmaS * density(x)
/// L_e(x)     = sigma_a(x) * emission    phase      = Henyey-Greenstein(phaseG)
/// ```
/// Units: distances in cm (engine units), coefficients per cm at unit density. The
/// defaults keep the Novak 2018 Table 1 smoke albedo (0.9) at a 1 m mean free path.
///
/// Rendered only by the DXR path tracer (`--path-trace --volume` / `r.PathTracer.Volume=1`):
/// Woodcock delta tracking for free flights, ratio tracking for every shadow transmittance,
/// unbiased single and multiple scattering (pathtracer_volume.hlsli). Up to
/// `kMaxPathVolumes` enabled components are gathered per frame; the raster path and the
/// froxel VolumetricFog ignore this component.
///
/// @ref Novak et al. 2018 "Monte Carlo Methods for Volumetric Light Transport Simulation"

#pragma once

#include "vector3.h"

#include <string>

namespace uz {

static constexpr uint32_t kMaxPathVolumes = 4;

struct VolumeComponent {
    /// Density grid: `builtin:sphere`, `builtin:cloud`, `builtin:constant`,
    /// `builtin:ramp`, or a project-relative `.uzvol` path (VolumeAsset::Resolve).
    std::string densityGrid = "builtin:sphere";

    /// Local half size of the box in cm; the entity's scale multiplies it.
    Vec3 halfExtents = {100.0f, 100.0f, 100.0f};

    /// Absorption coefficient per cm at unit density.
    float sigmaA = 0.001f;

    /// Scattering coefficient per cm at unit density.
    float sigmaS = 0.009f;

    /// Henyey-Greenstein asymmetry in [-0.99, 0.99]; 0 = isotropic.
    float phaseG = 0.0f;

    /// Emitted radiance per unit absorption (`sigma_a(x) * emission`). Default black.
    Vec3 emission = {0.0f, 0.0f, 0.0f};

    /// Multiplier on the sampled density (and on the Woodcock majorant).
    float densityScale = 1.0f;

    /// Whether this medium is gathered.
    bool enabled = true;
};

} // namespace uz
