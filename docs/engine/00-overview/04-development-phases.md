# Architecture Overview — Development Phases

> **Status**: Reference (roadmap)
> **Source**: ARCHITECTURE.md §Development Phases

## Phase Overview

| Phase | Name | Focus | Status |
|-------|------|-------|--------|
| **1** | Runtime Render Foundation | Core rendering pipeline as standalone runtime | **CURRENT** |
| **1.5** | Build Pipeline & Packager | Game build system, asset cooking, shipping exe | Planned (early) |
| **2** | Editor Extension | VSCode extension with full editor UI | Planned |
| **3** | MCP Integration | LLM integration for autonomous development | Planned |

## Phase 1: Runtime Render Foundation (CURRENT)

Build the core rendering pipeline as a standalone runtime with test harness. No extension or MCP until render is solid.

**Completed milestones:**
- 1.1.0a — Math library (Vec2/3/4, Mat4, Quat, Transform, DirectXMath SIMD)
- 1.2 — RHI DX12 backend (clear screen demo, 6069 FPS on RTX 4090, 183 tests)

**In progress:**
- 1.3 — Triangle rendering (first geometry on screen)
- Core systems (logging, assertions, config, memory, paths)
- Shader pipeline (VFS, DXC compilation, cache, hot-reload)
- Asset pipeline foundation (registry, importers, formats)

**Target:** A standalone `uzengine.exe` that loads a `.uzscene`, renders it with deferred PBR, and supports `--screenshot` for LLM visual feedback.

## Phase 1.5: Build Pipeline & Packager (EARLY)

Game build system that compiles a project into a self-contained, cross-platform executable.

**Why early?** POC never had this and it's critical. The build pipeline is where real optimization happens — texture compression, shader compilation, asset packing. Building it early ensures every feature is designed with shipping in mind from the start.

```
Project Source
     │
     ├─ Asset Processing
     │   ├── Texture compression (BC7, ASTC, ETC2 per platform)
     │   ├── Mesh optimization (LOD generation, vertex cache)
     │   └── Audio encoding (platform-specific codecs)
     │
     ├─ Shader Compilation
     │   ├── HLSL → DXIL (DX12)
     │   ├── HLSL → SPIR-V (Vulkan)
     │   └── HLSL → MSL (Metal via SPIRV-Cross)
     │
     ├─ Asset Packing (.uzpak archives, LZ4 compression)
     │
     └─ Executable Generation (link runtime, embed assets, per-platform)
```

**Output:** Self-contained executable + asset packs, no engine installation required.

## Phase 2: Editor Extension

VSCode extension with full editor UI, connected to the validated runtime.

**Approach:** Runtime as native N-API addon (.node) loaded directly by the extension host — no IPC, no WebSocket, no frame streaming for the viewport. Webview used only for 2D editor panels (inspector, hierarchy, asset browser).

**Editor tools planned:**
- Shader graph editor, animation controller, behavior tree editor (all webview-based)
- 3D viewport via native HWND (direct DX12 swapchain)
- H.264 frame streaming for preview/remote panels

## Phase 3: MCP Integration

LLM integration layer for autonomous development workflows.

**Key features:**
- MCP tools for scene manipulation (create entities, modify materials, generate code)
- Visual feedback loop via `--screenshot` + LLM vision
- AI provider abstraction (Claude, GPT, DeepSeek — configurable per-project)
- End-to-end content generation: prompt → texture/model/audio → engine asset
