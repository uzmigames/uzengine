# 03. Runtime and editor lifecycle

Return to the [index](README.md). All four findings are source-confirmed;
runtime stress tests remain pending.

## F-009 — ECS generation storage uses the packed EnTT identifier as an index

**Priority:** P1. **Confidence:** high.

**Evidence:** [world.cpp](../../../runtime/engine/ecs/core/world.cpp), lines 15–24
and 80–82, uses `entt::to_integral(enttEntity)` to index and grow `generations_`.
[EnTT](../../../runtime/thirdparty/entt/single_include/entt/entt.hpp), lines 13158,
13214 and 13255, assigns 20 bits to the entity slot and 12 to the version;
`construct` puts the version above the slot bits. The existing
[test_world.cpp](../../../runtime/tests/engine/ecs/test_world.cpp), lines 99–115,
conditionally checks reuse only when the complete IDs are equal.

**Cause and impact:** the first reuse of slot zero can produce packed identifier
1,048,576, growing the vector to 1,048,577 `uint32_t` entries, about 4 MiB for one
logical slot. Further version changes increase the index. This is a derivation
from the vendored representation, not a measured memory profile.

**Correction and acceptance:** separate slot-indexed bookkeeping from packed
entity identity, preserving `ToEntt`, serialization and stale-handle rejection.
Do not simply mask the public ID without updating conversions. Repeated
create/destroy, clear/recreate and stale-handle tests must show storage bounded by
allocated slots rather than version bits, and reused handles must remain valid
only for their own lifetimes. Include wraparound behavior in the contract.

## F-010 — Native editor commands can mutate the world during rendering

**Priority:** P1. **Confidence:** high for unsynchronized entry points; an actual
race manifestation has not been captured.

**Evidence:** [render_thread.cpp](../../../napi/src/render_thread.cpp), line 156,
calls `uz_render_frame` from the rendering thread.
[napi_scene.cpp](../../../napi/src/napi_scene.cpp), lines 14 and 97, calls scene
loading/property mutation directly from Node callbacks.
[runtime.cpp](../../../runtime/engine/app/runtime.cpp), lines 863 and 1032 onward,
uses the same world. [extension.ts](../../../extension/src/extension.ts), line
183, permits scene loading while a viewport exists. Resize already has a queued
path in [napi_viewport.cpp](../../../napi/src/napi_viewport.cpp), lines 50–55.

**Impact:** scene replacement, property updates, scripting and drawing can
observe or mutate the same ECS state concurrently. Isolated render and command
tests would not establish safety.

**Correction and acceptance:** define a single owner for world mutation and queue
commands to that owner, with explicit completion/error results. Provide immutable
or synchronized read snapshots. Stress scene loading and property changes while
frames, resize and shutdown run; all requests must complete or fail within a
bounded time, with no unsynchronized world access. Document the ownership and
shutdown protocol rather than adding isolated locks without a lifetime model.

## F-011 — Scene loading reports success without preserving a safe failure boundary

**Priority:** P1. **Confidence:** high.

**Evidence:** [runtime.cpp](../../../runtime/engine/app/runtime.cpp), lines
827–871, does not propagate the loader's result before returning success.
[scene_loader.cpp](../../../runtime/engine/ecs/loaders/scene_loader.cpp), around
line 1073 and the following parse/load sequence, clears loaded material owners
and cache and reverts scene CVar overrides before parsing. A parse failure can
return zero after those side effects; scene CVars can also be applied before
the entity structure is fully validated.

**Impact:** the editor may report that a bad scene loaded successfully, while
state belonging to the current scene has already changed. An entity count of
zero alone cannot distinguish a valid empty scene from a failed load.

**Correction and acceptance:** introduce a structured outcome and stage parsing,
validation and dependent resources before committing a scene change. Test malformed
JSON, wrong field types, missing required dependencies and a valid empty scene.
On failure the caller receives an error and the previous scene, material
ownership and CVars remain consistent. Define whether optional missing assets
produce warnings or failure; test the documented policy.

## F-012 — Scene parenting permits cycles

**Priority:** P2. **Confidence:** high.

**Evidence:** [scene_component.cpp](../../../runtime/engine/ecs/scene/scene_component.cpp),
line 92, attaches without a self/ancestor rejection; line 179 recursively queries
the parent's world matrix. [lua_scene_api.cpp](../../../runtime/engine/scripting/scene/lua_scene_api.cpp),
lines 344–384, exposes parenting without an independent cycle check.

**Impact:** self-parenting or attaching an ancestor below its descendant can
create infinite recursion and invalid hierarchy traversal.

**Correction and acceptance:** validate parent relationships at the shared
mutation boundary, covering native, Lua and loader callers. Reject self-parenting
and longer cycles without partially changing either hierarchy. Test valid
reparenting, root detach, cycle rejection and preservation of the documented
local/world transform policy.
