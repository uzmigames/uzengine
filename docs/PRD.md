# UzEngine — Product Requirements Document

> **Version**: 1.0
> **Status**: Living document (updated as scope evolves)
> **Last Updated**: 2026-03-01
> **Scope**: Phase 1–4 implementation requirements

---

## 1. Executive Summary

UzEngine is the first game engine designed from the ground up for **AI-assisted development**. Where Unity, Unreal, and Godot were built for human-first workflows, UzEngine treats AI agents as first-class collaborators. Every format is plain text. Every path is human-readable. Every system is live-editable without engine restart.

### Key Differentiators

| Aspect | Traditional Engines | UzEngine |
|--------|-------------------|----------|
| Asset references | GUIDs in `.meta` (opaque) | `res://` paths (readable) |
| AI scene generation | Impossible (binary `.uasset`/`.tscn.meta`) | Native (plain text `.uzscene`) |
| Hot-reload crashes | Common (missing script errors, access violations) | Graceful (missing scripts → warnings only) |
| Editor stability | Engine crash = editor crash | VSCode never crashes (separate process) |
| Script iteration speed | 2–10s C++ recompile | 60ms Lua hot-reload |

---

## 2. Problem Statement

### 2.1 The AI Agent Paradox

The rise of AI coding assistants (Claude, Copilot, Cursor) has fundamentally changed game development workflows. Yet existing game engines were designed in an era before LLMs. This creates friction:

**Problem 1: Binary Formats Block AI Generation**
- Unity uses GUID-based `.meta` files + binary `.prefab` assets. An AI agent cannot write or modify these.
- Unreal stores scenes in binary `.uasset` format with opaque references.
- Godot uses text `.tscn` files (good), but C# hot-reload is problematic.
- **Result**: AI cannot generate game assets directly; humans must translate between AI output and engine format.

**Problem 2: Hot-Reload Instability**
- Unity domain reload takes 1–10s and can leave zombie script references.
- Unreal Live Coding patches object code and frequently crashes with access violations.
- **Result**: Rapid iteration (AI + human feedback loop) becomes painful.

**Problem 3: Editor Crashes Take Down Everything**
- If the engine crashes while editing, all editor state is lost.
- Undo/redo history vanishes.
- **Result**: Long iteration cycles; humans spend time recovering state instead of building.

**Problem 4: Opaque Data Structures**
- Asset references are GUIDs or hashes that AI cannot parse.
- Prefab override systems are complex and invisible to AI.
- **Result**: AI-generated code and scenes require manual cleanup.

### 2.2 The UzEngine Solution

UzEngine solves these problems by embracing plain text:

1. **All scene data is INI-like text** (`.uzscene`) — AI agents read and write it natively.
2. **All asset paths are `res://` relative** — no GUIDs, no hidden metadata.
3. **Hot-reload is stable** — missing scripts → graceful warning, engine continues.
4. **Editor and runtime are separate processes** — engine crash = stale viewport frame, no lost work.
5. **Scripting supports both C++ and Lua** — high-performance core systems in C++, rapid prototyping in Lua.
6. **Every system is IPC-exposed** — AI agents have direct programmatic access via MCP tools.

---

## 3. Target Users

### 3.1 Primary: AI-Augmented Indie Developer

**Profile**:
- Solo developer or small team (1–5 people)
- Uses AI coding assistant daily (Claude Code, GitHub Copilot, Cursor)
- Comfortable with C++ and game development concepts
- Building mid-complexity games (FPS, RPG, platformer, turn-based strategy, simulation)
- Values iteration speed over visual polish in early development
- Wants to ship a complete game, not maintain a tech stack

**Pain Points with Existing Tools**:
- Spent hours debugging "Missing Script" errors after Claude renamed a class in Unity
- Godot C# hot-reload crashed the editor mid-development
- Unreal's Blueprint graphs are opaque to AI; cannot use Claude to generate gameplay logic
- Asset GUID references break when teamwork merges conflict
- Each IDE switch (VSCode for coding, engine for editing) breaks flow state

**How UzEngine Helps**:
- Claude Code can generate `.uzscene` files directly and attach Lua scripts without human cleanup.
- Lua hot-reload completes in 60ms; iterate continuously with AI.
- Engine crashes never take down VSCode; lose only the current frame.
- MCP tools give Claude direct read/write access to scene state, input, physics results.
- All tooling within VSCode — no window switching.

**Expected Outcome**: Ship a complete, polished game 30–50% faster than with existing engines, via AI-human collaboration.

### 3.2 Secondary: Engine Researcher / Student

**Profile**:
- Computer science or game development student
- Learning modern graphics programming (DX12, deferred shading, PBR)
- Interested in game architecture and physics integration
- Wants a clean, well-documented C++ codebase as reference
- Intimidated by Unreal's complexity, looking for a simpler alternative

**How UzEngine Helps**:
- Clean C++23 codebase with minimal macro magic (no UObject boilerplate, no RTTI hacks).
- Every architectural decision documented in `/docs/engine/design-decisions.md`.
- Each module (Renderer, Physics, Scene, Audio) is independently testable.
- Modular design: you can learn rendering without learning the entire physics engine.
- Well-commented source with design rationale in adjacent `.md` specs.

**Expected Outcome**: Ship coursework or portfolio projects faster; understand modern engine design via readable code.

### 3.3 Tertiary: Technical Artist (Phase 2+)

**Profile**:
- Experienced with Unreal's material editor or Unity's Shader Graph
- Comfortable with HLSL/GLSL but not deep C++
- Wants to iterate on materials, VFX, and shaders in real time
- Uses AI to generate shader code from visual intent

**How UzEngine Helps** (Phase 2+):
- `.uzmat` files are plain text; hot-reload materials in < 50ms.
- HLSL shaders with live error display in VSCode.
- Variant system allows material permutations without editing base shader.
- MCP shader assist tool lets Claude generate HLSL from English descriptions.
- Preview frames show results immediately after reload.

**Expected Outcome**: VFX artist can iterate on visual effects with Claude as co-author; realistic particle systems generated from descriptions.

---

## 4. Product Goals

### 4.1 Phase 1 Goals — MVP Editor

| Goal | Metric | Success Criteria |
|------|--------|------------------|
| **Scene editing** | Users can create, edit, save `.uzscene` files visually | Scene Tree + Inspector fully functional |
| **Lua scripting** | Claude can write and hot-reload Lua scripts | Hot-reload completes in < 100ms (target 60ms) |
| **Developer velocity** | Iteration speed comparable to Godot for simple scenes | Create 5-node scene in < 2 minutes (including Lua script attachment) |
| **Stability** | VSCode never crashes due to engine process failures | 0 crashes per 8-hour dev session |
| **AI usability** | Claude can generate a working 3-node scene from text description alone | 100% success rate on simple scenes (no human correction needed) |
| **Hot-reload robustness** | Lua and shader reloads never corrupt scene state | 1000 consecutive reloads without data loss |

### 4.2 Phase 2 Goals — Full Rendering

| Goal | Metric | Success Criteria |
|------|--------|------------------|
| **Visual quality** | PBR materials render correctly | GGX BRDF + IBL working, materials visually indistinguishable from reference |
| **Asset import** | glTF models import with correct materials and bones | 100% of Sketchfab free models import without error |
| **Performance** | Frame rate at 1080p with typical scene | ≥ 60 FPS for < 10K draw calls |
| **Shadow quality** | Directional light shadows are sharp and artifact-free | CSM 3 cascades, PCF softening, no cascade banding |
| **Audio playback** | Sounds trigger from scene events | XAudio2 plays WAV/OGG with 3D spatial audio, no latency |

### 4.3 Phase 3 Goals — Game Systems

| Goal | Metric | Success Criteria |
|------|--------|------------------|
| **Physics fidelity** | Rigid body simulation is stable and predictable | Jolt Physics stacking 100 boxes without jitter |
| **Animation** | Skeletal mesh animation blends smoothly | Locomotion state machine (idle ↔ walk ↔ run ↔ jump) works |
| **Input response** | Player controller feels responsive | 8ms input latency (frame + input processing) |
| **Save/load** | Game state persists across sessions | Save slot with thumbnail, can resume mid-game |
| **Localization** | Game renders in multiple languages | CSV-based i18n with runtime language switching, CJK fonts work |

### 4.4 Phase 4 Goals — Shipping

| Goal | Metric | Success Criteria |
|------|--------|------------------|
| **Build pipeline** | Developers can generate shipping binary | `.uzpak` builds in < 30s for typical project |
| **Installer** | Game ships with Windows installer | NSIS package with custom icon, version stamp |
| **NavMesh** | AI navigation works in complex levels | Recast baking < 5s for 1000 m² level |
| **Plugin system** | Community plugins integrate cleanly | First-party terrain plugin ships and reloads without crashes |

---

## 5. Functional Requirements

### 5.1 Scene System (Phase 1, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-001** | Engine MUST load `.uzscene` INI-format files into a hierarchical node tree | Enables scene persistence and AI generation |
| **FR-002** | Engine MUST save node tree state to `.uzscene` format | Enables iterative development |
| **FR-003** | Missing script files MUST generate a warning, NOT crash the engine | Graceful degradation for AI-generated code that may be incomplete |
| **FR-004** | Scene hierarchy MUST support arbitrary depth (parent/child/sibling relationships) | Standard game architecture pattern |
| **FR-005** | The IPC server MUST expose `scene.getTree`, `scene.addNode`, `scene.setProperty`, `scene.removeNode` | Enables AI programmatic scene manipulation |
| **FR-006** | Each node MUST have a unique ID within the scene (numeric, stable across save/load) | Required for referencing nodes in IPC commands |
| **FR-007** | Prefabs MUST be instantiable via `prefab = "res://..."` property | Enables prefab reuse without binary duplication |
| **FR-008** | Prefab overrides MUST be readable and writable in `.uzscene` format | Allows AI to generate prefab variants |

### 5.2 Lua Scripting (Phase 1, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-010** | Each scene node MAY have one `.lua` script attached via `script = "res://..."` property | Per-node behavior composition |
| **FR-011** | The LuaJIT VM MUST be sandboxed (no `io.*`, `os.*`, `require`, `load`, `debug.*`) | Security and stability |
| **FR-012** | Lua errors MUST disable the affected node only; engine MUST continue running | Graceful error recovery |
| **FR-013** | `.lua` file changes MUST trigger hot-reload in < 100ms (target: 60ms) | Enables rapid iteration |
| **FR-014** | Lua scripts MUST receive `ctx` table with methods: `scene`, `input`, `audio`, `events`, `saves`, `locale`, `console`, `vfx`, `nav`, `ui`, `log`, `delta_time`, `time_elapsed`, `frame_index` | Provides game engine access |
| **FR-015** | Lua runtime errors MUST be annotated in VSCode as file+line diagnostics | Enables IDE-integrated debugging |
| **FR-016** | Each node script MUST implement optional lifecycle callbacks: `on_start()`, `on_update(delta_time)`, `on_stop()` | Standard game script pattern |
| **FR-017** | Lua scripts MUST be able to read and modify node properties via `ctx.scene` API | Enables self-modification and inter-node communication |

### 5.3 Hot-Reload (Phase 1, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-020** | C++ `game.dll` hot-reload MUST preserve game state via `save_state`/`load_state` protocol | Ensures consistency across reloads |
| **FR-021** | Shader hot-reload MUST recompile only the changed shader (not full pipeline rebuild) | Minimizes iteration latency |
| **FR-022** | Lua hot-reload MUST call `on_stop()` on all affected nodes before reload, `on_start()` after | Ensures proper lifecycle and resource cleanup |
| **FR-023** | Hot-reload MUST NOT drop a frame permanently (may introduce frame stall during reload) | Ensures smooth playback resumes after reload |
| **FR-024** | Hot-reload MUST NOT corrupt node properties or scene hierarchy | Ensures scene state integrity |

### 5.4 Editor UI (Phase 1, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-030** | Scene Tree panel (VSCode TreeView) MUST display full node hierarchy with drag-to-reparent | Intuitive scene editing |
| **FR-031** | Inspector panel (Webview) MUST display and edit all node properties with appropriate widgets (text, number, color, dropdown, asset picker) | Intuitive property editing |
| **FR-032** | Undo/Redo MUST support at least 100 actions with Ctrl+Z / Ctrl+Shift+Z | Standard editor UX |
| **FR-033** | 3D gizmo handles MUST support Translate, Rotate, Scale modes with snapping | 3D editing affordance |
| **FR-034** | Multi-select MUST support Ctrl+click, Shift+click, and box selection | Batch editing |
| **FR-035** | Viewport MUST show a JPEG frame from the engine at 30 FPS when stationary, 60 FPS when moving | Real-time visual feedback |
| **FR-036** | Gizmo drag operations MUST reflect immediately in Inspector and `.uzscene` file | Immediate feedback |

### 5.5 Rendering (Phase 2, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-040** | Renderer MUST implement deferred shading with G-Buffer: Albedo, Normal, Emissive, Depth, Velocity, ObjectID | Enables scalable lighting |
| **FR-041** | Materials MUST support PBR workflow: albedo, roughness, metallic, normal map, emissive | Industry-standard material representation |
| **FR-042** | Renderer MUST support directional light with 3-cascade shadow maps, PCF 3×3 | Shadow quality |
| **FR-043** | Renderer MUST support point lights with dynamic shadow maps or omni-directional dual-paraboloid shadows | Local lighting |
| **FR-044** | Renderer MUST support spot lights with perspective shadow maps | Spot lighting |
| **FR-045** | SSAO (HBAO+, half-res compute) MUST be implemented in Phase 2 | Ambient occlusion realism |
| **FR-046** | Bloom post-process MUST be implemented in Phase 2 | Visual polish |
| **FR-047** | TAA (Temporal Anti-Aliasing) MUST be implemented in Phase 3 | Reduces jaggies, enables SSAO/SSR temporal denoising |
| **FR-048** | SSR (Screen-Space Reflections) MUST be implemented in Phase 3 via Hi-Z ray marching | Local reflection quality |

### 5.6 Asset Pipeline (Phase 2, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-050** | GLTF 2.0 import MUST support meshes, materials, and skeletal data | Industry-standard format |
| **FR-051** | FBX import MUST support meshes, skeletons, and skeletal animation | Asset interchange format |
| **FR-052** | Mesh import MUST generate 3 LOD levels automatically via `meshopt_simplify` | Performance |
| **FR-053** | Texture import MUST compress to BC7 (color) or BC5 (normal) via DirectXTex | Disk and VRAM efficiency |
| **FR-054** | Asset `.import.json` sidecars MUST be AI-readable and editable | Enables AI to configure imports |
| **FR-055** | `.uzpak` format MUST use per-file zstd compression and memory-mapped layout | Efficient shipping |

### 5.7 Physics (Phase 3, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-060** | Physics MUST use Jolt Physics (MIT license) | Open-source, high-performance |
| **FR-061** | Physics MUST run at fixed 60 Hz with accumulator pattern | Deterministic simulation |
| **FR-062** | Collision layers MUST be configurable in `.uzproj` (max 16 layers, bitfield) | Flexible collision filtering |
| **FR-063** | Character controller MUST support slope handling, step climbing, and grounded check | Standard FPS mechanic |
| **FR-064** | Physics queries (raycast, overlap, sweep) MUST be accessible from Lua scripts | Gameplay integration |
| **FR-065** | Rigid body simulation MUST support kinematic and dynamic modes | Editor moving objects + gameplay dynamics |
| **FR-066** | Ragdoll MUST be supported via `RagdollSettings` with active ragdoll motors | NPC death/stun mechanics |

### 5.8 Audio (Phase 2, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-070** | Audio MUST use XAudio2 (Windows native) | System-integrated, zero latency |
| **FR-071** | Audio MUST support 4 buses: Master, SFX, Music, Voice | Sound design flexibility |
| **FR-072** | Audio MUST support 64 simultaneous voices (voice pool, no runtime allocation) | Prevents allocation latency |
| **FR-073** | XAudio2 initialization failure MUST be a warning (game continues with no audio) | Graceful degradation |
| **FR-074** | 3D spatial audio MUST be supported in Phase 3 via X3DAudio | Immersive sound design |
| **FR-075** | Audio hot-reload across `game.dll` reload MUST preserve playing sounds | Smooth iteration |

### 5.9 Animation (Phase 3, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-080** | Animation system MUST support clip sampling with SLERP (rotation) and LERP (position/scale) | Smooth animation playback |
| **FR-081** | Animation state machine MUST support blend spaces and state transitions | Complex animation logic |
| **FR-082** | AnimNotify events MUST trigger at keyframes with callback to game code or Lua | Animation event integration |
| **FR-083** | IK MUST support two-bone analytical solver (limbs) and FABRIK iterative solver (chains) | Procedural animation refinement |
| **FR-084** | Root motion extraction MUST decouple character controller from animation | Avoid double-movement |

### 5.10 Input System (Phase 3, Required)

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **FR-090** | Input MUST support Enhanced Input pattern: `InputAction` + `InputMappingContext` | Device-agnostic, rebindable |
| **FR-091** | Input MUST support XInput (Xbox controller) and keyboard/mouse | Standard game input devices |
| **FR-092** | Input rebinding MUST persist to `saves/bindings.uzinput` | Player customization |
| **FR-093** | Input modifiers MUST include Normalize, Deadzone, Scale, Negate, Swizzle | Advanced input processing |

---

## 6. Non-Functional Requirements

### 6.1 Performance

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **NFR-001** | Viewport frame rate MUST be ≥ 60 FPS at 1080p for < 10K draw calls | Target frame rate |
| **NFR-002** | Lua script hot-reload MUST complete in < 100ms (target 60ms) | Enables rapid iteration |
| **NFR-003** | C++ game.dll hot-reload MUST complete in < 3000ms (target 2500ms) | Acceptable compilation latency |
| **NFR-004** | IPC command round-trip MUST complete in < 50ms | AI tool responsiveness |
| **NFR-005** | Physics step (60 Hz) MUST complete in < 4ms for < 1000 rigid bodies | Real-time simulation |
| **NFR-006** | Scene loading MUST complete in < 2s for scenes with < 500 nodes | Editor responsiveness |
| **NFR-007** | Memory usage MUST stay < 500MB RAM for typical scenes (< 1000 nodes, < 100MB assets) | Modest hardware targets |

### 6.2 Stability

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **NFR-010** | VSCode extension MUST NOT crash due to engine process failures | Editor availability |
| **NFR-011** | Lua runtime errors MUST NOT crash the engine process | Graceful error recovery |
| **NFR-012** | Missing assets (mesh, script, material, texture) MUST NOT crash the engine | Graceful asset missing handling |
| **NFR-013** | Engine MUST write a crash report (minidump + JSON) on unhandled exceptions (SEH) | Post-mortem debugging |
| **NFR-014** | Scene file save MUST be atomic (write to temp, then move) to prevent corruption on crash | Data safety |
| **NFR-015** | 1000 consecutive hot-reloads MUST not degrade engine stability or memory | Iteration stress test |

### 6.3 AI Usability

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **NFR-020** | All file formats MUST be plain text (no binary editor formats) | AI readability |
| **NFR-021** | All asset references MUST use `res://` relative paths (no GUIDs, no hashes) | AI parseability |
| **NFR-022** | The MCP server MUST expose ≥ 20 tools for AI agent interaction | Rich AI interface |
| **NFR-023** | Claude agent MUST be able to create a working 3-node scene from text description alone (100% success) | Demonstrates AI-native design |
| **NFR-024** | File formats MUST support comments and be compressible via gzip (< 10KB for typical 50-node scene) | Efficient data interchange |

### 6.4 Maintainability

| Requirement ID | Requirement | Rationale |
|---|---|---|
| **NFR-030** | All new code MUST achieve ≥ 95% line coverage via unit tests | Code reliability |
| **NFR-031** | All C++ headers MUST compile with zero warnings under `-Wall -Wextra -Wpedantic` | Code quality |
| **NFR-032** | All public APIs MUST be documented in `/docs/` specs or code comments | Maintainability |
| **NFR-033** | C ABI between `UzEngine.exe` and `game.dll` MUST be backward-compatible (fields append-only) | Forward compatibility |
| **NFR-034** | All architectural decisions MUST be documented in `/docs/engine/design-decisions.md` | Knowledge preservation |

---

## 7. Out of Scope

The following features are **explicitly not planned** for Phase 1–4:

### 7.1 Networking / Multiplayer
- Rollback netcode (client-side prediction, deterministic simulation)
- Steam Networking, Unreal Replication Graph, or similar
- **Reason**: Requires game design constraints (deterministic Lua, fixed timestep physics). Planned for Phase 5+.

### 7.2 Console / Mobile Ports
- PlayStation, Xbox, Nintendo Switch, iOS, Android
- **Reason**: Windows DX12 only for Phase 1–4. Platform abstraction in place for future ports.

### 7.3 Visual Scripting
- Blueprint equivalent, visual state machine editor, dialogue tree UI
- **Reason**: Lua + MCP tools + AI code generation cover the same use cases better.

### 7.4 Cloud Services
- Steam Cloud saves, cloud game recording, telemetry dashboards
- **Reason**: Phase 4+ extension. Local save slots are sufficient.

### 7.5 VR / AR Support
- OpenXR, SteamVR, Meta Quest
- **Reason**: Not in current roadmap. Requires separate viewport and input handling.

### 7.6 Procedural Generation Tools
- Marching Cubes, Perlin noise visualizers, L-system tools
- **Reason**: Game logic responsibility, not engine responsibility.

### 7.7 Built-In Dialogue System
- Dialogue tree nodes, choice branching, conversation state machine
- **Reason**: Better implemented as a game plugin or MCP tool.

### 7.8 Version Control Integration
- Git diff tools, merge conflict resolution for scene files
- **Reason**: Plain-text formats are already diff-friendly; external tools (git-merge-driver) handle special cases.

---

## 8. Technical Constraints

### 8.1 Platform & Hardware

| Constraint | Specification | Rationale |
|---|---|---|
| **Target OS** | Windows 10 Pro or later (x64) | DirectX 12 native |
| **GPU Requirement** | DX12 Feature Level 12.0 (≈ GTX 950 / RX 480 and newer) | Modern GPU features for deferred rendering |
| **CPU Requirement** | Intel 6th gen Core or AMD Ryzen 1000+ | Supports AVX2, modern cache hierarchy |
| **RAM** | 8 GB minimum, 16 GB recommended | Editor + engine + assets simultaneously |
| **Storage** | SSD 256 GB (OS + engine + projects) | Reasonable for development machines |

### 8.2 Compiler & Language

| Constraint | Specification | Rationale |
|---|---|---|
| **Compiler** | MSVC 2022 (v143), C++23 | Modern STL, modules (future), std::ranges |
| **Architecture** | x64 only | Aligns with engine target platforms |
| **Flags** | `-Wall -Wextra -Wpedantic` (zero warnings) | Code quality |
| **RTTI** | Disabled (`/GR-`) in hot-reload DLL | Stability |

### 8.3 Dependencies

| Library | Version | Rationale |
|---|---|---|
| **GLM** | ≥ 0.9.9 | Math library, header-only |
| **entt** | ≥ 3.13 | ECS, header-only |
| **nlohmann/json** | ≥ 3.11 | JSON serialization |
| **IXWebSocket** | ≥ 13.x | WebSocket server |
| **libjpeg-turbo** | ≥ 3.x | JPEG encoding/decoding |
| **Jolt Physics** | ≥ 5.x (Phase 3) | Physics simulation |
| **fastgltf** | ≥ 0.8 (Phase 2) | glTF import |
| **ufbx** | ≥ 0.14 (Phase 2) | FBX import |
| **DirectXTex** | ≥ 2024.x (Phase 2) | Texture compression |
| **zstd** | ≥ 1.5 (Phase 2) | `.uzpak` compression |
| **LuaJIT** | 2.1 (Phase 1) | Scripting runtime |
| **Recast + Detour** | ≥ 1.6 (Phase 4) | NavMesh baking |

All dependencies managed via **vcpkg** (no manual `.lib` copies).

---

## 9. Success Metrics

### 9.1 Phase 1 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Functional completeness** | All Phase 1 Rulebook tasks marked complete | 38 tasks in `.rulebook/tasks/phase1-*/` |
| **Test coverage** | ≥ 95% line coverage for all new code | `cmake --build . --target coverage` |
| **AI usability** | Claude creates 3-node scene without human correction | Run prompt test; count zero-correction succeeds |
| **Hot-reload performance** | Lua reload in 60ms (p99: 100ms) | Time measurement on reference hardware (i7-12700K) |
| **Stability** | Zero crashes in 8 hours of continuous iteration | Time trial with rapid hot-reload cycles |
| **Documentation** | All specs in `/docs/specs/` complete and current | Spec audit against code |

### 9.2 Phase 2 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Render quality** | GGX PBR materials visually match reference images | Compare glTF samples side-by-side |
| **Import fidelity** | 95% of Sketchfab free models import without error | Batch import test of 100 random models |
| **Performance** | ≥ 60 FPS at 1080p for < 10K draw calls | GPU benchmark on reference hardware |
| **Asset compression** | `.uzpak` archive < 50% of source assets | Measure PAK size vs. uncompressed total |
| **Audio latency** | < 10ms from sound trigger to audible output | Latency measurement with oscilloscope |

### 9.3 Phase 3 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Physics stability** | 100 boxes stack without jitter or tunneling | Physics stress test |
| **Animation smoothness** | State machine blends with zero glitches | Visual inspection of locomotion transitions |
| **Save persistence** | Game state survives save/load cycle with 100% data integrity | Serialize→ deserialize→ compare |
| **Input responsiveness** | 8ms input-to-response latency (frame + input processing) | Input lag measurement tool |
| **Localization** | All UI text renders in 5 languages with correct fonts | Visual inspection, CJK text test |

### 9.4 Phase 4 Success Criteria

| Metric | Target | Measurement |
|--------|--------|-------------|
| **Build time** | `.uzpak` generation < 30s for 500MB project | Time measurement |
| **NavMesh quality** | Agents navigate complex level without getting stuck | Path finding test with obstacles |
| **Plugin stability** | Third-party plugin reloads without crashes | Load→ unload→ reload cycle, 100 iterations |
| **Installer quality** | Windows installer installs and runs game without errors | Install test on clean Windows 10 VM |

### 9.5 Long-Term Success

- **Community**: ≥ 100 GitHub stars, ≥ 10 community-contributed plugins
- **Adoption**: ≥ 1 indie game shipped with UzEngine
- **Performance reputation**: UzEngine cited as "faster to iterate with AI than Unity/Godot" in game dev communities
- **Documentation**: ≥ 50 tutorial videos, ≥ 20 third-party guides

---

## 10. Appendix: Rulebook Tasks Reference

Complete mapping of all 38 implementation tasks to phases, specs, and complexity:

| Task ID | Phase | Complexity | Spec | Description |
|---------|-------|-----------|------|-------------|
| `phase1-scene-system` | 1 | L | scene-system | Node tree, `.uzscene` parser, SceneManager |
| `phase1-editor-panels` | 1 | L | editor-panels | Scene Tree, Inspector, Asset Browser (VSCode extension) |
| `phase1-play-mode` | 1 | M | runtime | Separate `--play` process, play controls |
| `phase1-shader-hot-reload` | 1 | M | shader-system | DXC integration, file watcher, error display |
| `phase1-asset-browser` | 1 | M | asset-pipeline | Asset registry, preview, import UI |
| `phase1-lua-scripting` | 1 | L | lua-scripting | LuaJIT per-node, hot-reload, sandboxing |
| `phase1-timer-system` | 1 | S | timer-system | Lua timer API, scheduling |
| `phase1-debug-profiling` | 1 | M | debug-profiling | Tracy, ImGui overlay, doctest |
| `phase1-undo-redo` | 1 | M | undo-redo | Command pattern, Ctrl+Z/Shift+Z |
| `phase1-editor-gizmos` | 1 | L | editor-gizmos | 3D translate/rotate/scale, drag mechanics |
| `phase1-multi-select` | 1 | M | multi-select | Bulk selection, shared inspector |
| `phase1-console-commands` | 1 | M | console-commands | In-game debug console, command registration |
| `phase1-crash-reporting` | 1 | M | crash-reporting | Minidump, crash JSON log, system info |
| `phase2-pbr-renderer` | 2 | XL | renderer | Deferred GBuffer, GGX BRDF, IBL, tonemapping |
| `phase2-gltf-import` | 2 | L | asset-pipeline | fastgltf, LOD via meshoptimizer |
| `phase2-shadow-maps` | 2 | L | renderer | CSM 3 cascades, PCF, cascade blending |
| `phase2-fbx-import` | 2 | M | asset-pipeline | ufbx, FBX → `.uzmesh`, preview |
| `phase2-texture-tools` | 2 | M | asset-pipeline | DirectXTex, BC7/BC5/BC6H compression |
| `phase2-asset-pak` | 2 | M | build-system | `.uzpak` format, zstd, VFS, CLI packer |
| `phase2-shader-variants` | 2 | M | shader-variants | Permutation system, auto-detection, PSO cache |
| `phase2-shader-library` | 2 | M | shader-library | Shared HLSL includes, GlobalParams |
| `phase2-point-spot-shadows` | 2 | L | point-spot-light-shadows | Point dual-paraboloid, spot perspective, atlases |
| `phase2-gpu-instancing` | 2 | M | gpu-instancing | Dynamic instancing, BatchBuilder, StructuredBuffer |
| `phase2-audio-system` | 2 | L | audio-system | XAudio2 engine, 4-bus mixing, Lua API |
| `phase2-ssao` | 2 | M | screen-space-effects | HBAO+ compute, bilateral blur |
| `phase2-ui-system` | 2 | L | ui-system | HTML/CSS game UI, data binding |
| `phase2-prefab-edit-mode` | 2 | M | prefab-edit-mode | Prefab isolation, override panel |
| `phase2-ai-native-features` | 2 | L | ai-native-features | MCP tools: scene_generate, shader_assist, scene_analyze |
| `phase3-skeletal-mesh` | 3 | L | skeletal-mesh | GPU skinning, morph targets, physics asset |
| `phase3-animation-system` | 3 | L | animation-system | State machine, blending, AnimNotify, IK |
| `phase3-ragdoll` | 3 | M | skeletal-mesh | Jolt ragdoll, active ragdoll motors |
| `phase3-input-system` | 3 | M | input-system | Enhanced Input, XInput, rebinding |
| `phase3-camera-system` | 3 | M | camera-system | SpringArm, modes, shake, DoF |
| `phase3-physics-system` | 3 | XL | physics-system | Jolt rigid bodies, colliders, triggers, character controller |
| `phase3-particle-system` | 3 | L | particle-system | GPU compute particles, `.uzfx` format, VFX |
| `phase3-event-bus` | 3 | S | event-bus | C++ EventBus<T>, Lua dispatch, engine events |
| `phase3-tags-groups` | 3 | S | tags-groups | Node tags, find_by_tag queries |
| `phase3-save-system` | 3 | M | save-system | Save/load game state, msgpack, save slots, autosave |
| `phase3-localization` | 3 | M | localization | CSV i18n, multi-language, runtime switching, font fallback |
| `phase3-ssr` | 3 | L | screen-space-effects | Hi-Z ray march SSR, temporal reprojection |
| `phase3-taa` | 3 | M | screen-space-effects | TAA resolve, Halton jitter, variance clamping |
| `phase4-build-pipeline` | 4 | L | build-system | Cook → Pack → Compile → Stage → Package |
| `phase4-navmesh` | 4 | L | navmesh | Recast baking, NavMeshAgent, DetourCrowd, RVO2 |
| `phase4-plugin-system` | 4 | L | plugin-system | `.uzplugin` manifest, stable C ABI, hot-reload |
| `phase4-terrain-system` | 4 | XL | terrain-system | `.uzterrain`, heightmap, splatmap, LOD, foliage |

**Complexity Legend**: S (Small, 1–3 days) | M (Medium, 1–2 weeks) | L (Large, 2–4 weeks) | XL (Extra Large, 1–2 months)

---

## 11. Document History

| Version | Date | Author | Changes |
|---------|------|--------|---------|
| 1.0 | 2026-03-01 | Documentation Operator | Initial PRD creation from engine vision, design decisions, and roadmap |

---

## 12. Related Documents

- [Engine Vision & Architecture Spec](specs/engine-vision.md) — Full technical architecture
- [Design Decisions & Comparisons](engine/design-decisions.md) — Why each choice was made
- [Module Structure](engine/module-structure.md) — Source tree organization
- [Lifecycle & Concurrency](engine/lifecycle-and-concurrency.md) — Threading model
- [Hot-Reload Mechanism](engine/hot-reload.md) — Technical deep dive
- All Phase specs in `/docs/specs/` — Detailed system designs
