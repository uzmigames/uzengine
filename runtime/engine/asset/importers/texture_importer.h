/// UzEngine Texture Importer
///
/// Converts common image formats (PNG, JPEG, TGA) into .uztexture binary
/// format with a full mipmap chain. Uses stb_image for decoding and a
/// box filter for mip generation.
///
/// # Supported Extensions
/// `.png`, `.jpg`, `.jpeg`, `.tga`
///
/// # Import Pipeline
/// 1. Decode source bytes with stb_image (force RGBA 4-channel output)
/// 2. Generate mipmap chain via 2x2 box filter downsampling
/// 3. Write via WriteUzTexture as Uncompressed_RGBA8
///
/// # Options
/// No importer-specific options. All fields in `AssetImportOptions` are ignored.
///
/// # Platform
/// Cross-platform — stb_image has no platform dependencies.

#pragma once

#include "asset_importer.h"

#include <string>
#include <vector>

namespace uz {

/// Imports raster images into .uztexture binary format.
///
/// Decodes the source image, generates a complete mipmap chain using a
/// box filter, and writes the result as an Uncompressed_RGBA8 .uztexture file.
class TextureImporter final : public IAssetImporter {
public:
    TextureImporter();

    /// Returns `"texture"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `1`).
    ///
    /// Bump when import logic or .uztexture output format changes.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".png", ".jpg", ".jpeg", ".tga"}` — supported extensions.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Imports an image file into .uztexture binary format.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the image file.
    /// * `sourceName` - Filename for error messages (e.g., "diffuse.png").
    /// * `options` - Ignored — texture import has no configurable options.
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uztexture binary with
    /// full mipmap chain. On failure: `result.error` describes the issue
    /// (decode failure, zero-size image, etc.).
    ///
    /// # Errors
    /// Logs `Asset::Error` with the source name and stb_image error on failure.
    /// Logs `Asset::Info` with dimensions and mip count on success.
    [[nodiscard]] AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;

private:
    std::vector<std::string> m_extensions;
};

} // namespace uz
