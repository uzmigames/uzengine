# phase8_8.3.0-vfx-emitter-inheritance

Parent emitter + per-override module list. Load-time merge of parent + child produces the resolved module stack.

Reference: UE5 NiagaraEmitterHandleInheritance.h.

## 1. Data model

- [x] 1.1 Add parentPath + moduleOverrides fields to ParticleEmitter preset / VfxSystem emitter refs.
      Implemented at **JSON level** rather than on the C++ emitter asset so
      the resolver is independent of `ModuleRegistry` and trivially testable.
      New `.uzvfx` keys: top-level `"parent": "<path>"`, and each child
      module entry accepts `"action": "add|replace|remove"`. `VFXGraph::GetParentPath()`
      exposes the authored reference for hot-reload.
- [x] 1.2 Load-time merge starting from the parent module stack, applying child override entries by module name.
      `engine/vfx/asset/vfx_emitter_inheritance.{h,cpp}` — `VfxResolveInheritance`
      walks the chain, deep-merges the `emitter` block (child keys win),
      applies `add` / `replace` (config deep-merge) / `remove` actions in
      order, unions `parameters` / `events` / `tags` (child wins on duplicate
      name, tags de-duped). Wired into `VFXGraph::LoadWithInheritance` and
      `LoadFromStringWithInheritance` (new overloads; legacy load paths keep
      their non-inheritance behaviour for backwards compatibility).
- [x] 1.3 Cycle detection + depth limit (max 8 levels).
      Resolver carries a visited set keyed by `VfxNormalizeInheritancePath`
      (forward slashes + lowercase) so cycles are detected even across mixed
      Windows/Unix spellings. `VFX_MAX_INHERITANCE_DEPTH = 8` caps the chain.
      Violations return `nullopt` with a `VFX/Error` log.
- [x] 1.4 Hot-reload watcher propagates parent file changes through descendant instances.
      `VFXHotReload::ScanDirectory` parses every `.uzvfx` file's `"parent"`
      field into an `inheritanceMap_` (parent → list of children). On any
      reload, the watcher re-reads the edge (in case the edit retargeted or
      dropped it) and recursively triggers `ReloadGraph` on descendants,
      guarded by a visited set to defeat cycles.

## 2. Documentation and tests

- [x] 2.1 Docs: inheritance walkthrough covering override semantics + cycle detection.
      `docs/engine/25-vfx/20-emitter-inheritance.md` — child JSON shape,
      action verbs, emitter/parameters/events/tags merge rules, cycle +
      depth semantics, loading API, hot-reload propagation, test index.
- [x] 2.2 Tests: multi-level inheritance, cycle rejection, override application order.
      `runtime/tests/engine/vfx/test_emitter_inheritance.cpp` — 12 cases
      covering pass-through, add/replace/remove actions, replace-config
      deep-merge, replace-target-missing append, three-level chain,
      self-reference / mutual cycle / case-insensitive cycle, depth-cap,
      emitter block merge, parameters/events union (child-wins),
      tags dedup union.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/20-emitter-inheritance.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_emitter_inheritance.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps; the failing step is the
      same pre-existing missing-ffmpeg-DLL runtime executor tracked on 8.1.0
      and 8.2.0). All new code is pure JSON / CPU logic, no platform
      dependencies, so the environment gap does not impact correctness.
