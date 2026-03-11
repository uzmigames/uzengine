/// UzEngine Asset Manifest — Implementation
///
/// Writer: converts string paths to xxHash64, sorts entries by hash for
/// binary search, and builds a flat dependency table where each entry's
/// deps are a contiguous slice of uint64 hashes.
///
/// Reader: loads the binary in one shot, parses entries + dep table into
/// a QueryResult vector sorted by pathHash for O(log n) binary search.

#include "asset_manifest.h"
#include "log.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifestWriter
// ─────────────────────────────────────────────────────────────────────────────

void AssetManifestWriter::AddEntry(const ManifestWriteEntry& entry) {
    m_entries.push_back(entry);
}

std::vector<uint8_t> AssetManifestWriter::Serialize() const {
    // Intermediate representation with pre-hashed paths for sorting
    struct HashedEntry {
        uint64_t pathHash;
        uint64_t size;
        uint32_t typeId;
        std::vector<uint64_t> depHashes;
    };

    std::vector<HashedEntry> hashed;
    hashed.reserve(m_entries.size());

    for (const auto& e : m_entries) {
        HashedEntry h;
        h.pathHash = XXH64(e.path.data(), e.path.size(), 0);
        h.size = e.size;
        h.typeId = static_cast<uint32_t>(e.type);
        for (const auto& dep : e.dependencies) {
            h.depHashes.push_back(XXH64(dep.data(), dep.size(), 0));
        }
        hashed.push_back(std::move(h));
    }

    std::sort(hashed.begin(), hashed.end(),
              [](const HashedEntry& a, const HashedEntry& b) {
                  return a.pathHash < b.pathHash;
              });

    // Deps are stored as a flat byte array; each entry records its offset+count into it
    std::vector<uint8_t> depTable;
    std::vector<ManifestEntry> entries;
    entries.reserve(hashed.size());

    for (const auto& h : hashed) {
        ManifestEntry me;
        me.pathHash = h.pathHash;
        me.size = h.size;
        me.typeId = h.typeId;
        me.depCount = static_cast<uint32_t>(h.depHashes.size());

        if (me.depCount > 0) {
            me.depOffset = static_cast<uint32_t>(depTable.size());
            for (uint64_t depHash : h.depHashes) {
                const auto* bytes = reinterpret_cast<const uint8_t*>(&depHash);
                depTable.insert(depTable.end(), bytes, bytes + sizeof(uint64_t));
            }
        } else {
            me.depOffset = 0;
        }

        entries.push_back(me);
    }

    // Final layout: [header][sorted entries][dep table] — contiguous, no padding
    ManifestHeader header;
    header.entryCount = static_cast<uint32_t>(entries.size());

    size_t totalSize = sizeof(ManifestHeader)
                     + entries.size() * sizeof(ManifestEntry)
                     + depTable.size();

    std::vector<uint8_t> result(totalSize);
    size_t offset = 0;

    std::memcpy(result.data() + offset, &header, sizeof(header));
    offset += sizeof(header);

    if (!entries.empty()) {
        std::memcpy(result.data() + offset, entries.data(),
                     entries.size() * sizeof(ManifestEntry));
        offset += entries.size() * sizeof(ManifestEntry);
    }

    if (!depTable.empty()) {
        std::memcpy(result.data() + offset, depTable.data(), depTable.size());
    }

    UZ_LOG(Asset, Debug, "Manifest serialized: entries={}, depTable={} bytes",
           entries.size(), depTable.size());

    return result;
}

bool WriteManifestFile(const std::string& path, const AssetManifestWriter& writer) {
    auto data = writer.Serialize();

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot write manifest: path='{}'", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()),
               static_cast<std::streamsize>(data.size()));
    file.close();

    UZ_LOG(Asset, Debug, "Wrote manifest: path='{}', size={} bytes", path, data.size());
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// AssetManifest — runtime loader with binary search
// ─────────────────────────────────────────────────────────────────────────────

bool AssetManifest::Load(const std::vector<uint8_t>& data) {
    m_entries.clear();
    m_loaded = false;

    if (data.size() < sizeof(ManifestHeader)) {
        UZ_LOG(Asset, Error, "Manifest too small: {} bytes (need >= {})",
               data.size(), sizeof(ManifestHeader));
        return false;
    }

    ManifestHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.magic != kManifestMagic) {
        UZ_LOG(Asset, Error, "Manifest bad magic: 0x{:08X} (expected 0x{:08X})",
               header.magic, kManifestMagic);
        return false;
    }

    if (header.version != kManifestVersion) {
        UZ_LOG(Asset, Error, "Manifest version mismatch: {} (expected {})",
               header.version, kManifestVersion);
        return false;
    }

    size_t entriesStart = sizeof(ManifestHeader);
    size_t entriesSize = header.entryCount * sizeof(ManifestEntry);

    if (data.size() < entriesStart + entriesSize) {
        UZ_LOG(Asset, Error, "Manifest truncated: need {} bytes for {} entries, have {}",
               entriesStart + entriesSize, header.entryCount, data.size());
        return false;
    }

    const auto* rawEntries = reinterpret_cast<const ManifestEntry*>(
        data.data() + entriesStart);

    // Dep table is whatever remains after header + entries (may be empty)
    size_t depTableStart = entriesStart + entriesSize;
    size_t depTableSize = data.size() - depTableStart;
    const uint8_t* depTablePtr = (depTableSize > 0)
                                 ? (data.data() + depTableStart)
                                 : nullptr;

    m_entries.reserve(header.entryCount);

    for (uint32_t i = 0; i < header.entryCount; ++i) {
        const ManifestEntry& me = rawEntries[i];
        QueryResult qr;
        qr.pathHash = me.pathHash;
        qr.size = me.size;
        qr.type = static_cast<AssetType>(me.typeId);

        if (me.depCount > 0 && depTablePtr != nullptr) {
            size_t depEnd = me.depOffset + me.depCount * sizeof(uint64_t);
            if (depEnd <= depTableSize) {
                qr.dependencies.resize(me.depCount);
                std::memcpy(qr.dependencies.data(),
                           depTablePtr + me.depOffset,
                           me.depCount * sizeof(uint64_t));
            }
        }

        m_entries.push_back(std::move(qr));
    }

    m_loaded = true;
    UZ_LOG(Asset, Debug, "Manifest loaded: entries={}", m_entries.size());
    return true;
}

bool AssetManifest::LoadFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        UZ_LOG(Asset, Error, "Cannot open manifest: path='{}'", path);
        return false;
    }

    auto size = file.tellg();
    if (size <= 0) {
        UZ_LOG(Asset, Error, "Empty manifest file: path='{}'", path);
        return false;
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return Load(data);
}

const AssetManifest::QueryResult* AssetManifest::Find(const std::string& path) const {
    uint64_t hash = XXH64(path.data(), path.size(), 0);
    return FindByHash(hash);
}

const AssetManifest::QueryResult* AssetManifest::FindByHash(uint64_t pathHash) const {
    // Entries are sorted ascending by pathHash — use lower_bound for O(log n)
    auto it = std::lower_bound(
        m_entries.begin(), m_entries.end(), pathHash,
        [](const QueryResult& entry, uint64_t hash) {
            return entry.pathHash < hash;
        });

    if (it != m_entries.end() && it->pathHash == pathHash) {
        return &(*it);
    }
    return nullptr;
}

bool AssetManifest::Exists(const std::string& path) const {
    return Find(path) != nullptr;
}

} // namespace uz
