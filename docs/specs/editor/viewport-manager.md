# Spec: Viewport Manager

> **Status**: Phase 1 (Required before any editor rendering feature)
> **Owner**: VSCode Extension (`vscode-uzengine/src/`) + Engine IPC (`src/ipc/`)
> **Purpose**: On-demand lifecycle management of render viewports — open, close, suspend, and resume based on VSCode tab visibility. Prevents idle viewports from consuming GPU resources on the developer's machine.
> **Priority**: P0 for editor robustness — multiple idle viewports will saturate the GPU and cause system-wide performance degradation.

---

## Problem Statement

The engine renders at full frame rate regardless of whether the viewport panel is visible. With multiple editor features sharing the JPEG streaming pipeline (Scene viewport, VFX preview, Material preview, Mesh preview, Texture viewer), each idle tab consumes:

- **GPU time**: Full frame render pass even when hidden
- **CPU time**: JPEG encode thread running continuously
- **Network bandwidth**: WebSocket frames sent to a hidden webview

On a developer workstation with 4 open preview tabs, each idle tab could consume 10–30% GPU utilization. This causes thermal throttling, fan noise, increased power usage, and stutters in the active tab.

**Solution**: The VSCode extension notifies the engine whenever a viewport tab becomes visible or hidden. The engine suspends rendering for hidden viewports and resumes immediately when they become visible again.

---

## Architecture

```
VSCode Extension (TypeScript)
│
├── ViewportManager                     ← Singleton, tracks all open viewports
│   ├── ViewportPanel (per tab)
│   │   ├── WebviewPanel                ← VSCode native tab
│   │   ├── ViewportState               ← active | suspended | closed
│   │   └── IPC channel (per viewport)  ← unique viewport_id
│   └── EngineClient                    ← sends IPC to engine
│
│  Tab visibility events:
│  onDidChangeViewState → active/inactive → IPC: viewport.setActive(id, bool)
│
▼
UzEngine.exe (C++)
│
├── ViewportRegistry                    ← tracks N viewports
│   ├── Viewport {id, state, surface}
│   │   ├── RenderSurface               ← per-viewport render targets
│   │   ├── FramePipeline               ← render graph instance per viewport
│   │   └── JpegStream                  ← WebSocket sender (suspended when inactive)
│   └── RenderScheduler                 ← only renders active viewports
│
└── IPC handlers:
    ├── viewport.create(config)         → ViewportId
    ├── viewport.destroy(id)
    ├── viewport.setActive(id, active)  → suspend/resume render + stream
    ├── viewport.resize(id, w, h)
    └── viewport.getStatus(id)          → fps, state, memory_mb
```

---

## Viewport Types

Each editor feature that needs a render window creates its own viewport type:

| Type | Created by | Purpose |
|------|-----------|---------|
| `scene` | Scene editor panel | Main 3D world view |
| `vfx_preview` | VFX node editor | Isolated VFX effect preview |
| `material_preview` | Material inspector | Sphere/plane with material applied |
| `mesh_preview` | Asset browser | Mesh thumbnail + detail view |
| `texture_preview` | Asset browser | Texture viewer (mips, channels) |
| `prefab_preview` | Prefab editor | Isolated prefab viewport |
| `animation_preview` | Animation editor | Character + animation clip |

All types share the same lifecycle protocol — they only differ in what the engine renders inside them.

---

## Engine Side: C Implementation

### ViewportId

```c
// src/engine/renderer/viewport_manager.h
typedef uint32_t ViewportId;
#define VIEWPORT_ID_INVALID 0
```

### Viewport State Machine

```
                  viewport.create()
                       │
                       ▼
              ┌─────INACTIVE──────┐
              │  (render paused,  │
              │   stream stopped) │
              └────────┬──────────┘
                       │ viewport.setActive(true)
                       ▼
              ┌──────ACTIVE───────┐
              │  (rendering at    │◄─┐
              │   target FPS,     │  │ viewport.setActive(true)
              │   JPEG streaming) │  │   (tab regains focus)
              └────────┬──────────┘  │
                       │             │
          viewport.setActive(false)  │
          (tab hidden / unfocused)   │
                       │             │
                       ▼             │
              ┌────SUSPENDED──────┐  │
              │  (last frame kept,│──┘
              │   0% GPU/CPU use) │
              └────────┬──────────┘
                       │ viewport.destroy()
                       ▼
                   DESTROYED
                (resources freed)
```

### ViewportDesc & ViewportState

```c
typedef enum ViewportState {
    VIEWPORT_STATE_INACTIVE   = 0, // Created but never activated
    VIEWPORT_STATE_ACTIVE     = 1, // Rendering at full rate
    VIEWPORT_STATE_SUSPENDED  = 2, // Hidden — render paused
    VIEWPORT_STATE_DESTROYED  = 3, // Resources freed
} ViewportState;

typedef enum ViewportType {
    VIEWPORT_TYPE_SCENE         = 0,
    VIEWPORT_TYPE_VFX_PREVIEW   = 1,
    VIEWPORT_TYPE_MATERIAL      = 2,
    VIEWPORT_TYPE_MESH          = 3,
    VIEWPORT_TYPE_TEXTURE       = 4,
    VIEWPORT_TYPE_PREFAB        = 5,
    VIEWPORT_TYPE_ANIMATION     = 6,
} ViewportType;

typedef struct ViewportDesc {
    ViewportType type;
    uint32_t     width, height;      // Initial resolution
    uint32_t     target_fps;         // 0 = uncapped; typical = 60 for scene, 30 for previews
    uint32_t     jpeg_quality;       // 75–95 (lower = less bandwidth)
    const char  *debug_name;         // e.g. "Scene Viewport", "VFX Preview: explosion"
} ViewportDesc;

typedef struct Viewport {
    ViewportId    id;
    ViewportState state;
    ViewportDesc  desc;

    // Render resources (allocated on create, freed on destroy)
    RhiTexture    color_target;      // RGBA8 render target
    RhiTexture    depth_target;      // D32 depth buffer
    RenderGraph  *render_graph;      // Per-viewport render graph instance

    // Streaming
    uint32_t      ws_client_id;      // WebSocket client ID (0 = none connected)
    uint64_t      frame_count;       // Frames rendered since activation
    double        last_active_time;  // Timestamp of last setActive(true) call
    double        fps_actual;        // Measured FPS (EMA over 30 frames)

    // Memory budget
    uint64_t      vram_bytes;        // Approx VRAM used by this viewport's targets
} Viewport;
```

### ViewportRegistry API

```c
// src/engine/renderer/viewport_manager.h

// Create a viewport — allocates render targets, returns unique ID.
// Call before any rendering or streaming.
ViewportId viewport_create(const ViewportDesc *desc);

// Destroy viewport and free all GPU resources.
// Safe to call while suspended. Notifies any connected WebSocket client.
void viewport_destroy(ViewportId id);

// Activate or suspend a viewport.
//   active = true  → start rendering at desc.target_fps, resume JPEG streaming
//   active = false → stop rendering after current frame, pause JPEG stream
//                    (last rendered frame stays visible in the webview)
void viewport_set_active(ViewportId id, bool active);

// Resize viewport render targets. Can be called while active or suspended.
// If active: takes effect on next frame. If suspended: deferred until next activation.
void viewport_resize(ViewportId id, uint32_t width, uint32_t height);

// Returns a snapshot of current viewport metrics.
typedef struct ViewportStatus {
    ViewportId    id;
    ViewportState state;
    float         fps;
    uint64_t      vram_bytes;
    uint32_t      width, height;
} ViewportStatus;
ViewportStatus viewport_get_status(ViewportId id);

// Called once per engine frame — renders all ACTIVE viewports in sequence.
// Suspended/inactive viewports are skipped entirely (zero GPU cost).
void viewport_registry_tick(float dt);

// Enumerate all viewports (for debug UI / IPC status queries).
uint32_t viewport_registry_count(void);
ViewportId viewport_registry_get(uint32_t index);
```

### Frame Scheduling Logic

```c
// src/engine/renderer/viewport_manager.c

void viewport_registry_tick(float dt) {
    for (uint32_t i = 0; i < g_viewport_count; i++) {
        Viewport *vp = &g_viewports[i];

        if (vp->state != VIEWPORT_STATE_ACTIVE) {
            continue; // Zero GPU cost for suspended/inactive viewports
        }

        // Frame rate cap (optional — 0 = uncapped)
        if (vp->desc.target_fps > 0) {
            vp->frame_accum += dt;
            float frame_dt = 1.0f / (float)vp->desc.target_fps;
            if (vp->frame_accum < frame_dt) continue;
            vp->frame_accum -= frame_dt;
        }

        // 1. Begin RHI command list for this viewport
        RhiCmdList cmd = rhi()->cmd_begin();

        // 2. Transition render target to RENDER_TARGET state
        rhi()->cmd_barrier_texture(cmd, vp->color_target,
            RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

        // 3. Execute render graph for this viewport type
        viewport_render(vp, cmd, dt);

        // 4. Transition to COPY_SRC for readback
        rhi()->cmd_barrier_texture(cmd, vp->color_target,
            RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);

        // 5. Submit and encode JPEG
        rhi()->cmd_submit(cmd);
        viewport_encode_and_stream(vp);

        vp->frame_count++;
    }
}

// Encode the rendered frame as JPEG and push to the WebSocket client.
// Called on render thread; JPEG encode happens on dedicated encode thread (ring buffer).
static void viewport_encode_and_stream(Viewport *vp) {
    if (vp->ws_client_id == 0) return; // No client connected — skip encode

    FrameData frame = {
        .viewport_id = vp->id,
        .texture     = vp->color_target,
        .width       = vp->desc.width,
        .height      = vp->desc.height,
        .quality     = vp->desc.jpeg_quality,
    };
    jpeg_stream_push(&g_jpeg_stream, frame); // Ring buffer, drop-oldest on overflow
}
```

---

## IPC Protocol

All viewport operations use the existing file-based IPC (JSON command/response). New handlers:

### `viewport.create`

```json
// Request
{
  "id": "cmd-001",
  "function": "viewport.create",
  "args": {
    "type": "scene",
    "width": 1280,
    "height": 720,
    "target_fps": 60,
    "jpeg_quality": 85,
    "debug_name": "Scene Viewport"
  }
}

// Response
{
  "id": "cmd-001",
  "ok": true,
  "result": {
    "viewport_id": 1,
    "ws_port": 9002,        // WebSocket port for this viewport's stream
    "vram_bytes": 7340032   // ~7 MB for 1280×720 RGBA8 + D32
  }
}
```

### `viewport.destroy`

```json
// Request
{ "id": "cmd-002", "function": "viewport.destroy", "args": { "viewport_id": 1 } }

// Response
{ "id": "cmd-002", "ok": true, "result": {} }
```

### `viewport.setActive`

```json
// Request — tab becomes visible
{
  "id": "cmd-003",
  "function": "viewport.setActive",
  "args": { "viewport_id": 1, "active": true }
}

// Request — tab hidden
{
  "id": "cmd-004",
  "function": "viewport.setActive",
  "args": { "viewport_id": 1, "active": false }
}

// Response (both cases)
{ "id": "cmd-003", "ok": true, "result": { "state": "active" } }
{ "id": "cmd-004", "ok": true, "result": { "state": "suspended" } }
```

### `viewport.resize`

```json
{
  "id": "cmd-005",
  "function": "viewport.resize",
  "args": { "viewport_id": 1, "width": 1920, "height": 1080 }
}
```

### `viewport.getStatus`

```json
// Response
{
  "id": "cmd-006",
  "ok": true,
  "result": {
    "viewport_id": 1,
    "state": "active",
    "fps": 59.8,
    "vram_bytes": 7340032,
    "width": 1280,
    "height": 720
  }
}
```

### Status Push (engine → extension, no polling needed)

The engine pushes viewport state changes proactively via WebSocket on a dedicated **control channel** (separate from the JPEG stream):

```json
// Sent when viewport suspends due to engine-side resource pressure
{ "type": "viewport.suspended", "viewport_id": 1, "reason": "resource_pressure" }

// Sent when viewport is destroyed
{ "type": "viewport.destroyed", "viewport_id": 1 }

// Sent once per second per active viewport (replaces global status.json for multi-viewport)
{ "type": "viewport.status", "viewport_id": 1, "fps": 59.8, "vram_bytes": 7340032 }
```

---

## WebSocket Architecture (Multi-Viewport)

With multiple viewports, each viewport gets its own **multiplexed stream** over a shared WebSocket connection. Every JPEG frame is prefixed with a 4-byte viewport ID header:

```
[4 bytes: viewport_id (uint32 little-endian)] [N bytes: JPEG data]
```

The VSCode extension demultiplexes frames by reading the prefix:

```typescript
// vscode-uzengine/src/stream-client.ts
ws.onmessage = (event: MessageEvent) => {
    const data = event.data as ArrayBuffer;
    const view = new DataView(data);
    const viewportId = view.getUint32(0, true); // little-endian
    const jpeg = data.slice(4);

    const panel = viewportManager.getPanel(viewportId);
    if (panel) {
        panel.postMessage({ type: 'frame', jpeg: jpeg });
    }
};
```

This allows a single WebSocket connection to stream multiple viewports simultaneously without port conflicts.

---

## VSCode Extension Side: TypeScript Implementation

### ViewportPanel (per tab)

```typescript
// vscode-uzengine/src/viewport-panel.ts

export type ViewportType =
    'scene' | 'vfx_preview' | 'material' | 'mesh' | 'texture' | 'prefab' | 'animation';

export interface ViewportConfig {
    type: ViewportType;
    title: string;
    targetFps: number;
    jpegQuality: number;
}

export class ViewportPanel implements vscode.Disposable {
    private panel: vscode.WebviewPanel;
    private viewportId: number = 0;
    private state: 'inactive' | 'active' | 'suspended' = 'inactive';

    constructor(
        private readonly config: ViewportConfig,
        private readonly manager: ViewportManager,
        context: vscode.ExtensionContext
    ) {
        this.panel = vscode.window.createWebviewPanel(
            `uzengine.viewport.${config.type}`,
            config.title,
            vscode.ViewColumn.One,
            { enableScripts: true, retainContextWhenHidden: true }
        );

        // Tab visibility → engine suspend/resume
        this.panel.onDidChangeViewState(e => {
            if (e.webviewPanel.visible) {
                this.resume();
            } else {
                this.suspend();
            }
        });

        // Tab closed → destroy viewport + free GPU resources
        this.panel.onDidDispose(() => {
            this.destroy();
        });
    }

    async initialize(width: number, height: number): Promise<void> {
        const result = await this.manager.engineClient.call('viewport.create', {
            type: this.config.type,
            width,
            height,
            target_fps: this.config.targetFps,
            jpeg_quality: this.config.jpegQuality,
            debug_name: this.config.title,
        });
        this.viewportId = result.viewport_id;
        this.panel.webview.html = this.buildHtml(result.ws_port, this.viewportId);
    }

    private async resume(): Promise<void> {
        if (this.state === 'active' || this.viewportId === 0) return;
        await this.manager.engineClient.call('viewport.setActive', {
            viewport_id: this.viewportId,
            active: true,
        });
        this.state = 'active';
    }

    private async suspend(): Promise<void> {
        if (this.state === 'suspended' || this.viewportId === 0) return;
        await this.manager.engineClient.call('viewport.setActive', {
            viewport_id: this.viewportId,
            active: false,
        });
        this.state = 'suspended';
    }

    private async destroy(): Promise<void> {
        if (this.viewportId !== 0) {
            await this.manager.engineClient.call('viewport.destroy', {
                viewport_id: this.viewportId,
            });
            this.viewportId = 0;
        }
        this.state = 'inactive';
        this.manager.onPanelDisposed(this);
    }

    dispose(): void {
        this.panel.dispose();
    }
}
```

### ViewportManager (singleton)

```typescript
// vscode-uzengine/src/viewport-manager.ts

export class ViewportManager implements vscode.Disposable {
    private panels = new Map<number, ViewportPanel>(); // viewportId → panel
    readonly engineClient: EngineClient;

    constructor(context: vscode.ExtensionContext, engineClient: EngineClient) {
        this.engineClient = engineClient;
    }

    async openScene(): Promise<ViewportPanel> {
        return this.openViewport({
            type: 'scene',
            title: 'Scene',
            targetFps: 60,
            jpegQuality: 85,
        });
    }

    async openVfxPreview(effectName: string): Promise<ViewportPanel> {
        return this.openViewport({
            type: 'vfx_preview',
            title: `VFX: ${effectName}`,
            targetFps: 30,    // Lower FPS for previews = less GPU pressure
            jpegQuality: 75,
        });
    }

    async openMeshPreview(meshName: string): Promise<ViewportPanel> {
        return this.openViewport({
            type: 'mesh',
            title: `Mesh: ${meshName}`,
            targetFps: 30,
            jpegQuality: 75,
        });
    }

    private async openViewport(config: ViewportConfig): Promise<ViewportPanel> {
        const panel = new ViewportPanel(config, this, this.context);
        await panel.initialize(1280, 720);
        this.panels.set(panel.viewportId, panel);
        return panel;
    }

    getPanel(viewportId: number): ViewportPanel | undefined {
        return this.panels.get(viewportId);
    }

    onPanelDisposed(panel: ViewportPanel): void {
        this.panels.delete(panel.viewportId);
    }

    // Suspend ALL viewports (e.g., when VSCode loses focus entirely)
    async suspendAll(): Promise<void> {
        for (const panel of this.panels.values()) {
            await panel.suspend();
        }
    }

    dispose(): void {
        for (const panel of this.panels.values()) {
            panel.dispose();
        }
        this.panels.clear();
    }
}
```

---

## Resource Budget & Auto-Suspend Policy

The engine enforces a **VRAM budget** and an **active viewport cap** to protect low-end developer machines:

### VRAM Budget

```c
// src/engine/renderer/viewport_manager.c

#define VIEWPORT_VRAM_BUDGET_BYTES  (512ULL * 1024 * 1024)  // 512 MB default
#define VIEWPORT_MAX_ACTIVE         4                        // Max simultaneous active viewports

// Called in viewport_set_active(id, true):
static bool viewport_can_activate(ViewportId id) {
    uint32_t active_count = 0;
    uint64_t active_vram  = 0;

    for (uint32_t i = 0; i < g_viewport_count; i++) {
        if (g_viewports[i].state == VIEWPORT_STATE_ACTIVE &&
            g_viewports[i].id != id) {
            active_count++;
            active_vram += g_viewports[i].vram_bytes;
        }
    }

    Viewport *vp = viewport_find(id);
    if (!vp) return false;

    if (active_count >= VIEWPORT_MAX_ACTIVE) {
        LOG_WARN("ViewportManager: max active viewports (%d) reached — "
                 "suspending to activate %s",
                 VIEWPORT_MAX_ACTIVE, vp->desc.debug_name);
        // Suspend the oldest-activated viewport to make room
        viewport_suspend_oldest();
    }

    if (active_vram + vp->vram_bytes > g_viewport_vram_budget) {
        LOG_WARN("ViewportManager: VRAM budget exceeded — rejecting activation of %s",
                 vp->desc.debug_name);
        return false;
    }

    return true;
}
```

### Budget Configuration (`.uzproj`)

```toml
[editor.viewports]
vram_budget_mb   = 512     # Total VRAM allocated to all viewports
max_active       = 4       # Max simultaneously rendering viewports
default_fps      = 60      # Default for scene viewports
preview_fps      = 30      # Default for asset preview viewports
jpeg_quality     = 85      # Default JPEG quality (75–95)
```

---

## Window Focus Optimization

When the VSCode window loses focus (developer switches to another app), all viewports should suspend automatically to save GPU resources while not in use:

### VSCode Extension

```typescript
// vscode-uzengine/src/extension.ts

vscode.window.onDidChangeWindowState(state => {
    if (!state.focused) {
        viewportManager.suspendAll();
    }
    // Re-activation is handled by individual panel onDidChangeViewState events
    // when the developer clicks back into VSCode tabs
});
```

### Engine Watchdog

The engine also implements a **heartbeat watchdog**: if no IPC command is received for 30 seconds, all viewports auto-suspend. This handles cases where the VSCode extension crashes or the WebSocket disconnects without sending `viewport.setActive(false)`.

```c
// src/engine/renderer/viewport_manager.c
#define VIEWPORT_HEARTBEAT_TIMEOUT_S 30.0

static void viewport_watchdog_tick(double current_time) {
    if (current_time - g_last_ipc_time > VIEWPORT_HEARTBEAT_TIMEOUT_S) {
        LOG_WARN("ViewportManager: IPC heartbeat timeout — suspending all active viewports");
        for (uint32_t i = 0; i < g_viewport_count; i++) {
            if (g_viewports[i].state == VIEWPORT_STATE_ACTIVE) {
                viewport_set_active(g_viewports[i].id, false);
            }
        }
    }
}
```

---

## File Structure

```
src/engine/renderer/
├── viewport_manager.h      ← ViewportId, ViewportDesc, ViewportState, all C API
├── viewport_manager.c      ← Registry, scheduling loop, VRAM budget enforcement
└── viewport_stream.c       ← JPEG encode + WebSocket multiplex per viewport

vscode-uzengine/src/
├── viewport-manager.ts     ← ViewportManager singleton
├── viewport-panel.ts       ← ViewportPanel (one per VSCode tab)
├── stream-client.ts        ← WebSocket client, viewport_id demux (replaces stream-panel.ts)
└── engine-client.ts        ← IPC call wrapper (file-based JSON, wraps existing IpcPoller)
```

---

## Migration: Current → Multi-Viewport

The existing `stream-panel.ts` is a single-viewport implementation. Migration path:

1. **Phase 1**: Wrap existing behavior — current single viewport becomes `viewport_id = 1` (scene viewport). `stream-panel.ts` adapted to use `ViewportPanel`. Backward compatible — no engine changes yet except adding `viewport_id` prefix to WebSocket frames.

2. **Phase 1 (engine)**: Add `ViewportRegistry` with single viewport hard-coded. Add `viewport.create/destroy/setActive` IPC handlers. Implement `viewport_registry_tick()` replacing the raw render loop.

3. **Phase 2+**: Add preview viewport types as other editor panels are implemented (VFX editor, Material inspector, Asset browser mesh preview).

---

## Definition of Done

### Engine
- [ ] `viewport_manager.h/c` — `viewport_create`, `viewport_destroy`, `viewport_set_active`, `viewport_resize`, `viewport_get_status`, `viewport_registry_tick`
- [ ] `viewport.create/destroy/setActive/resize/getStatus` IPC handlers registered
- [ ] WebSocket frames prefixed with 4-byte `viewport_id` header
- [ ] `VIEWPORT_MAX_ACTIVE` cap enforced with oldest-viewport eviction
- [ ] VRAM budget tracked per viewport
- [ ] Heartbeat watchdog: auto-suspend all viewports after 30s IPC silence
- [ ] `VIEWPORT_STATE_SUSPENDED` renders zero frames (verified by frame counter)
- [ ] Status push via WebSocket control channel (viewport.status every 1s per active viewport)

### VSCode Extension
- [ ] `ViewportPanel` — create/suspend/resume/destroy lifecycle tied to `onDidChangeViewState`
- [ ] `ViewportManager` — singleton, manages all open panels
- [ ] `onDidChangeWindowState` — `suspendAll()` when VSCode loses focus
- [ ] WebSocket `stream-client.ts` — demultiplexes by `viewport_id` prefix
- [ ] Panel `dispose` → `viewport.destroy` IPC call (no zombie viewports on tab close)
- [ ] Resize notification on `onDidChangeViewState` with new panel dimensions

### Tests
- [ ] Unit: `viewport_create` allocates correct VRAM (width × height × 8 bytes for RGBA8 + D32)
- [ ] Unit: `viewport_set_active(false)` stops frame counter increment
- [ ] Unit: VRAM budget enforcement — 5th viewport with budget full returns error
- [ ] Integration: tab close → viewport.destroy → VRAM freed within 1 frame
- [ ] Integration: VSCode focus lost → all viewports suspended → focus restored → active tab resumes
