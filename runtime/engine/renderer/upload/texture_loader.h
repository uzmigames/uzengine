/// UzEngine Texture Loader — One-call image file to GPU texture pipeline.
///
/// Loads image files from disk (or memory) via stb_image, creates a GPU texture
/// with SRV, uploads pixel data through an UploadContext, and transitions the
/// resource to ShaderResource state. The entire upload is synchronous — the GPU
/// copy completes before the function returns.
///
/// # Supported Formats
/// JPEG, PNG, TGA, BMP, HDR (converted to LDR RGBA8).
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the thread that owns the command list.

#pragma once

#include "rhi.h"
#include "rhi_types.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoadResult — Output of a texture load operation
// ─────────────────────────────────────────────────────────────────────────────

/// Result of a texture load operation, containing the GPU handle and metadata.
///
/// Check `handle.IsValid()` to determine success. On failure, all fields
/// retain their default values (invalid handle, UINT32_MAX SRV, 0x0 dimensions).
struct TextureLoadResult {
    RHITextureHandle handle;             ///< GPU texture handle. Invalid on failure.
    uint32_t srvIndex = UINT32_MAX;      ///< Bindless SRV index in global descriptor heap. UINT32_MAX on failure.
    uint32_t width = 0;                  ///< Texture width in texels.
    uint32_t height = 0;                 ///< Texture height in texels.
    uint16_t mipLevels = 0;              ///< Number of mip levels in the texture (0 on failure).
};

/// Computes the full mip chain count for a texture of the given dimensions.
///
/// Returns floor(log2(max(width, height))) + 1, which is the number of mip
/// levels from full resolution down to 1x1.
///
/// # Arguments
/// * `width` — Texture width in texels. Must be > 0.
/// * `height` — Texture height in texels. Must be > 0.
///
/// # Returns
/// Number of mip levels (1 for 1x1, 11 for 1024x1024, 15 for 16384x16384).
[[nodiscard]] constexpr uint16_t ComputeTextureMipCount(uint32_t width, uint32_t height) noexcept {
    uint32_t maxDim = (width > height) ? width : height;
    uint16_t levels = 1;
    while (maxDim > 1) {
        maxDim >>= 1;
        ++levels;
    }
    return levels;
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader — Static utility for loading textures from disk or memory
// ─────────────────────────────────────────────────────────────────────────────

/// Loads image files into GPU textures with a single function call.
///
/// Handles the full pipeline: decode pixels (stb_image) -> create GPU texture ->
/// upload via staging buffer -> barrier to ShaderResource -> wait for GPU completion.
///
/// Both `Load` and `LoadFromMemory` are synchronous: they call `WaitIdle()` internally
/// and leave the command list in a recording state so the caller can continue issuing
/// commands immediately after.
///
/// # Usage
/// ```cpp
/// auto result = TextureLoader::Load(device, cmdList, "textures/albedo.png");
/// if (result.handle.IsValid()) {
///     // Use result.srvIndex for bindless rendering
/// }
/// ```
class TextureLoader {
public:
    /// Loads a texture from a file path on disk.
    ///
    /// Decodes the image via stb_image (forced 4-channel RGBA), creates a GPU
    /// texture with `ShaderResource | CopyDst` usage, uploads pixel data through
    /// a staging buffer, transitions to ShaderResource state, and waits for GPU
    /// completion.
    ///
    /// The command list is left in a recording state after this call — `End()`,
    /// `Execute()`, `WaitIdle()`, and `Begin()` are called internally.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must not be null.
    /// * `cmdList` — Command list in recording state. Must not be null.
    /// * `path` — File path to the image. Supports JPEG, PNG, TGA, BMP, HDR.
    ///
    /// # Returns
    /// `TextureLoadResult` with a valid handle and SRV index on success.
    /// Returns default (invalid) result on failure (file not found, decode error,
    /// GPU resource creation failure).
    ///
    /// # Errors
    /// Logs `Renderer::Error` with the file path and failure reason.
    ///
    /// # Side Effects
    /// - Creates one GPU texture resource (caller owns lifetime via handle).
    /// - Flushes the command list and waits for GPU idle.
    /// - Logs success at `Renderer::Info` level with path, dimensions, and SRV index.
    [[nodiscard]] static TextureLoadResult Load(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        const std::string& path);

    /// Loads a texture from an in-memory image buffer.
    ///
    /// Identical to `Load()` but decodes from a byte buffer instead of a file path.
    /// Useful for images embedded in container formats (glTF, archives) or downloaded
    /// from network.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must not be null.
    /// * `cmdList` — Command list in recording state. Must not be null.
    /// * `data` — Pointer to the encoded image bytes (PNG, JPEG, etc.). Must not be null.
    /// * `size` — Size of the encoded image data in bytes. Must be > 0.
    /// * `debugName` — Optional label for GPU debug tools. Defaults to "memory_texture".
    ///
    /// # Returns
    /// `TextureLoadResult` with a valid handle and SRV index on success.
    /// Returns default (invalid) result on failure.
    ///
    /// # Errors
    /// Logs `Renderer::Error` with the debug name and failure reason.
    ///
    /// # Side Effects
    /// Same as `Load()` — creates a GPU texture, flushes command list, waits for GPU idle.
    [[nodiscard]] static TextureLoadResult LoadFromMemory(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        const uint8_t* data,
        size_t size,
        const char* debugName = "memory_texture");

    /// Uploads pre-decoded RGBA8 pixels to a GPU texture with full mip chain.
    ///
    /// Creates the GPU texture, generates mips via CPU box filter, uploads all
    /// mip levels through an UploadContext, transitions to ShaderResource state,
    /// flushes the command list, and waits for GPU idle.
    ///
    /// This is the same internal pipeline used by `Load()` and `LoadFromMemory()`,
    /// exposed publicly for use with pre-decoded pixel data (e.g., from
    /// `AsyncTextureDecoder`).
    ///
    /// The command list is left in a recording state after this call.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must not be null.
    /// * `cmdList` — Command list in recording state. Must not be null.
    /// * `pixels` — RGBA8 pixel data (width * height * 4 bytes). Must not be null.
    ///   Ownership is NOT transferred — caller retains ownership and must free.
    /// * `width` — Image width in texels. Must be > 0.
    /// * `height` — Image height in texels. Must be > 0.
    /// * `debugName` — Label for GPU debug tools. May be null.
    ///
    /// # Returns
    /// `TextureLoadResult` with a valid handle and SRV index on success.
    /// Returns default (invalid) result on failure.
    [[nodiscard]] static TextureLoadResult UploadPixels(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        unsigned char* pixels,
        uint32_t width,
        uint32_t height,
        const char* debugName);

    /// Uploads pre-decoded RGBA8 pixels to a GPU texture WITHOUT flushing the
    /// command list or waiting for GPU idle.
    ///
    /// Records all upload commands (staging copies, mip generation, barrier)
    /// on the provided command list but does NOT call End/Execute/WaitIdle.
    /// The caller is responsible for flushing the command list and releasing
    /// staging buffers after all batched uploads are recorded.
    ///
    /// This is designed for batch uploading multiple textures in a single
    /// command list submission (one WaitIdle for the entire batch).
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must not be null.
    /// * `cmdList` — Command list in recording state. Must not be null.
    /// * `pixels` — RGBA8 pixel data (width * height * 4 bytes). Must not be null.
    ///   Ownership is NOT transferred.
    /// * `width` — Image width in texels. Must be > 0.
    /// * `height` — Image height in texels. Must be > 0.
    /// * `debugName` — Label for GPU debug tools. May be null.
    /// * `outUpload` — UploadContext to accumulate staging buffers. The caller
    ///   must keep this alive until GPU submission completes, then call
    ///   `ReleaseStaging()`. Must not be null.
    ///
    /// # Returns
    /// `TextureLoadResult` with a valid handle and SRV index on success.
    /// Returns default (invalid) result on failure.
    [[nodiscard]] static TextureLoadResult UploadPixelsDeferred(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        unsigned char* pixels,
        uint32_t width,
        uint32_t height,
        const char* debugName,
        class UploadContext* outUpload);

    /// Uploads pre-decoded RGBA8 pixels with a pre-computed mip chain to a GPU
    /// texture WITHOUT flushing the command list or waiting for GPU idle.
    ///
    /// Unlike `UploadPixelsDeferred`, this variant skips CPU mip generation
    /// entirely — the caller provides pre-computed mip data (e.g., from
    /// `AsyncTextureDecoder` worker threads). This eliminates redundant main-thread
    /// mip computation when the async decoder has already generated the chain.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation. Must not be null.
    /// * `cmdList` — Command list in recording state. Must not be null.
    /// * `pixels` — RGBA8 mip 0 pixel data (width * height * 4 bytes). Must not be null.
    ///   Ownership is NOT transferred.
    /// * `width` — Mip 0 width in texels. Must be > 0.
    /// * `height` — Mip 0 height in texels. Must be > 0.
    /// * `debugName` — Label for GPU debug tools. May be null.
    /// * `outUpload` — UploadContext to accumulate staging buffers. Must not be null.
    /// * `mipData` — Array of pre-computed mip pixel pointers (mip 1, mip 2, ...).
    ///   Each pointer is RGBA8 data for that mip level. Array length = mipCount - 1.
    ///   Must not be null if mipCount > 1.
    /// * `mipWidths` — Array of mip widths corresponding to mipData entries.
    /// * `mipHeights` — Array of mip heights corresponding to mipData entries.
    /// * `mipCount` — Total number of mip levels including mip 0. Must be >= 1.
    ///
    /// # Returns
    /// `TextureLoadResult` with a valid handle and SRV index on success.
    /// Returns default (invalid) result on failure.
    [[nodiscard]] static TextureLoadResult UploadPixelsWithMipsDeferred(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        unsigned char* pixels,
        uint32_t width,
        uint32_t height,
        const char* debugName,
        class UploadContext* outUpload,
        uint8_t* const* mipData,
        const uint32_t* mipWidths,
        const uint32_t* mipHeights,
        uint16_t mipCount);
};

} // namespace uz
