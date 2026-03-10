# Containers Specification

## Overview
Zero-copy, allocation-friendly data structures for ECS, object pools, and game logic. No std::vector — all containers use fixed capacity or provided allocators.

## RingBuffer (SPSC Lock-Free Queue)

### Use Case
Single-Producer Single-Consumer: audio decoder → audio output, input events → game logic thread

### Interface
```cpp
template<typename T, size_t Capacity>
class RingBuffer {
public:
    RingBuffer();
    ~RingBuffer();

    bool TryPush(const T& item);  // Producer: add to queue
    bool TryPush(T&& item);  // Producer: add rvalue
    bool TryPop(T& out);  // Consumer: remove from queue

    bool Empty() const;
    bool Full() const;
    size_t Size() const;
    void Clear();

    static constexpr size_t GetCapacity();
};
```

### Design
- **Lock-free**: std::atomic<size_t> head, tail with acquire/release semantics
- **Power-of-2 capacity**: Index masking vs modulo
- **Cache-line alignment**: alignas(64) to prevent false-sharing
- **SPSC guarantee**: One thread calls Push/Size/Full, one thread calls Pop/Empty

### Scenario: Circular Queue Full

**Given** a RingBuffer<int, 4> with 3 items
**When** TryPush(item) on full queue
**Then** returns false, queue unchanged

## FreeList (Intrusive Free Chain)

### Use Case
Element-stable allocation: remove from middle without invalidating other pointers

### Interface
```cpp
template<typename T>
class FreeList {
public:
    FreeList(size_t capacity);
    ~FreeList();

    template<typename... Args>
    T* Allocate(Args&&... args);  // O(1) allocate

    void Free(T* ptr);  // O(1) free, intrusive chain
    bool Contains(const T* ptr) const;

    size_t Count() const;
    size_t GetCapacity() const;
};
```

### Design
- **Intrusive**: Free chain stored in unused memory of freed blocks
- **Element-stable**: Allocated addresses never change (no reallocation)
- **Constructor/destructor**: Placement new/delete called on blocks

### Scenario: Reuse After Free

**Given** a FreeList with one allocation at address P
**When** Free(P) then Allocate() again
**Then** returns P (same address, reused)

## HandleMap (Generational Handle Storage)

### Use Case
ECS entities, safe handles that detect use-after-free: Handle<Entity> with generation counter

### Interface
```cpp
template<typename T>
struct Handle {
    uint32_t index;
    uint32_t generation;

    bool IsValid() const;  // generation != 0
    bool operator==(Handle other) const;
};

template<typename T, size_t Capacity>
class HandleMap {
public:
    HandleMap();

    template<typename... Args>
    Handle<T> Create(Args&&... args);  // Allocate + return handle

    void Destroy(Handle<T> h);  // Deallocate + invalidate handle

    T* Get(Handle<T> h);  // nullptr if stale or invalid
    const T* Get(Handle<T> h) const;

    bool IsValidHandle(Handle<T> h) const;

    size_t Count() const;

    template<typename Fn>
    void ForEach(Fn&& fn);  // Dense iteration over alive entries
};
```

### Design
- **Generational**: Each slot has generation counter
- **Handle = (index, generation)**: Both must match to be valid
- **Reuse detection**: After Destroy(), next Create() increments generation
- **Dense iteration**: ForEach only iterates alive entries

### Scenario: Use-After-Free Detection

**Given** `Handle<Entity> h = map.Create()`  // h = {0, 1}
**When** `map.Destroy(h)` then `map.Create()`  // next = {0, 2}
**Then** `map.Get(h)` returns nullptr (stale handle)
**And** new Handle {0, 2} is valid

## FrameArray (Temporary Allocation)

### Use Case
Temporary allocations with known lifetime (single frame): particles, sorted entities, queries

### Interface
```cpp
class FrameArray {
public:
    FrameArray(const char* name, size_t sizeBytes);

    template<typename T, typename... Args>
    T* New(Args&&... args);

    void* Alloc(size_t size, size_t alignment = alignof(max_align_t));

    void BeginFrame();  // Reset allocator

    size_t Used() const;
    size_t Remaining() const;
};
```

### Design
- **Stateless**: Internal LinearAllocator, auto-resets per frame
- **No Free**: All allocations live until BeginFrame()
- **Fast**: Bump pointer allocation O(1)

### Scenario: Frame Scoped Allocation

**Given** a FrameArray with 1MB capacity
**When** BeginFrame() called in frame N, then allocate 100KB
**And** BeginFrame() called in frame N+1
**Then** all N frame allocations are discarded, offset resets to 0

## Allocators Specification

### LinearAllocator

```cpp
class LinearAllocator {
public:
    LinearAllocator(const char* name, size_t sizeBytes);
    ~LinearAllocator();

    void* Alloc(size_t size, size_t alignment = alignof(max_align_t));
    template<typename T, typename... Args>
    T* New(Args&&... args);

    void Reset();  // Reset offset to 0

    size_t Used() const;
    size_t Remaining() const;
    const char* Name() const;
};
```

**Use**: Frame allocations, streaming loads, arena allocation

### PoolAllocator

```cpp
template<typename T>
class PoolAllocator {
public:
    PoolAllocator(const char* name, size_t capacity);

    template<typename... Args>
    T* Alloc(Args&&... args);

    void Free(T* ptr);
    bool Contains(const T* ptr) const;

    size_t Count() const;
    size_t GetCapacity() const;
};
```

**Use**: Object pools for entities, particles, bullets

### FrameAllocator

```cpp
class FrameAllocator {
public:
    FrameAllocator(const char* name, size_t sizeBytes);

    void* Alloc(size_t size, size_t alignment = alignof(max_align_t));
    template<typename T, typename... Args>
    T* New(Args&&... args);

    void BeginFrame();

    size_t Used() const;
    size_t Remaining() const;
};
```

**Use**: Temporary per-frame allocations (sorting, queries, collision narrowphase)

## Scenario: ECS Entity Creation/Destruction

**Given** a HandleMap<Entity, 100000> entities
**When** game creates 50K entities with components
**And** deletes 20K entities by destroying handles
**Then** memory usage reflects live entity count
**And** destroyed handles return nullptr on Get()
**And** new entities reuse destroyed slots with new generations
**And** ForEach() iterates only 30K alive entities

## Testing Requirements

### RingBuffer Tests
- Push/pop sequences
- Overflow detection (full queue)
- Empty queue behavior
- Size tracking

### FreeList Tests
- Allocate and free
- Reuse of freed slots
- Contains() validation
- Capacity limits

### HandleMap Tests
- Create/destroy/get lifecycle
- Generational validation (stale handles)
- Dense iteration (ForEach)
- 100K entity stress test

### Allocator Tests
- Alignment correctness
- Capacity enforcement
- Named tracking
- Reset/frame semantics

## Coverage
95%+ line coverage across all container types.
