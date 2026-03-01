#pragma once

#include "encoder.h"

namespace uz {

/// Software H.264 encoder backed by Windows Media Foundation (MF).
/// Named FFmpegEncoder for interface compatibility with the encoder factory,
/// but uses the built-in Windows MF H.264 encoder (CMSH264EncoderMFT) internally.
/// Requires Windows 10+ and links against mf.lib, mfplat.lib, mfuuid.lib.
class FFmpegEncoder final : public IFrameEncoder {
public:
    FFmpegEncoder();
    ~FFmpegEncoder() override;

    /// Returns true if the Windows MF H.264 encoder is available on this system.
    [[nodiscard]] static bool isAvailable();

    [[nodiscard]] bool initialize(const EncoderConfig& config) override;
    [[nodiscard]] bool encode(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                              std::vector<uint8_t>& outNALUs) override;
    [[nodiscard]] const CodecConfig& getCodecConfig() const override { return codecConfig_; }
    void shutdown() override;

private:
    // Opaque pointers to MF COM objects — avoids leaking MF headers into this header.
    void* transform_  = nullptr; // IMFTransform*
    void* inputType_  = nullptr; // IMFMediaType* (input)
    void* outputType_ = nullptr; // IMFMediaType* (output)

    uint32_t width_       = 0;
    uint32_t height_      = 0;
    uint32_t fps_         = 0;
    int64_t  pts_         = 0;
    int64_t  frameDuration_ = 0; // in 100-ns units
    bool     initialized_ = false;
    bool     mfStarted_   = false;
    bool     useNV12_     = false; // true when ARGB32 input was rejected and NV12 fallback is active

    CodecConfig codecConfig_;

    bool configureEncoder(const EncoderConfig& config);
    bool extractCodecConfig();
    bool parseNALUnits(const uint8_t* data, size_t size);
};

} // namespace uz
