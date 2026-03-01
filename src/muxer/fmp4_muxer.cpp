#include "fmp4_muxer.h"

#include <cstring>

namespace uz {
namespace {

// 3x3 identity matrix for tkhd/mvhd (ISO 14496-12 §8.2.2)
// Fixed-point 16.16 format: diag = 0x00010000, rest = 0
constexpr uint8_t kIdentityMatrix[36] = {
    0x00, 0x01, 0x00, 0x00,  // a = 1.0
    0x00, 0x00, 0x00, 0x00,  // b = 0
    0x00, 0x00, 0x00, 0x00,  // u = 0
    0x00, 0x00, 0x00, 0x00,  // c = 0
    0x00, 0x01, 0x00, 0x00,  // d = 1.0
    0x00, 0x00, 0x00, 0x00,  // v = 0
    0x00, 0x00, 0x00, 0x00,  // x = 0
    0x00, 0x00, 0x00, 0x00,  // y = 0
    0x40, 0x00, 0x00, 0x00   // w = 1.0 (fixed-point 2.30)
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Box writer helpers — all big-endian per ISO BMFF
// ---------------------------------------------------------------------------

void FMP4Muxer::writeU8(std::vector<uint8_t>& buf, uint8_t v) {
    buf.push_back(v);
}

void FMP4Muxer::writeU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v));
}

void FMP4Muxer::writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back(static_cast<uint8_t>(v >> 24));
    buf.push_back(static_cast<uint8_t>(v >> 16));
    buf.push_back(static_cast<uint8_t>(v >> 8));
    buf.push_back(static_cast<uint8_t>(v));
}

void FMP4Muxer::writeU64(std::vector<uint8_t>& buf, uint64_t v) {
    writeU32(buf, static_cast<uint32_t>(v >> 32));
    writeU32(buf, static_cast<uint32_t>(v));
}

void FMP4Muxer::writeBytes(std::vector<uint8_t>& buf, const uint8_t* data, size_t len) {
    buf.insert(buf.end(), data, data + len);
}

void FMP4Muxer::writeBoxHeader(std::vector<uint8_t>& buf, uint32_t size, const char type[4]) {
    writeU32(buf, size);
    buf.push_back(static_cast<uint8_t>(type[0]));
    buf.push_back(static_cast<uint8_t>(type[1]));
    buf.push_back(static_cast<uint8_t>(type[2]));
    buf.push_back(static_cast<uint8_t>(type[3]));
}

void FMP4Muxer::patchU32(std::vector<uint8_t>& buf, size_t pos, uint32_t v) {
    buf[pos + 0] = static_cast<uint8_t>(v >> 24);
    buf[pos + 1] = static_cast<uint8_t>(v >> 16);
    buf[pos + 2] = static_cast<uint8_t>(v >> 8);
    buf[pos + 3] = static_cast<uint8_t>(v);
}

// ---------------------------------------------------------------------------
// splitAnnexB — extract NAL units from Annex B bitstream
// ---------------------------------------------------------------------------

std::vector<std::vector<uint8_t>> FMP4Muxer::splitAnnexB(const uint8_t* data, size_t size) {
    std::vector<std::vector<uint8_t>> nalus;

    size_t i = 0;
    while (i < size) {
        // Find next start code: 00 00 01 or 00 00 00 01
        size_t scLen = 0;
        if (i + 2 < size && data[i] == 0x00 && data[i + 1] == 0x00 && data[i + 2] == 0x01) {
            scLen = 3;
        } else if (i + 3 < size && data[i] == 0x00 && data[i + 1] == 0x00 &&
                   data[i + 2] == 0x00 && data[i + 3] == 0x01) {
            scLen = 4;
        } else {
            ++i;
            continue;
        }

        size_t nalStart = i + scLen;

        // Find the end of this NAL unit (next start code or end of data)
        size_t nalEnd = nalStart;
        while (nalEnd < size) {
            if (nalEnd + 2 < size && data[nalEnd] == 0x00 && data[nalEnd + 1] == 0x00) {
                if (data[nalEnd + 2] == 0x01) {
                    break; // 00 00 01
                }
                if (nalEnd + 3 < size && data[nalEnd + 2] == 0x00 && data[nalEnd + 3] == 0x01) {
                    break; // 00 00 00 01
                }
            }
            ++nalEnd;
        }

        if (nalEnd > nalStart) {
            uint8_t nalType = data[nalStart] & 0x1F;

            // Skip SPS (7) and PPS (8) — they're already in the avcC box
            if (nalType != 7 && nalType != 8) {
                nalus.emplace_back(data + nalStart, data + nalEnd);
            }
        }

        i = nalEnd;
    }

    return nalus;
}

// ---------------------------------------------------------------------------
// initialize — build and cache ftyp + moov (init segment)
// ---------------------------------------------------------------------------

bool FMP4Muxer::initialize(uint8_t profileIdc, uint8_t levelIdc,
                            const std::vector<uint8_t>& sps,
                            const std::vector<uint8_t>& pps,
                            uint32_t width, uint32_t height,
                            uint32_t timeScale,
                            uint32_t frameDurationTicks) {
    if (sps.empty() || pps.empty() || width == 0 || height == 0) {
        return false;
    }

    profileIdc_    = profileIdc;
    levelIdc_      = levelIdc;
    sps_           = sps;
    pps_           = pps;
    width_         = width;
    height_        = height;
    timeScale_     = timeScale;
    frameDuration_ = frameDurationTicks;
    sequenceNumber_ = 1;
    decodeTime_    = 0;

    initSegment_.clear();
    // Reserve a reasonable amount to avoid reallocations
    initSegment_.reserve(512 + sps.size() + pps.size());

    auto& buf = initSegment_;

    // ===== ftyp box =====
    // 8 (header) + 4 (major) + 4 (minor version) + 4*4 (compatible brands) = 32
    writeBoxHeader(buf, 32, "ftyp");
    writeBytes(buf, reinterpret_cast<const uint8_t*>("isom"), 4); // major brand
    writeU32(buf, 0x00000200);                                     // minor version
    writeBytes(buf, reinterpret_cast<const uint8_t*>("isom"), 4); // compatible
    writeBytes(buf, reinterpret_cast<const uint8_t*>("iso2"), 4);
    writeBytes(buf, reinterpret_cast<const uint8_t*>("avc1"), 4);
    writeBytes(buf, reinterpret_cast<const uint8_t*>("mp41"), 4);

    // ===== moov box =====
    size_t moovSizePos = buf.size();
    writeBoxHeader(buf, 0, "moov"); // size patched later

    // ----- mvhd (108 bytes) -----
    writeBoxHeader(buf, 108, "mvhd");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 0);           // creation_time
    writeU32(buf, 0);           // modification_time
    writeU32(buf, timeScale_);  // timescale
    writeU32(buf, 0);           // duration (unknown for live)
    writeU32(buf, 0x00010000);  // rate = 1.0 (fixed-point 16.16)
    writeU16(buf, 0x0100);      // volume = 1.0 (fixed-point 8.8)
    // 10 bytes reserved
    for (int j = 0; j < 10; ++j) writeU8(buf, 0);
    // 3x3 identity matrix (36 bytes)
    writeBytes(buf, kIdentityMatrix, 36);
    // pre-defined (24 bytes zeros)
    for (int j = 0; j < 24; ++j) writeU8(buf, 0);
    writeU32(buf, 2);           // next_track_id

    // ----- trak box -----
    size_t trakSizePos = buf.size();
    writeBoxHeader(buf, 0, "trak"); // patched later

    // --- tkhd (92 bytes) ---
    writeBoxHeader(buf, 92, "tkhd");
    writeU8(buf, 0);            // version
    writeU8(buf, 0); writeU8(buf, 0); writeU8(buf, 0x03); // flags = track_enabled | track_in_movie
    writeU32(buf, 0);           // creation_time
    writeU32(buf, 0);           // modification_time
    writeU32(buf, 1);           // track_id
    writeU32(buf, 0);           // reserved
    writeU32(buf, 0);           // duration (unknown)
    writeU32(buf, 0); writeU32(buf, 0); // reserved (8 bytes)
    writeU16(buf, 0);           // layer
    writeU16(buf, 0);           // alternate_group
    writeU16(buf, 0);           // volume (0 for video)
    writeU16(buf, 0);           // reserved
    writeBytes(buf, kIdentityMatrix, 36); // matrix
    writeU32(buf, width_ << 16);  // width (fixed-point 16.16)
    writeU32(buf, height_ << 16); // height (fixed-point 16.16)

    // --- mdia box ---
    size_t mdiaSizePos = buf.size();
    writeBoxHeader(buf, 0, "mdia"); // patched later

    // mdhd (32 bytes)
    writeBoxHeader(buf, 32, "mdhd");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 0);           // creation_time
    writeU32(buf, 0);           // modification_time
    writeU32(buf, timeScale_);  // timescale
    writeU32(buf, 0);           // duration (unknown)
    writeU16(buf, 0x55C4);      // language = "und" (undetermined)
    writeU16(buf, 0);           // pre-defined

    // hdlr (45 bytes)
    // 8 (header) + 4 (version/flags) + 4 (pre-defined) + 4 (handler_type)
    // + 12 (reserved) + 13 (name "VideoHandler\0") = 45
    writeBoxHeader(buf, 45, "hdlr");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 0);           // pre-defined
    writeBytes(buf, reinterpret_cast<const uint8_t*>("vide"), 4);
    for (int j = 0; j < 12; ++j) writeU8(buf, 0); // reserved
    writeBytes(buf, reinterpret_cast<const uint8_t*>("VideoHandler"), 12);
    writeU8(buf, 0);            // null terminator

    // --- minf box ---
    size_t minfSizePos = buf.size();
    writeBoxHeader(buf, 0, "minf"); // patched later

    // vmhd (20 bytes)
    writeBoxHeader(buf, 20, "vmhd");
    writeU32(buf, 0x00000001);  // version=0, flags=1 (required for vmhd)
    writeU16(buf, 0);           // graphicsmode
    writeU16(buf, 0); writeU16(buf, 0); writeU16(buf, 0); // opcolor (RGB)

    // dinf box
    // dinf = 8 (header) + dref
    // dref = 8 (header) + 4 (version/flags) + 4 (entry_count) + url(12)
    //      = 28
    // dinf total = 8 + 28 = 36
    writeBoxHeader(buf, 36, "dinf");
    writeBoxHeader(buf, 28, "dref");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 1);           // entry_count
    writeBoxHeader(buf, 12, "url ");
    writeU32(buf, 0x00000001);  // version=0, flags=1 (self-contained)

    // --- stbl box ---
    size_t stblSizePos = buf.size();
    writeBoxHeader(buf, 0, "stbl"); // patched later

    // stsd box
    // stsd header(8) + version/flags(4) + entry_count(4) + avc1 entry
    // avc1 = 8 (header) + 6 (reserved) + 2 (data_ref_idx) + 16 (pre-defined/reserved)
    //      + 2 (width) + 2 (height) + 4 (horiz_res) + 4 (vert_res) + 4 (reserved)
    //      + 2 (frame_count) + 32 (compressor_name) + 2 (depth) + 2 (pre-defined)
    //      + avcC
    // avcC = 8 (header) + 1 (version) + 1 (profile) + 1 (compat) + 1 (level)
    //      + 1 (lengthSizeMinusOne) + 1 (numSPS) + 2 (spsLen) + sps.size()
    //      + 1 (numPPS) + 2 (ppsLen) + pps.size()
    //      = 19 + sps.size() + pps.size()
    uint32_t avcCSize = 19 + static_cast<uint32_t>(sps_.size() + pps_.size());
    uint32_t avc1Size = 86 + avcCSize; // 78 (avc1 fixed fields) + 8 (header) = 86 + avcC
    uint32_t stsdSize = 16 + avc1Size;

    writeBoxHeader(buf, stsdSize, "stsd");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 1);           // entry_count

    // avc1 sample entry
    writeBoxHeader(buf, avc1Size, "avc1");
    for (int j = 0; j < 6; ++j) writeU8(buf, 0); // reserved
    writeU16(buf, 1);           // data_reference_index
    for (int j = 0; j < 16; ++j) writeU8(buf, 0); // pre-defined + reserved
    writeU16(buf, static_cast<uint16_t>(width_));
    writeU16(buf, static_cast<uint16_t>(height_));
    writeU32(buf, 0x00480000);  // horiz resolution 72 dpi (fixed-point 16.16)
    writeU32(buf, 0x00480000);  // vert resolution 72 dpi
    writeU32(buf, 0);           // reserved
    writeU16(buf, 1);           // frame_count
    for (int j = 0; j < 32; ++j) writeU8(buf, 0); // compressor_name
    writeU16(buf, 0x0018);      // depth = 24
    writeU16(buf, 0xFFFF);      // pre-defined = -1

    // avcC box (AVCDecoderConfigurationRecord)
    writeBoxHeader(buf, avcCSize, "avcC");
    writeU8(buf, 1);            // configurationVersion
    writeU8(buf, profileIdc_);
    writeU8(buf, 0xFF);         // profile_compatibility (all bits set — safe default)
    writeU8(buf, levelIdc_);
    writeU8(buf, 0xFF);         // lengthSizeMinusOne = 3 (4-byte NALU lengths) | reserved 0xFC
    writeU8(buf, 0xE1);         // numOfSequenceParameterSets = 1 | reserved 0xE0
    writeU16(buf, static_cast<uint16_t>(sps_.size()));
    writeBytes(buf, sps_.data(), sps_.size());
    writeU8(buf, 0x01);         // numOfPictureParameterSets = 1
    writeU16(buf, static_cast<uint16_t>(pps_.size()));
    writeBytes(buf, pps_.data(), pps_.size());

    // stts (16 bytes — empty, since fragments describe timing)
    writeBoxHeader(buf, 16, "stts");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 0);           // entry_count = 0

    // stsc (16 bytes — empty)
    writeBoxHeader(buf, 16, "stsc");
    writeU32(buf, 0);
    writeU32(buf, 0);

    // stsz (20 bytes — empty)
    writeBoxHeader(buf, 20, "stsz");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 0);           // sample_size = 0
    writeU32(buf, 0);           // sample_count = 0

    // stco (16 bytes — empty)
    writeBoxHeader(buf, 16, "stco");
    writeU32(buf, 0);
    writeU32(buf, 0);

    // Patch stbl size
    patchU32(buf, stblSizePos, static_cast<uint32_t>(buf.size() - stblSizePos));

    // Patch minf size
    patchU32(buf, minfSizePos, static_cast<uint32_t>(buf.size() - minfSizePos));

    // Patch mdia size
    patchU32(buf, mdiaSizePos, static_cast<uint32_t>(buf.size() - mdiaSizePos));

    // Patch trak size
    patchU32(buf, trakSizePos, static_cast<uint32_t>(buf.size() - trakSizePos));

    // ----- mvex box -----
    // mvex = 8 (header) + trex(32) = 40
    writeBoxHeader(buf, 40, "mvex");

    // trex (32 bytes)
    writeBoxHeader(buf, 32, "trex");
    writeU32(buf, 0);           // version=0, flags=0
    writeU32(buf, 1);           // track_id
    writeU32(buf, 1);           // default_sample_description_index
    writeU32(buf, frameDuration_); // default_sample_duration
    writeU32(buf, 0);           // default_sample_size
    writeU32(buf, 0);           // default_sample_flags

    // Patch moov size
    patchU32(buf, moovSizePos, static_cast<uint32_t>(buf.size() - moovSizePos));

    return true;
}

// ---------------------------------------------------------------------------
// muxFrame — produce one moof+mdat media segment
// ---------------------------------------------------------------------------

std::vector<uint8_t> FMP4Muxer::muxFrame(const std::vector<uint8_t>& annexBData,
                                           bool isKeyFrame) {
    // 1. Parse Annex B → list of NAL unit payloads (no start codes, no SPS/PPS)
    auto nalus = splitAnnexB(annexBData.data(), annexBData.size());

    // 2. Build AVCC data: 4-byte big-endian length prefix + NAL bytes for each unit
    std::vector<uint8_t> avccData;
    avccData.reserve(annexBData.size());
    for (const auto& nalu : nalus) {
        writeU32(avccData, static_cast<uint32_t>(nalu.size()));
        writeBytes(avccData, nalu.data(), nalu.size());
    }

    // 3. Build the moof + mdat segment
    //
    // moof layout:
    //   moof header (8)
    //     mfhd (16)
    //     traf header (8)
    //       tfhd (16)
    //       tfdt (20) — version=1 for 64-bit base_media_decode_time
    //       trun (28) — flags=0x000305, 1 sample
    //
    // trun fields with flags 0x000305:
    //   0x001 → data-offset present (int32)
    //   0x004 → first-sample-flags present (uint32)
    //   0x100 → sample-duration present (uint32 per sample)
    //   0x200 → sample-size present (uint32 per sample)
    //
    // trun body = 4 (version/flags) + 4 (sample_count) + 4 (data_offset)
    //           + 4 (first_sample_flags) + 4 (duration) + 4 (size) = 24
    // trun total = 8 (header) + 24 = 32
    //
    // traf = 8 + tfhd(16) + tfdt(20) + trun(32) = 76
    // moof = 8 + mfhd(16) + traf(76) = 100

    constexpr uint32_t kMfhdSize = 16;
    constexpr uint32_t kTfhdSize = 16;
    constexpr uint32_t kTfdtSize = 20;
    constexpr uint32_t kTrunSize = 32;
    constexpr uint32_t kTrafSize = 8 + kTfhdSize + kTfdtSize + kTrunSize; // 76
    constexpr uint32_t kMoofSize = 8 + kMfhdSize + kTrafSize;            // 100

    uint32_t avccSize = static_cast<uint32_t>(avccData.size());
    uint32_t mdatSize = 8 + avccSize;

    std::vector<uint8_t> segment;
    segment.reserve(kMoofSize + mdatSize);

    // --- moof ---
    writeBoxHeader(segment, kMoofSize, "moof");

    // mfhd
    writeBoxHeader(segment, kMfhdSize, "mfhd");
    writeU32(segment, 0);                   // version=0, flags=0
    writeU32(segment, sequenceNumber_++);

    // traf
    writeBoxHeader(segment, kTrafSize, "traf");

    // tfhd — flags 0x020000 = default-base-is-moof
    writeBoxHeader(segment, kTfhdSize, "tfhd");
    writeU8(segment, 0);                    // version
    writeU8(segment, 0x02); writeU8(segment, 0x00); writeU8(segment, 0x00); // flags = 0x020000
    writeU32(segment, 1);                   // track_id

    // tfdt — version=1 for 64-bit decode time
    writeBoxHeader(segment, kTfdtSize, "tfdt");
    writeU8(segment, 1);                    // version = 1
    writeU8(segment, 0); writeU8(segment, 0); writeU8(segment, 0); // flags = 0
    writeU64(segment, decodeTime_);

    // trun — flags=0x000305
    writeBoxHeader(segment, kTrunSize, "trun");
    writeU8(segment, 0);                    // version
    writeU8(segment, 0x00); writeU8(segment, 0x03); writeU8(segment, 0x05); // flags = 0x000305
    writeU32(segment, 1);                   // sample_count

    // data_offset: offset from moof start to first sample byte in mdat
    // = moof size + mdat header (8 bytes)
    int32_t dataOffset = static_cast<int32_t>(kMoofSize + 8);
    writeU32(segment, static_cast<uint32_t>(dataOffset));

    // first_sample_flags
    // Key frame: 0x02000000 (sample_depends_on = 2 → does NOT depend on others)
    // Non-key:   0x01010000 (sample_depends_on = 1 → depends on others, is_non_sync)
    uint32_t sampleFlags = isKeyFrame ? 0x02000000u : 0x01010000u;
    writeU32(segment, sampleFlags);

    // Per-sample entries (1 sample)
    writeU32(segment, frameDuration_);      // sample_duration
    writeU32(segment, avccSize);            // sample_size

    // --- mdat ---
    writeBoxHeader(segment, mdatSize, "mdat");
    writeBytes(segment, avccData.data(), avccData.size());

    // Advance decode timeline
    decodeTime_ += frameDuration_;

    return segment;
}

} // namespace uz
