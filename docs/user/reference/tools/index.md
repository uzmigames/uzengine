# Tools Reference

Build-time tools for asset processing and shader embedding.

> **Source**: `runtime/tools/`

---

| Tool | Description |
|------|-------------|
| [uzembed](uzembed.md) | Embeds `.hlsl` sources as `constexpr char[]` C++ headers for compile-time shader access |
| [uzcook](uzcook.md) | Batch-imports assets (HLSL → .uzshader), content-addressed cache, optional .uzpak archive |

---

## uzembed vs uzcook

| | uzembed | uzcook |
|---|---------|--------|
| **Purpose** | Embed shader source in binary | Compile shaders to bytecode |
| **Output** | `.gen.h` (C++ constexpr) | `.uzshader` (DXIL bytecode) |
| **Dependencies** | Standalone (no engine deps) | Engine library + DXC |
| **When to use** | Engine built-in shaders (grid, overlay, mesh) | Game/project shaders |
| **Caching** | None (regenerates every build) | Content-addressed (xxHash64, incremental) |
| **Build integration** | Runs before compilation | Runs as separate cook step |
