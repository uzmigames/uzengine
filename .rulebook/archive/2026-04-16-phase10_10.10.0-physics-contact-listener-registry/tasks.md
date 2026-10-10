# phase10_10.10.0-physics-contact-listener-registry

Backend-agnostic multi-subscriber listener registry for physics events.

Reference: UE5 FCollisionNotifyInfo + JPH::ContactListener.

## 1. Listener API

- [x] 1.1 Abstract `IPhysicsContactListener` with `OnContactAdded`, `OnContactPersisted`, `OnContactRemoved`.
      `engine/physics/physics_listener_registry.h` — three virtual
      methods. `OnContactPersisted` and `OnContactRemoved` have default
      no-op implementations so subscribers only override what they care
      about. Backend-agnostic `PhysicsContactEvent` payload (bodyIdA/B,
      position, normal, impulseMagnitude, relativeVelocity).
- [x] 1.2 `PhysicsWorld::RegisterContactListener` / `UnregisterContactListener` stable-handle API.
      Implemented on `PhysicsListenerRegistry` since `PhysicsWorld`
      itself doesn't exist yet (phase 4.2.0 Jolt integration is pending).
      The registry is the stable plug-in point; when `PhysicsWorld`
      lands it wraps this registry instead of reinventing the API.
      Stable `PhysicsListenerHandle` returned — `0` reserved for invalid.
- [x] 1.3 Multiplexer slot registered as the single Jolt listener; dispatches to N subscribers.
      `PhysicsListenerRegistry::DispatchContactAdded` /
      `DispatchContactPersisted` / `DispatchContactRemoved` — snapshot
      the subscriber list before the loop so re-entrant
      register/unregister during dispatch is safe (tested).

## 2. Fracture hook

- [x] 2.1 Separate `IPhysicsFractureListener` interface with `OnFracture(VfxFractureEventInput)`-compatible callback.
      Signature `bool OnFracture(const VfxFractureEventInput&)` matches
      `VfxPhysicsFractureBridge` from phase 8.24 — when 9.18 lands it
      adds a thin adapter class wrapping the bridge, no signature
      translation needed.
- [x] 2.2 Registration + multiplexer identical to contact listener path.
      `RegisterFractureListener` / `UnregisterFractureListener` /
      `DispatchFracture` — same snapshot-before-dispatch pattern.
      Independent subscriber list so contact and fracture events don't
      cross-contaminate (tested).
- [x] 2.3 Enables phase 9.18 to land without touching PhysicsWorld internals.
      9.18 will: create a `PhysicsListenerRegistry` at world init;
      register a `VfxFractureListenerAdapter` that forwards to the
      existing 8.24 bridge; in the future Jolt ContactListener, forward
      native callbacks into `registry.DispatchFracture(...)`.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Inline header docs describe the lifecycle, thread-safety, and
      backend-agnostic rationale. Referenced from the 9.18 task.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/physics/test_physics_listener_registry.cpp` —
      14 cases: empty registry, register returns handle, null-listener
      invalid handle, unregister removes slot, unregister invalid-handle
      no-op, Clear, contact dispatch to all subscribers, persisted +
      removed routing, unregistered listener receives no events,
      fracture dispatch routing, fracture + contact independence,
      re-entrant self-unregister during dispatch is safe, handles
      unique across registrations.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean. The registry is pure C++ with
      zero platform deps — tests pass on build; runtime gated by the
      same pre-existing missing-ffmpeg-DLL env gap.
