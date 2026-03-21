/// UzEngine .uzpak Archive — Implementation
///
/// Writer packs entries with 64-byte alignment and sorted index.
/// Reader loads into memory and provides O(1) hash-based lookup.

#include "pak_format.h"
#include "log.h"
#include "profiler.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "file_system.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
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
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    auto data = WritePak(entries);

    const FileResult result = FileWriteAll(path.c_str(), data.data(), data.size());
    if (result != FileResult::Success) {
        UZ_LOG(Asset, Error, "Cannot write pak file: path='{}': {}",
               path, FileResultToString(result));
        return false;
    }

    UZ_LOG(Asset, Info, "Wrote pak: path='{}', entries={}, size={} bytes",
           path, entries.size(), data.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Reader
// ─────────────────────────────────────────────────────────────────────────────

bool PakReader::Load(const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    auto data = FileReadAll(path.c_str());
    if (data.empty()) {
        UZ_LOG(Asset, Error, "Cannot open pak file: path='{}'", path);
        return false;
    }

    return Load(data);
}

std::vector<uint8_t> PakReader::Lookup(const std::string& path) const {
    UZ_PROFILE_FUNCTION();
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

// ─────────────────────────────────────────────────────────────────────────────
// XOR Scramble
// ─────────────────────────────────────────────────────────────────────────────

/// Applies a cyclic 8-byte XOR scramble in-place.
/// The key is derived from `pathHash ^ secret`, providing per-asset variation.
/// The same function is used for both scramble and descramble (XOR is its own inverse).
static void XorScramble(uint8_t* data, size_t size, uint64_t pathHash, uint64_t secret) {
    const uint64_t key = pathHash ^ secret;
    uint8_t keyBytes[8];
    std::memcpy(keyBytes, &key, sizeof(keyBytes));

    for (size_t i = 0; i < size; ++i) {
        data[i] ^= keyBytes[i & 7];
    }
}

std::vector<uint8_t> PakCompressBlob(
    const std::vector<uint8_t>& raw, uint64_t pathHash, uint64_t secret) {
    UZ_PROFILE_FUNCTION();

    if (raw.empty()) return {};

    // XOR scramble only -- LZ4 compression requires runtime/thirdparty/lz4/
    std::vector<uint8_t> out = raw;
    XorScramble(out.data(), out.size(), pathHash, secret);
    return out;
}

std::vector<uint8_t> PakDecompressBlob(
    const uint8_t* data, size_t compressedSize, size_t rawSize,
    uint64_t pathHash, uint64_t secret) {
    UZ_PROFILE_FUNCTION();

    if (data == nullptr || compressedSize == 0) return {};

    // XOR descramble only -- sizes must match when no LZ4 compression is active
    if (compressedSize != rawSize) {
        UZ_LOG(Asset, Error,
               "PakDecompressBlob: size mismatch without LZ4 (compressed={}, raw={})",
               compressedSize, rawSize);
        return {};
    }

    std::vector<uint8_t> out(data, data + compressedSize);
    XorScramble(out.data(), out.size(), pathHash, secret);
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Master Index Writer
// ─────────────────────────────────────────────────────────────────────────────

bool WriteMasterIndex(const std::string& path,
                      const std::vector<std::string>& pakFilenames,
                      const std::vector<MasterAssetEntry>& entries) {
    UZ_PROFILE_FUNCTION();

    // Validate pak filenames fit within the fixed-size buffer
    for (size_t i = 0; i < pakFilenames.size(); ++i) {
        if (pakFilenames[i].size() >= kMaxPakFilename) {
            UZ_LOG(Asset, Error,
                   "WriteMasterIndex: pak filename too long ({} >= {} limit): '{}'",
                   pakFilenames[i].size(), kMaxPakFilename, pakFilenames[i]);
            return false;
        }
    }

    // Validate all entries reference valid pakIds
    for (const auto& entry : entries) {
        if (entry.pakId >= pakFilenames.size()) {
            UZ_LOG(Asset, Error,
                   "WriteMasterIndex: entry has pakId={} but only {} paks registered",
                   entry.pakId, pakFilenames.size());
            return false;
        }
    }

    // Sort entries by pathHash for binary search compatibility
    std::vector<MasterAssetEntry> sorted = entries;
    std::sort(sorted.begin(), sorted.end(),
              [](const MasterAssetEntry& a, const MasterAssetEntry& b) {
                  return a.pathHash < b.pathHash;
              });

    // Calculate layout: header + entries + pak table + trailer
    const size_t headerSize = sizeof(MasterIndexHeader);
    const size_t entryTableOffset = headerSize;
    const size_t entryTableSize = sorted.size() * sizeof(MasterAssetEntry);
    const size_t pakTableOffset = entryTableOffset + entryTableSize;
    const size_t pakTableSize = pakFilenames.size() * sizeof(MasterPakTableEntry);
    const size_t trailerOffset = pakTableOffset + pakTableSize;
    const size_t totalSize = trailerOffset + sizeof(MasterIndexTrailer);

    std::vector<uint8_t> buffer(totalSize, 0);

    // Write header
    MasterIndexHeader header;
    header.pakCount = static_cast<uint32_t>(pakFilenames.size());
    header.entryCount = static_cast<uint32_t>(sorted.size());
    std::memcpy(buffer.data(), &header, sizeof(header));

    // Write asset entries (sorted)
    for (size_t i = 0; i < sorted.size(); ++i) {
        std::memcpy(buffer.data() + entryTableOffset + i * sizeof(MasterAssetEntry),
                    &sorted[i], sizeof(MasterAssetEntry));
    }

    // Write pak table
    for (size_t i = 0; i < pakFilenames.size(); ++i) {
        MasterPakTableEntry pakEntry;
        pakEntry.pakId = static_cast<uint32_t>(i);
        // Safe: validated above that filename fits within kMaxPakFilename
        std::strncpy(pakEntry.filename, pakFilenames[i].c_str(), kMaxPakFilename - 1);
        pakEntry.filename[kMaxPakFilename - 1] = '\0';
        pakEntry.checksum = 0; // Populated by the caller or during pak writing
        std::memcpy(buffer.data() + pakTableOffset + i * sizeof(MasterPakTableEntry),
                    &pakEntry, sizeof(MasterPakTableEntry));
    }

    // Write trailer
    MasterIndexTrailer trailer;
    trailer.pakTableOffset = pakTableOffset;
    trailer.entryTableOffset = entryTableOffset;
    std::memcpy(buffer.data() + trailerOffset, &trailer, sizeof(trailer));

    // Write to disk
    const FileResult result = FileWriteAll(path.c_str(), buffer.data(), buffer.size());
    if (result != FileResult::Success) {
        UZ_LOG(Asset, Error, "WriteMasterIndex: cannot write '{}': {}",
               path, FileResultToString(result));
        return false;
    }

    UZ_LOG(Asset, Info, "Wrote master index: path='{}', paks={}, entries={}, size={} bytes",
           path, pakFilenames.size(), sorted.size(), buffer.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Pak Split Writer
// ─────────────────────────────────────────────────────────────────────────────

PakSplitResult WritePakSplit(
    const std::vector<PakWriteEntry>& entries,
    const std::string& outputDir,
    const std::string& baseName,
    size_t maxPakSizeBytes,
    uint64_t secret) {
    UZ_PROFILE_FUNCTION();

    PakSplitResult result;

    // Clamp minimum pak size to header + trailer so at least one blob can fit
    // (header 16 + trailer 16 = 32, plus alignment padding for the first blob).
    const size_t minPakSize = sizeof(PakHeader) + sizeof(PakTrailer) + kPakAlignment;
    if (maxPakSizeBytes < minPakSize) {
        maxPakSizeBytes = minPakSize;
    }

    // Handle empty input: write an empty master index with no paks
    if (entries.empty()) {
        const std::string indexPath = outputDir + "/" + baseName + ".uzpakindex";
        const std::vector<std::string> noPaks;
        const std::vector<MasterAssetEntry> noEntries;
        if (!WriteMasterIndex(indexPath, noPaks, noEntries)) {
            UZ_LOG(Asset, Error, "WritePakSplit: failed to write empty master index '{}'",
                   indexPath);
            return result;
        }
        result.success = true;
        result.pakCount = 0;
        result.indexPath = indexPath;
        UZ_LOG(Asset, Info, "WritePakSplit: empty input — wrote empty index '{}'", indexPath);
        return result;
    }

    // Accumulators for the current pak being built
    std::vector<uint8_t> pakBuffer;
    size_t currentPakSize = sizeof(PakHeader);
    uint32_t currentPakId = 0;
    uint32_t entriesInCurrentPak = 0;

    // Reserve the header space in the first pak buffer
    pakBuffer.resize(sizeof(PakHeader), 0);

    // Collected data across all paks
    std::vector<MasterAssetEntry> masterEntries;
    masterEntries.reserve(entries.size());

    std::vector<std::vector<uint8_t>> pakBuffers; // completed pak buffers ready to write
    std::vector<uint32_t> pakEntryCounts;         // entry count per completed pak

    // Lambda: finalize the current pak buffer (append trailer) and stash it
    auto finalizePak = [&]() {
        // Write the header at the front of the buffer with the correct entry count and flags
        PakHeader header;
        header.entryCount = entriesInCurrentPak;
        header.flags = (secret != 0) ? kPakFlagScrambled : 0;
        std::memcpy(pakBuffer.data(), &header, sizeof(header));

        // Append trailer — indexOffset = 0 (no local index; master index handles lookups)
        PakTrailer trailer;
        trailer.indexOffset = 0;
        const size_t trailerPos = pakBuffer.size();
        pakBuffer.resize(trailerPos + sizeof(PakTrailer), 0);
        std::memcpy(pakBuffer.data() + trailerPos, &trailer, sizeof(trailer));

        pakBuffers.push_back(std::move(pakBuffer));
        pakEntryCounts.push_back(entriesInCurrentPak);

        // Reset for next pak
        pakBuffer.clear();
        pakBuffer.resize(sizeof(PakHeader), 0);
        currentPakSize = sizeof(PakHeader);
        entriesInCurrentPak = 0;
        ++currentPakId;
    };

    // Pack each entry
    for (const auto& entry : entries) {
        const uint64_t pathHash = XXH64(entry.path.data(), entry.path.size(), 0);
        const uint64_t contentHash = XXH64(entry.data.data(), entry.data.size(), 0);

        // Scramble the blob
        const std::vector<uint8_t> compressedBlob =
            PakCompressBlob(entry.data, pathHash, secret);

        // Where the blob would land in the current pak
        const size_t alignedOffset = AlignUp(currentPakSize, kPakAlignment);
        const size_t blobEnd = alignedOffset + compressedBlob.size();

        // If this blob would exceed the max pak size and the current pak already
        // has at least one entry, finalize the current pak and start a new one.
        if (blobEnd > maxPakSizeBytes && entriesInCurrentPak > 0) {
            finalizePak();
        }

        // Recalculate offset after potential pak rollover
        const size_t finalAlignedOffset = AlignUp(currentPakSize, kPakAlignment);
        const size_t finalBlobEnd = finalAlignedOffset + compressedBlob.size();

        // Ensure the buffer is large enough and zero-fill alignment padding
        if (pakBuffer.size() < finalBlobEnd) {
            pakBuffer.resize(finalBlobEnd, 0);
        }

        // Copy the scrambled blob into the pak buffer at the aligned offset
        if (!compressedBlob.empty()) {
            std::memcpy(pakBuffer.data() + finalAlignedOffset,
                        compressedBlob.data(), compressedBlob.size());
        }

        // Record the master index entry pointing into this pak
        MasterAssetEntry masterEntry;
        masterEntry.pathHash = pathHash;
        masterEntry.pakId = currentPakId;
        masterEntry.typeId = entry.typeId;
        masterEntry.offset = finalAlignedOffset;
        masterEntry.compressedSize = compressedBlob.size();
        masterEntry.rawSize = entry.data.size();
        masterEntry.contentHash = contentHash;
        masterEntries.push_back(masterEntry);

        currentPakSize = finalBlobEnd;
        ++entriesInCurrentPak;

        result.totalRawBytes += entry.data.size();
        result.totalPackedBytes += compressedBlob.size();
    }

    // Finalize the last pak if it has any entries
    if (entriesInCurrentPak > 0) {
        finalizePak();
    }

    const uint32_t totalPaks = currentPakId;

    // Build pak filenames and write each pak to disk
    std::vector<std::string> pakFilenames;
    pakFilenames.reserve(totalPaks);

    for (uint32_t i = 0; i < totalPaks; ++i) {
        char pakName[512];
        std::snprintf(pakName, sizeof(pakName), "%s_%03u.uzpak", baseName.c_str(), i + 1);
        pakFilenames.emplace_back(pakName);

        const std::string pakPath = outputDir + "/" + pakFilenames.back();
        const FileResult writeResult =
            FileWriteAll(pakPath.c_str(), pakBuffers[i].data(), pakBuffers[i].size());
        if (writeResult != FileResult::Success) {
            UZ_LOG(Asset, Error, "WritePakSplit: failed to write pak '{}': {}",
                   pakPath, FileResultToString(writeResult));
            return result;
        }

        UZ_LOG(Asset, Info, "WritePakSplit: wrote pak '{}' ({} entries, {} bytes)",
               pakPath, pakEntryCounts[i], pakBuffers[i].size());
    }

    // Write the master index
    const std::string indexPath = outputDir + "/" + baseName + ".uzpakindex";
    if (!WriteMasterIndex(indexPath, pakFilenames, masterEntries)) {
        UZ_LOG(Asset, Error, "WritePakSplit: failed to write master index '{}'", indexPath);
        return result;
    }

    result.success = true;
    result.pakCount = totalPaks;
    result.indexPath = indexPath;

    UZ_LOG(Asset, Info,
           "WritePakSplit: complete — {} assets, {} paks, raw={} bytes, packed={} bytes, index='{}'",
           entries.size(), totalPaks, result.totalRawBytes, result.totalPackedBytes, indexPath);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Master Index Reader
// ─────────────────────────────────────────────────────────────────────────────

bool MasterIndex::LoadFile(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    Unload();

    auto data = FileReadAll(path.c_str());
    if (data.empty()) {
        UZ_LOG(Asset, Error, "MasterIndex: cannot read file: '{}'", path);
        return false;
    }

    const size_t minSize = sizeof(MasterIndexHeader) + sizeof(MasterIndexTrailer);
    if (data.size() < minSize) {
        UZ_LOG(Asset, Error, "MasterIndex: file too small ({} bytes, need >= {}): '{}'",
               data.size(), minSize, path);
        return false;
    }

    // Read and validate header
    MasterIndexHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.magic != kMasterIndexMagic) {
        UZ_LOG(Asset, Error, "MasterIndex: invalid magic 0x{:08X} (expected 0x{:08X}): '{}'",
               header.magic, kMasterIndexMagic, path);
        return false;
    }

    if (header.version != kMasterIndexVersion) {
        UZ_LOG(Asset, Error, "MasterIndex: unsupported version {} (expected {}): '{}'",
               header.version, kMasterIndexVersion, path);
        return false;
    }

    // Read trailer from the end of the file
    MasterIndexTrailer trailer;
    std::memcpy(&trailer, data.data() + data.size() - sizeof(MasterIndexTrailer),
                sizeof(trailer));

    if (trailer.magic != kMasterIndexMagic) {
        UZ_LOG(Asset, Error, "MasterIndex: trailer magic mismatch: '{}'", path);
        return false;
    }

    // Validate table offsets are within file bounds
    const size_t entryTableSize = header.entryCount * sizeof(MasterAssetEntry);
    if (trailer.entryTableOffset + entryTableSize > data.size()) {
        UZ_LOG(Asset, Error, "MasterIndex: entry table exceeds file bounds: '{}'", path);
        return false;
    }

    const size_t pakTableSize = header.pakCount * sizeof(MasterPakTableEntry);
    if (trailer.pakTableOffset + pakTableSize > data.size()) {
        UZ_LOG(Asset, Error, "MasterIndex: pak table exceeds file bounds: '{}'", path);
        return false;
    }

    // Read pak table
    m_pakFilenames.reserve(header.pakCount);
    for (uint32_t i = 0; i < header.pakCount; ++i) {
        MasterPakTableEntry pakEntry;
        std::memcpy(&pakEntry,
                    data.data() + trailer.pakTableOffset + i * sizeof(MasterPakTableEntry),
                    sizeof(pakEntry));
        // Guarantee null termination
        pakEntry.filename[kMaxPakFilename - 1] = '\0';
        m_pakFilenames.emplace_back(pakEntry.filename);
    }

    // Read asset entries and build hash map
    m_assets.reserve(header.entryCount);
    for (uint32_t i = 0; i < header.entryCount; ++i) {
        MasterAssetEntry entry;
        std::memcpy(&entry,
                    data.data() + trailer.entryTableOffset + i * sizeof(MasterAssetEntry),
                    sizeof(entry));

        if (entry.pakId >= m_pakFilenames.size()) {
            UZ_LOG(Asset, Warning,
                   "MasterIndex: entry has pakId={} but only {} paks (skipping)",
                   entry.pakId, m_pakFilenames.size());
            continue;
        }

        AssetLocation loc;
        loc.pakFilename = m_pakFilenames[entry.pakId];
        loc.offset = entry.offset;
        loc.compressedSize = entry.compressedSize;
        loc.rawSize = entry.rawSize;
        loc.contentHash = entry.contentHash;
        loc.typeId = entry.typeId;
        m_assets[entry.pathHash] = std::move(loc);
    }

    m_loaded = true;
    UZ_LOG(Asset, Info, "Loaded master index: path='{}', paks={}, entries={}",
           path, m_pakFilenames.size(), m_assets.size());
    return true;
}

std::optional<MasterIndex::AssetLocation> MasterIndex::Lookup(uint64_t pathHash) const {
    auto it = m_assets.find(pathHash);
    if (it == m_assets.end()) return std::nullopt;
    return it->second;
}

bool MasterIndex::Contains(uint64_t pathHash) const {
    return m_assets.count(pathHash) > 0;
}

size_t MasterIndex::EntryCount() const {
    return m_assets.size();
}

size_t MasterIndex::PakCount() const {
    return m_pakFilenames.size();
}

const std::vector<std::string>& MasterIndex::PakFilenames() const {
    return m_pakFilenames;
}

void MasterIndex::Unload() {
    m_assets.clear();
    m_pakFilenames.clear();
    m_loaded = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// PakStreamReader
// ─────────────────────────────────────────────────────────────────────────────

PakStreamReader::~PakStreamReader() {
    Close();
}

PakStreamReader::PakStreamReader(PakStreamReader&& other) noexcept
    : m_path(std::move(other.m_path))
    , m_file(other.m_file) {
    other.m_file = nullptr;
}

PakStreamReader& PakStreamReader::operator=(PakStreamReader&& other) noexcept {
    if (this != &other) {
        Close();
        m_path = std::move(other.m_path);
        m_file = other.m_file;
        other.m_file = nullptr;
    }
    return *this;
}

bool PakStreamReader::Open(const std::string& pakPath) {
    UZ_PROFILE_FUNCTION();
    Close();

#ifdef _WIN32
    // Use _wfopen for better Windows path support when available,
    // but fopen handles ASCII paths correctly on all platforms.
    m_file = std::fopen(pakPath.c_str(), "rb");
#else
    m_file = std::fopen(pakPath.c_str(), "rb");
#endif

    if (m_file == nullptr) {
        UZ_LOG(Asset, Error, "PakStreamReader: cannot open '{}'", pakPath);
        return false;
    }

    m_path = pakPath;
    return true;
}

std::vector<uint8_t> PakStreamReader::ReadBlob(uint64_t offset, uint64_t size) {
    UZ_PROFILE_FUNCTION();

    if (m_file == nullptr) {
        UZ_LOG(Asset, Error, "PakStreamReader: ReadBlob called on closed reader");
        return {};
    }

    if (size == 0) return {};

    // Seek to the blob offset
#ifdef _WIN32
    if (_fseeki64(m_file, static_cast<int64_t>(offset), SEEK_SET) != 0) {
#else
    if (fseeko(m_file, static_cast<off_t>(offset), SEEK_SET) != 0) {
#endif
        UZ_LOG(Asset, Error, "PakStreamReader: seek failed at offset {} in '{}'",
               offset, m_path);
        return {};
    }

    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    const size_t bytesRead = std::fread(buffer.data(), 1, buffer.size(), m_file);
    if (bytesRead != buffer.size()) {
        UZ_LOG(Asset, Error,
               "PakStreamReader: partial read ({}/{} bytes at offset {}) in '{}'",
               bytesRead, size, offset, m_path);
        return {};
    }

    return buffer;
}

void PakStreamReader::Close() {
    if (m_file != nullptr) {
        std::fclose(m_file);
        m_file = nullptr;
    }
    m_path.clear();
}

bool PakStreamReader::IsOpen() const {
    return m_file != nullptr;
}

} // namespace uz
