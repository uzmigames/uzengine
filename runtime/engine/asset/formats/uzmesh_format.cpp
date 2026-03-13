/// UzEngine .uzmesh Binary Format — Serialization and Parsing
///
/// Writes and reads the .uzmesh binary format defined in uzmesh_format.h.
/// All operations are little-endian and platform-independent.
/// Vertex and index data regions are 64-byte aligned for direct GPU upload.

#include "uzmesh_format.h"
#include "profiler.h"

#include <cstring>

namespace uz {

namespace {

/// Rounds `offset` up to the next multiple of `alignment`.
[[nodiscard]] constexpr uint32_t AlignUp(uint32_t offset, uint32_t alignment) {
    return (offset + alignment - 1) & ~(alignment - 1);
}

} // anonymous namespace

std::vector<uint8_t> WriteUzMesh(
    UzMeshHeader header,
    const std::vector<UzMeshLOD>& lods,
    const std::vector<UzMeshSubmesh>& submeshes,
    const std::vector<uint8_t>& vertexData,
    const std::vector<uint8_t>& indexData) {
    UZ_PROFILE_FUNCTION();

    // Compute layout offsets with 64-byte alignment
    const uint32_t afterHeader = sizeof(UzMeshHeader);
    const uint32_t afterLods = afterHeader + static_cast<uint32_t>(lods.size() * sizeof(UzMeshLOD));
    const uint32_t afterSubmeshes = afterLods + static_cast<uint32_t>(submeshes.size() * sizeof(UzMeshSubmesh));

    const uint32_t vertexOffset = AlignUp(afterSubmeshes, kUzMeshDataAlignment);
    const uint32_t afterVertex = vertexOffset + static_cast<uint32_t>(vertexData.size());
    const uint32_t indexOffset = AlignUp(afterVertex, kUzMeshDataAlignment);
    const uint32_t totalSize = indexOffset + static_cast<uint32_t>(indexData.size());

    // Fill computed offsets into header
    header.magic = kUzMeshMagic;
    header.version = kUzMeshVersion;
    header.vertexDataOffset = vertexOffset;
    header.indexDataOffset = indexOffset;

    std::vector<uint8_t> out(totalSize, 0);

    std::memcpy(out.data(), &header, sizeof(header));

    if (!lods.empty()) {
        std::memcpy(out.data() + afterHeader, lods.data(), lods.size() * sizeof(UzMeshLOD));
    }
    if (!submeshes.empty()) {
        std::memcpy(out.data() + afterLods, submeshes.data(), submeshes.size() * sizeof(UzMeshSubmesh));
    }
    if (!vertexData.empty()) {
        std::memcpy(out.data() + vertexOffset, vertexData.data(), vertexData.size());
    }
    if (!indexData.empty()) {
        std::memcpy(out.data() + indexOffset, indexData.data(), indexData.size());
    }

    return out;
}

UzMeshParseResult ReadUzMesh(const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
    UzMeshParseResult result;

    if (data.size() < sizeof(UzMeshHeader)) {
        result.error = "Data too small for .uzmesh header";
        return result;
    }

    std::memcpy(&result.file.header, data.data(), sizeof(UzMeshHeader));

    if (result.file.header.magic != kUzMeshMagic) {
        result.error = "Invalid magic number — expected 'UZMS'";
        return result;
    }

    if (result.file.header.version != kUzMeshVersion) {
        result.error = "Unsupported .uzmesh version: " +
                       std::to_string(result.file.header.version);
        return result;
    }

    const auto& h = result.file.header;

    // Read LODs
    const size_t lodsStart = sizeof(UzMeshHeader);
    const size_t lodsSize = h.lodCount * sizeof(UzMeshLOD);
    if (data.size() < lodsStart + lodsSize) {
        result.error = "Data truncated — LOD array exceeds file size";
        return result;
    }
    result.file.lods.resize(h.lodCount);
    if (h.lodCount > 0) {
        std::memcpy(result.file.lods.data(), data.data() + lodsStart, lodsSize);
    }

    // Read submeshes
    const size_t submeshStart = lodsStart + lodsSize;
    const size_t submeshSize = h.totalSubmeshCount * sizeof(UzMeshSubmesh);
    if (data.size() < submeshStart + submeshSize) {
        result.error = "Data truncated — submesh array exceeds file size";
        return result;
    }
    result.file.submeshes.resize(h.totalSubmeshCount);
    if (h.totalSubmeshCount > 0) {
        std::memcpy(result.file.submeshes.data(), data.data() + submeshStart, submeshSize);
    }

    // Read vertex data
    const size_t vertexBytes = static_cast<size_t>(h.vertexCount) * h.vertexStride;
    if (data.size() < h.vertexDataOffset + vertexBytes) {
        result.error = "Data truncated — vertex data exceeds file size";
        return result;
    }
    result.file.vertexData.assign(
        data.data() + h.vertexDataOffset,
        data.data() + h.vertexDataOffset + vertexBytes);

    // Read index data
    const size_t indexBytes = static_cast<size_t>(h.indexCount) * h.indexStride;
    if (data.size() < h.indexDataOffset + indexBytes) {
        result.error = "Data truncated — index data exceeds file size";
        return result;
    }
    result.file.indexData.assign(
        data.data() + h.indexDataOffset,
        data.data() + h.indexDataOffset + indexBytes);

    result.success = true;
    return result;
}

} // namespace uz
