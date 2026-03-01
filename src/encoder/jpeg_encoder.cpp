#include "jpeg_encoder.h"

#include <turbojpeg.h>

#include <cstdio>

namespace uz {

JpegEncoder::JpegEncoder()
    : m_handle(tjInitCompress())
{
    if (!m_handle) {
        // tjInitCompress only fails on out-of-memory; treat as fatal.
        // The caller will receive false from encode() on the first call.
    }
}

JpegEncoder::~JpegEncoder() {
    if (m_handle) {
        tjDestroy(static_cast<tjhandle>(m_handle));
        m_handle = nullptr;
    }
}

bool JpegEncoder::encode(const uint8_t* rgba, int width, int height, int quality,
                         std::vector<uint8_t>& out)
{
    if (!m_handle) {
        return false;
    }

    unsigned char* jpegBuf  = nullptr;
    unsigned long  jpegSize = 0;

    const int pitch = width * 4; // 4 bytes per RGBA pixel, no padding

    const int ret = tjCompress2(
        static_cast<tjhandle>(m_handle),
        rgba,
        width,
        pitch,
        height,
        TJPF_RGBA,    // pixel format: R G B A
        &jpegBuf,
        &jpegSize,
        TJSAMP_420,   // chroma subsampling: 4:2:0 (good quality/size balance)
        quality,
        TJFLAG_FASTDCT
    );

    if (ret != 0 || !jpegBuf || jpegSize == 0) {
        if (jpegBuf) {
            tjFree(jpegBuf);
        }
        return false;
    }

    out.assign(jpegBuf, jpegBuf + jpegSize);
    tjFree(jpegBuf);
    return true;
}

} // namespace uz
