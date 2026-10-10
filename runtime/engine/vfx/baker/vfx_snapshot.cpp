#include "vfx_snapshot.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace uz {

constexpr const char VfxSnapshotHeader::kMagic[8];

namespace {

constexpr uint32_t kHeaderSize     = 128u;
constexpr uint32_t kEmitterNameMax = 64u;
constexpr uint32_t kChannelNameMax = 32u;
constexpr uint32_t kChannelEntrySize = 64u; // name(32) + type(4) + count(4) + offset(8) + length(8) + pad(8)

template <typename T>
void AppendLE(std::vector<uint8_t>& out, T v) {
    uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &v, sizeof(T));
    // Assume host is little-endian (all Windows/Linux/macOS targets are).
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

template <typename T>
bool ReadLE(const std::vector<uint8_t>& in, size_t& cursor, T& v) {
    if (cursor + sizeof(T) > in.size()) return false;
    std::memcpy(&v, in.data() + cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

void AppendPaddedString(std::vector<uint8_t>& out, const std::string& s, size_t fieldSize) {
    const size_t copyLen = std::min(s.size(), fieldSize - 1); // always leave a null
    out.insert(out.end(), s.begin(), s.begin() + copyLen);
    out.insert(out.end(), fieldSize - copyLen, static_cast<uint8_t>(0));
}

bool ReadPaddedString(const std::vector<uint8_t>& in, size_t& cursor,
                        size_t fieldSize, std::string& out) {
    if (cursor + fieldSize > in.size()) return false;
    const char* p = reinterpret_cast<const char*>(in.data() + cursor);
    size_t len = 0;
    while (len < fieldSize && p[len] != '\0') ++len;
    out.assign(p, len);
    cursor += fieldSize;
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Element type helpers
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxSnapshotElementSize(VfxSnapshotElementType type) noexcept {
    switch (type) {
        case VfxSnapshotElementType::Float:  return 4;
        case VfxSnapshotElementType::Float2: return 8;
        case VfxSnapshotElementType::Float3: return 12;
        case VfxSnapshotElementType::Float4: return 16;
        case VfxSnapshotElementType::Uint32: return 4;
        case VfxSnapshotElementType::Int32:  return 4;
        case VfxSnapshotElementType::Half:   return 2;
        case VfxSnapshotElementType::Bool:   return 1;
    }
    return 0;
}

const char* VfxSnapshotElementTypeName(VfxSnapshotElementType type) noexcept {
    switch (type) {
        case VfxSnapshotElementType::Float:  return "Float";
        case VfxSnapshotElementType::Float2: return "Float2";
        case VfxSnapshotElementType::Float3: return "Float3";
        case VfxSnapshotElementType::Float4: return "Float4";
        case VfxSnapshotElementType::Uint32: return "Uint32";
        case VfxSnapshotElementType::Int32:  return "Int32";
        case VfxSnapshotElementType::Half:   return "Half";
        case VfxSnapshotElementType::Bool:   return "Bool";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSnapshot::IsValid() const noexcept {
    if (header.emitterName.size() >= kEmitterNameMax) return false;
    if (header.aliveCount > header.capacity)            return false;

    std::unordered_set<std::string> seen;
    for (const auto& c : channels) {
        if (c.name.empty() || c.name.size() >= kChannelNameMax) return false;
        if (!seen.insert(c.name).second)                         return false;
        const uint32_t elemSize = VfxSnapshotElementSize(c.elementType);
        if (elemSize == 0)                                        return false;
        if (c.data.size() != static_cast<size_t>(c.elementCount) * elemSize) return false;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> VfxSnapshotSaveToBytes(const VfxSnapshot& snapshot) {
    if (!snapshot.IsValid()) return {};

    std::vector<uint8_t> out;
    out.reserve(kHeaderSize +
                static_cast<size_t>(snapshot.channels.size()) * kChannelEntrySize);

    // ── Header ──────────────────────────────────────────────────────────
    const size_t headerStart = out.size();
    out.insert(out.end(), std::begin(VfxSnapshotHeader::kMagic),
                           std::end(VfxSnapshotHeader::kMagic));
    AppendLE<uint32_t>(out, VfxSnapshotHeader::kVersion);
    AppendLE<uint32_t>(out, kHeaderSize);
    AppendLE<uint64_t>(out, snapshot.header.frameIndex);
    AppendLE<uint64_t>(out, snapshot.header.wallClockNs);
    AppendLE<uint64_t>(out, snapshot.header.rngSeed);
    AppendLE<uint32_t>(out, snapshot.header.capacity);
    AppendLE<uint32_t>(out, snapshot.header.aliveCount);
    AppendLE<uint32_t>(out, static_cast<uint32_t>(snapshot.channels.size()));
    AppendLE<uint32_t>(out, 0u); // flags reserved
    AppendPaddedString(out, snapshot.header.emitterName, kEmitterNameMax);
    // Pad header to exactly kHeaderSize bytes.
    const size_t headerUsed = out.size() - headerStart;
    if (headerUsed < kHeaderSize) {
        out.insert(out.end(), kHeaderSize - headerUsed, static_cast<uint8_t>(0));
    }

    // ── Channel table ───────────────────────────────────────────────────
    // Compute byte offsets for each channel's data relative to file start.
    const size_t channelTableSize = snapshot.channels.size() * kChannelEntrySize;
    const size_t dataStart = kHeaderSize + channelTableSize;
    size_t runningOffset = dataStart;

    for (const auto& c : snapshot.channels) {
        AppendPaddedString(out, c.name, kChannelNameMax);
        AppendLE<uint32_t>(out, static_cast<uint32_t>(c.elementType));
        AppendLE<uint32_t>(out, c.elementCount);
        AppendLE<uint64_t>(out, runningOffset);
        AppendLE<uint64_t>(out, static_cast<uint64_t>(c.data.size()));
        AppendLE<uint64_t>(out, 0u); // pad
        runningOffset += c.data.size();
    }

    // ── Channel data (packed in order) ──────────────────────────────────
    for (const auto& c : snapshot.channels) {
        out.insert(out.end(), c.data.begin(), c.data.end());
    }

    return out;
}

bool VfxSnapshotSaveToFile(const VfxSnapshot& snapshot, const std::string& path) {
    const auto bytes = VfxSnapshotSaveToBytes(snapshot);
    if (bytes.empty()) return false;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(ofs);
}

// ─────────────────────────────────────────────────────────────────────────────
// Load
// ─────────────────────────────────────────────────────────────────────────────

bool VfxSnapshotLoadFromBytes(const std::vector<uint8_t>& bytes,
                                VfxSnapshot& outSnapshot,
                                std::string& outError) {
    outSnapshot = {};
    if (bytes.size() < kHeaderSize) {
        outError = "file too small for header";
        return false;
    }

    // ── Magic + version ────────────────────────────────────────────────
    if (std::memcmp(bytes.data(), VfxSnapshotHeader::kMagic, 8) != 0) {
        outError = "bad magic (expected UZVFXSNP)";
        return false;
    }

    size_t cursor = 8;
    uint32_t version = 0, headerSize = 0;
    if (!ReadLE(bytes, cursor, version) || !ReadLE(bytes, cursor, headerSize)) {
        outError = "header truncated";
        return false;
    }
    if (version != VfxSnapshotHeader::kVersion) {
        outError = "unsupported version";
        return false;
    }
    if (headerSize != kHeaderSize) {
        outError = "unexpected header size";
        return false;
    }

    uint64_t frameIndex = 0, wallNs = 0, rngSeed = 0;
    uint32_t capacity = 0, alive = 0, channelCount = 0, flags = 0;
    if (!ReadLE(bytes, cursor, frameIndex) ||
        !ReadLE(bytes, cursor, wallNs)     ||
        !ReadLE(bytes, cursor, rngSeed)    ||
        !ReadLE(bytes, cursor, capacity)   ||
        !ReadLE(bytes, cursor, alive)      ||
        !ReadLE(bytes, cursor, channelCount) ||
        !ReadLE(bytes, cursor, flags)) {
        outError = "header fields truncated";
        return false;
    }

    std::string name;
    if (!ReadPaddedString(bytes, cursor, kEmitterNameMax, name)) {
        outError = "emitter name truncated";
        return false;
    }

    outSnapshot.header.frameIndex  = frameIndex;
    outSnapshot.header.wallClockNs = wallNs;
    outSnapshot.header.rngSeed     = rngSeed;
    outSnapshot.header.capacity    = capacity;
    outSnapshot.header.aliveCount  = alive;
    outSnapshot.header.emitterName = std::move(name);

    // Skip header padding to exactly kHeaderSize.
    cursor = kHeaderSize;

    // ── Channel table ───────────────────────────────────────────────────
    struct ChannelEntry {
        std::string name;
        uint32_t type;
        uint32_t count;
        uint64_t offset;
        uint64_t length;
    };
    std::vector<ChannelEntry> entries;
    entries.reserve(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i) {
        ChannelEntry e;
        uint64_t pad;
        if (!ReadPaddedString(bytes, cursor, kChannelNameMax, e.name) ||
            !ReadLE(bytes, cursor, e.type)   ||
            !ReadLE(bytes, cursor, e.count)  ||
            !ReadLE(bytes, cursor, e.offset) ||
            !ReadLE(bytes, cursor, e.length) ||
            !ReadLE(bytes, cursor, pad)) {
            outError = "channel entry " + std::to_string(i) + " truncated";
            return false;
        }
        entries.push_back(std::move(e));
    }

    // ── Channel data ────────────────────────────────────────────────────
    outSnapshot.channels.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.offset + e.length > bytes.size()) {
            outError = "channel '" + e.name + "' data out of range";
            return false;
        }
        if (e.type >= static_cast<uint32_t>(VfxSnapshotElementType::Bool) + 1) {
            outError = "channel '" + e.name + "' unknown element type";
            return false;
        }
        VfxSnapshotChannel c;
        c.name         = e.name;
        c.elementType  = static_cast<VfxSnapshotElementType>(e.type);
        c.elementCount = e.count;
        c.data.assign(bytes.begin() + static_cast<std::ptrdiff_t>(e.offset),
                       bytes.begin() + static_cast<std::ptrdiff_t>(e.offset + e.length));
        outSnapshot.channels.push_back(std::move(c));
    }

    return true;
}

bool VfxSnapshotLoadFromFile(const std::string& path,
                               VfxSnapshot& outSnapshot,
                               std::string& outError) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs) {
        outError = "cannot open file";
        return false;
    }
    const std::streamsize size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!ifs.read(reinterpret_cast<char*>(bytes.data()), size)) {
        outError = "cannot read file";
        return false;
    }
    return VfxSnapshotLoadFromBytes(bytes, outSnapshot, outError);
}

// ─────────────────────────────────────────────────────────────────────────────
// Human-readable dump
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxSnapshotDump(const VfxSnapshot& snapshot) {
    std::ostringstream ss;
    ss << "VfxSnapshot: emitter=\"" << snapshot.header.emitterName
       << "\" frame=" << snapshot.header.frameIndex
       << " alive=" << snapshot.header.aliveCount
       << "/" << snapshot.header.capacity
       << " rngSeed=" << snapshot.header.rngSeed
       << " wallClockNs=" << snapshot.header.wallClockNs << "\n";
    for (const auto& c : snapshot.channels) {
        ss << "  channel " << c.name << " "
           << VfxSnapshotElementTypeName(c.elementType)
           << " " << c.elementCount << " elems\n";
    }
    return ss.str();
}

} // namespace uz
