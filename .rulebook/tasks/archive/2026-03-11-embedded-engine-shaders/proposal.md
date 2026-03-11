# Proposal: embedded-engine-shaders

## Why

Runtime engine shaders (editor grid, overlay text, gizmos) are loaded from disk via fragile relative paths like `../../../../shaders/editor`. This breaks when the exe is deployed elsewhere, when demos run standalone, and raw `.hlsl` source files must NEVER ship in a game build. Godot embeds engine shaders as generated C++ headers at build time — we need the same.

## What Changes

1. **New tool**: `shader_embed.cpp` — single-file C++ tool (no deps) that converts `.hlsl` → `.hlsl.gen.h` headers containing `constexpr char[]` with the shader source
2. **New API**: `ShaderPipeline::LoadBytecodeFromSource(name, hlslSource, defines)` — compiles from in-memory source string instead of file
3. **New registry**: `EngineShaders` static class — provides `GetSource(EngineShader::OverlayVS)` etc.
4. **Build integration**: Zig `build.zig` runs `shader_embed` as build step; CMake uses `add_custom_command`
5. **Migration**: All `runtime/shaders/` converted to embedded; all `Mount("/editor", ...)` removed from demos

### Generated header format
```cpp
// overlay_vs.hlsl.gen.h — AUTO-GENERATED, DO NOT EDIT
#pragma once
namespace uz::generated {
constexpr char kOverlayVS[] = R"hlsl(
// ... original HLSL source ...
)hlsl";
} // namespace uz::generated
```

## Impact
- Affected specs: shader-pipeline (1.5.0)
- Affected code: `runtime/shaders/`, `ShaderPipeline`, all demos, `editor/font/`, `editor/grid/`, `editor/stats/`
- Breaking change: NO (external API unchanged, internal loading path changes)
- User benefit: Demos work without source tree; no fragile relative paths; ship-ready shader packaging
