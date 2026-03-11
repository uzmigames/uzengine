/// UzEngine .uzpak Archive — Implementation
///
/// Writer packs entries with 64-byte alignment and sorted index.
/// Reader loads into memory and provides O(1) hash-based lookup.

#include "pak_format.h"
#include "log.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Alignment helper
// ─────────────────────────────────────────────────────────────────────────────

static size_t AlignUp(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Writer
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> WritePak(const std::vector<PakWriteEntry>& entries) {
    // Build index entries with path hashes
    struct IndexBuild {
        PakIndexEntry index;
        size_t entryIdx;
    };

    std::vector<IndexBuild> builds;
    builds.reserve(entries.size());

    for (size_t i = 0; i < entries.size(); ++i) {
        IndexBuild b;
        b.index.pathHash = XXH64(entries[i].path.data(), entries[i].path.size(), 0);
        b.index.size = entries[i].data.size();
        b.index.contentHash = XXH64(entries[i].data.data(), entries[i].data.size(), 0);
        b.index.typeId = entries[i].typeId;
        b.entryIdx = i;
        builds.push_back(b);
    }

    // Sort by pathHash for binary search
    std::sort(builds.begin(), builds.end(),
              [](const IndexBuild& a, const IndexBuild& b) {
                  return a.index.pathHash < b.index.pathHash;
              });

    // Calculate data region: header + aligned blobs
    size_t dataOffset = sizeof(PakHeader);
    for (auto& b : builds) {
        size_t alignedOffset = AlignUp(dataOffset, kPakAlignment);
        b.index.offset = alignedOffset;
        dataOffset = alignedOffset + b.index.size;
    }

    size_t indexOffset = AlignUp(dataOffset, kPakAlignment);
    size_t indexSize = builds.size() * sizeof(PakIndexEntry);
    size_t trailerOffset = indexOffset + indexSize;
    size_t totalSize = trailerOffset + sizeof(PakTrailer);

    // Allocate and write
    std::vector<uint8_t> archive(totalSize, 0);

    // Header
    PakHeader header;
    header.entryCount = static_cast<uint32_t>(entries.size());
    std::memcpy(archive.data(), &header, sizeof(header));

    // Data blobs
    for (const auto& b : builds) {
        const auto& entry = entries[b.entryIdx];
        if (!entry.data.empty()) {
            std::memcpy(archive.data() + b.index.offset,
                        entry.data.data(), entry.data.size());
        }
    }

    // Index
    for (size_t i = 0; i < builds.size(); ++i) {
        std::memcpy(archive.data() + indexOffset + i * sizeof(PakIndexEntry),
                    &builds[i].index, sizeof(PakIndexEntry));
    }

    // Trailer
    PakTrailer trailer;
    trailer.indexOffset = indexOffset;
    std::memcpy(archive.data() + trailerOffset, &trailer, sizeof(trailer));

    return archive;
}

bool WritePakFile(const std::string& path, const std::vector<PakWriteEntry>& entries) {
    auto data = WritePak(entries);

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot write pak file: path='{}'", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.close();

    UZ_LOG(Asset, Info, "Wrote pak: path='{}', entries={}, size={} bytes",
           path, entries.size(), data.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────────

bool PakReader::Load(const std::vector<uint8_t>& data) {
    Unload();

    if (data.size() < sizeof(PakHeader) + sizeof(PakTrailer)) {
        UZ_LOG(Asset, Error, "Pak data too small: {} bytes", data.size());
        return false;
    }

    // Read header
    PakHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.magic != kPakMagic) {
        UZ_LOG(Asset, Error, "Pak invalid magic: expected 0x{:08X}, got 0x{:08X}",
               kPakMagic, header.magic);
        return false;
    }

    if (header.version != kPakVersion) {
        UZ_LOG(Asset, Error, "Pak unsupported version: {}", header.version);
        return false;
    }

    // Read trailer
    PakTrailer trailer;
    std::memcpy(&trailer, data.data() + data.size() - sizeof(PakTrailer), sizeof(trailer));

    if (trailer.magic != kPakMagic) {
        UZ_LOG(Asset, Error, "Pak trailer magic mismatch");
        return false;
    }

    // Read index
    size_t indexSize = header.entryCount * sizeof(PakIndexEntry);
    if (trailer.indexOffset + indexSize > data.size() - sizeof(PakTrailer)) {
        UZ_LOG(Asset, Error, "Pak index exceeds data bounds");
        return false;
    }

    m_data = data;
    m_index.reserve(header.entryCount);

    for (uint32_t i = 0; i < header.entryCount; ++i) {
        PakIndexEntry entry;
        std::memcpy(&entry,
                    m_data.data() + trailer.indexOffset + i * sizeof(PakIndexEntry),
                    sizeof(entry));

        m_index[entry.pathHash] = {entry.offset, entry.size, entry.contentHash};
    }

    m_loaded = true;
    UZ_LOG(Asset, Info, "Loaded pak: {} entries, {} bytes", header.entryCount, data.size());
    return true;
}

bool PakReader::LoadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot open pak file: path='{}'", path);
        return false;
    }

    auto size = file.tellg();
    if (size <= 0) return false;

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return Load(data);
}

std::vector<uint8_t> PakReader::Lookup(const std::string& path) const {
    uint64_t hash = XXH64(path.data(), path.size(), 0);
    return LookupByHash(hash);
}

std::vector<uint8_t> PakReader::LookupByHash(uint64_t pathHash) const {
    auto it = m_index.find(pathHash);
    if (it == m_index.end()) return {};

    const auto& entry = it->second;
    if (entry.offset + entry.size > m_data.size()) return {};

    return std::vector<uint8_t>(
        m_data.begin() + static_cast<ptrdiff_t>(entry.offset),
        m_data.begin() + static_cast<ptrdiff_t>(entry.offset + entry.size));
}

bool PakReader::Contains(const std::string& path) const {
    uint64_t hash = XXH64(path.data(), path.size(), 0);
    return m_index.count(hash) > 0;
}

size_t PakReader::EntryCount() const {
    return m_index.size();
}

std::vector<uint64_t> PakReader::GetPathHashes() const {
    std::vector<uint64_t> hashes;
    hashes.reserve(m_index.size());
    for (const auto& [hash, _] : m_index) {
        hashes.push_back(hash);
    }
    return hashes;
}

void PakReader::Unload() {
    m_data.clear();
    m_index.clear();
    m_loaded = false;
}

} // namespace uz
