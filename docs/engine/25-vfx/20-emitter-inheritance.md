# VFX emitter inheritance

Niagara-style parent → child inheritance for `.uzvfx` graph assets. A child
emitter references a parent and overrides only the modules it changes, so a
whole family of effects can share a base and a single edit on the parent
ripples through every descendant.

Reference: UE5 `NiagaraEmitterHandleInheritance.h` + `UNiagaraEmitter::bIsTemplateAsset`.

## Child JSON shape

```json
{
  "format":  "uzvfx",
  "version": 1,
  "parent":  "effects/base_smoke.uzvfx",
  "emitter": { "max_particles": 2000 },
  "modules": [
    { "type": "Gravity", "action": "replace", "config": { "strength": 9.8 } },
    { "type": "Burst",   "action": "add",     "config": { "count": 500 } },
    { "type": "Drag",    "action": "remove" }
  ],
  "parameters": [ ... ],
  "events":     [ ... ],
  "tags":       [ ... ]
}
```

| Key          | Behaviour                                                            |
|--------------|----------------------------------------------------------------------|
| `parent`     | Path to another `.uzvfx` — resolved recursively.                     |
| `emitter`    | Deep-merged with the parent; child keys win, unknown parent keys kept|
| `modules`    | Applied with an `action` verb (see below).                           |
| `parameters` | Union-by-`name`; child wins on duplicate.                            |
| `events`     | Union-by-`name`; child wins on duplicate.                            |
| `tags`       | Union with deduplication (order: parent first, then child).          |

## Module action verbs

Each child module entry may declare:

- `"add"` (default) — appended after every parent module.
- `"replace"` — finds the first parent module with the same `type`, deep-merges
  its `config` with the child override (child keys win), then substitutes the
  child entry back in place. When no parent match exists, the override is
  appended and a `VFX/Warning` is emitted.
- `"remove"` — drops the first parent module with the same `type`. Missing
  target logs `VFX/Warning`.

After resolution every `action` field is stripped so the downstream
`ModuleRegistry` parser never sees a key it does not understand.

## Cycle detection and depth cap

The resolver walks the parent chain with a `std::unordered_set` of normalised
parent paths. If a path reappears the resolution aborts with a
`VFX/Error` and a `nullopt` result (no partial graph is built). Additionally,
the chain depth is capped at `VFX_MAX_INHERITANCE_DEPTH = 8`. Path
normalisation (`VfxNormalizeInheritancePath`) converts backslashes to forward
slashes and lower-cases everything so `Effects\\Fire.uzvfx` and
`effects/fire.uzvfx` count as the same file.

## Loading API

- `VFXGraph::LoadWithInheritance(path, registry, loader, vfs)` — disk / VFS
  entry point. `loader` typically closes over the same VFS.
- `VFXGraph::LoadFromStringWithInheritance(jsonStr, registry, loader)` — used
  by tests and the editor preview pipeline.

Both record the authored `"parent"` path on the resulting graph — read via
`VFXGraph::GetParentPath()` — so the hot-reload watcher can find descendants
without re-parsing JSON.

The legacy `VFXGraph::Load` / `LoadFromString` entry points still accept the
`"parent"` key; they simply record it without resolving the chain. Call sites
that need inheritance must switch to the `WithInheritance` overloads.

## Hot-reload propagation

`VFXHotReload::ScanDirectory` parses every `.uzvfx` file's `"parent"` key and
stores the edge in an `inheritanceMap_`. When `ReloadGraph(path)` fires it:

1. Calls the user-registered `OnReload(path)` callbacks.
2. Re-reads the `"parent"` key in case the edit added / removed / retargeted it.
3. Recursively calls `ReloadGraph` on every descendant that references the
   changed file, guarded by a visited set so cycles are handled safely.

## Tests

`runtime/tests/engine/vfx/test_emitter_inheritance.cpp` covers 12 cases:

- pass-through when no parent (strips leftover `action`);
- single-level `add` / `replace` / `remove`;
- `replace` config deep-merge preserves parent-only keys;
- `replace` target missing → appends + warns;
- three-level chain preserves override order;
- self-reference, mutual cycle, case-insensitive cycle all rejected;
- depth-cap rejects over-deep chains;
- `emitter` block key-merge with child overriding;
- `parameters` / `events` union with child-wins-on-duplicate;
- `tags` union with deduplication.
