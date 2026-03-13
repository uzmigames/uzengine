# Core — Memory Management

> **Status**: Done
> **Source**: `runtime/engine/memory/ownership.h` (ownership), `runtime/engine/memory/allocators.h` (allocators)
> **Depends on**: Nothing (standalone)

## Purpose

No garbage collector. Deterministic memory management via Rust-inspired ownership semantics in C++ and purpose-built allocators for different allocation patterns. Every allocation is tracked by name in debug builds for budget monitoring.

## Part 1: Ownership System

### Philosophy

The engine uses three smart pointer types that encode ownership semantics at the type level, inspired by Rust's ownership model but adapted for C++:

| Type | Rust Equivalent | Semantics | Copyable | Shipping Overhead |
|------|----------------|-----------|----------|------------------|
| `Own<T>` | `Box<T>` | Exclusive ownership, move-only | No | Zero (raw pointer) |
| `Ref<T>` | `&T` | Immutable borrow | Yes (increments count) | Zero (raw pointer) |
| `RefMut<T>` | `&mut T` | Exclusive mutable borrow | No (move-only) | Zero (raw pointer) |

### Engine Comparison

| Engine | Memory Model | Safety Mechanism |
|--------|-------------|-----------------|
| **UE5** | `TSharedPtr`/`TUniquePtr` + UObject GC | GC for UObjects, manual for everything else |
| **Godot** | Reference counting (`Ref<T>`) + `ObjectDB` | RefCounted base class, debug leak detector |
| **Unity** | C# GC (managed heap) + NativeContainers (DOTS) | GC pauses (managed), safety system (DOTS) |
| **UzEngine** | `Own<T>` + `Ref<T>` / `RefMut<T>` + debug borrow checker | Compile-time types + runtime validation (debug only) |

Key difference: UzEngine has zero runtime overhead in shipping. The borrow checker is debug-only (compiled out via `#ifndef UZ_SHIPPING`). UE5's GC and Unity's managed heap both have runtime costs that can cause frame time spikes.

### API

```cpp
// Exclusive ownership — like std::unique_ptr but with borrow tracking
Own<Mesh> mesh = MakeOwn<Mesh>(vertices, indices);
mesh->Draw();

// Immutable borrow — multiple allowed simultaneously
{
    Ref<Mesh> r1(mesh);  // borrow 1
    Ref<Mesh> r2(mesh);  // borrow 2 (OK: multiple immutable)
    r1->GetVertexCount(); // read-only access
}
// borrows released when Ref goes out of scope

// Mutable borrow — exclusive, no other borrows allowed
{
    RefMut<Mesh> rm(mesh);  // exclusive mutable access
    rm->SetVertices(newData);
    // Taking Ref<Mesh> here would assert in debug!
}
```

### Borrow Rules (enforced in debug builds)

1. **Multiple `Ref<T>` borrows** can coexist — any number of readers
2. **One `RefMut<T>` borrow** requires exclusivity — no other borrows (Ref or RefMut) can exist
3. **Violations abort** with a diagnostic message in debug builds
4. **Zero cost** in shipping — all tracking is `#ifdef`'d out

### BorrowTracker (debug only)

Every `Own<T>` embeds a `BorrowTracker` with atomic counters for immutable and mutable borrows. The tracker is compiled out entirely in `UZ_SHIPPING` builds:

```cpp
// Debug build: Own<T> = T* + BorrowTracker (2 atomics)
// Shipping:    Own<T> = T* (same as raw pointer)
```

## Part 2: Allocators

Three purpose-built allocators for different allocation patterns, all named for memory budget tracking.

### LinearAllocator

Bump-pointer allocator. O(1) alloc, no individual free — `Reset()` reclaims everything at once. Ideal for per-scope temporaries.

```cpp
LinearAllocator alloc("frame_temps", 1024 * 1024);  // 1 MB
auto* data = alloc.New<MeshData>(vertices, indices);
// ... use data ...
alloc.Reset();  // reclaim everything instantly
```

| Property | Value |
|----------|-------|
| Alloc cost | O(1) — bump pointer + alignment |
| Free cost | N/A — individual free not supported |
| Reset cost | O(1) — set offset to 0 |
| Fragmentation | None (sequential allocation) |
| Thread safety | None — one per thread |

### PoolAllocator\<T\>

Fixed-size block pool with O(1) alloc/free via intrusive free list. Ideal for many objects of the same type (particles, entities, components).

```cpp
PoolAllocator<Particle> pool("particles", 10000);
Particle* p = pool.Alloc(position, velocity, lifetime);
// ... use p ...
pool.Free(p);  // O(1) — returns to free list
```

| Property | Value |
|----------|-------|
| Alloc cost | O(1) — pop from free list |
| Free cost | O(1) — push to free list |
| Fragmentation | None (all blocks same size) |
| Thread safety | None — one per thread |

### FrameAllocator

Thin wrapper over `LinearAllocator` that resets at the start of each frame. Makes the frame-scoped lifetime explicit.

```cpp
FrameAllocator alloc("render_frame", 2 * 1024 * 1024);  // 2 MB

// Each frame:
alloc.BeginFrame();  // reset — all previous pointers are now dangling
auto* cmd = alloc.New<DrawCommand>(mesh, material);
// ... use cmd throughout the frame ...
// No need to free — BeginFrame() reclaims everything
```

## Current Limitations

### No Custom Drop for Complex Types

`Own<T>` calls `delete m_ptr` — plain C++ destructor invocation. There is no custom "drop" mechanism (like Rust's `Drop` trait) for types that need non-trivial cleanup:

- **GPU resources** — releasing descriptor indices, waiting on fences before destroying buffers/textures
- **Event subscriptions** — unregistering from event buses on destruction
- **System registration** — removing from physics world, render world, audio engine

Currently, these require explicit `Destroy()` calls before the `Own<T>` goes out of scope. The C++ destructor (`~T()`) can handle some cleanup, but it cannot safely wait on GPU fences or interact with other systems that may already be destroyed during shutdown.

**Planned**: A `Disposable` interface or deferred-destruction queue that collects resources needing multi-frame cleanup (e.g., wait N frames for GPU to finish, then release descriptor). This would integrate with the frame lifecycle rather than relying on destructor timing.

### No Allocator Integration in Own\<T\>

`Own<T>` uses `new`/`delete` (global heap). It does not use the engine's custom allocators (`LinearAllocator`, `PoolAllocator`). This means `Own<T>` allocations don't benefit from memory tracking or cache-friendly allocation patterns.

**Planned**: `Own<T, Allocator>` template parameter or `MakeOwn<T>(allocator, args...)` overload.

### No Leak Detection

`Own<T>` prevents double-free and enforces single ownership, but it **cannot prevent leaks**:

- An `Own<T>` stored in a container that's never cleared — leak
- An `Own<T>` member in an object that's never destroyed — leak
- A raw `new` that bypasses `Own<T>` entirely (rule violation, but happens) — leak
- Circular references via raw pointer observers where neither side owns the cycle — leak

Currently `MakeOwn<T>()` is just `new T(args...)` — no allocation tracking, no category tagging, no shutdown audit. A leak silently goes unnoticed.

**Planned**: Debug-mode allocation tracker that:
1. Tags every `MakeOwn<T>()` with a category name (e.g., `"Mesh"`, `"Texture"`, `"Component"`)
2. Maintains per-category live allocation counts (atomic increment on alloc, decrement on free)
3. At engine shutdown, logs all categories with non-zero counts as leaks
4. Optionally captures allocation call sites (file:line) for leak reports

### No VRAM Lifecycle Management

CPU memory and GPU memory have fundamentally different lifetime rules:

```
CPU:  allocate → use → free                     (immediate, safe anytime)
GPU:  allocate → record commands → submit → GPU executes → fence → SAFE to free
```

Calling `delete` on a GPU resource while the GPU is still reading it causes a crash or silent corruption. The problem is that `Own<T>` has no concept of deferred destruction — it frees immediately when the pointer goes out of scope.

**Current workaround**: `device->WaitIdle()` before destroying GPU resources. This works but stalls the entire GPU pipeline — unacceptable in a render loop. It's only used at shutdown and resize.

**What's needed**: A per-frame deferred-destruction queue:

```
Frame N:     Record draw using texture X
Frame N:     Game code destroys texture X → pushed to deferred queue (not freed yet)
Frame N+1:   GPU still executing frame N commands — texture X still in use
Frame N+2:   Fence for frame N completes → deferred queue drains → texture X actually freed
```

This is how every production engine handles GPU resource lifetime:

| Engine | VRAM Lifecycle |
|--------|---------------|
| **UE5** | `FDeferredDeletionQueue` — resources queued for deletion after N frames. `FRHIResource::Release()` doesn't free immediately; decrements refcount, adds to queue when zero. |
| **Godot** | `RenderingDevice::_free_pending_resources()` — called each frame, frees resources whose fence has completed. Uses frame-indexed ring buffer. |
| **UzEngine** | **Not yet implemented** — currently `WaitIdle()` only. Needs deferred queue integrated with frame fence values. |

**Planned design**:

```cpp
/// Deferred GPU resource destruction queue.
/// Resources are held for kFramesInFlight frames before actual release.
class DeferredDestroyQueue {
public:
    /// Queue a resource for deferred destruction.
    /// The resource will be freed after the GPU finishes all commands
    /// that may reference it (tracked by frame fence value).
    void Enqueue(RHIBufferHandle buffer, uint64_t frameFenceValue);
    void Enqueue(RHITextureHandle texture, uint64_t frameFenceValue);

    /// Called once per frame after fence wait.
    /// Frees all resources whose frameFenceValue <= completedFenceValue.
    void Flush(uint64_t completedFenceValue);
};
```

This integrates with the existing triple-buffered frame model:
- Frame N records commands → fence value N
- Frame N+2 waits for fence N-2 → `Flush(fenceN-2)` releases resources queued at frame N-2
- Resources survive exactly `kFramesInFlight` frames — no stalls, no dangling

The `DescriptorAllocator` free-list and `ResourcePool<T>` generational slots are already designed for this pattern — they just need the deferred-destroy queue to drive the timing.

## Why Not GC

| Concern | GC | UzEngine |
|---------|-----|----------|
| Frame time | GC pauses (unpredictable spikes) | Deterministic — no pauses ever |
| Cache layout | Object headers + heap fragmentation | Tight allocator-controlled layout |
| GPU resources | GC can't manage fence-based lifetimes | Explicit lifecycle management (fence sync before destroy) |
| Server performance | GC spikes cause tick jitter | Predictable latency |

## Rules

1. **NEVER** use raw `new`/`malloc` in engine code — use `Own<T>`, `MakeOwn<T>()`, or an allocator
2. **NEVER** use `std::shared_ptr` — use `Own<T>` for ownership, `Ref<T>`/`RefMut<T>` for borrowing
3. **ALWAYS** name allocators — every allocator gets a debug name for tracking: `"particles"`, `"frame_temps"`, `"scene_nodes"`
4. **ALWAYS** prefer `FrameAllocator` for per-frame temporaries — no individual free overhead
5. **ALWAYS** prefer `PoolAllocator<T>` for many objects of the same type with individual free
