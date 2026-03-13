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
};

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

private:
    /// Internal implementation shared by Load and LoadFromMemory.
    /// Takes already-decoded RGBA pixels and handles GPU upload.
    static TextureLoadResult UploadPixels(
        IRHIDevice* device,
        IRHICommandList* cmdList,
        unsigned char* pixels,
        uint32_t width,
        uint32_t height,
        const char* debugName);
};

} // namespace uz
