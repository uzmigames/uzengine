#pragma once

#include <cstdint>
#include <vector>

namespace uz {

struct CodecConfig; // forward declare (defined in encoder/encoder.h)

/// Hand-written ISO 14496-12 Fragmented MP4 muxer.
///
/// Produces MSE-compatible fMP4 segments:
///   - An init segment (ftyp + moov) sent once on WebSocket connect.
///   - Media segments (moof + mdat) sent per-frame.
///
/// All multi-byte integers are written big-endian per ISOBMFF spec.
class FMP4Muxer {
public:
    /// Initialize the muxer with codec parameters from the encoder.
    /// @param profileIdc  H.264 profile byte (e.g. 0x42 = Baseline)
    /// @param levelIdc    H.264 level byte (e.g. 0x1F = 3.1)
    /// @param sps         Raw SPS NAL unit bytes WITHOUT start code
    /// @param pps         Raw PPS NAL unit bytes WITHOUT start code
    /// @param width       Video resolution width in pixels
    /// @param height      Video resolution height in pixels
    /// @param timeScale   Media timescale (90000 is standard for video)
    /// @param frameDurationTicks  Duration per frame in timescale units
    [[nodiscard]] bool initialize(uint8_t profileIdc, uint8_t levelIdc,
                                  const std::vector<uint8_t>& sps,
                                  const std::vector<uint8_t>& pps,
                                  uint32_t width, uint32_t height,
                                  uint32_t timeScale = 90000,
                                  uint32_t frameDurationTicks = 3000);

    /// Returns the cached fMP4 init segment (ftyp + moov).
    /// Must be sent to clients BEFORE any media segments.
    [[nodiscard]] const std::vector<uint8_t>& getInitSegment() const noexcept {
        return initSegment_;
    }

    /// Mux one H.264 access unit (Annex B format) into an fMP4 media segment.
    /// @param annexBData  Annex B bitstream from encoder (with start codes)
    /// @param isKeyFrame  true if this is an IDR frame
    /// @returns moof+mdat bytes ready for WebSocket delivery
    [[nodiscard]] std::vector<uint8_t> muxFrame(const std::vector<uint8_t>& annexBData,
                                                 bool isKeyFrame);

private:
    // Big-endian box writer helpers
    static void writeU8(std::vector<uint8_t>& buf, uint8_t v);
    static void writeU16(std::vector<uint8_t>& buf, uint16_t v);
    static void writeU32(std::vector<uint8_t>& buf, uint32_t v);
    static void writeU64(std::vector<uint8_t>& buf, uint64_t v);
    static void writeBytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len);
    static void writeBoxHeader(std::vector<uint8_t>& buf, uint32_t size, const char type[4]);
    static void patchU32(std::vector<uint8_t>& buf, size_t pos, uint32_t v);

    // Splits Annex B bitstream into individual NAL units (without start codes).
    // Filters out SPS/PPS NALs since they live in avcC.
    static std::vector<std::vector<uint8_t>> splitAnnexB(const uint8_t* data, size_t size);

    std::vector<uint8_t> initSegment_;

    uint8_t  profileIdc_    = 0x42;
    uint8_t  levelIdc_      = 0x1F;
    std::vector<uint8_t> sps_;
    std::vector<uint8_t> pps_;
    uint32_t width_         = 0;
    uint32_t height_        = 0;
    uint32_t timeScale_     = 90000;
    uint32_t frameDuration_ = 3000;
    uint32_t sequenceNumber_ = 1;
    uint64_t decodeTime_    = 0;
};

} // namespace uz
