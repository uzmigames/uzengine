/// UzEngine PNG Codec — libpng wrapper for reading and writing PNG files.
///
/// Provides 8-bit and 16-bit PNG decode/encode with proper gamma handling,
/// sRGB chunk detection, and channel expansion. Used by TextureImporter for
/// asset pipeline operations and by screenshot export.
///
/// # Why libpng over stb_image?
/// - 16-bit per channel support (heightmaps, HDR data)
/// - Robust metadata (sRGB, gamma, ICC profile detection)
/// - Fine-grained transform control (palette expansion, tRNS, strip/expand)
///
/// # Error handling
/// libpng uses setjmp/longjmp for error recovery. Each public function
/// sets up a jmp_buf via png_jmpbuf() and returns a failure sentinel
/// (empty PngImage or false/empty vector) on error. All resources are
/// cleaned up before the longjmp return path.
///
/// @ref libpng 1.6 — http://www.libpng.org/pub/png/libpng-manual.txt

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Decoded PNG image data.
///
/// After successful decode, `pixels` contains tightly-packed pixel data
/// in row-major order (top-to-bottom, left-to-right). For 8-bit images
/// each sample is one byte; for 16-bit images each sample is two bytes
/// in native (little-endian on x86) byte order.
struct PngImage {
    std::vector<uint8_t> pixels;  ///< Raw pixel data (8-bit or 16-bit per channel)
    uint32_t width    = 0;        ///< Image width in pixels
    uint32_t height   = 0;        ///< Image height in pixels
    uint32_t channels = 0;        ///< Channel count: 1=Gray, 2=GrayAlpha, 3=RGB, 4=RGBA
    uint32_t bitDepth = 8;        ///< Bits per channel: 8 or 16
    bool     srgb     = false;    ///< True if the sRGB chunk was present in the file
};

/// PNG codec — static utility class (no state).
///
/// All methods are thread-safe: they use only stack-local libpng state.
class PngCodec {
public:
    PngCodec() = delete;

    /// Decode a PNG file from disk.
    ///
    /// @param path       Filesystem path to the PNG file.
    /// @param forceRGBA  If true, expand output to 4 channels regardless of source
    ///                   format (grayscale, palette, RGB → RGBA).
    /// @return Decoded image on success, or an empty image (width==0) on failure.
    ///         Errors are logged via UZ_LOG(Asset, Error, ...).
    [[nodiscard]] static PngImage DecodeFile(const std::string& path, bool forceRGBA = true);

    /// Decode a PNG from an in-memory buffer.
    ///
    /// @param data       Pointer to the first byte of the PNG file data.
    /// @param dataSize   Size of the buffer in bytes.
    /// @param forceRGBA  If true, expand output to 4 channels.
    /// @return Decoded image on success, or an empty image (width==0) on failure.
    [[nodiscard]] static PngImage DecodeMemory(const uint8_t* data, size_t dataSize,
                                               bool forceRGBA = true);

    /// Encode an 8-bit image to a PNG file on disk.
    ///
    /// @param path       Output filesystem path.
    /// @param pixels     Raw pixel data (8 bits per channel, tightly packed).
    /// @param width      Image width in pixels.
    /// @param height     Image height in pixels.
    /// @param channels   Number of channels: 1 (Gray), 2 (GA), 3 (RGB), or 4 (RGBA).
    /// @return True on success, false on failure (logged via UZ_LOG).
    [[nodiscard]] static bool EncodeFile(const std::string& path, const uint8_t* pixels,
                                         uint32_t width, uint32_t height, uint32_t channels = 4);

    /// Encode an image to a PNG byte buffer in memory.
    ///
    /// @param pixels     Raw pixel data (tightly packed, row-major).
    /// @param width      Image width in pixels.
    /// @param height     Image height in pixels.
    /// @param channels   Number of channels: 1, 2, 3, or 4.
    /// @param bitDepth   Bits per channel: 8 or 16.
    /// @return PNG file bytes on success, or an empty vector on failure.
    [[nodiscard]] static std::vector<uint8_t> EncodeMemory(const uint8_t* pixels,
                                                           uint32_t width, uint32_t height,
                                                           uint32_t channels = 4,
                                                           uint32_t bitDepth = 8);
};

} // namespace uz
