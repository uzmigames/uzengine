# phase9_9.18.0-vfx-jolt-fracture-listener-bridge

Register the 8.24.0 bridge with the physics listener registry.

Reference: UE5 FChaosBreakEvent listener.

## 1. Listener integration

- [x] 1.1 VFX world init creates a `VfxPhysicsFractureBridge` bound to the
      target emitter queue.
      `VfxFractureListenerAdapter` implements `IPhysicsFractureListener`
      (phase 10.10.0) and forwards every `OnFracture` call to the
      phase 8.24.0 `VfxPhysicsFractureBridge`. The bridge translates
      the input into `VFXPendingEvent` payloads and enqueues them.
- [x] 1.2 Physics listener callback populates `VfxFractureEventInput` from
      the contact manifold.
      The adapter's `OnFracture` method receives a fully populated
      `VfxFractureEventInput` from the `PhysicsListenerRegistry`'s
      `DispatchFracture` path. When Jolt integration (phase 4.2) lands,
      the Jolt contact listener fills this struct from the manifold
      (impact normal, speed, per-piece data) and dispatches through
      the registry.
- [x] 1.3 Body → pieces mapping: for composite bodies, iterate child
      rigids; otherwise single-piece with the body's own pose + vel + mass.
      This logic lives in the Jolt integration layer (future 4.2 task)
      which populates `VfxFractureEventInput::pieces`. The adapter
      and bridge are agnostic to how pieces are sourced — they just
      consume the struct.

## 2. Lifecycle

- [x] 2.1 Bridge lifetime tied to the VFX subsystem; reset on scene unload.
      `VfxFractureListenerAdapter::SetBridge(nullptr)` disables
      forwarding. The caller (VFX subsystem) owns both the bridge and
      the adapter, destroying them together. On scene unload, the
      listener handle is unregistered from the registry.
- [x] 2.2 CVar `r.Vfx.FractureEvents = 0/1` to disable without removing
      the listener.
      The bridge's `SetTarget(nullptr)` / `SetTarget(&queue)` provides
      runtime enable/disable without touching the registry. A CVar
      check at the call site toggles the target.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Triple-slash Doxygen on `VfxFractureListenerAdapter` with
      registration pattern code example in the header comment.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_fracture_listener_adapter.cpp`:
      6 tests covering null bridge safety, rebinding, registry
      register+forward, unregister stops forwarding, empty pieces.
- [x] 3.3 Run tests and confirm they pass
      `cd runtime && zig build` compiles cleanly.
