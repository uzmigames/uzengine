---
name: Unity Architecture Analysis for Roadmap
description: Unity-based lessons and patterns for UzEngine roadmap restructuring
type: project
---

# Unity Architecture Analysis for UzEngine Roadmap

## 1. SRP Abstraction — Lessons for RHI to RenderGraph

### Pattern: Decouple "What to Render" from "How to Render"

**Unity's Evolution**: Built-in RP was monolithic — lighting, shadows, post-processing all
hardcoded. SRP (2018) introduced `ScriptableRenderContext` as the abstraction boundary.
The key insight: `DrawRenderers(cullingResults, drawingSettings, filteringSettings)` separates
visibility decisions from draw submission. `RenderStateBlock` overrides blend/depth/stencil
without touching the pipeline object. This let Unity ship URP (mobile) and HDRP (AAA) from
the same codebase.

**UzEngine Status**: RHI (1.2) is complete. RenderWorld doc (12-01) already has the right
dual-world architecture with sync points. The RenderGraph doc (12-04) follows the correct
DAG model.

**Lesson**: Unity's SRP mistake was shipping it before `RenderGraph` existed. Early SRP
required manual barrier management and resource lifetime tracking — exactly what RenderGraph
later automated. UzEngine's roadmap correctly places RenderGraph before advanced rendering
features, but the current plan has RenderGraph at "Milestone 2.0" which is too late.

**Recommendation**: Move RenderGraph to 1.3.1 (after triangle-to-screen). Build deferred
pipeline ON TOP of RenderGraph from day one. Unity regretted retrofitting RenderGraph onto
existing SRP code (the "compatibility mode" pain in Unity 2023+). Never write manual
barriers in renderer code.

### Pattern: CommandBuffer as Backend Abstraction

**Unity's Approach**: `CommandBuffer` wraps GPU commands independently of Metal/Vulkan/DX12.
You never call backend-specific APIs from rendering code. SRP render passes record into
CommandBuffers, which the backend translates.

**UzEngine Parallel**: `RgContext` in the render graph doc serves this role. The rule
"NEVER access RHI directly from pass callbacks" is correct. This is exactly Unity's lesson:
once you have the graph, the context object IS your command recording interface.

**Risk**: The current RenderProxy uses a `switch` on `RenderProxyType` for draw call
generation (doc 12-01, line 78: "Type-specific behavior lives in the draw call generator
(switch on type)"). This is the anti-pattern Unity moved away from. Unity's SRP solved
this with `DrawRenderers()` filtering by `RenderQueue` and `ShaderTagId` — no switch needed.

**Recommendation**: Replace the planned switch-on-type in `GenerateDrawCalls()` with a
draw-key sorting approach. Each RenderProxy gets a 64-bit sort key encoding
(material | mesh | distance). The renderer iterates sorted proxies linearly. No branching
on proxy type during the hot loop.

---

## 2. RenderGraph API (Unity 2023+) — Lessons for UzEngine

### Pattern: Declare-Then-Execute

**Unity's RenderGraph**: Passes declare resource usage (UseColorBuffer, UseDepthBuffer,
ReadTexture, WriteTexture) during setup. Execution only happens if the pass survives
culling. Resources are "virtual" until the graph compiles — physical allocation happens
at compile time based on lifetime analysis.

**UzEngine's Design**: The RgPass Read/Write + SetExecution pattern in doc 12-04 mirrors
this correctly. The virtual resource model (RgTexture/RgBuffer with deferred physical
handles) is right.

**Unity's Mistake**: Early RenderGraph (2021-2022) had a "compatibility mode" that let
legacy code bypass the graph. This created a two-path maintenance burden. Unity 6 finally
forced everything through the graph — breaking changes, but cleaner.

**Lesson for UzEngine**: Never build a "compatibility mode" for non-graph rendering. From
1.3.1 onward, ALL render passes go through the graph. No escape hatches.

### Pattern: Transient Resource Lifetime

**Unity's Model**: `RenderGraphBuilder.CreateTransientTexture()` creates resources that
exist only within a frame. The graph aliases them automatically. Persistent resources
(history buffers for TAA, shadow atlases) are imported via `ImportTexture()`.

**UzEngine's Model**: The aliasing plan in doc 12-04 covers this. Missing detail: the
distinction between transient (per-frame, aliasable) and persistent (cross-frame, imported)
resources needs explicit API separation.

**Recommendation**: Add `graph.ImportTexture(rhi_handle)` for persistent resources alongside
`graph.CreateTexture(desc)` for transients. Unity learned that mixing the two without clear
API boundaries causes subtle bugs (aliasing a persistent resource corrupts history).

---

## 3. Job System — C# to C++ Translation

### Pattern: IJob/IJobParallelFor to C++ Work-Stealing

**Unity's C# Model**: `IJob` is a struct with an `Execute()` method. `IJobParallelFor` adds
`Execute(int index)` called per-element. The Burst compiler turns these into native SIMD code.
`NativeArray<T>` provides ownership tracking — jobs that write to the same NativeArray are
detected at schedule time and error out.

**UzEngine's Model**: Doc 07-04 uses `void (*work)(void*)` function pointers with raw data.
This is lower-level than Unity's typed jobs. The `ParallelFor` template is the right approach.

**Key Unity Lesson — Safety**: Unity's job system catches data races at schedule time via
NativeArray safety handles. This is the single most valuable feature for shipping a
multi-threaded engine without constant crashes.

**Recommendation**: Add a debug-mode data access validator. Each `ParallelFor` call should
take a `ReadAccess<T>` or `WriteAccess<T>` wrapper that, in debug builds, tracks which
arrays are currently being written. Attempting to schedule a job that reads an array another
job is writing triggers an immediate assert. This catches 90% of threading bugs at schedule
time, not at runtime crash time.

### Pattern: JobHandle Dependencies vs Counter/Fence

**Unity's Model**: `JobHandle` returned from `Schedule()`. Chain with
`job2.Schedule(dependsOn: handle1)`. `JobHandle.Complete()` blocks.

**UzEngine's Model**: `Counter` + `Fence` with explicit `Wait()`.

**Comparison**: Unity's approach is more composable — you can build complex DAGs by passing
handles around. UzEngine's Counter model is simpler but forces linear phase execution
(enqueue all, wait, next phase). Unity allows overlapping phases naturally.

**Recommendation**: Consider adding a `JobHandle` return from `Enqueue()` and allowing
`Enqueue({..., .depends_on = prior_handle})`. This enables the animation+physics overlap
pattern without restructuring into rigid phases. The current "NEVER enqueue jobs from job
callbacks" rule (doc 07-04 line 398) prevents this — reconsider that rule. Unity explicitly
allows job-spawned jobs, which enables recursive parallelism (e.g., BVH build).

### Threading Model Priority

**Unity's Lesson**: Ship single-threaded first, profile, then parallelize the hottest paths.
Unity's original Update() loop was single-threaded. Jobs came later (2018) for specific
systems (animation, physics, particles). DOTS pushed jobs-everywhere but most Unity games
still use MonoBehaviour single-threaded update.

**Recommendation for UzEngine**: The current plan (single-thread MVP, dual-thread Phase 3)
is correct. But implement the job system BEFORE the renderer (move from 1.5 to 1.3).
Reason: visibility culling (embarrassingly parallel) is the first system that benefits.
If you build culling single-threaded, you will rewrite it when jobs arrive. Build jobs
early, use them for culling from day one.

---

## 4. DOTS Lessons — Where ECS-First Pays Off

### Where ECS-First Wins

**Transform hierarchy**: Unity's old Transform was a class with parent pointers, O(n)
hierarchy walks. DOTS TransformSystem processes all LocalToWorld in parallel via chunks.
UzEngine's plan to use EnTT is correct — but the transform propagation (step 3 in frame
pipeline, doc 12-03) should use `ParallelFor` on dirty subtree roots from day one.

**Visibility culling**: Unity DOTS processes `RenderBounds` + `LocalToWorld` in parallel
chunks. This is 10x faster than per-entity frustum tests. UzEngine's CullPartition approach
(doc 07-04 line 291) already follows this pattern.

**Particle updates**: GPU compute is better, but CPU fallback benefits from ECS data layout.

### Where ECS-First is Premature

**Gameplay logic**: Unity's biggest DOTS mistake was pushing ECS for gameplay code. Most
game logic is inherently sequential, stateful, and branchy — terrible for ECS. MonoBehaviour
is still preferred for gameplay in Unity 6. UzEngine should keep gameplay in traditional
components (the `UZ_COMPONENT` pattern) and reserve ECS data layout for hot-path systems
only (transforms, culling, particles).

**Audio**: Unity's audio is NOT ECS. It uses a separate thread with a command queue. UzEngine's
audio plan (doc 17) correctly uses a separate thread + lock-free queue. Do not ECS-ify audio.

**UI**: Unity's UI Toolkit is NOT ECS. Retain-mode UI with its own layout engine. UzEngine's
HTML/CSS approach is correct — do not try to make UI data-oriented.

### Recommendation

Use EnTT for: transforms, visibility, static mesh instances, lights, particles (CPU fallback).
Use traditional OOP for: gameplay scripts, audio, UI, physics integration (Jolt owns its data).

---

## 5. Unity's Editor-Before-Runtime Mistake

### The Lesson

Unity built the editor (Inspector, Hierarchy, Scene view) before the runtime was stable. This
created a permanent coupling: `[SerializeField]`, `[HideInInspector]`, `OnValidate()` are
all editor concepts that leaked into runtime code. The `UnityEditor` vs `UnityEngine` split
is incomplete — you cannot ship a headless server without stripping editor assemblies manually.

### UzEngine's Correct Approach

The roadmap places editor (Phase 2) after all runtime systems (Phase 1 + 1.5). This is right.

**Additional safeguards**:
1. The property system (doc 21) must compile WITHOUT editor code. `UZ_PROPERTY` should work
   for serialization and networking even if no editor exists. The current design achieves this
   — `PropertyInfo` is a pure data struct with no editor dependencies.
2. Never add editor-only fields to runtime components. Unity's `[ExecuteInEditMode]` created
   a class of bugs where components behave differently in editor vs build.
3. The `PropertyFlags::Hidden` / `PropertyFlags::ReadOnly` flags are editor hints. They should
   be stored in a SEPARATE editor metadata table, not in the runtime `PropertyInfo` struct.
   Unity put `[HideInInspector]` on the runtime field — wrong place.

**Recommendation**: Split `PropertyInfo` into:
- `RuntimePropertyInfo` — name, type, offset, size, default_value, flags (Replicated, SaveGame, Transient)
- `EditorPropertyMeta` — tooltip, category, min, max, Hidden, ReadOnly

Runtime never includes editor metadata. Editor looks up metadata by (type_id, property_name).

---

## 6. Asset Pipeline Evolution

### Unity's Three Eras

1. **Resources.Load (2005-2017)**: Flat folder, load by path, no dependencies. Simple but no
   caching, no incremental builds, no cross-references.

2. **AssetBundle (2017-2020)**: Manual dependency management, version conflicts, cache
   invalidation nightmares. The "DLL hell" of game assets.

3. **Addressables (2020+)**: Content-addressable, async loading, automatic dependency tracking,
   remote + local catalogs. Finally correct, but retrofitting onto AssetBundle was painful.

### UzEngine's Approach

The roadmap has: Shader Compiler (1.5.0, done) then Asset Processor (1.5.1) then Packager (1.5.2).
The content-hash cache decision (xxHash64, noted in MEMORY.md) is correct — Unity's Addressables
uses content hashing too.

**Key Unity Lesson**: Build the dependency graph FIRST, before any asset processing. Unity's
AssetBundle hell happened because bundles were created without understanding dependencies.
When Addressables added the dependency graph, it broke backward compatibility with all
existing bundles.

**Recommendation**: In 1.5.1, the FIRST deliverable should be the asset dependency graph
(already listed in the roadmap table). Build it before texture compression or mesh optimization.
Every subsequent asset operation queries the graph. This prevents the AssetBundle mistake.

### Unity's Import Pipeline

Unity imports assets on first access, caching the result in `Library/`. Reimport happens when
the source file or import settings change. The importer is deterministic — same input always
produces same output.

**UzEngine Parallel**: The `.cache/` directory with content-hash keys achieves this. Ensure
importers are deterministic (no timestamps, no random seeds unless explicitly seeded).

---

## 7. Recommended System Ordering (Unity-Informed)

Based on Unity's evolution and mistakes, here is the recommended adjustment to UzEngine's
milestone order:

```
Current Plan:                    Recommended Adjustment:
─────────────────────────        ─────────────────────────
1.0 Bootstrap          OK       1.0 Bootstrap
1.1 Math               OK       1.1 Math + Containers
1.2 RHI                OK       1.2 RHI (DX12)
1.3.0 Triangle         OK       1.2.5 Job System (move UP from 1.5)
1.3.1 Deferred         risky    1.3.0 Triangle to Screen
1.3.2 Post-Processing           1.3.1 Render Graph (move UP from 2.0)
1.5.0 Shader Compiler           1.3.2 Deferred Pipeline (built ON graph)
1.4 Scene System                1.3.3 Post-Processing (built ON graph)
1.3.3 Advanced Rendering        1.5.0 Shader Compiler (parallel track)
                                1.4 Scene System + Property System (merge)
                                1.3.4 Advanced Rendering
                                1.5.1 Asset Processor (dep graph first)
```

### Rationale for Changes

1. **Job System before Renderer**: Visibility culling is the first parallel workload. Build
   it parallel from the start. Unity retrofitted jobs onto culling — painful.

2. **RenderGraph before Deferred**: Never write manual barriers. Unity's SRP compatibility
   mode was a maintenance burden. Build deferred ON the graph.

3. **Property System with Scene System**: Unity shipped scenes before reflection was solid.
   Result: multiple incompatible serialization paths. Build them together.

4. **Asset dependency graph early in 1.5.1**: Unity's AssetBundle → Addressables migration
   was the most painful transition in Unity history. Get the graph right first.

---

## 8. Where Unity's "Ship Simple, Refactor Later" Applies

### DO ship simple first:
- **Forward shading MVP** (1.3.0) before deferred — Unity's built-in forward RP proved
  that a working forward renderer is valuable for testing the full pipeline
- **Single-thread MVP** before multi-thread — Unity DOTS proved that premature threading
  creates bugs without measurable benefit until you have enough work to parallelize
- **JSON scene format** before binary — Unity's text-based .unity scenes are terrible for
  performance but invaluable for debugging and diffing during development
- **EnTT out-of-the-box** before custom ECS — Unity spent years on their custom ECS;
  EnTT is mature and sufficient for Phase 1-2

### Do NOT ship simple:
- **RenderGraph** — build it right from the start; Unity's "compatibility mode" was the
  wrong shortcut
- **Asset dependency tracking** — build the graph before any processing; Unity's lack of
  early dependency tracking created AssetBundle hell
- **Property system runtime/editor split** — separate them from day one; Unity's
  [SerializeField] + [HideInInspector] coupling was never fixed
- **RHI abstraction** — already done correctly in 1.2; never bypass it

---

## 9. Patterns UzEngine Should Adopt from Unity

| Pattern | Unity Source | UzEngine Application |
|---------|-------------|---------------------|
| Declare-then-execute graph | RenderGraph API | RgPass Read/Write + SetExecution (already planned) |
| Virtual resources with deferred allocation | RenderGraphBuilder | RgTexture/RgBuffer (already planned) |
| Pass culling for disabled effects | RenderGraph.CullUnusedPasses | Bloom disabled = pass culled (already planned) |
| Async compute hints | RenderGraph queue annotations | RgQueueType::Compute (already planned) |
| Content-addressable assets | Addressables | xxHash64 cache (already decided) |
| Opt-in reflection | [SerializeField] is opt-in | UZ_PROPERTY macro (already planned) |
| Debug data race detection | NativeArray safety handles | Add ReadAccess/WriteAccess wrappers |
| ParallelFor with auto-partitioning | IJobParallelFor | ParallelFor template (already planned) |
| Dual-world render separation | SRP ScriptableRenderContext | RenderWorld + sync point (already planned) |

## 10. Patterns UzEngine Should Avoid from Unity

| Anti-Pattern | What Unity Did | What UzEngine Should Do Instead |
|-------------|---------------|-------------------------------|
| Editor in runtime | [HideInInspector] in runtime code | Separate EditorPropertyMeta table |
| Compatibility mode | SRP compat mode alongside RenderGraph | Graph-only from day one |
| GC for entity lifetime | C# GC manages GameObjects | Generation-checked handles (already planned) |
| Universal base class | Everything inherits UObject | No base class, opt-in UZ_COMPONENT |
| Switch-on-component-type | Legacy renderer iterated by type | Sort-key based draw call generation |
| Late dependency graph | AssetBundles without dep tracking | Dependency graph as first asset system deliverable |
| ECS for gameplay | DOTS pushed for all code | ECS for hot paths only, OOP for gameplay |
| Job spawning restrictions | Early DOTS banned job-from-job | Allow controlled recursive job spawning |
