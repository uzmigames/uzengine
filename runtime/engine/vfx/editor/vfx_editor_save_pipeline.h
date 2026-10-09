#pragma once

/// Editor save-pipeline hook — extension point for pre-save validation and
/// post-save side effects (phase 10.11.0).
///
/// Phase 9.5 (editor inline validator warnings) and phase 9.7 (thumbnail-
/// on-save) both need a spot to run logic around the VFX editor's Save
/// action without modifying each asset's `SaveToFile` implementation.
/// This header ships that extension point as a standalone registry: any
/// caller that wants hooks uses the `SaveWithPipeline` helper around
/// their existing save call; any caller that doesn't care keeps calling
/// `SaveToFile` directly and pays zero cost.
///
/// # Hook types
///
/// - **PreSave** — runs before the bytes hit disk. Returning `false`
///   aborts the save; used by the validator (9.5) to block invalid
///   assets.
/// - **PostSave** — runs after the save succeeds. Used by the thumbnail
///   capturer (9.7) to trigger a headless render.
///
/// # Dispatch rules
///
/// - Extensions run in registration order.
/// - `PreSave` returns `false` on the first dissenting extension; later
///   extensions are skipped. This keeps the "the validator said no"
///   semantics predictable (subsequent extensions would see stale data
///   and could mask the abort reason).
/// - `PostSave` is infallible from the pipeline's POV — individual
///   extensions log their own failures and move on.
///
/// # Thread safety
///
/// Not thread-safe. Save-path calls happen on the main / editor thread
/// only. Register / unregister must happen on the same thread or during
/// a stop-the-world moment.
///
/// @ref docs/engine/25-vfx/43-editor-save-pipeline.md

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Asset kinds — a single enum so extensions can filter cheaply.
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxSaveAssetKind : uint8_t {
    Unknown       = 0,
    EmitterPreset = 1, ///< .uzfx emitter serializer output
    VfxSystem     = 2, ///< .uzvfxsys compound system
    NodeGraph     = 3, ///< .uzvfxgraph (phase 8.10 DSL graph)
    TangentCurve  = 4, ///< .uztangentcurve (phase 7.16 tangent curve)
    Snapshot      = 5, ///< .uzvfxsnap (phase 8.19)
    BakeAsset     = 6, ///< .uzvfxbake (phase 7.18)
    Material      = 7, ///< .uzmat VFX material
    EffectType    = 8, ///< .uzvfxtype effect type spec
};

/// Returns a stable string for logs / inspector. Never null.
[[nodiscard]] const char* VfxSaveAssetKindName(VfxSaveAssetKind kind) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Extension interface
// ─────────────────────────────────────────────────────────────────────────────

/// Base class for save-pipeline extensions.
///
/// Default implementations are no-ops so subclasses only override the
/// hook they care about.
class IEditorSaveExtension {
public:
    virtual ~IEditorSaveExtension() = default;

    /// Runs before the asset is written to disk.
    ///
    /// # Arguments
    /// - `kind`     — asset type discriminator; extensions can filter.
    /// - `asset`    — opaque pointer to the asset being saved. Cast using
    ///                `kind`. May be null for asset types that don't
    ///                pre-serialise into a struct (raw JSON saves).
    /// - `outputPath` — filesystem path the asset will land at.
    ///
    /// # Returns
    /// `true` to proceed; `false` to abort the save. The pipeline stops
    /// iterating on the first `false` and reports the aborting
    /// extension's name via `lastAbortReason_`.
    virtual bool PreSave(VfxSaveAssetKind /*kind*/,
                          const void* /*asset*/,
                          const std::string& /*outputPath*/) {
        return true;
    }

    /// Runs after a successful save. Individual failures are the
    /// extension's responsibility to log.
    virtual void PostSave(VfxSaveAssetKind /*kind*/,
                           const void* /*asset*/,
                           const std::string& /*outputPath*/) {}

    /// Human-readable name for diagnostics (e.g., "VfxValidator",
    /// "ThumbnailCapture"). Default empty — concrete extensions
    /// override.
    [[nodiscard]] virtual const char* Name() const noexcept { return ""; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline registry
// ─────────────────────────────────────────────────────────────────────────────

using EditorSaveExtensionHandle = uint32_t;
constexpr EditorSaveExtensionHandle kInvalidEditorSaveExtensionHandle = 0u;

/// Global-per-process extension registry. One editor / one process so a
/// singleton fits; tests can reset via `Clear()`.
class EditorSavePipeline {
public:
    /// Returns the process-wide instance. Lazy-initialised.
    static EditorSavePipeline& Instance() noexcept;

    /// Registers an extension. Returns a stable handle or
    /// `kInvalidEditorSaveExtensionHandle` when `extension` is null.
    EditorSaveExtensionHandle Register(IEditorSaveExtension* extension);

    /// Unregisters by handle. No-op on invalid / unknown handles.
    void Unregister(EditorSaveExtensionHandle handle) noexcept;

    /// Runs every registered PreSave hook. Returns true when every hook
    /// returned true; false on the first abort. `outAbortReason` captures
    /// the aborting extension's name (empty when nothing aborted).
    bool RunPreSave(VfxSaveAssetKind kind,
                    const void* asset,
                    const std::string& outputPath,
                    std::string& outAbortReason);

    /// Convenience overload that discards the abort reason.
    bool RunPreSave(VfxSaveAssetKind kind,
                    const void* asset,
                    const std::string& outputPath) {
        std::string discard;
        return RunPreSave(kind, asset, outputPath, discard);
    }

    /// Runs every registered PostSave hook. Never aborts.
    void RunPostSave(VfxSaveAssetKind kind,
                      const void* asset,
                      const std::string& outputPath);

    [[nodiscard]] uint32_t ExtensionCount() const noexcept;

    /// Clears every registered extension. Used by tests + scene unload.
    void Clear() noexcept;

private:
    EditorSavePipeline() = default;

    struct Slot {
        EditorSaveExtensionHandle handle;
        IEditorSaveExtension*     extension;
    };
    std::vector<Slot>         slots_;
    EditorSaveExtensionHandle nextHandle_ = 1u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper wrapping a user save callback
// ─────────────────────────────────────────────────────────────────────────────

/// Wraps a save callback with PreSave / PostSave dispatch.
///
/// Use pattern at any call site:
/// ```cpp
/// bool ok = VfxSaveWithPipeline(
///     VfxSaveAssetKind::VfxSystem, &sysAsset, outputPath,
///     [&]() { return sysAsset.SaveToFile(outputPath); });
/// ```
///
/// # Returns
/// - `false` when PreSave aborted (save callback was never invoked);
/// - the save callback's return value otherwise.
///
/// PostSave only fires when the save callback returned true.
template <typename SaveFn>
bool VfxSaveWithPipeline(VfxSaveAssetKind kind,
                          const void* asset,
                          const std::string& outputPath,
                          SaveFn&& saveFn) {
    auto& pipe = EditorSavePipeline::Instance();
    std::string abortReason;
    if (!pipe.RunPreSave(kind, asset, outputPath, abortReason)) {
        return false;
    }
    const bool saved = saveFn();
    if (saved) {
        pipe.RunPostSave(kind, asset, outputPath);
    }
    return saved;
}

} // namespace uz
