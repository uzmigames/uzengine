#pragma once

// texture.h -- GPU texture loading and management.
//
// Loads image data via stb_image and uploads to RHI textures.
//
// RULE RHI-1: No backend types above rhi.h

#include <cstddef>
#include <cstdint>

#include "engine/renderer/rhi/rhi.h"

namespace uz {

// ---------------------------------------------------------------------------
// GpuTexture — a loaded texture with its RHI handle and metadata.
// ---------------------------------------------------------------------------
struct GpuTexture {
    RhiTexture handle = RHI_NULL;
    uint32_t   width  = 0;
    uint32_t   height = 0;
    RhiFormat  format = RHI_FORMAT_RGBA8_UNORM;
};

/// Load a texture from raw file data in memory (PNG, JPG, BMP, etc.).
/// Returns a GpuTexture with valid handle on success.
[[nodiscard]] GpuTexture load_texture_from_memory(const uint8_t* data,
                                                   size_t size,
                                                   const char* debug_name);

/// Load a texture from a file path on disk.
[[nodiscard]] GpuTexture load_texture_from_file(const char* path,
                                                 const char* debug_name);

/// Destroy a GPU texture. Resets handle to RHI_NULL.
void destroy_texture(GpuTexture& tex);

} // namespace uz
