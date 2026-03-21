/// UzEngine Draw List
///
/// Sorted list of draw commands for a single render pass. Commands are added
/// unsorted then sorted by a 64-bit key that encodes pipeline, material, depth,
/// and mesh ID to minimize GPU state changes.

#pragma once

#include "mat4.h"
#include "engine/rhi/rhi_types.h"  // RHITextureHandle

#include <algorithm>
#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EMobility — primitive mobility classification for shadow caching
// ─────────────────────────────────────────────────────────────────────────────

/// Primitive mobility for shadow cache draw list filtering.
///
/// Mirrors `SceneComponent::Mobility` at the renderer level to avoid coupling
/// the draw list to ECS headers. The scene renderer maps from the ECS enum
/// to this enum when building draw commands.
///
/// Standard mobility classification for shadow caching and culling.
enum class EMobility : uint8_t {
    Static,      ///< Never moves — eligible for shadow map caching.
    Stationary,  ///< Baked lighting but may have dynamic shadows. Treated as Static for shadow cache.
    Movable      ///< Fully dynamic — re-rendered every frame.
};

// ─────────────────────────────────────────────────────────────────────────────
// DrawCommand — a single recorded draw invocation
// ─────────────────────────────────────────────────────────────────────────────

/// A single draw command recorded into a `DrawList`.
///
/// The renderer iterates sorted commands in order and issues one GPU draw call
/// per command. The `sortKey` controls the iteration order — see `SortKey`.
struct DrawCommand {
    /// 64-bit sort key that encodes pipeline, material, depth, and mesh ID.
    ///
    /// Build this with `SortKey::Opaque` or `SortKey::Transparent`.
    uint64_t sortKey = 0;

    /// Object-to-world transform (column-major).
    Mat4 worldMatrix;

    /// Transpose of the inverse model matrix, used for correct normal transforms.
    ///
    /// Precomputed on the CPU to avoid per-pixel matrix inversion in the shader.
    Mat4 normalMatrix;

    /// Index into the mesh resource table.
    uint32_t meshId = 0;

    /// SRV heap index for BaseColor map (0 = white default).
    uint32_t albedoSrvIndex    = 0;
    /// SRV heap index for normal map (0 = flat normal).
    uint32_t normalSrvIndex    = 0;
    /// SRV heap index for roughness map (0 = 0.5 default).
    uint32_t roughnessSrvIndex = 0;
    /// SRV heap index for metallic map (0 = 0.0 default).
    uint32_t metallicSrvIndex  = 0;
    /// SRV heap index for AO map (0 = 1.0 default).
    uint32_t aoSrvIndex        = 0;
    /// SRV heap index for emissive map (0 = no emissive texture, use color only).
    uint32_t emissiveSrvIndex  = 0;

    /// Emissive tint color (linear RGB). Default (0,0,0) = no emission.
    /// Emissive tint is added to scene color during the lighting pass.
    float emissiveColorR  = 0.0f;
    float emissiveColorG  = 0.0f;
    float emissiveColorB  = 0.0f;
    /// Emissive brightness multiplier. 0 = no emission. Values > 1.0 drive bloom.
    float emissiveIntensity = 0.0f;

    // ── Material parameters (defaults are neutral — no change to existing rendering) ──

    /// Albedo tint: BaseColor * BaseColorTint (industry-standard material model).
    float baseColorTintR      = 1.0f;  ///< Albedo color multiplier R.
    float baseColorTintG      = 1.0f;  ///< Albedo color multiplier G.
    float baseColorTintB      = 1.0f;  ///< Albedo color multiplier B.
    float roughnessMultiplier = 1.0f;  ///< Roughness texture multiplier.
    float metallicMultiplier  = 1.0f;  ///< Metallic texture multiplier.
    float normalIntensity     = 1.0f;  ///< Normal map strength (0=flat, 1=full).
    float uvScaleX            = 1.0f;  ///< Texture tiling X.
    float uvScaleY            = 1.0f;  ///< Texture tiling Y.
    float uvOffsetX           = 0.0f;  ///< Texture offset X.
    float uvOffsetY           = 0.0f;  ///< Texture offset Y.
    float aoIntensity         = 1.0f;  ///< AO map strength.
    float opacity             = 1.0f;  ///< Material opacity (1=opaque).

    // ── POM (Parallax Occlusion Mapping) ─────────────────────────────────
    float    displacementScale = 0.0f;  ///< POM height scale. 0 = disabled.
    uint32_t heightmapSrvIndex = 0;     ///< SRV index for heightmap. 0 = none.
    float    pomSelfShadow     = 0.0f;  ///< Self-shadow strength [0,1].

    // ── Blend Mode ─────────────────────────────────────────────────────────
    /// Standard blend mode enum: Opaque, Masked, Translucent, Additive.
    uint32_t materialFlags     = 0;     ///< bits[1:0]=blend mode (0-3), bit[2]=ORM packed.
    /// Default alpha clip threshold: 1/3 (industry convention for masked materials).
    float    alphaClipThreshold = 0.3333f; ///< Alpha test discard threshold for Masked mode.
    uint32_t twoSided          = 0;     ///< 0 = back-face cull, 1 = no cull (two-sided rendering).
    uint32_t opacitySrvIndex   = 0;     ///< SRV index for opacity/alpha mask texture (0 = none, use opacity scalar).

    // ── Material Graph Support ────────────────────────────────────────────

    /// If valid, this draw command uses a material-graph-compiled PSO instead
    /// of the default GBuffer PSO. The pipeline was compiled by MaterialPipeline
    /// from the material's graph HLSL.
    RHIPipelineHandle materialGraphPipeline;

    /// Material graph parameter data (16 x float4 = 256 bytes, uploaded to b3).
    /// Only used when materialGraphPipeline is valid.
    float materialParams[64] = {};

    /// Number of active material parameter float4 slots (0-16).
    uint32_t materialParamCount = 0;

    /// Primitive mobility for shadow cache filtering.
    ///
    /// Static/Stationary primitives are cached in the static shadow atlas.
    /// Movable primitives are rendered every frame into the dynamic shadow atlas.
    /// Default: Movable (fully dynamic, backward compatible).
    EMobility mobility = EMobility::Movable;

    /// Texture handles for SM5 (non-bindless) SetFragmentTexture binding.
    /// These are ignored on SM6.6 bindless where SRV indices are used directly.
    RHITextureHandle albedoTexture;
    RHITextureHandle normalTexture;
    RHITextureHandle roughnessTexture;
    RHITextureHandle metallicTexture;
    RHITextureHandle aoTexture;
    RHITextureHandle emissiveTexture;
};

// ─────────────────────────────────────────────────────────────────────────────
// SortKey — 64-bit sort key construction helpers
// ─────────────────────────────────────────────────────────────────────────────

/// 64-bit sort key construction for draw call ordering.
///
/// Keys are designed to minimise GPU state changes when commands are sorted
/// ascending. The bit layout encodes the most expensive state changes in the
/// most-significant bits so they change least often.
///
/// # Bit Layout
/// ```
/// [63-56]  Pipeline/Shader ID  (8 bits  — PSO switches are the most expensive)
/// [55-48]  Material ID         (8 bits  — texture binds are the second-most expensive)
/// [47-24]  Depth               (24 bits — front-to-back reduces pixel shader invocations)
/// [23- 0]  Mesh ID             (24 bits — batching same geometry avoids VB/IB rebinds)
/// ```
///
/// For transparent objects use `Transparent()` which inverts the depth field
/// so that commands sort back-to-front (required for correct alpha blending).
namespace SortKey {

/// Builds an opaque sort key using front-to-back depth ordering.
///
/// Ascending sort by this key minimises overdraw for opaque geometry because
/// closer surfaces (small depth) are drawn before farther ones.
///
/// # Arguments
/// * `pipelineId` — PSO/shader identifier in [0, 255].
/// * `materialId` — Material index in [0, 255].
/// * `depth`      — Normalised linear depth in [0, 1] where 0 = near clip, 1 = far clip.
///                  Values outside [0, 1] are clamped.
/// * `meshId`     — Mesh resource index. Only the lower 24 bits are used.
///
/// # Returns
/// A 64-bit sort key. Sort ascending for correct front-to-back ordering.
[[nodiscard]] inline uint64_t Opaque(uint8_t pipelineId,
                                     uint8_t materialId,
                                     float   depth,
                                     uint32_t meshId) noexcept
{
    // Map [0,1] float to 24-bit integer. Saturate to avoid UB from out-of-range cast.
    const float clamped = depth < 0.0F ? 0.0F : (depth > 1.0F ? 1.0F : depth);
    const uint32_t depthBits = static_cast<uint32_t>(clamped * 16777215.0F); // 2^24 - 1

    return (static_cast<uint64_t>(pipelineId) << 56) |
           (static_cast<uint64_t>(materialId) << 48) |
           (static_cast<uint64_t>(depthBits)  << 24) |
           (static_cast<uint64_t>(meshId & 0x00FF'FFFFu));
}

/// Builds a transparent sort key using back-to-front depth ordering.
///
/// Ascending sort by this key draws far objects before near objects, which is
/// required for correct Porter-Duff alpha blending over opaque geometry.
///
/// # Arguments
/// * `pipelineId` — PSO/shader identifier in [0, 255].
/// * `materialId` — Material index in [0, 255].
/// * `depth`      — Normalised linear depth in [0, 1]. Inverted internally.
///                  Values outside [0, 1] are clamped.
/// * `meshId`     — Mesh resource index. Only the lower 24 bits are used.
///
/// # Returns
/// A 64-bit sort key. Sort ascending for correct back-to-front ordering.
[[nodiscard]] inline uint64_t Transparent(uint8_t pipelineId,
                                          uint8_t materialId,
                                          float   depth,
                                          uint32_t meshId) noexcept
{
    // Invert depth so deeper objects get smaller keys → sorted first.
    const float clamped = depth < 0.0F ? 0.0F : (depth > 1.0F ? 1.0F : depth);
    const uint32_t depthBits = static_cast<uint32_t>((1.0F - clamped) * 16777215.0F);

    return (static_cast<uint64_t>(pipelineId) << 56) |
           (static_cast<uint64_t>(materialId) << 48) |
           (static_cast<uint64_t>(depthBits)  << 24) |
           (static_cast<uint64_t>(meshId & 0x00FF'FFFFu));
}

} // namespace SortKey

// ─────────────────────────────────────────────────────────────────────────────
// DrawList — sorted container of draw commands for one render pass
// ─────────────────────────────────────────────────────────────────────────────

/// Sorted list of draw commands for a single render pass.
///
/// The typical per-frame workflow is:
/// 1. `Clear()` at the start of the frame.
/// 2. `Add()` one command per visible mesh.
/// 3. `Sort()` once, after all commands are recorded.
/// 4. Iterate `GetCommands()` in order to issue GPU draw calls.
///
/// This class is NOT thread-safe. Use from the render thread only.
class DrawList {
public:
    DrawList() = default;
    ~DrawList() = default;

    // Non-copyable, movable.
    DrawList(const DrawList&) = delete;
    DrawList& operator=(const DrawList&) = delete;
    DrawList(DrawList&&) noexcept = default;
    DrawList& operator=(DrawList&&) noexcept = default;

    /// Records a draw command.
    ///
    /// The command is appended unsorted. Call `Sort()` after all commands for
    /// the frame have been added.
    ///
    /// # Arguments
    /// * `cmd` — Draw command to append. Copied into the internal vector.
    void Add(const DrawCommand& cmd) { m_commands.push_back(cmd); }

    /// Sorts all recorded commands by sort key in ascending order.
    ///
    /// Must be called before iterating `GetCommands()`. Sorting is stable is
    /// NOT guaranteed — commands with equal sort keys may appear in any order.
    void Sort() {
        std::sort(m_commands.begin(), m_commands.end(),
            [](const DrawCommand& a, const DrawCommand& b) noexcept {
                return a.sortKey < b.sortKey;
            });
    }

    /// Discards all recorded commands.
    ///
    /// Call once at the start of every frame before recording new commands.
    /// Does not release the backing allocation (capacity is retained).
    void Clear() noexcept { m_commands.clear(); }

    /// Returns the recorded command list.
    ///
    /// If `Sort()` has been called the commands are in ascending sort-key order.
    /// If not, the order matches insertion order.
    ///
    /// # Returns
    /// Const reference to the internal vector. Valid until the next `Add()` or
    /// `Clear()` call.
    [[nodiscard]] const std::vector<DrawCommand>& GetCommands() const noexcept {
        return m_commands;
    }

    /// Returns the number of recorded commands.
    [[nodiscard]] size_t Count() const noexcept { return m_commands.size(); }

private:
    std::vector<DrawCommand> m_commands;
};

} // namespace uz
