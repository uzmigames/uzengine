# 11. Runtime and editor lifecycle - implementation design (F-009, F-010, F-011, F-020)

Return to the [index](README.md). Design only: no code, build or engine run was
performed. Line numbers refer to the working tree on 2026-10-15 and must be
rechecked before editing. Each section ends with the order and risks it assumes.

Shared facts used below:

- Vendored EnTT packs a 32-bit entity as `index | version << 20`:
  `entity_mask = 0xFFFFF`, `version_mask = 0xFFF`
  ([entt.hpp](../../../runtime/thirdparty/entt/single_include/entt/entt.hpp):11828-11829),
  `to_entity` (:11884), `to_version` (:11893), `construct` (:11917), `next`
  skips version `0xFFF`, so one slot cycles through 4095 versions (:11902-11905).
  `registry::valid` checks the version **and** the free-list boundary (:38125-38127).
- The editor runs the engine in a forked worker: extension host ->
  `RuntimeClient` (IPC) -> `runtime-worker.ts` (Node main thread, loads
  `uzengine.node`) -> `RenderThread` (`std::thread`) -> `uz_render_frame`.
  Frames return through two `Napi::ThreadSafeFunction`s, `process.send`
  (base64), `webview.postMessage`, and a WebCodecs `VideoDecoder`.

---

## F-009 (2.1) - ECS slot/version bookkeeping

### Root cause

`World` keys its generation table by the **packed** EnTT id, not the slot:

- [world.cpp](../../../runtime/engine/ecs/core/world.cpp):16-24 - `registry_.create()`,
  `slot = entt::to_integral(enttEntity)` (packed value), `EnsureGenerationSlot(slot)`,
  `generations_[slot]++`, `handle.id = slot`.
- :80-84 `EnsureGenerationSlot` resizes `generations_` to `packed + 1`.
- :38-53 `IsValid` indexes `generations_[entity.id]` with the packed id, then
  defers to `registry_.valid(ToEntt(entity))`.
- [world.h](../../../runtime/engine/ecs/core/world.h):248-254 `EntityFromEntt`
  does the same packed indexing; :259-261 documents the table as "indexed by
  the numeric value of entt::entity".

Consequences:

1. The first reuse of slot 0 yields packed id `1 << 20`, so the vector grows to
   1,048,577 entries (4 MiB) for one logical slot; every further version bump
   grows it again (bound: 2^32 entries). Memory is O(slots x versions), not O(slots).
2. Stale rejection today is carried almost entirely by EnTT's 12-bit version
   (`registry_.valid`), because each reuse produces a *different* packed id and
   therefore a *fresh* counter. The World counter only matters after 4095
   reuses of one slot, when the packed id repeats.
3. The existing test
   [test_world.cpp](../../../runtime/tests/engine/ecs/test_world.cpp):99-117
   guards its generation assertion with `if (second.id == first.id)`, which is
   never true (version bits differ), so the reuse contract is untested.
4. A second convention exists outside `World`:
   [lua_scene_api.cpp](../../../runtime/engine/scripting/scene/lua_scene_api.cpp):148-163
   and :274-279, and
   [script_system.cpp](../../../runtime/engine/scripting/script_system.cpp):321-326
   and :357-362 build `Entity{ id = entt::to_entity(e) /*slot only*/,
   generation = registry.current(e) + 1 }`. The comment at :149-150 claims this
   "matches what World::CreateEntity stores" - it does not (World stores the
   packed id). It agrees only while a slot's EnTT version is 0; after the first
   recycle of a slot, `Scene.findEntityByName` returns a handle that
   `World::IsValid` rejects. [lua_entity_api.cpp](../../../runtime/engine/scripting/scene/lua_entity_api.cpp):531
   and :573 use the correct `world->EntityFromEntt(e)`.

Constraint that decides the design: about 30 sites outside `World` rely on
`Entity.id` **being** the packed EnTT id via
`static_cast<entt::entity>(static_cast<uint32_t>(entity.id))` - every
`ecs/environment/*_component.cpp` (e.g.
[decal_component.cpp](../../../runtime/engine/ecs/environment/decal_component.cpp):34,85),
[scene_loader.cpp](../../../runtime/engine/ecs/loaders/scene_loader.cpp):224,257,419,486,550,728,
[lua_animation_api.cpp](../../../runtime/engine/scripting/animation/lua_animation_api.cpp):67,
and the C API ([runtime.cpp](../../../runtime/engine/app/runtime.cpp):953,971,1039,1106,1146,
which also *emits* packed ids at :921 and :942). The public id convention must
not change.

### Options

| | A. Re-key the table by slot, keep packed public id (recommended) | B. Make `Entity.id` the slot index | C. Drop the World counter, use EnTT's version only |
|---|---|---|---|
| Change surface | `world.cpp/.h` + 4 hand-rolled Lua/script sites | A + ~30 conversion sites + C API id semantics + Lua packing | `world.cpp/.h` only |
| Memory | O(slots), 4 B/slot | O(slots) | 0 |
| Stale rejection after 4095 reuses | yes (32-bit counter) | yes | **no** (ABA after 4095 reuses of one slot) |
| Serialization (`property_serializer.cpp:82,211` writes `{id, generation}`) | unchanged | id meaning changes | generation meaning changes |
| Risk | lowest | silent wrong-entity bugs where a site is missed (version-0 cast "works" until first recycle) | fails the audit's wraparound requirement |

### Recommended change (Option A)

`runtime/engine/ecs/core/world.h`:

```cpp
// private:
/// Per-slot World generation, indexed by entt::to_entity(packed id) - never by the
/// packed id itself. Size == number of entity slots ever allocated by the registry.
std::vector<uint32_t> generations_;

[[nodiscard]] static uint32_t SlotOf(uint32_t packedId) noexcept {
    return static_cast<uint32_t>(entt::to_entity(static_cast<entt::entity>(packedId)));
}
// public:
/// Number of entity slots ever allocated (bounded-memory contract / diagnostics).
[[nodiscard]] size_t SlotCount() const noexcept { return generations_.size(); }
```

`EntityFromEntt` (world.h:248-254) becomes:

```cpp
const uint32_t packed = static_cast<uint32_t>(entt::to_integral(enttEntity));
const uint32_t slot   = SlotOf(packed);
Entity e; e.id = packed;
e.generation = (slot < generations_.size()) ? generations_[slot] : 0u;
return e;
```

`runtime/engine/ecs/core/world.cpp`:

```cpp
Entity World::CreateEntity() {
    const auto enttEntity = registry_.create();
    const uint32_t packed = static_cast<uint32_t>(entt::to_integral(enttEntity));
    const uint32_t slot   = SlotOf(packed);
    EnsureGenerationSlot(slot);
    uint32_t& gen = generations_[slot];
    if (++gen == 0) { ++gen; }          // wrap: 0 is reserved (Entity::Invalid / never created)
    Entity handle; handle.id = packed; handle.generation = gen;
    ...
}
bool World::IsValid(Entity entity) const {
    if (!entity.IsValid()) return false;
    const uint32_t slot = SlotOf(entity.id);
    if (slot >= generations_.size() || entity.generation != generations_[slot]) return false;
    return registry_.valid(ToEntt(entity));   // alive flag, as HandleMap keeps m_alive beside m_generation
}
```

`DestroyEntity`, `Clear` (keep the table), `ToEntt`, `EntityCount` are unchanged.
`EnsureGenerationSlot` now receives a slot. Update the "Generational Handles"
paragraph in world.h:8-13 and [entity.h](../../../runtime/engine/ecs/core/entity.h):29-31
("id - packed EnTT identifier (index | version << 20); slot = entt::to_entity(id);
generation - World counter per slot, independent of EnTT's 12-bit version").

Contract (document in world.h):

- `IsValid(h)` is true iff `h` was returned by `CreateEntity`/`EntityFromEntt` for
  the current occupant of its slot and that occupant has not been destroyed or
  cleared. Slot reuse bumps the World generation, so the previous occupant's
  handles are rejected even when EnTT's packed id repeats (period 4095).
- Generation is a `uint32_t` that skips 0 on wrap; ABA would require 2^32-1
  reuses of a single slot (same bound the codebase accepts in
  [handle_map.h](../../../runtime/engine/containers/handle_map.h):261-265).
- `SlotCount() <= registry_.storage<entt::entity>().size()` always.

Same contract for the four hand-rolled sites: replace the `slot`/`gen`
construction at lua_scene_api.cpp:156-161, :274-279 and
script_system.cpp:321-326, :357-362 with `world->EntityFromEntt(enttEntity)`
(the pattern already used at lua_entity_api.cpp:531). Implementation check:
grep `to_entity(` under `runtime/engine` - only `world.h/.cpp` should remain.

### Acceptance criteria

1. 10,000 create/destroy cycles on one slot leave `SlotCount() == 1`.
2. After one destroy+create, the new handle has the same slot
   (`entt::to_entity`) and `generation == old + 1`; the old handle fails
   `IsValid`.
3. After 4095 destroy+create cycles, the packed id repeats
   (`current.id == first.id`) and `GetRegistry().valid(entt(first.id))` is
   true, yet `IsValid(first)` is false.
4. `Clear()` then create: pre-clear handles invalid, `SlotCount()` unchanged.
5. `EntityFromEntt(ToEntt(h)) == h` for every live handle; C API ids
   (`uz_create_entity` -> `uz_destroy_entity`/`uz_select_entity`) keep working
   unchanged; all existing `test_world.cpp`, `test_components.cpp` and
   `test_lua_*` tests pass without edits except the one rewritten test.
6. `Scene.findEntityByName` returns a World-valid handle after the slot it
   finds has been recycled.

### Test plan (`runtime/tests/engine/ecs/test_world.cpp`, fixture `WorldTest`)

- Rewrite `RecreateEntity_NewGenerationMakesOldHandleStale` (:99-117): assert
  unconditionally `entt::to_entity(entt(second.id)) == entt::to_entity(entt(first.id))`
  (EnTT's free list is LIFO), `second.generation == first.generation + 1`,
  `!IsValid(first)`, `IsValid(second)`. The old conditional was dead code.
- `SlotChurn_TenThousandCycles_SlotCountStaysOne` - negative control: the
  current code reaches `SlotCount() >= 1'048'577` on the first reuse.
- `VersionWrap_4095Reuses_OldHandleRejectedWhileEnttAcceptsIt` - the
  `GetRegistry().valid(...) == true` assertion is the control proving EnTT alone
  would accept the stale handle (Option C would fail here).
- `Clear_ThenRecreate_OldHandlesStale_SlotCountUnchanged`.
- `EntityFromEntt_RoundTrip_EqualsCreatedHandle` (+ `EntityFromEntt(entt::null)`
  fails `IsValid`).
- `BulkCreate_100K_SlotCountEqualsEntityCount`.
- `GetRegistry_*` (:565-580) stay as they are - they are the regression that the
  public id remained packed.
- `runtime/tests/engine/scripting/test_lua_scene_api.cpp`:
  `FindEntityByName_AfterSlotRecycle_ReturnsWorldValidHandle` - create "x",
  destroy, create "x" again (same slot, version 1), Lua lookup must equal the
  live handle and pass `IsValid`. Fails on the current hand-rolled construction.

### Dependencies and risks

Independent of the other three; do it first. Risk: any site that still
constructs handles by hand (grep above). No persisted-data impact: ids are
session-local and `property_serializer` keeps writing `{id, generation}`.

---

## F-010 (2.2) - Owner-thread world mutation with queued commands

### Root cause

Two threads touch `RuntimeState` without synchronization:

- Owner of frames: [render_thread.cpp](../../../napi/src/render_thread.cpp):156
  calls `uz_render_frame`, which runs Lua `ScriptSystem::Update`
  ([runtime.cpp](../../../runtime/engine/app/runtime.cpp):698-702), gizmo/selection
  `editorInput.Update` (:704-713) and `sceneRenderer.RenderFrame(&state->world, state->vfs.get())`
  (:753-756).
- Node main thread (worker IPC handler) calls, directly:
  `uz_load_scene` ([napi_scene.cpp](../../../napi/src/napi_scene.cpp):14),
  `uz_set_component_property` (:97), `uz_create_entity`/`uz_destroy_entity`
  (:48, :62), `uz_get_scene_tree` (registry view at runtime.cpp:917-925),
  `uz_select_entity` (writes `selectionManager` and `sceneRenderer` fields
  read at :708), `uz_play/uz_stop` (:1164-1186), `uz_set_editor_mode`
  (:1439-1466, initializes `editorInput` with `&world`), `uz_set_content_root`
  (:1468-1486, **replaces `state->vfs`** while the render thread dereferences it),
  `uz_capture_screenshot` (:1286, GPU work), `uz_set_viewport_position`
  ([napi_viewport.cpp](../../../napi/src/napi_viewport.cpp):59-74 -> runtime.cpp:641
  calls `uz_resize_viewport` = `WaitIdle` + swapchain resize directly), and the
  input forwarders (:1194-1260).
- The contract in [render_thread.h](../../../napi/src/render_thread.h):15-16
  ("input forwarding stores state atomically") is false:
  [input.h](../../../runtime/platform/windows/input.h):263-264 declares
  `InputSystem` "NOT thread-safe"; [selection_manager.h](../../../runtime/editor/selection/selection_manager.h):12-13
  likewise; `SceneRenderer::SetSelectedEntity/SetGizmo*` are plain members
  ([scene_renderer.h](../../../runtime/engine/renderer/scene/scene_renderer.h):881,913-925).
- Shutdown ordering bug: the worker's `shutdown` command
  ([runtime-worker.ts](../../../extension/src/runtime-worker.ts):305-329) calls
  `destroyViewport()` (:315) **without** `stopRenderLoop()`;
  `uz_destroy_viewport` (runtime.cpp:513-572) nulls `s_state` (:517) and deletes
  it (:569) while `ThreadMain` may be inside `uz_render_frame` holding the raw
  `state` captured at :653.
- Only resize is already queued (render_thread.cpp:120-125, applied at :143-149).

### Options

| | A. Owner = frame-driving thread; synchronous rendezvous with deadline (recommended) | B. One `std::mutex` around `uz_render_frame` and every C API call | C. Fully asynchronous commands (N-API promises, completion callbacks) |
|---|---|---|---|
| Lifetime model | explicit: ownership moves with `Start()`/`Stop()` (thread create/join are the happens-before edges) | none - what the audit rejects ("isolated locks without a lifetime model") | explicit |
| Main-thread blocking | <= 1 frame pickup + command duration | up to a full frame per call, unfair under contention | none |
| Code | `OwnerCommandQueue` (~120 lines, header-only, testable) + binding edits | ~40 lock sites, deadlock risk with Lua | A + promise plumbing in addon, worker and client |
| Shutdown | `Stop()` drains, `DestroyViewport` stops first | still needs a stop protocol | same as A |

A is the minimal model that has a lifetime contract. C is the right end state if
blocking the worker event loop ever becomes a problem; A keeps the IPC protocol
and `RuntimeClient` untouched, and the worker is a dedicated process whose event
loop only forwards frames.

### Recommended change (Option A)

**1. Engine primitive** `runtime/engine/app/owner_command_queue.h` (header-only,
`<mutex>/<condition_variable>/<functional>/<memory>`), so `test_runtime.exe`
can test it without the addon:

```cpp
namespace uz {
enum class OwnerStatus : uint8_t { Ok, Timeout, Stopped, Busy };

class OwnerCommandQueue {   // single-slot handoff: one submitter, one owner
public:
    /// Submitter side. Runs `fn` inline when no owner thread is attached.
    OwnerStatus Submit(std::function<void()> fn,
                       std::chrono::milliseconds pickupBudget,
                       std::chrono::milliseconds completionBudget);
    /// Owner side: executes at most one pending command. Call once per frame.
    void DrainOne();
    /// Owner side, on loop exit: fails a never-picked-up command with Stopped.
    void FailPending();
    void AttachOwner()  { m_ownerAttached.store(true, std::memory_order_release); }
    void DetachOwner()  { m_ownerAttached.store(false, std::memory_order_release); }
private:
    struct Command { std::function<void()> fn; std::mutex m; std::condition_variable cv;
                     bool pickedUp = false, done = false, abandoned = false; OwnerStatus status = OwnerStatus::Ok; };
    std::mutex m_mutex; std::shared_ptr<Command> m_pending; std::atomic<bool> m_ownerAttached{false};
};
}
```

Semantics (the memory-ordering/lock contract):

- `Submit`: if `!m_ownerAttached` -> `fn()` inline, `Ok`. Else lock `m_mutex`;
  if `m_pending` -> `Busy`; store a `shared_ptr<Command>`; wait on `cmd->cv`
  for `pickedUp` up to `pickupBudget` (2000 ms: longer than any frame including
  a resize `WaitIdle`); on timeout re-lock `m_mutex`, clear `m_pending` if it is
  still this command, return `Timeout` (never executed). Then wait for `done` up
  to `completionBudget`; on timeout set `abandoned`, return `Timeout` - the
  owner finishes the command and discards the signal; the `shared_ptr` keeps the
  record alive for both sides.
- `DrainOne` (owner): take `m_pending` under `m_mutex`, **release the mutex**,
  set `pickedUp` + notify, run `fn`, set `done` + notify. The command runs
  outside `m_mutex` so the submitter's timeout path can lock it.
- `FailPending`: under `m_mutex`, if a command was never picked up, set
  `status = Stopped, done = true`, notify.
- Visibility: all world state written by the owner before `done` is visible to
  the submitter after the wait (mutex release/acquire); ownership transfer at
  `Start`/`Stop` is ordered by `std::thread` construction/`join`.

**2. `RenderThread`** (render_thread.h/.cpp) composes it:

- Member `uz::OwnerCommandQueue m_commands;` plus
  `uz::RingBuffer<InputEvent, 256> m_input;` ([ring_buffer.h](../../../runtime/engine/containers/ring_buffer.h):3-31
  is the existing lock-free SPSC queue whose own doc names "producer: main
  thread, consumer: GPU thread").
- `Start()`: `m_commands.AttachOwner()` before creating the thread.
- `ThreadMain()` frame start, after the resize block (:143-149) and before
  `uz_render_frame` (:156): drain `m_input` (call `uz_on_mouse_move` etc. on the
  owner), then `m_commands.DrainOne()`. After the loop (before :202 and before
  `_exit` at :211): `m_commands.FailPending(); m_commands.DetachOwner();`.
- `Stop()`: unchanged order (flag, join, release TSFNs); after `join` the main
  thread is the owner again and later calls run inline.
- Public: `OwnerStatus RunOnOwner(std::function<void()>, std::chrono::milliseconds completionBudget)`
  and `void PushInput(const InputEvent&)` (drops with a counter when full; inline
  call when no owner is attached).
- Fix the contract comment at render_thread.h:13-19.
- Expose the singleton: `RenderThread& UzNapiRenderThread();` declared in
  [napi_common.h](../../../napi/src/napi_common.h):131-135, defined next to
  `g_renderThread` (napi_viewport.cpp:8).

**3. Bindings** - every call that reads or writes `RuntimeState` goes through
`RunOnOwner`; the result is copied inside the lambda (that copy *is* the
synchronized read snapshot the audit asks for; no separate snapshot type):

| Binding | Route | Budget |
|---|---|---|
| `loadScene`, `saveScene` (napi_scene.cpp:7-31) | owner | 30 s |
| `getSceneTree`, `createEntity`, `destroyEntity`, `getComponentJson`, `setComponentProperty`, `getEntityComponents`, `selectEntity`, `getSelectedEntity`, `play`, `stop`, `isPlaying` (:33-148) | owner | 5 s |
| `setEditorMode`, `setContentRoot` (napi_lifecycle.cpp) | owner | 5 s |
| `captureScreenshot`, `encodeFrame`, `renderFrame` (napi_viewport.cpp:78-135) | owner; `renderFrame` returns an error while the loop runs | 5 s |
| `resizeViewport`, `setViewportPosition` (:40-74) | `resizeViewport` becomes owner-routed (completes when applied); `requestResizeAsync` keeps the fire-and-forget queue | 5 s |
| `requestKeyframe`, `setEncodeBitrate`, `getCodecExtradata` (:137-166) | direct - thread-safe per [h264_encoder.h](../../../runtime/engine/ipc/h264_encoder.h):21-25 and the atomic `forceKeyframe` ([h264_encoder.cpp](../../../runtime/engine/ipc/h264_encoder.cpp):57-59) | - |
| `onMouseMove/Button/Key/KeyEx/Scroll` (napi_input.cpp) | `PushInput` | - |
| `destroyViewport` (:35-38), `shutdown` (napi_lifecycle.cpp) | **`UzNapiRenderThread().Stop()` first**, then the C call | - |

`OwnerStatus != Ok` maps to a JS `Error("owner thread timeout|stopped|busy (cmd)")`,
distinct from the native failure message. `runtime-worker.ts:305-329` also
calls `stopRenderLoop()` before `destroyViewport()` (belt and braces; the
native binding is authoritative).

The engine C API ([runtime.h](../../../runtime/engine/app/runtime.h):7-12) keeps
its "not thread-safe, call from the frame thread" contract; the ownership
mechanism lives in the N-API layer, matching where the resize queue already lives.

### Acceptance criteria

1. With the loop running, every world/scene/selection/editor-mode/content-root/
   screenshot request executes on the render thread between frames; none runs
   on the Node main thread (assertable in the primitive's tests via thread ids).
2. Requests complete or fail with `Ok | Timeout | Stopped | Busy` within
   `pickupBudget + completionBudget`; a timed-out command is never executed
   after the submitter returned (pickup phase) or is finished without touching a
   freed result (completion phase).
3. `destroyViewport`/`shutdown` never run concurrently with `uz_render_frame`:
   the render thread is joined before `s_state` is nulled.
4. Input events reach `InputSystem` only on the owner; no `InputSystem`,
   `SelectionManager` or `SceneRenderer` setter is called from the Node thread
   while the loop runs.
5. Overlapping load/property/resize/shutdown from the editor cannot crash the
   worker (integration check, below).

### Test plan

`runtime/tests/engine/app/test_owner_command_queue.cpp` (no fixed sleeps; use
latches/condvars; only the timeout tests measure time, with small budgets):

- `OwnerCommandQueue.NoOwnerAttached_RunsInlineOnCaller`
- `OwnerCommandQueue.OwnerAttached_RunsOnOwnerThread_ReturnsOk` (owner thread
  loops `DrainOne()` on a condvar; assert `std::this_thread::get_id()` inside `fn`)
- `OwnerCommandQueue.PickupTimeout_CommandNeverExecutes` (owner never drains
  until after the submitter returned; then drains and asserts the command was
  removed) - negative control for "late execution after timeout"
- `OwnerCommandQueue.CompletionTimeout_OwnerFinishesSafely` (fn blocks on a
  latch; submitter gets `Timeout`; release; no crash, status unchanged)
- `OwnerCommandQueue.FailPending_ReturnsStopped`
- `OwnerCommandQueue.SecondSubmitWhilePending_IsBusy` (bound = 1)
- `OwnerCommandQueue.DetachAfterStop_RunsInlineAgain`

Extension (`extension/src/tests/runtime-worker.test.ts`, injected fake addon
records call order): `shutdown stops the render loop before destroying the
viewport and the runtime`. Existing `RuntimeViewportTest` C-API tests
([test_runtime_api.cpp](../../../runtime/tests/engine/app/test_runtime_api.cpp):44-104)
stay as they are; they hang headless and are excluded in the serialized run.

Integration (not CI; run on a GPU machine with the harness style of
`viewport-streaming-test.ts`): `testOverlappingLoadPropertyResizeShutdown` -
interleave `loadScene`, 200 `setComponentProperty`, `resizeViewport`,
`stopRenderLoop`/`startRenderLoop`, then `destroyViewport` while frames stream;
pass = every promise settles with `Ok` or a typed error within budget and the
worker exits 0.

### Dependencies and risks

Do after F-011 (so `uz_load_scene` is one owner-routed call) and before F-020
(both edit `ThreadMain`). Risks: a command that itself takes > 30 s on the
owner (cold Sponza load) returns `Timeout` to the editor while still
completing - acceptable and reported explicitly; `Busy` cannot occur with one
submitter but is kept so a second caller fails loudly instead of racing;
Win32 message pumping (`runtime.cpp:664`) already happens on the render thread
and is unaffected.

---

## F-011 (2.3) - Transactional scene load

### Root cause

- [scene_loader.cpp](../../../runtime/engine/ecs/loaders/scene_loader.cpp):1073-1130
  performs side effects **before** validation: `s_sceneLoadedMaterials.clear()`
  (:1077) destroys the `Material` objects that live entities reference through
  raw pointers (`mesh.SetMaterial(0, mat.get())` at :771-772), `s_uzmatCache.clear()`
  (:1081), `RevertSceneCVarOverrides()` (:1085). Only then: empty check (:1087),
  `json::parse` (:1094), root check (:1102), **CVars applied (:1110-1113) before
  `entities` is validated (:1115-1120)**. Every fatal path returns `0`, which is
  also the count of a valid empty scene.
- [runtime.cpp](../../../runtime/engine/app/runtime.cpp):863-871 ignores the
  count and returns `1`; [napi_scene.cpp](../../../napi/src/napi_scene.cpp):14
  therefore never throws for a malformed scene.
- `LoadScene` is additive (no `Clear`): `uz_load_scene` never clears, so a second
  load stacks entities while :1077 has already freed the first scene's materials.
  The Lua path ([lua_scene_io_api.cpp](../../../runtime/engine/scripting/scene/lua_scene_io_api.cpp):101-103)
  does `world->Clear()` first, so there a failed load leaves an *empty* world,
  not the previous scene.
- Commit-phase exceptions: the helpers at :163-205 are type-checked, but
  `ComponentSerializerRegistry::Deserialize` (:994) reaches
  [property_serializer.cpp](../../../runtime/engine/property/property_serializer.cpp):211
  (`value["generation"].get<uint32_t>()`), which throws `nlohmann::json::type_error`
  on a wrong type; nothing catches it (only `parse_error` at :1095), so an
  exception escapes `uz_load_scene` (extern "C").

### Options

| | A. `LoadSceneEx` - validate, then commit (clear + build) inside the loader (recommended) | B. Two-phase API: `ParseScene(bytes) -> SceneDocument`, `InstantiateScene(World&, doc)` |
|---|---|---|
| Atomicity | validation is pure; commit cannot fail by construction | same |
| Replace/append policy | loader-owned (replace) | caller-owned |
| Churn | one new function + `uz_load_scene` + Lua binding; existing `size_t` overloads kept as wrappers | moves loader statics (materials/cvars/vfs) behind a document type; all callers change |
| Verdict | smallest change that meets the three requirements | cleaner long-term, not needed now |

### Recommended change (Option A)

`scene_loader.h` (after :123):

```cpp
enum class SceneLoadStatus : uint8_t { Ok, EmptyInput, ParseError, RootNotObject, MissingEntities, EntitiesNotArray };

struct SceneLoadResult {
    SceneLoadStatus status = SceneLoadStatus::Ok;
    size_t entityCount = 0;   ///< Entities created. 0 with status Ok is a valid empty scene.
    size_t warnings    = 0;   ///< Non-fatal issues (skipped entries/components/assets, cvar entries).
    std::string message;      ///< First fatal error (source name, byte offset for ParseError); empty on Ok.
    [[nodiscard]] bool ok() const noexcept { return status == SceneLoadStatus::Ok; }
};

/// Replaces the world's scene transactionally: nothing (world, loader-owned materials,
/// CVar overrides) changes unless validation succeeds.
[[nodiscard]] SceneLoadResult LoadSceneEx(World& world, const std::vector<uint8_t>& sceneJson,
                                          const std::string& sourceName, IVFS* vfs = nullptr);
```

The two `size_t LoadScene(...)` overloads (:114-123) become one-line wrappers
returning `LoadSceneEx(...).entityCount` and are documented as "0 is failure or
empty - prefer LoadSceneEx". Naming follows the existing `uz_on_key_ex` pattern.

`LoadSceneEx` ordering:

1. **Validate (no side effects):** empty -> `EmptyInput`; `parse` ->
   `ParseError` with `ex.byte`; root not object -> `RootNotObject`; `entities`
   missing -> `MissingEntities`, not array -> `EntitiesNotArray`. Resolve the
   `cvars` block into a local `std::vector<std::pair<std::string,float>>` using
   the checks now in `ApplySceneCVarOverrides` (:116-146) **without calling
   `reg.Set`** (non-object block, unknown name, unsupported type = warnings,
   counted). Nothing else is fatal.
2. **Commit (cannot fail):** `s_sceneLoadedMaterials.clear()`, `s_uzmatCache.clear()`,
   `RevertSceneCVarOverrides()`, apply the resolved cvars (recording previous
   values as today), `world.Clear()`, then the existing entity loop
   (`ProcessEntityJson`). Keep "CVars before entities" (:1108-1109). Set/clear
   `s_loadVfs` with an RAII guard around the commit.
3. Per-component `try { ... } catch (const nlohmann::json::exception& e)` inside
   `ProcessEntityJson`'s dispatch loop (:974-999): log `Scene::Warning`,
   `++warnings`, continue - the entity keeps its successfully parsed components,
   consistent with the documented per-entity policy (scene_loader.h:34-38).

Policy table (document in scene_loader.h, test both columns):

| Condition | Outcome |
|---|---|
| empty buffer, JSON syntax error, root not an object, `entities` missing/not array | fatal: `status != Ok`, no state change |
| entity entry not an object; component value not an object; unknown component/light type; wrong field types (defaults apply); `cvars` not an object / unknown cvar / unsupported value; missing or unparsable `.uzmaterial`, `.uzmat`, prefab (today's fallbacks) | warning, counted, load succeeds |

Callers:

- `uz_load_scene` (runtime.cpp:827-872): keep the file-read branch (empty read
  -> log + `return 0`), call `LoadSceneEx(state->world, bytes, path, state->vfs.get())`,
  log `result.message` at Error on failure and `return 0`; on success
  `selectionManager.Deselect()` + `sceneRenderer.SetSelectedEntity({})` (handles
  are stale after replace), then the existing `LuaSetWorld`, `return 1`.
  `napi_scene.cpp:14` already throws on 0; the error text reaches the console
  panel through the log callback.
- `lua_scene_io_api.cpp:101-103`: drop the explicit `world->Clear()`, call
  `LoadSceneEx`, return `nil, message` on failure so a failed Lua load also
  preserves the previous scene.

### Acceptance criteria

1. Malformed JSON, wrong root type, missing/non-array `entities`: `status`
   names the cause, `world.EntityCount()`, every live handle, the mesh material
   pointers and every CVar value are unchanged, and `uz_load_scene` returns 0.
2. `{"entities": []}` -> `Ok`, `entityCount == 0`, world empty (distinguishable
   from failure).
3. Wrong field types in components/cvars -> `Ok`, defaults applied,
   `warnings > 0`, no exception.
4. A successful load replaces the previous scene (old handles fail `IsValid`),
   and CVar overrides from the previous scene are reverted exactly once.
5. Missing optional assets -> `Ok` with warnings (documented policy).

### Test plan (`runtime/tests/engine/ecs/test_scene_loader.cpp`, fixture `SceneLoaderTest`)

Add a minimal in-memory `IVFS` fake in the test file (Read/Exists over a map;
match the pure virtuals in `vfs.h`) and register one test CVar through
`CVarRegistry` in a fixture derived from `SceneLoaderTest`.

- `LoadSceneEx_MalformedJson_FailsAndPreservesWorldCVarsAndMaterials`: load a
  valid scene with `cvars` + a `material.asset` through the fake VFS, then load
  `{not json` -> `ParseError`; entity count, handle validity, `GetMaterial(0)`
  pointer and the overridden CVar value unchanged. Negative control: current
  code reverts the CVar at :1085 before parsing, so this assertion fails today.
- `LoadSceneEx_MissingEntities_FailsWithoutApplyingCVars` (cvars block present
  and valid; CVar must keep its pre-load value - current code applies it at
  :1112 before the `entities` check).
- `LoadSceneEx_EmptyEntities_IsOkWithZeroCount` / `..._EmptyBuffer_IsEmptyInput`
  / `..._BareString_IsRootNotObject` / `..._EntitiesObject_IsEntitiesNotArray`.
- `LoadSceneEx_WrongFieldTypes_OkWithDefaultsAndWarnings` (`"position": "abc"`,
  `"fov": true`, `"cvars": 5`, a registry component with a non-numeric field).
- `LoadSceneEx_ReplacesPreviousScene_OldHandlesStale`.
- `LoadSceneEx_MissingMaterialAsset_WarnsAndSucceeds`.
- `LoadScene_LegacyWrapper_ReturnsZeroOnFailureAndCountOnSuccess`.
- `runtime/tests/engine/scripting/test_lua_scene_io_api.cpp`:
  `Scene_LoadScene_Malformed_PreservesPreviousScene` (today the world is cleared
  first).
- C API (`RuntimeViewportTest`, GPU-gated/headless-excluded):
  `LoadMalformedSceneReturnsZeroAndKeepsPreviousTree`.

### Dependencies and risks

Independent of threading; do before F-010. Risks: systems caching `Entity`
handles across a replace (selection, VFX attach at
[scene_vfx_sync.cpp](../../../runtime/engine/renderer/scene/scene_vfx_sync.cpp):64,
skeletal subsystem) must already tolerate `Clear()`; the Lua path exercised
that before. Replace semantics changes `uz_load_scene` from additive to
replace, which is the editor's intent; no caller depends on stacking.

---

## F-020 (2.5) - Bounded native/IPC queues, deadlines, shutdown errors, H.264 recovery

### Root cause (per stage)

1. Render thread -> Node: both TSFNs are created with `maxQueueSize = 0`
   (unlimited) ([render_thread.cpp](../../../napi/src/render_thread.cpp):70-93);
   `NonBlockingCall` can never report `napi_queue_full`, so every encoded frame
   (tens of KB at 20 Mbps) is retained while the worker's event loop is busy.
2. Worker -> extension host: `host.send(frameMsg)` ([runtime-worker.ts](../../../extension/src/runtime-worker.ts):380-391)
   ignores the `false` backlog return value and never counts in-flight messages.
3. Extension host -> webview: `postMessage` ([viewport-panel.ts](../../../extension/src/panels/viewport-panel.ts):226-229)
   is fire-and-forget; no in-flight bound.
4. Webview decoder: no `decodeQueueSize` bound; on `error` the decoder is set
   to `null` and never recreated (:559-562); a `decode()` throw is only logged
   (:691-693). After **any** drop of an encoded access unit the H.264 reference
   chain is broken, so dropping without forcing an IDR is unsafe - the audit's
   "arbitrary frame dropping is not a safe isolated fix".
5. Requests: `send()` registers `pending` with no deadline
   ([runtime-client.ts](../../../extension/src/runtime-client.ts):520-542, :534)
   and no bound on `pending.size()`; errors are untyped strings.
6. **Pre-condition defect (source-confirmed, not runtime-verified here):** the
   webview frame handler treats `msg.data` as a byte array (:675-683, `nalData[0] === 0`,
   `& 0x1F`) and passes it to `EncodedVideoChunk` (:685-689), but the panel posts
   the base64 **string** it received (:226-228, runtime-client.ts:598-605); there
   is no `atob`/`Uint8Array` conversion anywhere in the file. `EncodedVideoChunk`
   requires a `BufferSource`, so every frame throws into the catch at :691 and
   nothing is ever decoded; key detection on string characters is also always
   "delta". Recovery semantics cannot be built on this path without fixing it.

Already satisfied, keep as is: pending calls are rejected on worker exit and
on dispose (runtime-client.ts:450-455, :498-501); the encoder's
`RequestKeyframe` is thread-safe and forces an IDR
([h264_encoder.h](../../../runtime/engine/ipc/h264_encoder.h):137-141, tested by
`H264Encoder.RequestKeyframe_ForcesIDROutput` at
[test_h264_encoder.cpp](../../../runtime/tests/engine/ipc/test_h264_encoder.cpp):664);
`SubmitFrameAsync` drops *input* frames when the encoder is busy (h264_encoder.h:115-116,
h264_encoder.cpp:649-651), which lowers frame rate without breaking the stream.

### Options

| | A. Depth-bounded queues at every stage + dependency-safe drop (gate until next IDR) + per-request deadlines (recommended) | B. Backpressure the producer (block the render thread until consumers drain) | C. Drop freely, raise `keyframeInterval` |
|---|---|---|---|
| Memory | bounded by `depth x frame` per stage | bounded | unbounded queues remain |
| Latency / frame rate | drops under pressure, resumes at next IDR (<= 15 frames at the current interval, immediate with a forced key) | stalls rendering and editor input | corrupt frames until next IDR |
| Correctness | decoder only sees a key after a gap | correct | decoder errors |
| Code | small pure gate + counters | render loop coupling to JS | none |

### Recommended change (Option A)

**Native (`runtime/engine/ipc/keyframe_gate.h`, header-only, tested in the engine suite):**

```cpp
namespace uz {
/// True iff the Annex-B access unit contains an IDR slice (NAL type 5). Walks
/// 3/4-byte start codes; stops at the first VCL NAL (types 1-5). AUD/SPS/PPS alone
/// are not a key frame.
[[nodiscard]] bool IsAnnexBKeyframe(const uint8_t* data, size_t size) noexcept;

/// Dependency-safe delivery policy: after any dropped access unit, hold every
/// delta frame until the next IDR, so a decoder never sees a broken reference chain.
class KeyframeGate {
public:
    [[nodiscard]] bool ShouldDeliver(bool isKeyframe) noexcept;   // false while awaiting and !isKeyframe
    void OnDropped() noexcept { m_awaitingKey = true; }           // caller also requests a keyframe
    void Arm() noexcept      { m_awaitingKey = true; }            // encoder re-init / stream start
    [[nodiscard]] bool AwaitingKeyframe() const noexcept { return m_awaitingKey; }
private: bool m_awaitingKey = true;   // a fresh stream must start with a key
};
}
```

`render_thread.cpp`: `kFrameQueueDepth = 2`, `kStatsQueueDepth = 4` passed as
`maxQueueSize` (:77, :88). Encode path (:174-189): compute
`isKey = IsAnnexBKeyframe(encData, encSize)`; if `!m_gate.ShouldDeliver(isKey)`
skip; else `NonBlockingCall`; on `napi_queue_full` delete the copy,
`m_gate.OnDropped()`, `uz_request_keyframe()`. Stats drops stay silent. Call
`m_gate.Arm()` after a resize is applied (:146 - `uz_resize_viewport`
re-initializes the encoder at runtime.cpp:608-621, producing new SPS/PPS).

**Worker (`runtime-worker.ts`):** add `key: boolean` to the `frame` response
(computed once by a shared `extension/src/nal-utils.ts` `isAnnexBKeyframe(Uint8Array)`,
same rules as native) so later stages never re-parse. Bound in-flight sends:
`host.send(msg, undefined, undefined, () => inFlight--)`; if
`inFlight >= 2` or `send` returned `false` on the previous frame, treat as a
drop: `FrameGate.onDropped()` + `addon.requestKeyframe()`; while awaiting, skip
delta frames. `FrameGate` (`extension/src/frame-gate.ts`) mirrors `KeyframeGate`.
Fire-and-forget input: when `send` returns `false`, drop further input until a
send callback fires (bounded channel growth during a wedge).

**Client (`runtime-client.ts`):**

- `send()` (:520-542): each `pending` entry gets a `timer`; on expiry delete the
  entry and reject with `RuntimeClientError{code:'timeout'}`; clear the timer in
  `handleWorkerMessage` (:565-575) and in the exit/kill loops (:452-455, :498-501),
  which reject with `code:'worker-exit'` / `code:'disposed'`. Budgets:
  `init 30 s`, `createViewport 60 s`, `loadScene 60 s`, `saveScene 30 s`,
  default `10 s` (each >= the F-010 owner budget plus slack).
- `kMaxPendingRequests = 64`: beyond it `send()` rejects immediately with
  `code:'backpressure'`.
- `RuntimeClientError extends Error { code: 'timeout'|'worker-exit'|'disposed'|'backpressure'|'native' }`;
  messages unchanged so existing tests keep passing.
- `dispose()` (:140-154) keeps kill-immediately (deactivate is synchronous) but
  the rejection is typed `disposed`.
- Forward `key` to `frameCallback(data, key)`.

**Panel (`viewport-panel.ts`):** in-flight bound of 2 on `postMessage` thenables
(:226-229); on drop `FrameGate.onDropped()` + `client.requestKeyframe()`; handle a
new `requestKeyframe` webview message in `handleWebViewMessage` (:262-287); after
`await client.resizeViewport()` (owner-routed by F-010) re-query
`getCodecExtradata` and post `init` again (today `init` is sent once, :234-247).

**Webview script (:538-693):**

- Convert `msg.data` with `atob` into a `Uint8Array` before use; use `msg.key`
  for the chunk type (fixes the pre-condition defect).
- `awaitingKey = true` after `configure()`; drop deltas until the first key.
- If `decoder.decodeQueueSize > 4`: drop deltas, `awaitingKey = true`,
  `vscode.postMessage({type:'requestKeyframe'})`.
- `recoverDecoder()` on the `error` callback and on a `decode()` throw: close,
  `initDecoder(lastExtradata)`, `awaitingKey = true`, request a keyframe;
  after 3 consecutive failures stop retrying and show the overlay error text
  (no recovery loop).

### Acceptance criteria

1. No stage holds more than its depth (native 2 frames/4 stats, worker 2 in
   flight, panel 2 in flight, decoder queue 4): memory stays flat with a stalled
   consumer at any stage (manual harness `testMemoryStability` extended with an
   induced stall).
2. After any drop at any stage, the next access unit reaching the decoder is an
   IDR, and it arrives within `keyframeInterval` (15) frames or sooner through
   the forced request.
3. Every `RuntimeClient` request settles within its budget with a typed `code`;
   late replies after a timeout are ignored; the 65th concurrent request is
   rejected with `backpressure`.
4. A `VideoDecoder` error or decode exception results in a recreated decoder
   and a decoded key frame without user action; three consecutive failures end
   in a visible error state, not a silent black canvas.
5. The webview decodes the first frame of a fresh stream (the pre-condition
   defect is fixed).

### Test plan

gtest `runtime/tests/engine/ipc/test_keyframe_gate.cpp` (pure, headless):

- `KeyframeGate.FreshGate_HoldsDeltasUntilFirstKey`
- `KeyframeGate.AfterKey_DeliversDeltas` (negative control: no drop, nothing held)
- `KeyframeGate.AfterDrop_HoldsDeltasUntilNextKey_ThenReopensOnce`
- `KeyframeGate.DropWhileAwaiting_StaysArmed`
- `NalUtils.IdrAfterSpsPps_IsKey`; `NalUtils.AudThenIdr_IsKey` (the current
  webview first-NAL heuristic returns false here - negative control);
  `NalUtils.NonIdrSlice_IsNotKey`; `NalUtils.SpsPpsOnly_IsNotKey`;
  `NalUtils.TruncatedOrEmpty_IsNotKey`; `NalUtils.ThreeByteStartCode_IsHandled`.
- Existing `H264Encoder.RequestKeyframe_ForcesIDROutput` (encoder-gated via
  `GTEST_SKIP`) is the encoder-side guarantee the recovery relies on.

vitest:

- `runtime-client.test.ts`: `rejects with code "timeout" when the worker never
  replies and ignores the late reply`; `clears the deadline when the reply
  arrives first`; `rejects with code "backpressure" beyond the pending bound`;
  extend the exit/dispose tests (:269, :304) to assert `code`.
- `runtime-worker.test.ts` (injected host/addon): `tags frames with key`;
  `drops delta frames while the channel is backlogged, requests a keyframe, and
  resumes at the next key` (host.send returns false / withholds callbacks; fake
  addon records `requestKeyframe`); `drops fire-and-forget input while the channel
  is backlogged`.
- `frame-gate.test.ts` and `nal-utils.test.ts`: the same cases as the gtests.

Integration (manual harness `viewport-streaming-test.ts`):
`testWebviewDecodesFirstFrame`, `testDecodeRecoveryAfterInducedStall`
(artificial decoder delay; frames resume with a key within 15 frames),
`testRequestTimeoutSurfacesTypedError` (worker stub that never replies).

### Dependencies and risks

Land after F-010 (same `ThreadMain` region; shutdown ordering comes from F-010).
The client-side deadlines, typed errors and the webview byte fix are independent
and can be done first. Risks: the canvas and `configure()` hardcode 1280x720
(:550-551, :197) - a real resize needs the extradata re-init described above,
otherwise recovery loops into the 3-failure stop; `host.send`'s `false` is a
hint, so the in-flight counter is the actual bound; on Windows `kill('SIGTERM')`
terminates the worker immediately, so graceful native shutdown only runs for
panel-level `destroyViewport`, which is why F-010 puts the `Stop()` there.

---

## Order of work and cross-finding notes

1. **F-009** - isolated ECS change plus four Lua/script call sites; verify with
   `test_world.cpp` and the new Lua recycle test.
2. **F-011** - loader transaction, pure engine code; `uz_load_scene` shrinks to
   one call. Recommended before F-010 (tasks.md says "2.3 after 2.2"; the
   loader has no threading dependency and its tests run headless).
3. **F-010** - `OwnerCommandQueue` + `RenderThread` + bindings + the
   `Stop()`-before-destroy fix; fixes the false thread-safety comment.
4. **F-020** - native gate in the restructured `ThreadMain`, worker/panel/webview
   gates, client deadlines, webview byte conversion.

Verification notes for later: the serialized native run excludes
`RuntimeViewportTest.*` (headless hang), so acceptance for F-010/F-020 rests on
the engine-level primitives' gtests, the vitest suites, and the manual GPU
harness; record which of the three produced each claim. The webview decode
defect (F-020 item 6) is source-confirmed only; confirm it with the
`testWebviewDecodesFirstFrame` harness before building recovery on it.

---

## Status (2026-10-15)

Design-complete; **not implemented** (open items 2.1/2.2/2.3/2.5 remain `[ ]`).
Implementation and its gtest/vitest/GPU-harness verification require building and
running the engine/editor, which is deferred. This doc is the turnkey spec:
each finding has its root cause (file:line), the recommended change, acceptance
criteria, and a test plan. Two adjacent defects were surfaced and folded in: the
four hand-rolled Lua/script handle sites (F-009) and the webview base64->bytes
decode gap (F-020 item 6).
