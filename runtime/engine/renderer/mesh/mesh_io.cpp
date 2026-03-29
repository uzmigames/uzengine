/// UzEngine Mesh Format — Binary File I/O (Load / Save)

#include "mesh.h"
#include "log.h"
#include "profiler.h"

#include "file_system.h"

#include <cstring>
#include <fstream>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// LoadMesh
// ─────────────────────────────────────────────────────────────────────────────

Mesh LoadMesh(const std::string& path) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        UZ_LOG(Renderer, Error, "Failed to open mesh file: path='{}'", path);
        return mesh;
    }

    // Read header
    file.read(reinterpret_cast<char*>(&mesh.header), sizeof(MeshHeader));
    if (!file || file.gcount() != sizeof(MeshHeader)) {
        UZ_LOG(Renderer, Error, "Truncated mesh header: path='{}'", path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Validate magic
    if (mesh.header.magic != kMeshMagic) {
        UZ_LOG(Renderer, Error,
               "Invalid mesh magic: expected=0x{:08X}, got=0x{:08X}, path='{}'",
               kMeshMagic, mesh.header.magic, path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Validate version
    if (mesh.header.version != kMeshVersion) {
        UZ_LOG(Renderer, Error,
               "Unsupported mesh version: expected={}, got={}, path='{}'",
               kMeshVersion, mesh.header.version, path);
        mesh.header.magic = 0;
        return mesh;
    }

    // Read vertex data
    uint64_t vertexSize = mesh.GetVertexDataSize();
    if (vertexSize > 0) {
        mesh.vertexData.resize(static_cast<size_t>(vertexSize));
        file.seekg(static_cast<std::streamoff>(mesh.header.vertexDataOffset));
        file.read(reinterpret_cast<char*>(mesh.vertexData.data()),
                  static_cast<std::streamsize>(vertexSize));
        if (!file) {
            UZ_LOG(Renderer, Error,
                   "Truncated vertex data: expected={} bytes, path='{}'",
                   vertexSize, path);
            mesh.header.magic = 0;
            return mesh;
        }
    }

    // Read index data
    uint64_t indexSize = mesh.GetIndexDataSize();
    if (indexSize > 0) {
        mesh.indexData.resize(static_cast<size_t>(indexSize));
        file.seekg(static_cast<std::streamoff>(mesh.header.indexDataOffset));
        file.read(reinterpret_cast<char*>(mesh.indexData.data()),
                  static_cast<std::streamsize>(indexSize));
        if (!file) {
            UZ_LOG(Renderer, Error,
                   "Truncated index data: expected={} bytes, path='{}'",
                   indexSize, path);
            mesh.header.magic = 0;
            return mesh;
        }
    }

    UZ_LOG(Renderer, Info,
           "Loaded mesh: vertices={}, indices={}, format={}, path='{}'",
           mesh.header.vertexCount, mesh.header.indexCount,
           static_cast<uint32_t>(mesh.header.vertexFormat), path);

    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshFromMemory
// ─────────────────────────────────────────────────────────────────────────────

Mesh LoadMeshFromMemory(const std::vector<uint8_t>& data, const std::string& name) {
    UZ_PROFILE_FUNCTION();
    Mesh mesh{};

    if (data.size() < sizeof(MeshHeader)) {
        UZ_LOG(Renderer, Error, "Truncated mesh data: size={}, name='{}'", data.size(), name);
        return mesh;
    }

    std::memcpy(&mesh.header, data.data(), sizeof(MeshHeader));

    if (mesh.header.magic != kMeshMagic) {
        UZ_LOG(Renderer, Error, "Invalid mesh magic: expected=0x{:08X}, got=0x{:08X}, name='{}'",
               kMeshMagic, mesh.header.magic, name);
        mesh.header.magic = 0;
        return mesh;
    }

    if (mesh.header.version != kMeshVersion && mesh.header.version != 1) {
        UZ_LOG(Renderer, Error, "Unsupported mesh version: expected={} or 1, got={}, name='{}'",
               kMeshVersion, mesh.header.version, name);
        mesh.header.magic = 0;
        return mesh;
    }
    // v1 compatibility: no LOD data in header, treat as single LOD
    if (mesh.header.version == 1) {
        mesh.header.lodCount = 1;
        std::memset(mesh.header.lods, 0, sizeof(mesh.header.lods));
    }

    const uint64_t vertexSize = mesh.GetVertexDataSize();
    if (vertexSize > 0 && mesh.header.vertexDataOffset + vertexSize <= data.size()) {
        mesh.vertexData.resize(static_cast<size_t>(vertexSize));
        std::memcpy(mesh.vertexData.data(),
                    data.data() + mesh.header.vertexDataOffset,
                    static_cast<size_t>(vertexSize));
    }

    const uint64_t indexSize = mesh.GetIndexDataSize();
    if (indexSize > 0 && mesh.header.indexDataOffset + indexSize <= data.size()) {
        mesh.indexData.resize(static_cast<size_t>(indexSize));
        std::memcpy(mesh.indexData.data(),
                    data.data() + mesh.header.indexDataOffset,
                    static_cast<size_t>(indexSize));
    }

    UZ_LOG(Renderer, Info, "Loaded mesh from memory: vertices={}, indices={}, name='{}'",
           mesh.header.vertexCount, mesh.header.indexCount, name);
    return mesh;
}

// ─────────────────────────────────────────────────────────────────────────────
// SaveMesh
// ─────────────────────────────────────────────────────────────────────────────

bool SaveMesh(const std::string& path, const Mesh& mesh) {
    UZ_PROFILE_FUNCTION();

    // Build a single contiguous buffer so FileWriteAll can write atomically.
    const size_t totalSize = sizeof(MeshHeader)
                           + mesh.vertexData.size()
                           + mesh.indexData.size();
    std::vector<uint8_t> buffer;
    buffer.resize(totalSize);

    size_t offset = 0;
    std::memcpy(buffer.data() + offset, &mesh.header, sizeof(MeshHeader));
    offset += sizeof(MeshHeader);

    if (!mesh.vertexData.empty()) {
        std::memcpy(buffer.data() + offset, mesh.vertexData.data(), mesh.vertexData.size());
        offset += mesh.vertexData.size();
    }
    if (!mesh.indexData.empty()) {
        std::memcpy(buffer.data() + offset, mesh.indexData.data(), mesh.indexData.size());
    }

    const FileResult result = FileWriteAll(path.c_str(), buffer.data(), buffer.size());
    if (result != FileResult::Success) {
        UZ_LOG(Renderer, Error, "Write failed for mesh file: path='{}': {}",
               path, FileResultToString(result));
        return false;
    }

    return true;
}

} // namespace uz
