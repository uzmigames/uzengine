# Proposal: phase2-ui-profiler-devtools

## Why
The HTML/CSS game UI system (uz_ui_html.dll) currently provides no visibility into rendering performance. Slow CSS animations, excessive DOM node counts, and expensive layout recalculations are invisible to developers until they manifest as frame drops in shipped builds. Without per-frame CPU/GPU timing data and an element tree inspector, there is no way to diagnose or attribute UI-related performance regressions during development.

## What Changes
- Add CPU timing capture to UIPass: measure layout, paint, and composite phases per frame using QueryPerformanceCounter
- Add GPU timing capture to UIPass using DX12 timestamp queries on the UI render pass
- Track element count statistics per frame (total nodes, visible nodes, animated nodes)
- Expose new IPC primitive `ui.get_profiler_stats` returning a JSON snapshot of timing and element metrics
- Add a VSCode "UI DevTools" panel that polls `ui.get_profiler_stats` and displays a render time breakdown chart (layout/paint/composite)
- Add an element tree inspector in the VSCode panel showing the live DOM tree with per-node layout bounds and paint cost estimates
- Integrate profiler stats into the existing debug overlay system so in-engine overlays can display UI timing alongside GPU frame time

## Impact
- Affected specs: `docs/specs/ui-system.md`, `docs/specs/debug-profiling.md`
- Affected code: `src/ui/UIPass.cpp`, `src/ui/uz_ui_html.dll`, `src/ipc/IpcServer.cpp`, `vscode-ext/src/panels/UIDevToolsPanel.ts`
- Breaking change: NO
- User benefit: Developers can pinpoint exactly which CSS animations, DOM structures, or layout patterns are causing frame drops, reducing UI performance debugging from hours of guesswork to a targeted 5-minute workflow.
