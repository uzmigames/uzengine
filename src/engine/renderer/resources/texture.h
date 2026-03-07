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

// ---------------------------------------------------------------------------
// DecodedTexture — CPU-decoded image data (for deferred/batched GPU upload).
// ---------------------------------------------------------------------------
struct DecodedTexture {
    uint8_t*  pixels = nullptr;   // RGBA8 pixel data (stbi-allocated)
    uint32_t  width  = 0;
    uint32_t  height = 0;
    size_t    pixel_size = 0;     // width * height * 4
};

/// Decode an image file to CPU memory. Does NOT upload to GPU.
/// Caller must free pixels with free_decoded_texture().
[[nodiscard]] DecodedTexture decode_texture_from_file(const char* path);

/// Free CPU pixel data from a DecodedTexture.
void free_decoded_texture(DecodedTexture& decoded);

/// Free raw pixel data (stbi-allocated). Used by AsyncTextureLoader.
void free_decoded_texture_pixels(uint8_t* pixels);

} // namespace uz
