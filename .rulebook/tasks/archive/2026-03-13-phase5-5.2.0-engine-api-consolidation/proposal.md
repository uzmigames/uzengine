# Proposal: Engine API Consolidation — Eliminate Demo Boilerplate

## Why

Current demo applications (`pbr_test`, `deferred_test`, `textured_quad`) contain **~90% identical boilerplate code**:

- Window creation, device initialization, swapchain setup (100+ lines)
- Fence management and frame synchronization (80+ lines)
- Shader compilation bootstrap and PSO creation (120+ lines)
- Texture loading pipeline (100+ lines)
- Mesh uploading to GPU (80+ lines)
- Camera setup and math helpers (60+ lines)
- Per-frame constant buffer construction (80+ lines)
- GBuffer (if deferred) creation and lifecycle (60+ lines)

This creates **friction for new demos**: adding a new rendering test requires duplicating 600+ lines of non-demo code, which is error-prone and makes the codebase harder to test.

## What

Extract common patterns into **reusable engine APIs** across 10 layers:

1. **Math & Utility Foundations**: Mat4 TRS constructors, normal matrix helper
2. **Vertex Layout System**: Dynamic vertex format builders
3. **Camera Convenience Methods**: Automatic FOV conversion, look-from-angles
4. **TextureLoader**: One-call texture load + upload
5. **GPUMesh Uploader**: Mesh-to-GPU with SM6.6/SM5 fallback
6. **GBuffer Class**: Extracted lifecycle management
7. **ShaderManager**: Auto-detects bindless, manages compilation and caching
8. **PerFrameCB Builder**: Scene lighting + camera transforms in one call
9. **PipelineLibrary**: Caches standard PSOs (ForwardPBR, GBuffer, Lighting, GBufferDebug)
10. **DemoApplication Base Class**: Encapsulates window/device/frame sync, with virtual hooks

## Impact

**Before**: New demo = 800+ lines of boilerplate + 100 lines of demo logic
**After**: New demo = **50–100 lines of actual demo logic**, inherits DemoApplication

Benefits:
- **Faster iteration**: New rendering experiments take hours, not days
- **Less error-prone**: Boilerplate tested once in engine, not per-demo
- **Easier AI coding**: Smaller, focused code paths easier to understand and modify
- **Better maintainability**: Bug fix in texture loading fixes all demos automatically
- **Clearer demos**: Code expresses *what we're testing*, not *how to initialize D3D12*

## Success Criteria

1. All 10 layers implemented with complete unit tests (95%+ coverage)
2. Three existing demos migrated: textured_quad, pbr_test, deferred_test
3. Each migrated demo < 150 lines of actual demo logic (verified line count check)
4. All demos produce **visually identical** output before/after migration
5. All code follows UzEngine SOLID principles and clean code standards
6. Documentation updated: docs/engine/12-rendering/12-vertex-layout.md, camera utilities, demo guide
7. Zero compilation warnings, all tests passing, code review approved
