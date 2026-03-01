# UzEngine — Vision & Architecture Spec

> **Status**: Planning Phase
> **Reference engine**: Godot 4.x (best LLM/AI integration among major engines)
> **Primary language**: C++23
> **Target platform**: Windows 10+ (DX12), Linux (Vulkan) future

---

## 1. Overview

UzEngine is a 3D game engine designed to be **AI-native from the ground up**: every file format, API, and editor panel is designed so that an LLM agent can create, modify, and iterate on game content with zero manual GUI interaction.

The editor runs **inside VSCode** (via extension), with no separate window. The runtime (game engine) is a separate C++ process that communicates with the editor via IPC + WebSocket.

### Design Philosophy

| Principle | Implementation |
|-----------|----------------|
| **AI-first** | All scene/resource files are plain text with a simple, predictable format |
| **No GC** | Ownership via `unique_ptr`, borrows via raw ptr, generation-based handles |
| **Separation of concerns** | Editor ≠ Runtime. Play mode = separate OS process |
| **Text-driven** | Shaders, scenes, materials, configs = files editable by LLMs |
| **Modern GPU** | DX12 primary renderer, Vulkan future target |

---

## 2. Macro-Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  VSCode Extension (TypeScript + Node.js)                            │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────────┐  │
│  │  Scene Tree  │  │  Inspector Panel │  │  Shader Editor       │  │
│  │  (TreeView)  │  │  (Webview Form)  │  │  (CodeLens+Preview)  │  │
│  └──────────────┘  └─────────────────┘  └──────────────────────┘  │
│  ┌──────────────┐  ┌─────────────────┐  ┌──────────────────────┐  │
│  │  Viewport    │  │  Asset Browser  │  │  Project Settings    │  │
│  │  (WebGL/WS)  │  │  (TreeView)     │  │  (Webview Form)      │  │
│  └──────────────┘  └─────────────────┘  └──────────────────────┘  │
│  ┌──────────────┐  ┌─────────────────┐                             │
│  │  Play ▶ Stop │  │  Console / Log  │                             │
│  │  (StatusBar) │  │  (OutputChannel)│                             │
│  └──────────────┘  └─────────────────┘                             │
└─────────────────────────────────────────────────────────────────────┘
              │ WebSocket + JSON IPC
              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  UzEngine.exe (C++ Runtime)                                         │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  Scene System      │  Renderer (DX12)   │  Asset System      │  │
│  │  Node Tree         │  Forward+ pipeline │  Resource Manager  │  │
│  │  Scene Loader      │  Shader Compiler   │  File Watcher      │  │
│  ├────────────────────┼────────────────────┼────────────────────┤  │
│  │  Physics (Jolt)    │  Audio (miniaudio) │  Script VM         │  │
│  │  Collision         │  Spatial audio     │  (Phase 3)         │  │
│  ├────────────────────┼────────────────────┼────────────────────┤  │
│  │  Memory System     │  IPC / MCP Server  │  ECS (entt)        │  │
│  │  Arena Allocators  │  JSON handlers     │  Components        │  │
│  └──────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘
              │ File I/O
              ▼
┌─────────────────────────────────────────────────────────────────────┐
│  Project Files (Plain Text — AI-readable)                           │
│  ├── project.uzproj           (project config, TOML)                │
│  ├── scenes/
│  │   ├── main.uzscene         (main scene)
│  │   └── player.uzscene       (prefab scene)
│  ├── materials/
│  │   └── wood.uzmat           (material)
│  ├── shaders/
│  │   ├── pbr.hlsl             (HLSL shader)
│  │   └── cel.glsl             (GLSL shader)
│  └── assets/
│      ├── meshes/              (GLTF/OBJ source files)
│      └── textures/            (PNG/KTX2 source files)
└─────────────────────────────────────────────────────────────────────┘
```

---

## 3. Core Subsystems

### 3.1 Memory System (No GC, No Garbage)

Inspired by Casey Muratori / Ryan Fleury — no individually freed `new`/`delete`:

```cpp
// Arena allocator — lifetime determined by scope
Arena frame_arena;   // reset() every frame
Arena level_arena;   // reset() on level transition
Arena scratch;       // thread-local temporary stack

// Explicit ownership via unique_ptr for persistent objects
std::unique_ptr<Mesh> mesh = arena_make_unique<Mesh>(level_arena, vertices);

// Handle system for cross-lifetime references (no dangling pointers)
struct NodeHandle { uint32_t index; uint32_t generation; };
Node* Scene::get(NodeHandle h);  // returns null on stale handle, never UB
```

**Ownership Rules**:
- `unique_ptr<T>` = single owner, automatic destruction
- `T*` / `T&` = borrow (lifetime documented, must not outlive owner)
- `NodeHandle` / `ResourceHandle` = cross-lifetime reference (generation-checked)
- Never `shared_ptr` in hot paths (atomic refcount overhead)

### 3.2 Scene System (Node Tree)

Hierarchical node composition model, inspired by Godot:

```
Scene "Level1"
├── Node3D "World"
│   ├── StaticMesh "Ground"     [mesh="terrain.glb", material="grass.uzmat"]
│   ├── DirectionalLight "Sun"  [intensity=1.5, color=#FFF5E0]
│   └── Node3D "Buildings"
│       ├── StaticMesh "House1" [transform=...]
│       └── StaticMesh "House2" [transform=...]
├── Node3D "Player"
│   ├── CapsuleCollider         [radius=0.4, height=1.8]
│   ├── StaticMesh "Body"       [mesh="player.glb"]
│   └── Camera "MainCamera"     [fov=75, near=0.1, far=1000]
└── Node "GameLogic"            [script="game_logic.cpp"]
```

**`.uzscene` file format** (plain text, AI-friendly):
```ini
[uzscene version=1 name="Level1"]

[node id=1 type=Node3D name="World" parent=0]
transform = [1,0,0, 0,1,0, 0,0,1, 0,0,0]

[node id=2 type=StaticMesh name="Ground" parent=1]
mesh     = "res://assets/terrain.glb"
material = "res://materials/grass.uzmat"
cast_shadow = true

[node id=3 type=DirectionalLight name="Sun" parent=1]
intensity      = 1.5
color          = [1.0, 0.96, 0.87]
shadow_enabled = true
```

### 3.3 Shader System (HLSL + GLSL)

Dual-language support, compiled via DXC / glslang:

```
shaders/
├── pbr.hlsl         → DXIL bytecode  (DX12)
├── pbr.glsl         → SPIR-V         (Vulkan, future)
├── cel_shading.hlsl
└── water.hlsl
```

**Hot-reload via IPC**:
```json
{ "fn": "shader.reload", "args": { "path": "shaders/pbr.hlsl" } }
```

The VSCode shader editor shows live compilation errors and a preview frame after reload.

### 3.4 Material System

**`.uzmat` file format** (plain text):
```ini
[uzmaterial version=1 name="WoodFloor"]
shader = "res://shaders/pbr.hlsl"

albedo_texture = "res://textures/wood_albedo.png"
normal_texture = "res://textures/wood_normal.png"
roughness      = 0.7
metallic       = 0.0
tiling         = [2.0, 2.0]
```

### 3.5 ECS (entt)

Internal runtime systems use `entt` for cache-friendly iteration:

```cpp
// Components as plain POD structs
struct Transform    { glm::mat4 local; glm::mat4 world; };
struct MeshRenderer { ResourceHandle<Mesh> mesh; ResourceHandle<Material> mat; };
struct DirLight     { glm::vec3 color; float intensity; };

// System: propagate transform hierarchy
void system_update_transforms(entt::registry& reg) {
    auto view = reg.view<Transform, const NodeParent>();
    for (auto [entity, xform, parent] : view.each()) { /* ... */ }
}
```

### 3.6 Renderer Pipeline (DX12)

```
Scene Graph
    ↓ Frustum culling + occlusion
Draw Call Batching (GPU instancing)
    ↓
Shadow Pass   (Cascaded Shadow Maps, 3 cascades)
    ↓
Depth Prepass (early-Z rejection)
    ↓
G-Buffer Pass (albedo, normal, roughness, metallic)
    ↓
Lighting Pass (PBR — GGX BRDF, IBL)
    ↓
Transparency  (sorted, alpha blending)
    ↓
Post-Process  (tonemapping, bloom, SSAO)
    ↓
JPEG encode → WebSocket → VSCode Viewport
```

### 3.7 Play Mode (Separate Process)

```
Editor Mode                        Play Mode
────────────                       ──────────
VSCode Extension                   UzEngine.exe --play --project=./
    ↕ IPC (editor commands)            ↑
UzEngine.exe --editor                  spawned by the extension
    (no game loop, editor only)        runs the full game loop
    (scene tree, inspector, etc.)      streams frames to the viewport
```

Flow:
1. User clicks ▶ Play in VSCode status bar
2. Extension saves the current scene (if dirty)
3. Extension spawns `UzEngine.exe --play --scene=main.uzscene`
4. Viewport switches to streaming from the play process
5. User clicks ■ Stop → extension kills the play process

### 3.8 VSCode Editor Panels

| Panel | VSCode API | Purpose |
|-------|-----------|---------|
| **Viewport** | Webview (Canvas + WebSocket) | Live render from engine |
| **Scene Tree** | `TreeDataProvider` (native) | Node hierarchy |
| **Inspector** | Webview (Webview UI Toolkit) | Selected node properties |
| **Asset Browser** | `TreeDataProvider` (native) | Project files |
| **Shader Editor** | Language extension (HLSL/GLSL) | Syntax, errors, preview |
| **Console** | `OutputChannel` (native) | Engine logs |
| **Project Settings** | Webview | Global project config |
| **Play Controls** | Status bar items | ▶ Play / ■ Stop / ⏸ Pause |

### 3.9 Asset Pipeline

- **GLTF 2.0** as mesh/scene import format (industry standard)
- **PNG / KTX2** for textures (KTX2 with BC7 GPU compression)
- **WAV / OGG** for audio
- Each imported asset generates a `.import.json` sidecar:

```json
{
  "source":   "assets/player.glb",
  "type":     "Mesh",
  "settings": { "lod_levels": 3, "generate_normals": true },
  "output":   "assets/.import/player.glb.mesh"
}
```

---

## 4. File Formats — AI-Friendliness

All formats are designed to be:
1. **Human-readable** — no binary blobs
2. **Diff-friendly** — isolated changes per line
3. **Simple schema** — LLM can generate without reference docs
4. **No opaque GUIDs** — relative paths, readable IDs

| Extension | Content | Format |
|-----------|---------|--------|
| `.uzscene` | Scene / prefab | INI-like |
| `.uzmat` | Material | INI-like |
| `.uzproj` | Project config | TOML |
| `.hlsl` | HLSL shader | Plain text (industry standard) |
| `.glsl` | GLSL shader | Plain text (industry standard) |
| `.import.json` | Import metadata | JSON |

---

## 5. MCP / IPC — AI Agent Interface

Every engine operation is exposed as a callable IPC function via MCP:

```
scene.*
  scene.load(path)                    load a scene file
  scene.save(path)                    save current scene
  scene.getTree()                     return node tree as JSON
  scene.selectNode(id)                select node in editor
  scene.addNode(type, parent_id)      add a new node
  scene.removeNode(id)                remove node and children
  scene.setProperty(id, key, value)   modify a node property

shader.*
  shader.reload(path)                 recompile shader, hot-reload
  shader.getErrors(path)              return compile errors
  shader.list()                       list all project shaders

asset.*
  asset.import(path)                  import an asset file
  asset.list(type)                    list assets by type

engine.*
  engine.ping()                       health check
  engine.screenshot()                 capture current frame
  engine.play()                       start play mode
  engine.stop()                       stop play mode
  engine.status()                     fps, scene, mode, memory
```

---

## 6. Phase Roadmap

### Phase 0 — Current State ✅
- DX12 renderer (triangle / pyramid)
- JPEG streaming via WebSocket
- VSCode extension viewport
- Basic IPC/MCP (5 primitives: ping, status, screenshot, setRotation, setBackground)

### Phase 1 — Editor Core + Debug Foundation
- **Debug system**: ASSERT macros, Tracy CPU/GPU profiler, ImGui overlay, doctest unit tests, MSVC ASAN
- Scene system + `.uzscene` / `.uzmat` formats
- Scene Tree panel (VSCode `TreeDataProvider`)
- Inspector panel (Webview UI Toolkit)
- Play / Stop mode (separate OS process)
- Asset browser with import pipeline
- Shader editor: DXC integration, file watcher, live error display

### Phase 2 — Full Renderer + Asset Pipeline
- PBR materials (GGX BRDF, IBL, environment maps)
- GLTF 2.0 mesh/scene import (fastgltf + meshoptimizer LODs)
- FBX import (ufbx — MIT, 2-file C library)
- 3-cascade shadow maps (CSM, PCF 3×3)
- Texture tools: DirectXTex compression (BC7/BC5/BC6H), preview panel
- Asset packaging: `.uzpak` (zstd, memory-mapped, VFS)
- Post-process: tonemapping (ACES), bloom, SSAO

### Phase 3 — Engine Features
- Skeletal mesh: GPU skinning, morph targets, `.uzskel` / `.uzmesh` / `.uzphys`
- Animation: state machine, blending, AnimNotify, root motion, IK (two-bone + FABRIK)
- Ragdoll: Jolt Physics `RagdollSettings`, active ragdoll via motor torques
- Input: Enhanced Input style — `InputAction` + `InputMappingContext` + XInput
- Camera: SpringArm, modes (third-person/orbit/cinematic), shake, DoF, `.uzcam`
- Audio: miniaudio, spatial audio, triggered by AnimNotify

### Phase 4 — Build Pipeline & Shipping
- Cook stage: stb_image → BC7 DDS, ufbx/fastgltf → `.uzmesh`, DXC → `.dxil`
- Pack stage: `.uzpak` assembler with zstd per-file compression
- Shipping binary: `UZ_SHIPPING=1` strips editor/IPC/DXC runtime
- Stage + manifest: SHA256 file list for update diffing
- Windows packaging: CPack NSIS installer, application icon, version stamping
- Platform abstraction layer: `src/platform/` (Win32 now, Linux/macOS stubs)

---

## 7. Technology Stack

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| Renderer | DirectX 12 | Modern GPU, Windows native |
| Math | GLM | Industry standard, header-only |
| ECS | entt | Zero overhead, header-only, well-maintained |
| Physics | Jolt Physics | Open-source, high-perf, no GC, MIT license |
| Audio | miniaudio | Single-header, zero dependencies |
| GLTF import | fastgltf | Header-only, fast, spec-compliant |
| FBX import | ufbx | MIT, 2-file C library, adopted by Blender 2025 |
| LOD | meshoptimizer v1.0 | `meshopt_simplify` + `clusterlod.h` (Nanite-style DAG) |
| Texture compression | DirectXTex + bc7enc_rdo | BC7/BC5/BC6H + RDO for PAK compression |
| Asset packaging | Custom `.uzpak` | zstd per-file, memory-mapped, xxhash3 directory |
| Physics | Jolt Physics | High-perf, MIT, ragdoll support |
| Audio | miniaudio | Single-header, zero dependencies |
| Animation | Custom (SLERP/LERP + state machine) | Binary `.uzanim`, INI `.uzanim_sm` |
| Input | Custom Enhanced Input style | `InputAction` + `InputMappingContext` + XInput |
| JSON | nlohmann/json | Already in project |
| WebSocket | IXWebSocket | Already in project |
| JPEG stream | libjpeg-turbo | Already in project |
| HLSL compiler | DXC (DirectX Shader Compiler) | HLSL → DXIL |
| GLSL compiler | glslang | GLSL → SPIR-V |
| Editor UI | VSCode Webview UI Toolkit | Native VSCode look and feel |
| Packaging | CPack + NSIS | Windows installer |
| Build | CMake + vcpkg | Already configured |
| Language | C++23 | Modules, ranges, concepts |

---

## 8. Asset Pipeline

### Import (Cook)

| Source | Output | Tool |
|--------|--------|------|
| `.fbx` | `.uzmesh` + `.uzskel` + `.uzanim` | ufbx |
| `.glb` / `.gltf` | `.uzmesh` + materials | fastgltf |
| `.png` / `.hdr` | `.dds` (BC7/BC5/BC6H) | DirectXTex |
| `.wav` | `.ogg` | libvorbis |
| `.hlsl` | `.dxil` | DXC |

LOD chain: `meshopt_simplify` → LOD0 (100%), LOD1 (50%), LOD2 (25%).

### Pack

After cook, `PakWriter` assembles all outputs into `build/<platform>/game.uzpak`:
- Footer-at-end layout (memory-mappable, no seek)
- Per-file zstd compression
- xxhash3 directory sorted by path hash (O(log n) lookup)

### Shipping Binary

```cmake
target_compile_definitions(UzEngine-Runtime PRIVATE UZ_SHIPPING=1)
```

Strips: IPC server, DXC runtime, editor panels, asset importers. Mounts `game.uzpak` from executable directory.

---

## 9. Skeletal Mesh & Animation

### Three-Asset Separation

| Asset | Extension | Content |
|-------|-----------|---------|
| Skeleton | `.uzskel` | Bone hierarchy, bind pose, per-bone transform |
| Skinned Mesh | `.uzmesh` | Geometry with up to 4 bone influences per vertex |
| Physics Asset | `.uzphys` | Per-bone colliders, joint constraints, ragdoll config |

### Animation Pipeline

```
Animation Clip (.uzanim)
    ↓ Sample (SLERP rotations, LERP position/scale)
Bone Pose (local space)
    ↓ State Machine (.uzanim_sm)
Blended Pose (full-body / layered via .uzmask / additive)
    ↓ Root Motion extraction
Final Pose → GPU bone palette (StructuredBuffer<float4x4>)
    ↓ Vertex Shader skin pass
Deformed Mesh
```

**AnimNotify**: events at specific keyframes route to: `play_sound` (miniaudio), `spawn_vfx`, `anim_event` (game logic).

**IK**: Two-bone analytical (limbs) + FABRIK iterative (spine, chains).

### Ragdoll

Jolt `RagdollSettings` built from `.uzphys`. Activation: blend from animation to ragdoll in 0.3s. Active ragdoll via motor torques applies physics while tracking animation pose.

---

## 10. Input System

Enhanced Input style — device-agnostic, runtime rebindable:

```ini
# .uzinput (plain text, AI-friendly)
[action name="Jump" type=button]
[action name="Move" type=axis2d]

[context name="Player" priority=0]
[mapping action="Jump"  key=Space      device=keyboard]
[mapping action="Jump"  key=GamepadA   device=gamepad]
[mapping action="Move"  key=WASD       device=keyboard modifier=normalize]
[mapping action="Move"  key=LeftStick  device=gamepad]
```

**Modifiers**: Normalize, Deadzone, Scale, Negate, Swizzle.
**Triggers**: Pressed, Released, Hold, Tap, Pulse.
Runtime rebinding writes to `saves/bindings.uzinput`.

---

## 11. Camera System

```
Scene
└── SpringArm "CameraRig"          (.uzcam preset)
    ├── arm_length = 4.0
    ├── lag_speed  = 8.0            (smoothed follow)
    ├── collision  = sphere_cast    (avoids clipping)
    └── Camera "MainCamera"
        ├── mode  = third_person
        ├── fov   = 75 (animated)
        ├── dof   = { focus=6, aperture=2.8 }
        └── shake = { frequency=12, amplitude=0.3, decay=5 }
```

**Modes**: `third_person`, `first_person`, `orbit`, `cinematic` (Catmull-Rom spline path).

---

## 12. Ownership Model (Borrow-Checker in C++)

```cpp
// Rule 1: unique_ptr for ownership
class Scene {
    std::unique_ptr<Node> root_;
public:
    void set_root(std::unique_ptr<Node> node) { root_ = std::move(node); }
    Node* root() const { return root_.get(); }  // borrow — caller must not store
};

// Rule 2: raw pointer = borrow (no ownership, no delete)
void render_node(const Node* node);   // borrows, does not store

// Rule 3: generation handles for cross-lifetime references
struct NodeHandle { uint32_t index; uint32_t gen; };
Node* Scene::resolve(NodeHandle h) {
    if (nodes_[h.index].gen != h.gen) return nullptr;  // stale = null, never UB
    return &nodes_[h.index];
}

// Rule 4: arena for bulk allocations
Arena frame_arena;
DrawCall* calls = frame_arena.alloc_array<DrawCall>(1024);
// ... fill calls ...
// At end of frame: frame_arena.reset() — O(1), no individual free

// Avoid:
// ❌  shared_ptr in hot paths  (atomic refcount = cache miss)
// ❌  new/delete scattered     (use arena or pool)
// ❌  mutable global state     (ownership must be clear)
```
