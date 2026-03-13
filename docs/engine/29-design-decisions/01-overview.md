# Design Decisions — Philosophy & Rationale

> **Status**: Planned (Phase 0–4)
> **Source**: Architecture Decision Records (ADRs) in `docs/architecture/decisions/`
> **References**: POC 0.1 & 0.2 findings, UE5/Godot/Unity architecture studies

## Purpose

UzEngine is **not a clone** of Unreal, Godot, or Unity. It is the **first engine designed from the ground up for AI-assisted development**. This document captures the key architectural decisions that differentiate UzEngine and explains the rationale behind each choice.

Every decision trades off complexity, performance, and developer experience. When you encounter an unusual pattern or limitation, the ADRs explain why that choice was made and what alternatives were rejected.

## Core Philosophy

### 1. Reference-Driven Clean-Room Development

**Decision**: Study UE5 and Godot as references, but reimplement from scratch. Never copy code.

**Rationale**:
- UE5's source (`W:\UE_5.6_Source`) is our primary reference for parameter naming, math conventions, component hierarchy, and architecture patterns.
- Godot's source (`E:\godot`) provides alternative approaches for comparison.
- **Clean-room**: Read the reference, understand the approach, implement independently in UzEngine style.
- **Why?** Legal clarity, code ownership, opportunities to improve on reference designs.

**Consequence**:
- Lower implementation speed (re-implementing vs. copy-paste), but higher ownership and customization.
- Developers learn the "why" behind each pattern, not just the "how."

**Example**: UE5 uses `FVector` (SSE-aligned 16-byte struct), UzEngine uses `Vec3` (12-byte struct with DirectXMath SIMD ops). Different representation, same math semantics.

### 2. Open File Formats for LLM Manipulation

**Decision**: All engine data formats (scenes, shaders, configs, meshes, animations) are text-based or easily parseable.

**Formats**:
- Scenes: `.uzscene` (JSON with node hierarchy, components, properties)
- Shaders: `.uzshader` (HLSL with metadata JSON header)
- Textures: `.uztexture` (PNG/HDR + JSON metadata)
- Audio: `.uzaudio` (WAV/MP3/OGG + JSON metadata)
- Config: `.uzproject` (TOML)
- Archived: `.uzpak` (ZIP-compatible, readable)

**Rationale**:
- LLMs excel at text generation and transformation. JSON, HLSL, and TOML are in LLM training data.
- Humans can edit formats directly (manual debugging, scripted changes).
- Version control (git diff) is readable.
- Zero proprietary serialization; easy to write import/export tools.

**Consequence**:
- Slightly larger files on disk (vs. binary formats). Acceptable; modern storage is cheap.
- No encryption of game logic. Acceptable; if you're shipping in-game Lua scripts, they're already visible anyway.

**Example**: A scene file:
```json
{
  "nodes": [
    {
      "id": "player",
      "components": {
        "SceneComponent": { "position": [0, 1.7, 0], "rotation": [0, 0, 0], "scale": [1, 1, 1] },
        "CharacterControllerComponent": { "height": 1.8, "radius": 0.4 },
        "ScriptComponent": { "script": "scripts/player.lua" }
      }
    }
  ]
}
```

An LLM can generate this directly. A human can read and edit it. `git diff` shows exactly what changed.

### 3. Zig Build System (Cross-Compilation & Reproducibility)

**Decision**: Build engine with Zig 0.13+, not CMake or custom build script.

**Rationale**:
- **Cross-compilation**: Zig has first-class cross-compilation. Build for Windows/Linux/macOS from any platform.
- **C/C++ interop**: Zig's C FFI is seamless. No wrapper code needed; Zig directly calls DX12, Vulkan, Jolt Physics.
- **Reproducible builds**: Zig hash-tracks all dependencies and sources. Same hash = identical binary.
- **Single language for build logic**: No shell scripts, no CMake conditionals, just Zig code.
- **Dependency management**: Zig package manager (via `build.zig.zon`) is simpler than vcpkg or conan.

**Consequence**:
- Zig is less mature than CMake. Some edge cases require workarounds.
- Developers must learn Zig (or learn Zig's C API sufficiently).

**Alternative rejected**: CMake — good, but not cross-platform by default; requires per-platform configuration.

### 4. DX12 SM6.6 as Primary Rendering Path

**Decision**: Build renderer around SM6.6 **bindless** resource access. SM5.x as fallback.

**Rationale**:
- **Bindless**: Shaders access resources via `ResourceDescriptorHeap[index]` without descriptor tables. Eliminates descriptor management complexity.
- **Modern features**: Mesh shaders, variable rate shading, sampler feedback, DirectStorage.
- **Industry adoption**: UE5 uses bindless, Microsoft DirectStorage assumes bindless, modern Unreal features rely on it.
- **Fallback to SM5.0**: Traditional Input Assembler (IAB) + register-based binding for older GPUs. Same engine binary supports both via `#ifdef UZ_BINDLESS`.

**POC 0.2 validation**: Attempted SM5.0-first approach in 0.1 — painful. Created two code paths early, forced complexity. Learned: **start with modern, backport if needed, not vice versa**.

**Consequence**:
- Dropped support for DX11. DX12 Feature Level 12_0 (minimal SM5.x) is threshold.
- Older GPUs (GeForce GTX 960 and earlier) not supported.

**Code split**:
```hlsl
// Bindless path (SM6.6)
#ifdef UZ_BINDLESS
    float3 normal = g_ResourceHeap[meshData.normalTexture].Load(uv);
#else
    // Fallback path (SM5.0)
    float3 normal = g_NormalTexture.Sample(g_Sampler, uv);
#endif
```

### 5. N-API In-Process (Not Separate Process IPC)

**Decision**: Runtime is a Node.js N-API `.node` addon, NOT a separate executable.

**Rationale**:
- **Latency**: Function calls = direct C++ calls (< 1µs). Separate process would require serialization + IPC round-trip (> 1ms).
- **Shared memory**: Typed arrays in JS directly reference memory in C++. Zero-copy bulk data transfer.
- **Simplicity**: One process = one memory space. No process lifecycle management, no socket/pipe plumbing.
- **Hot-reload**: FileWatcher in runtime notifies extension directly. No process restart needed.

**POC 0.1 rejected this**: Used WebSocket + separate process. Introduced 5–10ms IPC latency per frame. Also: process pool cold-start was 2–3 seconds (unacceptable).

**POC 0.2 validated N-API in-process**: Latency < 1ms, scene load < 100ms, frame streaming 120 FPS with H.264 encoding.

**Consequence**:
- Runtime crashes crash the extension (and VSCode). Must be rock-solid.
- Requires NAPI_DISABLE_CPP_EXCEPTIONS (Electron incompatibility workaround).
- Memory usage is shared (extension + runtime in one process).

### 6. N-API Version Binding to Electron

**Decision**: Compile N-API addon against specific Electron version, not generic Node.js.

**Rationale**:
- VSCode is built on Electron, not generic Node.js.
- Electron version is tied to specific Node.js version (e.g., VSCode 1.111 = Electron 39.6.0 = Node 22.22.0).
- N-API ABI 140 is the interface — addon must be compiled for that ABI.
- If ABI mismatch, addon won't load ("DLL not found" or "invalid ABI").

**Current**: VSCode 1.111.0 = Electron 39.6.0 / Node 22.22.0 / ABI 140.

**Compile flag**: `--runtime=electron --runtime-version=39.6.0`

**Consequence**:
- N-API addon binary is Electron-version-specific. If user updates VSCode, addon must recompile.
- Automatic via `npm run build`, but must be in CI/CD.

### 7. LuaJIT for Scripting (Performance & Hot-Reload)

**Decision**: Embedded LuaJIT VM for game logic and mods, not C# or GDScript.

**Rationale**:
- **FFI performance**: LuaJIT's FFI can call C++ directly. Native call speed, no JIT compilation latency.
- **Hot-reload**: `.lua` files are interpreted. Change file → reload script (no recompile).
- **Sandboxing**: VM can strip dangerous globals (`os.execute`, `io.*`). Safe for mod distribution.
- **Lightweight**: ~400KB binary, minimal overhead.
- **LLM-friendly**: Lua syntax is simple, in training data.

**Alternative rejected**: C# — requires .NET runtime, larger binary, no native FFI speed.

**Alternative rejected**: GDScript — Godot-specific, not portable.

### 8. Jolt Physics (Deterministic, Modern C++)

**Decision**: Use Jolt Physics 5.1+, not Bullet or PhysX.

**Rationale**:
- **Deterministic**: Jolt is designed for networked games. Same input = same output (bit-perfect), essential for networked play and replay.
- **Modern C++**: Clean, std-aligned API. No legacy baggage (unlike Bullet or PhysX).
- **Active development**: Used by Horizon Forbidden West, Starfield. Ongoing support.
- **Open-source**: MIT license. Can inspect/modify if needed (e.g., custom collision handlers).

**Consequence**:
- Smaller community than Bullet (less Stack Overflow content).
- Performance-tuning knowledge must be learned directly from Jolt docs.

### 9. Content-Hash Cache (Not Timestamps)

**Decision**: Invalidate caches via content hash (xxHash64), not modification timestamps.

**Rationale**:
- **Deterministic**: Same source = same hash. Independent of filesystem time.
- **Distributed workflows**: Network drives, cloud sync (Dropbox, Google Drive). Timestamps are unreliable.
- **Cache correctness**: If asset hasn't changed (hash match), no recompile. Guarantees cache is up-to-date.
- **Includes tracked**: Hash includes source + all includes + compiler flags + target platform. Recompile if any change.

**Hash calc**:
```cpp
auto content_hash = xxHash64(source_code + all_include_contents + compiler_flags);
auto cache_key = fmt::format("{}_{:016x}.cache", asset_path, content_hash);
// Load from .cache/shader_{}_{:016x}.cache if exists, skip compile
```

**Consequence**:
- Slightly slower first load (must hash includes, not just check timestamp).
- But: more reliable. Worth it.

### 10. File Format Versioning (Not Backwards Compatibility)

**Decision**: Version all file formats (`.uzscene`, `.uzshader`, `.uztexture`). Breaking changes increment version. Old versions not supported.

**Rationale**:
- **Simplicity**: No version upgrade code. Format evolves, old files become obsolete.
- **Clarity**: Clear versioning prevents ambiguity (e.g., is `rotation` a Quat or Euler angles?).
- **Migration tool**: Old projects migrate via a tool (`uz migrate`), not runtime compatibility layer.

**Consequence**:
- Projects must be migrated when engine major version changes.
- But: engine code is simpler, fewer bugs.

## Architectural Comparison

| Decision | UE5 | Godot | Unity | **UzEngine** |
|----------|-----|-------|-------|-------------|
| Reference | Custom | Community | Custom | **UE5 + Godot (clean-room)** |
| File formats | Binary (uasset) | XML/binary mix | Binary (Unity assets) | **Text (JSON/HLSL/TOML)** |
| Build system | Unreal Build Tool (C#) | SCons | Unity build pipeline | **Zig** |
| Renderer baseline | DX12 + Vulkan | Vulkan | DX11 + Metal | **DX12 SM6.6 bindless** |
| IPC | Named pipes | In-process | In-process | **N-API in-process** |
| Scripting primary | C++ + Blueprint | GDScript | C# | **C++ + Lua** |
| Physics | PhysX | Godot Physics + Jolt | PhysX | **Jolt Physics** |
| Cache invalidation | Timestamps + hashing | Timestamps | Timestamps | **Content hash** |

## Key Lessons from POC

### POC 0.1: WebSocket Frame Streaming (Failed)
- **Goal**: Stream 3D frames to VSCode webview.
- **Approach**: Separate process, WebSocket, JPEG streaming.
- **Result**: 5–10ms IPC latency, 2 FPS viewport (unworkable).
- **Lesson**: Separate process is too slow. N-API in-process essential.
- **Resolution**: POC 0.2 moved to N-API addon, eliminated IPC latency.

### POC 0.2: H.264 Frame Streaming (Success)
- **Goal**: Stream 3D frames at 120 FPS to 2D editor panels.
- **Approaches tried**:
  - Raw RGBA copy: 2 FPS (bandwidth-limited)
  - JPEG (stb): 138 FPS (but low quality, still limited)
  - WebP (libwebp): 24 FPS (codec too slow)
  - H.264 (Media Foundation): **120 FPS** (hardware-accelerated)
- **Lesson**: Hardware encoding necessary for real-time video. WebCodecs + VideoDecoder browser API is performant.

### POC 0.2: DX12 SM6.6 Validation
- **Goal**: Render with modern features (mesh shaders, bindless).
- **Attempt**: SM5.0-first (safer).
- **Result**: Double code paths from day 1, confusing debugging, dual-branch complexity.
- **Resolution**: Start SM6.6, backport SM5.0 support later if needed.
- **Lesson**: Start modern, backport conservative. Reverse (start conservative, add modern) creates tech debt.

## Implementation Phases

| Phase | Key Decisions Solidified |
|-------|-------------------------|
| **Phase 0** | Clean-room reference study complete, N-API validated, H.264 streaming proven |
| **Phase 1** | DX12 SM6.6 primary path locked, Jolt Physics selected, Zig build system deployed |
| **Phase 2** | LuaJIT VM integrated, content-hash cache implemented, file format versioning locked |
| **Phase 3** | Save/Load system (binary msgpack + JSON debug), Localization (CSV), full IPC spec |
| **Phase 4** | Networking (Netcode for Entities), breakpoint debugging, advanced physics (cloth, particles) |

## Architecture Decision Records

Each major decision has a formal ADR in `docs/architecture/decisions/`:

- ADR-0001: N-API in-process vs separate process
- ADR-0002: DX12 SM6.6 bindless as primary
- ADR-0003: LuaJIT vs C# for scripting
- ADR-0004: Content-hash cache versioning
- ADR-0005: Open text formats (JSON/HLSL/TOML)
- ADR-0006: Jolt Physics selection
- ADR-0007: Zig build system

## Deviation Policy

If you encounter a situation where following these decisions creates a problem:

1. **Document the issue** — create an issue in the tracker.
2. **Propose an alternative** — explain the deviation and its cost.
3. **Validate via POC** — implement a small proof-of-concept.
4. **Consensus** — discuss with core team.
5. **Update ADR** — if approved, update the ADR and this document.

**Do not deviate silently.** Silent deviations create inconsistency and technical debt.

## Quick Reference

**For quick decisions, consult this table:**

| Scenario | Decision | Why |
|----------|----------|-----|
| How should I name an API? | Use UE5 conventions, clean-room impl. | Reference alignment, LLM familiarity |
| What file format for new asset type? | JSON or TOML + readable section | LLM manipulation, human readable |
| Should I support old file format? | No. Migrate via tool. | Keep engine code simple |
| What scripting language? | C++ (perf) or Lua (rapid) | Both first-class, LLM-friendly |
| How to invalidate cache? | Content hash (xxHash64) | Deterministic, distributed-safe |
| New physics feature? | Implement in Jolt, expose to Lua/C++ | Use standard Jolt API, keep binding thin |
| Frame streaming format? | H.264 (hardware encode), WebCodecs decode | 120 FPS, low latency |
