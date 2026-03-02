## 1. Research — Read Existing Specs

- [ ] 1.1 Read `docs/specs/debug-profiling.md` — understand current CPU/GPU profiling primitives
- [ ] 1.2 Read `docs/specs/shader-system.md` — understand current hot-reload mechanism for shaders
- [ ] 1.3 Read `docs/specs/lua-scripting.md` — understand Lua error reporting and reload flow
- [ ] 1.4 Read `docs/specs/crash-reporting.md` — understand existing IPC integration for crash events
- [ ] 1.5 Review existing IPC handler registry in `src/runtime/ipc/` — understand category/handler dispatch
- [ ] 1.6 Survey Debug Adapter Protocol spec (DAP 1.65) — message types: initialize, launch, setBreakpoints, stackTrace, variables, continue, next, stepIn

## 2. Design — Unified IPC Extension Model

- [ ] 2.1 Define IPC category taxonomy: `engine.*`, `scene.*`, `debug.*`, `profiler.*`, `reload.*`, `asset.*`
- [ ] 2.2 Specify message framing: `{ "category": "debug.breakpoint_hit", "seq": uint32, "body": {...} }`
- [ ] 2.3 Specify request/response correlation: `seq` field echoed in response as `request_seq`
- [ ] 2.4 Specify event (push) vs. request (pull) message distinction: `type: "event"` vs. `type: "response"`
- [ ] 2.5 Specify capability negotiation: `engine.capabilities` request → list of supported categories
- [ ] 2.6 Define port allocation: single WebSocket port 7777 (configurable via `.uzproj`), multiplexed

## 3. Design — DAP-over-IPC Bridge

- [ ] 3.1 Specify DAP message encapsulation: `{ "category": "debug.dap", "body": <raw DAP JSON> }`
- [ ] 3.2 Specify DAP session lifecycle: `debug.dap_attach` request → engine enables Lua debug hooks → `debug.dap_detach`
- [ ] 3.3 Specify breakpoint storage: `debug.set_breakpoints` → `{ path, lines[] }` → engine sets Lua debug hooks
- [ ] 3.4 Specify stack trace response format: frame array with `source`, `line`, `name`, `local_vars`
- [ ] 3.5 Specify variable inspection: `debug.get_variables` → `{ frame_id }` → flat key-value list (no deep object graph for safety)
- [ ] 3.6 Specify step commands: `debug.step_over`, `debug.step_into`, `debug.step_out`, `debug.continue`
- [ ] 3.7 Specify `debug.breakpoint_hit` event: `{ source, line, reason, stack_frames[] }`

## 4. Design — Flame Profiler Protocol

- [ ] 4.1 Specify `profiler.start` / `profiler.stop` request messages
- [ ] 4.2 Specify `profiler.frame` event schema:
  - `frame_index`: uint64
  - `cpu_zones[]`: `{ name, thread_id, start_ns, end_ns, depth }`
  - `gpu_queries[]`: `{ name, start_gpu_tick, end_gpu_tick }`
  - `frame_time_ms`: float
- [ ] 4.3 Specify CPU zone instrumentation macro: `UZ_PROFILE_SCOPE(name)` → RAII push/pop
- [ ] 4.4 Specify GPU timestamp query pool: ring buffer of `D3D12_QUERY_TYPE_TIMESTAMP` pairs
- [ ] 4.5 Specify GPU tick-to-millisecond calibration: `ID3D12CommandQueue::GetTimestampFrequency`
- [ ] 4.6 Specify profiler ring buffer: last 120 frames retained in memory, streamed on demand
- [ ] 4.7 Specify `profiler.snapshot` request → returns last N frames as JSON for VSCode panel

## 5. Design — Hot-Reload Protocol Extension

- [ ] 5.1 Specify `reload.shader` event: `{ path, variant_key, status: "ok"|"error", error_msg? }`
- [ ] 5.2 Specify `reload.script` event: `{ path, node_ids[], status, error_msg? }`
- [ ] 5.3 Specify `reload.asset` event: `{ path, asset_type, status, error_msg? }`
- [ ] 5.4 Specify `reload.request` message (editor → engine): force reload specific path
- [ ] 5.5 Specify reload latency requirements: shader < 500ms, Lua < 200ms, data asset < 100ms
- [ ] 5.6 Specify error display: failed reload must surface in VSCode Problems panel

## 6. Design — VSCode Extension Integration

- [ ] 6.1 Specify Debug Adapter registration in `package.json` — `type: "uzengine"`, `request: "attach"`
- [ ] 6.2 Specify `UzEngineDebugAdapterServer` class — translates DAP protocol ↔ IPC messages
- [ ] 6.3 Specify Profiler panel WebView — receives `profiler.frame` events, renders flame chart via canvas
- [ ] 6.4 Specify status bar item — shows reload status (spinning icon during reload, error count badge)
- [ ] 6.5 Specify `.vscode/launch.json` template for attach configuration
- [ ] 6.6 Specify connection resilience: auto-reconnect on engine restart (5s polling)

## 7. Write Spec

- [ ] 7.1 Create `docs/specs/debug-infrastructure.md` — full spec with all sections from steps 2–6
- [ ] 7.2 Add message flow diagrams: DAP attach sequence, profiler start/frame/stop, hot-reload cycle
- [ ] 7.3 Add JSON schema appendix for all new IPC message types
- [ ] 7.4 Update `docs/specs/debug-profiling.md` — add "IPC Integration" section referencing new spec
- [ ] 7.5 Update `docs/README.md` — add `debug-infrastructure.md` to specs table

## 8. Review

- [ ] 8.1 Verify all DAP protocol steps needed for Lua debugging are covered
- [ ] 8.2 Verify profiler frame message fits in a single WebSocket frame (< 16 KB for 120 zones)
- [ ] 8.3 Verify hot-reload messages are compatible with existing `reload.*` handlers in the runtime
- [ ] 8.4 Confirm single-port multiplexing does not conflict with existing `engine.*` and `scene.*` handlers
- [ ] 8.5 Confirm VSCode extension debug adapter approach matches VS Code API 1.87 requirements
