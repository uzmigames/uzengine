# Internal Rendering Diagnostics — Industry Survey

<!-- doc-meta
title: Internal Rendering Diagnostics — Industry Survey
category: research
engine: multi-engine
theme: rendering
created: 2026-03-30
updated: 2026-03-30
status: active
author: research-agent
-->

## Overview

Survey of how major game engines (UE5, Unity, Godot, O3DE, Frostbite, Wicked Engine) implement
**self-contained** rendering diagnostics — without relying on external tools like RenderDoc or PIX.
Covers GPU timestamp profiling, render-target dumping, shader-side debug output, breadcrumb markers,
and in-engine debug overlays.

## Context

Collected to inform UzEngine's internal diagnostic layer. Current tooling (`--shadow-debug 1`,
RenderDoc integration) is useful but depends on external processes or manual frame capture. This
survey identifies what engines bake directly into their runtime for always-available, low-friction
debugging.

---

## Technique 1 — GPU Timestamp Query Profiling (stat gpu / GPU Visualizer)

### Reference Implementation: UE5

UE5 calls this system `FRealtimeGPUProfiler`. Every render pass is wrapped in
`RDG_GPU_STAT_SCOPE(GraphBuilder, PassName)` macros. Those macros issue DX12/Vulkan timestamp
queries at the start and end of each scope. A ring buffer of query heaps (typically 3–4 frames
deep) is maintained per command queue. At the end of each frame, `ResolveQueryData` copies heap
data into a CPU-accessible readback buffer. Results are read back N frames later (matching the
ring depth) to avoid GPU stalls.

**Triggering**: `stat gpu` console command (always-on, text overlay) or `ProfileGPU` / Ctrl+Shift+,
(one-shot GPU Visualizer window, hierarchical bar chart). In shipped builds without the editor,
results are written to the log instead of the UI.

**Output format**: On-screen text list of pass names + millisecond durations. GPU Visualizer shows
a full hierarchical tree (BasePass, Shadows, Translucency, etc.) with aggregate totals.

**Five instrumentation levels in UE5's RDG**:
1. `SCOPED_NAMED_EVENT` — CPU timeline only (visible in Unreal Insights thread panel)
2. `SCOPE_CYCLE_COUNTER` — CPU `stat` counters
3. `RDG_EVENT_SCOPE` — GPU marker consumed by RenderDoc, PIX, Snapdragon Profiler, RDG Insights
4. `RDG_GPU_STAT_SCOPE` — feeds `stat gpu` and `ProfileGPU` overlays
5. `RDG_CSV_STAT_EXCLUSIVE_SCOPE` — writes per-pass CSV data for automated perf regression

**Performance cost**: Timestamp queries add ~0.01–0.1 ms overhead on DX12. Ring buffer readback
introduces N-frame display latency (not execution latency).

**Does it require shader modification?** No. Pure CPU-side timestamp insertion.

### DX12 API Pattern (canonical)

```cpp
// 1. Create query heap (once, at engine init)
D3D12_QUERY_HEAP_DESC heapDesc = {};
heapDesc.Type  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
heapDesc.Count = MAX_TIMESTAMPS * FRAMES_IN_FLIGHT;  // ring buffer
device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_queryHeap));

// 2. Create readback buffer (CPU-accessible)
// D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_DIMENSION_BUFFER
// size = sizeof(UINT64) * MAX_TIMESTAMPS * FRAMES_IN_FLIGHT

// 3. Per-pass bracketing (insert before + after each pass)
UINT queryIdx = AllocTimestampSlot();  // ring index
cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIdx * 2 + 0);
// ... pass work ...
cmdList->EndQuery(m_queryHeap, D3D12_QUERY_TYPE_TIMESTAMP, queryIdx * 2 + 1);

// 4. Per-frame resolve (after all passes, before Present)
cmdList->ResolveQueryData(m_queryHeap,
    D3D12_QUERY_TYPE_TIMESTAMP, 0, usedSlots * 2,
    m_readbackBuffer, frameOffset * sizeof(UINT64) * usedSlots * 2);

// 5. CPU readback (N frames later, inside fence-gated code)
UINT64* data = nullptr;
m_readbackBuffer->Map(0, nullptr, (void**)&data);
UINT64 freq;
m_cmdQueue->GetTimestampFrequency(&freq);
double ms = double(data[end] - data[start]) / double(freq) * 1000.0;
m_readbackBuffer->Unmap(0, nullptr);
```

Note: DX12 uses only `EndQuery` for timestamps (no `BeginQuery` counterpart).
Only `EndQuery` is called; each call captures an instantaneous clock reading.

---

## Technique 2 — RDG Debug Modes and DumpGPU (UE5)

### Console Variables

| Variable | Effect |
|---|---|
| `r.RDG.Debug=1` | CPU-intensive warnings about inefficiencies; formats draw event names even without frame events active |
| `r.RDG.Debug=2` | Adds additional validation output |
| `r.RDG.ImmediateMode=1` | Executes passes sequentially during creation — simplifies crash debugging from pass wiring logic |
| `DumpGPU` | One-time command: dumps all RDG intermediate textures and buffers for the next frame |

### DumpGPU Mechanism

When `DumpGPU` is issued, UE5's `FRDGBuilder::Execute()` adds a read-back copy pass after each
registered RDG texture and buffer. Contents are copied to `UPLOAD`/`READBACK` heaps and written to
`Saved/GPUDumps/`. Output is a self-contained HTML viewer + raw binary files for every resource,
every pass.

**Triggering**: One-shot console command. No persistent overhead. Single frame only.

**Output format**: Directory `Saved/GPUDumps/<timestamp>/`, containing:
- `viewer.html` — standalone HTML/JS app that lets you browse all resources visually
- One binary file per RDG resource (named by pass + resource name)
- Launcher scripts (`.bat`/`.sh`) to open the viewer

**Shader modification required?** No. RDG inserts the copy passes transparently.

**Performance cost on triggered frame**: Significant — one extra copy per resource. Normal frames
have zero cost.

---

## Technique 3 — Buffer Visualization / GBuffer Dump (UE5)

### Console Variables

| Variable | Effect |
|---|---|
| `r.BufferVisualizationTarget <name>` | Renders a single GBuffer channel to the viewport (Albedo, Normal, etc.) |
| `r.BufferVisualizationDumpBaseFilename <path>` | Dumps all visualization targets to disk as PNG/EXR |
| `r.BufferVisualizationOverviewTargets <list>` | Controls which buffers appear in Overview mode |

### Mechanism

The Buffer Visualization pass runs as a post-process. When a dump is requested, UE5 schedules a
readback of the resolved visualization texture. The readback result is compressed and written to
disk by the game thread. Format depends on the HDR flag: 8-bit PNG for LDR buffers, 16-bit EXR
for HDR (SceneColor, Depth, etc.).

**Triggering**: Console variable (persistent while set) or one-shot via movie capture mode.

**Output format**: Files under the path set by `r.BufferVisualizationDumpBaseFilename`.

**Shader modification required?** No. The visualization is a post-process that samples the GBuffer.

---

## Technique 4 — DRED Auto-Breadcrumbs (DX12 built-in)

Used for post-crash GPU fault diagnosis, not live profiling. Answers: "which pass was executing
when the GPU TDR'd?"

### Enabling

```cpp
// Must be called BEFORE device creation
CComPtr<ID3D12DeviceRemovedExtendedDataSettings> pDredSettings;
D3D12GetDebugInterface(IID_PPV_ARGS(&pDredSettings));
pDredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
pDredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
```

### How It Works

DRED uses `ID3D12GraphicsCommandList2::WriteBufferImmediate` to insert a 32-bit counter after
every render op (Draw, Dispatch, Copy, Resolve, etc.) in command lists. The write target is a
buffer allocated in system memory (survives device removal). On device removal, the counter value
indicates how many ops completed before the fault.

**WriteBufferImmediate modes**:
- `MARKER_IN` — write occurs after all preceding commands have **started** on GPU
- `MARKER_OUT` — write occurs after all preceding commands have **completed** on GPU
- `DEFAULT` — no ordering guarantee

### Reading Back After Crash

```cpp
CComPtr<ID3D12DeviceRemovedExtendedData> pDred;
pDevice->QueryInterface(IID_PPV_ARGS(&pDred));

D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT output;
pDred->GetAutoBreadcrumbsOutput(&output);

// Walk linked list of D3D12_AUTO_BREADCRUMB_NODE
const D3D12_AUTO_BREADCRUMB_NODE* node = output.pHeadAutoBreadcrumbNode;
while (node) {
    UINT lastCompleted = *node->pLastBreadcrumbValue;
    // node->pCommandHistory[lastCompleted] = last completed op type
    node = node->pNext;
}
```

DRED 1.2 adds PIX/string context data to each breadcrumb node, so you see "DrawInstanced after
BeginFoliage" rather than just "DrawInstanced #71".

**Performance cost**: 2–5% overhead on typical AAA DX12 workloads. Off by default.

**Shader modification required?** No. Fully driver-side.

---

## Technique 5 — Shader Printf via Structured Buffer

Software-only printf from HLSL shaders — no external tools, results readable same frame+N.

### Mechanism (MJP / therealmjp.github.io)

1. A large `RWByteAddressBuffer` (e.g., 1024×1024×4 bytes) is created and cleared each frame.
   First 4 bytes = atomic counter.
2. The buffer descriptor index is placed at a fixed "magic" bindless slot (e.g., slot 1024) so
   shaders can reach it without explicit binding.
3. Shaders call `DebugPrint("value={0}", someFloat)` which expands to:
   - `InterlockedAdd` on the counter to atomically reserve N bytes
   - Write a `DebugPrintHeader` record (NumBytes, StringSize, NumArgs)
   - Pack the format string character-by-character as uint bytes
   - Write argument type codes + values
4. After the frame, the print buffer is copied to a `READBACK` heap.
5. N frames later (fence-gated), the CPU iterates records, decodes the format string, substitutes
   `{0}`, `{1}` placeholders, and logs or displays the result.

```hlsl
// In shader (HLSL SM 6.6 bindless)
DebugPrint("ShadowBias={0} Depth={1}", shadowBias, depth);

// Expanded by macro to:
// uint slot; g_printBuf.InterlockedAdd(0, headerSize + stringLen + argBytes, slot);
// ... write header, chars, args into slot ...
```

**Performance cost**: One `InterlockedAdd` per print call. Can be gated with `#ifdef DEBUG_PRINTS`.
Multi-frame readback latency (matching frame pipeline depth).

**Shader modification required?** Yes — macros must be added to the shader. Non-intrusive to
release builds when disabled.

**Output format**: CPU-decoded strings logged to console or debug overlay.

---

## Technique 6 — GPU-Driven Debug Text Overlay (No CPU Readback)

Alternative to shader printf that avoids multi-frame readback by rendering text directly from GPU
buffers. Source: Joran Joestar (Gabriel Sassone), "GPU Driven Text".

### Mechanism

Four GPU buffers form the pipeline:

| Buffer | Purpose |
|---|---|
| Data Buffer | Atomic counters + packed character bitmap data |
| Entry Buffer | String metadata (screen XY, data offset, char count) |
| Dispatch Buffer | Per-character instance data generated by a compute shader |
| Indirect Buffer | Total character count for `DrawInstancedIndirect` |

Shaders write debug values via `atomicAdd` to reserve Entry and Data slots, then pack character
bitmaps. A compute pass generates per-character instances. A vertex pass expands quads and the
fragment shader decompresses the bitmap bits to render each character pixel.

**Key advantage**: Zero CPU readback overhead. Text rendered same frame it is written.

**Performance cost**: Minimal — a small compute + draw pass. Character bitmaps are 4×float per
character (24-bit packed bitmaps).

**Shader modification required?** Yes — atomic writes in shaders.

---

## Technique 7 — Frostbite Frame Graph Internal Diagnostics

Based on "FrameGraph: Extensible Rendering Architecture in Frostbite" (GDC 2017, Yuriy O'Donnell).

The FrameGraph is rebuilt from scratch every frame (code-driven). This gives the graph full
knowledge of every pass and resource dependency, enabling:

- **Automatic barrier insertion** with full resource transition tracking
- **Memory aliasing statistics** (which transient resources shared memory)
- **Visual graph dump** — the frame graph can serialize itself to DOT format for viewing as a
  directed graph (nodes = passes, edges = resource reads/writes)
- **Resource lifetime tracking** — first-use and last-use frame indices visible per resource
- **Debug pass injection** — because passes are registered objects with explicit I/O, the system
  can inject a "dump resource to disk" pass after any named output

**Triggering**: Developer-facing API, typically gated by a debug flag or console variable.

**Output format**: DOT graph file or custom binary format. No HTML viewer documented publicly.

**Shader modification required?** No. Frame graph layer handles this.

---

## Technique 8 — Godot 4 RENDER_TIMESTAMP Macros

Godot 4's `RendererRD` uses `RENDER_TIMESTAMP` macros scattered throughout the rendering code:

```cpp
RENDER_TIMESTAMP("> Render Viewports");
// ... render work ...
RENDER_TIMESTAMP("< Render Viewports");
```

These macros conditionally insert GPU timestamp queries (via `RenderingDevice::draw_command_insert_breadcrumb`
on Vulkan/DX12) or CPU high-precision counters on lower platforms. Results are exposed through the
in-editor Profiler panel (Monitor tab) and the debugger's Frame Time display.

**Triggering**: Always instrumented in debug builds. Editor shows timeline automatically.

**Output format**: Editor Profiler panel — timeline chart, ms per segment.

**Shader modification required?** No.

---

## Technique 9 — O3DE Atom PassSystem Validation

O3DE's Atom renderer uses a data-driven pass system (JSON `.pass` files). Validation is built into
pass compilation:

- Each pass declares its inputs/outputs in JSON. The `PassSystem` validates resource compatibility
  (format, dimensions, read/write access) at load time, not render time.
- A `--debug-pass-name <name>` flag (or equivalent console variable) can force a specific pass's
  output to the swapchain for visual inspection.
- Pass profiling is integrated via `AZ::RHI::ScopeProducer` timing queries, accessible through the
  in-editor profiling panels.

**Triggering**: Static validation at load time; runtime inspection via console.

**Output format**: Error messages in log at load time; visual output to swapchain for pass override.

---

## Technique 10 — Wicked Engine `wi::profiler`

Wicked Engine ships `wiProfiler.h` with `wi::profiler::BeginRange` / `EndRange` macros wrapping
both CPU (`std::chrono`) and GPU timestamp queries. Results are displayed as a flame graph via
Dear ImGui's `ImDrawList::AddRectFilled` — no external tool required.

Key architecture details:
- Single `ID3D12QueryHeap` per queue, ring-indexed `N × FRAMES_IN_FLIGHT` entries
- A fence per frame slot gates CPU readback (read frame N−3, write frame N)
- `GetTimestampFrequency()` used once at init, cached
- `wi::profiler::DrawFrame(cmd)` renders the flame graph as an ImGui overlay on the final pass

**Triggering**: Always active in debug builds. Toggle in runtime via `wi::profiler::SetEnabled`.

**Output format**: On-screen flame graph via ImGui. Also written to `wi::backlog` (in-engine
scrolling log console).

---

## Technique 11 — Automated Golden-Image / Pixel-Readback Regression

Pattern used by Chromium GPU testing, game CI pipelines, and some in-house tools:

1. Capture a render target to CPU via `READBACK` heap (staging buffer copy).
2. Compare pixel values against a stored reference image.
3. Report pass/fail with PSNR or SSIM metric.

No industry-standard engine ships this as a user-facing feature; it lives in test infrastructure.
The canonical DX12 readback pattern:

```cpp
// In render graph: copy texture to readback buffer
D3D12_TEXTURE_COPY_LOCATION src = { .pResource = tex, .Type = TEXTURE, ... };
D3D12_TEXTURE_COPY_LOCATION dst = { .pResource = readbackBuf, .Type = PLACED_FOOTPRINT, ... };
cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
// Fence wait (N frames later)
readbackBuf->Map(0, nullptr, &pixels);
// Compare pixels against reference
```

**Shader modification required?** No.

**Performance cost**: One copy per captured texture. Only done in test mode.

---

## Summary Matrix

| Technique | Trigger | Output | Shader Mod? | Perf Cost | Always-On? |
|---|---|---|---|---|---|
| GPU Timestamp (stat gpu) | Console cmd | On-screen text | No | ~0.05 ms | Yes (debug) |
| RDG DumpGPU | One-shot console | HTML + binary files | No | High (1 frame) | No |
| Buffer Visualization Dump | Console var | PNG/EXR files | No | Medium (copy) | No |
| DRED Breadcrumbs | Pre-device init | Crash report struct | No | 2–5% | Optional |
| Shader Printf (buffer) | `#define` gate | Log / overlay | Yes | Low (1 atomic) | No |
| GPU-Driven Text | Always-on pass | Rendered overlay | Yes | Very low | Yes |
| Frame Graph DOT dump | Debug flag | DOT graph file | No | None (CPU only) | No |
| Godot RENDER_TIMESTAMP | Always (debug) | Editor panel | No | ~0.02 ms | Yes (debug) |
| O3DE Pass validation | Load time | Log errors | No | None at runtime | Yes |
| Wicked wi::profiler | Toggle | ImGui flame graph | No | ~0.03 ms | Yes (debug) |
| Pixel readback regression | Test mode | PSNR/SSIM result | No | Medium (copy) | No |

---

## Key Takeaways

- **GPU timestamp query rings** are the universal pattern for per-pass timing. DX12 requires only
  `EndQuery` (no Begin). Ring size = `N × FRAMES_IN_FLIGHT`. Readback is fence-gated, N frames late.
- **RDG/frame-graph architectures** enable zero-code-change resource dumping: the graph injects
  copy passes after any registered output, because resource ownership is centralized.
- **Shader printf** is practical but requires bindless descriptors (SM 6.6) and multi-frame
  readback. GPU-driven text is zero-latency but higher complexity.
- **DRED breadcrumbs** are the correct tool for device-removed crashes. Must be enabled before
  device creation. 2–5% cost makes them suitable for debug builds only.
- **Always-on overlays** (stat gpu, wi::profiler) rely on ring-buffered timestamp queries with
  SmoothedAverage display to avoid per-frame jitter. Display values lag by 3–4 frames but that is
  unnoticeable.
- **No engine** adds shader-side instrumentation for diagnostics in release builds. All persistent
  diagnostic passes are compile-time gated or console-variable gated.

---

## References

- [UE5 Stat Commands Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/stat-commands-in-unreal-engine)
- [UE5 GPUDump Viewer](https://dev.epicgames.com/documentation/en-us/unreal-engine/gpudump-viewer-tool-in-unreal-engine)
- [UE5 RDG Notes (dawnarc.com)](https://dawnarc.com/2024/03/ue5rdg-notes/)
- [Shader Printf in HLSL and DX12 — therealmjp.github.io](https://therealmjp.github.io/posts/hlsl-printf/)
- [GPU Profiling 101 — Nathan Reed](https://www.reedbeta.com/blog/gpu-profiling-101/)
- [GPU Execution Timing Basics Vk/DX12 — Pavel Smejkal](https://pavelsmejkal.net/Posts/GPUTimingBasics)
- [Range-Based CPU/GPU Profiler Implementation — Dieter Baetens](https://debaetsd.github.io/posts/profiler/)
- [D3D12 DRED Breadcrumbs — DirectX Developer Blog](https://devblogs.microsoft.com/directx/dred/)
- [D3D12 WriteBufferImmediate Spec](https://microsoft.github.io/DirectX-Specs/d3d/D3D12WriteBufferImmediate.html)
- [GPU-Driven Text — Gabriel Sassone](https://jorenjoestar.github.io/post/gpu_driven_text/gpu_driven_text/)
- [In-Game GPU Profiling DX12 SetBackgroundProcessingMode — NVIDIA Dev Blog](https://developer.nvidia.com/blog/in-game-gpu-profiling-for-dx12-using-setbackgroundprocessingmode/)
- [Frostbite FrameGraph GDC 2017](https://www.gdcvault.com/play/1024612/FrameGraph-Extensible-Rendering-Architecture-in)
- [Wicked Engine Documentation](https://github.com/turanszkij/WickedEngine/blob/master/Content/Documentation/WickedEngine-Documentation.md)
- [Unity Frame Debugger Manual](https://docs.unity3d.com/Manual/FrameDebugger.html)
- [O3DE Atom Pass System](https://docs.o3de.org/docs/atom-guide/dev-guide/passes/pass-system/)
- [Unreal Art Optimization — GPU Visualizer](https://unrealartoptimization.github.io/book/profiling/gpu-visualizer/)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-30 | Initial research compilation |
