/// UzEngine Draw List
///
/// Sorted list of draw commands for a single render pass. Commands are added
/// unsorted then sorted by a 64-bit key that encodes pipeline, material, depth,
/// and mesh ID to minimize GPU state changes.

#pragma once

#include "mat4.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace uz {

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
