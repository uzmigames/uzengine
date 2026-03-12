/// UzEngine Virtual File System — Implementation
///
/// Implements three IVFS backends:
/// - LooseFileVFS: disk I/O via std::ifstream + std::filesystem
/// - PakVFS: delegates to PakReader for hash-based archive lookup
/// - LayeredVFS: iterates mount stack in priority order

#include "vfs.h"
#include "log.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LooseFileVFS — disk-based file access for development mode
// ─────────────────────────────────────────────────────────────────────────────

LooseFileVFS::LooseFileVFS(const std::string& rootDir)
    : m_rootDir(rootDir) {
    // Ensure trailing slash so concatenation works: "root/" + "path"
    if (!m_rootDir.empty() && m_rootDir.back() != '/' && m_rootDir.back() != '\\') {
        m_rootDir += '/';
    }
    UZ_LOG(Asset, Debug, "LooseFileVFS mounted: root='{}'", m_rootDir);
}

std::string LooseFileVFS::ResolvePath(const std::string& path) const {
    return m_rootDir + path;
}

std::vector<uint8_t> LooseFileVFS::Read(const std::string& path) {
    std::string fullPath = ResolvePath(path);
    std::ifstream file(fullPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }

    auto size = file.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);

    if (!file.good()) {
        UZ_LOG(Asset, Error, "LooseFileVFS: read failed after {} bytes: path='{}'",
               data.size(), fullPath);
        return {};
    }

    return data;
}

bool LooseFileVFS::Exists(const std::string& path) {
    std::string fullPath = ResolvePath(path);
    return fs::exists(fullPath) && fs::is_regular_file(fullPath);
}

std::string LooseFileVFS::Name() const {
    return "LooseFileVFS(" + m_rootDir + ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// PakVFS — .uzpak archive access for shipping builds
// ─────────────────────────────────────────────────────────────────────────────

PakVFS::PakVFS(PakReader reader)
    : m_reader(std::move(reader))
    , m_name("PakVFS") {
    UZ_LOG(Asset, Debug, "PakVFS mounted: entries={}", m_reader.EntryCount());
}

std::unique_ptr<PakVFS> PakVFS::FromFile(const std::string& path) {
    PakReader reader;
    if (!reader.LoadFile(path)) {
        UZ_LOG(Asset, Error, "PakVFS: failed to load archive: path='{}'", path);
        return nullptr;
    }

    auto vfs = std::make_unique<PakVFS>(std::move(reader));
    vfs->m_name = "PakVFS(" + path + ")";
    return vfs;
}

std::vector<uint8_t> PakVFS::Read(const std::string& path) {
    return m_reader.Lookup(path);
}

bool PakVFS::Exists(const std::string& path) {
    return m_reader.Contains(path);
}

std::string PakVFS::Name() const {
    return m_name;
}

// ─────────────────────────────────────────────────────────────────────────────
// LayeredVFS — priority-based mount stack for content layering
// ─────────────────────────────────────────────────────────────────────────────

void LayeredVFS::Mount(std::shared_ptr<IVFS> vfs, int priority) {
    if (!vfs) {
        UZ_LOG(Asset, Error, "LayeredVFS: cannot mount null VFS backend");
        return;
    }

    UZ_LOG(Asset, Debug, "LayeredVFS: mounting '{}' at priority {}",
           vfs->Name(), priority);

    m_mounts.push_back({std::move(vfs), priority});

    // Re-sort so highest priority is first in iteration order
    std::sort(m_mounts.begin(), m_mounts.end(),
              [](const MountPoint& a, const MountPoint& b) {
                  return a.priority > b.priority;
              });
}

void LayeredVFS::UnmountAll() {
    m_mounts.clear();
    UZ_LOG(Asset, Debug, "LayeredVFS: all mounts cleared");
}

size_t LayeredVFS::MountCount() const {
    return m_mounts.size();
}

std::vector<uint8_t> LayeredVFS::Read(const std::string& path) {
    for (auto& mount : m_mounts) {
        auto data = mount.vfs->Read(path);
        if (!data.empty()) {
            return data;
        }
    }
    return {};
}

bool LayeredVFS::Exists(const std::string& path) {
    for (auto& mount : m_mounts) {
        if (mount.vfs->Exists(path)) {
            return true;
        }
    }
    return false;
}

std::string LayeredVFS::Name() const {
    return "LayeredVFS(" + std::to_string(m_mounts.size()) + " mounts)";
}

} // namespace uz
