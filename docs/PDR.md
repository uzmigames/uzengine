# UzEngine 0.2 — Project Design Review (PDR)

> **Repository**: https://github.com/uzmigames/uzengine
> **Date**: 2026-03-10
> **Status**: Phase 1 — Runtime Foundation (pre-implementation)
> **POC Status**: POC 0.2 validated (N-API + DX12 + H.264 streaming @ 120 FPS)

---

## 1. Project Summary

**UzEngine** is a AAA-quality game engine fully embedded in VSCode, designed as the first engine built for autonomous AI-driven development. It uses open JSON file formats, a C++ runtime loaded as an N-API `.node` addon, and an MCP server for LLM integration.

### Key Differentiators
- **AI-first**: open formats (JSON everywhere), visual feedback loop (screenshot CLI), MCP tools for LLM manipulation
- **VSCode-native**: engine runs inside the editor process (N-API addon), not as a separate app
- **AAA rendering**: Unreal 5.6-quality renderer (clean-room), SM6.6 baseline, deferred PBR pipeline
- **No GC**: Rust-inspired ownership/borrow model in C++ for deterministic performance

---

## 2. Architecture Validation

### 2.1 — Runtime Execution Model: **VALIDATED**

| Decision | Validation | Risk |
|----------|-----------|------|
| N-API in-process addon | POC 0.2: DX12 render + H.264 encode + WebCodecs decode = 120 FPS at 854x480 with 50K instances | Addon crash kills extension host (Electron restarts in ~1s) — **acceptable** |
| Electron ABI matching | POC 0.2: `--runtime=electron --runtime-version=39.6.0`, delay-load hook, `NAPI_DISABLE_CPP_EXCEPTIONS` | ABI must match exactly; documented in MEMORY.md |
| H.264 frame streaming (webview panels) | POC 0.2: MF HW encoder 2.2ms + WebCodecs 3.3ms = 120 FPS sustained | Only for preview/remote panels — viewport uses shared GPU surface |
| Viewport via native HWND | Design decision — runtime creates own DX12 swapchain window | **NOT YET VALIDATED** — critical for Phase 2.1 |

### 2.2 — Technology Choices: **REVIEWED**

| Component | Choice | Rationale | Risk |
|-----------|--------|-----------|------|
| Graphics API | DX12 SM6.6 (primary) | Modern features, POC proved SM5-first was painful | Windows-only initially |
| Physics | Jolt | Fast, modern, MIT license | Less mature than PhysX |
| ECS | entt | Header-only, fast, well-maintained | No built-in serialization |
| Audio | miniaudio | Single-file, cross-platform | Limited spatial audio |
| Build system | Zig (primary) + CMake fallback | Fast incremental builds | Zig ecosystem still young |
| Scripting | C++ (primary) + Lua (secondary) | Performance + rapid prototyping | Two language support surfaces |
| File formats | JSON everywhere | LLM-friendly, human-readable, diffable | Slower parse vs binary (mitigated by cooked assets) |
| UI (game) | Embedded HTML/CSS | LLM-friendly, designer-accessible | Browser embedding adds complexity |

### 2.3 — Unvalidated Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Native HWND inside VSCode | High — viewport rendering depends on this | Prototype early in Phase 2.1, fallback to H.264 streaming |
| SM6.6 hardware coverage | Medium — excludes pre-2020 GPUs | SM5 fallback backend planned |
| Zig build system maturity | Medium — may hit toolchain issues | CMake fallback always available |
| C++ hot-reload (DLL swap) | Medium — state serialization is fragile | Prioritize Lua scripting for iteration speed |
| Memory model (borrow checker in C++) | Low-Medium — debug overhead, learning curve | Debug-only validation, zero-cost in release |
| Embedded browser for game UI | Medium — memory/performance overhead | Evaluate lightweight alternatives (custom renderer) |

---

## 3. Scope Analysis

### 3.1 — Phase 1 (Runtime Foundation) — **Estimated: 12-18 months**

| Milestone | Complexity | Critical Path? |
|-----------|-----------|---------------|
| 1.0 Bootstrap | Medium | **YES** — everything depends on this |
| 1.1 Math & Containers | Low-Medium | **YES** — RHI needs math |
| 1.2 RHI (DX12 SM6.6) | **HIGH** | **YES** — renderer needs RHI |
| 1.3 Renderer | **VERY HIGH** | **YES** — core deliverable |
| 1.4 Scene System | Medium-High | **YES** — gameplay needs scenes |
| 1.5 Gameplay Systems | **VERY HIGH** (aggregate) | Parallelizable sub-milestones |

### 3.2 — Critical Path

```
1.0 → 1.1 → 1.2 → 1.3.0 → 1.3.1 → 1.3.2 → 1.4 → 1.5.x
                                                    ↗ (parallel)
                              1.5.0 Shader Compiler ─
```

The critical path runs through **RHI → Renderer → Scene**. Physics, animation, audio, etc. can be parallelized after 1.4.

### 3.3 — Phase 1.5 (Build Pipeline) — **Start early, parallel to 1.3**

Shader compiler should start alongside the renderer (they co-depend). Asset processor and packager follow.

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

---

## 5. Quality Gates

### Per-Milestone Gates
- [ ] All unit tests pass (95%+ coverage)
- [ ] Zero compiler warnings
- [ ] Zero linter warnings
- [ ] Performance benchmarks meet targets (see ROADMAP.md Success Metrics)
- [ ] Documentation updated in docs/
- [ ] Screenshot regression test (when renderer exists)

### Success Metrics (from ROADMAP.md)

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

1. **Zig vs CMake as primary build**: Zig is fast but ecosystem is immature. Should we commit fully or use CMake primary?
2. **Embedded browser for game UI**: Which library? Ultralight? CEF? Custom lightweight renderer?
3. **DLSS/FSR/XeSS SDK integration timing**: Include in 1.3.3 or defer to Phase 4.2?
4. **CI/CD pipeline**: GitHub Actions? Self-hosted runners for GPU tests?
5. **Console support timeline**: PS5/Xbox modules — when to start NDA discussions?

---

*This PDR is a living document. Update as decisions are validated and risks are resolved.*
*Reference: [ARCHITECTURE.md](ARCHITECTURE.md) | [ROADMAP.md](ROADMAP.md) | [DAG.md](DAG.md)*
