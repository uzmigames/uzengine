# Application Lifecycle — Overview

> **Status**: Done (Milestone 2.0)
> **Source**: `runtime/engine/app/`
> **Depends on**: Core systems (logging, config, paths), RHI, Platform window
> **References**: UE5 `FEngineLoop`, Godot `Main::setup/start/iteration/cleanup`

## Purpose

The application lifecycle defines the ordered sequence of initialization, per-frame update, and shutdown for the entire engine. Currently, each demo and tool manages its own init/loop/shutdown — the lifecycle system will replace this boilerplate with a structured framework.

## Problem

Without a lifecycle system, every demo repeats 200+ lines:
- Initialize logging, config, paths, window, RHI, swap chain, command list, fence
- Run a custom render loop
- Shutdown in reverse order (hoping nothing is forgotten)

This is fragile: miss one shutdown step and you get GPU leaks. Miss the ordering and you get crashes.

## Three-Phase Architecture

Inspired by UE5's `FEngineLoop` (PreInit → Init → Tick → Exit) and Godot's `Main::setup/start/iteration/cleanup`.

### Phase 0 — PreInit (Before GPU)

```
├── Initialize logging, config, paths, memory
├── Mount VFS / asset registry
├── Fire: OnPreInit event
└── Load plugins at phase CORE
```

No GPU, no window. Pure CPU-side foundation.

### Phase 1 — Init (GPU + Systems)

```
├── Create platform window
├── Initialize RHI (device, swap chain, command queues)
├── Initialize renderer (shader pipeline, global resources)
├── Load plugins at phase RHI
├── Fire: OnPostInit event
└── Load plugins at phase ENGINE
```

GPU is live. All engine systems are available but no gameplay is running.

### Phase 2 — Start (World Goes Live)

```
├── Create World (if scene specified)
├── Spawn initial entities
├── Call BeginPlay on all entities/components
├── Fire: OnWorldBeginPlay event
└── Load plugins at phase SCENE
```

Gameplay is active. Entities are ticking.

## Main Loop

Fixed-timestep physics + variable-rate rendering, inspired by Godot's `MainTimerSync`:

```
Frame Start
├── Poll OS events (window, input)
├── Update TimeManager (deltaTime, totalTime)
│
│  [Physics Loop — N steps per frame]
│  ├── Fire: OnPrePhysics
│  ├── Tick group: PrePhysics
│  ├── Step physics simulation (fixed dt)
│  ├── Tick group: PostPhysics
│  └── Fire: OnPostPhysics
│
│  [Game Update — once per frame]
│  ├── Fire: OnPreUpdate
│  ├── Tick group: Update
│  ├── Tick group: PostUpdate
│  ├── Flush deferred events
│  └── Fire: OnPostUpdate
│
│  [Render — once per frame]
│  ├── Fire: OnPreRender
│  ├── RenderWorld collects visible entities
│  ├── Build draw calls, submit to RHI
│  ├── Present swap chain
│  └── Fire: OnPostRender
│
└── Process deferred entity destroy
```

### Why Fixed-Timestep Physics?

Variable-timestep physics causes non-deterministic behavior — objects at 30 FPS tunnel through walls that they don't at 60 FPS. Fixed-timestep (e.g., 1/60s) with interpolation produces identical physics results regardless of frame rate.

## Shutdown

Strict reverse order (mirrors UE5's guarantee):

```
Phase 2 — Stop
├── Fire: OnWorldEndPlay
├── Call EndPlay on all entities (reverse creation order)
├── Unload plugins at phase SCENE (reverse)
└── Destroy World

Phase 1 — Deinit
├── Unload plugins at phase ENGINE (reverse)
├── Unload plugins at phase RHI (reverse)
├── Fire: OnPreShutdown
├── Flush GPU, destroy renderer
├── Destroy RHI device
└── Destroy platform window

Phase 0 — Cleanup
├── Unload plugins at phase CORE (reverse)
├── Fire: OnPostShutdown
└── Shutdown logging, config, memory
```

### Why Reverse Order?

If system A was initialized before system B, then B might depend on A. Shutting down A first would leave B referencing destroyed resources. Reverse order guarantees no dangling references.

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Lifecycle model | `FEngineLoop` (PreInit/Init/Tick/Exit) | `Main::setup/start/iteration/cleanup` | **3-phase** (PreInit/Init/Start + reverse shutdown) |
| Tick groups | PrePhysics, DuringPhysics, PostPhysics, PostUpdateWork | `_physics_process`, `_process` | PrePhysics, PostPhysics, Update, PostUpdate |
| Physics timestep | Fixed (substeps) + variable render | Fixed (configurable Hz) + variable render | Fixed + variable render |
| Plugin phases | Module loading order (Early, Default, PostDefault) | GDExtension init levels | CORE, RHI, ENGINE, SCENE |
| Event system | Delegates + multicast delegates | Signals (observer pattern) | Event bus (planned) |

## Rules

1. **ALWAYS initialize in order, shutdown in reverse** — never shortcut the sequence
2. **ALWAYS use fixed-timestep for physics** — variable timestep causes non-deterministic behavior
3. **NEVER call RHI functions before Phase 1 Init** — GPU isn't ready
4. **NEVER skip EndPlay** — components must clean up GPU resources, event subscriptions, etc.
5. **ALWAYS process deferred destroys at end of frame** — never destroy entities mid-iteration
