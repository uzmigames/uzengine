# Changelog — extension

## [0.2.0] — 2026-03-10

### Added
- VSCode extension skeleton with activation entry point
- Native addon loading (`uzengine.node` from `native/`)
- ESLint 9 flat config with typescript-eslint 8 (errors only, no warnings)
- Prettier 3.8 config
- Vitest 4 config with 95% coverage threshold
- LogOutputChannel logging (replaces console.log)
- Syntax highlighting for HLSL (`.hlsl`, `.hlsli`, `.fx`, `.fxh`)
- Syntax highlighting for Zig (`.zig`)
- Language registration for UzEngine formats (`.uzscene`, `.uzmaterial`, `.uzmesh`, `.uzprefab`, `.uzproject`)
- Markdown fenced code block injection for `hlsl` and `zig`
- C/C++ syntax injection for UzEngine types, macros, and interfaces:
  - Assert macros (`UZ_CHECK`, `UZ_ENSURE`, `UZ_VERIFY`)
  - Logging (`UZ_LOG`, `UZ_DECLARE_LOG_CATEGORY`, `UZ_DEFINE_LOG_CATEGORY`)
  - Profiling (`UZ_PROFILE_FUNCTION`, `UZ_PROFILE_SCOPE`, `UZ_PROFILE_FRAME_MARK`)
  - Stat counters (`UZ_DECLARE_STAT`, `UZ_DEFINE_STAT`, `UZ_STAT_INCREMENT`)
  - RHI interfaces (`IRHIDevice`, `IRHICommandList`, `IRHISwapChain`, `IRHIFence`)
  - RHI types and enums (`RHIFormat`, `RHIBackend`, `RHIHandle`, `ShaderModel`, etc.)
  - Shader pipeline (`ShaderVFS`, `ShaderCache`, `ShaderCompiler`, `ShaderPipeline`, etc.)
  - Math types (`Vec2`, `Vec3`, `Vec4`, `Mat4`, `Quat`, `Transform`, `AABB`, `Frustum`)
  - Containers (`RingBuffer`, `FreeList`, `HandleMap`, `FrameArray`, `ResourcePool`)
  - Memory (`Own`, `Ref`, `RefMut`, `LinearAllocator`, `PoolAllocator`, `FrameAllocator`)
  - DX12 backend (`DX12Device`, `DX12SwapChain`, `DX12Fence`, `DescriptorAllocator`)
  - Platform (`PlatformWindow`, `InputSystem`, `WindowDesc`)
- Extension marketplace icon (`icon.png`)
- File type icons (SVG) for HLSL, Zig, and all UzEngine formats
- `open-vscode.bat` launcher with `--extensionDevelopmentPath` and `--remote-debugging-port=9229`
