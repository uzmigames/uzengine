# Technology Choices — Decision Matrix & Rationale

> **Status**: Completed (Phase 0–1)
> **Source**: Architecture Decision Records, research artifacts
> **References**: Industry comparisons (Unreal, Godot, Unity, Stride)

## Decision Framework

For each technology choice, we evaluate:

1. **Performance** — Throughput, latency, memory usage
2. **Productivity** — Development velocity, learning curve, ecosystem
3. **Ownership** — License, maintenance burden, source availability
4. **Differentiation** — Does it align with UzEngine's unique mission (AI-first)?
5. **Scalability** — Can it grow from POC to full engine?

## Key Technology Choices

### 1. Build System: Zig vs CMake vs Meson

| Aspect | CMake | Meson | **Zig** |
|--------|-------|-------|--------|
| **Cross-compilation** | Per-platform setup | Built-in | **First-class** |
| **Dependency management** | vcpkg (external) | wrap() (works) | **build.zig.zon (native)** |
| **Language** | Domain-specific (TCL-like) | Domain-specific | **General-purpose (Zig lang)** |
| **Complexity** | High (lots of boilerplate) | Medium | **Medium (steep initial curve)** |
| **Community size** | Huge | Growing | Small |
| **C/C++ interop** | Via cmake_parse | Via declare_dependency | **FFI built-in** |
| **Reproducibility** | Depends on scripts | Good | **Excellent (hash-tracked)** |

**Decision**: **Zig** for UzEngine.

**Rationale**:
- First-class cross-compilation (ship to Windows, Linux, macOS from any platform)
- Native dependency management (no external tool like vcpkg)
- Zig's C FFI eliminates wrapper code for DX12, Vulkan, Jolt
- Hash-tracked build ensures reproducible binaries
- Aligns with "small codebase, high ownership" philosophy

**Tradeoff**: Zig ecosystem smaller than CMake/Meson. Some third-party packages require custom build integration. Acceptable: core dependencies (DX12, Vulkan, Jolt) are C and have good FFI support.

**Reference**: UE5 uses Unreal Build Tool (proprietary), Godot uses SCons (Python-based). Neither was suitable for UzEngine's multi-platform, dependency-light approach.

### 2. Renderer: DirectX 12 SM6.6 vs Vulkan vs DX11

| Aspect | DX11 | Vulkan | **DX12 SM6.6** |
|--------|------|--------|---|
| **Modern features** | Limited | Yes | **Yes (mesh shaders, bindless)** |
| **Cross-platform** | Windows only | Linux/Mac/Android | **Windows primary** |
| **Learning curve** | Moderate | Steep | Moderate |
| **Validation tools** | Limited | SPIR-V + Vulkan Profiles | **DRED, shader debugging** |
| **Industry adoption** | Declining | Growing | **Industry standard (next-gen)** |
| **Driver maturity** | Mature (stable) | Maturing | Excellent |
| **Bindless support** | No (workaround) | Yes (via descriptors) | **Yes (native)** |

**Decision**: **DX12 SM6.6 as primary**, DX12 SM5.0 and Vulkan as fallbacks.

**Rationale**:
- **SM6.6 bindless** eliminates descriptor table management (biggest source of DX12 complexity)
- Mesh shaders, variable rate shading, DirectStorage all SM6.6+ features
- UE5 uses DX12 SM6.6 as baseline (validates approach)
- Microsoft invested in DirectX (not sunsetting anytime soon)
- **"Modern baseline"** philosophy: start with cutting-edge, backport if needed (not vice versa)

**Tradeoff**: No Metal/console support initially. Acceptable for Phase 1–2 (Windows-focused). Phase 3+ adds console-specific RHI backends.

**SM5.0 fallback rationale**: Older GPUs (GTX 960 and earlier) lack SM6.6. Rather than abandon them, create `#ifdef UZ_BINDLESS` split. POC 0.2 proved this is manageable (separate shader paths, same binary).

### 3. Scripting: LuaJIT vs C# vs GDScript

| Aspect | GDScript | C# | **LuaJIT** |
|--------|----------|-----|---|
| **Language maturity** | Godot-specific | Mature (.NET) | Mature (20+ years) |
| **Performance** | Slow JIT | Fast JIT | **Fastest JIT for FFI** |
| **Hot-reload** | Easy (same VM) | Difficult (AppDomain unload) | **Trivial (interpreter)** |
| **FFI to C++** | Binding layer | P/Invoke overhead | **Direct call (LuaJIT FFI)** |
| **Sandbox** | Built-in (Godot VM) | Partial (.NET Security) | **Easy (strip unsafe APIs)** |
| **Ecosystem** | Godot-only | Massive (.NET) | Small (but focused) |
| **File format** | Text (.gd) | Text (.cs) | **Text (.lua)** |

**Decision**: **LuaJIT** (with C++ for performance-critical code).

**Rationale**:
- **Hot-reload is critical** for AI-assisted development (change script → reload scene instantly)
- **FFI performance** — LuaJIT FFI calls native C++ at near-native speed (~10% overhead). C# P/Invoke adds measurable latency.
- **Simplicity** — Lua is lightweight, in training data for LLMs (can generate Lua code)
- **Sandboxing** — Can disable `os.execute`, `io.*` to prevent untrusted scripts from damaging system
- **File format** — Plain text (readable by LLMs, git-diff friendly)

**Tradeoff**: Smaller ecosystem than C#. Less IDE support. Acceptable: core performance code stays C++, Lua for game logic only.

**Reference**: Roblox uses Lua (40 million players, proven at scale). Game Dev Kit uses Lua. Our philosophy aligns.

### 4. Physics: Jolt vs Bullet3 vs PhysX

| Aspect | Bullet3 | PhysX | **Jolt** |
|--------|---------|-------|---|
| **Determinism** | Deterministic (bit-exact) | Non-deterministic (float approximation) | **Deterministic (bit-exact)** |
| **Modern C++** | Legacy C (90s code) | C++ (but proprietary) | **Modern C++17** |
| **Active development** | Minimal | Nvidia maintains | **Continuous (Horizon FW)** |
| **Performance** | Good | Excellent | **Excellent** |
| **Constraints** | Basic | Advanced (motors, constraints) | **Advanced** |
| **Open source** | Yes (ZLib) | Yes (BSD-3, but NVIDIA owned) | **Yes (MIT)** |
| **Use cases** | Game Dev, research | UE5, Unreal | Horizon Forbidden West, Starfield |

**Decision**: **Jolt Physics**.

**Rationale**:
- **Determinism is essential** for networked games and replay systems (same input = same output, bit-perfect)
- **Modern C++** API is clean, easier to bind to Lua, integrate with ECS
- **Active development** — Used in AAA games, ongoing support
- **MIT license** — Can modify if needed (unlike PhysX proprietary terms)
- **Open source** — Can inspect/audit (important for AI-driven systems where transparency matters)

**Tradeoff**: Smaller community than Bullet3. Less Stack Overflow content. Acceptable: Jolt is well-documented, used by industry, and codebase is readable.

### 5. File Formats: JSON vs YAML vs TOML

| Format | JSON | YAML | **TOML** |
|--------|------|------|---|
| **Readability (humans)** | Moderate | Excellent | Excellent |
| **Readability (LLMs)** | **Excellent** | Good | Excellent |
| **Writing by hand** | Error-prone | Easy | Easy |
| **Parsing complexity** | Trivial | Complex (whitespace-sensitive) | Simple |
| **Schema validation** | JSON Schema (standard) | No standard | No standard |
| **Nested data** | Easy | Easy | Moderate |
| **Comments** | No | Yes | Yes |
| **Use cases** | APIs, config | Config, docs | Config, Cargo.toml |

**Decision**: **JSON for assets (.uzscene, .uzlevel), TOML for project config (.uzproject)**.

**Rationale**:
- **JSON**: LLMs train on massive JSON datasets. Can generate scene files directly from prompts.
- **TOML**: Human-friendly project config (key=value is intuitive). Comments allowed.
- **Schema validation**: JSON Schema standard, enforced by tools
- **Parsing**: No YAML ambiguity (two spaces vs tabs? Quoted string vs key?), which can cause LLM issues

**Tradeoff**: More verbose than YAML, but clarity > brevity in AI-driven contexts.

### 6. Dependency Management: vcpkg vs Conan vs Zig build.zig.zon

| Tool | vcpkg | Conan | **Zig build.zig.zon** |
|------|-------|-------|---|
| **Language** | CMake (DSL) | Python (general) | Zig (general) |
| **Manifest mode** | Recent addition | Native | **First-class** |
| **Binary cache** | Per-machine | Per-recipe | **Hash-based** |
| **Transitive deps** | Auto-resolved | Manual/auto | **Auto-resolved** |
| **Learning curve** | Steep | Steep | Medium |
| **CI/CD integration** | Good | Excellent | Growing |
| **ABI compatibility** | Per-version | Per-recipe | **Per-hash** |

**Decision**: **Zig native package manager** (build.zig.zon).

**Rationale**:
- **Manifest mode** — Same file format for all platforms (no per-OS config)
- **Hash-tracked** — Dependency hashing ensures reproducible builds across CI/CD
- **In-process** — No external tool; everything in Zig
- **Simplicity** — Fewer abstraction layers than vcpkg/Conan

**Tradeoff**: Zig package ecosystem smaller than vcpkg. Core dependencies (Jolt, DX12 headers) available; niche packages may require manual integration.

### 7. Editor Backend: VSCode Extension vs Custom Editor vs UE5-style Editor

| Aspect | VSCode Ext | Custom (Qt/Dear ImGui) | **UE5-style** |
|--------|---|---|---|
| **Development time** | Fast (web stack) | Slow (platform code) | Slow (custom rendering) |
| **Customization** | Limited (webview constraints) | Unlimited | Unlimited |
| **Distribution** | VSCode Marketplace | Custom installer | Game engine ecosystem |
| **User familiarity** | High (billion+ VSCode users) | None (custom app) | Medium (game devs) |
| **Integration with IDE** | Native (same process) | Separate window | Separate process |
| **Cross-platform** | Yes (Electron) | Conditional (Qt good) | Conditional |

**Decision**: **VSCode Extension** with HWND viewport for 3D, webview for 2D.

**Rationale**:
- **Leverage existing IDE** — Users already have VSCode open
- **Distribution free** — Marketplace handles installation
- **User familiarity** — Billion+ VSCode users, no learning curve
- **Development velocity** — TypeScript + HTML/CSS faster than C++ UI framework
- **Integration** — N-API addon runs in VSCode process (no separate process/IPC overhead)

**Tradeoff**: VSCode plugin constraints (no native UI primitives, webview sandbox). Acceptable: 2D panels (inspector, hierarchy) use webviews fine; 3D viewport is native HWND (no webview limits).

**Why not UE5-style editor?** UE5 shipped with embedded VS2019. We chose the opposite: embed UzEngine into existing editor (VSCode). More distribution-friendly.

### 8. Networking: Netcode for Entities vs Photon vs Unreal Replication Graph

| Framework | Photon | Unreal RG | **Netcode for Entities** |
|---|---|---|---|
| **License** | Proprietary (CCU-based) | Built into UE5 | **Open source (MIT)** |
| **Determinism** | Non-deterministic (float sync) | Deterministic (tick-based) | **Deterministic** |
| **Language** | C#/.NET | C++ (UE5 only) | C# / **C++ (future)** |
| **Player count** | 1–100 players (typical) | 64–256 players (scale out) | **100+ players** |
| **Bandwidth** | ~80 Kbps per player | ~10 Kbps per player | **~10 Kbps** |
| **Latency model** | Client-authoritative | Tick-based (Server Auth) | **Tick-based** |

**Decision**: **Netcode for Entities** (Phase 4+, after UzEngine core stabilizes).

**Rationale**:
- **Open source** — MIT license, no CCU fees, can fork if needed
- **Deterministic** — Aligns with Jolt Physics (bit-exact same input/output)
- **Bandwidth efficient** — Delta compression, tick-based model
- **Written in C#** — Can bind Lua layer on top (learn from UE5 approach)

**Tradeoff**: Netcode for Entities is newer, less battle-tested than Photon. Acceptable: Phase 4 gives time for it to mature. UE5 ships it by default (validates approach).

## Comparison Matrix: UzEngine vs Industry

| Technology | UE5 | Godot | Unity | **UzEngine** |
|---|---|---|---|---|
| **Build system** | Unreal Build Tool (C#) | SCons (Python) | Proprietary | **Zig** |
| **Renderer** | D3D12 + Vulkan | Vulkan-first | D3D11 + Metal | **D3D12 SM6.6** |
| **Scripting primary** | C++ / Blueprint | GDScript | C# | **C++ / Lua** |
| **Physics** | PhysX | Godot Physics + Jolt | PhysX | **Jolt** |
| **Editor** | Standalone (C++) | Standalone (C++) | Standalone (C#) | **VSCode extension** |
| **IPC model** | Named pipes | In-process | In-process | **N-API in-process** |
| **File formats** | Binary (uasset) | XML/binary mix | Binary (Unity assets) | **JSON + TOML** |
| **Networking** | Custom engine | GodotMP (community) | Mirror / Photon | **Netcode for Entities** |
| **AI-friendliness** | Black-box binaries | Readable configs | Black-box binaries | **Text-first, LLM-friendly** |

## Risk Assessment

### High Risk, High Reward Choices

1. **Zig build system** — Smaller community, but huge payoff (reproducible builds, cross-compilation)
2. **LuaJIT scripting** — Smaller ecosystem, but hot-reload changes iteration loop
3. **VSCode extension editor** — Webview sandbox limitations, but eliminates separate editor process

### Hedge Strategies

1. **Fallback rendering paths** — DX12 SM5.0 + Vulkan ensure GPU coverage
2. **C++ for perf-critical code** — Lua for game logic only, C++ for engine systems
3. **Platform-agnostic abstraction** — IPC protocol not VSCode-specific (could port to other editors)

## Future Pivots (If Needed)

If a choice proves wrong (e.g., Zig ecosystem insufficient), migration path:

1. **Zig → CMake** — CMake can invoke Zig or replace it; not a hard lock-in
2. **LuaJIT → Rust** — Script layer could switch to Rust if hot-reload becomes non-critical
3. **JSON → protobuf** — If file size becomes issue (unlikely), can add binary format
4. **VSCode → Custom editor** — Qt-based custom editor could replace VSCode integration (large effort)

**Philosophy**: Make choices based on today's best data, remain flexible if data changes.

