# Spec: Async Streaming & Dynamic Loading

> **Status**: Planned (Phase 3)
> **Subsystem**: Asset Streaming, Memory Management
> **Dependencies**: Asset Pipeline, Memory System, Coroutine System
> **Related Docs**: [asset-pipeline.md](asset-pipeline.md), [memory-system.md](../engine/memory-system.md), [lua-scripting-guide.md](../guides/lua-scripting-guide.md)

---

## Overview

The Async Streaming system provides non-blocking asset loading with memory budgeting, supporting:
- **Large open worlds** — Load level chunks asynchronously without frame stutters
- **Memory-limited platforms** — Streaming budgets (VRAM, RAM) with LRU eviction
- **Progressive loading** — Placeholder resources while real assets load in background
- **Streaming priorities** — Distance-based LOD, viewport coverage, explicit boost
- **Coroutine integration** — `ctx.coroutine.wait_until(handle)` for Lua scripts to pause until asset ready
- **Console debugging** — IPC overlay shows in-flight requests, bandwidth, memory usage

This is essential for **AI-driven development** — the system is observable, debuggable, and integrates with the Lua scripting environment so AI can understand loading patterns.

---

## Architecture

```
AssetRegistry (central)
│
├── AssetHandle<T>
│   ├── state: Pending → Loading → Ready | Error
│   ├── get() → T* (nullptr if not ready)
│   ├── wait() → bool (blocks until ready)
│   ├── cancel() → void (stop in-flight load)
│
├── StreamingIOThread (background thread pool)
│   ├── LoadRequest { handle_id, priority, asset_path }
│   ├── Request queue (sorted by priority)
│   ├── Process loop: pull request → load file → decode → notify
│
├── StreamingMemoryBudget
│   ├── VRAM limit (e.g., 1.5GB on console)
│   ├── RAM limit (e.g., 2GB)
│   ├── Current usage tracking
│   ├── LRU eviction: if over budget, evict least-recently-used
│   ├── Priority-weighted eviction: low-priority unused assets evicted first
│
└── StreamingManager (singleton)
    ├── load_async(path, priority) → AssetHandle<T>
    ├── preload(path[], priority) → void
    ├── set_memory_budget(vram, ram) → void
    ├── flush() → void (wait for all pending)
    └── get_debug_info() → StreamingDebugInfo
```

---

## AssetHandle<T> API

Handles are opaque, moveable (not copyable) references to loading assets.

```cpp
// In engine.hpp

namespace uz {

enum class AssetState {
    Pending = 0,    // Created, not yet queued
    Loading = 1,    // In-flight load request
    Ready = 2,      // Successfully loaded, get() returns valid pointer
    Error = 3,      // Load failed (file not found, decode error, OOM)
};

template <typename T>
class AssetHandle {
public:
    // Non-copyable, moveable
    AssetHandle() = delete;
    AssetHandle(const AssetHandle&) = delete;
    AssetHandle& operator=(const AssetHandle&) = delete;

    AssetHandle(AssetHandle&& other) noexcept = default;
    AssetHandle& operator=(AssetHandle&& other) noexcept = default;

    // Check state without blocking
    [[nodiscard]] AssetState state() const noexcept;

    // Get pointer if ready, nullptr otherwise (non-blocking)
    [[nodiscard]] T* get() const noexcept;

    // Block until ready or error
    // Returns true if ready, false if error
    [[nodiscard]] bool wait() const;

    // Block with timeout (milliseconds)
    // Returns true if ready before timeout, false if timeout or error
    [[nodiscard]] bool wait(uint32_t timeout_ms) const;

    // Cancel in-flight load, return true if cancelled, false if already ready/error
    [[nodiscard]] bool cancel() noexcept;

    // Get error message if state() == AssetState::Error
    [[nodiscard]] std::string_view error() const noexcept;

    // Get asset file path
    [[nodiscard]] std::string_view path() const noexcept;

    // Size in bytes if loaded, 0 otherwise
    [[nodiscard]] size_t size_bytes() const noexcept;

    // Unique handle ID for debugging
    [[nodiscard]] uint64_t id() const noexcept;

    // Check if handle is still valid (not destroyed after timeout)
    [[nodiscard]] bool is_valid() const noexcept;
};

} // namespace uz
```

### Lua Binding

```lua
-- ctx.assets (from asset-pipeline.md) extended with async

local handle = ctx.assets:load_async("res://meshes/character.gltf", "high")
-- priority: "high" | "normal" | "low" (string)

-- Non-blocking check
if handle:is_ready() then
    local mesh = handle:get()
    -- use mesh
end

-- Wait with timeout
if handle:wait(5000) then  -- 5 second timeout
    local mesh = handle:get()
else
    print("Load timeout or error: " .. handle:error())
end

-- Coroutine integration
ctx.coroutine.wait_until(handle)  -- Lua pauses, returns when ready
local mesh = handle:get()
```

---

## IO Thread Pool & Request Queue

The streaming system runs a configurable number of background threads that decode assets without blocking the render/game thread.

```cpp
struct StreamingRequest {
    uint64_t handle_id;           // Unique ID for this load request
    std::string path;             // "res://meshes/foo.gltf"
    AssetType type;               // Mesh, Texture, Audio, etc.
    uint8_t priority;             // 0 (low) to 255 (high)
    uint64_t requested_at_ms;     // Time request was queued
    AssetHandle<void>* result;    // Where to store result (set by loader)
};

class StreamingIOThread {
public:
    explicit StreamingIOThread(size_t thread_count = 4);
    ~StreamingIOThread();

    // Queue a load request
    void queue_request(const StreamingRequest& req);

    // Number of in-flight requests
    [[nodiscard]] size_t pending_count() const;

    // Block until all queued requests complete
    void flush();

    // Graceful shutdown: cancel all pending, return immediately
    void shutdown();
};
```

**Thread Pool Behavior:**
- Default: 4 background threads on desktop, 2 on console
- Threads pull requests from a priority queue (sorted by priority)
- When priority is equal, FIFO (earliest request first)
- Load decode happens in background thread; result pointer is atomic-stored
- Main thread polls handle state or waits on condition variable

**Deadlock Prevention:**
- Background threads never call game code or access ECS
- No locks held across asset decode (only queue insertion/removal)
- Game thread never blocks indefinitely (wait() has timeout support)

---

## Placeholder Resources

While an asset is loading, the system provides temporary standin resources to avoid visual pops or crashes.

```cpp
class PlaceholderProvider {
public:
    // Called when AssetHandle<Mesh> becomes Loading
    // Returns low-detail placeholder mesh immediately
    [[nodiscard]] static Mesh* placeholder_mesh();

    // Called when AssetHandle<Texture> becomes Loading
    // Returns magenta checkerboard texture (256x256)
    [[nodiscard]] static Texture* placeholder_texture();

    // Called when AssetHandle<Texture> errors
    // Returns error pattern texture
    [[nodiscard]] static Texture* error_texture();

    // Default white texture for null references
    [[nodiscard]] static Texture* white_texture();
};
```

**Default Placeholders:**
- **Mesh**: Sphere (32 triangles, no bones)
- **Texture**: Magenta checkerboard (indicates missing asset)
- **Audio**: Silence (1 second)
- **Material**: White diffuse (unlit)

Shaders must handle sampling from placeholders gracefully. In practice, a scene preview or play session using placeholders should be *visually distinct* so developers know loading is in progress.

---

## Streaming Memory Budget

The engine enforces per-platform memory limits and evicts unused assets automatically.

```cpp
struct MemoryBudget {
    size_t vram_limit_bytes;      // e.g., 1.5 GB on console
    size_t ram_limit_bytes;       // e.g., 2.0 GB
    size_t vram_current_bytes;    // Current usage
    size_t ram_current_bytes;
};

class StreamingMemoryBudget {
public:
    void set_limits(size_t vram, size_t ram);

    [[nodiscard]] MemoryBudget current() const;

    // Try to allocate memory
    // Returns true if enough free; false if over budget and eviction couldn't free enough
    [[nodiscard]] bool try_allocate(size_t vram, size_t ram);

    // Eviction policy: LRU with priority weighting
    // 1. Find assets with state Ready that are not currently referenced
    // 2. Sort by (priority, last_access_time) ascending
    // 3. Evict until budget pressure is relieved
    void evict_unused(size_t target_free_bytes);

    // Per-asset priority for eviction (1-255)
    // Low priority (1): heavy textures, optional LODs
    // High priority (255): critical gameplay assets (player mesh, HUD)
    void set_asset_priority(uint64_t handle_id, uint8_t priority);
};
```

**Eviction Strategy:**
1. **LRU (Least Recently Used):** Tracks time of last `get()` call on handle
2. **Priority-weighted:** Assets with `priority = 1` evicted before `priority = 10`
3. **Referenced assets:** Never evicted while handle is alive
4. **Persistence:** Once evicted, handle transitions to `Pending` and can be reloaded

**Example Tuning (PC):**
```toml
[streaming]
vram_limit_mb = 1500
ram_limit_mb = 2000
eviction_policy = "lru-priority"
```

**Example Tuning (PlayStation 5):**
```toml
[streaming]
vram_limit_mb = 1500   # Tight GPU VRAM budget
ram_limit_mb = 1000    # Tight system RAM
eviction_policy = "lru-priority"
```

---

## Level Streaming

For open-world or large linear scenes, partition `.uzscene` files into spatial chunks and stream them dynamically.

```
Level Structure:
├── level1.uzscene (master, static)
│   ├── Streaming volume (cuboid) → refs chunk_a.uzscene
│   ├── Streaming volume (cuboid) → refs chunk_b.uzscene
│   └── Streaming volume (cuboid) → refs chunk_c.uzscene
│
├── chunk_a.uzscene (subscene reference)
│   ├── All nodes under "ChunkA" root
│   └── Physics, audio, scripts included
│
├── chunk_b.uzscene
└── chunk_c.uzscene
```

**In Editor (Unity-like):**
1. Create master scene, place streaming volume nodes
2. Each volume has a "subscene_path" property pointing to chunk file
3. Volumes are trigger-based (AABB collision)

**At Runtime:**
- Engine tracks player position in main scene
- When player enters streaming volume, the referenced chunk begins loading
- When player exits, unloads (with hysteresis to avoid thrashing)
- Overlapping chunks load in priority order (distance to player)

**Lua Script Example:**
```lua
-- Subscene handle in node properties or script
local chunk_handle = self.subscene_handle

-- Optionally wait for load
if not chunk_handle:is_ready() then
    print("Waiting for chunk to load...")
    ctx.coroutine.wait_until(chunk_handle)
end

-- Access loaded subscene root
local chunk_root = chunk_handle:get()
if chunk_root then
    chunk_root.active = true  -- Make visible
end
```

---

## Streaming Priorities

Assets are prioritized by multiple factors. The loading system sorts requests dynamically.

```cpp
enum class StreamingPriority : uint8_t {
    Low = 0,
    Normal = 128,
    High = 255,
};

// Priority boost factors (summed before sorting)
// 1. Distance to camera (inverse: closer = higher priority)
// 2. Viewport coverage (larger silhouette = higher priority)
// 3. Explicit boost (set by script or editor)

class StreamingPriorityCalculator {
public:
    // Calculate priority based on distance from camera
    [[nodiscard]] static uint8_t by_distance(
        const Vec3& asset_position,
        const Vec3& camera_position,
        float max_distance = 100.0f
    );

    // Boost priority for critical assets
    [[nodiscard]] static uint8_t with_boost(
        uint8_t base_priority,
        uint8_t boost_amount = 50
    );
};
```

**Priority Calculation (Dynamic):**
1. Base priority: High/Normal/Low
2. Distance boost: `priority += clamp(1.0 - distance/max_distance, 0, 1) * 100`
3. Explicit boost: `priority += manual_boost` (script sets this for crucial assets)
4. Result clamped to 0-255

**Example Script:**
```lua
-- Load two assets, boost one by distance
local player_pos = self.node.position

local weapon_mesh = ctx.assets:load_async("res://meshes/sword.gltf", "high")
local distant_building = ctx.assets:load_async(
    "res://meshes/building.gltf",
    "low"  -- low priority, but distance will boost it
)

-- Explicitly boost building if it's visible
if is_in_viewport(distant_building) then
    distant_building:boost_priority(100)
end
```

---

## Coroutine Integration

Lua coroutines pause script execution until an asset is ready. This integrates with the timer system.

```cpp
// In LuaScriptingEngine

// New function: ctx.coroutine.wait_until(handle)
// Called from Lua: yields the current coroutine
// Resumed when handle transitions to Ready or Error

class LuaCoroutineManager {
public:
    // Called from Lua
    void wait_until(AssetHandle<void>* handle);

    // Update called once per frame (after streaming poll)
    // Resumes coroutines whose handles are now ready
    void update();

    [[nodiscard]] size_t active_coroutines() const;
};
```

**Lua Example:**

```lua
function on_start()
    -- Start a coroutine that waits for assets
    ctx.coroutine.launch(load_and_play_animation)
end

function load_and_play_animation()
    print("Loading character mesh...")
    local mesh_handle = ctx.assets:load_async("res://meshes/character.gltf", "high")

    -- Coroutine pauses here until mesh is ready
    ctx.coroutine.wait_until(mesh_handle)

    if not mesh_handle:is_ready() then
        print("Failed to load mesh: " .. mesh_handle:error())
        return
    end

    local mesh = mesh_handle:get()
    print("Mesh loaded, applying to node...")
    self.node.mesh = mesh

    -- Continue with animation
    ctx.coroutine.schedule_after(1000, function()
        print("Animation done")
    end)
end
```

---

## Lua API

Extended from asset-pipeline.md with async support.

```lua
-- Load asset asynchronously
local handle = ctx.assets:load_async(path, priority)
-- path: "res://meshes/foo.gltf" | "res://textures/bar.png" | "res://audio/bloop.wav"
-- priority: "high" | "normal" | "low" (string)
-- returns: AssetHandle<T> (type inferred from extension)

-- Check if ready (non-blocking)
if handle:is_ready() then
    local asset = handle:get()
end

-- Wait with optional timeout
if handle:wait(5000) then
    local asset = handle:get()
else
    print("Timeout or error: " .. handle:error())
end

-- Coroutine-friendly wait
ctx.coroutine.wait_until(handle)
local asset = handle:get()

-- Get state enum
local state = handle:state()
-- "pending" | "loading" | "ready" | "error"

-- Get error message if failed
if handle:state() == "error" then
    print("Load failed: " .. handle:error())
end

-- Cancel in-flight load
if handle:cancel() then
    print("Cancelled")
else
    print("Already loaded")
end

-- Get asset size
local bytes = handle:size_bytes()

-- Get handle ID for debugging
local id = handle:id()

-- Preload multiple assets
ctx.assets:preload({
    "res://meshes/a.gltf",
    "res://meshes/b.gltf",
    "res://textures/c.png",
}, "normal")

-- Set memory budget (called from game.dll init)
ctx.streaming:set_memory_budget(1500, 2000)  -- MB VRAM, MB RAM

-- Get current memory usage
local usage = ctx.streaming:memory_usage()
-- { vram_mb = 800, ram_mb = 1200, vram_limit_mb = 1500, ram_limit_mb = 2000 }

-- Get streaming stats (for overlay)
local stats = ctx.streaming:debug_info()
-- {
--   pending_requests = 5,
--   in_progress_requests = 3,
--   completed_requests = 42,
--   average_load_time_ms = 150,
--   bandwidth_mbps = 45.2,
--   evicted_assets = 3,
-- }
```

---

## Debug Features

### Streaming Overlay

**IPC Command** (from editor):
```
streaming.debug_overlay_enabled = true
```

Shows on-screen (top-left corner):
```
Streaming Debug Overlay
─────────────────────────
Pending: 2 | Loading: 3 | Ready: 148
VRAM: 800MB / 1500MB (53%)
RAM:  1200MB / 2000MB (60%)

Top 5 In-Flight:
1. res://meshes/character.gltf (est 500ms)
2. res://textures/terrain_albedo.png (est 200ms)
3. res://meshes/building_a.gltf (est 800ms)
...

Recent Evictions (LRU):
- res://meshes/tree_04.gltf (priority 10)
- res://textures/sky_night.png (priority 5)
```

### IPC Debug Commands

```
# Get current streaming state
streaming.get_status → { pending, loading, ready, errors, memory_usage }

# Set memory budget
streaming.set_memory_budget {"vram_mb": 1500, "ram_mb": 2000}

# Simulate memory pressure for testing
streaming.simulate_memory_pressure 0.9  # 90% usage

# Get all in-flight requests
streaming.get_requests → [ {path, priority, started_ms, estimated_remaining_ms}, ... ]

# Cancel a specific request by handle ID
streaming.cancel_handle 12345

# Force garbage collection of unused assets
streaming.garbage_collect

# Enable/disable bandwidth throttling
streaming.set_bandwidth_limit 50  # MB/s, useful for testing slow drives
```

### Per-Asset Debug Info

```lua
-- Get handle metadata for debugging
local info = handle:debug_info()
-- {
--   id = 12345,
--   path = "res://meshes/character.gltf",
--   state = "loading",
--   priority = 255,
--   started_ms = 1709424000000,
--   size_bytes = 5242880,
--   error = nil,
-- }
```

---

## Performance Considerations

### Bandwidth Management

- **Target I/O bandwidth**: 50-100 MB/s on HDD, 200+ MB/s on SSD
- **Thread pool depth**: 4 threads on desktop, 2 on console (I/O bound, not CPU bound)
- **File decompression**: Always use `.uzpak` (streaming + compression) for large assets
- **Texture streaming**: BC7 compression reduces disk I/O by ~6x vs uncompressed

### Memory Efficiency

1. **Immediate unload of evicted assets**: Keep freed VRAM/RAM available for new requests
2. **No placeholder duplication**: Placeholders are shared singletons, not per-handle
3. **Memory pooling**: Streaming thread reuses buffers for file I/O

### Frame Impact

- **Game thread cost**: Near-zero — only atomic flag checks and optional coroutine resumption
- **Render thread cost**: Near-zero — no interaction with DX12 submission

---

## Test Coverage

### Unit Tests

```cpp
// tests/async_streaming_test.cpp

TEST(AssetHandleTest, InitialStatePending) {
    AssetHandle<Mesh> handle = registry.create_handle<Mesh>("res://test.gltf");
    EXPECT_EQ(handle.state(), AssetState::Pending);
    EXPECT_FALSE(handle.is_ready());
}

TEST(AssetHandleTest, SuccessfulLoad) {
    auto handle = registry.load_async("res://meshes/sphere.gltf", AssetPriority::Normal);
    ASSERT_TRUE(handle.wait(5000));
    EXPECT_EQ(handle.state(), AssetState::Ready);
    EXPECT_NE(handle.get(), nullptr);
}

TEST(AssetHandleTest, TimeoutFail) {
    auto handle = registry.load_async("res://huge_mesh.gltf", AssetPriority::Low);
    EXPECT_FALSE(handle.wait(10));  // 10ms timeout (mesh load > 10ms)
    EXPECT_EQ(handle.state(), AssetState::Loading);
}

TEST(AssetHandleTest, CancelInFlight) {
    auto handle = registry.load_async("res://slow.gltf", AssetPriority::Low);
    EXPECT_TRUE(handle.cancel());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_NE(handle.state(), AssetState::Ready);
}

TEST(MemoryBudgetTest, EvictionLRU) {
    StreamingMemoryBudget budget;
    budget.set_limits(1024 * 1024, 2048 * 1024);  // 1MB VRAM, 2MB RAM

    auto handle1 = registry.load_async("res://texture_a.png", AssetPriority::Normal);
    auto handle2 = registry.load_async("res://texture_b.png", AssetPriority::Normal);

    handle1.wait();
    handle2.wait();

    // Simulate pressure: load a large mesh
    // Budget should evict least-recently-used texture
    auto handle3 = registry.load_async("res://large_mesh.gltf", AssetPriority::High);
    handle3.wait();

    // handle1 or handle2 should be evicted (whichever was accessed first)
}

TEST(PriorityQueueTest, SortByPriority) {
    StreamingIOThread io(1);  // Single thread for deterministic ordering

    io.queue_request({0, "res://low.gltf", AssetType::Mesh, 0});
    io.queue_request({1, "res://high.gltf", AssetType::Mesh, 255});
    io.queue_request({2, "res://med.gltf", AssetType::Mesh, 128});

    // High priority should load first
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_TRUE(registry.get_handle(1)->is_ready());
}

TEST(CoroutineIntegrationTest, WaitUntilReady) {
    LuaScriptingEngine lua;

    std::string script = R"(
        local handle = ctx.assets:load_async("res://test.gltf", "high")
        local before = os.clock()
        ctx.coroutine.wait_until(handle)
        local after = os.clock()
        assert(handle:is_ready())
        return (after - before) > 0
    )";

    auto result = lua.eval(script);
    EXPECT_TRUE(result.as_bool());
}
```

### Integration Tests

- Load scene with 10 subscene chunks, verify all load within 30s
- Memory pressure test: load assets beyond VRAM budget, verify eviction + graceful fallback
- Coroutine interrupt test: cancel asset mid-load, verify coroutine resumes with error

---

## Phase Roadmap

**Phase 3 (Initial):**
- AssetHandle<T> API
- Streaming IO thread pool
- Priority queue
- Placeholder resources
- Memory budgets & LRU eviction
- Lua integration (load_async, wait_until)

**Phase 4 (Enhancement):**
- Level streaming with subscene integration
- Bandwidth throttling & async I/O on console
- Virtual memory support (streaming to disk)
- Streaming profile recording for offline analysis

---

## Related Docs

- [asset-pipeline.md](asset-pipeline.md) — Synchronous asset loading (baseline)
- [memory-system.md](../engine/memory-system.md) — Arena allocators, Handle<T> lifetime
- [lua-scripting-guide.md](../guides/lua-scripting-guide.md) — Coroutine system, ctx.coroutine API
- [asset-pipeline-guide.md](../guides/asset-pipeline-guide.md) — Practical asset usage examples
