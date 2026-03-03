# Proposal: tools-draw-call-analyzer

## Why

Draw call count is one of the most direct predictors of CPU-side render thread bottlenecks in DX12 applications. Each `DrawIndexedInstanced` call requires recording a command into a command list, which — even with multi-threaded command recording — has non-trivial CPU overhead. When draw calls spike from 500 to 5,000 between two scene changes, something changed: a new material was introduced that broke instancing, a prefab spawner started creating unique per-instance materials, or a mesh was added without LODs so it draws at full resolution from every distance. Without instrumentation, identifying the cause requires a full PIX session.

The `tools-draw-call-analyzer` captures one rendered frame's draw call list using DX12 command list interception, groups calls by material/mesh/pass/shader, and presents the results in a VSCode panel with a treemap visualization and sortable table. The analyzer highlights the highest-cost groups (by draw call count, polygon count, and estimated CPU submission time) and suggests specific fixes: "12 unique mesh instances use the same material — consider GPU instancing," or "Pass 'Depth Prepass' has 347 draw calls — consider enabling HZB culling." These suggestions are rule-based and deterministic, not heuristic.

The tool is also exposed as an MCP primitive `tools.analyze_draw_calls()`, enabling AI agents to programmatically check render efficiency after generating a scene. An agent that generates a large scene from a natural language prompt can immediately self-audit the draw call budget and revise the scene before presenting it to the user.

## What Changes

- DX12 command list wrapper `DrawCallRecorder`: intercepts `DrawIndexedInstanced`, `DrawInstanced`, and `ExecuteIndirect` calls during one frame, records {PSO hash, mesh ID, material ID, pass name, vertex count, instance count, timestamp}
- Frame capture trigger: IPC handler `tools.capture_draw_calls()` activates the recorder for the next frame and returns the serialized draw call list as JSON
- Grouping and analysis engine: `DrawCallAnalyzer` class groups by (PSO, mesh, pass), computes per-group totals, identifies instancing opportunities (same PSO+mesh, different transforms only), identifies redundant state changes
- Suggestion engine: rule-based analysis produces `DrawCallSuggestion[]` with severity (warning/info), affected group, and fix description
- VSCode panel `DrawCallAnalyzerPanel.ts`: treemap view (area = draw call count) + sortable table with columns {group, draw calls, triangles, instances, suggestion}
- CSV/JSON export: save analysis report for offline review or CI logging
- MCP tool `tools.analyze_draw_calls` returns JSON report for AI agent consumption
- Performance: recording overhead < 2% of frame time; analysis runs off render thread in < 100ms
- Unit tests: mock command list with known draw calls; assert grouping, suggestions, and JSON format are correct

## Impact

- Affected specs: `docs/specs/debug-profiling.md` (add draw call analyzer section), `docs/specs/ai-native-features.md` (add tools.analyze_draw_calls MCP tool), `docs/specs/renderer.md` (document DrawCallRecorder hook point)
- Affected code: `engine/src/renderer/draw_call_recorder.cpp` (new), `engine/src/tools/draw_call_analyzer.cpp` (new), `vscode-extension/src/panels/DrawCallAnalyzerPanel.ts` (new), `vscode-extension/src/commands/analyzeDrawCalls.ts` (new)
- Breaking change: NO (recording hook is opt-in, disabled in shipping builds via `#ifdef UZ_TOOLS`)
- User benefit: Identifies draw call hot spots and batching opportunities without launching an external GPU profiler; rule-based suggestions teach developers optimization patterns; MCP integration enables AI agents to self-audit generated scenes for rendering efficiency
