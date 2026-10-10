#pragma once

/// `.uzvfxbake` asset — offline-baked flipbook atlas of a VFX effect.
///
/// Mirrors Niagara's Baker output: a headless run of an effect captures
/// N frames into a 2D atlas texture; the runtime then samples that atlas
/// at high LOD distances instead of simulating the full effect. Cheapest
/// possible fallback rendering for background / distant VFX.
///
/// # Asset format (`.uzvfxbake`)
///
/// JSON metadata pointing at the atlas texture:
///
/// ```json
/// {
///   "format":      "uzvfxbake",
///   "version":     1,
///   "source":      "vfx/systems/fireball.uzvfxsys",
///   "atlas_path":  "vfx/bakes/fireball_atlas.png",
///   "frame_count": 36,
///   "rows":        6,
///   "cols":        6,
///   "fps":         30.0,
///   "duration_s":  1.2,
///   "cell_width":  256,
///   "cell_height": 256
/// }
/// ```
///
/// The atlas texture itself is produced by the `uzbake vfx` CLI (deferred
/// to `phase7_7.18.1-vfx-baker-cli-integration`). The runtime LOD swap
/// that consumes the bake lives with the CLI so loader + renderer land
/// together.
///
/// # Runtime consumption (future)
///
/// Once the LOD swap ships, high-LOD emitters will reference a
/// `VfxBakeAsset` and the billboard VS will compute the frame index
/// from effect-local time:
///
/// ```
/// frameIndex = min(frac(ageNormalised) * frameCount, frameCount - 1)
/// ```
///
/// The atlas is sampled with the flipbook helpers shipped in 3.91.0.
///
/// @ref UE5 FNiagaraBakerRenderer + UNiagaraBakerSettings

#include <cstdint>
#include <memory>
#include <string>

namespace uz {

class IVFS;

/// Fields loaded from `.uzvfxbake` JSON.
struct VfxBakeAsset {
    /// Path to the `.uzvfxsys` the bake was captured from. Used for
    /// round-trip debugging + re-baking workflows.
    std::string sourcePath;

    /// Path to the atlas texture (PNG / BC7 — decided by the bake CLI).
    /// Resolved via the engine VFS at runtime.
    std::string atlasPath;

    /// Number of captured frames. `rows * cols` must equal this.
    uint32_t frameCount = 0u;

    /// Atlas grid dimensions. `rows * cellHeight` and `cols * cellWidth`
    /// define the final texture size.
    uint32_t rows = 0u;
    uint32_t cols = 0u;

    /// Playback rate authored during the bake. The runtime advances the
    /// flipbook at this rate when cycling; editor tooling may resample
    /// to a different rate at load time.
    float fps = 30.0f;

    /// Total baked duration in seconds. Derived from frameCount / fps
    /// when set to 0, but stored explicitly so downstream math doesn't
    /// need to recompute it.
    float durationSeconds = 0.0f;

    /// Per-cell pixel dimensions. Full-atlas size = cols * cellWidth
    /// by rows * cellHeight. Stored explicitly so decoders can reserve
    /// buffers without loading the atlas first.
    uint32_t cellWidth  = 0u;
    uint32_t cellHeight = 0u;

    /// Returns true when the metadata is internally consistent:
    /// `rows * cols == frameCount` and positive cell dimensions.
    /// Does NOT verify the atlas file exists — that's a VFS-side
    /// concern handled by the runtime loader.
    [[nodiscard]] bool IsWellFormed() const noexcept;

    /// Loads the `.uzvfxbake` JSON metadata. Returns `nullptr` on any
    /// parse / schema failure (logged with VFX category). The atlas
    /// texture itself is NOT loaded — callers hand `atlasPath` to their
    /// texture loader after this returns.
    [[nodiscard]] static std::unique_ptr<VfxBakeAsset> Load(const std::string& path,
                                                             IVFS* vfs = nullptr);

    /// Parses an in-memory JSON string. Same failure contract as `Load`.
    [[nodiscard]] static std::unique_ptr<VfxBakeAsset> LoadFromString(const std::string& jsonStr);

    /// Writes the metadata as a `.uzvfxbake` JSON file. Returns `true`
    /// on success. The bake CLI calls this after producing the atlas.
    [[nodiscard]] bool SaveToFile(const std::string& path) const;
};

} // namespace uz
