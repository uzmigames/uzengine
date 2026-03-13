# Core Module Reference

Core systems shared by all engine subsystems.

> **Guide**: [Architecture](../../guides/architecture.md)

## Logging (`runtime/engine/logging/log.h`)

### LogLevel

```cpp
enum class LogLevel : uint8_t {
    Trace   = 0,  // Very detailed (stripped in shipping)
    Debug   = 1,  // Debug output (stripped in shipping)
    Info    = 2,  // Informational messages
    Warning = 3,  // Non-fatal warnings
    Error   = 4,  // Recoverable errors
    Fatal   = 5,  // Calls std::abort after logging
};
```

### LogCategory

```cpp
struct LogCategory {
    const char* name;         // e.g., "RHI", "Scene"
    LogLevel compileTimeMin;  // Messages below this are compiled out
};
```

### Functions

| Function | Description |
|----------|-------------|
| `LogInit(path)` | Initialize logging (console + rotating file). Safe to call multiple times. |
| `LogShutdown()` | Flush and close all loggers. |

### Macros

| Macro | Description |
|-------|-------------|
| `UZ_LOG(Category, Level, fmt, ...)` | Log a message to the named category |
| `UZ_DECLARE_LOG_CATEGORY(Name, MinLevel)` | Declare category in header |
| `UZ_DEFINE_LOG_CATEGORY(Name, MinLevel)` | Define category in one .cpp |

Built-in categories: Core, RHI, Renderer, Scene, Physics, Audio, Input, Platform, Memory, IO, Config, Window, Engine.

---

## Assertions (`runtime/engine/assertions/engine_assert.h`)

| Macro | On Failure | Shipping? |
|-------|-----------|-----------|
| `UZ_CHECK(expr)` | Log + stack trace + abort | Active |
| `UZ_ENSURE(expr)` | Log + stack trace + continue | Stripped |
| `UZ_VERIFY(expr)` | Log + stack trace + abort | Active |

All macros capture stack traces via `CaptureStackBackTrace` and trigger debugger breaks when attached.

---

## Config (`runtime/engine/config/config.h`)

### Config

```cpp
class Config {
    bool LoadFromFile(const std::string& path);   // Load .uzproject JSON
    void ApplyCLIOverrides(int argc, const char* const* argv);

    std::string GetString(const std::string& key, const std::string& def = "");
    int GetInt(const std::string& key, int def = 0);
    float GetFloat(const std::string& key, float def = 0.0f);
    bool GetBool(const std::string& key, bool def = false);
};
```

Supports dotted-path resolution: `render.rhi`, `render.shader_model`.
CLI overrides use `--key=value` format.

---

## Path Resolution (`runtime/engine/paths/paths.h`)

Engine resource paths are resolved relative to the executable directory.

> **Full reference**: [Engine Paths](paths.md)

### Functions

| Function | Description |
|----------|-------------|
| `GetExeDir()` | Returns the directory containing the running executable |
| `ResolveEngineResource(path)` | Resolves a relative path against the exe directory |

```cpp
// Resolve DLL next to exe
auto dxcPath = uz::ResolveEngineResource("dxcompiler.dll");
// → "C:/build/build/bin/dxcompiler.dll"

// Absolute paths pass through unchanged
auto abs = uz::ResolveEngineResource("C:/custom/path.dll");
// → "C:/custom/path.dll"
```

Result is cached after first call. Platform-native API is used:
- Windows: `GetModuleFileNameW(NULL, ...)`
- Linux: `readlink("/proc/self/exe", ...)`
- macOS: `_NSGetExecutablePath(...)`

---

## Profiling (`runtime/engine/profiling/profiler.h`)

### Macros

| Macro | Description |
|-------|-------------|
| `UZ_PROFILE_SCOPE(name)` | RAII scope marker for Tracy |
| `UZ_STAT_INC(group, name)` | Increment atomic counter |
| `UZ_STAT_ADD(group, name, value)` | Add to atomic counter |
| `UZ_STAT_SET(group, name, value)` | Set atomic counter |

### StatCounter

Atomic int64 counters organized by stat group (Core, Memory, Renderer, Physics, Audio, IO).

---

## Memory (`core/memory/`)

### Ownership Types (`ownership.h`)

| Type | Semantics |
|------|-----------|
| `Own<T>` | Exclusive ownership (non-copyable, movable) |
| `Ref<T>` | Shared immutable borrow (debug borrow checking) |
| `RefMut<T>` | Exclusive mutable borrow (debug borrow checking) |

### Allocators (`allocators.h`)

| Class | Pattern | Alloc | Free |
|-------|---------|-------|------|
| `LinearAllocator` | Bump pointer | O(1) | Reset only |
| `PoolAllocator` | Fixed-size blocks | O(1) | O(1) |
| `FrameAllocator` | Linear + per-frame reset | O(1) | NewFrame() |

All allocators: named, budget-tracked, report `Used()` / `Remaining()` / `GetSize()`.

> **Guide**: [Memory & Allocators](../../guides/memory/overview.md)

---

## Random (`core/random/random.h`)

PCG-based pseudo-random number generator (XSH-RR variant) with deterministic seeding.
Passes the BigCrush test suite. Suitable for procedural generation, physics randomization, and sampling.
Not suitable for cryptography.

### PCGRandom

| Method | Description |
|--------|-------------|
| `PCGRandom(seed)` | Construct with a single 64-bit seed for a deterministic sequence |
| `PCGRandom(seed, seq)` | Construct with seed + stream identifier for independent parallel streams |
| `Seed(seed)` | Re-seed the generator in place |
| `Next()` | Next uniformly distributed `uint32_t` in `[0, 2^32)` |
| `Float()` | Uniform `float` in `[0.0, 1.0)` |
| `Double()` | Uniform `double` in `[0.0, 1.0)` |
| `Range(min, max)` | Uniform `float` in `[min, max)` |
| `Int(min, max)` | Uniform `int32_t` in `[min, max]` inclusive |
| `UInt(max)` | Uniform `uint32_t` in `[0, max)` |
| `Bool()` | Random boolean (50/50 coin flip) |
| `UnitSphere(x, y, z)` | Random unit vector on the sphere surface (Marsaglia method) |
| `UnitDisk(x, y)` | Random point inside the unit disk (rejection sampling) |

```cpp
PCGRandom rng(42);
float speed  = rng.Range(5.0f, 15.0f);  // [5, 15)
int   damage = rng.Int(10, 25);          // [10, 25]
uint32_t idx = rng.UInt(array.size());  // valid index
```

Thread-safe only when each thread owns its own `PCGRandom` instance. Use the two-argument
constructor to create independent streams: same seed, different `seq` values.

```cpp
// Independent streams per thread
PCGRandom thread0_rng(master_seed, 0);
PCGRandom thread1_rng(master_seed, 1);
```
