## 1. FileWatcher (`src/engine/core/file_watcher.h/.cpp`)

- [x] 1.1 `FileWatcher::watch(directory, callback)` — returns false if dir doesn't exist
- [x] 1.2 Linux backend: `inotify_init1` + `inotify_add_watch` (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE)
- [x] 1.3 Windows backend: `ReadDirectoryChangesW` with overlapped I/O (stub compiles on Linux)
- [x] 1.4 macOS backend: stub (`watch()` returns false)
- [x] 1.5 Background thread via `uz::platform::thread_create`
- [x] 1.6 Debounce: 200ms window per path via `std::unordered_map<string, uint64_t>`
- [x] 1.7 `FileWatchEvent`: Modified, Created, Deleted
- [x] 1.8 `stop()` / `is_watching()` / `set_debounce_ms()`

## 2. ShaderManager (`src/engine/renderer/shader_manager.h/.cpp`)

- [x] 2.1 `register_shader(name, vs_path, ps_path)` — in-memory cache entry
- [x] 2.2 `reload(path)` — finds affected shaders, marks valid, invokes callback (Linux stub: always succeeds)
- [x] 2.3 `get_errors(path)` — returns last compile errors for path
- [x] 2.4 `list()` — returns all registered shader names
- [x] 2.5 `get_info(name)` — returns `ShaderInfo*` (paths, is_valid, reload_count, errors)
- [x] 2.6 Thread-safe via `uz::platform::Mutex`

## 3. IPC Handlers (`src/common/ipc_shader_handlers.h/.cpp`)

- [x] 3.1 `shader.reload` — `{path}` → `{ok, errors, elapsed_ms}`
- [x] 3.2 `shader.getErrors` — `{path}` → `{ok, errors}`
- [x] 3.3 `shader.list` — `{}` → `{ok, shaders:[{name,vs_path,ps_path,is_valid}]}`
- [x] 3.4 `shader.getSource` — `{path}` → `{ok, source}`
- [x] 3.5 `shader.setSource` — `{path, source}` → `{ok, error}`

## 4. Tests

- [x] 4.1 `tests/core/test_file_watcher.cpp` — 20 tests: lifecycle, inotify detect (modify/create/delete), debounce, rewatch
- [x] 4.2 `tests/renderer/test_shader_manager.cpp` — 46 tests: ShaderManager unit + IPC handler tests
- [x] 4.3 936/936 total tests after integration

## 5. CMakeLists.txt Integration

- [x] 5.1 `src/engine/core/file_watcher.cpp` added to `UzEngineTests`
- [x] 5.2 `src/engine/renderer/shader_manager.cpp` added to `UzEngineTests`
- [x] 5.3 `src/common/ipc_shader_handlers.cpp` added to `UzEngineTests`
- [x] 5.4 Test files added to `UzEngineTests`
