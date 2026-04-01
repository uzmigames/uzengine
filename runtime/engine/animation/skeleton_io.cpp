/// UzEngine Skeleton I/O — .uzskel binary format read/write implementation.

#include "skeleton_io.h"
#include "skeleton.h"

#include <cstring>
#include <fstream>

#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Animation);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — little-endian binary read/write
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Appends a value to a byte buffer in little-endian format.
template <typename T>
void WriteLE(std::vector<uint8_t>& buf, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
}

/// Appends raw bytes to a byte buffer.
void WriteBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const auto offset = buf.size();
    buf.resize(offset + size);
    std::memcpy(buf.data() + offset, data, size);
}

/// Reads a value from a byte buffer in little-endian format.
/// Returns false if there are not enough bytes remaining.
template <typename T>
bool ReadLE(const uint8_t* data, size_t dataSize, size_t& offset, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset + sizeof(T) > dataSize) {
        return false;
    }
    std::memcpy(&out, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

/// Reads raw bytes from a byte buffer.
/// Returns false if there are not enough bytes remaining.
bool ReadBytes(const uint8_t* data, size_t dataSize, size_t& offset, void* out, size_t size) {
    if (offset + size > dataSize) {
        return false;
    }
    std::memcpy(out, data + offset, size);
    offset += size;
    return true;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// In-Memory I/O
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> WriteSkeleton(const Skeleton& skeleton) {
    const uint32_t boneCount = skeleton.GetBoneCount();

    // Estimate buffer size: header + per-bone data.
    // Average bone name ~16 chars, per-bone fixed data = 44 bytes.
    const size_t estimatedSize = kUzSkelHeaderSize + boneCount * (44 + 16);
    std::vector<uint8_t> buf;
    buf.reserve(estimatedSize);

    // Header: magic(4) + version(4) + boneCount(4) + reserved(4)
    WriteLE(buf, kUzSkelMagic);
    WriteLE(buf, kUzSkelVersion);
    WriteLE(buf, boneCount);
    WriteLE(buf, static_cast<uint32_t>(0)); // reserved

    // Per-bone data.
    for (uint32_t i = 0; i < boneCount; ++i) {
        const BoneInfo& bone = skeleton.GetBone(i);

        // Name: uint16 length + chars (not null-terminated).
        const auto nameLen = static_cast<uint16_t>(bone.name.size());
        WriteLE(buf, nameLen);
        if (nameLen > 0) {
            WriteBytes(buf, bone.name.data(), nameLen);
        }

        // Parent index as int16.
        const auto parentIdx = static_cast<int16_t>(bone.parentIndex);
        WriteLE(buf, parentIdx);

        // Translation: 3 floats (x, y, z).
        WriteLE(buf, bone.bindTranslation.x);
        WriteLE(buf, bone.bindTranslation.y);
        WriteLE(buf, bone.bindTranslation.z);

        // Rotation: 4 floats (x, y, z, w) — quaternion.
        WriteLE(buf, bone.bindRotation.x);
        WriteLE(buf, bone.bindRotation.y);
        WriteLE(buf, bone.bindRotation.z);
        WriteLE(buf, bone.bindRotation.w);

        // Scale: 3 floats (x, y, z).
        WriteLE(buf, bone.bindScale.x);
        WriteLE(buf, bone.bindScale.y);
        WriteLE(buf, bone.bindScale.z);
    }

    return buf;
}

std::optional<Skeleton> ReadSkeleton(const std::vector<uint8_t>& data) {
    if (data.size() < kUzSkelHeaderSize) {
        UZ_LOG(Animation, Error, "ReadSkeleton — data too small for header ({} bytes, need {})",
               data.size(), kUzSkelHeaderSize);
        return std::nullopt;
    }

    size_t offset = 0;
    const uint8_t* ptr = data.data();
    const size_t dataSize = data.size();

    // Read header.
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t boneCount = 0;
    uint32_t reserved = 0;

    if (!ReadLE(ptr, dataSize, offset, magic)) return std::nullopt;
    if (!ReadLE(ptr, dataSize, offset, version)) return std::nullopt;
    if (!ReadLE(ptr, dataSize, offset, boneCount)) return std::nullopt;
    if (!ReadLE(ptr, dataSize, offset, reserved)) return std::nullopt;

    // Validate magic.
    if (magic != kUzSkelMagic) {
        UZ_LOG(Animation, Error, "ReadSkeleton — invalid magic: 0x{:08X} (expected 0x{:08X})",
               magic, kUzSkelMagic);
        return std::nullopt;
    }

    // Validate version.
    if (version != kUzSkelVersion) {
        UZ_LOG(Animation, Error, "ReadSkeleton — unsupported version: {} (expected {})",
               version, kUzSkelVersion);
        return std::nullopt;
    }

    // Validate bone count.
    if (boneCount > kMaxBones) {
        UZ_LOG(Animation, Error, "ReadSkeleton — bone count {} exceeds maximum {}",
               boneCount, kMaxBones);
        return std::nullopt;
    }

    // Read bones.
    Skeleton skeleton;

    for (uint32_t i = 0; i < boneCount; ++i) {
        BoneInfo bone;

        // Name.
        uint16_t nameLen = 0;
        if (!ReadLE(ptr, dataSize, offset, nameLen)) {
            UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} name length", i);
            return std::nullopt;
        }

        if (nameLen > 0) {
            bone.name.resize(nameLen);
            if (!ReadBytes(ptr, dataSize, offset, bone.name.data(), nameLen)) {
                UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} name", i);
                return std::nullopt;
            }
        }

        // Parent index.
        int16_t parentIdx = -1;
        if (!ReadLE(ptr, dataSize, offset, parentIdx)) {
            UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} parent", i);
            return std::nullopt;
        }
        bone.parentIndex = static_cast<int32_t>(parentIdx);

        // Translation.
        if (!ReadLE(ptr, dataSize, offset, bone.bindTranslation.x) ||
            !ReadLE(ptr, dataSize, offset, bone.bindTranslation.y) ||
            !ReadLE(ptr, dataSize, offset, bone.bindTranslation.z)) {
            UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} translation", i);
            return std::nullopt;
        }

        // Rotation.
        if (!ReadLE(ptr, dataSize, offset, bone.bindRotation.x) ||
            !ReadLE(ptr, dataSize, offset, bone.bindRotation.y) ||
            !ReadLE(ptr, dataSize, offset, bone.bindRotation.z) ||
            !ReadLE(ptr, dataSize, offset, bone.bindRotation.w)) {
            UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} rotation", i);
            return std::nullopt;
        }

        // Scale.
        if (!ReadLE(ptr, dataSize, offset, bone.bindScale.x) ||
            !ReadLE(ptr, dataSize, offset, bone.bindScale.y) ||
            !ReadLE(ptr, dataSize, offset, bone.bindScale.z)) {
            UZ_LOG(Animation, Error, "ReadSkeleton — unexpected end of data reading bone {} scale", i);
            return std::nullopt;
        }

        if (!skeleton.AddBone(std::move(bone))) {
            UZ_LOG(Animation, Error, "ReadSkeleton — failed to add bone {}", i);
            return std::nullopt;
        }
    }

    // Finalize: build lookup and compute inverse bind matrices.
    skeleton.BuildNameIndex();
    skeleton.ComputeInverseBindMatrices();

    return skeleton;
}

// ─────────────────────────────────────────────────────────────────────────────
// Disk I/O
// ─────────────────────────────────────────────────────────────────────────────

bool SaveSkeleton(const Skeleton& skeleton, const std::string& path) {
    const std::vector<uint8_t> data = WriteSkeleton(skeleton);

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        UZ_LOG(Animation, Error, "SaveSkeleton — failed to open '{}' for writing", path);
        return false;
    }

    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file.good()) {
        UZ_LOG(Animation, Error, "SaveSkeleton — write error for '{}'", path);
        return false;
    }

    return true;
}

std::optional<Skeleton> LoadSkeleton(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        UZ_LOG(Animation, Error, "LoadSkeleton — failed to open '{}'", path);
        return std::nullopt;
    }

    const auto fileSize = static_cast<size_t>(file.tellg());
    if (fileSize < kUzSkelHeaderSize) {
        UZ_LOG(Animation, Error, "LoadSkeleton — file '{}' too small ({} bytes)", path, fileSize);
        return std::nullopt;
    }

    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(fileSize);
    file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(fileSize));
    if (!file.good()) {
        UZ_LOG(Animation, Error, "LoadSkeleton — read error for '{}'", path);
        return std::nullopt;
    }

    return ReadSkeleton(data);
}

} // namespace uz
