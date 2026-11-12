/// volume_asset.cpp — .uzvol parsing/writing, procedural grids and the Texture3D upload.

#include "volume_asset.h"

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_command_list.h"
#include "engine/asset/vfs.h"
#include "paths.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {
namespace {

constexpr char kMagic[8] = {'U', 'Z', 'V', 'O', 'L', 0, 0, 0};

uint32_t BytesPerVoxel(VolumeFormat format) { return format == VolumeFormat::R16F ? 2u : 1u; }

// Round-to-nearest-even IEEE half encode/decode for the R16F grids. Values are
// densities in [0, few], so no denormal or overflow paths matter in practice,
// but both are handled for a faithful round trip.
uint16_t FloatToHalf(float value) {
    uint32_t bits; std::memcpy(&bits, &value, 4);
    const uint32_t sign = (bits >> 16) & 0x8000u;
    int32_t exponent = int32_t((bits >> 23) & 0xffu) - 127 + 15;
    uint32_t mantissa = bits & 0x7fffffu;
    if (exponent >= 31) return uint16_t(sign | 0x7c00u);
    if (exponent <= 0) {
        if (exponent < -10) return uint16_t(sign);
        mantissa |= 0x800000u;
        const uint32_t shift = uint32_t(14 - exponent);
        uint32_t half = mantissa >> shift;
        const uint32_t remainder = mantissa & ((1u << shift) - 1u), halfway = 1u << (shift - 1);
        if (remainder > halfway || (remainder == halfway && (half & 1u))) ++half;
        return uint16_t(sign | half);
    }
    uint32_t half = uint32_t(exponent << 10) | (mantissa >> 13);
    const uint32_t remainder = mantissa & 0x1fffu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u))) ++half;
    return uint16_t(sign | half);
}

float HalfToFloat(uint16_t half) {
    const uint32_t sign = uint32_t(half & 0x8000u) << 16;
    uint32_t exponent = (half >> 10) & 0x1fu, mantissa = half & 0x3ffu, bits;
    if (exponent == 0) {
        if (mantissa == 0) { bits = sign; }
        else {
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
            mantissa &= 0x3ffu;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | (mantissa << 13);
    } else {
        bits = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
    }
    float value; std::memcpy(&value, &bits, 4);
    return value;
}

uint32_t Hash3(uint32_t x, uint32_t y, uint32_t z, uint32_t seed) {
    uint32_t h = x * 0x8da6b343u ^ y * 0xd8163841u ^ z * 0xcb1ab31fu ^ seed * 0x9e3779b9u;
    h = (h ^ (h >> 16)) * 0x7feb352du;
    h = (h ^ (h >> 15)) * 0x846ca68bu;
    return h ^ (h >> 16);
}

float Smooth(float t) { return t * t * (3.f - 2.f * t); }

// Periodic 3D value noise in [0,1] on an integer lattice of `period` cells.
float ValueNoise(float x, float y, float z, uint32_t period, uint32_t seed) {
    const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
    const float tx = Smooth(x - fx), ty = Smooth(y - fy), tz = Smooth(z - fz);
    auto lattice = [&](int dx, int dy, int dz) {
        const uint32_t ix = uint32_t(int(fx) + dx) % period, iy = uint32_t(int(fy) + dy) % period,
                       iz = uint32_t(int(fz) + dz) % period;
        return float(Hash3(ix, iy, iz, seed) & 0xffffffu) / float(0xffffffu);
    };
    const float c00 = lattice(0,0,0) + (lattice(1,0,0) - lattice(0,0,0)) * tx;
    const float c10 = lattice(0,1,0) + (lattice(1,1,0) - lattice(0,1,0)) * tx;
    const float c01 = lattice(0,0,1) + (lattice(1,0,1) - lattice(0,0,1)) * tx;
    const float c11 = lattice(0,1,1) + (lattice(1,1,1) - lattice(0,1,1)) * tx;
    const float c0 = c00 + (c10 - c00) * ty, c1 = c01 + (c11 - c01) * ty;
    return c0 + (c1 - c0) * tz;
}

} // namespace

// ── CPU grid ────────────────────────────────────────────────────────────────

void VolumeAsset::Allocate(uint32_t w, uint32_t h, uint32_t d, VolumeFormat format) {
    m_width = w; m_height = h; m_depth = d; m_format = format;
    m_data.assign(uint64_t(w) * h * d * BytesPerVoxel(format), 0);
}

void VolumeAsset::Store(uint32_t x, uint32_t y, uint32_t z, float density) {
    const uint64_t index = (uint64_t(z) * m_height + y) * m_width + x;
    if (m_format == VolumeFormat::R16F) {
        const uint16_t half = FloatToHalf(density);
        std::memcpy(m_data.data() + index * 2, &half, 2);
    } else {
        m_data[index] = uint8_t(std::lround(std::clamp(density, 0.f, 1.f) * 255.f));
    }
}

float VolumeAsset::Voxel(uint32_t x, uint32_t y, uint32_t z) const {
    const uint64_t index = (uint64_t(z) * m_height + y) * m_width + x;
    if (m_format == VolumeFormat::R16F) {
        uint16_t half; std::memcpy(&half, m_data.data() + index * 2, 2);
        return HalfToFloat(half);
    }
    return float(m_data[index]) / 255.f;
}

float VolumeAsset::GetMaxDensity() const {
    float maximum = 0.f;
    for (uint32_t z = 0; z < m_depth; ++z)
        for (uint32_t y = 0; y < m_height; ++y)
            for (uint32_t x = 0; x < m_width; ++x) maximum = std::max(maximum, Voxel(x, y, z));
    return maximum;
}

float VolumeAsset::Sample(float u, float v, float w) const {
    if (!IsValid()) return 0.f;
    // Clamp to voxel centres, then the standard lerp of the eight neighbours:
    // the same as the shader's clamped UVW + trilinear SampleLevel.
    auto axis = [](float t, uint32_t n, uint32_t& i0, uint32_t& i1, float& f) {
        const float half = 0.5f / float(n);
        t = std::clamp(t, half, 1.f - half) * float(n) - 0.5f;
        const float floorT = std::floor(t);
        i0 = uint32_t(std::clamp(int(floorT), 0, int(n) - 1));
        i1 = std::min(i0 + 1u, n - 1u);
        f = t - floorT;
    };
    uint32_t x0, x1, y0, y1, z0, z1; float fx, fy, fz;
    axis(u, m_width, x0, x1, fx); axis(v, m_height, y0, y1, fy); axis(w, m_depth, z0, z1, fz);
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    const float c00 = lerp(Voxel(x0,y0,z0), Voxel(x1,y0,z0), fx), c10 = lerp(Voxel(x0,y1,z0), Voxel(x1,y1,z0), fx);
    const float c01 = lerp(Voxel(x0,y0,z1), Voxel(x1,y0,z1), fx), c11 = lerp(Voxel(x0,y1,z1), Voxel(x1,y1,z1), fx);
    return lerp(lerp(c00, c10, fy), lerp(c01, c11, fy), fz);
}

bool VolumeAsset::Load(const std::vector<uint8_t>& bytes, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; m_width = m_height = m_depth = 0; m_data.clear(); return false; };
    if (bytes.size() < sizeof(UzVolHeader)) return fail("file shorter than the 32-byte UZVOL header");
    UzVolHeader header; std::memcpy(&header, bytes.data(), sizeof(header));
    if (std::memcmp(header.magic, kMagic, 8) != 0) return fail("bad magic (expected UZVOL)");
    if (header.version != kUzVolVersion) return fail("unsupported UZVOL version");
    if (header.format > uint32_t(VolumeFormat::R16F)) return fail("unknown voxel format");
    if (!header.width || !header.height || !header.depth
        || header.width > kUzVolMaxEdge || header.height > kUzVolMaxEdge || header.depth > kUzVolMaxEdge)
        return fail("grid dimensions must be 1..256 per axis");
    const auto format = VolumeFormat(header.format);
    const uint64_t expected = uint64_t(header.width) * header.height * header.depth * BytesPerVoxel(format);
    if (bytes.size() != sizeof(header) + expected) return fail("payload size does not match the header dimensions");
    Allocate(header.width, header.height, header.depth, format);
    std::memcpy(m_data.data(), bytes.data() + sizeof(header), size_t(expected));
    return true;
}

std::vector<uint8_t> VolumeAsset::Serialize() const {
    if (!IsValid()) return {};
    UzVolHeader header{};
    header.width = m_width; header.height = m_height; header.depth = m_depth;
    header.format = uint32_t(m_format);
    std::vector<uint8_t> bytes(sizeof(header) + m_data.size());
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), m_data.data(), m_data.size());
    return bytes;
}

bool VolumeAsset::LoadFile(IVFS* vfs, const std::string& path, std::string* error) {
    // Scene data: a relative path below a content root, never absolute or '..'.
    if (!NormalizeVfsPath(path)) { if (error) *error = "path escapes the content roots"; return false; }
    if (vfs && vfs->Exists(path)) return Load(vfs->Read(path), error);
    const std::filesystem::path candidates[] = {
        ResolveEngineResource("content/" + path),
        std::filesystem::current_path() / path,
    };
    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(candidate, ec)) continue;
        std::ifstream file(candidate, std::ios::binary | std::ios::ate);
        if (!file.is_open()) continue;
        const auto size = file.tellg();
        std::vector<uint8_t> bytes(static_cast<size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), size);
        return Load(bytes, error);
    }
    if (error) *error = "file not found in the project VFS, engine content or working directory";
    return false;
}

bool VolumeAsset::Resolve(const std::string& spec, IVFS* vfs, VolumeAsset& out, std::string* error) {
    static constexpr const char* kPrefix = "builtin:";
    if (spec.rfind(kPrefix, 0) == 0) {
        const std::string name = spec.substr(std::strlen(kPrefix));
        if (name == "sphere") { out = GenerateSphereVolume(64, 0.9f, 1.f); return true; }
        if (name == "cloud") { out = GenerateCloudVolume(96, 7u); return true; }
        if (name == "constant") { out = GenerateConstantVolume(16, 1.f); return true; }
        if (name == "ramp") { out = GenerateRampVolume(64); return true; }
        if (error) *error = "unknown builtin volume '" + name + "'";
        return false;
    }
    return out.LoadFile(vfs, spec, error);
}

VolumeAsset VolumeAsset::GenerateSphereVolume(uint32_t resolution, float radius, float density,
                                              VolumeFormat format) {
    VolumeAsset asset;
    resolution = std::clamp(resolution, 1u, kUzVolMaxEdge);
    asset.Allocate(resolution, resolution, resolution, format);
    const float voxel = 2.f / float(resolution);
    for (uint32_t z = 0; z < resolution; ++z)
        for (uint32_t y = 0; y < resolution; ++y)
            for (uint32_t x = 0; x < resolution; ++x) {
                const float px = (float(x) + 0.5f) * voxel - 1.f, py = (float(y) + 0.5f) * voxel - 1.f,
                            pz = (float(z) + 0.5f) * voxel - 1.f;
                const float distance = std::sqrt(px*px + py*py + pz*pz);
                // One-voxel soft edge: unit inside, zero outside, linear in between.
                const float edge = std::clamp((radius - distance) / voxel + 0.5f, 0.f, 1.f);
                asset.Store(x, y, z, density * edge);
            }
    return asset;
}

VolumeAsset VolumeAsset::GenerateConstantVolume(uint32_t resolution, float density) {
    VolumeAsset asset;
    resolution = std::clamp(resolution, 1u, kUzVolMaxEdge);
    asset.Allocate(resolution, resolution, resolution, VolumeFormat::R8);
    for (uint32_t z = 0; z < resolution; ++z)
        for (uint32_t y = 0; y < resolution; ++y)
            for (uint32_t x = 0; x < resolution; ++x) asset.Store(x, y, z, density);
    return asset;
}

VolumeAsset VolumeAsset::GenerateRampVolume(uint32_t resolution) {
    VolumeAsset asset;
    resolution = std::clamp(resolution, 1u, kUzVolMaxEdge);
    asset.Allocate(resolution, resolution, resolution, VolumeFormat::R16F);
    for (uint32_t z = 0; z < resolution; ++z)
        for (uint32_t y = 0; y < resolution; ++y)
            for (uint32_t x = 0; x < resolution; ++x)
                asset.Store(x, y, z, (float(x) + 0.5f) / float(resolution));
    return asset;
}

VolumeAsset VolumeAsset::GenerateCloudVolume(uint32_t resolution, uint32_t seed) {
    VolumeAsset asset;
    resolution = std::clamp(resolution, 4u, kUzVolMaxEdge);
    asset.Allocate(resolution, resolution, resolution, VolumeFormat::R8);
    // Five-octave fBm of periodic value noise, remapped by a flattened
    // ellipsoid so the cloud fills the box with a cumulus-like silhouette and
    // fades to zero before the faces (no clipped density at the OBB walls).
    const float voxel = 2.f / float(resolution);
    for (uint32_t z = 0; z < resolution; ++z)
        for (uint32_t y = 0; y < resolution; ++y)
            for (uint32_t x = 0; x < resolution; ++x) {
                const float px = (float(x) + 0.5f) * voxel - 1.f, py = (float(y) + 0.5f) * voxel - 1.f,
                            pz = (float(z) + 0.5f) * voxel - 1.f;
                const float radial = std::sqrt(px*px + py*py + pz*pz*1.8f);
                const float shape = std::clamp(1.f - radial, 0.f, 1.f);
                float fbm = 0.f, amplitude = 0.5f, frequency = 4.f, total = 0.f;
                for (int octave = 0; octave < 5; ++octave) {
                    const float cells = frequency;
                    fbm += amplitude * ValueNoise((px + 1.f) * 0.5f * cells, (py + 1.f) * 0.5f * cells,
                                                  (pz + 1.f) * 0.5f * cells, uint32_t(cells), seed + uint32_t(octave) * 17u);
                    total += amplitude; amplitude *= 0.5f; frequency *= 2.f;
                }
                fbm /= total;
                // Puffy threshold: noise erodes the ellipsoid, dense at the core.
                const float density = std::clamp((shape * 1.4f + fbm * 1.6f - 1.05f) * 2.5f, 0.f, 1.f);
                asset.Store(x, y, z, density * std::clamp(shape * 4.f, 0.f, 1.f));
            }
    return asset;
}

// ── GPU ─────────────────────────────────────────────────────────────────────

bool VolumeAsset::Upload(IRHIDevice* device, IRHICommandList* cmdList) {
    if (!IsValid() || !device || !cmdList || m_texture.IsValid()) return false;
    const uint32_t bytesPerVoxel = BytesPerVoxel(m_format);
    // 256-byte aligned rows, height rows per slice, slices contiguous: the
    // layout CopyBufferToTexture expects for a volume (as the cloud noise upload).
    const uint32_t pitch = (m_width * bytesPerVoxel + 255u) & ~255u;
    std::vector<uint8_t> staged(uint64_t(pitch) * m_height * m_depth, 0);
    for (uint32_t z = 0; z < m_depth; ++z)
        for (uint32_t y = 0; y < m_height; ++y)
            std::memcpy(staged.data() + (uint64_t(z) * m_height + y) * pitch,
                        m_data.data() + (uint64_t(z) * m_height + y) * m_width * bytesPerVoxel,
                        size_t(m_width) * bytesPerVoxel);
    RHITextureDesc desc{};
    desc.width = m_width;
    desc.height = m_height;
    desc.depthOrArraySize = uint16_t(m_depth);
    desc.mipLevels = 1;
    desc.format = m_format == VolumeFormat::R16F ? RHIFormat::R16Float : RHIFormat::R8Unorm;
    desc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    desc.is3D = true;
    desc.debugName = "PathVolumeGrid";
    m_texture = device->CreateTexture(desc);
    if (!m_texture.IsValid()) return false;
    m_srv = device->GetTextureSRVIndex(m_texture);
    RHIBufferDesc upload{};
    upload.sizeBytes = staged.size();
    upload.usage = RHIBufferUsage::CopySrc;
    upload.memory = RHIMemoryType::Upload;
    upload.debugName = "PathVolumeGrid_Upload";
    m_staging = device->CreateBuffer(upload);
    void* mapped = m_staging.IsValid() ? device->MapBuffer(m_staging) : nullptr;
    if (m_srv == UINT32_MAX || !mapped) {
        if (mapped) device->UnmapBuffer(m_staging);
        ReleaseGPU(device);
        UZ_LOG(Renderer, Error, "VolumeAsset: Texture3D upload allocation failed ({}x{}x{})",
               m_width, m_height, m_depth);
        return false;
    }
    std::memcpy(mapped, staged.data(), staged.size());
    device->UnmapBuffer(m_staging);
    RHIBarrierDesc barrier{};
    barrier.texture = m_texture;
    barrier.stateBefore = RHIResourceState::Common;
    barrier.stateAfter = RHIResourceState::CopyDst;
    cmdList->ResourceBarrier(&barrier, 1);
    cmdList->CopyBufferToTexture(m_staging, 0, pitch, m_texture, 0, 0);
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);
    return true;
}

void VolumeAsset::ReleaseGPU(IRHIDevice* device) {
    if (!device) return;
    if (m_staging.IsValid()) device->DestroyBuffer(m_staging);
    if (m_texture.IsValid()) device->DestroyTexture(m_texture);
    m_staging = {};
    m_texture = {};
    m_srv = UINT32_MAX;
}

} // namespace uz
