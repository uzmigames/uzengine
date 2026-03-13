# Containers Module Reference

Zero-copy, allocation-friendly data structures.

> **Headers**: `core/containers/`
> **Guide**: [Memory & Allocators](../../guides/memory/overview.md)

## RingBuffer\<T, N\> (`ring_buffer.h`)

Lock-free SPSC (single-producer, single-consumer) circular queue.
Uses acquire/release memory ordering for thread safety.

```cpp
RingBuffer<Command, 256> queue;

queue.Push(cmd);              // Returns false if full
Command out;
bool ok = queue.Pop(out);     // Returns false if empty

queue.Size();                 // Current element count
queue.IsEmpty();
queue.IsFull();
```

**Use for**: Command queues between threads, producer-consumer patterns.

---

## FreeList\<T\> (`free_list.h`)

Intrusive free list for element-stable O(1) allocation. Elements are
stored in a flat vector; freed slots are chained together.

```cpp
FreeList<Entity> pool(1024);  // Reserve 1024 slots

uint32_t idx = pool.Alloc();  // O(1)
Entity& e = pool.Get(idx);    // Direct access
pool.Free(idx);                // O(1), chains to free list
```

**Use for**: Entity storage, component pools, any collection with frequent add/remove.

---

## HandleMap\<T\> (`handle_map.h`)

Generational handle → T mapping. Handles include a generation counter
so that stale handles (pointing to freed/reallocated slots) are detected.

```cpp
HandleMap<Mesh> meshes;

Handle h = meshes.Insert(mesh);
Mesh* m = meshes.Get(h);       // nullptr if stale
meshes.Remove(h);
meshes.Get(h);                  // nullptr (generation mismatch)
```

| Method | Description |
|--------|-------------|
| `Insert(T)` | Returns Handle (index + generation) |
| `Get(Handle)` | Returns `T*` or nullptr if invalid |
| `Remove(Handle)` | Marks slot free, bumps generation |
| `IsValid(Handle)` | Check without access |
| `Size()` | Active element count |

**Use for**: GPU resource pools (RHI handles wrap this pattern).

---

## FrameArray\<T\> (`frame_array.h`)

Temporary dynamic array with frame lifetime. Backed by a linear allocator
that resets each frame.

```cpp
FrameArray<DrawCall> draws(frameAllocator);

draws.Push(drawCall);
draws[0];                // Index access
draws.Size();            // Element count
// Invalidated on allocator.Reset() / NewFrame()
```

**Use for**: Per-frame lists (draw calls, visibility results, debug primitives).
