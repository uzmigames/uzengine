// ---------------------------------------------------------------------------
// texture.cpp -- GPU texture loading via stb_image + RHI
//
// RULE RHI-1: No backend types above rhi.h
// RULE CS-7:  No exceptions
// ---------------------------------------------------------------------------

#include "texture.h"

#include <stb_image.h>

#include "engine/core/diag.h"
#include "engine/core/platform.h"

namespace uz {

GpuTexture load_texture_from_memory(const uint8_t* data,
                                     size_t size,
                                     const char* debug_name) {
    GpuTexture tex;

    if (!data || size == 0) {
        DIAG("load_texture_from_memory: null data");
        return tex;
    }

    // Decode image to RGBA using stb_image.
    int w = 0, h = 0, channels = 0;
    uint8_t* pixels = stbi_load_from_memory(
        data, static_cast<int>(size), &w, &h, &channels, 4 /* RGBA */);

    if (!pixels) {
        DIAG_FMT("load_texture_from_memory: stbi decode failed: %s",
                 stbi_failure_reason());
        return tex;
    }

    // Create RHI texture.
    RhiTextureDesc desc{};
    desc.width      = static_cast<uint32_t>(w);
    desc.height     = static_cast<uint32_t>(h);
    desc.depth      = 1;
    desc.mip_levels = 1;
    desc.array_size = 1;
    desc.format     = RHI_FORMAT_RGBA8_UNORM;
    desc.dimension  = RHI_TEXTURE_DIM_2D;
    desc.usage      = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;
    desc.debug_name = debug_name;

    tex.handle = rhi()->texture_create(&desc);
    if (tex.handle == RHI_NULL) {
        DIAG("load_texture_from_memory: texture_create failed");
        stbi_image_free(pixels);
        return tex;
    }

    // Upload pixel data.
    const size_t pixel_size = static_cast<size_t>(w) * h * 4;
    rhi()->texture_upload(tex.handle, pixels, pixel_size);

    tex.width  = static_cast<uint32_t>(w);
    tex.height = static_cast<uint32_t>(h);
    tex.format = RHI_FORMAT_RGBA8_UNORM;

    stbi_image_free(pixels);

    DIAG_FMT("load_texture: %dx%d '%s'", w, h, debug_name ? debug_name : "");
    return tex;
}

GpuTexture load_texture_from_file(const char* path, const char* debug_name) {
    GpuTexture tex;

    if (!path) {
        DIAG("load_texture_from_file: null path");
        return tex;
    }

    // Read file into memory.
    size_t file_size = 0;
    char* file_data = platform::file_read_all(path, &file_size);
    if (!file_data || file_size == 0) {
        DIAG_FMT("load_texture_from_file: cannot read '%s'", path);
        return tex;
    }

    tex = load_texture_from_memory(
        reinterpret_cast<const uint8_t*>(file_data),
        file_size,
        debug_name ? debug_name : path);

    free(file_data);
    return tex;
}

void destroy_texture(GpuTexture& tex) {
    if (tex.handle != RHI_NULL) {
        rhi()->texture_destroy(tex.handle);
        tex.handle = RHI_NULL;
    }
    tex.width  = 0;
    tex.height = 0;
}

} // namespace uz
