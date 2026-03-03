# Debug Infrastructure: Unified IPC Extension Model

> **Status**: Design (Phase 1)
> **Owner**: `Engine` module (`src/engine/ipc/`)
> **Purpose**: Single unified IPC protocol for all debugging subsystems (DAP, profiler, hot-reload) multiplexed over one WebSocket port
> **Downstream tasks**: `phase1-lua-dap-server`, `phase1-debug-profiling`, `phase1-shader-hot-reload`

---

## Problem Statement

Debug support in VSCode requires three independent protocols:
1. **Debug Adapter Protocol (DAP)** — for stepping through Lua code, setting breakpoints
2. **Flame Profiler** — for CPU/GPU performance analysis, frame timeline visualization
3. **Hot-Reload Protocol** — for shader/script/asset reload status and error reporting

Without unification, the implementation would:
- Open 3 separate TCP ports (7777, 7778, 7779) creating port conflicts
- Define 3 independent JSON message schemas with inconsistent versioning
- Require 3 separate integrations in the VSCode extension (UzEngineDebugAdapterServer, ProfilerPanel, ReloadStatusBar)
- Create coupling between the IPC server and the VSCode extension

This spec defines a **single multiplexed IPC protocol** where all debug subsystems share:
- One WebSocket port (default 7777, configurable)
- One message framing format (`category`, `seq`, `body`)
- One request/response correlation model
- One VSCode extension integration point

---

## Design Principles

1. **Single port, multiplexed categories** — all debug subsystems share port 7777, identified by message `category` prefix
2. **Request/response correlation** — `seq` field echoed in responses for matching
3. **Events (push) vs. requests (pull)** — `type: "event"` for unsolicited, `type: "response"` for RPC replies
4. **Capability negotiation** — clients discover supported categories at connect time
5. **Extensible categories** — new debug subsystems add new categories without protocol changes
6. **No breaking changes** — existing `engine.*` and `scene.*` handlers remain unchanged

---

## Message Framing

All IPC messages sent over the WebSocket follow this envelope format:

```json
{
  "category": "debug.breakpoint_hit",
  "type": "event",
  "seq": 12345,
  "body": {
    // Subsystem-specific payload
  }
}
```

### Message Structure

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `category` | string | Yes | Namespace + message type, e.g., `debug.dap`, `profiler.frame`, `reload.shader` |
| `type` | string | Yes | `"event"` (push), `"request"` (RPC call), or `"response"` (RPC reply) |
| `seq` | uint32 | Yes | Sequence number for correlation (client generates for requests, server echoes in responses) |
| `body` | object | Yes | Message payload (format depends on category) |
| `request_seq` | uint32 | No | For responses: echoes the `seq` of the corresponding request |
| `success` | bool | No | For responses: `true` if request succeeded, `false` if error |
| `error` | string | No | For responses: error message if `success: false` |

### Message Direction

| Type | Sender | Purpose | Example |
|------|--------|---------|---------|
| **event** | Engine | Unsolicited notification (push) | `debug.breakpoint_hit`, `profiler.frame`, `reload.shader` |
| **request** | Client | RPC call to engine (pull) | `profiler.snapshot`, `debug.get_variables` |
| **response** | Engine | Reply to client request | Reply to `profiler.snapshot` with frame data |

---

## Category Taxonomy

All IPC categories fall into one of these families:

### Existing (Unchanged)

- **engine.*** — engine lifecycle, resource statistics
- **scene.*** — scene loading, node manipulation
- **asset.*** — asset cooking, cache invalidation

### New (Phase 1+)

- **debug.*** — Lua debugging (DAP-over-IPC bridge)
- **profiler.*** — CPU/GPU profiler (frame zones, GPU queries)
- **reload.*** — hot-reload status and control (shaders, scripts, assets)

---

## 1. DAP-over-IPC Bridge

The Debug Adapter Protocol (DAP) is a JSON-RPC protocol for debuggers. Instead of opening a separate TCP port, all DAP messages are **encapsulated in IPC messages** and tunneled over the existing WebSocket.

### DAP Session Lifecycle

```
Client (VSCode)                          Engine
   │                                        │
   ├─ debug.dap_attach ──────────────────> │  Enable Lua debug hooks
   │                                        │
   │ <─────────────────────── debug.dap_attached
   │                                        │
   ├─ debug.set_breakpoints ─────────────> │  Hook Lua debug
   │                                        │
   │ <─────────────────────── debug.breakpoint_set
   │                                        │
   │                              [Lua execution]
   │                                        │
   │ <─────────────────────── debug.breakpoint_hit
   │                                        │
   ├─ debug.stack_trace ──────────────────> │  Get call stack
   │                                        │
   │ <─────────────────────── debug.stack_trace_reply
   │                                        │
   ├─ debug.get_variables ────────────────> │  Inspect frame locals
   │                                        │
   │ <─────────────────────── debug.variables_reply
   │                                        │
   ├─ debug.continue ─────────────────────> │  Resume execution
   │                                        │
   │ <─────────────────────── debug.continued
   │                                        │
   └─ debug.dap_detach ───────────────────> │  Disable hooks
                                            │
```

### Request: `debug.dap_attach`

Attach to the running Lua VM and enable debug hooks.

```json
{
  "category": "debug.dap_attach",
  "type": "request",
  "seq": 1,
  "body": {
    "stop_on_entry": false,
    "console": "integratedTerminal",
    "sourceRoot": "${workspaceFolder}"
  }
}
```

**Response**:
```json
{
  "category": "debug.dap_attach",
  "type": "response",
  "seq": 1,
  "request_seq": 1,
  "success": true,
  "body": {
    "capabilities": {
      "supportsSetBreakpoint": true,
      "supportsStackTrace": true,
      "supportsVariables": true,
      "supportsStepOver": true,
      "supportsStepInto": true,
      "supportsStepOut": true,
      "supportsContinue": true
    }
  }
}
```

### Request: `debug.set_breakpoints`

Set breakpoints at specific file:line pairs.

```json
{
  "category": "debug.set_breakpoints",
  "type": "request",
  "seq": 2,
  "body": {
    "source": "res://scripts/player.lua",
    "lines": [10, 25, 42]
  }
}
```

**Response**:
```json
{
  "category": "debug.set_breakpoints",
  "type": "response",
  "seq": 2,
  "request_seq": 2,
  "success": true,
  "body": {
    "breakpoints": [
      { "line": 10, "verified": true },
      { "line": 25, "verified": true },
      { "line": 42, "verified": true }
    ]
  }
}
```

### Event: `debug.breakpoint_hit`

Fired when execution hits a breakpoint.

```json
{
  "category": "debug.breakpoint_hit",
  "type": "event",
  "seq": 100,
  "body": {
    "source": "res://scripts/player.lua",
    "line": 25,
    "column": 0,
    "reason": "breakpoint",
    "thread_id": 1,
    "stack_frames": [
      {
        "id": 0,
        "name": "on_update",
        "source": "res://scripts/player.lua",
        "line": 25,
        "column": 0
      },
      {
        "id": 1,
        "name": "game_loop",
        "source": "res://engine/core/game_loop.lua",
        "line": 120,
        "column": 0
      }
    ]
  }
}
```

### Request: `debug.stack_trace`

Get the call stack at the breakpoint.

```json
{
  "category": "debug.stack_trace",
  "type": "request",
  "seq": 3,
  "body": {
    "thread_id": 1,
    "start_frame": 0,
    "levels": 10
  }
}
```

**Response**:
```json
{
  "category": "debug.stack_trace",
  "type": "response",
  "seq": 3,
  "request_seq": 3,
  "success": true,
  "body": {
    "stack_frames": [
      {
        "id": 0,
        "name": "on_update",
        "source": "res://scripts/player.lua",
        "line": 25
      },
      {
        "id": 1,
        "name": "game_loop",
        "source": "res://engine/core/game_loop.lua",
        "line": 120
      }
    ],
    "total_frames": 2
  }
}
```

### Request: `debug.get_variables`

Inspect local variables in a stack frame.

```json
{
  "category": "debug.get_variables",
  "type": "request",
  "seq": 4,
  "body": {
    "frame_id": 0
  }
}
```

**Response** (flat key-value list, no deep object graphs):
```json
{
  "category": "debug.get_variables",
  "type": "response",
  "seq": 4,
  "request_seq": 4,
  "success": true,
  "body": {
    "variables": [
      { "name": "self", "type": "table", "value": "PlayerController" },
      { "name": "input_x", "type": "number", "value": "0.5" },
      { "name": "is_jumping", "type": "boolean", "value": "false" },
      { "name": "velocity", "type": "table", "value": "{x=10.5, y=0, z=5.2}" }
    ]
  }
}
```

### Request: `debug.step_over`

Execute one line of Lua code and break again.

```json
{
  "category": "debug.step_over",
  "type": "request",
  "seq": 5,
  "body": { "thread_id": 1 }
}
```

### Request: `debug.step_into`

Step into the next function call.

```json
{
  "category": "debug.step_into",
  "type": "request",
  "seq": 6,
  "body": { "thread_id": 1 }
}
```

### Request: `debug.step_out`

Step out of the current function.

```json
{
  "category": "debug.step_out",
  "type": "request",
  "seq": 7,
  "body": { "thread_id": 1 }
}
```

### Request: `debug.continue`

Resume execution until the next breakpoint.

```json
{
  "category": "debug.continue",
  "type": "request",
  "seq": 8,
  "body": { "thread_id": 1 }
}
```

### Request: `debug.dap_detach`

Detach and disable debug hooks.

```json
{
  "category": "debug.dap_detach",
  "type": "request",
  "seq": 9,
  "body": {}
}
```

---

## 2. Flame Profiler Protocol

The profiler captures CPU zones (C++ scopes) and GPU timestamp queries into a **frame event** streamed to VSCode for visualization.

### Data Model

**CPU Zone** — corresponds to one `UZ_PROFILE_ZONE()` scope:

```
{
  "name": "Shadow Pass",
  "thread_id": 0,
  "start_ns": 1000000000,  // nanoseconds since frame start
  "end_ns": 1005000000,
  "depth": 0,
  "color_rgb": 0xFF6B9D
}
```

**GPU Query** — corresponds to one GPU timestamp pair:

```
{
  "name": "G-Buffer Pass",
  "start_gpu_tick": 100000,
  "end_gpu_tick": 150000,
  "gpu_tick_freq": 1000000000  // GPU clock frequency (Hz)
}
```

### Request: `profiler.start`

Begin profiling.

```json
{
  "category": "profiler.start",
  "type": "request",
  "seq": 10,
  "body": {
    "frame_buffer_size": 120,
    "gpu_enabled": true
  }
}
```

### Request: `profiler.stop`

End profiling and flush the current frame.

```json
{
  "category": "profiler.stop",
  "type": "request",
  "seq": 11,
  "body": {}
}
```

### Event: `profiler.frame`

Emitted once per rendered frame (throttled). Contains CPU zones and GPU queries for that frame.

```json
{
  "category": "profiler.frame",
  "type": "event",
  "seq": 200,
  "body": {
    "frame_index": 1234,
    "frame_time_ms": 16.67,
    "cpu_zones": [
      {
        "name": "Update",
        "thread_id": 0,
        "start_ns": 0,
        "end_ns": 2000000,
        "depth": 0
      },
      {
        "name": "Physics Step",
        "thread_id": 0,
        "start_ns": 500000,
        "end_ns": 1500000,
        "depth": 1
      },
      {
        "name": "Render",
        "thread_id": 0,
        "start_ns": 2000000,
        "end_ns": 14000000,
        "depth": 0
      },
      {
        "name": "Shadow Pass",
        "thread_id": 0,
        "start_ns": 2500000,
        "end_ns": 5000000,
        "depth": 1
      },
      {
        "name": "G-Buffer Pass",
        "thread_id": 0,
        "start_ns": 5000000,
        "end_ns": 10000000,
        "depth": 1
      }
    ],
    "gpu_queries": [
      {
        "name": "Shadow Pass",
        "start_gpu_tick": 100000,
        "end_gpu_tick": 150000,
        "gpu_tick_freq": 1000000000
      },
      {
        "name": "G-Buffer Pass",
        "start_gpu_tick": 150000,
        "end_gpu_tick": 300000,
        "gpu_tick_freq": 1000000000
      }
    ]
  }
}
```

### Request: `profiler.snapshot`

Request the last N frames of profiling data.

```json
{
  "category": "profiler.snapshot",
  "type": "request",
  "seq": 12,
  "body": {
    "num_frames": 120,
    "include_gpu": true
  }
}
```

**Response**:
```json
{
  "category": "profiler.snapshot",
  "type": "response",
  "seq": 12,
  "request_seq": 12,
  "success": true,
  "body": {
    "frames": [
      { "frame_index": 1234, "frame_time_ms": 16.67, "cpu_zones": [...], "gpu_queries": [...] },
      { "frame_index": 1235, "frame_time_ms": 16.68, "cpu_zones": [...], "gpu_queries": [...] }
    ]
  }
}
```

### Implementation Notes

- **CPU zones**: Captured via `UZ_PROFILE_ZONE(name)` macro, stored in ring buffer
- **GPU queries**: DX12 timestamp queries, converted to milliseconds using `ID3D12CommandQueue::GetTimestampFrequency`
- **Ring buffer**: Last 120 frames retained in memory
- **Throttling**: Emit `profiler.frame` once per frame maximum
- **Memory**: ~16 KB per frame with 120 zones → 2 MB total for 120-frame history

---

## 3. Hot-Reload Protocol Extension

The hot-reload system notifies VSCode of reload status and errors for shaders, Lua scripts, and data assets.

### Event: `reload.shader`

Fired when a shader finishes hot-reloading (success or error).

```json
{
  "category": "reload.shader",
  "type": "event",
  "seq": 300,
  "body": {
    "path": "res://shaders/pbr.hlsl",
    "variant_key": "pbr_skinned",
    "status": "ok",
    "error_msg": null,
    "compile_time_ms": 120
  }
}
```

### Event: `reload.script`

Fired when a Lua script finishes hot-reloading.

```json
{
  "category": "reload.script",
  "type": "event",
  "seq": 301,
  "body": {
    "path": "res://scripts/player.lua",
    "node_ids": [42, 43],
    "status": "error",
    "error_msg": "line 10: unexpected symbol 'end'",
    "compile_time_ms": 5
  }
}
```

### Event: `reload.asset`

Fired when a data asset (texture, mesh, audio) finishes hot-reloading.

```json
{
  "category": "reload.asset",
  "type": "event",
  "seq": 302,
  "body": {
    "path": "res://textures/stone_albedo.png",
    "asset_type": "texture",
    "status": "ok",
    "error_msg": null,
    "cook_time_ms": 200
  }
}
```

### Request: `reload.request`

Explicitly request a reload of a specific file (editor → engine).

```json
{
  "category": "reload.request",
  "type": "request",
  "seq": 13,
  "body": {
    "path": "res://shaders/pbr.hlsl",
    "force": true
  }
}
```

### Latency SLOs

| Type | Target | Reason |
|------|--------|--------|
| Shader reload | < 500ms | User feedback, iteration speed |
| Lua script reload | < 200ms | Quick iteration |
| Asset reload | < 100ms | Texture/mesh updates |

### Error Display

Failed reloads surface in VSCode:
- **Problems panel** — one entry per failed file (category: `reload`)
- **Status bar item** — shows spinner during reload, error badge if failed
- **Output channel** — logs raw IPC messages for debugging

---

## 4. VSCode Extension Integration

The VSCode extension integrates with the unified IPC protocol at three points.

### 4.1 Debug Adapter Registration

In `vscode-uzengine/package.json`:

```json
{
  "debuggers": [
    {
      "type": "uzengine",
      "label": "UzEngine Lua",
      "program": "${command:uzengine.startDebugServer}",
      "runtime": "node",
      "configurationAttributes": {
        "launch": {
          "properties": {
            "program": {
              "type": "string",
              "description": "Workspace path"
            }
          }
        }
      },
      "initialConfigurations": [
        {
          "name": "Attach to UzEngine",
          "type": "uzengine",
          "request": "attach",
          "program": "${workspaceFolder}"
        }
      ]
    }
  ]
}
```

### 4.2 Debug Adapter Server Class

In `vscode-uzengine/src/debugAdapter.ts`:

```typescript
class UzEngineDebugAdapterServer implements vscode.DebugAdapterDescriptorFactory {
  createDebugAdapterDescriptor(
    session: vscode.DebugSession,
    executable: vscode.DebugAdapterExecutable | undefined
  ): vscode.ProviderResult<vscode.DebugAdapterDescriptor> {
    // Return a DebugAdapterInlineImplementation that:
    // 1. Connects to IPC WebSocket
    // 2. Translates VSCode DAP messages → debug.* IPC messages
    // 3. Translates debug.* events → VSCode DebugSession events
  }
}
```

### 4.3 Profiler Panel WebView

In `vscode-uzengine/src/profilerPanel.ts`:

```typescript
class ProfilerPanel {
  // VSCode WebView that:
  // 1. Receives profiler.frame events from IPC
  // 2. Renders flame chart using canvas
  // 3. Allows zoom, pan, search by zone name
  // 4. Shows frame time histogram and per-thread breakdown
}
```

### 4.4 Reload Status Bar Item

In `vscode-uzengine/src/reloadStatusBar.ts`:

```typescript
class ReloadStatusBar implements vscode.Disposable {
  // Updates on reload.* events:
  // - Spinner icon during reload
  // - Checkmark on success
  // - X with error count on failure
  // - Click to show Problems panel
}
```

### 4.5 VSCode Launch Config Template

In `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Attach to UzEngine",
      "type": "uzengine",
      "request": "attach",
      "program": "${workspaceFolder}",
      "stopOnEntry": false,
      "console": "integratedTerminal",
      "sourceRoot": "${workspaceFolder}/scripts"
    }
  ]
}
```

### 4.6 Connection Resilience

If the engine restarts:
- **Auto-reconnect**: Poll engine every 5 seconds for WebSocket availability
- **Session state**: VSCode maintains breakpoint list, reconnects on reconnection
- **User notification**: Toast message when connection lost/restored

---

## 5. Port Allocation & Multiplexing

### Single Port Model

```
Default: 7777
Configurable via: .uzproj [debug] ipc_port = 7777
```

All debug subsystems share this port. The message `category` field determines routing:

```
WebSocket :7777
    │
    ├─ category: "debug.*"       → LuaDapServer handler
    ├─ category: "profiler.*"    → ProfilerServer handler
    ├─ category: "reload.*"      → ReloadServer handler
    └─ category: "engine.*"      → Existing engine handlers
```

### No Conflicts with Existing Handlers

The `engine.*` and `scene.*` handlers continue unchanged:

```
category: "engine.capabilities"      ← Existing (unchanged)
category: "engine.capture_frame"     ← Existing (unchanged)
category: "debug.dap_attach"         ← New (debug subsystem)
category: "profiler.start"           ← New (profiler subsystem)
category: "reload.shader"            ← New (reload subsystem)
```

---

## 6. Capability Negotiation

On connect, clients discover supported categories:

```json
{
  "category": "engine.capabilities",
  "type": "request",
  "seq": 1,
  "body": {}
}
```

**Response**:
```json
{
  "category": "engine.capabilities",
  "type": "response",
  "seq": 1,
  "request_seq": 1,
  "success": true,
  "body": {
    "categories": {
      "engine": ["capabilities", "capture_frame", "debugOverlay"],
      "scene": ["list_nodes", "get_node_props"],
      "debug": ["dap_attach", "set_breakpoints", "stack_trace", "get_variables", "continue", "step_over"],
      "profiler": ["start", "stop", "snapshot"],
      "reload": ["request"]
    },
    "version": "1.0",
    "phase": "1"
  }
}
```

---

## 7. Message Flow Diagrams

### 7.1 DAP Debug Session Flow

```
Client                              Engine

    │ debug.dap_attach              │
    ├──────────────────────────────>│  Set Lua debug hooks
    │                               │
    │         debug.dap_attached    │
    │<──────────────────────────────┤
    │                               │
    │ debug.set_breakpoints         │
    ├──────────────────────────────>│  Hook res://scripts/player.lua:25
    │                               │
    │     debug.breakpoint_set      │
    │<──────────────────────────────┤
    │                               │
    │                          [User presses play]
    │                               │
    │                      [Execution hits line 25]
    │                               │
    │     debug.breakpoint_hit      │
    │<──────────────────────────────┤  Includes stack frames
    │                               │
    │ debug.stack_trace             │
    ├──────────────────────────────>│  Get call stack
    │                               │
    │    debug.stack_trace_reply    │
    │<──────────────────────────────┤
    │                               │
    │ debug.get_variables           │
    ├──────────────────────────────>│  Inspect locals at frame 0
    │                               │
    │   debug.variables_reply       │
    │<──────────────────────────────┤
    │                               │
    │ debug.step_over               │
    ├──────────────────────────────>│  Execute next line
    │                               │
    │     debug.breakpoint_hit      │
    │<──────────────────────────────┤  At line 26
    │                               │
    │ debug.continue                │
    ├──────────────────────────────>│  Resume until next breakpoint
    │                               │
    │       debug.continued         │
    │<──────────────────────────────┤
    │                               │
    │ debug.dap_detach              │
    ├──────────────────────────────>│  Disable debug hooks
    │                               │
```

### 7.2 Profiler Frame Capture Flow

```
Client                              Engine

    │ profiler.start                │
    ├──────────────────────────────>│  Begin capturing zones/queries
    │                               │
    │     profiler.started          │
    │<──────────────────────────────┤
    │                               │
    │                          [Frame 1 renders]
    │                               │
    │     profiler.frame            │
    │<──────────────────────────────┤  CPU zones + GPU queries
    │                               │
    │                          [Frame 2 renders]
    │                               │
    │     profiler.frame            │
    │<──────────────────────────────┤  CPU zones + GPU queries
    │                               │
    │ profiler.snapshot             │
    ├──────────────────────────────>│  Request last 120 frames
    │                               │
    │   profiler.snapshot_reply     │
    │<──────────────────────────────┤  Full frame history
    │                               │
    │ profiler.stop                 │
    ├──────────────────────────────>│  Stop capturing
    │                               │
    │      profiler.stopped         │
    │<──────────────────────────────┤
    │                               │
```

### 7.3 Hot-Reload Status Flow

```
Editor (FileWatcher)                Engine

       [res://shaders/pbr.hlsl saved]
            │
            │ reload.request        │
            ├──────────────────────>│  Force reload
            │                       │
            │         [DXC compiling...]
            │                       │
            │    reload.shader      │
            │<──────────────────────┤  "ok" status
            │                       │
       [res://scripts/player.lua saved]
            │
            │ reload.request        │
            ├──────────────────────>│  Force reload
            │                       │
            │         [Lua parse]
            │         [Error on line 10]
            │                       │
            │    reload.script      │
            │<──────────────────────┤  "error" status
            │                       │
       [VSCode Problems panel updated]
       [Status bar shows error badge]
            │
```

---

## 8. JSON Schema Appendix

Complete JSON schema definitions for all new IPC message types.

### 8.1 Debug Messages

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "definitions": {
    "DebugDapAttach": {
      "type": "object",
      "properties": {
        "category": { "const": "debug.dap_attach" },
        "type": { "const": "request" },
        "seq": { "type": "integer" },
        "body": {
          "type": "object",
          "properties": {
            "stop_on_entry": { "type": "boolean" },
            "console": { "enum": ["integratedTerminal", "externalTerminal"] },
            "sourceRoot": { "type": "string" }
          }
        }
      },
      "required": ["category", "type", "seq", "body"]
    },
    "DebugBreakpointHit": {
      "type": "object",
      "properties": {
        "category": { "const": "debug.breakpoint_hit" },
        "type": { "const": "event" },
        "seq": { "type": "integer" },
        "body": {
          "type": "object",
          "properties": {
            "source": { "type": "string" },
            "line": { "type": "integer" },
            "column": { "type": "integer" },
            "reason": { "enum": ["breakpoint", "step", "entry"] },
            "thread_id": { "type": "integer" },
            "stack_frames": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "id": { "type": "integer" },
                  "name": { "type": "string" },
                  "source": { "type": "string" },
                  "line": { "type": "integer" },
                  "column": { "type": "integer" }
                }
              }
            }
          },
          "required": ["source", "line", "reason", "thread_id"]
        }
      },
      "required": ["category", "type", "seq", "body"]
    }
  }
}
```

### 8.2 Profiler Messages

```json
{
  "definitions": {
    "ProfilerFrame": {
      "type": "object",
      "properties": {
        "category": { "const": "profiler.frame" },
        "type": { "const": "event" },
        "seq": { "type": "integer" },
        "body": {
          "type": "object",
          "properties": {
            "frame_index": { "type": "integer" },
            "frame_time_ms": { "type": "number" },
            "cpu_zones": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "name": { "type": "string" },
                  "thread_id": { "type": "integer" },
                  "start_ns": { "type": "integer" },
                  "end_ns": { "type": "integer" },
                  "depth": { "type": "integer" }
                },
                "required": ["name", "thread_id", "start_ns", "end_ns", "depth"]
              }
            },
            "gpu_queries": {
              "type": "array",
              "items": {
                "type": "object",
                "properties": {
                  "name": { "type": "string" },
                  "start_gpu_tick": { "type": "integer" },
                  "end_gpu_tick": { "type": "integer" },
                  "gpu_tick_freq": { "type": "integer" }
                },
                "required": ["name", "start_gpu_tick", "end_gpu_tick", "gpu_tick_freq"]
              }
            }
          },
          "required": ["frame_index", "frame_time_ms", "cpu_zones"]
        }
      },
      "required": ["category", "type", "seq", "body"]
    }
  }
}
```

### 8.3 Reload Messages

```json
{
  "definitions": {
    "ReloadShader": {
      "type": "object",
      "properties": {
        "category": { "const": "reload.shader" },
        "type": { "const": "event" },
        "seq": { "type": "integer" },
        "body": {
          "type": "object",
          "properties": {
            "path": { "type": "string" },
            "variant_key": { "type": "string" },
            "status": { "enum": ["ok", "error"] },
            "error_msg": { "type": ["string", "null"] },
            "compile_time_ms": { "type": "number" }
          },
          "required": ["path", "status", "compile_time_ms"]
        }
      },
      "required": ["category", "type", "seq", "body"]
    }
  }
}
```

---

## 9. Phase Roadmap

| Phase | Task | Status |
|-------|------|--------|
| Phase 1 | `phase1-lua-dap-server` — Implement `debug.*` handlers | Pending |
| Phase 1 | `phase1-debug-profiling` — Implement `profiler.*` handlers | Pending |
| Phase 1 | `phase1-shader-hot-reload` — Implement `reload.shader` | Pending |
| Phase 2 | `phase2-script-hot-reload` — Implement `reload.script` | Pending |
| Phase 2 | `phase2-asset-hot-reload` — Implement `reload.asset` | Pending |
| Phase 2 | `phase2-vscode-dap-integration` — VSCode Debug Adapter | Pending |
| Phase 2 | `phase2-profiler-ui-panel` — VSCode Profiler WebView | Pending |
| Phase 3+ | Advanced debugging (async traces, timeline events) | Planned |

---

## Related Docs

- [debug-profiling.md](./debug-profiling.md) — Assert system, Tracy, ImGui overlay, doctest
- [crash-reporting.md](./crash-reporting.md) — Unhandled exception capture, minidump, IPC integration
- [ipc-mcp.md](../../guides/ipc-mcp.md) — IPC protocol overview, message routing, category dispatch
- [vscode-extension.md](../../guides/vscode-extension.md) — VSCode extension architecture, panel integration
