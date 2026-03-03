## 1. Platform Detection & API (`src/engine/core/platform.h`)

- [x] 1.1 `UZ_PLATFORM_WINDOWS`, `UZ_PLATFORM_LINUX`, `UZ_PLATFORM_MACOS` detection macros
- [x] 1.2 `uz::platform::get_time_seconds()` — monotonic clock (CLOCK_MONOTONIC / QPC)
- [x] 1.3 `uz::platform::get_time_microseconds()` — high-resolution microsecond timer
- [x] 1.4 `uz::platform::path_exists()`, `file_size()`, `file_read_all()`, `file_write_all()`, `mkdir_recursive()`
- [x] 1.5 `uz::platform::Thread`, `thread_create()`, `thread_join()`, `thread_detach()`, `thread_current_id()`, `thread_sleep_ms()`
- [x] 1.6 `uz::platform::Mutex`, `mutex_create()`, `mutex_destroy()`, `mutex_lock()`, `mutex_unlock()`, `mutex_try_lock()`
- [x] 1.7 `uz::platform::DynLib`, `dynlib_open()`, `dynlib_close()`, `dynlib_symbol()`

## 2. Implementation (`src/engine/core/platform.cpp`)

- [x] 2.1 Linux: `clock_gettime(CLOCK_MONOTONIC)`, `stat()`, POSIX file I/O, `pthread_t`, `PTHREAD_MUTEX_ERRORCHECK`, `dlopen/dlsym/dlclose`
- [x] 2.2 Windows: `QueryPerformanceCounter`, `GetFileAttributesExW`, `CreateFileW/ReadFile/WriteFile`, `CreateThread`, `CRITICAL_SECTION`, `LoadLibraryA/GetProcAddress`
- [x] 2.3 Both platforms guarded by `#if defined(UZ_PLATFORM_*)` blocks

## 3. Tests (`tests/core/test_platform.cpp`)

- [x] 3.1 59 tests across 12 suites: timer (5), file I/O (14), mkdir (6), thread (7), thread_id (3), sleep (2), mutex (10), dynlib (7), integration (1)
- [x] 3.2 All tests pass on Linux (dev container, Clang 18)
- [x] 3.3 870/870 total tests after integration

## 4. CMakeLists.txt Integration

- [x] 4.1 `src/engine/core/platform.cpp` added to `UzEngineTests`
- [x] 4.2 `tests/core/test_platform.cpp` added to `UzEngineTests`
- [x] 4.3 `-lpthread -ldl` added for Linux builds
