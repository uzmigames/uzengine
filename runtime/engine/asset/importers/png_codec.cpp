/// UzEngine PNG Codec — Implementation
///
/// Wraps libpng 1.6 C API for decode and encode of 8-bit and 16-bit PNG files.
/// Uses setjmp/longjmp for libpng error recovery (standard libpng pattern).
///
/// @ref libpng 1.6 — http://www.libpng.org/pub/png/libpng-manual.txt

#include "png_codec.h"
#include "log.h"

#include <png.h>

#include <csetjmp>
#include <cstdio>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Memory read/write helpers for in-memory PNG operations
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// State for the custom memory-read callback used by DecodeMemory.
struct MemoryReadState {
    const uint8_t* data   = nullptr;
    size_t         size   = 0;
    size_t         offset = 0;
};

/// libpng custom read function — copies bytes from a memory buffer.
///
/// Called by libpng instead of fread() when decoding from memory.
/// Signals a read error if the requested byte count exceeds the remaining
/// buffer, which causes libpng to longjmp to the error handler.
void PngReadFromMemory(png_structp png, png_bytep outBytes, png_size_t byteCount) {
    auto* state = static_cast<MemoryReadState*>(png_get_io_ptr(png));
    if (state->offset + byteCount > state->size) {
        png_error(png, "Read past end of memory buffer");
        return;
    }
    std::memcpy(outBytes, state->data + state->offset, byteCount);
    state->offset += byteCount;
}

/// State for the custom memory-write callback used by EncodeMemory.
struct MemoryWriteState {
    std::vector<uint8_t>* output = nullptr;
};

/// libpng custom write function — appends bytes to a vector.
void PngWriteToMemory(png_structp png, png_bytep data, png_size_t length) {
    auto* state = static_cast<MemoryWriteState*>(png_get_io_ptr(png));
    state->output->insert(state->output->end(), data, data + length);
}

/// libpng custom flush function — no-op for memory writes.
void PngFlushMemory(png_structp /*png*/) {
    // Nothing to flush for an in-memory buffer.
}

/// Maps channel count to the corresponding PNG color type constant.
///
/// @return PNG_COLOR_TYPE_* value, or -1 if the channel count is unsupported.
[[nodiscard]] int ChannelsToPngColorType(uint32_t channels) {
    switch (channels) {
        case 1: return PNG_COLOR_TYPE_GRAY;
        case 2: return PNG_COLOR_TYPE_GRAY_ALPHA;
        case 3: return PNG_COLOR_TYPE_RGB;
        case 4: return PNG_COLOR_TYPE_RGB_ALPHA;
        default: return -1;
    }
}

/// Core decode logic shared by DecodeFile and DecodeMemory.
///
/// Assumes `png` and `info` are already created and the I/O source is
/// configured (either via png_init_io or png_set_read_fn). Applies
/// transforms based on the source format and the forceRGBA flag.
///
/// @param png       Initialized png_struct with I/O source set.
/// @param info      Allocated info_struct.
/// @param forceRGBA Expand to 4 channels if true.
/// @return Decoded PngImage, or empty (width==0) on libpng error.
[[nodiscard]] PngImage DecodeCore(png_structp png, png_infop info, bool forceRGBA) {
    PngImage result;

    // setjmp is the standard libpng error recovery mechanism.
    // If libpng encounters an error, it longjmps here and we return empty.
    if (setjmp(png_jmpbuf(png))) {
        return PngImage{};
    }

    png_read_info(png, info);

    const uint32_t width     = png_get_image_width(png, info);
    const uint32_t height    = png_get_image_height(png, info);
    png_byte       colorType = png_get_color_type(png, info);
    png_byte       bitDepth  = png_get_bit_depth(png, info);

    // ── Apply transforms to normalize the pixel format ──────────────────

    // Expand palette images to RGB.
    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(png);
    }

    // Expand sub-8-bit grayscale to 8 bits.
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png);
    }

    // If a tRNS chunk is present, convert it to a full alpha channel.
    if (png_get_valid(png, info, PNG_INFO_tRNS)) {
        png_set_tRNS_to_alpha(png);
    }

    if (forceRGBA) {
        // Expand grayscale to RGB.
        if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(png);
        }
        // Add an opaque alpha channel if not present.
        if ((colorType & PNG_COLOR_MASK_ALPHA) == 0) {
            png_set_filler(png, 0xFFFF, PNG_FILLER_AFTER);
        }
    }

    // For 16-bit images, swap bytes from network (big-endian) to native
    // little-endian order so that the pixel buffer can be cast directly
    // to uint16_t arrays on x86.
    if (bitDepth == 16) {
        png_set_swap(png);
    }

    // Commit all transforms and re-read the updated info.
    png_read_update_info(png, info);

    // ── Read pixel data ─────────────────────────────────────────────────

    const uint32_t updatedChannels = static_cast<uint32_t>(png_get_channels(png, info));
    const uint32_t updatedBitDepth = static_cast<uint32_t>(png_get_bit_depth(png, info));
    const size_t   rowBytes        = png_get_rowbytes(png, info);

    result.width    = width;
    result.height   = height;
    result.channels = updatedChannels;
    result.bitDepth = updatedBitDepth;

    result.pixels.resize(static_cast<size_t>(height) * rowBytes);

    // Build row pointer array for png_read_image.
    std::vector<png_bytep> rowPointers(height);
    for (uint32_t y = 0; y < height; ++y) {
        rowPointers[y] = result.pixels.data() + y * rowBytes;
    }

    png_read_image(png, rowPointers.data());
    png_read_end(png, nullptr);

    // ── Check for sRGB metadata ─────────────────────────────────────────

    int srgbIntent = 0;
    if (png_get_sRGB(png, info, &srgbIntent) == PNG_INFO_sRGB) {
        result.srgb = true;
    }

    return result;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// DecodeFile
// ─────────────────────────────────────────────────────────────────────────────

PngImage PngCodec::DecodeFile(const std::string& path, bool forceRGBA) {
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeFile: cannot open file '{}'", path);
        return PngImage{};
    }

    // Verify the PNG signature (first 8 bytes).
    uint8_t header[8];
    if (std::fread(header, 1, 8, fp) != 8 || png_sig_cmp(header, 0, 8) != 0) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeFile: not a valid PNG file '{}'", path);
        std::fclose(fp);
        return PngImage{};
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeFile: png_create_read_struct failed");
        std::fclose(fp);
        return PngImage{};
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeFile: png_create_info_struct failed");
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(fp);
        return PngImage{};
    }

    // On libpng error, execution longjmps into DecodeCore's setjmp, which
    // returns an empty PngImage. We still need to clean up here.
    if (setjmp(png_jmpbuf(png))) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeFile: libpng error reading '{}'", path);
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        return PngImage{};
    }

    png_init_io(png, fp);
    // Tell libpng we already consumed the 8-byte signature.
    png_set_sig_bytes(png, 8);

    PngImage result = DecodeCore(png, info, forceRGBA);

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// DecodeMemory
// ─────────────────────────────────────────────────────────────────────────────

PngImage PngCodec::DecodeMemory(const uint8_t* data, size_t dataSize, bool forceRGBA) {
    if (!data || dataSize < 8) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeMemory: null or too-small buffer (size={})", dataSize);
        return PngImage{};
    }

    // Verify the PNG signature.
    if (png_sig_cmp(const_cast<png_bytep>(data), 0, 8) != 0) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeMemory: invalid PNG signature");
        return PngImage{};
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeMemory: png_create_read_struct failed");
        return PngImage{};
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeMemory: png_create_info_struct failed");
        png_destroy_read_struct(&png, nullptr, nullptr);
        return PngImage{};
    }

    if (setjmp(png_jmpbuf(png))) {
        UZ_LOG(Asset, Error, "PngCodec::DecodeMemory: libpng error during decode");
        png_destroy_read_struct(&png, &info, nullptr);
        return PngImage{};
    }

    MemoryReadState readState;
    readState.data   = data;
    readState.size   = dataSize;
    readState.offset = 8; // Skip the signature — we already validated it.

    png_set_read_fn(png, &readState, PngReadFromMemory);
    png_set_sig_bytes(png, 8);

    PngImage result = DecodeCore(png, info, forceRGBA);

    png_destroy_read_struct(&png, &info, nullptr);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeFile
// ─────────────────────────────────────────────────────────────────────────────

bool PngCodec::EncodeFile(const std::string& path, const uint8_t* pixels,
                          uint32_t width, uint32_t height, uint32_t channels) {
    if (!pixels || width == 0 || height == 0) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: invalid parameters ({}x{}, pixels={})",
               width, height, pixels ? "valid" : "null");
        return false;
    }

    const int colorType = ChannelsToPngColorType(channels);
    if (colorType < 0) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: unsupported channel count {}", channels);
        return false;
    }

    FILE* fp = std::fopen(path.c_str(), "wb");
    if (!fp) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: cannot open '{}' for writing", path);
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: png_create_write_struct failed");
        std::fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: png_create_info_struct failed");
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeFile: libpng error writing '{}'", path);
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        return false;
    }

    png_init_io(png, fp);

    png_set_IHDR(png, info, width, height, 8,
                 colorType, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    const size_t rowBytes = static_cast<size_t>(width) * channels;
    for (uint32_t y = 0; y < height; ++y) {
        // png_write_row takes a non-const pointer; the data is not modified.
        png_write_row(png, const_cast<png_bytep>(pixels + y * rowBytes));
    }

    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    std::fclose(fp);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// EncodeMemory
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> PngCodec::EncodeMemory(const uint8_t* pixels,
                                             uint32_t width, uint32_t height,
                                             uint32_t channels, uint32_t bitDepth) {
    if (!pixels || width == 0 || height == 0) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: invalid parameters ({}x{}, pixels={})",
               width, height, pixels ? "valid" : "null");
        return {};
    }

    if (bitDepth != 8 && bitDepth != 16) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: unsupported bit depth {} (must be 8 or 16)",
               bitDepth);
        return {};
    }

    const int colorType = ChannelsToPngColorType(channels);
    if (colorType < 0) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: unsupported channel count {}", channels);
        return {};
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: png_create_write_struct failed");
        return {};
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: png_create_info_struct failed");
        png_destroy_write_struct(&png, nullptr);
        return {};
    }

    std::vector<uint8_t> output;
    // Reserve a rough estimate to reduce reallocations.
    output.reserve(static_cast<size_t>(width) * height * channels * (bitDepth / 8) / 2);

    MemoryWriteState writeState;
    writeState.output = &output;

    if (setjmp(png_jmpbuf(png))) {
        UZ_LOG(Asset, Error, "PngCodec::EncodeMemory: libpng error during encode");
        png_destroy_write_struct(&png, &info);
        return {};
    }

    png_set_write_fn(png, &writeState, PngWriteToMemory, PngFlushMemory);

    png_set_IHDR(png, info, width, height, static_cast<int>(bitDepth),
                 colorType, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png, info);

    // For 16-bit output, swap bytes to network (big-endian) order as
    // required by the PNG spec. The input buffer is in native LE order.
    if (bitDepth == 16) {
        png_set_swap(png);
    }

    const size_t bytesPerSample = bitDepth / 8;
    const size_t rowBytes       = static_cast<size_t>(width) * channels * bytesPerSample;
    for (uint32_t y = 0; y < height; ++y) {
        png_write_row(png, const_cast<png_bytep>(pixels + y * rowBytes));
    }

    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);

    return output;
}

} // namespace uz
