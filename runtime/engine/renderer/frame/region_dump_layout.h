/// UzEngine — Region dump record layout (the single source of truth on the C++ side).
///
/// The record count of each field group exists here and, for the GPU, in
/// `shaders/runtime/common/region_dump.hlsli`. It used to exist in three places — the writer's
/// total, the file header, and the Python reader's offsets — and on 2026-09-10 two of them
/// disagreed for a day, which shifted every LIGHTING_EXTRA channel by one record and produced a
/// ledger row that had to be retracted. Format 4 writes the layout into every file so a reader
/// never has to keep its own copy.
///
/// @see docs/analysis/flicker-hypothesis-ledger.md — "A reader bug, and what it cost"

#pragma once

#include <cstdint>

namespace uz {

/// Records per field group, in field-bit order:
///   bit 0 lighting, bit 1 taa, bit 2 output, bit 3 taa_extra, bit 4 lighting_extra, bit 5 gather.
inline constexpr uint32_t kRegionGroupRecords[6] = {2u, 5u, 1u, 11u, 13u, 31u};
inline constexpr uint32_t kRegionGroupCount = 6u;

/// Total records per pixel for a field mask.
inline uint32_t RegionRecordsPerPixel(uint32_t fields) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < kRegionGroupCount; ++i) {
        if (fields & (1u << i)) n += kRegionGroupRecords[i];
    }
    return n;
}

/// The same counts packed **5 bits per group**, for the format-5 file header: six groups no longer
/// fit six bits each in one word. A group absent from `fields` contributes zero, so the reader can
/// tell "not present" from "present with n records" without consulting anything outside the file.
inline uint32_t RegionGroupRecordsPacked(uint32_t fields) {
    uint32_t packed = 0;
    for (uint32_t i = 0; i < kRegionGroupCount; ++i) {
        if (fields & (1u << i)) packed |= (kRegionGroupRecords[i] & 0x1fu) << (i * 5u);
    }
    return packed;
}

} // namespace uz
