## 1. ViewportRegistry (`src/engine/renderer/viewport_registry.h/.cpp`)

- [x] 1.1 `ViewportId`, `ViewportState` (Inactive/Active/Suspended/Destroyed), `ViewportType`, `ViewportError` enums
- [x] 1.2 `ViewportDesc` struct: type, width, height, target_fps, jpeg_quality, vram_bytes, debug_name
- [x] 1.3 `ViewportStatus` struct: id, state, type, dimensions, frame_count, vram_bytes, fps
- [x] 1.4 Constants: `kMaxViewports=16`, `kMaxActiveViewports=4`, `kVramBudgetBytes=512MB`
- [x] 1.5 `create(desc)` — allocates slot, returns ViewportId
- [x] 1.6 `destroy(id)` — frees slot
- [x] 1.7 `set_active(id, bool)` — state machine transitions with cap + VRAM budget enforcement
- [x] 1.8 `resize(id, w, h)` — updates dimensions
- [x] 1.9 `tick()` — increments frame_count for ACTIVE; heartbeat watchdog (>30s auto-suspends all)
- [x] 1.10 `suspend_all()`, `active_count()`, `total_count()`, `total_vram_active()`
- [x] 1.11 `record_ipc_heartbeat()`, `seconds_since_heartbeat()`
- [x] 1.12 `ViewportSuspendCallback` — invoked on auto-suspend (VRAM budget or watchdog)
- [x] 1.13 Thread-safe via `uz::platform::Mutex`

## 2. IPC Handlers (`src/common/ipc_viewport_handlers.h/.cpp`)

- [x] 2.1 `viewport.create` → `{ok, viewport_id}`
- [x] 2.2 `viewport.destroy` → `{ok}`
- [x] 2.3 `viewport.setActive` → `{ok, error_code?}`
- [x] 2.4 `viewport.resize` → `{ok}`
- [x] 2.5 `viewport.getStatus` → `{ok, status:{...}}`
- [x] 2.6 `viewport.list` → `{ok, viewports:[...], active_count, total_count}`

## 3. Tests (`tests/renderer/test_viewport_registry.cpp`)

- [x] 3.1 86 tests: lifecycle, cap enforcement, VRAM budget, tick, resize, heartbeat, thread safety, IPC handlers, null registry
- [x] 3.2 1022/1022 total tests after integration

## 4. CMakeLists.txt Integration

- [x] 4.1 `src/engine/renderer/viewport_registry.cpp` added to `UzEngineTests`
- [x] 4.2 `src/common/ipc_viewport_handlers.cpp` added to `UzEngineTests`
- [x] 4.3 `tests/renderer/test_viewport_registry.cpp` added to `UzEngineTests`
