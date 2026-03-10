# Debug Infrastructure (src/engine/core/debug/ + core/platform.h)

## New Debug Infrastructure (2026-03-09 rewrite)

These files are a clean C++23 rewrite. They replace the older assert.h/profiler.h
that lived directly in `src/engine/core/`.

### platform.h (src/engine/core/platform.h)
- Platform detection: UZ_PLATFORM_WINDOWS/LINUX/MACOS
- Compiler detection: UZ_COMPILER_MSVC/CLANG/GCC
- UZ_FORCE_INLINE, UZ_NOINLINE, UZ_DEBUG_BREAK()
- UZ_LIKELY/UZ_UNLIKELY: __builtin_expect on GCC/Clang, no-op on MSVC
- UZ_CACHE_LINE_SIZE=64, UZ_ALIGNAS(n)
- `uz::platform::now_us()` -- QPC on Windows (freq cached in static local), clock_gettime on Linux/macOS
- `uz::platform::thread_id()` -- GetCurrentThreadId / SYS_gettid / pthread_threadid_np
- Includes `<Windows.h>` with WIN32_LEAN_AND_MEAN + NOMINMAX guards

### debug/assert.h
- UZ_ASSERT(cond) -- debug-only, compiled out in UZ_SHIPPING
- UZ_VERIFY(cond) -- always evaluates, asserts only in debug
- UZ_CHECK(cond, msg) -- always-on with message (even shipping)
- UZ_UNREACHABLE() -- debug: assert; shipping: __assume(0) / __builtin_unreachable()
- `uz::detail::assert_fail()` -- [[noreturn]], writes to stderr + UZ_DEBUG_BREAK() + abort()
- Depends on platform.h (for UZ_DEBUG_BREAK, UZ_UNLIKELY, UZ_COMPILER_*)

### debug/logging.h (header-only)
- LogLevel enum: Trace/Debug/Info/Warn/Error/Fatal (u8)
- LogCallback = void(*)(LogLevel, const char*)
- log_set_level(), log_get_level(), log_add_callback() (max 8), log_clear_callbacks()
- log_message() formats into thread_local 4KB buffer, dispatches to callbacks or stderr fallback
- UZ_LOG(level, fmt, ...) + UZ_LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL convenience macros
- Inline global state via `detail::g_log_state` (C++17 inline variable)
- printf-style format (not std::format) for portability

### debug/profiler.h + profiler.cpp
- ProfilerEntry: name(const char*), start_us(u64), duration_us(u64), thread_id(u32)
- ProfilerZone: RAII scoped timer (non-copyable, non-movable)
- Ring buffer: 4096 entries, atomic u32 write index (lock-free via fetch_add relaxed)
- PROFILER_ZONE("name") macro using __COUNTER__ suffix; compiled out in UZ_SHIPPING
- profiler_record(), profiler_get_entries(), profiler_reset()
- Timing via uz::platform::now_us()

### Dependency Chain
```
types.h  <--  platform.h  <--  debug/assert.h
                           <--  debug/profiler.h  -->  debug/profiler.cpp
              types.h      <--  debug/logging.h (also uses UZ_UNUSED from types.h)
```
