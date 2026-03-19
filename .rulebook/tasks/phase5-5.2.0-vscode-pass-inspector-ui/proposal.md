# Proposal: VSCode Extension — Render Pass Inspector UI

## Why

The Render Pass Inspector C++ backend is complete (task 3.53.0). It exposes 10 Lua API functions (`uz.pass_inspector.*`) and CLI flags (`--vis`, `--pass-validate`, `--pass-dump`, `--pass-toggle`, `--pass-compare`). The VSCode extension needs a visual UI to make these tools accessible from the editor without typing commands.

## Prerequisites
- Task 3.53.0 (Render Pass Inspector) — COMPLETE
- VSCode extension IPC bridge — must be able to call Lua API from the extension

## What Changes

### 1. Render Passes Tree View Panel
- New "Render Passes" panel in the editor sidebar
- Hierarchical list of all registered render targets (gbuffer.*, ssgi.*, ssr.*, etc.)
- Checkbox per pass to toggle on/off (calls `uz.pass_inspector.toggle()`)
- Click to visualize (calls `uz.pass_inspector.vis()`)
- Context menu: "Dump All", "Validate", "Grid View"

### 2. Buffer Viewer Webview
- Click any pass in the tree → opens a webview panel showing the render target
- Supports zoom, pan, pixel inspector (hover shows RGBA values)
- HDR false-color toggle
- Side-by-side compare mode

### 3. Performance Bar Chart
- Bottom panel showing per-pass GPU timing as horizontal bars
- Updates each frame via `uz.pass_inspector.get_timing()`
- Click a bar to visualize that pass's output

## Impact
- Affected code: `vscode-uzengine/src/` (TypeScript)
- New files: `pass-inspector-panel.ts`, `buffer-viewer.ts`, `perf-chart.ts`
- No C++ changes needed — all backend APIs already exist
