# Proposal: tools-gpu-memory-viz

## Why

GPU memory exhaustion is the single most common cause of device-lost crashes in DX12 applications, and it is almost always discovered too late. A developer adds a new 4K texture pack, a cinematics artist imports a hero character with high-res blend shapes, a level designer duplicates a scene 8 times for a stress test — and then one day on a 4GB GPU the screen goes black and D3D12 returns `DXGI_ERROR_DEVICE_REMOVED`. By this point, determining which asset caused the overrun requires hours of manual PIX heap inspection.

The `tools-gpu-memory-viz` panel makes GPU memory allocation visible in real-time, inside VSCode, without any external tools. It queries DX12 heap allocations via `ID3D12Device::GetMemoryInfo`, `IDXGIAdapter3::QueryVideoMemoryInfo`, and the engine's internal resource tracker (which already tags every allocation with a resource name and category). It renders a live VSCode WebView panel with a breakdown by category (albedo textures, normal maps, depth buffers, render targets, mesh vertex buffers, index buffers, compute buffers), a time-series graph showing VRAM usage over the last 60 seconds, and a sortable table of the top 50 allocations by size.

The long-term value of this tool is proactive budgeting. Rather than discovering VRAM overruns at the last QA pass, teams can set category budgets (e.g., "textures ≤ 1.5GB, render targets ≤ 256MB") in their project config and see real-time colored bars (green/yellow/red) in the panel throughout development. The panel becomes a permanent fixture in the developer's VSCode layout, not a tool they only open when something breaks.

## What Changes

- Engine-side resource tracker: `GpuResourceTracker` class tags every `CreateCommittedResource` and `CreatePlacedResource` call with `{name, category, size_bytes, heap_type}` — stored in a flat array, lock-free per-frame update
- IPC handler `tools.get_gpu_memory_report()` returns serialized allocation snapshot: `{total_vram_mb, budget_mb, categories: [{name, allocated_mb, budget_mb, resources: [{name, size_mb, format, dimensions}]}]}`
- IPC subscription `tools.subscribe_gpu_memory(interval_ms)` sends periodic updates for live graph
- VSCode panel `GpuMemoryVizPanel.ts`: React WebView with:
  - Category bar chart (current vs. budget, color-coded green/yellow/red)
  - 60-second time-series sparkline per category
  - Sortable resource table with columns {name, category, size_mb, format, dimensions}
  - "Snapshot" button exports JSON for offline analysis
- Budget configuration in `game.project.toml` under `[vram_budgets]` section: per-category MB limits
- Budget breach notifications: VSCode warning notification when any category exceeds 80% of budget; error notification at 100%
- Histogram auto-export: on engine shutdown, writes `vram-session-report.json` summarizing peak usage per category
- Unit tests: mock allocation tracker with synthetic resource set; assert report format and category aggregation are correct

## Impact

- Affected specs: `docs/specs/memory-system.md` (add GpuResourceTracker), `docs/specs/debug-profiling.md` (add VRAM visualization section)
- Affected code: `engine/src/renderer/gpu_resource_tracker.cpp` (new), `engine/src/ipc/handlers/tools_handlers.cpp` (add get_gpu_memory_report), `vscode-extension/src/panels/GpuMemoryVizPanel.ts` (new), `vscode-extension/src/commands/openGpuMemoryViz.ts` (new)
- Breaking change: NO (resource tracker is additive; existing allocations remain unchanged, only tracking metadata is added)
- User benefit: Makes GPU memory usage visible in real-time without external profilers; enables proactive budget enforcement throughout development; reduces device-lost crashes from VRAM oversubscription by surfacing problems days or weeks before ship
