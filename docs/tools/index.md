# UzEngine — Tools & Editors Index

This directory contains detailed specifications for every major authoring tool in UzEngine. Each document is an implementation blueprint: it defines the UI layout, data model, file formats, C++ class hierarchy, IPC protocol, and Lua API for one editing domain.

All tools share the same foundational design principles:

- **VSCode-native**: panels are VSCode WebView panels communicating with the engine via IPC/MCP
- **AI-friendly formats**: all file formats are plain-text, INI-like, deterministic, and LLM-writable
- **Live preview**: every tool streams DX12-rendered frames via JPEG+WebSocket (same pipeline as the 3D viewport)
- **Undo/Redo**: all operations integrate with the shared `UndoStack` via `EditorCommand`
- **Non-destructive**: external file edits are detected and surfaced as conflict dialogs

---

## Tools by Domain

### Rendering & Shading

| Tool | File | Description |
|------|------|-------------|
| **Visual Shader Editor** | [shader-editor.md](shader-editor.md) | Node-based HLSL shader authoring. 108 node types, real-time preview, compiles to `.hlsl` + `.uzmat`. Replaces hand-writing shader code for artists. |
| **Material Editor** | [material-editor.md](material-editor.md) | High-level material instance configuration. PBR texture slots, multipart character material slots, layering system (dirt/wetness/blood/burn), dye/tint channels, LOD materials. |
| **Post-Process Pipeline Editor** | [postprocess-editor.md](postprocess-editor.md) | Post-Process Volume system for level designers. Volume trigger zones with priority blending, color grading (LUT/curves/wheels), exposure, bloom, DoF, SSAO, SSR, tone mapping, film grain, vignette, chromatic aberration, motion blur, fog override, custom PP materials. |
| **Global Illumination** | [global-illumination.md](global-illumination.md) | Lumen-inspired dynamic GI: radiance probe grid (L2 SH, 65k probes), SSIL, baked lightmaps (Embree path tracer), reflection captures with parallax correction, emissive voxel injection, DXR + SDF fallback. |
| **Visibility & GPU Culling** | [visibility-culling.md](visibility-culling.md) | HZB two-phase occlusion culling, Nanite-style meshlet rendering (AS+MS shaders), virtual geometry cluster DAG, LOD system with crossfade dither, impostor baker, 7 debug visualization modes. |
| **Image Quality Pipeline** | [image-quality-pipeline.md](image-quality-pipeline.md) | TAA (ghost reduction: variance clipping, disocclusion, firefly), FSR 3, XeSS, DLSS 3.5 (SR+FG+RR), engine-native optical flow frame interpolation, denoiser, CAS sharpening, Dynamic Resolution Scaling. |
| **Render Console & Scalability** | [render-console.md](render-console.md) | 100+ `r.*` CVars for live render pipeline control, 7 scalability groups (Low/Medium/High/Ultra), GPU profiler with DX12 timestamp queries, high-res/EXR/video capture, Lua `ctx.render` API for game Settings screens. |

### Characters & Animation

| Tool | File | Description |
|------|------|-------------|
| **Skeleton, Control Rig & Socket Editor** | [skeleton-rig-editor.md](skeleton-rig-editor.md) | Bone hierarchy editing, IK control rig authoring, socket placement (weapon/FX/camera attach points), ragdoll physics body setup, LOD bone reduction, multipart character assembly. |
| **Animation Editor** | [animation-editor.md](animation-editor.md) | Keyframe timeline, bezier curve editor, Animation State Machine (FSM), Blend Trees (1D/2D), Animation Montages, IK solvers, root motion, animation retargeting. |

### VFX & Environment

| Tool | File | Description |
|------|------|-------------|
| **VFX & Particle Editor** | [particle-editor.md](particle-editor.md) | GPU compute particle simulation (65536 particles/emitter). Node-based or module-stack authoring. Sprite/Mesh/Ribbon/Beam emitters. Full HLSL compute shaders for spawn/simulate/render. |
| **Terrain & World Building Editor** | [terrain-editor.md](terrain-editor.md) | Heightmap sculpting, splatmap texture painting, GPU-instanced foliage, world tile composition (1km tiles), atmosphere & sky system, procedural generation with AI MCP integration. |
| **Foliage & PCG Editor** | [foliage-pcg-editor.md](foliage-pcg-editor.md) | Deep foliage instancing (48-byte GPU instances, HZB culling, wind HLSL) + node-based PCG graph (30+ nodes, biome system, GPU compute for 10k+ points, per-tile streaming). |

### Audio

| Tool | File | Description |
|------|------|-------------|
| **Audio Mixer & Sound Designer** | [audio-editor.md](audio-editor.md) | Visual mixing console (4 XAudio2 buses), custom EQ/Compressor XAPOs, Sound Cue node graph (random/sequential selection), 3D audio scene placement, adaptive music sequencer. |

### UI & Game Interface

| Tool | File | Description |
|------|------|-------------|
| **UI Editor** | [ui-editor.md](ui-editor.md) | WYSIWYG HTML+CSS game UI authoring for `uz_ui_html.dll`. Drag-drop widgets, CSS variable data binding for Lua integration, keyframe UI animations, multi-resolution preview. |

### Level Design & Gameplay

| Tool | File | Description |
|------|------|-------------|
| **Level Blueprint Editor** | [level-blueprint-editor.md](level-blueprint-editor.md) | Visual scripting (node graph) that compiles to LuaJIT. 50+ action nodes, flow control, variables, function graphs, debug breakpoints. Generated Lua coexists with hand-written code. |
| **Cinematic Sequencer** | [cinematic-editor.md](cinematic-editor.md) | Non-linear timeline editor for in-engine cutscenes. 9 track types (camera, actor, animation, audio, event, subtitle, post-process, fade). Camera rail splines, shot system, Lua-triggered playback. |

### AI & Navigation

| Tool | File | Description |
|------|------|-------------|
| **NavMesh & AI Pathfinding Editor** | [navmesh-editor.md](navmesh-editor.md) | Recast+Detour build parameter UI, area type painting, NavMesh links (jump/ladder/door), DetourCrowd debug (256 agents), query visualization, incremental tile rebuild. |

---

## File Format Summary

Every tool produces one or more plain-text files that can be read and written by AI agents:

| Format | Tool | Description |
|--------|------|-------------|
| `.uzshader` | Shader Editor | Node graph definition (INI-like) |
| `.uzmat` | Material Editor, Shader Editor | Material instance (PBR parameters, texture slots, layers) |
| `.uzskel` | Skeleton Editor | Bone hierarchy + sockets + LOD masks |
| `.uzrig` | Control Rig Editor | IK controls + constraints + space switches |
| `.uzphys` / `.uzragdoll` | Ragdoll Editor | Physics bodies + joint constraints |
| `.uzanim` | Animation Editor | Keyframe clip (bone channels, morph targets, notifies) |
| `.uzfsm` | Animation Editor | Animation State Machine |
| `.uzblend` | Animation Editor | Blend Tree definition |
| `.uzmontage` | Animation Editor | Animation Montage (sections + notifies + slots) |
| `.uzfx` | Particle Editor | VFX effect (emitters + modules + curves) |
| `.uzterrain` | Terrain Editor | Terrain tile definition (layers, foliage, atmosphere) |
| `.uzworld` | Terrain Editor | World composition manifest (tile grid) |
| `.uzcue` | Audio Editor | Sound Cue node graph |
| `.uzmix` | Audio Editor | Mixer snapshot (bus volumes, EQ, compressor) |
| `.uzmusic` | Audio Editor | Adaptive music sequencer definition |
| `.uzui` | UI Editor | Game UI document (HTML+CSS, anchor layout) |
| `.uzblueprint` | Level Blueprint Editor | Visual script graph (compiles to .lua) |
| `.uzcinematic` | Cinematic Sequencer | Cutscene definition (tracks + keyframes) |
| `.uznavmesh` / `.uznav` | NavMesh Editor | NavMesh binary tiles + build parameters INI |
| `.uzppv` | Post-Process Editor | Post-Process Volume configuration (volume shape, blend, all PP settings) |
| `.uzgi` | Global Illumination | GI mode + probe grid + SSIL + lightmap bake settings |
| `.uzfoliage_type` | Foliage & PCG Editor | Foliage asset definition (mesh, LODs, wind, collision, density rules) |
| `.uzfoliage_cluster` | Foliage & PCG Editor | Foliage cluster (weighted mix of foliage types) |
| `.uzpcg` | Foliage & PCG Editor | PCG node graph (procedural content generation) |
| `.uzbiome` | Foliage & PCG Editor | Biome definition (PCG rules + foliage clusters) |
| `.uzimpostor` | Visibility & Culling | Impostor config (angle count, atlas resolution, texture paths) |
| `render.cfg` | Render Console | Project render CVar overrides |
| `scalability.cfg` | Render Console | Scalability group definitions (Low/Medium/High/Ultra) |

---

## Integration Map

```
                    ┌─────────────────────────────────┐
                    │         VSCode Extension         │
                    │   (TypeScript WebView Panels)    │
                    └────────────────┬────────────────┘
                                     │ IPC / MCP
                    ┌────────────────▼────────────────┐
                    │         UzEngine.exe             │
                    │       (editor mode, DX12)        │
                    └──┬──────┬──────┬──────┬─────────┘
                       │      │      │      │
              ┌────────▼──┐ ┌─▼────┐ │ ┌───▼──────┐
              │ Renderer  │ │ ECS  │ │ │  Audio   │
              │ DX12 pass │ │EnTT  │ │ │ XAudio2  │
              └────────┬──┘ └─┬────┘ │ └───┬──────┘
                       │      │      │     │
              ┌────────▼──────▼──────▼─────▼──────┐
              │             game.dll               │
              │     (LuaJIT VM + C ABI bridge)     │
              └────────────────────────────────────┘
```

All tools communicate with `UzEngine.exe --editor` via the IPC file protocol or MCP server. The engine renders previews to DX12 offscreen targets, encodes as JPEG, and streams over WebSocket to the VSCode WebView.

---

## Implementation Phases

Tools are scheduled across the engine roadmap phases:

| Phase | Tools |
|-------|-------|
| **Phase 1** (MVP Editor) | Basic scene editing panels (see `docs/specs/editor-panels.md`) |
| **Phase 2** | Shader Editor, Material Editor, Post-Process Editor, Render Console, Audio Editor, UI Editor |
| **Phase 3** | Animation Editor, Skeleton/Rig/Socket Editor, Particle Editor, Level Blueprint Editor, Image Quality Pipeline |
| **Phase 4** | Terrain Editor, Foliage & PCG Editor, NavMesh Editor, Cinematic Sequencer, Global Illumination |
| **Phase 5** | Visibility & GPU Culling (Meshlets/Nanite), Control Rig, Ragdoll Setup, World Composition, Adaptive Music Sequencer |

---

## Related Documentation

- [docs/specs/editor-panels.md](../specs/editor-panels.md) — Base editor panel architecture (Scene Tree, Inspector, Asset Browser)
- [docs/specs/renderer.md](../specs/renderer.md) — DX12 render pipeline that powers all tool previews
- [docs/specs/shader-system.md](../specs/shader-system.md) — Shader compilation pipeline consumed by the Shader Editor
- [docs/specs/animation-system.md](../specs/animation-system.md) — Runtime animation system targeted by the Animation Editor
- [docs/specs/skeletal-mesh.md](../specs/skeletal-mesh.md) — Skeletal mesh runtime used by Skeleton/Rig Editor
- [docs/specs/particle-system.md](../specs/particle-system.md) — GPU particle runtime for the Particle Editor
- [docs/specs/ui-system.md](../specs/ui-system.md) — `uz_ui_html.dll` runtime for the UI Editor
- [docs/specs/audio-system.md](../specs/audio-system.md) — XAudio2 runtime for the Audio Editor
- [docs/specs/navmesh.md](../specs/navmesh.md) — Recast/Detour runtime for the NavMesh Editor
- [docs/specs/terrain-system.md](../specs/terrain-system.md) — Terrain rendering runtime
- [docs/specs/screen-space-effects.md](../specs/screen-space-effects.md) — SSAO, SSR, TAA post-process effects (consumed by Post-Process Editor)
- [docs/specs/gpu-instancing.md](../specs/gpu-instancing.md) — GPU instancing pipeline (consumed by Foliage & Visibility systems)
- [docs/specs/shader-variants.md](../specs/shader-variants.md) — ShaderVariantKey system (MESHLET, LOD_CROSSFADE, IMPOSTOR variants)
- [docs/PRD.md](../PRD.md) — Product Requirements Document
- [docs/ROADMAP.md](../ROADMAP.md) — Implementation roadmap with all phases and dependencies
