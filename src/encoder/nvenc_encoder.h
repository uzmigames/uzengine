#pragma once

#include "encoder.h"

namespace uz {

/// NVENC hardware H.264 encoder.
///
/// Uses dynamic loading (LoadLibrary / GetProcAddress) so the project compiles
/// even without the NVIDIA Video Codec SDK headers installed.  When the SDK
/// headers are not available the encoder reports isAvailable() == true (the DLL
/// exists on the machine) but initialize() returns false, causing the encoder
/// factory to fall back to the FFmpeg/x264 software path.
///
/// Full NVENC implementation requires the NVIDIA Video Codec SDK headers from
/// https://developer.nvidia.com/video-codec-sdk -- once those are in the
/// include path, replace the stub bodies in nvenc_encoder.cpp.
class NvEncEncoder final : public IFrameEncoder {
public:
    NvEncEncoder();
    ~NvEncEncoder() override;

    /// Probes for nvEncodeAPI64.dll on the system.
    [[nodiscard]] static bool isAvailable();

    [[nodiscard]] bool initialize(const EncoderConfig& config) override;
    [[nodiscard]] bool encode(const uint8_t* rgbaData, uint32_t width, uint32_t height,
                              std::vector<uint8_t>& outNALUs) override;
    [[nodiscard]] const CodecConfig& getCodecConfig() const override { return codecConfig_; }
    void shutdown() override;

private:
    void* nvencLib_      = nullptr; // HMODULE for nvEncodeAPI64.dll
    void* nvencPtr_      = nullptr; // NV_ENCODE_API_FUNCTION_LIST* (future)
    void* hEncoder_      = nullptr; // encoder session handle (future)
    void* inputBuf_      = nullptr; // NV_ENC_INPUT_PTR (future)
    void* outputBuf_     = nullptr; // NV_ENC_REGISTERED_PTR (future)
    void* bitstreamBuf_  = nullptr; // NV_ENC_OUTPUT_PTR (future)

    uint32_t width_  = 0;
    uint32_t height_ = 0;
    bool     firstFrame_ = true;

    CodecConfig codecConfig_;

    /// Scratch buffer for any pixel-format conversion (RGBA -> NV12 etc.)
    std::vector<uint8_t> stagingRGBA_;

    /// Parse Annex-B bitstream and extract raw SPS/PPS NAL units into codecConfig_.
    bool extractSPSPPS(const uint8_t* data, size_t size);
};

} // namespace uz
