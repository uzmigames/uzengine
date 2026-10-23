/// UzEngine Mesh Format — Binary File I/O (Load / Save)

#include "mesh.h"
#include "log.h"
#include "profiler.h"
#include "uzmesh_format.h"

#include "file_system.h"

#include <cfloat>
#include <cstring>
#include <fstream>

namespace uz {

namespace {

/// Returns true when `[offset, offset + size)` lies within `limit` bytes.
/// Subtraction form: file-controlled `offset + size` can wrap past UINT64_MAX.
[[nodiscard]] bool MeshRangeInBounds(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

} // anonymous namespace

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

    // Validate magic — try UZMS fallback for skinned meshes
    if (mesh.header.magic != kMeshMagic) {
        if (mesh.header.magic == kUzMeshMagic) {
            // Skinned mesh (UZMS) — load entire file and delegate to LoadMeshFromMemory
            file.seekg(0, std::ios::end);
            const auto fileSize = file.tellg();
            file.seekg(0);
            std::vector<uint8_t> allData(static_cast<size_t>(fileSize));
            file.read(reinterpret_cast<char*>(allData.data()), fileSize);
            return LoadMeshFromMemory(allData, path);
        }
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

    // Header offsets/sizes are file-controlled: bound them by the real file size
    // (wrap-safe subtraction form) before allocating buffers for them.
    file.seekg(0, std::ios::end);
    const std::streamoff fileEnd = file.tellg();
    if (!file || fileEnd < 0) {
        UZ_LOG(Renderer, Error, "Cannot determine mesh file size: path='{}'", path);
        mesh.header.magic = 0;
        return mesh;
    }
    const auto fileSize = static_cast<uint64_t>(fileEnd);

    // Read vertex data
    uint64_t vertexSize = mesh.GetVertexDataSize();
    if (!MeshRangeInBounds(mesh.header.vertexDataOffset, vertexSize, fileSize)) {
        UZ_LOG(Renderer, Error,
               "Truncated vertex data: offset={}, expected={} bytes, file={} bytes, path='{}'",
               mesh.header.vertexDataOffset, vertexSize, fileSize, path);
        mesh.header.magic = 0;
        return mesh;
    }
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
    if (!MeshRangeInBounds(mesh.header.indexDataOffset, indexSize, fileSize)) {
        UZ_LOG(Renderer, Error,
               "Truncated index data: offset={}, expected={} bytes, file={} bytes, path='{}'",
               mesh.header.indexDataOffset, indexSize, fileSize, path);
        mesh.header.magic = 0;
        return mesh;
    }
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
        // ── UZMS fallback: convert skinned mesh to static for rendering ──
        // Skinned meshes use UZMS format (magic 0x534D5A55). Strip bone data
        // from each vertex (last 8 bytes) to convert SkinnedVertex (56B) → Vertex (48B).
        uint32_t probe = 0;
        if (data.size() >= 4) std::memcpy(&probe, data.data(), 4);
        if (probe == kUzMeshMagic) {
            auto uzResult = ReadUzMesh(data);
            if (!uzResult.success) {
                UZ_LOG(Renderer, Error, "Failed to parse UZMS: {}, name='{}'",
                       uzResult.error, name);
                mesh.header.magic = 0;
                return mesh;
            }
            const auto& uz = uzResult.file;
            const uint32_t srcStride = uz.header.vertexStride;
            const uint32_t dstStride = sizeof(Vertex);
            const uint32_t vertCount = uz.header.vertexCount;

            // The static stream is read as `Vertex` (48 B) per vertex below; a
            // narrower source stride would read past the parsed vertex data.
            if (vertCount > 0 && srcStride < dstStride) {
                UZ_LOG(Renderer, Error,
                       "UZMS vertexStride {} is smaller than the {}-byte Vertex layout, name='{}'",
                       srcStride, dstStride, name);
                mesh.header.magic = 0;
                return mesh;
            }

            // Strip bone data: copy first 48 bytes per vertex into the static
            // bind-pose stream, but keep a full 56 B copy in `skinnedVertexData`
            // so the GPU skinning compute shader can read the original stream.
            if (srcStride > dstStride && vertCount > 0) {
                mesh.vertexData.resize(static_cast<size_t>(vertCount) * dstStride);
                for (uint32_t i = 0; i < vertCount; ++i) {
                    std::memcpy(mesh.vertexData.data() + static_cast<size_t>(i) * dstStride,
                                uz.vertexData.data() + static_cast<size_t>(i) * srcStride,
                                dstStride);
                }
                mesh.skinnedVertexData = uz.vertexData;
                mesh.skeletonPath      = uz.skeletonPath;
            } else {
                mesh.vertexData = uz.vertexData;
            }
            mesh.indexData = uz.indexData;

            // LOD0 index count from submesh descriptors. Summed in 64-bit and
            // bounded by the parsed index count: the UZMH header built below
            // drives how many index bytes GPU upload copies out of indexData.
            const uint32_t uzIndexCount = uz.header.indexCount; // aligned copy of packed field
            uint64_t lod0IndexCount64 = 0;
            if (!uz.lods.empty()) {
                for (uint32_t si = uz.lods[0].submeshOffset;
                     si < uz.lods[0].submeshOffset + uz.lods[0].submeshCount
                     && si < static_cast<uint32_t>(uz.submeshes.size()); ++si) {
                    lod0IndexCount64 += uz.submeshes[si].indexCount;
                }
            } else {
                lod0IndexCount64 = uzIndexCount;
            }
            if (lod0IndexCount64 > uzIndexCount) {
                UZ_LOG(Renderer, Error,
                       "UZMS LOD0 submeshes reference {} indices but the mesh has {}, name='{}'",
                       lod0IndexCount64, uzIndexCount, name);
                mesh.header.magic = 0;
                return mesh;
            }
            const auto lod0IndexCount = static_cast<uint32_t>(lod0IndexCount64);

            // Compute AABB from vertex positions
            Vec3 bMin{FLT_MAX, FLT_MAX, FLT_MAX};
            Vec3 bMax{-FLT_MAX, -FLT_MAX, -FLT_MAX};
            for (uint32_t i = 0; i < vertCount; ++i) {
                const auto* pos = reinterpret_cast<const float*>(
                    mesh.vertexData.data() + static_cast<size_t>(i) * dstStride);
                if (pos[0] < bMin.x) bMin.x = pos[0];
                if (pos[1] < bMin.y) bMin.y = pos[1];
                if (pos[2] < bMin.z) bMin.z = pos[2];
                if (pos[0] > bMax.x) bMax.x = pos[0];
                if (pos[1] > bMax.y) bMax.y = pos[1];
                if (pos[2] > bMax.z) bMax.z = pos[2];
            }

            // Build UZMH header from UZMS data
            mesh.header.magic = kMeshMagic;
            mesh.header.version = kMeshVersion;
            mesh.header.vertexFormat = MeshVertexFormat::Standard;
            mesh.header.vertexStride = dstStride;
            mesh.header.vertexCount = vertCount;
            mesh.header.indexFormat = (uz.header.indexStride == 2)
                ? MeshIndexFormat::UInt16 : MeshIndexFormat::UInt32;
            mesh.header.indexCount = lod0IndexCount;
            mesh.header.submeshCount = uz.lods.empty() ? 0 : uz.lods[0].submeshCount;
            mesh.header.boundsMin = bMin;
            mesh.header.boundsMax = bMax;
            mesh.header.vertexDataOffset = sizeof(MeshHeader);
            mesh.header.indexDataOffset = sizeof(MeshHeader) + mesh.vertexData.size();
            std::memset(mesh.header.reserved, 0, sizeof(mesh.header.reserved));

            // Map LOD descriptors (UZMS LOD → UZMH header lods[])
            mesh.header.lodCount = uz.header.lodCount;
            std::memset(mesh.header.lods, 0, sizeof(mesh.header.lods));
            if (uz.header.lodCount > 1) {
                // Indices consumed so far (LOD0 + earlier LODs); 64-bit so
                // file-controlled submesh counts cannot wrap.
                uint64_t usedIndices = lod0IndexCount64;
                for (uint32_t li = 1; li < uz.header.lodCount && li <= 3; ++li) {
                    if (li >= uz.lods.size()) break;
                    uint64_t lodIdxCount = 0;
                    for (uint32_t si = uz.lods[li].submeshOffset;
                         si < uz.lods[li].submeshOffset + uz.lods[li].submeshCount
                         && si < static_cast<uint32_t>(uz.submeshes.size()); ++si) {
                        lodIdxCount += uz.submeshes[si].indexCount;
                    }
                    const uint64_t lodByteOff = usedIndices * uz.header.indexStride;
                    if (lodIdxCount > uzIndexCount - usedIndices ||
                        lodByteOff > UINT32_MAX) {
                        UZ_LOG(Renderer, Error,
                               "UZMS LOD{} submeshes overrun the {}-index buffer, name='{}'",
                               li, uzIndexCount, name);
                        mesh.header.magic = 0;
                        return mesh;
                    }
                    mesh.header.lods[li - 1].indexOffset = static_cast<uint32_t>(lodByteOff);
                    mesh.header.lods[li - 1].indexCount = static_cast<uint32_t>(lodIdxCount);
                    usedIndices += lodIdxCount;
                }
            }

            // LOD0 submeshes
            if (!uz.lods.empty()) {
                for (uint32_t si = uz.lods[0].submeshOffset;
                     si < uz.lods[0].submeshOffset + uz.lods[0].submeshCount
                     && si < static_cast<uint32_t>(uz.submeshes.size()); ++si) {
                    SubMesh sm;
                    sm.startIndex = uz.submeshes[si].indexOffset;
                    sm.indexCount = uz.submeshes[si].indexCount;
                    sm.materialIndex = uz.submeshes[si].materialIndex;
                    mesh.submeshes.push_back(sm);
                }
            }

            UZ_LOG(Renderer, Info,
                   "Loaded UZMS mesh as static: vertices={}, indices={}, lods={}, name='{}'",
                   vertCount, lod0IndexCount, mesh.header.lodCount, name);
            return mesh;
        }

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
    if (!MeshRangeInBounds(mesh.header.vertexDataOffset, vertexSize, data.size())) {
        UZ_LOG(Renderer, Error,
               "Truncated vertex data: offset={}, expected={} bytes, data={} bytes, name='{}'",
               mesh.header.vertexDataOffset, vertexSize, data.size(), name);
        mesh.header.magic = 0;
        return mesh;
    }
    if (vertexSize > 0) {
        mesh.vertexData.resize(static_cast<size_t>(vertexSize));
        std::memcpy(mesh.vertexData.data(),
                    data.data() + mesh.header.vertexDataOffset,
                    static_cast<size_t>(vertexSize));
    }

    const uint64_t indexSize = mesh.GetIndexDataSize();
    if (!MeshRangeInBounds(mesh.header.indexDataOffset, indexSize, data.size())) {
        UZ_LOG(Renderer, Error,
               "Truncated index data: offset={}, expected={} bytes, data={} bytes, name='{}'",
               mesh.header.indexDataOffset, indexSize, data.size(), name);
        mesh.header.magic = 0;
        return mesh;
    }
    if (indexSize > 0) {
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
