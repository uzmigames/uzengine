# UzEngine 0.2 — Dependency Acyclic Graph (DAG)

> Milestone dependency graph. Arrows mean "depends on". Read top-to-bottom.

---

## Visual DAG

```
                            ┌──────────────┐
                            │  1.0         │
                            │  Bootstrap   │
                            └──────┬───────┘
                                   │
                            ┌──────▼───────┐
                            │  1.1         │
                            │  Math &      │
                            │  Containers  │
                            └──────┬───────┘
                                   │
                            ┌──────▼───────┐
                            │  1.2         │
                            │  RHI         │
                            │  (DX12 SM6.6)│
                            └──────┬───────┘
                                   │
                  ┌────────────────┼────────────────┐
                  │                │                 │
           ┌──────▼───────┐┌──────▼───────┐         │
           │  1.3.0       ││  1.5.0       │         │
           │  Triangle to ││  Shader      │         │
           │  Screen      ││  Compiler    │         │
           └──────┬───────┘└──────┬───────┘         │
                  │               │                  │
           ┌──────▼───────┐      │                  │
           │  1.3.1       │      │                  │
           │  Deferred    │      │                  │
           │  Pipeline    │      │                  │
           └──────┬───────┘      │                  │
                  │               │                  │
           ┌──────▼───────┐      │                  │
           │  1.3.2       │      │                  │
           │  Post-       │      │                  │
           │  Processing  │      │                  │
           └──────┬───────┘      │                  │
                  │               │                  │
                  └───────┬───────┘                  │
                          │                          │
                   ┌──────▼───────┐                  │
                   │  1.4         │                  │
                   │  Scene       │                  │
                   │  System      │                  │
                   └──────┬───────┘                  │
                          │                          │
     ┌────────┬───────┬───┼───┬────────┬─────────┐  │
     │        │       │   │   │        │         │  │
┌────▼──┐┌───▼──┐┌───▼─┐│┌──▼───┐┌───▼──┐┌─────▼──┤
│ 1.5.0 ││1.5.1 ││1.5.2│││1.5.3 ││1.5.4 ││ 1.5.5  │
│Physics││Anim  ││Audio│││Nav&AI││Script││Terrain │
└───┬───┘└──┬───┘└──┬──┘│└──┬───┘└──┬───┘└────┬───┘
    │       │       │   │   │       │         │
    │       │       │   │   │    ┌──▼───┐     │
    │       │       │   │   │    │1.5.6 │     │
    │       │       │   │   │    │VFX & │     │
    │       │       │   │   │    │Parti.│     │
    │       │       │   │   │    └──┬───┘     │
    │       │       │   │   │       │         │
    │       │       │   │   │    ┌──▼───┐     │
    │       │       │   │   │    │1.5.7 │     │
    │       │       │   │   │    │UI    │     │
    │       │       │   │   │    │Runtime│    │
    │       │       │   │   │    └──┬───┘     │
    │       │       │   │   │       │         │
    └───────┴───────┴───┼───┴───────┴─────────┘
                        │
              ┌─────────┼──────────┐
              │         │          │
       ┌──────▼───┐     │   ┌──────▼───────┐
       │  1.5.1   │     │   │  1.5.2       │
       │  Asset   │     │   │  Packager    │
       │  Process.│     │   └──────┬───────┘
       └──────┬───┘     │          │
              │         │          │
              └─────────┼──────────┘
                        │
                 ┌──────▼───────┐
                 │  2.0         │
                 │  N-API       │◄──── POC 0.2 VALIDATED
                 │  Foundation  │
                 └──────┬───────┘
                        │
                 ┌──────▼───────┐
                 │  2.1         │
                 │  Viewport &  │
                 │  Core UI     │
                 └──────┬───────┘
                        │
              ┌─────────┼──────────┐
              │                    │
       ┌──────▼───────┐    ┌──────▼───────┐
       │  2.2         │    │  3.0         │
       │  Core        │    │  MCP Server  │
       │  Editors     │    └──────┬───────┘
       └──────┬───────┘           │
              │            ┌──────▼───────┐
       ┌──────▼───────┐    │  3.1         │
       │  2.3         │    │  AI Tools    │
       │  Advanced    │    └──────┬───────┘
       │  Editors     │           │
       └──────┬───────┘    ┌──────▼───────┐
              │            │  3.2         │
              │            │  AI Content  │
              │            │  Pipeline    │
              │            └──────┬───────┘
              │                   │
              └─────────┬─────────┘
                        │
              ┌─────────┼──────────┐
              │         │          │
       ┌──────▼──┐┌─────▼────┐┌───▼────────┐
       │  4.0    ││  4.1     ││  4.2       │
       │Platform ││Networking││Optimization │
       │  SDKs   ││          ││             │
       └─────┬───┘└────┬─────┘└─────┬──────┘
             │         │            │
             └─────────┼────────────┘
                       │
                ┌──────▼───────┐
                │  4.3         │
                │  Release     │
                └──────────────┘
```

---

## Dependency Table

| Milestone | Depends On | Blocks |
|-----------|-----------|--------|
| **1.0** Bootstrap | — | 1.1 |
| **1.1** Math & Containers | 1.0 | 1.2 |
| **1.2** RHI (DX12 SM6.6) | 1.1 | 1.3.0, 1.5.0-SC |
| **1.3.0** Triangle to Screen | 1.2 | 1.3.1 |
| **1.3.1** Deferred Pipeline | 1.3.0 | 1.3.2 |
| **1.3.2** Post-Processing | 1.3.1 | 1.4 |
| **1.3.3** Advanced Rendering | 1.3.2 | 4.2 |
| **1.4** Scene System | 1.3.2 | 1.5.0-P through 1.5.7 |
| **1.5.0-P** Physics | 1.4 | 2.0 |
| **1.5.1-A** Animation | 1.4 | 2.0 |
| **1.5.2-AU** Audio | 1.4 | 2.0 |
| **1.5.3** Nav & AI | 1.4 | 2.0 |
| **1.5.4** Scripting | 1.4 | 2.0 |
| **1.5.5** Terrain & World | 1.4 | 2.0 |
| **1.5.6** VFX & Particles | 1.5.4 | 2.0 |
| **1.5.7** UI Runtime | 1.5.6 | 2.0 |
| **1.5.0-SC** Shader Compiler | 1.2 | 1.5.1-AP |
| **1.5.1-AP** Asset Processor | 1.5.0-SC, 1.4 | 1.5.2-PK |
| **1.5.2-PK** Packager | 1.5.1-AP | 2.0 |
| **2.0** N-API Foundation | Phase 1 + 1.5 complete | 2.1 |
| **2.1** Viewport & Core UI | 2.0 | 2.2, 3.0 |
| **2.2** Core Editors | 2.1 | 2.3 |
| **2.3** Advanced Editors | 2.2 | 4.x |
| **3.0** MCP Server | 2.1 | 3.1 |
| **3.1** AI Tools | 3.0 | 3.2 |
| **3.2** AI Content Pipeline | 3.1 | 4.x |
| **4.0** Platform SDKs | 2.3, 3.2 | 4.3 |
| **4.1** Networking | 2.3, 3.2 | 4.3 |
| **4.2** Optimization | 1.3.3, 2.3, 3.2 | 4.3 |
| **4.3** Release | 4.0, 4.1, 4.2 | — |

---

## Critical Path (longest dependency chain)

```
1.0 → 1.1 → 1.2 → 1.3.0 → 1.3.1 → 1.3.2 → 1.4 → 1.5.x → 2.0 → 2.1 → 2.2 → 2.3 → 4.x → 4.3
```

**Critical path length**: 14 milestones (from Bootstrap to Release)

---

## Parallelization Opportunities

| Window | Can Run In Parallel |
|--------|-------------------|
| After 1.2 (RHI done) | **1.3.0** + **1.5.0-SC** (Shader Compiler) |
| After 1.4 (Scene done) | **1.5.0-P** + **1.5.1-A** + **1.5.2-AU** + **1.5.3** + **1.5.4** + **1.5.5** (6 gameplay milestones) |
| After 1.5.0-SC | **1.5.1-AP** (Asset Processor) — parallel with gameplay milestones |
| After 2.1 (Viewport done) | **2.2** + **3.0** (Editors + MCP Server) |
| After 2.3 + 3.2 | **4.0** + **4.1** + **4.2** (Platform + Networking + Optimization) |

---

## Validated Nodes (POC 0.2)

```
✅ 2.0 — N-API Foundation (partial)
   ✅ .node compilation via cmake-js
   ✅ Electron ABI matching (delay-load, NAPI_DISABLE_CPP_EXCEPTIONS)
   ✅ H.264 frame streaming (MF HW + WebCodecs = 120 FPS)
   🔄 MediaStreamTrackGenerator (testing)
   ⬜ WebRTC local peer (planned)
   ⬜ napi/ + extension/ split (planned)
   ⬜ Shared GPU surface / native HWND (planned)
```

---

*Reference: [ARCHITECTURE.md](ARCHITECTURE.md) | [ROADMAP.md](ROADMAP.md) | [PDR.md](PDR.md)*
