# Memory & Allocators

UzEngine provides purpose-built allocators for different allocation patterns,
plus a Rust-inspired ownership model for safe resource management.

> **Headers**: `runtime/engine/memory/allocators.h`, `runtime/engine/memory/ownership.h`
> **API Reference**: [Core Reference](../../reference/core/index.md)

## Allocator Types

### LinearAllocator (Bump Pointer)

Fastest allocator. Allocates by advancing a pointer. Only supports bulk reset.

```cpp
LinearAllocator alloc("Scratch", 1024 * 1024);  // 1 MB

void* a = alloc.Alloc(256);          // bump pointer
auto* obj = alloc.New<MyStruct>();   // typed allocation

alloc.Reset();  // free everything at once
```

**Use for**: Temporary per-frame data, string builders, command buffers.

### PoolAllocator (Fixed-Size Blocks)

O(1) alloc/free for same-size objects. Uses an intrusive free chain.

```cpp
PoolAllocator pool("Particles", sizeof(Particle), 10000);

void* p = pool.Alloc();   // O(1) — pop from free list
pool.Free(p);              // O(1) — push to free list
```

**Use for**: Particles, entities, components, any type with high churn.

### FrameAllocator

Linear allocator with automatic per-frame reset. Call `NewFrame()` at the
start of each frame to reset the pointer.

```cpp
FrameAllocator frame("FrameTemp", 2 * 1024 * 1024);  // 2 MB

// Frame N
void* data = frame.Alloc(512);
// ... use data during this frame ...

frame.NewFrame();  // resets — all previous allocations are invalidated
```

**Use for**: Per-frame temporaries (render commands, visibility lists, debug text).

## Ownership Model

UzEngine uses smart pointer wrappers inspired by Rust's ownership semantics:

| Type | Rust Equivalent | Semantics |
|------|-----------------|-----------|
| `Own<T>` | `Box<T>` | Exclusive ownership, non-copyable |
| `Ref<T>` | `&T` | Shared immutable borrow |
| `RefMut<T>` | `&mut T` | Exclusive mutable borrow |

In debug builds, `Ref<T>` and `RefMut<T>` enforce borrow rules at runtime
using `std::atomic` counters — multiple `Ref<T>` allowed, only one `RefMut<T>`.

## Budget Tracking

All allocators are named and report their usage:

```cpp
alloc.Name();       // "Scratch"
alloc.Used();       // bytes allocated
alloc.Remaining();  // bytes available
alloc.GetSize();    // total capacity
```

This integrates with the stat counter system for runtime monitoring.

## Related Pages

- [Profiling & Debugging](../profiling/overview.md) — Stat counters and monitoring
- [Architecture](../architecture.md) — System design
