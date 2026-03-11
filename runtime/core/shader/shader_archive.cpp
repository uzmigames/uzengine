/// UzEngine Shader Archive — implementation.

#include "shader_archive.h"
#include "uz_log.h"

#include <cstring>
#include <fstream>

UZ_DEFINE_LOG_CATEGORY(ShaderArchive, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// File format layout helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr size_t kHeaderSize = 4 * sizeof(uint32_t);  // magic + version + count + reserved
static constexpr size_t kEntrySize = kHashFieldSize + 2 * sizeof(uint64_t);  // hash + offset + size

// ─────────────────────────────────────────────────────────────────────────────
// Build — static factory, creates archive on disk
// ─────────────────────────────────────────────────────────────────────────────

bool ShaderArchive::Build(const std::filesystem::path& outputPath,
                          const std::vector<ShaderArchiveEntry>& entries) {
    std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        UZ_LOG(ShaderArchive, Error, "Failed to open '{}' for writing", outputPath.string());
        return false;
    }

    // Validate entries
    for (const auto& entry : entries) {
        if (entry.hash.empty()) {
            UZ_LOG(ShaderArchive, Error, "Entry has empty hash");
            return false;
        }
        if (entry.hash.size() >= kHashFieldSize) {
            UZ_LOG(ShaderArchive, Error, "Hash '{}' exceeds max length {}", entry.hash, kHashFieldSize - 1);
            return false;
        }
    }

    const auto count = static_cast<uint32_t>(entries.size());
    const size_t dataOffset = kHeaderSize + count * kEntrySize;

    // ── Header ───────────────────────────────────────────────────────────────
    const uint32_t magic = kShaderArchiveMagic;
    const uint32_t version = kShaderArchiveVersion;
    const uint32_t reserved = 0;

    file.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    file.write(reinterpret_cast<const char*>(&count), sizeof(count));
    file.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));

    // ── Entry table ──────────────────────────────────────────────────────────
    uint64_t currentDataOffset = dataOffset;
    for (const auto& entry : entries) {
        // Hash field — zero-filled, null-terminated
        char hashField[kHashFieldSize] = {};
        std::memcpy(hashField, entry.hash.c_str(), entry.hash.size());
        file.write(hashField, kHashFieldSize);

        // Offset and size
        const auto size = static_cast<uint64_t>(entry.bytecode.size());
        file.write(reinterpret_cast<const char*>(&currentDataOffset), sizeof(currentDataOffset));
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));

        currentDataOffset += size;
    }

    // ── Data section ─────────────────────────────────────────────────────────
    for (const auto& entry : entries) {
        if (!entry.bytecode.empty()) {
            file.write(reinterpret_cast<const char*>(entry.bytecode.data()),
                       static_cast<std::streamsize>(entry.bytecode.size()));
        }
    }

    file.flush();
    if (!file.good()) {
        UZ_LOG(ShaderArchive, Error, "Write error while building archive '{}'", outputPath.string());
        return false;
    }

    UZ_LOG(ShaderArchive, Info, "Built shader archive '{}' with {} entries",
           outputPath.string(), count);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Load — reads archive into memory
// ─────────────────────────────────────────────────────────────────────────────

bool ShaderArchive::Load(const std::filesystem::path& archivePath) {
    // Replace any previous state
    Unload();

    std::ifstream file(archivePath, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(ShaderArchive, Error, "Failed to open archive '{}'", archivePath.string());
        return false;
    }

    // ── Read header ──────────────────────────────────────────────────────────
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    uint32_t reserved = 0;

    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&count), sizeof(count));
    file.read(reinterpret_cast<char*>(&reserved), sizeof(reserved));

    if (!file.good()) {
        UZ_LOG(ShaderArchive, Error, "Failed to read header from '{}'", archivePath.string());
        return false;
    }

    if (magic != kShaderArchiveMagic) {
        UZ_LOG(ShaderArchive, Error, "Invalid magic 0x{:08X} in '{}', expected 0x{:08X}",
               magic, archivePath.string(), kShaderArchiveMagic);
        return false;
    }

    if (version != kShaderArchiveVersion) {
        UZ_LOG(ShaderArchive, Error, "Unsupported version {} in '{}', expected {}",
               version, archivePath.string(), kShaderArchiveVersion);
        return false;
    }

    // ── Read entry table ─────────────────────────────────────────────────────
    const size_t dataOffset = kHeaderSize + count * kEntrySize;

    for (uint32_t i = 0; i < count; ++i) {
        char hashField[kHashFieldSize] = {};
        uint64_t offset = 0;
        uint64_t size = 0;

        file.read(hashField, kHashFieldSize);
        file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
        file.read(reinterpret_cast<char*>(&size), sizeof(size));

        if (!file.good()) {
            UZ_LOG(ShaderArchive, Error, "Failed to read entry {} from '{}'", i, archivePath.string());
            Unload();
            return false;
        }

        std::string hash(hashField);

        // Store offset relative to data section start for m_data indexing
        LoadedEntry entry{};
        entry.offset = offset - dataOffset;
        entry.size = size;
        m_index[std::move(hash)] = entry;
    }

    // ── Read data section ────────────────────────────────────────────────────
    // Get remaining file size
    const auto currentPos = file.tellg();
    file.seekg(0, std::ios::end);
    const auto fileEnd = file.tellg();
    const auto dataSize = static_cast<size_t>(fileEnd - static_cast<std::streamoff>(dataOffset));

    if (dataSize > 0) {
        m_data.resize(dataSize);
        file.seekg(static_cast<std::streamoff>(dataOffset));
        file.read(reinterpret_cast<char*>(m_data.data()), static_cast<std::streamsize>(dataSize));

        if (!file.good()) {
            UZ_LOG(ShaderArchive, Error, "Failed to read data section from '{}'", archivePath.string());
            Unload();
            return false;
        }
    }

    m_loaded = true;
    UZ_LOG(ShaderArchive, Info, "Loaded shader archive '{}' with {} entries",
           archivePath.string(), count);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query methods
// ─────────────────────────────────────────────────────────────────────────────

void ShaderArchive::Unload() {
    m_index.clear();
    m_data.clear();
    m_data.shrink_to_fit();
    m_loaded = false;
}

std::vector<uint8_t> ShaderArchive::Lookup(const std::string& hash) const {
    const auto it = m_index.find(hash);
    if (it == m_index.end()) {
        return {};
    }

    const auto& entry = it->second;
    if (entry.offset + entry.size > m_data.size()) {
        UZ_LOG(ShaderArchive, Error, "Corrupt entry for hash '{}': offset={}, size={}, dataSize={}",
               hash, entry.offset, entry.size, m_data.size());
        return {};
    }

    return std::vector<uint8_t>(
        m_data.begin() + static_cast<std::ptrdiff_t>(entry.offset),
        m_data.begin() + static_cast<std::ptrdiff_t>(entry.offset + entry.size));
}

bool ShaderArchive::Contains(const std::string& hash) const {
    return m_index.find(hash) != m_index.end();
}

size_t ShaderArchive::EntryCount() const {
    return m_index.size();
}

std::vector<std::string> ShaderArchive::GetHashes() const {
    std::vector<std::string> hashes;
    hashes.reserve(m_index.size());
    for (const auto& [hash, entry] : m_index) {
        hashes.push_back(hash);
    }
    return hashes;
}

bool ShaderArchive::IsLoaded() const {
    return m_loaded;
}

} // namespace uz
