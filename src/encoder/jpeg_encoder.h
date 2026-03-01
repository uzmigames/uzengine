#pragma once

#include <cstdint>
#include <vector>

namespace uz {

/**
 * Lightweight RGBA-to-JPEG encoder backed by libjpeg-turbo's TurboJPEG API.
 *
 * Usage:
 *   JpegEncoder enc;
 *   std::vector<uint8_t> blob;
 *   enc.encode(rgba_ptr, 1280, 720, 85, blob);
 *   // blob now contains a self-contained JPEG image
 *
 * Thread safety: NOT thread-safe. Create one instance per thread.
 */
class JpegEncoder {
public:
    JpegEncoder();
    ~JpegEncoder();

    // Non-copyable, non-movable (tjhandle is an opaque raw pointer).
    JpegEncoder(const JpegEncoder&) = delete;
    JpegEncoder& operator=(const JpegEncoder&) = delete;

    /**
     * Encode a single RGBA frame to JPEG.
     *
     * @param rgba    Pointer to RGBA pixel data (4 bytes per pixel, row-major).
     * @param width   Frame width in pixels.
     * @param height  Frame height in pixels.
     * @param quality JPEG quality [1-100]. 85 is a good default.
     * @param out     Output buffer — filled with the JPEG blob on success.
     * @return        true on success, false on libjpeg-turbo error.
     */
    bool encode(const uint8_t* rgba, int width, int height, int quality,
                std::vector<uint8_t>& out);

private:
    void* m_handle; // tjhandle (void* to avoid pulling turbojpeg.h into every TU)
};

} // namespace uz
