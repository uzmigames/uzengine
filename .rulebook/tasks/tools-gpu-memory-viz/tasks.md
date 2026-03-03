# Tasks: tools-gpu-memory-viz

## 1. GpuResourceTracker Engine Class

- [ ] 1.1 Create `engine/src/renderer/gpu_resource_tracker.h` defining `GpuResourceTracker` class with `Track(name, category, size_bytes, heap_type)`, `Untrack(resource_ptr)`, and `Snapshot() -> GpuMemoryReport` methods
- [ ] 1.2 Create `engine/src/renderer/gpu_resource_tracker.cpp` with a lock-free per-frame flat array storing `{name, category, size_bytes, heap_type}` entries
- [ ] 1.3 Hook `GpuResourceTracker::Track` into every `CreateCommittedResource` and `CreatePlacedResource` call in the engine's DX12 resource management layer
- [ ] 1.4 Hook `GpuResourceTracker::Untrack` into every resource release path
- [ ] 1.5 Define `GpuAllocationEntry` struct: `{name, category, size_mb, format, dimensions_w, dimensions_h, heap_type}`
- [ ] 1.6 Define resource category enum: `ALBEDO_TEXTURE`, `NORMAL_MAP`, `DEPTH_BUFFER`, `RENDER_TARGET`, `MESH_VERTEX_BUFFER`, `INDEX_BUFFER`, `COMPUTE_BUFFER`

## 2. GpuMemoryReport Data Model

- [ ] 2.1 Define `GpuMemoryReport` struct: `{total_vram_mb, budget_mb, categories: [{name, allocated_mb, budget_mb, resources: [{name, size_mb, format, dimensions}]}]}`
- [ ] 2.2 Query `IDXGIAdapter3::QueryVideoMemoryInfo` to populate `total_vram_mb` and `budget_mb` fields
- [ ] 2.3 Query `ID3D12Device::GetMemoryInfo` to cross-reference allocation totals
- [ ] 2.4 Aggregate per-allocation entries into category buckets by `GpuAllocationEntry::category`
- [ ] 2.5 Sort resources within each category by `size_mb` descending (top 50 by size reported in IPC response)

## 3. IPC Handlers

- [ ] 3.1 Add `tools.get_gpu_memory_report()` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp` returning serialized `GpuMemoryReport` JSON
- [ ] 3.2 Add `tools.subscribe_gpu_memory(interval_ms)` IPC handler: registers a periodic callback that sends `GpuMemoryReport` snapshots at the specified interval
- [ ] 3.3 Implement subscription management: track active subscribers, cancel subscription on client disconnect
- [ ] 3.4 Register both IPC handlers in the engine's IPC dispatch table

## 4. Budget Configuration

- [ ] 4.1 Parse `[vram_budgets]` section from `game.project.toml`: read per-category MB limits (`albedo_textures`, `normal_maps`, `render_targets`, `mesh_buffers`, etc.)
- [ ] 4.2 Expose budget thresholds in `GpuMemoryReport` for each category
- [ ] 4.3 Compute `budget_status` per category: `green` (< 80%), `yellow` (80–99%), `red` (>= 100%)

## 5. Budget Breach Notifications

- [ ] 5.1 Implement budget monitoring loop in the IPC subscription path: after each snapshot, check if any category exceeds 80% of budget
- [ ] 5.2 Send `tools.gpu_memory_warning` IPC notification event when a category reaches 80% of budget (once per category per session)
- [ ] 5.3 Send `tools.gpu_memory_error` IPC notification event when any category reaches 100% of budget

## 6. Session Report Auto-Export

- [ ] 6.1 Implement `GpuResourceTracker::WriteSessionReport(output_path)`: write `vram-session-report.json` on engine shutdown summarizing peak usage per category
- [ ] 6.2 Call `WriteSessionReport` from the engine shutdown sequence
- [ ] 6.3 Define `VramSessionReport` JSON format: `{session_duration_s, categories: [{name, peak_mb, budget_mb, budget_pct}]}`

## 7. VSCode Extension Panel

- [ ] 7.1 Create `vscode-extension/src/panels/GpuMemoryVizPanel.ts` as a React `WebviewPanel`
- [ ] 7.2 Implement category bar chart: horizontal bars for each category showing `allocated_mb` vs. `budget_mb`, color-coded green/yellow/red
- [ ] 7.3 Implement 60-second time-series sparkline per category using subscription data; evict data older than 60 seconds
- [ ] 7.4 Implement sortable resource table with columns: `name`, `category`, `size_mb`, `format`, `dimensions`
- [ ] 7.5 Implement "Snapshot" button that calls `tools.get_gpu_memory_report` and exports JSON to a user-specified path
- [ ] 7.6 Create `vscode-extension/src/commands/openGpuMemoryViz.ts` and register command `uzengine.openGpuMemoryViz`

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/tools/test_gpu_resource_tracker.cpp`: add 20 mock allocations across 5 categories; call `Snapshot()`; assert category totals and top-50 resource list are correct
- [ ] 8.2 Write `tests/tools/test_gpu_memory_report_json.cpp`: serialize `GpuMemoryReport` to JSON; assert all required fields present and category budget_status reflects correct thresholds
- [ ] 8.3 Write `tests/tools/test_gpu_budget_notifications.cpp`: add allocations that push a category over 80% and 100%; assert correct notification events are emitted
- [ ] 8.4 Write `tests/tools/test_gpu_tracker_untrack.cpp`: add 10 allocations then remove 5; assert `Snapshot()` reflects only the remaining 5
- [ ] 8.5 Write `tests/tools/test_gpu_session_report.cpp`: simulate session with known peak allocations; call `WriteSessionReport`; assert output JSON matches expected peak values
- [ ] 8.6 Verify ≥95% line and branch coverage across all `engine/src/renderer/gpu_resource_tracker.cpp` using OpenCppCoverage
