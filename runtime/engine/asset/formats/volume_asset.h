/// UzEngine VolumeAsset — scalar density grids for heterogeneous participating media.
///
/// A dense, axis-aligned voxel grid of unit-less density in [0, maxDensity], sampled
/// trilinearly by the DXR path tracer's delta/ratio tracking (pathtracer_volume.hlsli,
/// `PathVolumeGrid*`). The grid covers the unit cube of a `VolumeComponent`'s oriented
/// box; the medium's extinction at a point is `(sigmaA + sigmaS) * density(x)`, so the
/// Woodcock majorant is `(sigmaA + sigmaS) * GetMaxDensity()`.
///
/// # .uzvol binary layout (little-endian)
/// ```
/// Offset  Size              Section
/// 0x0000  32 bytes          UzVolHeader: "UZVOL\0\0\0", version 1, width, height, depth,
///                           format (R8 = 0, R16F = 1), reserved
/// 0x0020  w*h*d*bytes       raw voxels, x fastest, then y, then z (slice-major)
/// ```
/// R8 stores density * 255 (UNORM); R16F stores the IEEE half of the density.
///
/// Built-in procedural grids avoid shipping binary fixtures: `builtin:sphere`,
/// `builtin:cloud`, `builtin:constant`, `builtin:ramp` (see `Resolve`).
///
/// @ref Novak et al. 2018 "Monte Carlo Methods for Volumetric Light Transport Simulation"

#pragma once

#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
class IVFS;

static constexpr uint32_t kUzVolVersion = 1;

enum class VolumeFormat : uint32_t {
    R8 = 0,    ///< 8-bit UNORM density (value / 255).
    R16F = 1,  ///< IEEE half-float density.
};

#pragma pack(push, 1)
/// .uzvol file header — exactly 32 bytes, little-endian.
struct UzVolHeader {
    char magic[8] = {'U', 'Z', 'V', 'O', 'L', 0, 0, 0};
    uint32_t version = kUzVolVersion;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t depth = 0;
    uint32_t format = 0;   ///< VolumeFormat
    uint32_t reserved = 0; ///< Must be 0.
};
#pragma pack(pop)
static_assert(sizeof(UzVolHeader) == 32, "UzVolHeader must be exactly 32 bytes");

/// Largest grid edge accepted by `Load` (256^3 R16F = 32 MiB).
static constexpr uint32_t kUzVolMaxEdge = 256;

class VolumeAsset {
public:
    // ── CPU grid ────────────────────────────────────────────────────────────

    /// Parses a .uzvol byte stream. On failure the asset is left empty and
    /// `error` (when given) names the first violated rule.
    bool Load(const std::vector<uint8_t>& bytes, std::string* error = nullptr);

    /// Reads `path` from the project VFS first, then the engine content
    /// directory next to the executable, then the working directory.
    bool LoadFile(IVFS* vfs, const std::string& path, std::string* error = nullptr);

    /// Serializes the grid as a .uzvol byte stream (empty when invalid).
    [[nodiscard]] std::vector<uint8_t> Serialize() const;

    /// Resolves a component's `densityGrid` string: `builtin:sphere`,
    /// `builtin:cloud`, `builtin:constant`, `builtin:ramp`, or a .uzvol path.
    static bool Resolve(const std::string& spec, IVFS* vfs, VolumeAsset& out,
                        std::string* error = nullptr);

    /// Solid sphere of `density` with radius `radius` in unit-cube units
    /// (1 = touches the faces), soft 1-voxel edge. R8 by default.
    static VolumeAsset GenerateSphereVolume(uint32_t resolution, float radius, float density,
                                            VolumeFormat format = VolumeFormat::R8);
    /// Every voxel at `density` (a homogeneous box: analytic Beer-Lambert).
    static VolumeAsset GenerateConstantVolume(uint32_t resolution, float density);
    /// density(x) = voxel-centre u coordinate, stored R16F so trilinear
    /// sampling reproduces the linear ramp (a known analytic optical depth).
    static VolumeAsset GenerateRampVolume(uint32_t resolution);
    /// Deterministic cumulus-like fBm cloud shaped by an ellipsoid falloff.
    static VolumeAsset GenerateCloudVolume(uint32_t resolution, uint32_t seed);

    [[nodiscard]] bool IsValid() const { return m_width && m_height && m_depth && !m_data.empty(); }
    [[nodiscard]] uint32_t Width() const { return m_width; }
    [[nodiscard]] uint32_t Height() const { return m_height; }
    [[nodiscard]] uint32_t Depth() const { return m_depth; }
    [[nodiscard]] VolumeFormat Format() const { return m_format; }
    [[nodiscard]] const std::vector<uint8_t>& Data() const { return m_data; }

    /// Largest decoded voxel value: the delta-tracking majorant factor.
    [[nodiscard]] float GetMaxDensity() const;
    /// Decoded voxel value (no bounds check beyond the grid).
    [[nodiscard]] float Voxel(uint32_t x, uint32_t y, uint32_t z) const;
    /// Trilinear sample at normalized coordinates in [0,1]^3, clamped to the
    /// voxel-centre range exactly like `PathVolumeGridDensity` in HLSL.
    [[nodiscard]] float Sample(float u, float v, float w) const;

    // ── GPU ─────────────────────────────────────────────────────────────────

    /// Creates the Texture3D and records its upload on `cmdList` (Common ->
    /// CopyDst -> ShaderResource). The staging buffer lives until `ReleaseGPU`.
    bool Upload(IRHIDevice* device, IRHICommandList* cmdList);
    void ReleaseGPU(IRHIDevice* device);
    [[nodiscard]] RHITextureHandle GetTexture3D() const { return m_texture; }
    [[nodiscard]] uint32_t GetSrvIndex() const { return m_srv; }

private:
    void Allocate(uint32_t w, uint32_t h, uint32_t d, VolumeFormat format);
    void Store(uint32_t x, uint32_t y, uint32_t z, float density);

    uint32_t m_width = 0, m_height = 0, m_depth = 0;
    VolumeFormat m_format = VolumeFormat::R8;
    std::vector<uint8_t> m_data;
    RHITextureHandle m_texture{};
    RHIBufferHandle m_staging{};
    uint32_t m_srv = UINT32_MAX;
};

} // namespace uz
