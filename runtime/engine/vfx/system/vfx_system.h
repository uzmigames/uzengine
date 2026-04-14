/// UzEngine VfxSystem — Multi-emitter compound VFX asset (Niagara parity).
///
/// A `VfxSystem` is an authorable asset that contains references to multiple
/// emitters, enabling compound effects (e.g., fireball = fire head + sparks +
/// smoke) to be expressed as a single unit. The runtime counterpart is
/// `VfxSystemInstance` which owns child `ParticleEmitter` instances and
/// propagates transform/lifecycle state to them each frame.
///
/// # Asset Format (.uzvfxsys)
/// JSON file with structure:
/// ```json
/// {
///   "format": "uzvfxsys",
///   "version": 1,
///   "name": "Fireball",
///   "emitters": [
///     {"name": "head",   "path": "preset:FireballHead",   "id": "e1", "localSpace": true},
///     {"name": "sparks", "path": "preset:FireballSparks", "id": "e2", "localSpace": true},
///     {"name": "smoke",  "path": "preset:FireballSmoke",  "id": "e3", "localSpace": true}
///   ]
/// }
/// ```
///
/// # emitterPath Resolution
/// - `"preset:<Name>"` — resolved at runtime to an inline preset function
///   defined in `engine/vfx/components/emitter_presets.h`
///   (e.g., `"preset:FireballHead"` -> `ApplyPreset_FireballHead`).
/// - Any other string — treated as a path to a `.uzfx` asset and loaded
///   via `UzfxLoader` (if the `.uzfx` pipeline is present).
///
/// # Distinction from `VFXSystem` (note capitalisation)
/// `VFXSystem` (all caps) is the global runtime singleton that owns the
/// particle budget and dispatches per-frame compute work across all
/// registered emitters. `VfxSystem` (mixed case) is a **compound asset**
/// — a group of emitters authored together. A `VfxSystemInstance` registers
/// each of its child emitters with the global `VFXSystem`.
///
/// @ref UE5 `NiagaraSystem.h` — multi-emitter asset architecture
/// @ref UE5 `NiagaraEmitterHandle.h` — emitter reference struct

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace uz {

class IVFS;

/// Reference to a single emitter within a `VfxSystem` (Niagara `EmitterHandle` parity).
///
/// Describes *where* to get the emitter data (preset name or `.uzfx` path) and
/// *how* the child emitter should interact with its parent system's transform.
struct VfxSystemEmitterRef {
    /// Human-readable display name for tooling (e.g., "head", "sparks", "smoke").
    /// Not required to be unique; `id` is the stable identifier.
    std::string name;

    /// Path to the source emitter definition.
    /// Either `"preset:<Name>"` (resolved to a preset function) or a `.uzfx`
    /// asset path. Must not be empty.
    std::string emitterPath;

    /// Stable identifier for this emitter slot within the system.
    /// Persists across re-orderings of the `emitters` array, so references
    /// (e.g., parameter bindings) remain valid. Auto-generated when missing.
    std::string id;

    /// When false, the emitter is skipped during `VfxSystemInstance::Init`.
    /// Useful for authoring variants of a system without deleting emitters.
    bool enabled = true;

    /// When true (default), the child emitter's world position is updated
    /// each frame to match the parent system's transform.
    /// When false, the emitter stays at world origin regardless of system
    /// movement (useful for one-shot world-space bursts).
    bool localSpace = true;
};

/// VFX System asset — a group of emitters authored and instantiated as one unit.
///
/// Loaded from `.uzvfxsys` JSON files. Runtime instantiation creates a
/// `VfxSystemInstance` which owns one `ParticleEmitter` per enabled
/// `VfxSystemEmitterRef`.
///
/// # Lifecycle
/// 1. `Load(path, vfs)` — parse JSON from VFS or disk.
/// 2. Pass asset to `VfxSystemInstance::Init(*asset, ...)` to spawn child emitters.
/// 3. Asset can be shared/cached across multiple instances (read-only after load).
struct VfxSystem {
    /// Display name of the system (from the JSON "name" field).
    std::string name;

    /// Emitter references in authoring order. Each entry yields one child
    /// `ParticleEmitter` when instantiated (subject to `enabled` flag).
    std::vector<VfxSystemEmitterRef> emitters;

    /// Path from which this asset was loaded. Empty when loaded from an
    /// in-memory string via `LoadFromString`.
    std::string sourcePath;

    /// Loads a `.uzvfxsys` JSON file and validates its schema.
    ///
    /// # Arguments
    /// - `path`: Virtual or disk path to the `.uzvfxsys` file.
    /// - `vfs`: Optional VFS; when null, falls back to direct disk IO.
    ///
    /// # Returns
    /// Owned asset on success, `nullptr` on parse/validation failure.
    /// All failure reasons are logged with VFX category.
    [[nodiscard]] static std::unique_ptr<VfxSystem> Load(const std::string& path,
                                                          IVFS* vfs = nullptr);

    /// Parses a JSON string directly (used by tests and the editor).
    ///
    /// # Returns
    /// Owned asset on success, `nullptr` on parse/validation failure.
    /// `sourcePath` is empty on the resulting asset.
    [[nodiscard]] static std::unique_ptr<VfxSystem> LoadFromString(const std::string& jsonStr);

    /// Serialises this asset to a `.uzvfxsys` JSON file on disk.
    ///
    /// # Returns
    /// `true` on success, `false` if the file cannot be opened/written.
    [[nodiscard]] bool SaveToFile(const std::string& path) const;
};

} // namespace uz
