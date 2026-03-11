# UzEngine 0.2 — Roadmap

> **Repository**: https://github.com/uzmigames/uzengine

> Complete development roadmap from zero to AAA-ready engine.
> Each milestone has clear deliverables and acceptance criteria.
> Reference: [ARCHITECTURE.md](ARCHITECTURE.md) for design details.

---

## Overview

```
Phase 1: Runtime Foundation ──────────────────────────────────────►
  1.0 Bootstrap ► 1.1 Core ► 1.2 RHI ► 1.3 Renderer ► 1.4 Scene ► 1.5 Gameplay

Phase 1.5: Build Pipeline ────────────────────────────────────────►
  1.5.0 Shader Compiler ► 1.5.1 Asset Processor ► 1.5.2 Packager

Phase 2: Editor Extension ───────────────────────────────────────►
  2.0 N-API ► 2.1 Viewport ► 2.2 Core Editors ► 2.3 Advanced Editors

Phase 3: MCP Integration ───────────────────────────────────────►
  3.0 MCP Server ► 3.1 AI Tools ► 3.2 AI Content Pipeline

Phase 4: Polish & Shipping ──────────────────────────────────────►
  4.0 Platform SDKs ► 4.1 Networking ► 4.2 Optimization ► 4.3 Release
```

---

## Phase 1: Runtime Foundation

### 1.0 — Bootstrap & Project Setup ✅ COMPLETED

**Goal**: Compilable project with build system, logging, and a window.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Build system setup | Zig build (primary) + CMake fallback. MSVC/Clang support. | `zig build` and `cmake --build` both produce executable |
| Setup script | `scripts/setup.ps1` / `setup.sh` — install vcpkg, fetch deps | Fresh clone → run script → builds successfully |
| Directory structure | Create full runtime directory tree per ARCHITECTURE.md | All directories exist with placeholder README |
| Logging system | `UZ_LOG(category, level, fmt, ...)` with console + file backends | Logs appear in console with category/level/timestamp |
| Assertion macros | `UZ_CHECK()`, `UZ_ENSURE()`, `UZ_VERIFY()`, `UZ_CHECKF()` | Fatal asserts crash with stack trace, ensure logs + continues |
| CPU profiler | `UZ_PROFILE_SCOPE(name)` RAII timer, `UZ_PROFILE_FUNCTION()` | Profile data queryable, outputs to console/file |
| Memory foundation | `Own<T>`, `Ref<T>`, `RefMut<T>` ownership types. Debug borrow checker. | Double-borrow asserts in debug, zero-cost in release |
| Platform window | Win32 window creation + message loop. `PlatformWindow` abstraction. | Window opens, receives input, resizable, closeable |
| Input system | Keyboard/mouse/gamepad capture. Input action mapping. | Key press/release/held detectable, gamepad works |
| Config system | `.uzproject` JSON loader, cascading config resolution | CLI args override project settings override engine defaults |
| JSON schemas | Initial schemas: `uzproject.schema.json` | Schema validation catches invalid config |
| Test harness | doctest integration, first smoke tests | `zig build test` runs and passes |

**Dependencies**: None (leaf milestone)

---

### 1.1 — Math & Containers ✅ COMPLETED

**Goal**: Complete math library and core data structures.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Math wrapper | `Vec2`, `Vec3`, `Vec4`, `Mat4`, `Quat`, `Transform` over DirectXMath | All types construct, operate, convert correctly |
| SIMD validation | Verify SSE4/AVX2 codegen in release builds | Disassembly shows SIMD instructions for math ops |
| Color types | `LinearColor`, `sRGB`, conversion utilities | sRGB ↔ linear round-trips correctly |
| AABB / Frustum | Bounding box, frustum planes, intersection tests | Frustum culling test passes with known geometry |
| Containers | Ring buffer, free list, handle map (for RID-style handles) | Unit tests pass, no leaks in debug |
| Memory allocators | Linear allocator, pool allocator, frame allocator | Allocation benchmarks, leak detection in debug |
| Random | PCG random number generator | Statistical distribution tests pass |

**Dependencies**: 1.0

---

### 1.2 — RHI (Rendering Hardware Interface) ✅ COMPLETED

**Goal**: Abstract GPU interface with DX12 SM6.6 backend working.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| RHI interface | `IRHIDevice`, `IRHICommandList`, `IRHIBuffer`, `IRHITexture`, `IRHIPipeline`, `IRHISwapChain`, `IRHIFence` | Interface compiles, all methods pure virtual |
| DX12 backend | `DX12Device`, `DX12CommandList`, etc. implementing RHI | Device creates successfully on DX12-capable GPU |
| D3D12MemoryAllocator | Integrate D3D12MA for GPU memory management | GPU allocations tracked, no leaks on shutdown |
| Swap chain | Double/triple buffered present | Window shows solid color, no tearing |
| Command recording | Record + execute command lists | Draw calls execute without validation errors |
| Resource creation | Buffers (vertex, index, constant), textures (2D, cube) | Resources create, upload data, bind to pipeline |
| Pipeline state | PSO creation from shader + state description | PSO compiles and binds |
| Shader loading | Load DXIL shaders, reflection via SPIRV-Reflect | Shader loads, root signature auto-generated from reflection |
| Descriptor management | Bindless descriptor heap (SM6.6 ResourceDescriptorHeap) | Descriptors allocate/free, bindless access works |
| GPU profiler | `UZ_GPU_TIMESTAMP(name)` triple-buffered queries | GPU timings readable, no stalls |
| GPU breadcrumbs | Crash context markers for GPU hangs | Breadcrumb data recoverable after TDR |
| Debug layer | D3D12 debug + GPU-based validation in debug builds | Validation errors caught and logged |
| Clear color test | Render loop clearing to animated color | Smooth color animation in window |

**Dependencies**: 1.0, 1.1

---

### 1.3 — Renderer

**Goal**: PBR deferred renderer with basic lighting.

#### 1.3.0 — Triangle to Screen
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Vertex/Index buffers | Upload mesh data to GPU | Triangle renders on screen |
| Constant buffers | Camera matrices, per-object transforms | Object transforms correctly in 3D |
| Texture sampling | Load PNG/JPG → upload → sample in shader | Textured quad renders correctly |
| Camera | Perspective/ortho projection, view matrix, controls | WASD + mouse look camera |
| Mesh loading | Load `.uzmesh` (JSON header + binary vertex data) | Stanford bunny or similar renders |

#### 1.3.1 — Deferred Pipeline
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| G-Buffer | Albedo, Normal, Metallic/Roughness, Depth, Emissive | G-Buffer textures visualizable individually |
| Geometry pass | Render scene to G-Buffer | Multiple objects write correct G-Buffer data |
| Lighting pass | Fullscreen quad, reconstruct position from depth | Point/directional lights illuminate scene correctly |
| PBR shading | Cook-Torrance BRDF (GGX NDF, Smith G, Fresnel) | Material metallic/roughness slider produces correct visuals |
| Shadow maps | Directional light CSM (4 cascades) | Shadows render, cascade transitions smooth |
| Forward pass | Transparent objects after deferred resolve | Alpha-blended objects render correctly |

#### 1.3.2 — Post-Processing
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Post-process stack | Ordered effect pipeline, intermediate render targets | Effects chain executes in correct order |
| Tonemapping | ACES / Filmic tonemap | HDR scene maps to LDR correctly |
| Bloom | Downsample + blur + upsample + composite | Bright areas glow, no artifacts |
| SSAO | Screen-space ambient occlusion (GTAO or similar) | Occlusion visible in corners/crevices |
| SSR | Screen-space reflections | Reflections visible on glossy surfaces |
| TAA | Temporal anti-aliasing with jitter + history | Stable image, reduced aliasing vs no AA |
| Motion blur | Per-object velocity buffer + blur | Moving objects show motion blur |
| DOF | Depth of field (bokeh) | Near/far blur based on focus distance |
| Color grading | LUT-based color correction | LUT applies visually |
| FXAA/SMAA fallback | For non-TAA path | Anti-aliasing without temporal history |

#### 1.3.3 — Advanced Rendering
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| DLSS integration | NVIDIA DLSS SDK, motion vectors + depth input | Upscaled image quality ≈ native at higher FPS |
| FSR integration | AMD FSR, works on any GPU | FSR quality modes produce good results |
| XeSS integration | Intel XeSS SDK | XeSS works, auto-fallback chain (DLSS→XeSS→FSR) |
| Virtual shadow maps | Clipmap-based shadow pages | Stable shadows at distance, no cascade artifacts |
| Volumetric fog | Ray-marched volumetric lighting | Fog visible around lights, god rays |
| Sky/Atmosphere | Physically-based sky model (Hillaire) | Sky renders with correct scattering |
| Reflection probes | Baked cubemap probes with parallax correction | Reflections on non-planar surfaces |
| Screenshot mode | `--screenshot path.png` CLI command | PNG saved, matches viewport |
| Headless mode | `--headless` render without window | Render executes, screenshot works without display |

**Dependencies**: 1.2

---

### 1.4 — Scene System

**Goal**: ECS, transform hierarchy, serialization — load and render scenes.

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| entt integration | Entity creation, component add/remove/query | 100K entities create/destroy in <1ms |
| TransformComponent | Position, rotation, scale. Local + world. | Entity transforms update correctly |
| Transform hierarchy | Parent-child, dirty flags, batch update | Child follows parent, only dirty recalculated |
| Mobility | Static (baked) vs Movable (per-frame) | Static entities skip transform update |
| MeshComponent | References mesh + material assets | Entity renders with assigned mesh/material |
| LightComponent | Point, directional, spot. Color, intensity, range. | Lights affect scene rendering |
| CameraComponent | FOV, near/far, active camera selection | Camera component drives render view |
| Scene serialization | `.uzscene` JSON load/save | Save scene → reload → identical state |
| Prefab system | `.uzprefab` JSON, instantiate hierarchy | Prefab spawns with correct components |
| Asset references | Path-based, content hash validation | Broken ref detected and logged |
| Schema validation | Validate `.uzscene`/`.uzprefab` against JSON Schema | Invalid files rejected with clear error |
| Hot reload (assets) | File watcher on content/ → detect changes → reload | Modify texture on disk → updates in viewport |

**Dependencies**: 1.3

---

### 1.5 — Gameplay Systems

**Goal**: Physics, animation, audio, navigation, scripting — playable scenes.

#### 1.5.0 — Physics
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Jolt integration | JoltPhysicsServer, job system bridge | Physics world steps, objects fall with gravity |
| Rigidbody component | Dynamic, static, kinematic | Box drops onto floor, bounces |
| Colliders | Box, sphere, capsule, mesh, convex hull | Collision detection works for all shapes |
| Raycasts | `physics::Raycast(origin, dir, maxDist)` | Raycast hits correct object |
| Triggers | Enter/exit/stay events | Trigger volume fires events |
| Joints | Hinge, ball, slider, fixed | Constrained objects behave correctly |
| Fixed timestep | 60Hz physics, substeps, interpolation | Smooth motion at any display framerate |
| VHACD integration | Auto convex decomposition for mesh colliders | Complex mesh generates valid convex hull set |

#### 1.5.1 — Animation
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Skeleton | Bone hierarchy, bind pose, inverse bind matrices | Skeleton loads from `.uzskeleton` |
| Skinning | GPU skinning (compute shader) | Skinned mesh deforms correctly |
| Animation clips | `.uzanimsequence` load, keyframe interpolation | Animation plays back smoothly |
| AnimationPlayer | Simple playback, speed, loop, blend | Cross-fade between two animations |
| AnimationTree | State machine + blend tree from JSON | Walk→Run blend by speed parameter |
| Blend spaces | 1D and 2D blend spaces | 2D locomotion blend (speed × direction) |
| Root motion | Extract root bone translation/rotation | Character moves via animation, not code |
| Additive layers | Additive animation on top of base | Lean animation adds on top of walk |
| Montages | One-shot overlays (attack, emote) | Attack montage plays on upper body only |
| Animation events | Notify at keyframe (footstep, sound, VFX) | Event fires at correct animation time |
| Retargeting | Skeleton mapping between different rigs | Animation from rig A plays on rig B |

#### 1.5.2 — Audio
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| miniaudio integration | Device init, playback, capture | Audio plays through speakers |
| AudioSource component | 3D position, attenuation, looping | Sound quieter with distance |
| Submix hierarchy | Main → SFX / Music / Voice / Reverb | Per-group volume control works |
| Audio thread | Separate thread, lock-free command queue | No audio glitches during heavy game load |
| Spatial audio | Distance attenuation, stereo panning, HRTF | Sound localizable in 3D space |
| Reverb zones | Per-zone reverb parameters | Sound reverberates in enclosed spaces |
| Streaming | Stream large audio files from disk | Long music track plays without loading stall |
| Codec support | OGG (Vorbis), WAV, MP3 (dr_libs), Opus | All formats load and play |

#### 1.5.3 — Navigation & AI
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Recast integration | NavMesh generation from scene geometry | NavMesh generates for test level |
| Detour pathfinding | A* path query on navmesh | Agent finds path around obstacles |
| NavAgent component | Auto-movement along path, obstacle avoidance | Agent navigates to target point |
| NavObstacle component | Dynamic obstacles carve navmesh | Agent reroutes around new obstacle |
| NavLink component | Off-mesh connections (jumps, ladders) | Agent uses link to traverse gap |
| RVO2 integration | Crowd avoidance for multiple agents | 100 agents navigate without overlap |
| Behavior trees | Task, decorator, service nodes. Blackboard. | Simple patrol→chase→attack BT works |
| EQS queries | "Find cover", "find flanking position" | Query returns valid spatial results |
| Perception | Sight (cone), hearing (radius) | AI detects player in sight cone |

#### 1.5.4 — Scripting
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| C++ game DLL | Separate DLL, component registration via `UZ_REGISTER_MODULE()` | Custom component compiles in game DLL, runs in engine |
| C++ hot reload | Serialize → unload DLL → rebuild → reload → deserialize | Modify C++ code → rebuild → state preserved |
| Lua VM | Embedded Lua 5.4, sandboxed | Lua script executes in engine |
| Lua component binding | Attach Lua script to entity, access components | Lua script moves entity via TransformComponent |
| Lua hot reload | File watcher → reload script module | Edit Lua file → behavior changes instantly |
| API bindings | Expose engine API to Lua (scene, input, physics, audio) | Lua can spawn entities, play sounds, raycast |

#### 1.5.5 — Terrain & World
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Heightmap terrain | Load heightmap, generate mesh, clipmap LOD | Terrain renders with LOD transitions |
| Terrain materials | Multi-layer painting, blend weights | 4+ material layers blend smoothly |
| Virtual texturing | Tile-based streaming, GPU feedback buffer | Large terrain textures stream without pop-in |
| Foliage instancing | GPU instanced grass/trees, per-instance culling | 1M+ grass instances at 60fps |
| World partition | Grid-based cell streaming, async load/unload | Moving through world loads/unloads cells seamlessly |
| Streaming volumes | Manual load/unload triggers | Enter volume → new area loads |

#### 1.5.6 — VFX & Particles
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| GPU particle system | Compute shader spawn/update/render | 1M particles at 60fps |
| Emitter modules | Spawn rate, initial velocity, lifetime, size/color over life | Fire/smoke VFX looks reasonable |
| Force modules | Gravity, wind, turbulence, noise, vortex | Particles react to forces |
| Collision | Particles collide with depth buffer | Sparks bounce off surfaces |
| Ribbons | Trail/ribbon renderer | Sword slash trail effect |
| Mesh particles | Render mesh instances as particles | Debris chunks fly from explosion |
| Events | Death event → spawn sub-emitter | Firework: rocket dies → sparks spawn |
| `.uzvfx` loader | Load Niagara-style JSON system definition | VFX system loads and plays from JSON |

#### 1.5.7 — UI Runtime
| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Embedded HTML/CSS renderer | Lightweight browser layer for game UI | HTML page renders as overlay |
| C++ data binding | `ui->Bind("health", &health)` reactive updates | UI updates when C++ value changes |
| Lua data binding | `ui:bind("score", score)` | UI updates when Lua value changes |
| Event system | UI button click → C++/Lua callback | Button press triggers game action |
| imgui debug | imgui overlay for engine debug UI | FPS counter, entity inspector, profiler |

**Dependencies**: 1.4

---

## Phase 1.5: Build Pipeline

### 1.5.0 — Shader Compiler ✅ COMPLETED

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| DXC integration | HLSL → DXIL (DX12), HLSL → SPIR-V (Vulkan) | Shaders compile to both targets |
| SPIRV-Cross | SPIR-V → MSL (Metal), SPIR-V → GLSL (fallback) | Cross-compiled shaders valid |
| Permutation system | Feature flag defines → variant compilation | N variants generated from 1 shader |
| Shader cache | Content hash → skip recompilation | Second build skips unchanged shaders |
| Reflection | Auto-extract root signature / descriptor layout | No manual binding specification needed |
| Runtime compilation | Compile on save for hot reload (dev only) | Edit shader → auto-recompile → viewport updates |

### 1.5.1 — Asset Processor

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Texture compression | BC7 (DX), ASTC (mobile), ETC2 (GLES) | Textures compress per target platform |
| Mesh optimization | meshoptimizer: vertex cache, overdraw, LOD generation | Mesh draw call count reduced |
| Audio encoding | OGG/Opus encoding from WAV source | Audio files compressed, play correctly |
| Asset dependency graph | Track which assets depend on which | Changing texture reprocesses dependent materials |
| Incremental processing | Only reprocess changed assets | Second build processes only modified assets |

### 1.5.2 — Packager

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| `.uzpak` archive | Pack processed assets into archive files | Assets load from .uzpak at runtime |
| Scene baking | Strip editor data, resolve refs, binary serialize | Baked scene loads faster than JSON |
| Executable generation | Link runtime (no editor module) + embed assets | Self-contained .exe runs game |
| Windows packaging | .exe + .uzpak output | Game runs on machine without engine |
| Config stripping | Remove dev-only config, bake shipping settings | Shipping build has no debug overhead |

**Dependencies**: 1.3 (shaders), 1.4 (assets)

---

## Phase 2: Editor Extension

### 2.0 — N-API Foundation

> **POC 0.2 validated**: N-API .node addon inside Electron/VSCode works. DX12 render + H.264 MF encode + WebCodecs decode = **120 FPS at 854x480 with 50K instances**. See [ARCHITECTURE.md — Lessons from POC 0.2](ARCHITECTURE.md#lessons-from-poc-02-n-api-stress-test).

**Project structure**: `napi/` (C++ → `.node` binary) is separate from `extension/` (TypeScript). Root `package.json` builds both. See [ARCHITECTURE.md — Project Structure](ARCHITECTURE.md#project-structure).

| Task | Details | Status |
|------|---------|--------|
| .node compilation | Runtime compiles as native N-API addon via cmake-js | **DONE** (POC 0.2) |
| Electron ABI matching | `--runtime=electron --runtime-version=<ver>`, delay-load hook, NAPI_DISABLE_CPP_EXCEPTIONS | **DONE** (POC 0.2) |
| H.264 frame streaming | MF HW encoder (C++) + WebCodecs VideoDecoder (webview) for preview panels | **DONE** (POC 0.2 — 120 FPS) |
| MediaStreamTrackGenerator | VideoFrame → TrackGenerator → `<video>` (native compositor path) | **Testing** |
| WebRTC local peer | RTCPeerConnection for zero-postMessage frame delivery | **Planned** |
| napi/ + extension/ split | Separate napi/ (C++ build) from extension/ (TS), root build script | **Planned** |
| TypeScript bindings | Auto-generated TS types from C++ API | Full type safety in extension code |
| Extension scaffold | VSCode extension package, activation, commands | Extension activates, registers commands |
| Lifecycle management | Start/stop runtime from extension | Runtime initializes/shuts down cleanly |
| Shared GPU surface | Runtime creates own HWND for viewport — no readback/encode | Viewport shows render output with zero transfer overhead |
| Runtime pool | Pre-warmed runtime instances for instant scene open (POC 0.1 lesson) | Open scene tab → renders in <500ms |
| Build-safe teardown | `--pool-shutdown` CLI + build script hook — kill pool before recompile | Recompile never fails due to locked binary |

### 2.1 — Viewport & Core UI

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Viewport panel | Native viewport in VSCode, mouse/keyboard forwarded | 3D scene renders in VSCode, camera controls work |
| Entity hierarchy | Webview tree view of scene entities | Select entity in tree → highlights in viewport |
| Inspector panel | Webview property editor for selected entity | Modify transform → entity moves in viewport |
| Asset browser | Webview file browser for content/ directory | Browse, preview, drag-drop assets |
| Console panel | Webview log viewer with category filtering | Engine logs stream to console panel |
| Toolbar | Play/pause/stop, transform mode (translate/rotate/scale) | Buttons control engine state |

### 2.2 — Core Editors

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Shader graph editor | Node-based material editor (webview) | Create PBR material via nodes → renders in viewport |
| Animation controller | State machine editor for animation blending | Walk→Run→Jump state machine works |
| Behavior tree editor | Node-based AI behavior tree editor | Patrol→Chase BT editable and runs |
| Localization editor | String table manager, culture overview | Add/edit translations, switch culture in editor |
| UI editor | WYSIWYG HTML/CSS game UI editor | Design health bar → renders in game |

### 2.3 — Advanced Editors

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Visual script editor | Blueprint-style node graph → C++/Lua codegen | Visual script opens door on trigger |
| Control rig editor | Procedural animation constraint editor | IK setup via editor, runs at runtime |
| Animation sequence editor | Timeline, keyframes, curves, events | Edit animation clip, preview in viewport |
| VFX graph editor | Niagara-style emitter system editor | Create fire VFX via editor nodes |
| Terrain editor | Sculpt, paint, foliage placement | Modify terrain in viewport |
| Scene editor (full) | Multi-select, copy/paste, undo/redo, snap, grid | Full level editing workflow |

**Dependencies**: Phase 1 complete, 1.5 partial

---

## Phase 3: MCP Integration

### 3.0 — MCP Server

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| MCP server scaffold | Node.js server, stdio/SSE transport | Claude connects via MCP protocol |
| Scene tools | create_entity, modify_component, delete_entity | LLM creates entities via MCP |
| Asset tools | import_asset, modify_material, create_vfx | LLM generates assets via MCP |
| Query tools | get_scene_tree, get_entity_components, get_logs | LLM reads engine state |
| Screenshot tool | capture_screenshot → return image | LLM sees rendered scene |

### 3.1 — AI Tool Integration

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| AI provider abstraction | `ai::Generate(category, prompt)` routing | Provider swap via config, no code change |
| Texture generation | Nano Banana / FLUX integration via MCP tool | LLM generates texture → auto-imported |
| 3D model generation | Meshy / Hunyuan / Tripo integration | LLM generates mesh → imported + split |
| Voice generation | ElevenLabs integration | LLM generates NPC dialogue audio |
| SFX generation | ElevenLabs SFX integration | LLM generates sound effects |
| Music generation | AIVA / Stable Audio integration | LLM generates background music |
| Animation capture | DeepMotion integration | Video → animation clip pipeline |

### 3.2 — AI Content Pipeline

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| End-to-end VFX | Prompt → texture + mesh + material + VFX | "Fire explosion" → complete VFX asset |
| End-to-end character | Prompt → model + skeleton + animations + sounds | "Knight character" → playable character |
| End-to-end level | Prompt → terrain + props + lighting + audio | "Forest clearing" → walkable level |
| Visual iteration | Screenshot → LLM feedback → auto-adjust | LLM iterates on VFX until satisfied |
| Schema validation feedback | Invalid JSON → error → LLM auto-corrects | Self-healing asset generation |

**Dependencies**: Phase 2 complete

---

## Phase 4: Polish & Shipping

### 4.0 — Platform SDKs

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Platform abstraction | `IPlatformServices` unified interface | Game code uses one API for all platforms |
| Steam integration | Steamworks: login, achievements, leaderboards, workshop | Steam overlay works, achievements unlock |
| Epic integration | EOS: login, achievements, matchmaking | EOS login flow works |
| Google Play | GPGS: login, achievements, billing, ads | Android game publishes to Play Store |
| Console SDKs | Sony/Microsoft (NDA, private modules) | Separate repos, builds for PS5/Xbox |

### 4.1 — Networking

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Replication system | Property sync, delta compression, relevancy | Client sees server-authoritative state |
| RPCs | Client→Server, Server→Client, Multicast | RPC executes on correct machine |
| Dedicated server | Headless server build, `--server` flag | Server runs without GPU |
| Listen server | Client hosts game | Host plays + serves simultaneously |
| Prediction/reconciliation | Client-side prediction, server correction | Smooth movement despite latency |
| Replay system | Record/playback network state | Game replays accurately |

### 4.2 — Optimization

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| GPU-driven rendering | Indirect draw, multi-draw-indirect | Draw call count independent of object count |
| Nanite-style meshes | Virtual geometry, cluster-based LOD | Dense scenes render efficiently |
| Lumen GI | Software RT (screen-space + surface cache + radiance cache) | Indirect lighting without baked lightmaps |
| Hardware RT | DXR reflections, shadows, AO, GI | RT features on RTX/RDNA2+ GPUs |
| Path tracing | Full path tracer for reference/cinematics | Path traced image converges correctly |
| Memory optimization | Streaming, budget system, pool tuning | Stays within target memory budget |
| CPU optimization | Profile-driven, hot path optimization | 60fps target on mid-range hardware |

### 4.3 — Release

| Task | Details | Acceptance Criteria |
|------|---------|-------------------|
| Documentation | API docs, tutorials, examples | New user can create project and render scene |
| Example projects | FPS template, third-person template | Templates build and run |
| VS Marketplace | Publish extension to VS Code marketplace | Extension installable from marketplace |
| CI/CD | GitHub Actions: build matrix, tests, package, publish | Every PR builds + tests on MSVC/Clang/Zig |
| Screenshot regression | Automated render comparison vs golden images | Visual regressions caught before merge |
| Benchmark suite | Standard scenes, FPS/memory/draw call tracking | Performance tracked over time |
| License & legal | Engine license, thirdparty license compliance | All licenses documented and compatible |

**Dependencies**: All previous phases

---

## Milestone Priority Order

The recommended implementation sequence, balancing dependencies and early value:

```
1.0  Bootstrap & Project Setup          ████░░░░░░░░░░░░░░░░
1.1  Math & Containers                  ░████░░░░░░░░░░░░░░░
1.2  RHI (DX12 SM6.6)                   ░░████░░░░░░░░░░░░░░
1.3.0 Triangle to Screen                ░░░░██░░░░░░░░░░░░░░
1.3.1 Deferred Pipeline                 ░░░░░███░░░░░░░░░░░░
1.3.2 Post-Processing                   ░░░░░░░██░░░░░░░░░░░
1.5.0 Shader Compiler (parallel)        ░░░░░░███░░░░░░░░░░░
1.4  Scene System                       ░░░░░░░░███░░░░░░░░░
1.3.3 Advanced Rendering                ░░░░░░░░░███░░░░░░░░
1.5.0 Physics                           ░░░░░░░░░░███░░░░░░░
1.5.1 Animation                         ░░░░░░░░░░░███░░░░░░
1.5.2 Audio                             ░░░░░░░░░░░░██░░░░░░
1.5.3 Nav & AI                          ░░░░░░░░░░░░░██░░░░░
1.5.4 Scripting                         ░░░░░░░░░░░░░░██░░░░
1.5.5 Terrain & World                   ░░░░░░░░░░░░░░░██░░░
1.5.6 VFX & Particles                   ░░░░░░░░░░░░░░░██░░░
1.5.7 UI Runtime                        ░░░░░░░░░░░░░░░░█░░░
1.5.1 Asset Processor (parallel)        ░░░░░░░░░░░░░░██░░░░
1.5.2 Packager                          ░░░░░░░░░░░░░░░░██░░
2.0  N-API Foundation                   ░░░░░░░░░░░░░░░░░██░
2.1  Viewport & Core UI                 ░░░░░░░░░░░░░░░░░░██
2.2  Core Editors                       ░░░░░░░░░░░░░░░░░░░█
2.3  Advanced Editors                   ░░░░░░░░░░░░░░░░░░░█
3.x  MCP                                ░░░░░░░░░░░░░░░░░░░█
4.x  Polish & Shipping                  ░░░░░░░░░░░░░░░░░░░█
```

---

## Success Metrics

| Metric | Target |
|--------|--------|
| **Frame rate** | 60fps @ 1080p on mid-range GPU (RTX 3060 / RX 6700) |
| **Draw calls** | GPU-driven: <100 CPU draw calls regardless of object count |
| **Entity count** | 100K+ entities with physics at 60fps |
| **Particle count** | 1M+ GPU particles at 60fps |
| **Load time** | Scene cold load <3s, hot reload <500ms |
| **Memory** | <2GB GPU, <4GB CPU for typical scene |
| **Build time** | Incremental C++ build <5s (Zig), <15s (MSVC) |
| **Screenshot** | `--screenshot` completes in <2s |
| **AI generation** | End-to-end VFX from prompt <60s |

---

*This roadmap is a living document. Update as decisions are made and milestones are completed.*
*Reference: [ARCHITECTURE.md](ARCHITECTURE.md) for detailed design of each system.*
