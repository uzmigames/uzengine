# Entity/Component — Event System

> **Status**: Planned (Milestone 2.0)
> **Source**: Spec in `docs/ENGINE.md` §7
> **Depends on**: World (§10-01)
> **References**: UE5 multicast delegates, Godot signals

## Purpose

Type-safe event system with two modes: **immediate** broadcast and **deferred** (queued to safe flush points). Replaces hardcoded cross-system calls with a decoupled observer pattern.

## MulticastDelegate

```cpp
/// Type-safe multicast delegate. Multiple listeners, broadcast to all.
template<typename... Args>
class MulticastDelegate {
public:
    using Fn = std::function<void(Args...)>;

    DelegateHandle Add(Fn callback);
    DelegateHandle AddObject(void* obj, Fn callback);  // auto-removes if obj destroyed
    void Remove(DelegateHandle handle);
    void Broadcast(Args... args);                       // immediate call
    void BroadcastDeferred(Args... args);               // queued to frame flush
    void Clear();
};
```

`AddObject()` associates a listener with an object pointer. If the object is destroyed (detected via weak reference), the listener is automatically unregistered — preventing dangling callback crashes.

## Engine-Level Events (Global)

```cpp
namespace EngineEvents {
    // Lifecycle
    extern MulticastDelegate<>              OnPreInit;
    extern MulticastDelegate<>              OnPostInit;
    extern MulticastDelegate<>              OnPreShutdown;
    extern MulticastDelegate<>              OnPostShutdown;

    // Frame
    extern MulticastDelegate<float>         OnPreUpdate;       // delta
    extern MulticastDelegate<float>         OnPostUpdate;      // delta
    extern MulticastDelegate<>              OnPreRender;
    extern MulticastDelegate<>              OnPostRender;

    // Physics
    extern MulticastDelegate<float>         OnPrePhysics;      // fixed_delta
    extern MulticastDelegate<float>         OnPostPhysics;     // fixed_delta
}
```

## World-Level Events

```cpp
class World {
public:
    MulticastDelegate<Entity>               OnEntityCreated;
    MulticastDelegate<Entity>               OnEntityDestroyed;
    MulticastDelegate<>                     OnBeginPlay;
    MulticastDelegate<>                     OnEndPlay;
};
```

## Deferred Event Queue

Inspired by Godot's `MessageQueue::flush()`. Deferred events are flushed at known safe points — between tick groups, after update, before render. This avoids mutation during iteration.

```cpp
class EventQueue {
public:
    void Push(std::function<void()> event);
    void Flush();  // called by the main loop at safe points
};
```

### Why Deferred Events

If a behavior's `OnTick` fires an event that destroys an entity, and another listener accesses that entity — crash. Deferring events to safe points ensures all iteration is complete before mutations happen.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Mechanism | Multicast delegates + dynamic delegates | Signals (observer pattern) | **MulticastDelegate (typed)** |
| Deferred | `ENQUEUE_RENDER_COMMAND` (render only) | `MessageQueue` (call_deferred) | **EventQueue (general-purpose)** |
| Auto-cleanup | Weak references (`TWeakObjectPtr`) | Signal connections auto-disconnect on free | **`AddObject()` auto-removes** |

## Rules

1. **ALWAYS use `BroadcastDeferred()`** for events that may trigger entity creation/destruction
2. **ALWAYS remove listeners** when the listening object is destroyed (or use `AddObject()`)
3. **NEVER fire events during event flush** — this would cause infinite recursion. Events pushed during flush are queued for the next flush.
