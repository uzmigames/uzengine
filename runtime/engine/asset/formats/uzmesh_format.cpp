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
    const std::vector<uint8_t>& indexData,
    const std::string& skeletonPath) {
    UZ_PROFILE_FUNCTION();

    // Compute layout offsets with 64-byte alignment
    const uint32_t afterHeader = sizeof(UzMeshHeader);
    const uint32_t afterLods = afterHeader + static_cast<uint32_t>(lods.size() * sizeof(UzMeshLOD));
    const uint32_t afterSubmeshes = afterLods + static_cast<uint32_t>(submeshes.size() * sizeof(UzMeshSubmesh));

    const uint32_t vertexOffset = AlignUp(afterSubmeshes, kUzMeshDataAlignment);
    const uint32_t afterVertex = vertexOffset + static_cast<uint32_t>(vertexData.size());
    const uint32_t indexOffset = AlignUp(afterVertex, kUzMeshDataAlignment);
    const uint32_t afterIndex = indexOffset + static_cast<uint32_t>(indexData.size());

    // Skeleton path is appended after index data for skinned meshes (null-terminated)
    const uint32_t skelPathLen = skeletonPath.empty()
                                     ? 0u
                                     : static_cast<uint32_t>(skeletonPath.size() + 1); // +1 for null
    const uint32_t totalSize = afterIndex + skelPathLen;

    // Fill computed offsets and skinned metadata into header
    header.magic = kUzMeshMagic;
    header.version = kUzMeshVersion;
    header.vertexDataOffset = vertexOffset;
    header.indexDataOffset = indexOffset;

    if (!skeletonPath.empty()) {
        header.flags |= kUzMeshFlag_Skinned;
        header.reserved[0] = skelPathLen;
    }

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
    if (!skeletonPath.empty()) {
        // Write null-terminated skeleton path after index data
        std::memcpy(out.data() + afterIndex, skeletonPath.c_str(), skelPathLen);
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

    if (result.file.header.version == 0 || result.file.header.version > kUzMeshVersion) {
        result.error = "Unsupported .uzmesh version: " +
                       std::to_string(result.file.header.version);
        return result;
    }

    // Reject unknown flag bits — only kUzMeshFlag_Skinned (0x1) is defined
    constexpr uint32_t kKnownFlags = kUzMeshFlag_Skinned;
    if (result.file.header.flags & ~kKnownFlags) {
        result.error = "Unknown flags in .uzmesh header: 0x" +
                       std::to_string(result.file.header.flags);
        return result;
    }

    const auto& h = result.file.header;

    // Consumers reinterpret vertex/index bytes by these strides, so a zero vertex
    // stride or a non-2/4 index stride would make them read past the data.
    if (h.vertexCount > 0 && h.vertexStride == 0) {
        result.error = "Invalid vertexStride 0 for " + std::to_string(h.vertexCount) + " vertices";
        return result;
    }
    if (h.indexCount > 0 && h.indexStride != 2 && h.indexStride != 4) {
        result.error = "Invalid indexStride " + std::to_string(h.indexStride) +
                       " (must be 2 or 4)";
        return result;
    }

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

    // Read vertex data (offset/size checks in wrap-safe subtraction form)
    const size_t vertexBytes = static_cast<size_t>(h.vertexCount) * h.vertexStride;
    if (h.vertexDataOffset > data.size() || vertexBytes > data.size() - h.vertexDataOffset) {
        result.error = "Data truncated — vertex data exceeds file size";
        return result;
    }
    result.file.vertexData.assign(
        data.data() + h.vertexDataOffset,
        data.data() + h.vertexDataOffset + vertexBytes);

    // Read index data
    const size_t indexBytes = static_cast<size_t>(h.indexCount) * h.indexStride;
    if (h.indexDataOffset > data.size() || indexBytes > data.size() - h.indexDataOffset) {
        result.error = "Data truncated — index data exceeds file size";
        return result;
    }
    result.file.indexData.assign(
        data.data() + h.indexDataOffset,
        data.data() + h.indexDataOffset + indexBytes);

    // v2: Read skeleton path for skinned meshes
    if (h.version >= 2 && (h.flags & kUzMeshFlag_Skinned)) {
        const uint32_t skelPathLen = h.reserved[0];
        if (skelPathLen > 0) {
            const size_t skelPathOffset = h.indexDataOffset + indexBytes; // <= data.size()
            if (skelPathLen > data.size() - skelPathOffset) {
                result.error = "Data truncated — skeleton path exceeds file size";
                return result;
            }
            // Extract string without the null terminator
            const auto* pathBegin = reinterpret_cast<const char*>(data.data() + skelPathOffset);
            const size_t strLen = (skelPathLen > 0 && pathBegin[skelPathLen - 1] == '\0')
                                      ? skelPathLen - 1
                                      : skelPathLen;
            result.file.skeletonPath.assign(pathBegin, strLen);
        }
    }

    result.success = true;
    return result;
}

} // namespace uz
