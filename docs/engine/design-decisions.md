# UzEngine — Design Decisions & Engine Comparisons

> This document records *why* UzEngine was designed the way it was, with explicit reference to the pain points observed in Unity, Unreal Engine, and Godot. These are not theoretical — they are based on practical experience with all three engines.

---

## 1. Engine Comparison Matrix

| Concern | Unity | Unreal | Godot | UzEngine |
|---------|-------|--------|-------|----------|
| **AI usability** | Medium (C# text, but `.meta`/GUID chaos) | Poor (binary `.uasset`, Blueprint-first) | Good (GDScript text, node tree) | **Best** (all plain text, path-based refs, LLM-generatable) |
| **Hot reload speed** | Slow (domain reload 1–10s in large projects) | Slow + unstable (Live Coding crashes) | Fast (GDScript, slow for C++) | **Fast** (DLL shadow copy, only game code recompiles) |
| **Crash on code change** | Yes (removed scripts leave zombie references) | Yes (access violation in Live Coding) | Rare | **No** (missing scripts → warning only, engine continues) |
| **Asset reference stability** | Fragile (GUID in `.meta`; delete `.meta` = broken) | Fragile (binary `.uasset` cross-refs) | Good (path-based) | **Good** (path-based, no `.meta`) |
| **Prefab / instantiation** | Good concept, slow `Instantiate()` | Heavy (UObject, GC) | Good (scene-in-scene) | **Good** (scene-in-scene, arena alloc, built-in pool) |
| **C++ ergonomics** | N/A (C# engine) | Poor (UObject boilerplate, RTTI macros) | N/A (GDScript primary) | **Clean** (plain C++ in `game.dll`, no macros) |
| **Visual quality ceiling** | Medium | High (Lumen, Nanite) | Medium | **Planned high** (custom DX12, PBR, GI Phase 3+) |
| **Editor stability** | Crashes increase with project size | Frequent crashes editing C++ | Stable | **VSCode never crashes** (engine is separate process) |

---

## 2. Specific Decisions

### 2.1 No `.meta` Files / No GUIDs

**Problem observed in Unity**: Asset references are GUIDs stored in `.meta` sidecar files. Deleting or moving a `.meta` without the asset (or vice versa) breaks every scene that used that asset — silently. In team environments this becomes a constant source of broken projects.

**Decision**: Asset identity = `res://`-relative path. Period. The source file *is* the reference. The engine never generates opaque identifiers for source assets.

```
✅  mesh = "res://assets/player.glb"     ← stable, human-readable, find-replaceable
❌  mesh = "{a3f8b2c1-4d5e-...}"         ← fragile, invisible to diff, breaks on rename
```

Import artifacts (`.uzmesh`, `.dds`, etc.) live in `.import/` which is gitignored and always regeneratable. They have no stable identity — they are caches, not sources.

---

### 2.2 Scripts Degrade Gracefully (No Zombie References)

**Problem observed in Unity**: Removing a `MonoBehaviour` class leaves GameObjects in scenes holding a reference to a now-missing type. Unity displays `Missing Script` in the Inspector and throws `NullReferenceException` at runtime. In large projects with many contributors this accumulates until the scene is broken.

**Problem observed in Unreal**: Live Coding partially patches object code. When a class is removed, the `UObject` subsystem cannot remap the reference, leading to access violations.

**Decision**: Scripts are identified by *file path* in the scene, not by type name or GUID. When the engine loads a scene, if a script file is not found:

1. Log a warning: `[scene] script not found: res://game/enemy_ai.cpp — node 'Enemy1' active without logic`
2. Node remains in the scene tree, visible, with its transform and components intact
3. Engine continues running — no crash, no broken scene
4. When the file is (re-)created, next scene reload re-attaches it automatically

This makes iterating with AI significantly safer: an AI agent can delete and recreate script files without ever breaking the scene state.

---

### 2.3 `game.dll` Boundary — C ABI, Not C++ Virtual

**Problem observed in Unreal**: Live Coding uses C++ `__declspec(dllexport)` with full C++ types across the DLL boundary. When the DLL is reloaded, vtables, RTTI typeinfo, and `UObject` registry entries become stale. Access violations follow. The Unreal team has struggled with this for years.

**Problem observed in Godot (GDExtension)**: Callables store C++ function pointers that become invalid after the extension DLL is reloaded, causing crash-on-editor-exit.

**Decision**: The only boundary between `UzEngine.exe` and `game.dll` is a **C struct of function pointers** (`game_api_t`). No C++ types cross the boundary. No vtables. No RTTI. The ABI is stable because:

- C function pointers have a stable calling convention (`__cdecl`)
- `GameContext*` is a versioned C struct (add fields only at end)
- State persistence is explicit (`save_state`/`load_state` by the game code)

This is the same approach used by id Software (Quake), Casey Muratori (Handmade Hero), and The Machinery engine — proven to work reliably across reload cycles.

---

### 2.4 VSCode as Editor — Engine Is a Separate Process

**Problem observed in Unreal**: The editor *is* the engine process. Crashing the engine crashes the editor. C++ compilation happens inside the editor process (via the build system). Any UB or access violation during development takes the whole editor down.

**Problem observed in Unity**: Similar — the Unity editor and the game runtime are the same process (with a domain boundary). A crash in game code can corrupt editor state.

**Decision**: VSCode is the editor UI. `UzEngine.exe` is the runtime. They communicate over IPC (file-based JSON) and WebSocket. If the engine crashes:

- VSCode keeps running
- The extension shows a "disconnected" status
- The extension can restart the engine process automatically
- No work is lost in the editor (scenes are files on disk)

This is also why we use JPEG streaming for the viewport — a WebView with a Canvas has no crash surface; it just shows a stale frame when the engine is down.

---

### 2.5 No Blueprint Equivalent

**Problem observed in Unreal**: Blueprint is a visual scripting system that makes rapid prototyping easy for designers but is essentially opaque to AI agents. Claude cannot read, write, or reason about a Blueprint graph stored in a `.uasset`.

**Decision**: There is no visual scripting in UzEngine. All game logic is C++ in `game.dll`. The tradeoff is that non-programmers cannot write game logic without C++ — but since UzEngine is AI-native, the expectation is that the AI agent writes the code based on plain-text intent instructions.

For data-driven behavior (state machines, dialogue trees, etc.), the formats are plain text:
- `.uzanim_sm` — animation state machine (INI-like)
- `.uzinput` — input action mappings (INI-like)
- Future: behavior tree format (INI-like)

---

### 2.6 Prefabs Are `.uzscene` Files

**Problem observed in Unity**: Prefabs are a separate concept from scenes, with their own binary `.prefab` format and their own GUID-based instance override system. Managing prefab variants, nested prefabs, and prefab override propagation is notoriously complex and error-prone.

**Problem observed in Godot**: Godot's "scenes as prefabs" model is simpler and works well. Instancing a scene creates a subtree copy; overrides are stored in the parent scene.

**Decision**: Prefabs in UzEngine are `.uzscene` files instantiated as subtrees. The engine expands them at load time by copying the node tree. Overrides are written as property assignments in the parent scene, after the `prefab = ...` line:

```ini
[node id=10 type=Node3D name="Enemy1" parent=1]
prefab = "res://prefabs/enemy.uzscene"

# Override individual properties after prefab expansion
[override node_path="Enemy1/Body" property="material"]
value = "res://materials/elite_enemy.uzmat"
```

No binary format. No GUID cross-references. AI can read and write this directly.

---

### 2.7 Object Pooling Is Opt-In, Not Required

**Problem observed in Unity**: `Instantiate()` allocates a new GameObject on the heap, with GC implications. For high-frequency spawning (bullets, particles), manual pooling is required and often reimplemented in every project.

**Decision**: UzEngine's arena allocation makes node creation cheap — no GC, no heap fragmentation. For most cases, `scene.instantiate_prefab()` is fast enough. For high-churn scenarios, a simple **enable/disable pool** is available as a utility:

```cpp
// One-time setup (load prefab instances into pool)
NodePool bullet_pool = NodePool::create(scene, "res://prefabs/bullet.uzscene", 100, level_arena);

// Per-frame: acquire (re-enable) and release (disable)
auto h = bullet_pool.acquire();
auto h = bullet_pool.release(h);
```

No custom pool implementation needed per project. The pool is just indices into the scene's existing node array — no allocation at spawn time.

---

## 3. What Godot Got Right (And We Adopt)

| Feature | Godot Implementation | Our Adaptation |
|---------|---------------------|---------------|
| **Node tree** | `Node` hierarchy, child/parent | Identical concept, C++ structs (no GDScript) |
| **Scene-as-prefab** | Any `.tscn` can be instanced | Any `.uzscene` can be instanced |
| **Signals** | Observer pattern, type-safe in GDScript | Phase 3: `EventBus<T>` (typed, no string dispatch) |
| **Path-based asset refs** | `res://` prefix | Identical — `res://` prefix |
| **No mandatory binary formats** | `.tscn` and `.tres` are text | All formats are text |
| **Recoverable missing resources** | Resource shows as missing, scene loads | Script missing → warning, scene loads |
| **Hot reload of scripts** | GDScript auto-reloads | C++ `game.dll` shadow-copy reload |

---

## 4. What Unreal Got Right (Targeted for Phase 2–3)

| Feature | Unreal Implementation | Our Plan |
|---------|----------------------|---------|
| **Visual quality** | Lumen GI, Nanite virtualized geometry | Phase 3+: Screen-space GI, clustered LOD |
| **Shadow quality** | Virtual Shadow Maps | Phase 2: 3-cascade CSM + PCF; Phase 3: VSM |
| **Physically-based materials** | Full PBR, material parameter collections | Phase 2: GGX BRDF, IBL, material instances |
| **Skeletal animation quality** | Full-body IK, blend spaces | Phase 3: FABRIK IK, animation state machine |
| **Physics fidelity** | Chaos Physics | Phase 3: Jolt Physics (MIT, high-perf) |

The visual quality gap is acknowledged. It will be closed incrementally, starting with a solid renderer foundation (Phase 2 PBR) before attempting GI.

---

## 5. What Unity Got Right (Selectively Adopted)

| Feature | Unity Implementation | Our Adaptation |
|---------|---------------------|---------------|
| **Ease of getting started** | Drag-and-drop prefab, Inspector | Inspector via VSCode Webview (Phase 1) |
| **Component clarity** | Inspector shows all components | Inspector shows all node properties |
| **Play mode isolation** | Play mode isolates runtime state | `--play` = separate process, total isolation |
| **Workflow speed** | Fast iteration for small projects | DLL hot reload targets same speed |

Unity's **Prefab** concept is its best feature. We adopt the idea (scene-as-prefab) while avoiding the implementation (GUID + `.meta`).

---

## 6. Scripting Model — C++ + Lua, Not One or the Other

### 6.1 Why Not C++ Only

Pure C++ scripting (like the initial design) has a hard limitation: **iteration speed**. Every behavior change requires a C++ recompile (~2.5s DLL hot-reload at best). For NPC behavior, triggers, pickups, UI events, and gameplay prototyping, that is too slow — especially when working with an AI agent that generates and iterates on code.

### 6.2 Why Not Lua Only

Lua alone cannot own the engine lifecycle, manage memory, call into DX12, or implement high-performance systems. And Lua-first designs (like GDScript) lose type safety and IDE tooling quality. Putting C++ back in means two runtimes that must agree on types — which is worse than one C++ layer that owns everything.

### 6.3 Decision: Lua Complements C++

```
game.dll (C++, required)
│  owns: engine lifecycle, ECS systems, physics callbacks, resource loading
│  owns: LuaJIT VM — creates it, registers bindings, drives scripts
│  owns: state persistence across hot-reload
│
LuaJIT VM (inside game.dll)
│  one per scene
│  each node can attach one .lua script
│  Lua sees: ctx.scene, ctx.input, Vec3, NodeRef, etc.
│  Lua cannot: touch GPU, allocate Arena memory, call OS APIs
```

**Speed spectrum in practice:**

| Change | What to modify | Reload time |
|--------|---------------|-------------|
| Renderer optimization | `src/renderer/` + full rebuild | ~30s (acceptable — rare) |
| New game system | `game.cpp` → `game.dll` rebuild | ~2.5s (frequent) |
| NPC behavior tweak | `res://game/enemy_ai.lua` save | ~60ms (constant iteration) |

### 6.4 Why LuaJIT (Not Lua 5.4, Not Wren, Not Python)

| Criterion | LuaJIT 2.1 | Lua 5.4 | Wren | Python |
|-----------|-----------|---------|------|--------|
| Performance | JIT ~10x faster | Interpreter | Bytecode VM | Interpreter |
| Game industry adoption | High (WoW, Roblox, LÖVE2D) | High | Low | Low |
| C binding ease | Excellent (FFI) | Good (C API) | Good | Complex |
| Binary size | ~800KB JIT / ~200KB interp | ~400KB | ~100KB | ~8MB |
| AI familiarity | Very high | Very high | Low | Very high |
| Hot-reload support | Excellent | Good | Limited | Fragile |

LuaJIT's FFI is the decisive advantage: in dev builds, Lua code can directly access C structs without binding overhead — useful for per-frame data reads that would otherwise require a binding call.

### 6.5 What Godot Got Right Here

Godot's GDScript-per-node model is the right idea. We adopt it:
- Each node has one optional script attachment
- Script file = module with `on_start` / `on_update` / `on_stop`
- Missing script = graceful degradation
- Hot-reload replaces only the changed script, not the entire scene

We replace GDScript with Lua (better performance, better AI tooling, industry standard) and add C++ as the primary heavy-lifting layer below it.

---

### 2.8 (Legacy note — see Section 6)

The original architecture note ("all logic in game.dll, no visual scripting") still holds for complex systems. Section 6 adds Lua as a per-node complementary layer. The design decision table in Section 1 has been updated:

| **Scripting** | C# (Unity) | Blueprint/C++ (Unreal) | GDScript/C++ (Godot) | **C++ game.dll + Lua per-node** |
