## 1. CPU Timing Capture in UIPass
- [ ] 1.1 Add `UIPassTimings` struct in `src/ui/UIPass.h` with fields `double layout_ms`, `double paint_ms`, `double composite_ms`, `double total_ms`
- [ ] 1.2 Wrap the layout phase in `src/ui/UIPass.cpp` with `QueryPerformanceCounter` before/after calls; store elapsed time in `UIPassTimings::layout_ms`
- [ ] 1.3 Wrap the paint phase in `src/ui/UIPass.cpp` with `QueryPerformanceCounter` before/after calls; store elapsed time in `UIPassTimings::paint_ms`
- [ ] 1.4 Wrap the composite phase in `src/ui/UIPass.cpp` with `QueryPerformanceCounter` before/after calls; store elapsed time in `UIPassTimings::composite_ms`
- [ ] 1.5 Accumulate `UIPassTimings::total_ms = layout_ms + paint_ms + composite_ms` at the end of each frame

## 2. GPU Timing Capture in UIPass
- [ ] 2.1 Create a DX12 timestamp query heap (`D3D12_QUERY_TYPE_TIMESTAMP`) with capacity 4 in `src/ui/UIPass.cpp` during initialization
- [ ] 2.2 Insert `ID3D12GraphicsCommandList::EndQuery` calls at the start and end of the UI render pass on the GPU command list
- [ ] 2.3 Resolve the timestamp queries to a readback buffer and compute `gpu_ms = (end_tick - start_tick) / gpu_frequency` per frame
- [ ] 2.4 Store `gpu_ms` in the `UIPassTimings` struct as `double gpu_ms`

## 3. Element Count Statistics
- [ ] 3.1 Add fields to `UIPassTimings`: `uint32_t total_nodes`, `uint32_t visible_nodes`, `uint32_t animated_nodes`
- [ ] 3.2 After each DOM layout pass in `uz_ui_html.dll`, call the injected JavaScript `window.__uz_get_element_stats()` which returns `{total, visible, animated}` counts
- [ ] 3.3 Store the returned counts in `UIPassTimings` each frame

## 4. IPC Handler
- [ ] 4.1 Add IPC handler `ui.get_profiler_stats` in `src/ipc/IpcServer.cpp` returning JSON with all `UIPassTimings` fields: `layout_ms`, `paint_ms`, `composite_ms`, `total_ms`, `gpu_ms`, `total_nodes`, `visible_nodes`, `animated_nodes`
- [ ] 4.2 Implement ring-buffer history of the last 128 frames in `src/ui/UIPass.cpp` so the IPC handler can also return `history: [{...}, ...]` for chart rendering in the panel

## 5. VSCode UI DevTools Panel
- [ ] 5.1 Create `vscode-uzengine/src/panels/UIDevToolsPanel.ts` with a `UIDevToolsPanel` class extending `vscode.WebviewPanel`
- [ ] 5.2 Implement a polling loop in the panel that calls `ui.get_profiler_stats` via IPC every 100ms and updates the webview with the latest data
- [ ] 5.3 Render a render time breakdown bar chart in the panel webview showing layout, paint, composite, and GPU costs as color-coded segments
- [ ] 5.4 Display current-frame counters for total nodes, visible nodes, and animated nodes with color-coded thresholds (green < 500 total, yellow 500–2000, red > 2000)
- [ ] 5.5 Register the panel as a VSCode command `uzengine.openUIDevTools` in `vscode-uzengine/package.json` `contributes.commands`

## 6. Element Tree Inspector
- [ ] 6.1 Add IPC handler `ui.get_element_tree` in `src/ipc/IpcServer.cpp` that calls the injected JavaScript `window.__uz_get_element_tree()` and returns a JSON tree of DOM nodes with per-node `id`, `tag`, `rect: {x,y,w,h}`, and `paint_cost_estimate` fields
- [ ] 6.2 Render the element tree as a collapsible tree view in the UI DevTools panel webview, showing the `rect` bounds and `paint_cost_estimate` per node

## 7. Debug Overlay Integration
- [ ] 7.1 Add `ui_pass_ms` and `ui_gpu_ms` metrics to the existing in-engine debug overlay system in `src/ui/UIPass.cpp`, displayed alongside GPU frame time when the debug overlay is enabled

## 8. Testing
- [ ] 8.1 Create `vscode-uzengine/src/__tests__/UIDevToolsPanel.test.ts` with Jest test suite `UIDevToolsPanelTests`
- [ ] 8.2 Write test `PollsStatsEvery100ms`: mock the IPC client; assert the panel calls `ui.get_profiler_stats` at least once per 100ms polling interval over a 300ms simulated time window
- [ ] 8.3 Write test `StatsJsonParsedCorrectly`: feed a mock IPC response `{layout_ms:1.2, paint_ms:0.8, composite_ms:0.3, gpu_ms:2.1, total_nodes:150, visible_nodes:120, animated_nodes:10}`; assert the panel data model fields match
- [ ] 8.4 Write test `ElementTreeRenderedAsTree`: feed a mock 3-level `ui.get_element_tree` response; assert the webview HTML contains collapsible tree nodes matching the input
- [ ] 8.5 Write test `PanelRegisteredAsCommand`: parse `package.json` and assert `uzengine.openUIDevTools` appears in `contributes.commands`
- [ ] 8.6 Create `tests/renderer/test_ui_pass_timings.cpp` with Google Test suite `UIPassTimingsTest`
- [ ] 8.7 Write test `TimingsStructInitializedToZero`: create a default `UIPassTimings`; assert all fields are 0.0
- [ ] 8.8 Write test `TotalMsIsSumOfPhases`: set `layout_ms=1.0, paint_ms=2.0, composite_ms=0.5`; call the accumulation logic; assert `total_ms == 3.5`
- [ ] 8.9 Verify Jest line coverage >= 95% for `UIDevToolsPanel.ts`; verify C++ line + branch coverage >= 95% for `UIPass.cpp` timing paths

## 9. Documentation
- [ ] 9.1 Update `docs/specs/ui-system.md` with the `UIPassTimings` struct, the `ui.get_profiler_stats` IPC schema, and the element tree inspector
- [ ] 9.2 Update `docs/specs/debug-profiling.md` with the UI DevTools panel workflow and how to interpret layout/paint/composite/GPU timings
