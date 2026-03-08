## 1. C++ Profiling Macros

- [x] 1.1 Create `src/engine/core/profiler.h` defining `UZ_PROFILE_SCOPE(name)` macro: when `TRACY_ENABLE` is defined it expands to `ZoneNamedN(_uz_zone_##__LINE__, name, true)`; when `TRACY_ENABLE` is not defined it expands to nothing (zero-cost no-op)
- [x] 1.2 Define `UZ_PROFILE_FUNCTION()` shorthand macro in `src/engine/core/profiler.h` that passes `__FUNCTION__` as the zone name to `UZ_PROFILE_SCOPE`, enabling single-line function-level instrumentation
- [x] 1.3 Define `UZ_PROFILE_COLOR(name, color)` variant that passes an ARGB color hint to the Tracy zone for visual grouping in the Tracy timeline (e.g. physics zones red, audio zones blue)
- [x] 1.4 Verify all macros compile with zero warnings at `/W4` (MSVC) and `-Wall -Wextra` (Clang/GCC) in both `TRACY_ENABLE` and non-Tracy configurations

## 2. Internal Zone Data Collection

- [x] 2.1 Create `src/engine/core/profiler_ring_buffer.h` implementing a lock-free ring buffer (`ProfileZoneRecord[]`) that stores per-zone data captured each frame: zone name (const char*), thread ID, start timestamp (QPC ticks), end timestamp (QPC ticks), and call count
- [x] 2.2 Implement `ProfilerCollector::BeginZone(name)` and `ProfilerCollector::EndZone(name)` in `src/engine/core/profiler.cpp` that record into the ring buffer using `QueryPerformanceCounter` for timestamps; these functions are called by the Lua bindings (section 3) and the IPC data path (section 4)
- [x] 2.3 Implement `ProfilerCollector::FlushFrame(frame_id)` that marks the ring buffer write boundary for the completed frame and returns a `std::span<const ProfileZoneRecord>` covering the last N frames (N configurable, default 8)

## 3. Lua Profiling API

- [x] 3.1 Create `src/engine/scripting/lua_profiler.cpp` implementing `ctx.profiler:begin(name)` that calls `ProfilerCollector::BeginZone(name)` and simultaneously emits `___tracy_emit_zone_begin_callstack` when `TRACY_ENABLE` is defined
- [x] 3.2 Implement `ctx.profiler:end(name)` in `src/engine/scripting/lua_profiler.cpp` that calls `ProfilerCollector::EndZone(name)` and emits `___tracy_emit_zone_end` for the matching Tracy zone
- [x] 3.3 Register the `profiler` table in the Lua VM's `ctx` global during scripting system initialization in `src/engine/scripting/lua_vm.cpp`
- [x] 3.4 Add `ctx.profiler:begin()`/`ctx.profiler:end()` documentation to `docs/specs/lua-scripting.md` with an example showing how to instrument a custom `find_all_enemies()` function

## 4. IPC Profiler Commands

- [x] 4.1 Add `profiler.get_frame_data` IPC handler in `src/ipc/ipc_handlers.cpp` that reads from `ProfilerCollector::FlushFrame()` and serializes the last N frames as a JSON array: `[{"name": "...", "thread_id": 1, "start_us": 1234, "end_us": 1290, "inclusive_us": 56, "call_count": 1}, ...]`
- [x] 4.2 Accept an optional `frames` parameter in `profiler.get_frame_data` (default 1, max 8) controlling how many frames of data to return; validate the range and return an error JSON on invalid input
- [x] 4.3 Add `profiler.enable` IPC handler that sets a `g_profiler_enabled` atomic bool to `true`; the ring buffer only records data when this flag is set to avoid overhead when the VSCode profiler panel is not open
- [x] 4.4 Add `profiler.disable` IPC handler that sets `g_profiler_enabled` to `false` and flushes the ring buffer; log the enable/disable transition with `UZ_LOG_INFO`

## 5. VSCode Profiler Panel Integration

- [x] 5.1 Create `vscode-uzengine/src/panels/ProfilerPanel.ts` implementing a WebView panel that calls `profiler.enable` on open and `profiler.disable` on close
- [x] 5.2 Poll `profiler.get_frame_data` at 10 Hz when the panel is visible and render a lightweight flame chart in the WebView using inline SVG: each zone is a colored rectangle with width proportional to `inclusive_us`, nested by call depth
- [x] 5.3 Display total frame time, per-zone inclusive time in microseconds, and call count for the most recent frame in a sortable table below the flame chart

## 6. Testing

- [x] 6.1 Create `tests/core/test_profiler.cpp` verifying that `UZ_PROFILE_SCOPE` and `UZ_PROFILE_FUNCTION` macros compile without warnings in both Tracy-enabled and Tracy-disabled configurations (use a CMake test target for each)
- [x] 6.2 Test `ProfilerCollector::BeginZone("test")` followed by `ProfilerCollector::EndZone("test")` produces a `ProfileZoneRecord` with non-zero `start_us` and `end_us >= start_us`
- [x] 6.3 Test `FlushFrame()` returns all zones recorded since the previous flush and clears the ring buffer for the next frame
- [x] 6.4 Test that recording more zones than the ring buffer capacity overwrites the oldest entries without crashing (verify no out-of-bounds access with `valgrind` or AddressSanitizer)
- [x] 6.5 Test concurrent `BeginZone`/`EndZone` calls from 4 threads simultaneously produce the correct number of records with no data corruption (thread IDs must all be distinct)
- [x] 6.6 Test `profiler.get_frame_data` IPC handler returns valid JSON with `name`, `start_us`, `end_us`, `inclusive_us`, and `call_count` keys for each zone
- [x] 6.7 Test `profiler.get_frame_data` with `frames: 0` returns an error; with `frames: 9` returns an error; with `frames: 4` returns data for up to 4 frames
- [x] 6.8 Test `profiler.enable` / `profiler.disable` IPC handlers toggle recording; verify `FlushFrame()` returns zero records when disabled
- [x] 6.9 Test Lua `ctx.profiler:begin("zone")` + `ctx.profiler:end("zone")` creates a matching pair of records in the ring buffer (verifiable via `FlushFrame()`)
- [x] 6.10 Test Lua `ctx.profiler:end("zone")` called without a matching `begin` does not crash and emits `UZ_LOG_WARN` about the mismatched zone name
- [x] 6.11 Run coverage -- all new files in `src/engine/core/` and `src/engine/scripting/` must reach >= 95% line and branch coverage before marking complete
