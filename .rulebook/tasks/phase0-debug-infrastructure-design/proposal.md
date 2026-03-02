# Proposal: phase0-debug-infrastructure-design

## Why

Debugging a running game inside VSCode requires three independent protocols: a debug adapter (DAP) for stepping through Lua code, a flame profiler for CPU/GPU frame time, and a hot-reload protocol for shaders and scripts. Without a unified design, each feature will open its own TCP port, define its own JSON schema, and create conflicts in the VSCode extension. A single pre-implementation spec locks in the shared IPC extension model, port allocation, message framing, and the VSCode panel integration points for all three debug facilities.

## What Changes

- **No code changes** — this is a design-only task.
- **`docs/specs/debug-infrastructure.md`** — new spec covering:
  - Unified IPC extension model: categories (`debug.*`, `profiler.*`, `reload.*`)
  - DAP-over-IPC bridge: DAP JSON messages tunneled through existing WebSocket
  - Flame profiler wire format: `profiler.frame` message schema (CPU zones, GPU queries)
  - Hot-reload protocol extension: `reload.shader`, `reload.script`, `reload.asset` messages
  - VSCode extension integration points: Debug Adapter registration, Profiler panel WebView, reload status bar item
  - IPC port allocation: single port (default 7777), multiplexed by category prefix

## Impact

- Affected specs: `docs/specs/debug-infrastructure.md` (new), `docs/specs/debug-profiling.md` (extended)
- Downstream tasks: `phase1-lua-dap-server`, `phase1-debug-profiling`, `phase1-shader-hot-reload`
- Breaking change: NO — design only
- User benefit: single debug port, unified VSCode integration, no port conflicts between subsystems
