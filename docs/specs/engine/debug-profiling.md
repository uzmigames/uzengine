# Debug, Test & Profiling System

> **Status**: Planning
> **Priority**: High — essential for engine development and game project creation

---

## Overview

A comprehensive debug/test/profiling system is fundamental to engine development. Without it, performance bottlenecks are invisible, bugs survive until they cause hard-to-diagnose failures, and regression detection requires manual testing.

The system covers four orthogonal concerns:
1. **Runtime asserts** — catch violated invariants immediately at the source
2. **CPU/GPU profiling** — locate performance bottlenecks (Tracy + PIX)
3. **Memory debugging** — detect leaks, overflows, use-after-free (ASAN + arena tracking)
4. **Unit testing** — regression safety net for engine subsystems (doctest + CTest)

---

## 1. Assert System (`src/core/assert.h`)

```cpp
// Debug: evaluates expression, prints file:line:function, triggers __debugbreak()
// Release: strips to nothing (ASSERT) or evaluates only (VERIFY)

ASSERT(ptr != nullptr);           // fatal in Debug, stripped in Release
VERIFY(Init());                   // evaluated in Release, fatal only in Debug
ASSERT_MSG(x > 0, "x must be positive, got %d", x);  // formatted message
```

**Tiers**:

| Macro | Debug | Release | Use Case |
|-------|-------|---------|----------|
| `ASSERT(expr)` | break + log | stripped | Internal invariants |
| `ASSERT_MSG(expr, fmt, ...)` | break + formatted log | stripped | Complex invariants |
| `VERIFY(expr)` | break + log | evaluates, no break | Side-effect expressions |
| `ENSURE(expr)` | break + log | log + continue | Recoverable errors |

---

## 2. CPU Profiler: Tracy (`src/core/profiler.h`)

**Library**: [Tracy Profiler](https://github.com/wolfpld/tracy) — header-only, ~50ns per zone overhead, real-time nanosecond resolution.

### Usage Pattern

```cpp
#include "profiler.h"  // wraps Tracy macros

void Renderer::render_frame() {
    UZ_PROFILE_FUNCTION();          // auto-names zone "Renderer::render_frame"
    FrameMark;                       // signals end of frame to Tracy

    {
        UZ_PROFILE_ZONE("Shadow Pass");
        shadow_system_.render(cmd_list_);
    }
    {
        UZ_PROFILE_ZONE("G-Buffer Pass");
        gbuffer_.render(cmd_list_);
    }
}
```

### Macro Wrappers

```cpp
// src/core/profiler.h
#if UZ_PROFILING
    #include <tracy/Tracy.hpp>
    #define UZ_PROFILE_FUNCTION()          ZoneScoped
    #define UZ_PROFILE_ZONE(name)          ZoneScopedN(name)
    #define UZ_PROFILE_FRAME_MARK()        FrameMark
    #define UZ_PROFILE_VALUE(name, val)    TracyPlot(name, val)
    #define UZ_PROFILE_MESSAGE(msg)        TracyMessage(msg, sizeof(msg))
#else
    #define UZ_PROFILE_FUNCTION()
    #define UZ_PROFILE_ZONE(name)
    #define UZ_PROFILE_FRAME_MARK()
    #define UZ_PROFILE_VALUE(name, val)
    #define UZ_PROFILE_MESSAGE(msg)
#endif
```

### GPU Zones (DX12)

```cpp
#include <tracy/TracyD3D12.hpp>

// Once at startup:
tracy::D3D12QueueCtx* tracy_ctx = TracyD3D12Context(device_, queue_);

// Per frame:
{
    TracyD3D12Zone(tracy_ctx, cmd_list_, "Lighting Pass");
    // draw calls...
}
TracyD3D12Collect(tracy_ctx);  // at end of frame
```

### CMake

```cmake
option(UZ_PROFILING "Enable Tracy profiler" OFF)
if(UZ_PROFILING)
    find_package(Tracy CONFIG REQUIRED)
    target_link_libraries(UzEngine PRIVATE Tracy::TracyClient)
    target_compile_definitions(UzEngine PRIVATE UZ_PROFILING=1 TRACY_ENABLE=1)
endif()
```

---

## 3. Frame Debugger: PIX for Windows

**Tool**: [PIX on Windows](https://devblogs.microsoft.com/pix/) — free, official Microsoft DX12 frame debugger.

### In-code PIX Events

```cpp
#include <pix3.h>

void Renderer::render_shadow_pass(ID3D12GraphicsCommandList* cmd) {
    PIXBeginEvent(cmd, PIX_COLOR_DEFAULT, "Shadow Pass");
    // draw calls
    PIXEndEvent(cmd);
}
```

### IPC Trigger for PIX Capture

```json
{ "fn": "engine.capture_frame", "args": {} }
```

```cpp
// IPC handler:
ipc_registry_.register("engine.capture_frame", [](auto&) {
    PIXCaptureNextFrame();  // triggers a GPU capture on next frame
    return json{{"ok", true}};
});
```

### RenderDoc (Secondary)

For cross-platform work or shader debugging on non-Nvidia hardware. IPC command `engine.capture_renderdoc` triggers `RenderDoc::StartFrameCapture()`.

---

## 4. In-Engine Debug Overlay: ImGui

**Library**: [Dear ImGui](https://github.com/ocornut/imgui) with `imgui_impl_dx12.cpp` backend.

Renders to the DX12 backbuffer **before** JPEG encoding — appears live in the VSCode viewport.

### Panels

```cpp
// src/debug/debug_overlay.cpp
void DebugOverlay::render(FrameStats& stats) {
    if (!enabled_) return;

    ImGui::SetNextWindowPos({10, 10});
    if (ImGui::Begin("Engine##debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("FPS: %.1f  (%.2f ms)", stats.fps, stats.frame_ms);
        ImGui::Text("GPU: %.2f ms", stats.gpu_ms);
        ImGui::PlotLines("##ft", stats.frame_times, 128, 0, nullptr, 0, 33.3f, {200, 40});
        ImGui::Separator();
        ImGui::Text("Draw calls: %u", stats.draw_calls);
        ImGui::Text("Triangles: %uk", stats.triangles / 1000);
        ImGui::Separator();
        ImGui::Text("Arena/frame: %.1f KB", stats.frame_arena_bytes / 1024.0f);
        ImGui::Text("Arena/level: %.1f MB", stats.level_arena_bytes / 1024.0f / 1024.0f);
        ImGui::Separator();
        ImGui::Text("JPEG: %zu KB  (%.1f ms)", stats.jpeg_bytes / 1024, stats.jpeg_ms);
        ImGui::Text("WS clients: %d", stats.ws_clients);
    }
    ImGui::End();
}
```

### IPC Toggle

```json
{ "fn": "engine.debugOverlay", "args": { "enabled": true } }
```

---

## 5. Memory Debugging

### MSVC AddressSanitizer

```cmake
option(UZ_ASAN "Enable Address Sanitizer" OFF)
if(UZ_ASAN)
    target_compile_options(UzEngine PRIVATE /fsanitize=address)
    target_compile_options(UzEngine_Tests PRIVATE /fsanitize=address)
endif()
```

Detects: buffer overflows, use-after-free, double-free, stack-use-after-return.

### Arena Leak Tracking

```cpp
struct Arena {
#ifdef UZ_DEBUG
    struct AllocRecord { size_t size; const char* file; int line; };
    std::vector<AllocRecord> records_;
    size_t high_water_mark_ = 0;
#endif

    void* alloc(size_t size, const char* file = nullptr, int line = 0) {
        // ... standard arena allocation ...
#ifdef UZ_DEBUG
        records_.push_back({size, file, line});
        high_water_mark_ = std::max(high_water_mark_, used_);
#endif
        return ptr;
    }

    void reset() {
#ifdef UZ_DEBUG
        if (debug_validate_) dump_stats();
        records_.clear();
#endif
        used_ = 0;
    }
};

#define UZ_ALLOC(arena, size) (arena).alloc(size, __FILE__, __LINE__)
```

---

## 6. Unit Testing: doctest + CTest

**Library**: [doctest](https://github.com/doctest/doctest) — single header, fastest compile time of any C++ test framework.

### File Layout

```
tests/
├── CMakeLists.txt
├── main.cpp                  # DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
├── test_arena.cpp
├── test_jpeg_encoder.cpp
├── test_scene_loader.cpp
├── test_ipc_registry.cpp
├── test_ring_buffer.cpp
└── test_asset_pak.cpp
```

### Example Test

```cpp
// tests/test_jpeg_encoder.cpp
#include <doctest/doctest.h>
#include "encoder/jpeg_encoder.h"

TEST_SUITE("JpegEncoder") {
    TEST_CASE("produces valid JPEG SOI marker") {
        JpegEncoder enc;
        std::vector<uint8_t> pixels(1280 * 720 * 4, 128);
        auto result = enc.encode(pixels.data(), 1280, 720, 85);
        REQUIRE(result.size() > 0);
        CHECK(result[0] == 0xFF);
        CHECK(result[1] == 0xD8);  // SOI
    }

    TEST_CASE("quality 1 produces smaller output than quality 95") {
        JpegEncoder enc;
        std::vector<uint8_t> pixels(1280 * 720 * 4, 128);
        auto hi = enc.encode(pixels.data(), 1280, 720, 95);
        auto lo = enc.encode(pixels.data(), 1280, 720, 1);
        CHECK(lo.size() < hi.size());
    }
}
```

### CMake Integration

```cmake
# tests/CMakeLists.txt
enable_testing()

add_executable(UzEngine_Tests
    tests/main.cpp
    tests/test_arena.cpp
    tests/test_jpeg_encoder.cpp
    tests/test_scene_loader.cpp
    tests/test_ipc_registry.cpp
    tests/test_ring_buffer.cpp
    tests/test_asset_pak.cpp
)

target_link_libraries(UzEngine_Tests PRIVATE UzEngine_lib doctest::doctest)

add_test(NAME UzEngineTests COMMAND UzEngine_Tests)
```

### Running Tests

```bash
cmake --build build --config Debug --target UzEngine_Tests
ctest --build-config Debug --output-on-failure
```

---

## 7. IPC Debug Commands

```
engine.debugOverlay(enabled: bool)      toggle ImGui overlay
engine.capture_frame()                  trigger PIX frame capture
engine.capture_renderdoc()              trigger RenderDoc capture
engine.dump_arena_stats()               log all arena high-water marks
engine.set_profiling(enabled: bool)     enable/disable Tracy capture
engine.run_tests()                      run doctest suite, stream results
```

---

## 8. Dependencies

| Library | vcpkg ID | License | Notes |
|---------|----------|---------|-------|
| Tracy Profiler | `tracy` | MIT | Enable with `UZ_PROFILING=1` |
| ImGui | `imgui[dx12-binding]` | MIT | Debug overlay, DX12 backend |
| doctest | `doctest` | MIT | Single header, zero release overhead |
| PIX | `winpixeventruntime` | MIT-like | DX12 event markers only; frame capture is separate PIX app |

---

## 9. Build Configurations

| Config | ASSERT | Tracy | ImGui | ASAN | Tests |
|--------|--------|-------|-------|------|-------|
| Debug | on | off | on | optional | yes |
| Release | off | off | off | off | ci only |
| Profile | off | **on** | on | off | no |
| Shipping | off | off | off | off | no |
