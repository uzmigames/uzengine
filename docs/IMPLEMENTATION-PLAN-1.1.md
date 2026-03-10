# Implementation Plan — Milestone 1.1 Math & Containers

**Status**: Spec complete. Ready to implement.

**Duration**: ~2-3 weeks (parallel implementation possible)

**Thirdparty Setup**: Run `scripts/setup.ps1` first to download spirv-reflect, xxhash, sse_mathfun, mikktspace.

---

## Phase 1: Math Foundation (Week 1)

### Task 1.1.0a — Vec2/Vec3/Vec4 Types
**Files**: `runtime/src/core/math/uz_math.h` (~250 lines)

**What to implement**:
```cpp
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; };

// Constructors
Vec3() = default;
Vec3(float x, float y, float z);
Vec3(float* ptr);
Vec3(XMVECTOR xm);

// Operators
Vec3 operator+(const Vec3& other) const;
Vec3 operator*(float scalar) const;
bool operator==(const Vec3& other) const;

// Methods
float dot(const Vec3& other) const;
Vec3 cross(const Vec3& other) const;
float length() const;
Vec3 normalize() const;
```

**Acceptance**: All constructors, operators, and methods compile. Tests pass.

### Task 1.1.0b — Mat4 Type
**Files**: `runtime/src/core/math/uz_math.h` (~100 lines added)

**What to implement**:
```cpp
struct Mat4 {
    XMFLOAT4X4 m;  // Column-major (DirectX convention)

    Mat4() = default;
    Mat4(const XMMATRIX& xm);

    Mat4 operator*(const Mat4& other) const;
    Vec4 operator*(const Vec4& v) const;

    Mat4 transpose() const;
    Mat4 inverse() const;

    Vec3 getTranslation() const;
    Quat getRotation() const;
    Vec3 getScale() const;
};
```

**Acceptance**: Matrix multiplication verified. Inverse passes numerical stability tests.

### Task 1.1.0c — Quat Type
**Files**: `runtime/src/core/math/uz_math.h` (~100 lines added)

**What to implement**:
```cpp
struct Quat {
    float x, y, z, w;

    Quat() = default;
    Quat(float x, float y, float z, float w);
    Quat(const Vec3& axis, float angle);
    Quat(const XMVECTOR& xm);

    Quat operator*(const Quat& other) const;

    Quat conjugate() const;
    Quat inverse() const;
    Quat normalize() const;

    Quat slerp(const Quat& other, float t) const;
    Mat4 toMatrix() const;
};
```

**Acceptance**: Slerp produces smooth blending. Matrix conversion round-trips correctly.

### Task 1.1.0d — Transform Type
**Files**: `runtime/src/core/math/uz_math.h` (~80 lines added)

**What to implement**:
```cpp
struct Transform {
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    Transform() : position(0,0,0), rotation(0,0,0,1), scale(1,1,1) {}

    Mat4 toMatrix() const;
    static Transform fromMatrix(const Mat4& m);

    Transform compose(const Transform& parent) const;  // parent-relative
    Transform inverse() const;
};
```

**Acceptance**: Parent-child hierarchy works correctly. Inverse composition is numerically stable.

### Task 1.1.0e — Math Utils & Tests
**Files**:
- `runtime/src/core/math/uz_math_utils.h` (~100 lines)
- `tests/core/test_math_vectors.cpp` (~200 lines)
- `tests/core/test_math_matrix.cpp` (~150 lines)
- `tests/core/test_math_quat.cpp` (~150 lines)
- `tests/core/test_transform.cpp` (~100 lines)

**Utilities**:
```cpp
float lerp(float a, float b, float t);
Vec3 lerp(const Vec3& a, const Vec3& b, float t);
float clamp(float x, float min, float max);
float distance(const Vec3& a, const Vec3& b);
bool almostEqual(float a, float b, float epsilon);
```

**Tests**: 600+ lines covering all types, operations, edge cases, numerical stability.

**Acceptance**: 95%+ coverage. All tests pass. Disassembly shows SIMD instructions.

---

## Phase 2: Color & Geometry (Week 1-2)

### Task 1.1.1 — Color Types
**Files**:
- `runtime/src/core/math/uz_color.h` (~100 lines)
- `runtime/src/core/math/uz_color.cpp` (~50 lines, optional LUT)
- `tests/core/test_color.cpp` (~80 lines)

**What to implement**:
```cpp
struct LinearColor {
    float r, g, b, a;
    sRGB toSRGB() const;
    Vec4 toVec4() const;
};

struct sRGB {
    uint8_t r, g, b, a;
    LinearColor toLinear() const;
};

float linear_to_srgb(float linear);
float srgb_to_linear(float srgb);
```

**Acceptance**: Round-trip error < 0.01%. Tests verify gamma curve.

### Task 1.1.2 — Geometry Types
**Files**:
- `runtime/src/core/geometry/uz_geometry.h` (~150 lines)
- `runtime/src/core/geometry/uz_geometry.cpp` (~80 lines)
- `tests/core/test_geometry.cpp` (~150 lines)

**What to implement**:
```cpp
struct AABB {
    Vec3 min, max;

    bool contains(const Vec3& point) const;
    bool intersects(const AABB& other) const;
    bool intersectsSphere(const Vec3& center, float radius) const;
    AABB expand(const Vec3& point) const;
    Vec3 center() const;
};

struct Frustum {
    Plane planes[6];  // near, far, left, right, top, bottom

    bool contains(const Vec3& point) const;
    bool intersects(const AABB& aabb) const;
    static Frustum extractFromMatrix(const Mat4& proj);
};
```

**Acceptance**: Frustum culling verified against known geometries. AABB expansion is correct.

---

## Phase 3: Containers (Week 2)

### Task 1.1.3a — RingBuffer & FreeList
**Files**:
- `runtime/src/core/containers/uz_ring_buffer.h` (~80 lines)
- `runtime/src/core/containers/uz_free_list.h` (~120 lines)
- `tests/core/test_containers.cpp` (~180 lines, part 1)

**What to implement**:
```cpp
template<typename T, size_t Size>
class RingBuffer {
public:
    void push(const T& val);
    T pop();
    bool full() const;
    bool empty() const;
    size_t count() const;
private:
    T buffer[Size];
    size_t head = 0, tail = 0;
};

template<typename T>
class FreeList {
public:
    FreeList(size_t capacity);
    T* allocate();
    void free(T* ptr);
    size_t allocated() const;
private:
    T* buffer;
    T* freeHead;
};
```

**Acceptance**: RingBuffer overflow doesn't corrupt. FreeList reuses freed slots. Zero leaks.

### Task 1.1.3b — HandleMap (Critical for ECS)
**Files**:
- `runtime/src/core/containers/uz_handle.h` (~40 lines)
- `runtime/src/core/containers/uz_handle_map.h` (~150 lines)
- `tests/core/test_containers.cpp` (~180 lines, part 2)

**What to implement**:
```cpp
struct Handle {
    uint16_t index;
    uint16_t generation;

    bool operator==(const Handle& other) const;
};

template<typename T>
class HandleMap {
public:
    HandleMap(size_t capacity);

    Handle insert(const T& val);
    T* get(Handle h);
    bool remove(Handle h);
    bool valid(Handle h) const;

    // Dense iteration
    template<typename F>
    void forEach(F&& fn);
private:
    struct Entry {
        T value;
        uint16_t generation;
    };
    std::vector<Entry> entries;
};
```

**Acceptance**: 100K entities create/destroy stress test passes. Handle stability verified.

---

## Phase 4: Allocators (Week 2-3)

### Task 1.1.4 — Memory Allocators
**Files**:
- `runtime/src/core/memory/uz_allocator.h` (~80 lines)
- `runtime/src/core/memory/uz_linear_allocator.h` (~80 lines)
- `runtime/src/core/memory/uz_pool_allocator.h` (~100 lines)
- `runtime/src/core/memory/uz_frame_allocator.h` (~80 lines)
- `runtime/src/core/memory/uz_allocator.cpp` (~50 lines)
- `tests/core/test_allocators.cpp` (~180 lines)

**What to implement**:
```cpp
class IAllocator {
public:
    virtual ~IAllocator() = default;
    virtual void* allocate(size_t size, size_t align) = 0;
    virtual void deallocate(void* ptr) = 0;
};

class LinearAllocator : public IAllocator {
public:
    LinearAllocator(size_t capacity);
    void* allocate(size_t size, size_t align) override;
    void reset();
    size_t used() const;
private:
    uint8_t* buffer;
    size_t offset = 0;
};

template<typename T, size_t MaxCount>
class PoolAllocator : public IAllocator {
    // Intrusive free list
};

class FrameAllocator : public IAllocator {
    // Resets per frame, audit for leaks
};
```

**Acceptance**: LinearAllocator stress tested. PoolAllocator fragmentation = 0. FrameAllocator audits detect leaks.

---

## Phase 5: Random & Polish (Week 3)

### Task 1.1.5 — Random Number Generator
**Files**:
- `runtime/src/core/random/uz_random.h` (~80 lines)
- `runtime/src/core/random/uz_random.cpp` (~30 lines)
- `tests/core/test_random.cpp` (~100 lines)

**What to implement**:
```cpp
class Random {
public:
    Random(uint64_t seed = 0);

    uint32_t next();      // [0, 2^32)
    float nextf();        // [0.0, 1.0)
    uint32_t next(uint32_t max);
    float nextRange(float min, float max);

    void setSeed(uint64_t seed);
private:
    uint64_t state;  // PCG state
};
```

**Acceptance**: Distribution passes chi-square test. Deterministic seeding works.

### Task 1.1.6 — Documentation & Integration
**Files**:
- Update [ARCHITECTURE.md](ARCHITECTURE.md) with Math & Containers section
- Update [ROADMAP.md](ROADMAP.md) mark 1.1 complete
- Create `runtime/src/core/README.md` with module overview

**What to document**:
- All types and their invariants
- When to use each container (RingBuffer vs FreeList vs HandleMap)
- Allocator strategies (when to use Linear vs Pool vs Frame)
- SIMD considerations
- Performance targets and how to measure

**Acceptance**: New developer can understand and extend the math/container layer from docs alone.

---

## Testing Strategy

### Unit Tests (all modules)
- **Vec3/Vec4/Mat4**: arithmetic correctness, numerical stability, edge cases
- **Quat**: slerp blending, matrix conversion round-trip
- **AABB/Frustum**: intersection tests with known geometries
- **Containers**: stress tests, edge cases, iterator correctness
- **Allocators**: fragmentation tests, alignment checks, leak detection

### Benchmark Tests
- **Vector length**: target <5 cycles
- **Matrix multiplication**: target <20 cycles
- **Memory allocation**: O(1) ops
- **Random generation**: >1 billion values/sec

### Coverage Requirements
- **95%+ line coverage** across all modules
- **100% path coverage** for critical paths (Vec3 arithmetic, HandleMap insert/remove)
- **No dead code** — delete if not used

### Validation
- **Disassembly check**: Verify SSE/AVX instructions in release build
- **Numerical validation**: Use golden values from reference engines (UE5, Godot)
- **Integration smoke test**: Load/render simple scene using all math types

---

## Sequential Implementation Order

1. **Vec2/Vec3/Vec4 + Mat4** (math core — everything depends on this)
2. **Tests for vectors/matrix** (validate before continuing)
3. **Quat + Transform** (scene transforms)
4. **Color types** (needed for rendering later)
5. **AABB + Frustum** (culling prep)
6. **RingBuffer** (simplest container)
7. **FreeList** (object pools)
8. **HandleMap** (critical for ECS — test thoroughly)
9. **Allocators** (all 3, in order: Linear → Pool → Frame)
10. **Random** (simplest, can do anytime)
11. **Documentation + integration tests**

---

## File Size & Responsibility Checks

| File | Lines | Responsibility |
|------|-------|-----------------|
| uz_math.h | ~250 | Vector/Matrix/Quat/Transform types only |
| uz_math.cpp | ~120 | Inverse, slerp, decompose — complex math only |
| uz_math_utils.h | ~100 | Utility functions (lerp, clamp, etc.) |
| uz_color.h/cpp | ~150 | Color types and conversions |
| uz_geometry.h/cpp | ~230 | AABB, Frustum, intersection tests |
| uz_ring_buffer.h | ~80 | Ring buffer template |
| uz_free_list.h | ~120 | Free list allocator |
| uz_handle_map.h | ~150 | Handle + map (dense storage) |
| uz_allocator.h/cpp | ~130 | Allocator interface + base impl |
| uz_linear_allocator.h | ~80 | Bump allocator |
| uz_pool_allocator.h | ~100 | Pool allocator template |
| uz_frame_allocator.h | ~80 | Frame allocator |
| uz_random.h/cpp | ~110 | PCG RNG |

**Total**: ~1900 lines source code + ~1350 lines tests = 3250 lines

All files stay under 1500-line limit. Single responsibility per file.

---

## Thirdparty Integration (Placeholder for Future)

### xxhash (1.5 Asset Processor)
```cpp
#include "xxhash/xxhash.h"

uint64_t fingerprint = XXH64(materialData, sizeof(materialData), kSeed);
```

### SSE Math Fun (1.1.6, if needed)
```cpp
#include "sse_mathfun/sse_mathfun.h"

float fast_sin = sin_ps(angle);  // SIMD sin
```

### atomic_queue (1.2 RHI)
```cpp
#include "atomic_queue/atomic_queue.h"

atomic_queue::AtomicQueue<GPUCommand> cmdQueue(1024);
```

---

## Success Criteria Checklist

- [ ] All types compile and link
- [ ] All operators work correctly (verified by tests)
- [ ] 95%+ test coverage across all modules
- [ ] Disassembly shows SIMD instructions in release build
- [ ] Vector length: <5 cycles, matrix mult: <20 cycles
- [ ] HandleMap scales to 100K entities
- [ ] FrameAllocator detects leaks in debug
- [ ] All files under 1500 lines
- [ ] Zero compiler warnings
- [ ] Documentation complete for all types
- [ ] Code builds on MSVC and Clang
- [ ] 1.1 marked complete in ROADMAP.md

---

**Ready to start?** Run `scripts/setup.ps1` to pull the new thirdparty deps, then begin Task 1.1.0a.
