# VFX Graph Editor — Particle System Authoring

> **Status**: Planned (Phase 3–4)
> **Source**: `extension/editor/vfx-editor.ts`, `engine/runtime/vfx/vfx_graph.h`
> **References**: Node Graph Framework (§40.09), Asset Pipeline (§43), UE5 Niagara, Godot GPUParticles3D, Unity VFX Graph

## Purpose

The VFX Graph Editor specializes the Node Graph Framework (§40.09) for visual particle system authoring. Designers chain together modular VFX nodes without writing code. The emitter forms a data-driven pipeline: Spawn Rate → Initial Velocity → Physics (gravity/drag) → Rendering (sprite/mesh/ribbon). The system supports live preview in the viewport, hot-reload on save, and a marketplace for reusable module presets.

## Architecture

```
VFX Editor (TypeScript/Webview)
    │
    ├─ Graph Panel (node canvas)
    │   ├─ Spawn Rate (how many particles per frame)
    │   ├─ Spawn Shape (point, box, sphere, cone, circle, line)
    │   ├─ Initial Velocity (direction + speed)
    │   ├─ Initial Color / Lifetime / Size
    │   │
    │   ├─ Physics Modules (connected to Spawn)
    │   │   ├─ Gravity (constant acceleration)
    │   │   ├─ Drag (air resistance)
    │   │   ├─ Collision (bounce/stick on meshes)
    │   │   ├─ Noise (turbulence: Perlin/Simplex)
    │   │   └─ Attraction (force toward target point/entity)
    │   │
    │   ├─ Update Modules (per-frame updates)
    │   │   ├─ Kill (lifetime expiry, bounds checking)
    │   │   ├─ Color Over Life (gradient editor)
    │   │   └─ Scale Over Life (size curve)
    │   │
    │   └─ Render Modules (output)
    │       ├─ Sprite (billboard, size from curve)
    │       ├─ Mesh (instanced static mesh)
    │       ├─ Ribbon (trail connecting consecutive particles)
    │       └─ Light (emissive particle with falloff)
    │
    ├─ Parameters Panel (right sidebar)
    │   ├─ Spawn Rate slider (particles/sec)
    │   ├─ Spawn Lifetime range (min/max)
    │   ├─ Render Target (material reference)
    │   └─ Performance Preset (CPU vs GPU)
    │
    ├─ Curve Editors (per-module)
    │   ├─ Right-click input → "Add Curve"
    │   └─ Keyframe editor with Bezier tangents
    │
    ├─ Viewport Preview
    │   ├─ Play / Pause / Stop buttons
    │   ├─ Live particle count + GPU time
    │   └─ Continuous hot-reload on graph save
    │
    └─ Module Marketplace
        ├─ Save current subgraph as preset
        ├─ Browse community presets (fire, smoke, sparks, etc.)
        └─ Import preset → add to graph

Runtime: VFX Graph → .uzvfx (JSON) → Load Graph → Compile to GPU compute shader
```

## VFX Module Types

### Spawn Modules

Control initial particle state when born.

```cpp
/// Base class for spawn modules (set up new particles).
struct SpawnModule {
    virtual void Execute(SpawnContext& ctx, float delta_time) = 0;
};

/// Spawn Rate module: emit N particles per frame.
struct SpawnRateModule : SpawnModule {
    float particles_per_second = 100.0f;

    /// Execute spawns particles_per_second * delta_time new particles.
    void Execute(SpawnContext& ctx, float delta_time) override;
};

/// Spawn Shape module: set position of new particles in a shape.
enum class SpawnShape { Point, Box, Sphere, Cone, Circle, Line };
struct SpawnShapeModule : SpawnModule {
    SpawnShape shape = SpawnShape::Point;
    Vec3 extents = {1.0f, 1.0f, 1.0f};  // Box extents or sphere radius
    float cone_angle = 45.0f;             // For cone

    void Execute(SpawnContext& ctx, float delta_time) override;
};

/// Initial Velocity module: set velocity of new particles.
struct InitialVelocityModule : SpawnModule {
    Vec3 direction = {0.0f, 1.0f, 0.0f};  // Direction (normalized)
    float speed = 5.0f;                    // Magnitude (m/s)
    float randomness = 0.1f;               // Random variation

    void Execute(SpawnContext& ctx, float delta_time) override;
};
```

### Physics Modules

Affect particle motion over time.

```cpp
/// Base class for physics modules (per-particle updates).
struct PhysicsModule {
    virtual void Execute(ParticleBuffer& particles, float delta_time) = 0;
};

/// Gravity module: constant downward acceleration.
struct GravityModule : PhysicsModule {
    float strength = 9.81f;  // Acceleration magnitude (m/s²)

    void Execute(ParticleBuffer& particles, float delta_time) override;
};

/// Drag module: velocity damping (air resistance).
struct DragModule : PhysicsModule {
    float coefficient = 0.1f;  // Drag factor (0-1, typically)

    void Execute(ParticleBuffer& particles, float delta_time) override;
};

/// Collision module: particles bounce off geometry.
struct CollisionModule : PhysicsModule {
    EntityHandle collider_entity;  // Reference to scene mesh/terrain
    float bounce_elasticity = 0.7f;
    float friction = 0.2f;
    bool stick_on_impact = false;  // Kill and stick to surface

    void Execute(ParticleBuffer& particles, float delta_time) override;
};

/// Noise module: Perlin/Simplex turbulence.
struct NoiseModule : PhysicsModule {
    enum class NoiseType { Perlin, Simplex };
    NoiseType type = NoiseType::Perlin;
    float scale = 1.0f;              // World space frequency
    float strength = 0.5f;           // Force magnitude
    float time_scale = 1.0f;         // Animation speed

    void Execute(ParticleBuffer& particles, float delta_time) override;
};

/// Attraction module: particles pulled toward target point or entity.
struct AttractionModule : PhysicsModule {
    Vec3 target_position = {0.0f, 0.0f, 0.0f};
    EntityHandle target_entity;  // If set, follows entity position
    float strength = 5.0f;           // Attraction force magnitude
    float falloff_radius = 100.0f;   // Distance at which effect weakens

    void Execute(ParticleBuffer& particles, float delta_time) override;
};
```

### Render Modules

Define how particles appear on screen.

```cpp
/// Base class for render modules (draw particles).
struct RenderModule {
    virtual void Render(const ParticleBuffer& particles, RenderContext& ctx) = 0;
};

/// Sprite module: billboard particles (always face camera).
struct SpriteModule : RenderModule {
    AssetRef material;           // Material with base texture
    bool align_to_motion = false;  // Orient toward velocity direction

    void Render(const ParticleBuffer& particles, RenderContext& ctx) override;
};

/// Mesh module: instanced static mesh per particle.
struct MeshModule : RenderModule {
    AssetRef mesh;               // Static mesh asset
    AssetRef material;           // Material override
    bool random_rotation = false;

    void Render(const ParticleBuffer& particles, RenderContext& ctx) override;
};

/// Ribbon module: trail connecting particles (like a ribbon or comet tail).
struct RibbonModule : RenderModule {
    AssetRef material;
    float max_length = 10.0f;    // Max distance of ribbon trail
    int subdivision_count = 4;   // Sub-segments per particle

    void Render(const ParticleBuffer& particles, RenderContext& ctx) override;
};

/// Light module: emissive particles that emit light into the scene.
struct LightModule : RenderModule {
    Vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float radius = 5.0f;         // Light falloff radius

    void Render(const ParticleBuffer& particles, RenderContext& ctx) override;
};
```

## Parameter Curves

Any module input can be driven by a curve over particle lifetime:

```typescript
/// Curve editor: click input → "Add Curve" → open keyframe panel.
export class CurveEditor {
    private keyframes: Array<{time: number, value: number, tangent_in: number, tangent_out: number}> = [];
    private canvas: HTMLCanvasElement;

    constructor(private container: HTMLElement) {
        this.setupUI();
    }

    /// Add keyframe at time (0-1, normalized lifetime).
    addKeyframe(time: number, value: number) {
        this.keyframes.push({time, value, tangent_in: 0, tangent_out: 0});
        this.keyframes.sort((a, b) => a.time - b.time);
        this.render();
    }

    /// Evaluate curve at normalized time (0-1).
    evaluate(t: number): number {
        // Hermite interpolation between keyframes
        const lower = this.keyframes.find(k => k.time <= t);
        const upper = this.keyframes.find(k => k.time > t);

        if (!lower || !upper) {
            return lower?.value ?? upper?.value ?? 0;
        }

        const dt = upper.time - lower.time;
        const local_t = (t - lower.time) / dt;

        return this.hermiteInterpolate(lower, upper, local_t);
    }

    private hermiteInterpolate(a: any, b: any, t: number): number {
        // Hermite polynomial for smooth curves
        const h00 = 2*t*t*t - 3*t*t + 1;
        const h10 = t*t*t - 2*t*t + t;
        const h01 = -2*t*t*t + 3*t*t;
        const h11 = t*t*t - t*t;

        return h00 * a.value + h10 * a.tangent_out + h01 * b.value + h11 * b.tangent_in;
    }

    private render() {
        const ctx = this.canvas.getContext('2d')!;
        ctx.clearRect(0, 0, this.canvas.width, this.canvas.height);

        // Draw grid
        ctx.strokeStyle = '#333';
        ctx.lineWidth = 1;
        for (let i = 0; i <= 4; i++) {
            const x = (i / 4) * this.canvas.width;
            const y = (i / 4) * this.canvas.height;
            ctx.beginPath();
            ctx.moveTo(x, 0);
            ctx.lineTo(x, this.canvas.height);
            ctx.stroke();
        }

        // Draw curve
        ctx.strokeStyle = '#0f0';
        ctx.lineWidth = 2;
        ctx.beginPath();
        for (let i = 0; i <= 100; i++) {
            const t = i / 100;
            const val = this.evaluate(t);
            const x = t * this.canvas.width;
            const y = (1 - val) * this.canvas.height;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // Draw keyframes
        for (const kf of this.keyframes) {
            const x = kf.time * this.canvas.width;
            const y = (1 - kf.value) * this.canvas.height;
            ctx.fillStyle = '#0f0';
            ctx.fillRect(x - 3, y - 3, 6, 6);
        }
    }
}
```

## .uzvfx File Format

VFX graphs save as JSON for transparency and AI manipulation:

```json
{
  "name": "fire_burst",
  "description": "Orange explosion particles with smoke trail",
  "version": "1.0",
  "max_particles": 1000,
  "spawn_modules": [
    {
      "type": "SpawnRate",
      "id": "spawn_1",
      "particles_per_second": 500.0
    },
    {
      "type": "SpawnShape",
      "id": "shape_1",
      "shape": "Sphere",
      "extents": [0.5, 0.5, 0.5]
    },
    {
      "type": "InitialVelocity",
      "id": "vel_1",
      "direction": [0.0, 1.0, 0.0],
      "speed": 8.0,
      "randomness": 0.3
    }
  ],
  "physics_modules": [
    {
      "type": "Gravity",
      "id": "gravity_1",
      "strength": 3.0
    },
    {
      "type": "Drag",
      "id": "drag_1",
      "coefficient": 0.5
    },
    {
      "type": "Noise",
      "id": "turbulence_1",
      "type": "Perlin",
      "scale": 2.0,
      "strength": 2.0
    }
  ],
  "render_modules": [
    {
      "type": "Sprite",
      "id": "render_1",
      "material": "res://materials/particle_fire.uzmat",
      "align_to_motion": true
    }
  ],
  "curves": {
    "color_over_life": {
      "module_id": "render_1",
      "property": "color",
      "keyframes": [
        {"time": 0.0, "value": [1.0, 0.5, 0.0, 1.0]},
        {"time": 0.5, "value": [1.0, 0.8, 0.0, 0.8]},
        {"time": 1.0, "value": [0.2, 0.2, 0.2, 0.0]}
      ]
    },
    "scale_over_life": {
      "module_id": "render_1",
      "property": "size",
      "keyframes": [
        {"time": 0.0, "value": 0.5},
        {"time": 0.5, "value": 1.0},
        {"time": 1.0, "value": 0.0}
      ]
    }
  }
}
```

## Live Preview

The viewport shows particles in real-time as you edit:

```typescript
/// VFX preview in viewport (native HWND).
export class VFXPreview {
    private vfxGraph: VFXGraph | null = null;
    private emitter: VFXEmitter | null = null;
    private isPlaying: boolean = true;
    private statsOverlay: StatsOverlay;

    async loadGraph(graphPath: string) {
        this.vfxGraph = await this.ipc.call("vfx.load_graph", [graphPath]);

        // Spawn emitter at camera focus
        const emitterData = {
            graph: graphPath,
            position: [0, 0, 0],
            rotation: [0, 0, 0, 1],
            max_particles: this.vfxGraph.max_particles
        };

        this.emitter = await this.ipc.call("vfx.spawn_emitter", [emitterData]);
    }

    /// Called when graph is saved.
    async onGraphChanged(newGraphPath: string) {
        if (this.emitter) {
            // Hot-reload: destroy old, spawn new
            await this.ipc.call("vfx.destroy_emitter", [this.emitter.id]);
        }

        await this.loadGraph(newGraphPath);

        // Reset playhead
        if (this.isPlaying) {
            this.emitter!.Play();
        }
    }

    /// Update every frame.
    update(delta_time: number) {
        if (this.isPlaying && this.emitter) {
            // Stats are fetched from runtime
            const stats = this.emitter.GetStats();
            this.statsOverlay.update({
                particle_count: stats.alive_particles,
                gpu_time_ms: stats.gpu_time,
                memory_mb: stats.memory / (1024 * 1024)
            });
        }
    }

    play() { this.isPlaying = true; this.emitter?.Play(); }
    pause() { this.isPlaying = false; this.emitter?.Pause(); }
    stop() { this.isPlaying = false; this.emitter?.Stop(); }
}
```

## Performance Stats Overlay

Real-time monitoring while editing:

```typescript
/// Overlay showing particle count, GPU cost, memory usage.
export class VFXStatsOverlay {
    private particleCount: number = 0;
    private gpuTimeMs: number = 0;
    private memoryMb: number = 0;
    private overlay: HTMLElement;

    constructor(container: HTMLElement) {
        this.overlay = document.createElement('div');
        this.overlay.style.cssText = `
            position: absolute;
            top: 10px;
            right: 10px;
            background: rgba(0, 0, 0, 0.7);
            color: #0f0;
            padding: 10px;
            font-family: monospace;
            font-size: 12px;
            border: 1px solid #0f0;
        `;
        container.appendChild(this.overlay);
    }

    update(stats: {particle_count: number, gpu_time_ms: number, memory_mb: number}) {
        this.overlay.innerHTML = `
            <div>Particles: ${stats.particle_count}</div>
            <div>GPU Time: ${stats.gpu_time_ms.toFixed(2)}ms</div>
            <div>Memory: ${stats.memory_mb.toFixed(1)}MB</div>
        `;
    }
}
```

## Module Marketplace

Save and load reusable presets:

```typescript
/// Module marketplace for sharing presets.
export class ModuleMarketplace {
    private presets: Map<string, VFXModulePreset> = new Map();

    /// Save current subgraph as reusable preset.
    async savePreset(name: string, modules: VFXModule[], description: string) {
        const preset: VFXModulePreset = {
            name,
            description,
            modules,
            thumbnail: await this.capturePreview(),
            version: "1.0"
        };

        // Upload to marketplace (local or cloud)
        await this.ipc.call("vfx.marketplace_save_preset", [preset]);
        this.presets.set(name, preset);
    }

    /// Browse presets and import into current graph.
    async importPreset(presetName: string, targetNodeId: string) {
        const preset = this.presets.get(presetName);
        if (!preset) {
            throw new Error(`Preset '${presetName}' not found`);
        }

        // Add modules to graph and connect to target node
        await this.ipc.call("vfx.graph_insert_preset", [targetNodeId, preset]);
    }

    async listCommunityPresets(): Promise<VFXModulePreset[]> {
        return await this.ipc.call("vfx.marketplace_list_presets", []);
    }
}
```

## Comparison

| Feature | UE5 (Niagara) | Godot (GPUParticles3D) | Unity (VFX Graph) | **UzEngine** |
|---|---|---|---|---|
| **Node graph authoring** | Visual emitter stack | Property-based (no graph) | Nodes in graph | **Specialized node graph (§40.09)** |
| **Spawn shapes** | Box, Sphere, Torus, Cylinder, Cone | Limited (sphere/box) | Sphere, Cone, Box, Line, Circle, Mesh | **Point, Box, Sphere, Cone, Circle, Line** |
| **Physics modules** | Gravity, Drag, Collision, Curl Noise | Basic gravity/damping | Turbulence, Gravity, Drag | **Gravity, Drag, Collision, Noise, Attraction** |
| **Parameter curves** | Per-attribute curves | Curves via GDScript | Per-input Bezier curves | **Curve editor (Hermite interpolation)** |
| **Live preview** | Real-time in viewport | Real-time | Real-time | **Real-time + hot-reload on save** |
| **File format** | Binary .uasset + Niagara bytecode | .tres text asset | Binary VFXG + C# code gen | **.uzvfx (JSON), open format** |
| **Marketplace** | Marketplace (Epic) | Asset Store (limited) | Asset Store | **Local marketplace + AI-friendly presets** |

## Comparison with Reference Engines

**UE5 Niagara**: Mature stack-based system with inheritance, 2D/3D emitters, GPU-driven simulation. UzEngine targets simpler workflows with hot-reload for rapid iteration (AI-assisted).

**Godot GPUParticles3D**: Property-only (no visual graph), limited to basic physics, simpler than Niagara but less expressive.

**Unity VFX Graph**: Modern node-based system, fast iteration, similar goals to UzEngine. UzEngine JSON format allows AI manipulation directly.

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | Basic node graph (spawn, physics, render), live preview, .uzvfx JSON serialization, hot-reload on save, performance stats overlay |
| **Phase 4** | Curve editors, parameter curves (color/scale over lifetime), collision detection, noise modules, marketplace (local presets), advanced render modules (ribbon, light) |
| **Phase 4+** | GPU-driven simulation (compute shaders), cloud marketplace, procedural preset generation, VFX debugging tools (particle trails, force visualization) |

