# Proposal: VSCode Extension N-API Foundation (Phase 2.0)

## Why

The UzEngine runtime is a standalone executable with no programmatic API for external tools. The VSCode extension loads a `.node` addon that exposes only 4 stub functions (`getVersion`, `getBackend`, `init`, `shutdown`) — none of which create a GPU device, render a frame, or load a scene. This blocks the entire editor experience: viewport preview, scene tree, inspector, asset browser, and AI-assisted editing (MCP) all depend on the extension being able to drive the runtime.

POC 0.2 proved the architecture: N-API in-process addon achieves 120 FPS H.264 streaming and 6069 FPS native HWND rendering. The foundation is validated — what's missing is wiring the real runtime (DX12 device, SceneRenderer, ECS World, shader pipeline) into the C API and N-API bridge.

## What Changes

### Phase A: Runtime C API Expansion
Expand `runtime/engine/app/runtime.h` from 4 stubs to ~20 functions covering:
1. **Viewport lifecycle** — create/destroy/resize viewport with DX12 device + swapchain + SceneRenderer
2. **Frame rendering** — render one frame, return stats (fps, draw calls, triangles)
3. **Scene management** — load/save scenes, query entity hierarchy as JSON
4. **Entity/Component access** — create/destroy entities, get/set component properties as JSON
5. **Input forwarding** — mouse move/button/scroll, keyboard events for camera + gizmo
6. **Log streaming** — callback registration for UZ_LOG messages
7. **Screenshot capture** — readback current frame as PNG buffer

### Phase B: N-API Bridge Expansion
Expand `napi/src/addon.cpp` from 4 exports to full surface:
1. Async wrappers (Promises) for heavy ops (scene load, screenshot)
2. Sync wrappers for lightweight ops (input, resize)
3. TypeScript declaration file with full type safety
4. Typed array transfer for frame/screenshot buffers (zero-copy)
5. Callback registration for log streaming and hot-reload

### Phase C: Extension Integration
1. Viewport WebView panel with HWND embedding
2. Scene tree panel (entity hierarchy)
3. Console panel (log streaming)
4. Engine Mode layout toggle (Ctrl+Shift+E)
5. File association: `.uzscene` double-click opens viewport

### Phase D: Build Pipeline
1. Root `npm run build`: zig → .lib → cmake-js/MSVC → .node → tsc
2. `preBuild` pool shutdown to release file locks

## Impact
- Affected specs: `docs/specs/2.0-napi-foundation.md`, `docs/engine/28-ipc-protocol/*`, `docs/engine/06-build-toolchain/04-napi-bridge.md`
- Affected code:
  - MODIFY: `runtime/engine/app/runtime.h/.cpp` (C API: 4 → ~20 functions)
  - MODIFY: `napi/src/addon.cpp` (N-API: 4 → ~20 exports)
  - MODIFY: `extension/src/extension.ts` (viewport, scene, console)
  - NEW: `extension/src/uzengine.d.ts` (TypeScript declarations)
  - NEW: `extension/src/panels/viewport-panel.ts`
  - NEW: `extension/src/panels/scene-tree-panel.ts`
  - NEW: `extension/src/panels/console-panel.ts`
  - NEW: `extension/src/engine-mode.ts`
- Breaking change: NO — existing 4-function API preserved
- User benefit: Open `.uzscene` in VSCode → live 3D viewport + entity hierarchy + component inspector
- Enables: Phase 2.1 (Viewport & Core UI), Phase 3.0 (MCP), AI-assisted editing
