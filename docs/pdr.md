# UzEngine 0.6 — Project Design Review (PDR)

> **Repository**: https://github.com/uzmigames/uzengine
> **Date**: 2026-04-03 (last updated)
> **Status**: Phases 1-3 COMPLETED, Phase 4 IN PROGRESS, Phase 5 PARTIAL (viewport ✅), Phase 7 PARTIAL (post-process ✅, VFX ✅, DXR ✅)
> **POC Status**: POC 0.2 validated → Production 0.6 (DX12 SM6.8 bindless, 145 shaders, 22 demo scenes)

---

## 1. Project Summary

**UzEngine** is a AAA-quality game engine fully embedded in VSCode, designed as the first engine built for autonomous AI-driven development. It uses open JSON file formats, a C++ runtime loaded as an N-API `.node` addon, and an MCP server for LLM integration.

### Key Differentiators

- **AI-first**: open formats (JSON everywhere), visual feedback loop (screenshot CLI), MCP tools for LLM manipulation
- **VSCode-native**: engine runs inside the editor process (N-API addon), not as a separate app
- **AAA rendering**: Unreal 5.6-quality renderer (clean-room), SM6.6 baseline, deferred PBR pipeline
- **No GC**: Rust-inspired ownership/borrow model in C++ for deterministic performance
- **Engine Mode**: toggle VSCode layout to Unreal/Godot-like IDE while preserving AI chat panels — documented in [docs/engine/32-engine-mode/](engine/32-engine-mode/)
- **VFX AI Tool**: AI-automated VFX pipeline via Claude + ComfyUI + TRELLIS.2 for mesh/texture/shader generation — documented in [docs/engine/33-vfx-ai-tool/](engine/33-vfx-ai-tool/)

---

## 2. Architecture Validation

### 2.1 — Runtime Execution Model: **VALIDATED**

| Decision | Validation | Risk |
|----------|-----------|------|
| N-API in-process addon | ✅ POC 0.2: DX12 render + H.264 encode + WebCodecs decode = 120 FPS at 854x480 with 50K instances | Addon crash kills extension host (Electron restarts in ~1s) — **acceptable** |
| Electron ABI matching | ✅ POC 0.2: `--runtime=electron --runtime-version=39.6.0`, delay-load hook, `NAPI_DISABLE_CPP_EXCEPTIONS` | ABI must match exactly; documented in MEMORY.md |
| H.264 frame streaming (webview panels) | ✅ POC 0.2: MF HW encoder 2.2ms + WebCodecs 3.3ms = 120 FPS sustained | Only for preview/remote panels — viewport uses shared GPU surface |
| Shader pipeline (DXC + FXC dual compiler) | ✅ Milestone 1.3: DXC for SM6.6+, FXC for SM5.0 fallback, 90 tests passing, 5 demos | Cache differentiated by ShaderTarget enum, no stale-cache risk |
| Viewport via native HWND | ✅ v0.3.0: native HWND inside VSCode, DX12 SM6.8 at 108+ FPS (RTX 4090), Agility SDK DLL-hosted loading | **VALIDATED** — shipping in v0.3.0+ |

### 2.2 — Technology Choices: **REVIEWED**

| Component | Choice | Rationale | Risk |
|-----------|--------|-----------|------|
| Graphics API | DX12 SM6.6 (primary), DX11, Vulkan 1.3, Metal | Modern features, POC proved SM5-first was painful | DX12 primary; DX11/Vulkan/Metal at visual parity |
| Physics | Jolt | Fast, modern, MIT license | Less mature than PhysX |
| ECS | entt | Header-only, fast, well-maintained | No built-in serialization |
| Audio | miniaudio | Single-file, cross-platform | Limited spatial audio |
| Build system | Zig (primary) + CMake fallback | Fast incremental builds | Zig ecosystem still young |
| Scripting | C++ (primary) + Lua (secondary) | Performance + rapid prototyping | Two language support surfaces |
| File formats | JSON everywhere | LLM-friendly, human-readable, diffable | Slower parse vs binary (mitigated by cooked assets) |
| UI (game) | Embedded HTML/CSS | LLM-friendly, designer-accessible | Browser embedding adds complexity |
| AI texture generation | ComfyUI + SDXL (local) | Free, local, no cloud dependency | Requires capable GPU, setup complexity |

### 2.3 — Unvalidated Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Native HWND inside VSCode | High — viewport rendering depends on this | Prototype early in Phase 5.1, fallback to H.264 streaming |
| SM6.6 hardware coverage | Medium — excludes pre-2020 GPUs | ✅ SM5.0 fallback implemented via FXC (Milestone 1.3) |
| Zig build system maturity | Medium — may hit toolchain issues | CMake fallback always available |
| C++ hot-reload (DLL swap) | Medium — state serialization is fragile | Prioritize Lua scripting for iteration speed (Phase 6.3) |
| Memory model (borrow checker in C++) | Low-Medium — debug overhead, learning curve | Debug-only validation, zero-cost in release |
| Embedded browser for game UI | Medium — memory/performance overhead | Evaluate lightweight alternatives (Phase 6.4) |
| ComfyUI local generation quality | Medium — depends on SDXL model quality | Claude Vision feedback loop validates output iteratively |

---

## 3. Scope Analysis

### 3.1 — Phase Structure Overview

The project uses a 9-phase structure for better granularity. Phases 1-3 are complete, Phase 4 is in progress.

| Phase | Name | Status | Est. Duration |
|-------|------|--------|---------------|
| Phase 1 | Runtime Foundation | ✅ COMPLETED | Done |
| Phase 2 | Runtime Framework | ✅ COMPLETED | Done |
| Phase 3 | First Pixels | ✅ COMPLETED | Done |
| Phase 4 | Playable Scene | 🔨 IN PROGRESS (Input + Camera done, Physics next) | In progress |
| Phase 5 | Editor | PARTIAL ✅ (Extension + IPC ✅, Viewport ✅) | In progress |
| Phase 6 | Advanced Gameplay | PARTIAL ✅ (Lua scripting done early) | In progress |
| Phase 7 | Content & Polish | PARTIAL ✅ (Post-process ✅, VFX ✅, Advanced rendering partial) | In progress |
| Phase 8 | MCP & AI | Planned | 2-3 months |
| Phase 9 | Shipping | Planned | 2-3 months |

### 3.2 — Phase 1 (Runtime Foundation) — ✅ COMPLETED

| Milestone | Complexity | Status |
|-----------|-----------|--------|
| 1.0 Bootstrap | Medium | ✅ DONE — logging, assertions, profiler, memory, window, config, test harness |
| 1.1 Math & Containers | Low-Medium | ✅ DONE — Vec2/3/4, Mat4, Quat, Transform, AABB, HandleMap, allocators |
| 1.2 RHI (DX12 SM6.6) | **HIGH** | ✅ DONE — 6,069 FPS clear screen, D3D12MA, DRED, bindless SM6.6, 183 tests |
| 1.3 Shader Pipeline | **HIGH** | ✅ DONE — DXC+FXC dual compiler, VFS, xxHash64 cache, hot-reload, reflection, permutations, 90 tests |

### 3.3 — Phase 2 (Runtime Framework) — ✅ COMPLETED

| Milestone | Complexity | Status |
|-----------|-----------|--------|
| 2.0 Application Lifecycle | Medium | ✅ DONE — EngineLoop, subsystem registry, module registry, time manager, project loader |
| 2.1 Job System | Medium | ✅ DONE — thread pool, job counters, fences |
| 2.2 Resource System | Medium-High | ✅ DONE — asset registry, importers, .uzshader/.uzmesh/.uztexture/.uzsdf formats, manifest, cache |

### 3.4 — Phase 3 (First Pixels) — ✅ COMPLETED (Both Tracks)

**Track A — Rendering: ✅ COMPLETE**

| Milestone | Status |
|-----------|--------|
| 3.0 Triangle | ✅ DONE — clear_screen + triangle demos |
| 3.1 Render Graph | ✅ DONE — DAG-based pass scheduling |
| 3.2 Forward PBR | ✅ DONE — GGX BRDF, Burley diffuse |
| 3.3 Deferred | ✅ DONE — GBuffer + deferred lighting |
| 3.4 Shadows | ✅ DONE — CSM (4 cascades), SDF shadows, shadow cache, local light atlas |
| 3.5 Post-Process | ✅ DONE — GTAO, SSR, TAA, TSR, Bloom, Auto Exposure, Tonemap, Hi-Z, FXAA, DoF, Motion Blur, Lens Flare, CAS, Color Grading, SSGI |
| Atmosphere (bonus) | ✅ DONE — Sky atmosphere (Bruneton), height fog, volumetric clouds, aerial perspective, god rays |
| IBL (bonus) | ✅ DONE — BRDF LUT, irradiance, prefiltered specular, reflection probes |

**Track B — ECS/Scene: ✅ COMPLETE**

| Milestone | Status |
|-----------|--------|
| 3.0b World + Entity | ✅ DONE — EnTT ECS, World, WorldSubsystem |
| 3.1b Components | ✅ DONE — 11 component types (Camera, Light, StaticMesh, SkyAtmosphere, SkyLight, HeightFog, VolumetricClouds, ReflectionProbe, etc.) |
| 3.2b Property System | ✅ DONE — ClassInfo, ClassRegistry, PropertySerializer |
| 3.3b Tick System | ✅ DONE — TickableComponent, TickSystem, TickSubsystem |
| 3.4b Scene Loader | ✅ DONE — .uzscene format |
| 3.5b Rendering Bridge | ✅ DONE — Track A + Track B merged |

Both tracks ran in parallel and merged at 3.5b.

### 3.5 — Phase 4 (Playable Scene) — 🔨 IN PROGRESS

| Milestone | Status |
|-----------|--------|
| 4.0 Input | ✅ DONE — Lua input API (keyboard/mouse polling) |
| 4.1 Camera | ✅ DONE — camera system, Lua camera API |
| 4.2 Physics (Jolt) | Planned — **NEXT** |
| 4.3 Animation | Planned |
| 4.4 Audio | Planned |
| 4.5 Prefabs | Planned |

### 3.6 — Phase 5 (Editor) — Overlaps Phase 4

Starts from Phase 4.0+. Sequence: 5.0 Extension+IPC → 5.1 Viewport → 5.2 Hierarchy+Inspector → 5.3 Asset Browser → 5.4 Gizmos → 5.5 Node-Graph → 5.6 Material Editor → 5.7+ Advanced

Includes **Engine Mode** (5.x): VSCode layout toggle to Unreal/Godot-like IDE.

### 3.7 — Phase 6 (Advanced Gameplay) — PARTIAL

6.3 Lua Scripting was implemented early (LuaJIT 2.1 VM, sandbox, 12+ Lua APIs, script system).

| Milestone | Status |
|-----------|--------|
| 6.0 Navigation | Planned |
| 6.1 Behavior Trees | Planned |
| 6.2 C++ Hot-Reload | Planned |
| 6.3 Lua Scripting | ✅ DONE (implemented early) |
| 6.4 UI | Planned |
| 6.5 Save | Planned |

### 3.8 — Phase 7 (Content & Polish)

After Phase 3+6: 7.0 Asset Cooking → 7.1 Advanced Post-Process → 7.2 Streaming → 7.3 Terrain → 7.4 VFX → 7.5 Advanced Rendering

### 3.9 — Phase 8 (MCP & AI)

After Phase 5: 8.0 MCP Server → 8.1 AI Tools → 8.2 AI Content Pipeline

Includes **VFX AI Tool** (8.2): Claude shaders + ComfyUI textures + TRELLIS.2 meshes, with visual feedback loop (multi-frame contact sheet JPEG for Claude Vision evaluation, ~$0.03/analysis).

### 3.10 — Phase 9 (Shipping)

After everything: 9.0 Platform SDKs → 9.1 Networking → 9.2 Optimization → 9.3 Release

### 3.11 — Critical Path

```
1.0 → 1.1 → 1.2 → 1.3 → 2.0 → 2.1 → 2.2 → 3.0/3.0b → 3.5b → 4.0 → 4.5 → 5.0 → 5.1 → ... → 9.3
```

The critical path runs through **RHI → Shader Pipeline → Runtime Framework → First Pixels → Playable Scene → Editor → Shipping**. Phase 3 parallel tracks (A+B) are the longest segment.

---

## 4. Design Decisions Log

| # | Decision | Date | Rationale |
|---|----------|------|-----------|
| D-001 | Runtime as N-API addon, not separate process | POC 0.2 | IPC/streaming bottleneck proven by POC 0.1 (244 FPS render → 60 FPS display). In-process gives zero-copy GPU sharing. |
| D-002 | SM6.6 as baseline, not SM5 | POC 0.1 | Starting SM5 and upgrading was painful — shaders needed rewriting. Start modern, create fallbacks later. |
| D-003 | JSON for all authored formats | Design phase | LLM-friendly, diffable, schema-validatable. Binary only for cooked/compiled assets. |
| D-004 | Viewport via native HWND, not webview | Design phase | Zero transfer overhead. Webview only for 2D editor panels. |
| D-005 | H.264 MF + WebCodecs for webview streaming | POC 0.2 | 120 FPS proven. For preview panels and remote/headless only. |
| D-006 | Rust-inspired ownership (no GC) | Design phase | Deterministic frame times, cache-friendly, GPU sync compatibility. |
| D-007 | napi/ separate from extension/ | POC 0.2 | Runtime readable independently; .node consumable by CLI/tests; clean build separation. |
| D-008 | Deferred rendering as primary pipeline | Design phase | Standard for AAA (Unreal, Frostbite). Forward pass for transparents only. |
| D-009 | Jolt for physics (not PhysX) | Design phase | MIT license, modern C++, good performance. |
| D-010 | Clean-room from UE 5.6 | Design phase | AAA-quality math/algorithms, zero legal exposure. |
| D-011 | ComfyUI + SDXL for local texture generation | 2026-03-12 | "Nano Banana" is a Google Gemini marketing name, not a real tool. ComfyUI+SDXL is the actual local diffusion stack — free, private, runs on the dev's GPU. |
| D-012 | Visual feedback via multi-frame contact sheet JPEG | 2026-03-12 | Claude Vision analyzes rendered contact sheets to evaluate VFX iteration quality. Cost ~$0.03/analysis. Enables fully autonomous VFX loop. |
| D-013 | Engine Mode — VSCode layout toggle | 2026-03-12 | Toggle VSCode to Unreal/Godot-like IDE layout while preserving AI chat panels. Documented in [docs/engine/32-engine-mode/](engine/32-engine-mode/). |
| D-014 | Phase restructure from 4 phases to 9 phases | 2026-03-12 | Original 4-phase structure was too coarse for planning granularity. 9-phase structure provides clearer milestones, better parallelization, and cleaner dependency tracking. |

---

## 5. Quality Gates

### Phase 1 Gates — MET ✅

| Gate | Status |
|------|--------|
| All unit tests pass (95%+ coverage) | ✅ 183 tests passing (1.2 RHI) + 90 tests (1.3 Shader) |
| Zero compiler warnings | ✅ Clean build across all milestones |
| Zero linter warnings | ✅ |
| Documentation updated in docs/ | ✅ 196 files across 33 sections in docs/engine/ |

### Per-Milestone Gates (ongoing)

- [ ] All unit tests pass (95%+ coverage)
- [ ] Zero compiler warnings
- [ ] Zero linter warnings
- [ ] Performance benchmarks meet targets (see roadmap.md Success Metrics)
- [ ] Documentation updated in docs/engine/
- [ ] Screenshot regression test (when renderer exists)

### Success Metrics (from roadmap.md)

| Metric | Target |
|--------|--------|
| Frame rate | 60 FPS @ 1080p on RTX 3060 / RX 6700 |
| Draw calls | GPU-driven: <100 CPU draw calls |
| Entity count | 100K+ entities with physics @ 60 FPS |
| Particles | 1M+ GPU particles @ 60 FPS |
| Load time | Scene cold <3s, hot reload <500ms |
| Memory | <2GB GPU, <4GB CPU |
| Build time | Incremental <5s (Zig), <15s (MSVC) |
| Screenshot | `--screenshot` <2s |
| AI generation | End-to-end VFX from prompt <60s |

---

## 6. Open Questions

1. **Embedded browser for game UI**: Which library? Ultralight? CEF? Custom lightweight renderer? (Phase 6.4)
2. **DLSS/FSR/XeSS SDK integration timing**: TSR upscaler already implemented; external SDK integration deferred to Phase 7.5
3. **CI/CD pipeline**: GitHub Actions? Self-hosted runners for GPU tests?
4. **Console support timeline**: PS5/Xbox modules — when to start NDA discussions?
5. ~~**Zig vs CMake as primary build**~~: **RESOLVED** — Zig is primary build system, CMake kept as fallback. Proven stable through Phases 1-3.
6. ~~**DX11 backend priority**~~: **RESOLVED** — DX11 backend implemented and at visual parity with DX12. 4 backends total (DX12, DX11, Vulkan, Metal).

---

*This PDR is a living document. Update as decisions are validated and risks are resolved.*
*Reference: [architecture.md](architecture.md) | [roadmap.md](roadmap.md) | [dag.md](dag.md) | [docs/engine/](engine/)*
