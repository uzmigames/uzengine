#pragma once

/// VFX emitter inheritance — load-time merge of parent + child `.uzvfx` assets.
///
/// Niagara solves the "explosion + sparks + smoke + blood" multiplication
/// problem by letting a child emitter reference a parent and override only the
/// modules it needs. UzEngine implements this at the JSON level so the resolver
/// is independent of the `ModuleRegistry` and easy to test.
///
/// # Child JSON additions
/// ```json
/// {
///   "format": "uzvfx",
///   "version": 1,
///   "parent": "effects/base_smoke.uzvfx",
///   "emitter": { "max_particles": 2000 },
///   "modules": [
///     { "type": "Gravity", "action": "replace", "config": { "strength": 9.8 } },
///     { "type": "Burst",   "action": "add",     "config": { "count": 500 } },
///     { "type": "Drag",    "action": "remove" }
///   ]
/// }
/// ```
///
/// # Action semantics
/// Each entry in the child `modules` array may declare an `action`:
///
/// - `"add"` (default when omitted): appended after all parent modules.
/// - `"replace"`: replaces the first parent module with the same `type`,
///   merging the parent's `config` with the child's (child keys win).
///   When no parent match exists, the override is appended (i.e. treated
///   like `add`) and a `VFX/Warning` is emitted.
/// - `"remove"`: drops the first parent module matching `type`. Emits a
///   `VFX/Warning` when the target is missing.
///
/// # Cycle detection and depth limit
/// The resolver carries a set of normalised parent paths already on the stack.
/// A recursive reference terminates the chain with a `VFX/Error` and a `nullopt`
/// result. Additionally, the chain depth is capped at `VFX_MAX_INHERITANCE_DEPTH`
/// (8) so an unintentionally long chain fails fast instead of running out of
/// stack.
///
/// # Emitter block merge
/// The child's `emitter` block is key-level merged into the parent's — child
/// keys override, unknown parent keys are preserved. Parameters and events are
/// appended from parent to child in declaration order. Duplicate parameter /
/// event names keep the child definition and warn.
///
/// @ref UE5 NiagaraEmitterHandleInheritance.h
/// @ref docs/engine/25-vfx/20-emitter-inheritance.md

#include <json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>

namespace uz {

/// Maximum inheritance chain depth.
///
/// A chain longer than this rejects at resolve time. 8 levels covers the
/// deepest Niagara hierarchies seen in shipped titles (base → family → variant
/// → per-character tuning).
inline constexpr uint32_t VFX_MAX_INHERITANCE_DEPTH = 8u;

/// Parent-JSON loader callback. Given a path (as written in the child's
/// `"parent"` key), returns the parent's JSON text. Empty string signals
/// "file not found" and aborts resolution with an error.
///
/// The callback is invoked once per unique parent path in the chain. Callers
/// typically close over the VFS handle they already use elsewhere:
/// ```cpp
/// VfxInheritanceLoader loader = [&](const std::string& p) {
///   auto bytes = vfs->Read(p);
///   return std::string(bytes.begin(), bytes.end());
/// };
/// ```
using VfxInheritanceLoader =
    std::function<std::string(const std::string& parentPath)>;

/// Resolves inheritance on `childJson` by merging its parent chain.
///
/// When `childJson` declares a `"parent"` path, the resolver fetches the
/// parent via `loader`, recurses on its own `"parent"` key, and merges
/// modules / emitter / parameters / events back onto the result. When there
/// is no parent the input is returned unchanged (minus action metadata on
/// module entries, which is stripped so `VFXGraph::ParseModules` doesn't see
/// unexpected keys).
///
/// # Arguments
/// - `childJson`  — parsed JSON object for the child asset.
/// - `loader`     — callback that reads a parent JSON file by path.
/// - `maxDepth`   — inheritance chain cap; defaults to VFX_MAX_INHERITANCE_DEPTH.
///
/// # Returns
/// - Resolved JSON object when the chain is valid.
/// - `std::nullopt` when a cycle is detected, depth is exceeded, a parent
///   cannot be loaded, or the parent JSON fails to parse. All failure
///   reasons are logged under the VFX category.
///
/// # Thread safety
/// Not thread-safe — the loader callback is invoked sequentially from the
/// thread that calls `VfxResolveInheritance`.
[[nodiscard]] std::optional<nlohmann::json>
VfxResolveInheritance(const nlohmann::json& childJson,
                      const VfxInheritanceLoader& loader,
                      uint32_t maxDepth = VFX_MAX_INHERITANCE_DEPTH);

/// Editor variant of `VfxResolveInheritance`. Differs from the runtime
/// path in that `action: "remove"` modules are NOT dropped — instead they
/// are kept in the resolved module list and tagged with
/// `"override": "OverrideRemove"` so the editor's stack panel can render
/// them dimmed + struck-through (matching Niagara's "Show Removed Modules"
/// view).
///
/// All other behaviour matches `VfxResolveInheritance` — same tagging of
/// surviving modules (`Inherited` / `OverrideAdd` / `OverrideReplace`),
/// same parameter / event / tag merging, same cycle detection.
[[nodiscard]] std::optional<nlohmann::json>
VfxResolveInheritanceForEditor(const nlohmann::json& childJson,
                               const VfxInheritanceLoader& loader,
                               uint32_t maxDepth = VFX_MAX_INHERITANCE_DEPTH);

/// Internal recursive entry — exposed only for tests and for callers that
/// need to pass their own visited set (e.g. the hot-reload watcher tracking
/// partial reloads).
[[nodiscard]] std::optional<nlohmann::json>
VfxResolveInheritanceImpl(const nlohmann::json& childJson,
                          const VfxInheritanceLoader& loader,
                          std::unordered_set<std::string>& visited,
                          uint32_t depth,
                          uint32_t maxDepth);

/// Normalises a parent-reference path for the cycle-detection set.
///
/// Converts backslashes to forward slashes and lower-cases the result so
/// `Effects\\Fire.uzvfx` and `effects/fire.uzvfx` compare equal (Windows is
/// case-insensitive; this matches VFXHotReload's own path normalisation).
[[nodiscard]] std::string VfxNormalizeInheritancePath(const std::string& path);

} // namespace uz
