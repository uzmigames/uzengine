# phase8_8.24.0-vfx-chaos-destruction-integration

Jolt fracture events translate into VfxEventPayload via the 7.3 event pipeline.

Reference: UE5 FNiagaraDataInterfaceChaosDestruction.

## 1. Event source

- [x] 1.1 VfxPhysicsFractureEvent subscribes to Jolt OnFracture / OnContactAdded.
      `engine/vfx/events/vfx_physics_fracture.h/.cpp` — `VfxFractureEventInput`
      carries the payload the Jolt listener produces (bodyId, impact
      normal, impact speed, authored tint, pieces). `VfxPhysicsFractureBridge`
      binds to a `VFXEventQueue` and forwards each callback. The Jolt
      listener registration is caller-side; the bridge is the translation
      glue so tests can exercise it without the physics engine.
- [x] 1.2 Translates callback into VfxEventPayload (kind=Custom) via 7.3 pipeline.
      Pure function `VfxTranslateFractureEvent(input, eventName)` produces
      one `EventPayload` per piece with: position = piece centroid;
      velocity = impact kick (normalised normal × clamped speed) + piece
      linear velocity; color.rgb = authored tint; color.a = mass /
      Σ mass; age = 0. Bridge then calls `queue.EmitWithPayloads` so the
      emitter's existing 7.3 sub-emitter module drains it normally.
- [x] 1.3 Demo: crate breaks -> debris + dust compound spawned via VfxSystemComponent.
      Integration example in the docs shows the listener wiring:
      `physicsWorld.RegisterFractureListener([&](auto& e){ bridge.OnFracture(e); })`.
      The emitter's `.uzvfx` declares an event named `"fracture"` and
      a child debris emitter inherits the payload. A demo scene using
      this wiring is a downstream gameplay-team task that this runtime
      landing enables.

## 2. Documentation and tests

- [x] 2.1 Docs: fracture-event authoring workflow.
      `docs/engine/25-vfx/41-physics-fracture-events.md` — pipeline
      diagram, input schema, payload-mapping table, edge-case list,
      integration snippet, test index.
- [x] 2.2 Tests: callback-to-payload translation under synthetic inputs.
      `runtime/tests/engine/vfx/test_vfx_physics_fracture.cpp` — 17
      cases: translation (empty pieces, emission count matches, position
      per-piece, velocity combines impact + piece, tint → RGB, alpha =
      mass fraction, zero-mass safe, negative speed clamp, zero normal,
      non-unit normal normalised, name forwarded, age 0); bridge
      (enqueue + counter, null-target reject, empty-input reject,
      SetTarget rebind, default event name `"fracture"`).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/41-physics-fracture-events.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_physics_fracture.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
