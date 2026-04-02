---
name: Render Graph Documentation (Phase 3a-3.1.0)
description: Comprehensive API docs and pass authoring guide for render graph system
type: project
---

## Status

**COMPLETED** — 2026-03-12

- `docs/engine/12-rendering/04-render-graph.md` (710 lines) — Full API reference
- `docs/engine/12-rendering/05-render-graph-guide.md` (823 lines) — Hands-on pass authoring guide

## Files Updated

1. **04-render-graph.md** — Complete replacement from placeholder
   - Architecture overview with ASCII diagram
   - Core components: RgResourceId, RgTextureDesc, RgBuilder, RgPassBuilder, RgPassResources, RgResourcePool
   - Four compilation phases: topological sort, lifetime computation, pass culling, barrier scheduling
   - Four detailed pass examples (render target, deferred lighting, compute HZB, depth prepass)
   - API reference tables (all methods documented)
   - Thread safety notes
   - Common patterns (double-buffering, multi-pass, optional effects, imported resources)
   - Debug/profiling guide
   - Performance considerations
   - Common errors with solutions
   - Engine comparison (UE5 vs Godot vs UzEngine)
   - Phase roadmap (3a-3.1.0 through 3a-3.5.0)

2. **05-render-graph-guide.md** — New comprehensive guide
   - Step-by-step tutorials for 4 pass types:
     1. Full-screen post-processing (tonemap)
     2. Deferred shading (multiple inputs)
     3. Compute shader with buffer output (HZB)
     4. Depth-only prepass
   - Resource management patterns (transient, imported, pooling strategy)
   - Pass patterns by GPU operation (RTT, compute→graphics, copy, indirect rendering)
   - Debugging techniques (trace logging, resource validation, GPU captures)
   - Performance optimization strategies
   - Troubleshooting section with 4 common issues
   - Implementation checklist

3. **docs/engine/README.md** — Line 35 status updated
   - Changed: `**Planned**` → `**Planned (3a-3.1.0)**`
   - Added "API + guide" clarification

## API Coverage

### RgBuilder (all public methods documented)
- `CreateTexture(name, desc)`
- `CreateBuffer(name, desc)`
- `ImportTexture(name, handle, currentState)`
- `ImportBuffer(name, handle, currentState)`
- `AddPass(name, setupFn)`
- `Compile()`
- `Execute(cmdList)`
- Queries: `GetPassCount()`, `GetCulledPassCount()`, `GetBarrierCount()`, `GetResourceCount()`, `GetResource(id)`

### RgPassBuilder (all public methods documented)
- `Read(id, state)`
- `Write(id, state)`
- `SetExecutionCallback(fn)`

### RgPassResources (all public methods documented)
- `GetTexture(id)`
- `GetBuffer(id)`

### RgResourcePool (all public methods documented)
- `AcquireTexture(desc, debugName)`
- `AcquireBuffer(desc, debugName)`
- `ReleaseTexture(handle, desc)`
- `ReleaseBuffer(handle, desc)`
- `Trim()`
- `GetCachedTextureCount()`
- `GetCachedBufferCount()`

## Key Design Decisions Documented

1. **Virtual handles** — RgResourceId decoupled from physical RHI handle to enable pooling/aliasing
2. **Per-frame builders** — New RgBuilder each frame, pool is persistent
3. **Declarative dependencies** — Read()/Write() declare what graph needs for topological sort
4. **Automatic barriers** — Compiler inserts barriers based on state transitions
5. **Pass culling** — Outputs nobody consumes are removed automatically
6. **Resource pooling** — Descriptor-keyed hash of texture/buffer dimensions enables reuse

## Code Examples Provided

- Full-screen post-processing pass (tonemap)
- Deferred shading with multiple inputs (GBuffer → Lighting)
- Compute shader with buffer output (HZB mip chain)
- Depth-only prepass (render geometry to depth only)
- Double-buffered render targets (ping-pong)
- Multi-pass downsampling chain
- Optional effects with conditional registration
- Imported external resources (swapchain)
- Compute → Graphics handoff
- Copy operations
- Indirect rendering

**Total code examples**: 40+

## Standards Met

- ✅ Rust-style documentation (`///` on all public items, `// ───` section headers)
- ✅ Detailed enough for auto-generated API docs (each item describes what, params, returns, side effects, examples)
- ✅ Cross-references to related docs (RHI, Render World, Shader Pipeline)
- ✅ Architecture diagrams (ASCII art with flow)
- ✅ Engine comparisons (UE5 FRDGBuilder, Godot RenderingDevice)
- ✅ Rules (10 MUST/SHOULD/NEVER directives)
- ✅ Troubleshooting (4 common errors with solutions)
- ✅ Phase roadmap (3a-3.1.0 through 3a-3.5.0)
- ✅ Performance notes (memory, barriers, culling, profiling)
- ✅ Markdown conventions (H1 per file, sequential heading levels, consistent formatting)
- ✅ No orphaned documentation (all docs linked from section 12-rendering)

## Implementation Sources Analyzed

- `runtime/engine/renderer/render_graph/render_graph_types.h` (207 lines)
- `runtime/engine/renderer/render_graph/render_graph_builder.h` (228 lines)
- `runtime/engine/renderer/render_graph/render_graph_builder.cpp` (450+ lines, read partially)
- `runtime/engine/renderer/render_graph/render_graph_pass.h` (106 lines)
- `runtime/engine/renderer/render_graph/render_graph_resources.h` (160 lines)
- `runtime/engine/renderer/render_graph/render_graph_resources.cpp` (100+ lines, read partially)

## Key Findings

1. **Topological sort** uses Kahn's algorithm with per-resource access chains
2. **Culling** is backward-propagating (if output not live, pass is culled)
3. **Barrier scheduling** happens after topological sort by walking passes and tracking state
4. **Resource pooling** uses FNV64 hash of descriptor fields (width, height, format, usage, etc.)
5. **Virtual → Physical resolution** happens only at execution time in `Execute()`
6. **Pass execution order** is determined by topological sort, not insertion order
7. **Imported resources** allow external lifetime management (e.g., swapchain)
8. **State tracking** is per-resource, updated as barriers are scheduled

## Missing / Future Documentation

- Async compute support (mentioned in old docs, not in current implementation)
- Advanced aliasing visualization tools
- Built-in pass library (mentioned but not fully implemented yet)
- Performance profiling guide (GPU memory aliasing statistics)

## Why Two Files?

- **04-render-graph.md**: API reference + compiler implementation details (710 lines)
  - Audience: Rendering system architects, optimization engineers
  - Focus: What resources exist, how compiler works, why decisions matter

- **05-render-graph-guide.md**: Tutorial + hands-on patterns (823 lines)
  - Audience: Graphics programmers authoring new passes
  - Focus: How to declare passes, what patterns work, debugging techniques

Both are necessary because they serve different audiences and purposes.
