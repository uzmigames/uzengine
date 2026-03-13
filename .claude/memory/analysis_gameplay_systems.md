---
name: Gameplay Systems Build Order Analysis for Roadmap
description: Gameplay systems dependency chain and priority for UzEngine roadmap
type: project
---

# Gameplay Systems Build Order Analysis

Analysis date: 2026-03-12
Based on: ROADMAP.md + docs/engine/ sections 11, 13-20

---

## 1. Dependency Map

```
                    Platform Window (1.0) ✅
                         │
                    Math/Containers (1.1) ✅
                         │
                      RHI (1.2) ✅
                         │
                    Renderer (1.3)
                         │
                   Scene/ECS (1.4)
                    ╱    │    ╲
                  ╱      │      ╲
          Input (§13)  Camera (§14)  Scripting-C++ (§15)
             │           │              │
             └─────┬─────┘              │
                   │                    │
              Physics (§16) ◄───────────┘
              ╱    │    ╲
            ╱      │      ╲
     Audio (§17)  Anim (§19)  Camera-SpringArm (§14)
        │          │              │
        │          │         [SpringArm needs physics sweep]
        │          │
        └────┬─────┘
             │
      Navigation (§20.5) ◄── needs physics + scene geometry
             │
      Behavior Trees (§20.3) ◄── needs navigation + blackboard
             │
      UI Runtime (§18) ◄── needs input routing, data binding
             │
      Save System (§20.4) ◄── needs ECS serialization, all systems stable
             │
      Scripting-Lua (§15) ◄── needs all C++ APIs stable to bind
             │
      World Streaming (§11) ◄── needs scene + physics + navigation
             │
      Terrain/Foliage (§11) ◄── needs renderer + streaming
             │
      VFX/Particles (§20.6) ◄── needs renderer + GPU compute
             │
      Ability System (§20.2) ◄── needs animation + gameplay effects
```

## 2. Input -> Camera -> Physics -> Animation Chain

This is the core gameplay frame loop. Order matters:

```
Frame Tick Order:
1. Input System        — poll hardware, run modifier/trigger pipeline, fire callbacks
2. Game Logic / Scripts — consume input, make decisions
3. Physics             — simulate 120Hz fixed step, resolve collisions, sweep queries
4. Animation           — evaluate state machines, sample keyframes, compute joints
5. Camera              — follow target (post-physics position), SpringArm sweep (physics query)
6. Audio               — update listener position (from camera), spatialization
7. Render              — submit to RenderWorld with final transforms
```

Key coupling points:
- **Camera depends on Physics**: SpringArm collision avoidance uses `physics_query_sphere_cast()`
- **Animation depends on Physics**: root motion feeds into character movement component
- **Audio depends on Camera**: listener position = active camera position
- **Navigation depends on Physics**: agents use character controller movement

## 3. When to Introduce Scripting (Lua)

**Recommendation: Lua LATE (after C++ gameplay systems are stable)**

Rationale from docs:
- Scripting §15 shows C++ as Phase 2, Lua as Phase 3
- Lua binds to `Vec3, Entity, World, Input, Audio, Physics` — these must exist first
- Lua API surface (`ctx.world`, `ctx.input`, `ctx.audio`) requires stable C++ APIs
- C++ Behavior system (Phase 2) provides game logic without Lua
- Hot-reload of C++ DLL serves the rapid iteration need initially

**Build order**:
1. C++ Behavior system + UZ_REGISTER_BEHAVIOR (Phase 2) — enough for demos
2. All C++ gameplay systems (input, camera, physics, animation, audio)
3. Lua VM + bindings (Phase 3) — bind stable APIs, enable modding

## 4. Audio, UI, VFX — When Necessary?

### Audio (§17)
- **Phase 2 basics**: PlayAt/Play2D, bus tree, basic attenuation
- **Minimum playable**: needs at least fire-and-forget 2D audio (menu clicks, collisions)
- **Can defer**: spatialization, occlusion, voice virtualization, music system
- **Dependency**: only needs scene system (for AudioSourceComponent position)

### UI Runtime (§18)
- **Phase 3**: HTML/CSS renderer + data binding
- **Minimum playable**: imgui debug overlay (already planned for Phase 2) is sufficient
- **Game UI**: not needed until you have a game that needs menus/HUD
- **Dependency**: input routing (§13 context stack for modal UI), renderer (overlay compositing)

### VFX/Particles (1.5.6)
- **Late**: requires GPU compute pipeline, renderer depth buffer (for collision)
- **Not needed for minimum playable**
- **Dependency**: renderer post-processing, compute shaders

## 5. Navigation/AI — When?

**Phase 3, after physics + scene are stable.**

Dependency chain:
```
Scene geometry (1.4) → NavMesh baking (Recast, offline via uzcook)
Physics (1.5.0)     → Agent character movement (CharacterMovementComponent)
NavMesh + Physics   → NavMeshAgentComponent (runtime pathfinding + steering)
NavMeshAgent        → Behavior Trees (AI decision-making drives MoveTo)
```

Navigation baking is offline (uzcook tool), so it can be developed in parallel with physics.
Runtime pathfinding needs physics for agent movement.
BT nodes like `MoveTo` need NavMeshAgent.

## 6. World Streaming, Terrain, Foliage

**Advanced systems — defer to late Phase 3 or Phase 4.**

### World Streaming (§11)
- Needs: scene system, physics (cross-cell contacts), save system (per-cell state), async I/O
- Grid-based partition + streaming volumes
- Not needed until world exceeds single-scene memory budget
- **When**: after all core gameplay works in a single scene

### Terrain (1.5.5)
- Needs: renderer (clipmap LOD, virtual texturing), physics (heightmap collider)
- Not needed for indoor/small scenes
- **When**: when open-world gameplay is targeted

### Foliage (1.5.5)
- Needs: renderer (GPU instancing, per-instance culling), terrain (placement)
- Pure visual system, no gameplay dependency
- **When**: after terrain, for visual polish

## 7. "Minimum Playable Game" Definition

### Pong-level (simplest possible game):
```
Required:
├── Renderer (1.3.0)     — draw rectangles/sprites on screen
├── Scene/ECS (1.4)      — entities with transforms
├── Input (§13)          — keyboard left/right or up/down
├── Physics (§16 basic)  — ball-paddle collision (or manual AABB check)
└── Audio (§17 basic)    — bounce sound effect (fire-and-forget 2D)

NOT required:
├── Camera (beyond basic ortho)
├── Animation
├── Scripting (Lua)
├── Navigation/AI
├── UI (score can be debug text)
├── Save system
├── World streaming
├── Terrain/foliage
└── VFX
```

### Third-person action demo (more realistic MVP):
```
Required:
├── Renderer (1.3.1)     — deferred pipeline, basic lighting
├── Scene/ECS (1.4)      — entity hierarchy, prefabs
├── Input (§13)          — full enhanced input (gamepad + keyboard)
├── Camera (§14)         — SpringArm third-person follow
├── Physics (§16)        — rigidbodies, raycasts, character controller
├── Animation (§19)      — skeletal mesh, state machine (idle/walk/run)
├── Audio (§17)          — footsteps, ambient, 3D positional
└── C++ Behaviors (§15)  — game logic (player controller, enemy AI stub)

Stretch:
├── Navigation (§20.5)   — enemy pathfinding
├── Behavior Trees        — patrol/chase AI
├── UI (§18)             — health bar, pause menu
└── Save System          — checkpoint save
```

## 8. Recommended Implementation Order

| Priority | System | Milestone | Depends On | Enables |
|----------|--------|-----------|------------|---------|
| 1 | **Input System** | 1.5 (first) | Scene/ECS (1.4) | All gameplay, camera, UI |
| 2 | **Physics (core)** | 1.5.0 | Scene/ECS | Camera spring arm, navigation, character movement |
| 3 | **Camera System** | 1.5 (with physics) | Input + Physics | Playable third-person, audio listener |
| 4 | **C++ Behaviors** | 1.5.4 (partial) | Scene/ECS | Game logic without Lua |
| 5 | **Animation (core)** | 1.5.1 | Renderer + Scene | Skinned characters, state machines |
| 6 | **Audio (core)** | 1.5.2 | Scene (position) | Sound feedback, spatial audio |
| 7 | **Navigation** | 1.5.3 | Physics + Scene geometry | AI movement |
| 8 | **Behavior Trees** | 1.5.3 | Navigation | AI decision-making |
| 9 | **Save System** | 1.5.4 | ECS serialization | Persistence |
| 10 | **Lua Scripting** | 1.5.4 | All C++ APIs stable | Rapid iteration, modding |
| 11 | **UI Runtime** | 1.5.7 | Input + Renderer | Game menus, HUD |
| 12 | **Data Tables** | 1.5 (any time) | Core systems | Data-driven design |
| 13 | **VFX/Particles** | 1.5.6 | Renderer compute | Visual polish |
| 14 | **World Streaming** | 1.5.5 | Scene + Physics + Save | Open worlds |
| 15 | **Terrain** | 1.5.5 | Renderer + Physics | Open-world landscapes |
| 16 | **Foliage** | 1.5.5 | Terrain + Renderer | Visual density |
| 17 | **Ability System** | Phase 4 | Animation + Gameplay effects | Combat systems |

## 9. What Can Be Deferred to Post-MVP

| System | Why Deferrable |
|--------|---------------|
| Lua scripting | C++ Behaviors sufficient for demos |
| UI Runtime (HTML/CSS) | imgui debug UI covers dev needs |
| VFX/Particles | Visual polish, not gameplay-blocking |
| World Streaming | Only needed for large open worlds |
| Terrain/Foliage | Only needed for outdoor environments |
| Ability System (GAS) | Only needed for RPG/action combat |
| RVO2 crowd avoidance | Basic navigation works without it |
| Audio occlusion/HRTF | Basic spatialization sufficient |
| Save system | Can be added any time (ECS serialization is the hard part) |
| Behavior tree decorators/services | Basic Sequence/Selector sufficient for simple AI |

## 10. Key Risk: Input Must Come First

The roadmap places scripting (1.5.4) after navigation (1.5.3), but input is buried inside 1.5 generically. Input should be the FIRST gameplay system because:
- Every other system needs it for testing (camera controls, debug commands)
- It has minimal dependencies (just platform layer + ECS)
- The enhanced input pipeline (modifiers, triggers, contexts) takes time to get right
- Without input, you cannot manually test any gameplay system
