// sky_cubemap.h -- Procedural sky cubemap generation for IBL pipeline.
//
// Generates a hemisphere sky gradient cubemap and convolves it into
// irradiance and prefiltered environment maps for split-sum IBL.
//
// All work is CPU-side; textures are uploaded via rhi()->texture_upload().
// Call once at startup and whenever sky parameters change.

#pragma once

#include "engine/renderer/rhi/rhi.h"

namespace uz {

struct SkyCubemapParams {
    float sky_zenith[3];   // deep blue color at top of sky dome
    float sky_horizon[3];  // pale blue color at horizon
    float sky_ground[3];   // warm earth color below horizon
    float sun_dir[3];      // normalized sun direction vector
    float sun_intensity;   // sun disk intensity multiplier
};

struct SkyCubemapResult {
    RhiTexture env_cubemap;       // 256x256 RGBA16F source cubemap
    RhiTexture irradiance_cube;   // 32x32 RGBA16F diffuse irradiance
    RhiTexture prefiltered_cube;  // 128x128 RGBA16F specular prefiltered (5 mip levels)
};

// Generate ONLY the source env cubemap (fast — ~50ms).
// irradiance_cube and prefiltered_cube are RHI_NULL.
// Use IblBaker::bake() for GPU-accelerated convolution (~1ms).
SkyCubemapResult generate_sky_cubemaps(const SkyCubemapParams& params);

// Full CPU fallback: env cubemap + CPU-convolved irradiance + prefiltered (~1300ms).
// Only use when GPU IBL bake is unavailable.
SkyCubemapResult generate_sky_cubemaps_full(const SkyCubemapParams& params);

} // namespace uz
