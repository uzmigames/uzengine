/// UzEngine Texture Loader — Implementation.
///
/// Decodes images via stb_image and uploads to GPU via UploadContext.

#include "texture_loader.h"
#include "upload_context.h"
#include "log.h"

#include <stb_image.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::Load
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::Load(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const std::string& path)
{
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: null device or cmdList");
        return {};
    }

    int w = 0;
    int h = 0;
    int channels = 0;

    // Force 4-channel RGBA output regardless of source format
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: failed to decode image: path='{}', reason='{}'",
               path, stbi_failure_reason());
        return {};
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::Load: invalid dimensions: path='{}', {}x{}",
               path, w, h);
        stbi_image_free(pixels);
        return {};
    }

    auto result = UploadPixels(device, cmdList, pixels,
                               static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h),
                               path.c_str());

    stbi_image_free(pixels);

    if (result.handle.IsValid()) {
        UZ_LOG(Renderer, Info, "Loaded texture: path='{}', {}x{}, SRV={}",
               path, result.width, result.height, result.srvIndex);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadFromMemory
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::LoadFromMemory(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    const uint8_t* data,
    size_t size,
    const char* debugName)
{
    if (!device || !cmdList) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: null device or cmdList");
        return {};
    }

    if (!data || size == 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: null or empty data buffer, name='{}'",
               debugName ? debugName : "unknown");
        return {};
    }

    int w = 0;
    int h = 0;
    int channels = 0;

    // stbi expects int length; clamp to INT_MAX for safety
    const int len = (size > static_cast<size_t>(INT32_MAX))
                        ? INT32_MAX
                        : static_cast<int>(size);

    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!pixels) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: failed to decode image: name='{}', reason='{}'",
               debugName ? debugName : "unknown", stbi_failure_reason());
        return {};
    }

    if (w <= 0 || h <= 0) {
        UZ_LOG(Renderer, Error, "TextureLoader::LoadFromMemory: invalid dimensions: name='{}', {}x{}",
               debugName ? debugName : "unknown", w, h);
        stbi_image_free(pixels);
        return {};
    }

    const char* name = debugName ? debugName : "memory_texture";
    auto result = UploadPixels(device, cmdList, pixels,
                               static_cast<uint32_t>(w),
                               static_cast<uint32_t>(h),
                               name);

    stbi_image_free(pixels);

    if (result.handle.IsValid()) {
        UZ_LOG(Renderer, Info, "Loaded texture: path='{}', {}x{}, SRV={}",
               name, result.width, result.height, result.srvIndex);
    }

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::UploadPixels — shared GPU upload logic
// ─────────────────────────────────────────────────────────────────────────────

TextureLoadResult TextureLoader::UploadPixels(
    IRHIDevice* device,
    IRHICommandList* cmdList,
    unsigned char* pixels,
    uint32_t width,
    uint32_t height,
    const char* debugName)
{
    // Create GPU texture with ShaderResource + CopyDst usage
    RHITextureDesc desc{};
    desc.width = width;
    desc.height = height;
    desc.format = RHIFormat::RGBA8Unorm;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.debugName = debugName;

    RHITextureHandle texture = device->CreateTexture(desc);
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error, "TextureLoader: failed to create GPU texture: name='{}', {}x{}",
               debugName ? debugName : "unknown", width, height);
        return {};
    }

    // Upload pixel data via staging buffer
    const uint64_t dataSize = static_cast<uint64_t>(width) * height * 4;

    UploadContext upload(device);
    if (!upload.UploadTexture(cmdList, texture, pixels, dataSize)) {
        UZ_LOG(Renderer, Error, "TextureLoader: staging upload failed: name='{}', {}x{}",
               debugName ? debugName : "unknown", width, height);
        device->DestroyTexture(texture);
        return {};
    }

    // Transition from CopyDst to ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture = texture;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    // Flush the command list and wait for GPU completion
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    // Safe to release staging buffers now that GPU is idle
    upload.ReleaseStaging();

    // Reset command list to recording state for caller
    cmdList->Begin();

    // Build result
    TextureLoadResult result{};
    result.handle = texture;
    result.srvIndex = device->GetTextureSRVIndex(texture);
    result.width = width;
    result.height = height;

    return result;
}

} // namespace uz
