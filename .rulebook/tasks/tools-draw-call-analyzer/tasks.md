# Tasks: tools-draw-call-analyzer

## 1. DX12 Command List Recorder

- [ ] 1.1 Create `engine/src/renderer/draw_call_recorder.h` defining `DrawCallRecorder` class with `BeginCapture()`, `EndCapture()`, `GetRecordedCalls() -> DrawCallList` methods
- [ ] 1.2 Create `engine/src/renderer/draw_call_recorder.cpp` implementing interception of `DrawIndexedInstanced`, `DrawInstanced`, and `ExecuteIndirect` calls
- [ ] 1.3 Define `DrawCallEntry` struct: `{pso_hash, mesh_id, material_id, pass_name, vertex_count, instance_count, timestamp_ns}`
- [ ] 1.4 Define `DrawCallList` struct: `{frame_number, entries[], capture_duration_ms}`
- [ ] 1.5 Implement one-frame capture mode: recorder activates for exactly one frame then deactivates; thread-safe with render thread using lock-free ring buffer
- [ ] 1.6 Guard all `DrawCallRecorder` code with `#ifdef UZ_TOOLS` to prevent overhead in shipping builds

## 2. IPC Capture Trigger

- [ ] 2.1 Add `tools.capture_draw_calls()` IPC handler in `engine/src/ipc/handlers/tools_handlers.cpp`
- [ ] 2.2 Implement: on receipt of IPC command, set a `capture_next_frame` atomic flag; renderer reads this flag before each frame and activates `DrawCallRecorder` for that frame
- [ ] 2.3 After capture, serialize `DrawCallList` to JSON and return via IPC response
- [ ] 2.4 Register the IPC handler in the engine's IPC dispatch table

## 3. Grouping and Analysis Engine

- [ ] 3.1 Create `engine/src/tools/draw_call_analyzer.h` defining `DrawCallAnalyzer` class
- [ ] 3.2 Create `engine/src/tools/draw_call_analyzer.cpp` implementing `Analyze(DrawCallList) -> DrawCallReport`
- [ ] 3.3 Implement grouping by `(pso_hash, mesh_id, pass_name)` key: compute per-group `{draw_call_count, total_triangles, total_instances}`
- [ ] 3.4 Implement instancing opportunity detection: groups with same `(pso_hash, mesh_id)` but multiple entries and `instance_count == 1` are flagged (could be GPU-instanced)
- [ ] 3.5 Implement redundant state-change detection: consecutive entries with same `pso_hash` but different `mesh_id` in non-instanced state
- [ ] 3.6 Define `DrawCallReport` struct: `{frame_number, total_draw_calls, groups[], suggestions[]}`

## 4. Suggestion Engine

- [ ] 4.1 Define `DrawCallSuggestion` struct: `{severity: warning|info, affected_group_key, message, fix_description}`
- [ ] 4.2 Implement rule: if group has >1 entry with same PSO+mesh and `instance_count == 1`, emit warning "N unique instances use same material — consider GPU instancing"
- [ ] 4.3 Implement rule: if any pass has >300 draw calls, emit warning "Pass '{name}' has {N} draw calls — consider enabling HZB culling"
- [ ] 4.4 Implement rule: if same PSO appears in >50 non-consecutive groups (thrashing), emit warning "PSO '{hash}' used non-consecutively in {N} groups — sort draw calls by PSO"
- [ ] 4.5 Implement all suggestion rules in under 100ms for a 5000-entry draw call list (benchmark test)

## 5. MCP Tool Exposure

- [ ] 5.1 Register `tools.analyze_draw_calls` as an MCP tool in the MCP server tool registry
- [ ] 5.2 Implement the MCP handler: calls `tools.capture_draw_calls` IPC, runs `DrawCallAnalyzer::Analyze`, returns JSON `DrawCallReport`
- [ ] 5.3 Add `DrawCallReport` JSON schema documentation to `docs/specs/ai-native-features.md`

## 6. CSV/JSON Export

- [ ] 6.1 Implement JSON export: `DrawCallReport::ToJson()` producing full structured report
- [ ] 6.2 Implement CSV export: `DrawCallReport::ToCsv()` with columns: `group_key, draw_calls, triangles, instances, suggestion`
- [ ] 6.3 Add `tools.export_draw_calls(format: "json"|"csv", output_path)` IPC handler

## 7. VSCode Extension Panel

- [ ] 7.1 Create `vscode-extension/src/panels/DrawCallAnalyzerPanel.ts` as a `WebviewPanel`
- [ ] 7.2 Implement treemap view: each group rendered as a rectangle where area proportional to draw call count
- [ ] 7.3 Implement sortable table with columns: `group`, `draw_calls`, `triangles`, `instances`, `suggestion`
- [ ] 7.4 Implement "Capture Frame" button that calls `tools.capture_draw_calls` IPC and refreshes the panel
- [ ] 7.5 Create `vscode-extension/src/commands/analyzeDrawCalls.ts` and register the command `uzengine.analyzeDrawCalls`

## 8. Testing (≥95% line + branch coverage)

- [ ] 8.1 Write `tests/tools/test_draw_call_analyzer_grouping.cpp`: mock `DrawCallList` with known groups; assert `DrawCallReport` groups match expected keys, counts, and triangle totals
- [ ] 8.2 Write `tests/tools/test_draw_call_analyzer_suggestions.cpp`: mock list with 12 same-PSO+mesh non-instanced entries; assert instancing suggestion is generated; mock list with 400-draw-call pass; assert HZB suggestion is generated
- [ ] 8.3 Write `tests/tools/test_draw_call_json_export.cpp`: serialize `DrawCallReport` to JSON; assert all required fields present and format correct
- [ ] 8.4 Write `tests/tools/test_draw_call_recorder.cpp`: mock DX12 command list; call `DrawIndexedInstanced` multiple times; assert `GetRecordedCalls()` returns matching entries with correct metadata
- [ ] 8.5 Write `tests/tools/test_draw_call_analysis_performance.cpp`: generate 5000-entry list; assert `DrawCallAnalyzer::Analyze` completes in < 100ms
- [ ] 8.6 Verify ≥95% line and branch coverage across all `engine/src/tools/draw_call_analyzer.cpp` and `engine/src/renderer/draw_call_recorder.cpp` using OpenCppCoverage
