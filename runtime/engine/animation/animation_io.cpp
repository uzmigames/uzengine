/// UzEngine Animation I/O — .uzanim binary format read/write implementation.

#include "animation_io.h"
#include "animation_clip.h"

#include <cstring>
#include <fstream>

#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Animation);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — little-endian binary read/write
// ─────────────────────────────────────────────────────────────────────────────

namespace {

template <typename T>
void WriteLE(std::vector<uint8_t>& buf, T value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto offset = buf.size();
    buf.resize(offset + sizeof(T));
    std::memcpy(buf.data() + offset, &value, sizeof(T));
}

void WriteBytes(std::vector<uint8_t>& buf, const void* data, size_t size) {
    const auto offset = buf.size();
    buf.resize(offset + size);
    std::memcpy(buf.data() + offset, data, size);
}

template <typename T>
bool ReadLE(const uint8_t* data, size_t dataSize, size_t& offset, T& out) {
    static_assert(std::is_trivially_copyable_v<T>);
    if (offset + sizeof(T) > dataSize) return false;
    std::memcpy(&out, data + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool ReadBytes(const uint8_t* data, size_t dataSize, size_t& offset, void* out, size_t size) {
    if (offset + size > dataSize) return false;
    std::memcpy(out, data + offset, size);
    offset += size;
    return true;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// WriteAnimation — serialize to byte buffer
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> WriteAnimation(const AnimationClip& clip) {
    std::vector<uint8_t> buf;
    // Rough size estimate: header + per-channel fixed (~32 B) + keyframes
    size_t estimatedKeys = 0;
    for (const auto& ch : clip.channels) {
        estimatedKeys += ch.positionKeys.size() * 16
                      + ch.rotationKeys.size() * 20
                      + ch.scaleKeys.size() * 16;
    }
    buf.reserve(kUzAnimHeaderSize
                + clip.name.size() + clip.skeletonName.size()
                + clip.channels.size() * 48
                + estimatedKeys);

    // Header
    WriteLE(buf, kUzAnimMagic);
    WriteLE(buf, kUzAnimVersion);
    WriteLE(buf, clip.duration);
    WriteLE(buf, clip.sampleRate);
    WriteLE(buf, static_cast<uint32_t>(clip.channels.size()));

    const uint32_t flags = (clip.loop ? 1u : 0u);
    WriteLE(buf, flags);

    WriteLE(buf, static_cast<uint16_t>(clip.name.size()));
    WriteLE(buf, static_cast<uint16_t>(clip.skeletonName.size()));
    WriteLE(buf, static_cast<uint32_t>(0)); // reserved

    // Variable-length strings after header
    WriteBytes(buf, clip.name.data(), clip.name.size());
    WriteBytes(buf, clip.skeletonName.data(), clip.skeletonName.size());

    // Channels
    for (const auto& ch : clip.channels) {
        WriteLE(buf, static_cast<uint16_t>(ch.boneName.size()));
        WriteBytes(buf, ch.boneName.data(), ch.boneName.size());

        WriteLE(buf, static_cast<uint32_t>(ch.positionKeys.size()));
        WriteLE(buf, static_cast<uint32_t>(ch.rotationKeys.size()));
        WriteLE(buf, static_cast<uint32_t>(ch.scaleKeys.size()));

        for (const auto& k : ch.positionKeys) {
            WriteLE(buf, k.time);
            WriteLE(buf, k.value.x);
            WriteLE(buf, k.value.y);
            WriteLE(buf, k.value.z);
        }
        for (const auto& k : ch.rotationKeys) {
            WriteLE(buf, k.time);
            WriteLE(buf, k.value.x);
            WriteLE(buf, k.value.y);
            WriteLE(buf, k.value.z);
            WriteLE(buf, k.value.w);
        }
        for (const auto& k : ch.scaleKeys) {
            WriteLE(buf, k.time);
            WriteLE(buf, k.value.x);
            WriteLE(buf, k.value.y);
            WriteLE(buf, k.value.z);
        }
    }

    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReadAnimation — deserialize from byte buffer
// ─────────────────────────────────────────────────────────────────────────────

std::optional<AnimationClip> ReadAnimation(const std::vector<uint8_t>& data) {
    if (data.size() < kUzAnimHeaderSize) {
        UZ_LOG(Animation, Error, "ReadAnimation: buffer too small ({} < {})",
               data.size(), kUzAnimHeaderSize);
        return std::nullopt;
    }

    const uint8_t* d = data.data();
    const size_t sz = data.size();
    size_t off = 0;

    uint32_t magic = 0, version = 0, channelCount = 0, flags = 0, reserved = 0;
    float duration = 0, sampleRate = 0;
    uint16_t nameLen = 0, skelNameLen = 0;

    if (!ReadLE(d, sz, off, magic) || magic != kUzAnimMagic) {
        UZ_LOG(Animation, Error, "ReadAnimation: bad magic (got 0x{:08X}, want 0x{:08X})",
               magic, kUzAnimMagic);
        return std::nullopt;
    }
    if (!ReadLE(d, sz, off, version) || version != kUzAnimVersion) {
        UZ_LOG(Animation, Error, "ReadAnimation: bad version ({})", version);
        return std::nullopt;
    }
    if (!ReadLE(d, sz, off, duration) ||
        !ReadLE(d, sz, off, sampleRate) ||
        !ReadLE(d, sz, off, channelCount) ||
        !ReadLE(d, sz, off, flags) ||
        !ReadLE(d, sz, off, nameLen) ||
        !ReadLE(d, sz, off, skelNameLen) ||
        !ReadLE(d, sz, off, reserved)) {
        UZ_LOG(Animation, Error, "ReadAnimation: truncated header");
        return std::nullopt;
    }

    AnimationClip clip;
    clip.duration = duration;
    clip.sampleRate = sampleRate;
    clip.loop = (flags & 1u) != 0;

    clip.name.resize(nameLen);
    if (nameLen > 0 && !ReadBytes(d, sz, off, clip.name.data(), nameLen)) {
        UZ_LOG(Animation, Error, "ReadAnimation: truncated clip name");
        return std::nullopt;
    }
    clip.skeletonName.resize(skelNameLen);
    if (skelNameLen > 0 && !ReadBytes(d, sz, off, clip.skeletonName.data(), skelNameLen)) {
        UZ_LOG(Animation, Error, "ReadAnimation: truncated skeleton name");
        return std::nullopt;
    }

    clip.channels.reserve(channelCount);
    for (uint32_t c = 0; c < channelCount; ++c) {
        BoneChannel ch;
        uint16_t boneNameLen = 0;
        if (!ReadLE(d, sz, off, boneNameLen)) {
            UZ_LOG(Animation, Error, "ReadAnimation: truncated at channel {} header", c);
            return std::nullopt;
        }
        ch.boneName.resize(boneNameLen);
        if (boneNameLen > 0 && !ReadBytes(d, sz, off, ch.boneName.data(), boneNameLen)) {
            UZ_LOG(Animation, Error, "ReadAnimation: truncated at channel {} name", c);
            return std::nullopt;
        }

        uint32_t posCount = 0, rotCount = 0, sclCount = 0;
        if (!ReadLE(d, sz, off, posCount) ||
            !ReadLE(d, sz, off, rotCount) ||
            !ReadLE(d, sz, off, sclCount)) {
            UZ_LOG(Animation, Error, "ReadAnimation: truncated at channel {} counts", c);
            return std::nullopt;
        }

        ch.positionKeys.resize(posCount);
        for (uint32_t i = 0; i < posCount; ++i) {
            float t, x, y, z;
            if (!ReadLE(d, sz, off, t) ||
                !ReadLE(d, sz, off, x) ||
                !ReadLE(d, sz, off, y) ||
                !ReadLE(d, sz, off, z)) {
                UZ_LOG(Animation, Error, "ReadAnimation: truncated pos key {} of channel {}", i, c);
                return std::nullopt;
            }
            ch.positionKeys[i] = {t, Vec3{x, y, z}};
        }
        ch.rotationKeys.resize(rotCount);
        for (uint32_t i = 0; i < rotCount; ++i) {
            float t, x, y, z, w;
            if (!ReadLE(d, sz, off, t) ||
                !ReadLE(d, sz, off, x) ||
                !ReadLE(d, sz, off, y) ||
                !ReadLE(d, sz, off, z) ||
                !ReadLE(d, sz, off, w)) {
                UZ_LOG(Animation, Error, "ReadAnimation: truncated rot key {} of channel {}", i, c);
                return std::nullopt;
            }
            ch.rotationKeys[i] = {t, Quat{x, y, z, w}};
        }
        ch.scaleKeys.resize(sclCount);
        for (uint32_t i = 0; i < sclCount; ++i) {
            float t, x, y, z;
            if (!ReadLE(d, sz, off, t) ||
                !ReadLE(d, sz, off, x) ||
                !ReadLE(d, sz, off, y) ||
                !ReadLE(d, sz, off, z)) {
                UZ_LOG(Animation, Error, "ReadAnimation: truncated scl key {} of channel {}", i, c);
                return std::nullopt;
            }
            ch.scaleKeys[i] = {t, Vec3{x, y, z}};
        }

        clip.channels.push_back(std::move(ch));
    }

    return clip;
}

// ─────────────────────────────────────────────────────────────────────────────
// Disk I/O
// ─────────────────────────────────────────────────────────────────────────────

bool SaveAnimation(const AnimationClip& clip, const std::string& path) {
    const auto bytes = WriteAnimation(clip);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        UZ_LOG(Animation, Error, "SaveAnimation: cannot open '{}' for write", path);
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        UZ_LOG(Animation, Error, "SaveAnimation: write failed '{}'", path);
        return false;
    }
    UZ_LOG(Animation, Info, "SaveAnimation: wrote '{}' ({} bytes, {} channels, {:.2f}s)",
           path, bytes.size(), clip.channels.size(), clip.duration);
    return true;
}

std::optional<AnimationClip> LoadAnimation(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        UZ_LOG(Animation, Error, "LoadAnimation: cannot open '{}'", path);
        return std::nullopt;
    }
    const auto size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!in.read(reinterpret_cast<char*>(buf.data()), size)) {
        UZ_LOG(Animation, Error, "LoadAnimation: read failed '{}'", path);
        return std::nullopt;
    }
    return ReadAnimation(buf);
}

} // namespace uz
