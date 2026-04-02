#pragma once

/// Per-emitter module stack defining spawn, update, and render behavior.
///
/// Each emitter has an ordered list of spawn modules (run once per new particle),
/// update modules (run every frame per alive particle), and an optional render
/// module. The `ModuleCompositor` uses these lists to generate composed compute
/// shaders.
///
/// # Dirty Flag
/// Adding or removing modules sets `dirty = true`. The simulation pass checks
/// this flag and recomposes shaders via `ModuleCompositor` before dispatch.
/// After composition, `dirty` is cleared and `composedCacheKey` is updated.
///
/// # Thread Safety
/// Not thread-safe. All methods must be called on the main/render thread.

#include "engine/vfx/modules/vfx_module.h"
#include "engine/rhi/rhi_types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// EmitterModuleStack -- per-emitter collection of composable VFX modules
// ---------------------------------------------------------------------------

/// Per-emitter collection of VFX modules that define its behavior.
///
/// Holds spawn modules (initialize new particles), update modules (simulate
/// alive particles each frame), and an optional render module. When any
/// module list changes, the stack marks itself dirty so the simulation pass
/// knows to recompose the HLSL shaders.
///
/// # Ownership
/// The stack owns all modules via `std::unique_ptr`. Raw pointer accessors
/// (`GetUpdateModulePtrs`, `GetSpawnModulePtrs`) return non-owning views
/// valid only while the stack is not modified.
struct EmitterModuleStack {
    // -- Module lists --------------------------------------------------------

    /// Modules executed when spawning new particles (in order).
    std::vector<std::unique_ptr<IVFXModule>> spawnModules;

    /// Modules executed every frame on alive particles (in order).
    /// Typical order: UpdateModule, TurbulenceModule, ConstraintModule, DecayModule.
    std::vector<std::unique_ptr<IVFXModule>> updateModules;

    /// Optional render module (e.g., SpriteRenderModule).
    std::unique_ptr<IVFXModule> renderModule;

    // -- Composition cache ---------------------------------------------------

    /// Cached composed update shader HLSL source (empty if not yet composed).
    std::string composedUpdateHLSL;

    /// Cached composed spawn shader HLSL source (empty if not yet composed).
    std::string composedSpawnHLSL;

    /// Cache key for the current module combination (0 = not composed yet).
    uint64_t composedCacheKey = 0;

    /// Compiled compute PSO for the composed update shader (invalid = not compiled).
    RHIPipelineHandle composedUpdatePSO;

    /// Compiled compute PSO for the composed spawn shader (invalid = not compiled).
    RHIPipelineHandle composedSpawnPSO;

    /// Whether the composed shaders need recomposition (modules changed).
    bool dirty = true;

    // -- Mutators ------------------------------------------------------------

    /// Adds a spawn module to the end of the spawn list. Marks stack as dirty.
    ///
    /// # Parameters
    /// - `mod`: Module to add. Must not be null.
    ///
    /// # Side Effects
    /// Sets `dirty = true`.
    void AddSpawnModule(std::unique_ptr<IVFXModule> mod);

    /// Adds an update module to the end of the update list. Marks stack as dirty.
    ///
    /// # Parameters
    /// - `mod`: Module to add. Must not be null.
    ///
    /// # Side Effects
    /// Sets `dirty = true`.
    void AddUpdateModule(std::unique_ptr<IVFXModule> mod);

    /// Sets the render module, replacing any existing one. Marks stack as dirty.
    ///
    /// # Parameters
    /// - `mod`: Module to set. May be null to clear the render module.
    ///
    /// # Side Effects
    /// Sets `dirty = true`.
    void SetRenderModule(std::unique_ptr<IVFXModule> mod);

    /// Clears all modules and resets composition cache.
    ///
    /// After this call, `spawnModules` and `updateModules` are empty,
    /// `renderModule` is null, cached HLSL strings are cleared, and
    /// `dirty` is set to true.
    void Clear();

    // -- Accessors -----------------------------------------------------------

    /// Returns non-owning pointers to update modules (for ModuleCompositor).
    ///
    /// # Returns
    /// Vector of raw pointers. Valid only while the stack is not modified.
    /// Empty if no update modules are registered.
    [[nodiscard]] std::vector<IVFXModule*> GetUpdateModulePtrs() const;

    /// Returns non-owning pointers to spawn modules (for ModuleCompositor).
    ///
    /// # Returns
    /// Vector of raw pointers. Valid only while the stack is not modified.
    /// Empty if no spawn modules are registered.
    [[nodiscard]] std::vector<IVFXModule*> GetSpawnModulePtrs() const;

    /// Returns true if the stack has any update or spawn modules.
    [[nodiscard]] bool HasModules() const noexcept;
};

} // namespace uz
