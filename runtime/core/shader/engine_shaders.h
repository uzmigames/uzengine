/// UzEngine Built-in Shader Registry
///
/// Thin wrapper that includes the auto-generated `engine_shaders.gen.h`
/// produced by `uzembed` at build time. That generated header contains:
/// - `#include` for every individual `.hlsl.gen.h` shader source header
/// - `enum class EngineShader` with one entry per shader
/// - `struct EngineShaders` with `GetSource()` and `GetName()` accessors
///
/// # Usage
/// ```cpp
/// #include "engine_shaders.h"
///
/// auto bytecode = pipeline.LoadBytecodeFromSource(
///     EngineShaders::GetName(EngineShader::OverlayVS),
///     EngineShaders::GetSource(EngineShader::OverlayVS));
/// ```
///
/// # Adding New Engine Shaders
/// Just add a `.hlsl` file to `runtime/shaders/editor/` and rebuild.
/// `uzembed` automatically discovers it and regenerates the enum + registry.

#pragma once

#include "editor/engine_shaders.gen.h"
