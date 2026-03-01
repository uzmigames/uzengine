#pragma once

#include <cstdint>
#include <vector>

#include "../common/types.h"

namespace uz {

/// Codec-level configuration extracted after the first IDR frame is encoded.
/// Consumers (muxer, SDP generator) use this to set up the decode pipeline.
struct CodecConfig {
    uint8_t profileIdc = 0x42; // Baseline
    uint8_t levelIdc   = 0x1F; // Level 3.1
    std::vector<uint8_t> sps;  // raw SPS NAL unit (no start code)
    std::vector<uint8_t> pps;  // raw PPS NAL unit (no start code)
};

/// Abstract interface for frame encoders.
/// Implementations (NVENC, FFmpeg/x264) derive from this and are selected
/// at runtime by the encoder factory based on hardware availability.
class IFrameEncoder {
public:
    virtual ~IFrameEncoder() = default;

    // Non-copyable, non-movable -- implementations manage opaque GPU resources
    IFrameEncoder(const IFrameEncoder&) = delete;
    IFrameEncoder& operator=(const IFrameEncoder&) = delete;
    IFrameEncoder(IFrameEncoder&&) = delete;
    IFrameEncoder& operator=(IFrameEncoder&&) = delete;

    /// Set up the encoder with the given configuration.
    /// Returns true on success. On failure the encoder is left in a
    /// safe state and shutdown() need not be called.
    [[nodiscard]] virtual bool initialize(const EncoderConfig& config) = 0;

    /// Encode one RGBA frame.
    /// @param rgbaData  Pointer to width*height*4 bytes of RGBA pixel data.
    /// @param width     Frame width in pixels.
    /// @param height    Frame height in pixels.
    /// @param outNALUs  Filled with Annex-B H.264 NAL units on success.
    /// @returns true if encoding succeeded.
    [[nodiscard]] virtual bool encode(const uint8_t* rgbaData,
                                      uint32_t width, uint32_t height,
                                      std::vector<uint8_t>& outNALUs) = 0;

    /// Retrieve codec configuration (SPS/PPS).
    /// Valid only after the first successful encode() that produced an IDR frame.
    [[nodiscard]] virtual const CodecConfig& getCodecConfig() const = 0;

    /// Release all encoder resources. Safe to call multiple times.
    virtual void shutdown() = 0;

    /// Returns true if this encoder backend is available on the current system.
    /// Each derived class provides its own static isAvailable().
    static bool isAvailable() { return false; }

protected:
    IFrameEncoder() = default;
};

} // namespace uz
