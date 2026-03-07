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

// Generate all IBL cubemaps from procedural sky parameters.
// Creates three cubemap textures:
//   1) Source environment cubemap (256x256 per face, 1 mip)
//   2) Irradiance cubemap (32x32 per face, 1 mip) -- diffuse hemisphere integral
//   3) Prefiltered environment cubemap (128x128 per face, 5 mips) -- specular split-sum
//
// NOTE: The current RHI texture_upload() only writes subresource 0 (face 0, mip 0).
// Full cubemap upload requires RHI extension (texture_upload_subresource).
// Until then, the generated CPU pixel data is correct but only face 0 is GPU-resident.
SkyCubemapResult generate_sky_cubemaps(const SkyCubemapParams& params);

} // namespace uz
