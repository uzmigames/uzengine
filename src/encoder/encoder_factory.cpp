#include "encoder_factory.h"

#include "nvenc_encoder.h"
#include "ffmpeg_encoder.h"

#include <cstdio>

namespace uz {

std::unique_ptr<IFrameEncoder> createEncoder(const EncoderConfig& config) {
    // Attempt NVENC hardware encoder first (if user prefers it and hardware is present)
    if (config.preferNVENC && NvEncEncoder::isAvailable()) {
        auto enc = std::make_unique<NvEncEncoder>();
        if (enc->initialize(config)) {
            printf("[Encoder] NVENC hardware encoder initialized\n");
            return enc;
        }
        printf("[Encoder] NVENC available but init failed, falling back to software\n");
    }

    // Fall back to Windows Media Foundation software encoder
    if (FFmpegEncoder::isAvailable()) {
        auto enc = std::make_unique<FFmpegEncoder>();
        if (enc->initialize(config)) {
            printf("[Encoder] Windows MF software encoder initialized\n");
            return enc;
        }
        printf("[Encoder] Windows MF encoder available but init failed\n");
    }

    printf("[Encoder] ERROR: No encoder available\n");
    return nullptr;
}

} // namespace uz
