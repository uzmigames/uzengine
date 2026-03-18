# PSO Cache Implementation Plan (Task 100.1)

> **Status**: Planning
> **Owner**: rhi-engineer
> **Depends on**: rhi.h (done), rhi_types.h (done), shader_cache.h (done), xxHash (vendored), StatCounter (done), fnv_hash.h (done)

## 1. Architecture Design

### 1.1 PsoCacheKey

The key must capture every field that affects the compiled PSO, excluding transient data (debugName, pointer values). Two identical descriptors must always produce the same key.

```cpp
/// Content-hash key for PSO deduplication.
/// Deterministic: same descriptor fields -> same key across runs.
struct PsoCacheKey {
    uint64_t hash;  // xxHash64 of the canonical byte representation

    bool operator==(const PsoCacheKey& o) const { return hash == o.hash; }
    bool operator!=(const PsoCacheKey& o) const { return hash != o.hash; }
};
```

**Hash inputs** (hashed in this exact order via `XXH3_64bits_update`):

```
Graphics PSO:
  vertexShader.index + vertexShader.generation     (8 bytes)
  pixelShader.index + pixelShader.generation       (8 bytes)
  topology                                          (1 byte, padded to 4)
  rasterizer.cullMode                               (1)
  rasterizer.fillMode                               (1)
  rasterizer.frontCounterClockwise                  (1)
  rasterizer.depthBias                              (4 float)
  rasterizer.slopeScaledDepthBias                   (4 float)
  depthStencil.depthEnable                          (1)
  depthStencil.depthWrite                           (1)
  depthStencil.depthFunc                            (1)
  renderTargetBlend[0..7] (each: 7 fields)          (7 * 8 = 56 bytes)
  renderTargetFormats[0..7]                          (8 bytes)
  renderTargetCount                                  (4)
  depthStencilFormat                                 (1, padded to 4)
  inputElementCount                                  (4)
  vertexStride                                       (4)
  inputElements[0..N] (if N > 0)                     (N * sizeof(RHIInputElementDesc))
```

**Why shader handles, not shader hashes?** Shader handles are deterministic within a session (same shader -> same handle). For cross-session disk cache, we use a separate `PsoDiskKey` that includes shader bytecode hashes (from `ShaderCache`'s identity hashes). This avoids hashing bytecode on every PSO lookup.

**Why xxHash64 and not FNV-1a?** The key payload is 100-200 bytes. xxHash64 has vastly better avalanche properties at this size. FNV-1a is reserved for small struct keys (< 32 bytes) per `fnv_hash.h` header comment.

### 1.2 In-Memory Cache

Open-addressing hash map, power-of-2 capacity, Robin Hood probing.

```
File: runtime/engine/rhi/pso_cache.h
File: runtime/engine/rhi/pso_cache.cpp
```

```cpp
class PsoCache {
public:
    /// Initializes with a fixed capacity (must be power-of-2).
    /// Allocates from permanent arena — no heap allocations after Init().
    void Init(uint32_t capacity = 256);
    void Shutdown();

    /// Looks up a PSO by key. Returns invalid handle on miss.
    RHIPipelineHandle Lookup(const PsoCacheKey& key) const;

    /// Inserts a PSO into the cache. Overwrites if key exists (hot-reload).
    void Insert(const PsoCacheKey& key, RHIPipelineHandle pipeline);

    /// Evicts all entries whose shader handle matches (hot-reload invalidation).
    /// Returns number of evicted entries.
    uint32_t InvalidateByShader(RHIShaderHandle shader);

    /// Evicts all entries. Does NOT destroy the pipeline handles — caller's
    /// responsibility (the backend owns the GPU objects).
    void Clear();

    /// Cache statistics.
    uint32_t GetEntryCount() const;
    uint32_t GetCapacity() const;
    float GetLoadFactor() const;

private:
    struct Slot {
        PsoCacheKey key;
        RHIPipelineHandle pipeline;
        bool occupied = false;
    };

    Slot* m_slots = nullptr;       // Power-of-2 array
    uint32_t m_capacity = 0;
    uint32_t m_count = 0;
    uint32_t m_mask = 0;           // capacity - 1
};
```

**Capacity**: 256 slots initial. At 70% load factor, rehash to 2x. Typical game has 30-80 unique PSOs, so 256 is generous. Rehash allocates from permanent arena (one-shot, no fragmentation).

**No STL**: No `std::unordered_map`. Open-addressing in a flat array is cache-friendly and predictable. Follows the engine's "no hidden allocations" rule.

### 1.3 Disk Persistence

Disk cache is **per-backend** because the serialized blob format is API-specific.

```
cache/pso/
  dx12/
    pso_cache.bin          <- ID3D12PipelineLibrary blob
  vulkan/
    pipeline_cache.bin     <- VkPipelineCache data
  metal/
    binary_archive/        <- MTLBinaryArchive directory
```

**DX12 path**: Use `ID3D12PipelineLibrary1`. On init, `CreatePipelineLibrary()` from the stored blob. On each PSO creation, `StorePipeline()` with a string key = hex(PsoCacheKey.hash). On shutdown, `Serialize()` to disk.

**Vulkan path**: Use `VkPipelineCache`. On init, `vkCreatePipelineCache()` with stored data. Pass the cache object to all `vkCreateGraphicsPipelines()` calls. On shutdown, `vkGetPipelineCacheData()` to disk.

**Metal path**: Use `MTLBinaryArchive` (macOS 11+ / iOS 14+). On init, load from URL. On PSO creation, `addRenderPipelineFunctions`. On shutdown, `serializeToURL`.

**Versioning**: The disk cache file has a 16-byte header:

```c
struct PsoDiskHeader {
    uint32_t magic;        // 'UZPS' = 0x53505A55
    uint32_t version;      // Bump on RHIGraphicsPipelineDesc layout change
    uint32_t backendId;    // RHIBackend enum value
    uint32_t reserved;     // Padding / future flags
};
```

If magic/version/backendId mismatch, discard and start fresh. No migration.

### 1.4 Warmup / Precache API

```cpp
/// Added to IRHIDevice or as standalone utility:

/// Schedules PSO creation for a list of known pipeline descriptors.
/// Called during scene load. Creates PSOs on a background thread (DX12)
/// or front-loads them into the pipeline cache (Vulkan).
///
/// # Arguments
/// * `descs` — Array of graphics pipeline descriptors to precache.
/// * `count` — Number of descriptors.
void PrecacheGraphicsPipelines(const RHIGraphicsPipelineDesc* descs, uint32_t count);
```

**Where called from**: `SceneLoader::Load()` after materials are parsed. Each material knows its shader combination + blend/depth state. The loader collects unique descriptors and calls `PrecacheGraphicsPipelines()`.

**Implementation**: On DX12, this calls `ID3D12PipelineLibrary1::LoadGraphicsPipeline()` for each key. On cache miss, it falls through to `CreateGraphicsPipeline()`. On Vulkan, the `VkPipelineCache` handles this automatically — just creating the pipelines populates the cache.

**Manifest file** (optional, editor-only):

```json
// cache/pso/precache_manifest.json
{
    "scene_default": [
        "a1b2c3d4e5f6",
        "f7e8d9c0b1a2"
    ]
}
```

Scene-keyed lists of PSO hash strings. Generated during cooking, consumed during scene load. Shipping builds embed this in the `.uzpak`.

## 2. Integration Points

### 2.1 Position in the RHI Call Stack

```
PipelineLibrary::GetForwardPBR()
    │
    ▼
PsoCache::Lookup(key)           ← NEW: in-memory dedup
    │
    ├── HIT → return cached RHIPipelineHandle
    │
    └── MISS
        │
        ▼
    IRHIDevice::CreateGraphicsPipeline(desc)
        │                              ← Backend uses disk cache internally
        ▼
    PsoCache::Insert(key, handle)  ← NEW: store for future lookups
        │
        ▼
    return handle
```

The `PsoCache` sits **above** `IRHIDevice` but **inside** the RHI layer (same directory). It is NOT a backend-specific class — it works with any backend. The disk cache, however, is backend-internal.

### 2.2 Interception Strategy

**Option A (chosen): Wrapper in PipelineLibrary** — `PipelineLibrary` already caches per-pass PSOs. We add `PsoCache` as a member and route all `CreateGraphicsPipeline` calls through it. Any code that creates PSOs outside `PipelineLibrary` also goes through the cache.

**Option B (rejected): Override IRHIDevice** — Would require a decorator pattern around the device, adding complexity. The cache is better as an explicit layer.

```cpp
// In PipelineLibrary::GetForwardPBR():
PsoCacheKey key = ComputePsoCacheKey(desc);
auto cached = m_psoCache.Lookup(key);
if (cached.IsValid()) {
    UZ_STAT_INCREMENT(RHI, PsoCacheHits);
    return cached;
}

auto handle = m_device->CreateGraphicsPipeline(desc);
if (handle.IsValid()) {
    m_psoCache.Insert(key, handle);
    UZ_STAT_INCREMENT(RHI, PsoCacheMisses);
}
return handle;
```

### 2.3 Backend Changes

| Backend | Change | File |
|---------|--------|------|
| DX12 | Add `ID3D12PipelineLibrary1*` member, load/save blob, pass to PSO creation | `rhi_dx12.cpp` |
| Vulkan | Add `VkPipelineCache` member, load/save data, pass to `vkCreateGraphicsPipelines` | `rhi_vulkan.cpp` |
| Metal | Add `MTLBinaryArchive*` member, load/save, pass to pipeline descriptor | `rhi_metal.mm` |

The backend disk cache is **transparent** to the caller. `CreateGraphicsPipeline()` internally checks the disk cache before compiling.

### 2.4 Stats / Telemetry

```cpp
// In profiler.h or rhi/pso_cache.h:
UZ_DECLARE_STAT(RHI, PsoCacheHits);
UZ_DECLARE_STAT(RHI, PsoCacheMisses);
UZ_DECLARE_STAT(RHI, PsoCacheEvictions);   // Hot-reload invalidations
UZ_DECLARE_STAT(RHI, PsoCreateTimeUs);     // Total microseconds spent in CreateGraphicsPipeline
```

Reset per frame except `PsoCreateTimeUs` (cumulative, reset on scene load).

## 3. Data Flow

### 3.1 Scene Load Warmup

```
SceneLoader::Load("level1.uzscene")
    │
    ├── Parse materials → collect unique (shader, blend, depth, RT format) combos
    │
    ├── Build RHIGraphicsPipelineDesc[] array (deduplicated)
    │
    └── PrecacheGraphicsPipelines(descs, count)
            │
            ├── For each desc:
            │   key = ComputePsoCacheKey(desc)
            │   if (m_psoCache.Lookup(key).IsValid()) → skip
            │   else → CreateGraphicsPipeline(desc) → Insert into cache
            │
            └── Log: "Precached {} PSOs ({} hits, {} misses) in {:.1f} ms"
```

### 3.2 Runtime CreateGraphicsPipeline

```
Caller (PipelineLibrary, material system, etc.)
    │
    ▼
ComputePsoCacheKey(desc)
    │
    ▼
PsoCache::Lookup(key)
    │
    ├── HIT → UZ_STAT_INCREMENT(RHI, PsoCacheHits) → return handle
    │
    └── MISS
        ▼
    IRHIDevice::CreateGraphicsPipeline(desc)
        │
        │   [Backend-internal: check disk cache]
        │   DX12: ID3D12PipelineLibrary1::LoadGraphicsPipeline(hexKey)
        │     HIT  → return PSO from library
        │     MISS → D3D12CreateGraphicsPipeline → StorePipeline(hexKey, pso)
        │
        │   Vulkan: vkCreateGraphicsPipelines with VkPipelineCache
        │     (Vulkan driver handles disk cache internally)
        │
        ▼
    PsoCache::Insert(key, handle)
    UZ_STAT_INCREMENT(RHI, PsoCacheMisses)
    return handle
```

### 3.3 Disk Cache Save/Load

```
Engine startup:
    IRHIDevice::Init()
        → LoadDiskCache("cache/pso/{backend}/")
        → If file missing or version mismatch → start empty

Engine shutdown (or explicit flush):
    IRHIDevice::Shutdown()
        → SaveDiskCache("cache/pso/{backend}/")
        → Write header + backend-specific blob
```

**When to save**: On clean shutdown only. Crash = no save (stale cache is fine, just slower next launch). Editor can also save on scene close.

## 4. Files to Create / Modify

### New Files

| File | Purpose | ~Lines |
|------|---------|--------|
| `runtime/engine/rhi/pso_cache.h` | PsoCacheKey + PsoCache class declaration | ~120 |
| `runtime/engine/rhi/pso_cache.cpp` | PsoCache implementation (open-addressing map, key computation) | ~250 |

### Modified Files

| File | Change | Scope |
|------|--------|-------|
| `runtime/engine/rhi/rhi_types.h` | Add `PsoCacheKey` struct (or keep in pso_cache.h) | ~10 lines |
| `runtime/engine/renderer/core/pipeline_library.h` | Add `PsoCache m_psoCache` member, add `CreateCachedPipeline()` helper | ~15 lines |
| `runtime/engine/renderer/core/pipeline/ (split: pipeline_lifecycle.cpp, pipeline_geometry.cpp, pipeline_shadow.cpp, pipeline_atmosphere.cpp, pipeline_postprocess.cpp)` | Route all `CreateGraphicsPipeline` through cache lookup | ~40 lines |
| `runtime/engine/rhi/rhi_dx12.cpp` | Add `ID3D12PipelineLibrary1` load/save, pass to PSO creation | ~80 lines |
| `runtime/engine/rhi/rhi_vulkan.cpp` | Add `VkPipelineCache` load/save, pass to pipeline creation | ~60 lines |
| `runtime/engine/profiling/profiler.h` | Add `UZ_DECLARE_STAT(RHI, PsoCacheHits/Misses/Evictions)` | ~5 lines |

### File Dependency Graph

```
pso_cache.h
    ├── includes: rhi_types.h (RHIPipelineHandle, RHIShaderHandle)
    ├── includes: <cstdint>
    └── used by: pipeline_library.h, rhi_dx12.cpp, rhi_vulkan.cpp

pso_cache.cpp
    ├── includes: pso_cache.h
    ├── includes: xxhash.h (XXH3_64bits)
    ├── includes: profiler.h (stat counters)
    └── includes: log.h (UZ_LOG)
```

## 5. Testing Strategy

### 5.1 Unit Tests — `runtime/tests/engine/rhi/test_pso_cache.cpp`

```
PsoCacheKey Tests:
  - Same descriptor → same key (deterministic)
  - Different shader handles → different key
  - Different blend state → different key
  - Different rasterizer → different key
  - Different RT formats → different key
  - Different RT count → different key
  - Different depth format → different key
  - Different topology → different key
  - Different input elements → different key
  - Bindless (0 elements) vs SM5 (3 elements) → different key
  - debugName difference → SAME key (debugName excluded)
  - Default-constructed descs → consistent key

PsoCache (in-memory) Tests:
  - Insert + Lookup → returns same handle
  - Lookup miss → returns invalid handle
  - Insert duplicate key → overwrites (returns new handle)
  - InvalidateByShader → evicts matching, keeps others
  - Clear → all lookups return invalid
  - Load factor stays below 0.7 after many inserts
  - Capacity doubles on rehash
  - GetEntryCount tracks correctly through insert/evict/clear

Integration Tests:
  - PipelineLibrary with PsoCache → second GetForwardPBR() call is cache hit
  - Hot-reload: recompile shader → InvalidateByShader → next Get*() rebuilds PSO
  - Stat counters increment correctly (hits/misses/evictions)
```

### 5.2 Disk Persistence Tests — `runtime/tests/engine/rhi/test_pso_disk_cache.cpp`

```
  - Save empty cache → load → no crash, 0 entries
  - Save with entries → load → entries present (header valid)
  - Load with wrong magic → discard, start fresh
  - Load with wrong version → discard, start fresh
  - Load with wrong backendId → discard, start fresh
  - Load truncated file → discard, start fresh
  - File missing → start fresh (no error)
```

### 5.3 Backend-Specific Smoke Tests

```
DX12:
  - CreateGraphicsPipeline → ID3D12PipelineLibrary1::StorePipeline called
  - Second identical CreateGraphicsPipeline → LoadGraphicsPipeline returns cached

Vulkan:
  - VkPipelineCache created on init with loaded data
  - vkGetPipelineCacheData returns non-empty after pipeline creation
```

## 6. Dependencies and Blockers

| Dependency | Status | Location | Notes |
|------------|--------|----------|-------|
| xxHash | Vendored | `runtime/thirdparty/xxhash/` | Use `XXH3_64bits()` for key computation |
| StatCounter | Done | `runtime/engine/profiling/profiler.h` | `UZ_DECLARE_STAT` / `UZ_DEFINE_STAT` macros |
| FnvHash | Done | `runtime/engine/containers/fnv_hash.h` | NOT used for PSO keys (too small for 100+ byte payloads) |
| ShaderCache | Done | `runtime/engine/shader/shader_cache.h` | Pattern to follow for disk cache layout |
| Permanent arena | Needed | TBD | For open-addressing array allocation. Fallback: `new[]` once at init |
| DX12 `ID3D12PipelineLibrary1` | Available | Windows SDK | Requires `D3D_FEATURE_LEVEL_12_0` (already required) |
| Vulkan `VkPipelineCache` | Available | Vulkan 1.0 | No extra extensions needed |
| Metal `MTLBinaryArchive` | Available | macOS 11+ | Check availability at runtime |

**No blockers.** All dependencies are available. Implementation can start immediately.

## 7. Implementation Order

1. **`pso_cache.h` + `pso_cache.cpp`** — Key computation + in-memory cache (no disk, no backend changes)
2. **Unit tests for key + cache** — Validate correctness before integration
3. **Integrate into `PipelineLibrary`** — Route existing `CreateGraphicsPipeline` calls through cache
4. **Add stat counters** — Hits/misses/evictions
5. **DX12 disk cache** — `ID3D12PipelineLibrary1` integration
6. **Vulkan disk cache** — `VkPipelineCache` integration
7. **Metal disk cache** — `MTLBinaryArchive` integration (if Metal backend exists)
8. **Warmup/precache API** — Scene-load precaching
9. **Disk persistence tests** — Round-trip save/load validation

Steps 1-4 are the MVP. Steps 5-9 are incremental improvements.
