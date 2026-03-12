---
name: Editor/Extension Build Order Analysis for Roadmap
description: VSCode editor framework implementation order for UzEngine roadmap
type: project
---

# Editor/Extension Build Order Analysis

Analyzed docs:
- `docs/ROADMAP.md` Phase 2 section
- `docs/engine/40-editor-framework/01-overview.md`
- `docs/engine/40-editor-framework/20-editor-ipc.md`
- `docs/engine/40-editor-framework/05-hierarchy-panel.md`
- `docs/engine/40-editor-framework/04-inspector.md`
- `docs/engine/40-editor-framework/09-node-graph-framework.md`
- `docs/engine/28-ipc-protocol/01-overview.md`

---

## 1. When Should Editor Development Start?

**Answer: Incrementally, starting during Phase 1.4 (Scene System), not after full runtime.**

Rationale:
- The N-API foundation (Phase 2.0) is already partially proven by POC 0.2. The bridge works.
- The IPC protocol design can be spec'd and implemented independently of deep runtime features.
- Hierarchy and Inspector panels only need the Scene System (1.4) for their data source — they do NOT need physics, animation, audio, or any Phase 1.5 system.
- The extension scaffold (package.json, activation, commands, lifecycle) has zero runtime dependencies.
- Waiting for all of Phase 1 to complete before touching the editor adds months of unnecessary delay.

**Recommended overlap:** Begin Phase 2.0 (N-API, IPC stub, extension scaffold) in parallel with 1.4 (Scene System). The first editor panels can land immediately after 1.4 ships.

---

## 2. Editor Panel Dependency Map

```
IPC Protocol (28-ipc)
    │
    └── N-API Bridge (2.0)
            │
            ├── Viewport (2.1) ──────────────────── Requires: 1.2 RHI + 1.3.0 triangle
            │       │
            │       ├── Camera controls
            │       ├── Gizmo System ───────────── Requires: 1.4 Scene + Selection
            │       └── Selection / Raycast ──────── Requires: 1.4 Scene entities
            │
            ├── Hierarchy Panel (2.1) ─────────────── Requires: 1.4 Scene serialization
            │       │
            │       └── Selection sync to viewport
            │
            ├── Inspector Panel (2.1→2.2) ─────────── Requires: 1.4 Property System (§21)
            │       │
            │       └── Component property metadata from runtime reflection
            │
            ├── Asset Browser (2.1) ──────────────── Requires: 1.5.1 Asset Processor (useful)
            │       │                                  but workable with plain file listing at 2.1
            │       └── Asset thumbnails ──────────── Requires: 1.3.x renderer (for GPU thumbnails)
            │
            ├── Console Panel (2.1) ──────────────── Requires: only IPC + log streaming
            │       └── Zero heavy dependencies — can ship with N-API
            │
            └── Toolbar / Play-Stop (2.1) ─────────── Requires: runtime lifecycle IPC
```

**Node-Graph Editors (2.2+):**

```
BaseGraphEditor (09-node-graph-framework)
    │
    ├── Material Editor ────────────────────────── Requires: 1.5.0 Shader Compiler + 1.3 Renderer
    ├── Animation Controller ───────────────────── Requires: 1.5.1 Animation system
    ├── VFX Graph Editor ───────────────────────── Requires: 1.5.6 VFX & Particles
    ├── Behavior Tree Editor ───────────────────── Requires: 1.5.3 Navigation & AI (BT runtime)
    └── PCG Graph Editor ───────────────────────── Requires: 1.5.5 Terrain & World
```

---

## 3. Editor Panel → Runtime Milestone Matching

| Editor Panel | Earliest Possible Start | Unlocked By |
|---|---|---|
| Extension scaffold (package.json, activation) | Now | Nothing — pure TS |
| IPC stub + N-API bridge | Parallel with 1.3.0 | 1.2 RHI proven |
| Console Panel | With N-API bridge | IPC callbacks for log streaming |
| Viewport (basic render) | After 1.3.0 | Triangle to screen + HWND |
| Viewport (scene camera + orbit) | After 1.3.1 | Deferred pipeline |
| Hierarchy Panel (basic tree) | After 1.4 | Scene serialization + entity IDs |
| Inspector Panel (Bool/Int/Float/String) | After 1.4 | Property System §21 |
| Asset Browser (file listing) | After N-API | VFS listing via IPC |
| Asset Browser (thumbnails) | After 1.3.x | GPU texture rendering |
| Selection + Gizmos (translate) | After 1.4 | Scene entities + raycast |
| Undo/Redo | After 1.4 | ICommand + Scene mutations |
| **Material Editor** | After 1.5.0 (shader) | Shader compiler + 1.3 renderer |
| **Animation Controller** | After 1.5.1 | Animation system + state machines |
| **Behavior Tree Editor** | After 1.5.3 | BT runtime in Navigation & AI |
| **VFX Graph Editor** | After 1.5.6 | Particle system |
| **PCG Graph Editor** | After 1.5.5 | Terrain & World |
| Control Rig Editor | After 1.5.1 + IK | Animation system + IK |

---

## 4. Incremental vs Big-Bang Recommendation

**Recommendation: Strongly incremental, with these phases:**

### Phase 2.0 — Foundation (parallel with 1.4)
- N-API bridge: compile .node, validate Electron ABI
- IPC stub: function dispatch, error propagation, callback registration
- Extension scaffold: package.json, activation, commands, lifecycle (start/stop runtime)
- Console panel: log streaming via IPC callbacks
- Toolbar skeleton: play/pause/stop buttons wired to runtime lifecycle

**No renderer needed for these — they are pure TS + IPC.**

### Phase 2.1 — Basic Scene Editing (after 1.4)
- Viewport: HWND + swapchain + camera orbit (gizmo translate only)
- Hierarchy: tree view, single-select, expand/collapse, rename
- Inspector: Bool/Int/Float/String properties only (no color picker, no vec3 compound)
- Asset browser: file listing (no thumbnails yet)
- Selection: click-to-select in viewport, sync to hierarchy

**This is the Minimum Viable Editor. At this point you can open a scene, select entities, move them around, rename them, and see the hierarchy.**

### Phase 2.2 — Full Core Editing (after 1.5.0 shader + 1.5.1 anim)
- Inspector: complete widget set (Color, Vec3, Enum, Entity ref, Asset path)
- Hierarchy: virtual scrolling, drag-drop reparent, multi-select, context menu, search
- Asset browser: thumbnails, drag-drop to viewport
- Gizmos: rotate + scale handles
- Undo/redo: full command stack with transactions
- Node-Graph Framework (BaseGraphEditor): shared canvas, pan/zoom, pin system
- Material Editor (first specialized graph editor)
- Animation Controller Editor

### Phase 2.3 — Advanced Editors (after 1.5.3–1.5.6)
- Behavior Tree Editor (needs BT runtime from 1.5.3)
- VFX Graph Editor (needs particles from 1.5.6)
- PCG Graph Editor (needs terrain from 1.5.5)
- Sequencer/Timeline Editor (after animation + audio)
- Control Rig Editor (after IK system in 1.5.1)

---

## 5. Minimum Viable Editor Definition

The MVE is the smallest editor that enables basic 3D scene editing workflows:

**Required panels:**
1. Viewport — render the scene, orbit camera, click to select entities, translate gizmo
2. Hierarchy Panel — tree of entities, single-select, expand/collapse, rename inline, create/delete
3. Inspector Panel — show TransformComponent (position/rotation/scale as Vec3 editors), Bool/Int/Float/String properties
4. Console Panel — streaming runtime logs with level filter (Info/Warning/Error)
5. Toolbar — Play / Pause / Stop, Translate mode toggle

**Required IPC messages for MVE:**
- `scene.load` / `scene.save` / `scene.undo` / `scene.redo`
- `entity.create` / `entity.delete` / `entity.rename` / `entity.get`
- `selection.set` / `selection.get` / `on_selection_changed`
- `entity.set_transform` / `entity.get_transform`
- `viewport.set_camera` / `viewport.focus_selection`
- `editor.get_hierarchy` / `hierarchy.changed` event

**Runtime prerequisites for MVE:**
- 1.2 RHI (DX12 swapchain, HWND)
- 1.3.0 Triangle to screen (basic render loop)
- 1.4 Scene System (ECS, TransformComponent, serialization, scene file load/save)
- N-API bridge with IPC dispatch

**NOT required for MVE:**
- Physics, animation, audio, navigation, scripting
- Asset thumbnails, advanced gizmos (rotate/scale), multi-select
- Node-graph editors (material, animation, VFX)
- Asset browser (beyond basic file listing)

---

## 6. IPC Protocol — Must It Exist Before Editor Work?

**Yes, the IPC protocol contract must be agreed on before panel development, but the full implementation can be incremental.**

Specifically:

- **The message format** (JSON id/method/params/response structure from §28) must be locked first. This is the API contract between C++ runtime and TypeScript extension. Changing it mid-development is painful.
- **The N-API bridge** (function call dispatch, error propagation, callback registration) must compile and work before any panel can make real IPC calls.
- **The specific message namespaces** (scene.*, entity.*, selection.*, viewport.*) can be implemented incrementally — implement only what the current panel needs.
- **Frame streaming** (H.264 encoder/decoder) is needed before viewport streaming, but the Hierarchy/Inspector/Console panels work without it since they use the IPC call/response pattern, not frame streaming.

**Practical order:**
1. Lock message format and IPC call/response envelope
2. Build N-API bridge (ipc.call, ipc.on, error propagation)
3. Implement scene.* and entity.* handlers in C++ (needed for Hierarchy)
4. Implement selection.* handlers (needed for Hierarchy ↔ Viewport sync)
5. Add frame streaming (H.264 MF + WebCodecs) for Viewport panel
6. Add property.* handlers for Inspector
7. Add remaining message categories as panels need them

---

## 7. Node-Graph Editors — Build Order

Node-graph editors share the `BaseGraphEditor` framework (§40.09), so the framework comes first:

```
BaseGraphEditor (framework itself)
    — built at start of Phase 2.2
    — depends only on: IPC + N-API, NO runtime system dependencies
    — pure TypeScript: WebGL canvas, pan/zoom, node CRUD, serialization
    — can be developed and tested standalone before C++ compilation targets exist

Material Editor
    — first specialized editor, built on BaseGraphEditor
    — requires: 1.5.0 Shader Compiler (DXC) + 1.3 Renderer for live preview
    — compilation target: HLSL → DXIL
    — earliest: after 1.5.0 shader pipeline milestone

Animation Controller Editor
    — state machine + blend space nodes
    — requires: 1.5.1 Animation (state machine runtime)
    — earliest: after 1.5.1

VFX Graph Editor
    — emitter node types, spawn/physics/render modules
    — requires: 1.5.6 VFX & Particles
    — latest of all content editors

Behavior Tree Editor
    — composite/decorator/task nodes, blackboard
    — requires: 1.5.3 Navigation & AI (BT execution runtime)
    — after 1.5.3

PCG Graph Editor
    — generator/sampler/output nodes
    — requires: 1.5.5 Terrain & World
    — after 1.5.5
```

**Critical insight:** The `BaseGraphEditor` TypeScript framework (canvas, zoom, pan, virtual rendering, serialization, undo/redo, pin system, node registry) can be built and thoroughly tested WITHOUT any runtime compilation target. Build the canvas/UX layer first, then wire compilation backends as their runtime systems come online.

---

## 8. Revised Editor Milestone Order (Recommended)

```
1.4  Scene System completes
    │
    ├── 2.0a  Extension scaffold + IPC stub + Console panel        (parallel with late 1.4)
    │
    ├── 2.0b  N-API bridge + message dispatch + error propagation  (parallel with late 1.4)
    │
    └── 2.1a  Basic Hierarchy + Inspector (Bool/Int/Float) + MVE Viewport
              (after 1.4)

1.5.0 Shader Compiler + 1.3.x Renderer
    │
    └── 2.1b  Asset Browser (with GPU thumbnails) + Material Editor (Phase 2.2 entry)

1.5.1 Animation
    │
    ├── 2.2a  BaseGraphEditor framework (standalone TS, no C++ dependency)
    │
    └── 2.2b  Animation Controller Editor (wired to 1.5.1 state machines)

1.5.3 Navigation & AI
    └── 2.2c  Behavior Tree Editor

1.5.5 Terrain + 1.5.6 VFX
    └── 2.3a  VFX Graph Editor + PCG Graph Editor

After all Phase 1.5
    └── 2.3b  Full Scene Editor (multi-select, copy/paste, snap, grid), Sequencer, Control Rig
```

---

## Summary Table

| Question | Answer |
|---|---|
| Start editor when? | Parallel with 1.4 for scaffold/IPC; panels after 1.4 ships |
| IPC before editor? | Yes — message format must be locked; implementation incremental |
| Minimum viable editor? | Viewport + Hierarchy + Inspector (basic) + Console + Toolbar |
| MVE requires? | 1.2 RHI + 1.3.0 Triangle + 1.4 Scene + N-API bridge |
| Node-graph framework when? | BaseGraphEditor: early 2.2 (TS-only, no runtime deps). Material editor after 1.5.0 shader compiler |
| Material editor when? | After 1.5.0 shader compiler |
| Animation editor when? | After 1.5.1 animation system |
| VFX/BT/PCG editors when? | After their respective 1.5.x runtime systems |
| Incremental or big-bang? | **Incremental** — MVE ships right after 1.4, advanced editors trail their runtime milestones |
