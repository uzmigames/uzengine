/// UzEngine Texture Importer
///
/// Converts common image formats (PNG, JPEG, TGA) into .uztexture binary
/// format with a full mipmap chain. Uses stb_image for decoding and a
/// box filter for mip generation. Optionally compresses each mip level
/// to a GPU block-compressed format (BC1/BC3/BC4/BC5/BC7) via etcpak.
///
/// # Supported Extensions
/// `.png`, `.jpg`, `.jpeg`, `.tga`
///
/// # Import Pipeline
/// 1. Decode source bytes with stb_image (force RGBA 4-channel output)
/// 2. Generate mipmap chain via 2x2 box filter downsampling
/// 3. If `options.textureCompression` != `Uncompressed_RGBA8`, compress
///    each mip level to the requested BC format using etcpak.
/// 4. If `options.textureSRGB` is true, set the sRGB flag in the header.
/// 5. Write via WriteUzTexture
///
/// # Options
/// - `textureCompression` — Target `TextureFormat` (default: `Uncompressed_RGBA8`).
/// - `textureSRGB` — If `true`, marks the .uztexture header with the sRGB flag.
///
/// # Platform
/// Cross-platform — stb_image and etcpak have no platform dependencies.

#pragma once

#include "asset_importer.h"

#include <string>
#include <vector>

namespace uz {

/// Imports raster images into .uztexture binary format.
///
/// Decodes the source image, generates a complete mipmap chain using a
/// box filter, optionally compresses each mip to a BC format via etcpak,
/// and writes the result as a .uztexture file.
class TextureImporter final : public IAssetImporter {
public:
    TextureImporter();

    /// Returns `"texture"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `2`).
    ///
    /// Bump when import logic or .uztexture output format changes.
    /// v2: added BC1/BC3/BC4/BC5/BC7 GPU texture compression via etcpak.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".png", ".jpg", ".jpeg", ".tga"}` — supported extensions.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Imports an image file into .uztexture binary format.
    ///
    /// # Arguments
    /// * `sourceData` - Raw bytes of the image file.
    /// * `sourceName` - Filename for error messages (e.g., "diffuse.png").
    /// * `options` - `textureCompression` selects the output pixel format;
    ///   `textureSRGB` sets the sRGB flag in the .uztexture header.
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uztexture binary with
    /// full mipmap chain (optionally block-compressed). On failure:
    /// `result.error` describes the issue (decode failure, zero-size image, etc.).
    ///
    /// # Errors
    /// Logs `Asset::Error` with the source name and stb_image error on failure.
    /// Logs `Asset::Info` with dimensions, mip count, and format on success.
    [[nodiscard]] AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;

private:
    std::vector<std::string> m_extensions;
};

} // namespace uz
