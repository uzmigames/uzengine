# Task 5.2.0 — VSCode Extension: Render Pass Inspector UI

## Render Passes Tree View Panel
- [ ] 1.1 Create `pass-inspector-panel.ts` — TreeDataProvider for registered render targets.
- [ ] 1.2 Call `uz.pass_inspector.list_targets()` via IPC to populate the tree.
- [ ] 1.3 Checkbox per pass category (ssgi, gtao, ssr, fog, bloom, etc.) — calls `uz.pass_inspector.toggle()`.
- [ ] 1.4 Click target → calls `uz.pass_inspector.vis(target)` and updates viewport.
- [ ] 1.5 Context menu: "Dump All Passes", "Validate All", "Show Grid".
- [ ] 1.6 Auto-refresh tree on scene change or pass registration update.

## Buffer Viewer Webview
- [ ] 2.1 Create `buffer-viewer.ts` — WebviewPanel for displaying render target images.
- [ ] 2.2 Fetch buffer image via `uz.pass_inspector.dump()` or `--vis` screenshot.
- [ ] 2.3 Zoom/pan via mouse wheel + drag.
- [ ] 2.4 Pixel inspector: hover shows RGBA values at cursor position.
- [ ] 2.5 HDR false-color toggle button.
- [ ] 2.6 Side-by-side compare: split view with/without a pass.

## Performance Bar Chart
- [ ] 3.1 Create `perf-chart.ts` — WebviewPanel with horizontal bar chart.
- [ ] 3.2 Poll `uz.pass_inspector.get_timing()` via IPC each frame.
- [ ] 3.3 Render bars proportional to GPU time (ms).
- [ ] 3.4 Click bar → visualize that pass's output.
- [ ] 3.5 Color-code bars by category (geometry=blue, lighting=yellow, post=green, GI=purple).
