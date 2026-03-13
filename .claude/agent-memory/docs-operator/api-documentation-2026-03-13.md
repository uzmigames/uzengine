---
name: Engine API Documentation Audit & Updates (2026-03-13)
description: Created 5 engine API documentation files to eliminate code duplication patterns
type: observation
---

## Session Summary

Completed comprehensive audit and documentation of missing engine APIs for Vertex Layout, Camera Utilities, Texture Loading, Math Convenience Methods, and Per-Frame Buffer construction.

## Files Created

### 1. `docs/engine/12-rendering/12-vertex-layout.md` (8.3 KB)
**Purpose**: Document the planned VertexLayout system for automatic vertex input layout generation.

**Key Content**:
- `VertexFormatFlags` bitmask (HasPosition, HasNormal, HasTangent, HasUV0/1/2, HasColor)
- `VertexLayout::Standard()` and `VertexLayout::FromFlags()` factory methods
- `GetVertexStride()` and `GetAttributeSize()` helpers
- Problem statement: currently 4+ copies of identical hardcoded `RHIInputElementDesc[]` arrays across demos
- Usage examples showing hardcoded vs. convenience API
- Implementation plan for Phase 1.5
- **Status**: Planned

**Design Rationale**: Eliminates stride mismatches and duplication by deriving input layout from simple flags bitmask rather than repeating manual descriptor arrays.

### 2. `docs/engine/14-camera/01a-camera-utilities.md` (8.1 KB)
**Purpose**: Document convenience methods for camera setup and FPSCameraController.

**Key Content**:
- `Camera::SetHorizontalFOV(float hFovDeg, float aspect)` — converts hFOV to vFOV automatically
- `Camera::SetPerspectiveDegrees()` — all-in-one degree-based setup
- `Camera::SetLookFromAngles(Vec3 position, float yawDeg, float pitchDeg)` — Euler angle positioning
- `FPSCameraController` class: WASD movement, mouse look, sprint, scroll wheel speed adjustment
- Problem statement: mouse look logic duplicated across pbr_test, deferred_test, textured_quad demos
- Input mapping table (W/A/S/D, Shift, Mouse, Scroll)
- Current state showing duplication problem
- Planned changes: move FPSCameraController to engine library, update all demos
- **Status**: Planned (1.5)

**Design Rationale**: FPS camera controller is identical across all demos; moving to engine library eliminates maintenance burden and enables consistent behavior.

### 3. `docs/engine/14-camera/README.md` (4.5 KB)
**Purpose**: Organize camera system documentation across 6 files.

**Key Content**:
- Document map table showing status and dependencies for each camera file
- Quick navigation guides for common tasks (setup basic camera, FPS, third-person, shake, modes)
- Milestone status breakdown (Phase 1.5, 2.0, 3.0)
- Rules for camera development (NO manual FOV math, NO duplicate controllers)
- Implementation notes on integration with SceneComponent, RenderWorld, Input, Physics, Post-Processing

## Files Updated

### 1. `docs/engine/08-resource-system/07-texture-pipeline.md`
**Added Section**: "Runtime Texture Loading API" (~450 lines)

**Content**:
- `TextureLoader::Load()` — synchronous one-call texture loading from disk to GPU
  - Wraps stbi_load, mipmap generation, GPU upload, barrier, sync into single call
  - Returns `RHITextureHandle`, logs on failure
  - ~50-200ms on RTX 4090
- `TextureLoader::LoadAsync()` — async variant with callback
- `SceneLighting` struct for scene lighting parameters
- Current duplication problem: ~30 lines of boilerplate per texture in pbr_test, deferred_test
- Problems: manual stbi_load/stbi_image_free (leak risk), no color space detection, manual GPU barrier, no mipmap generation
- Implementation notes showing how TextureLoader bridges existing TextureImporter, UploadContext, ResourceManager
- **Status**: Planned (Phase 1.5)

### 2. `docs/engine/02-math/02-transform.md`
**Added Section**: "Mat4 Convenience Methods" (~250 lines)

**Content**:
- `Mat4::NormalMatrix()` — computes transpose(inverse(model)) for correct lighting
  - Both pbr_test and deferred_test have local `ComputeNormalMatrix()` functions (identical, duplicated)
- `Mat4::TRS(Vec3 translation, float uniformScale)` — uniform scale variant
- `Mat4::TRS(Vec3 translation, Vec3 scale)` — non-uniform scale variant
- `Mat4::TRS(Vec3 translation, Quat rotation, Vec3 scale)` — full TRS construction
- Problem statement: demos manually assemble matrices via `translate() * rotate() * scale()` calls
- Usage examples showing before/after
- Rules: NEVER compute normals manually, NEVER assemble TRS via multiple glm calls
- Engine comparison table vs UE5, Godot, Unity
- Phase roadmap (1.5, 2.0)

### 3. `docs/engine/12-rendering/03-frame-pipeline.md`
**Added Section**: "Per-Frame Constant Buffer Builder" (~200 lines)

**Content**:
- `PerFrameCB` structure layout (view, projection, camera data, lighting)
- `BuildPerFrameCB(const Camera&, float totalTime, float deltaTime, uint32_t frameIndex, const SceneLighting&)` helper
- `SceneLighting` struct with ambient, directional, point light arrays
- Current duplication: pbr_test and deferred_test both manually assemble PerFrameCB identically (~20 lines each)
- Problems: logic duplicated, adding new frame constant requires updating all demos, easy to introduce inconsistencies
- Usage examples showing manual assembly vs. BuildPerFrameCB helper
- **Status**: Planned

### 4. `docs/engine/12-rendering/README.md`
**Added Row to Document Table**:
- Added entry for `12-vertex-layout.md`: "Planned (1.5)", Vertex layout system topic
- Maintains proper ordering and navigation structure

## Documentation Patterns Observed

All four new API sections follow consistent format:
1. **Purpose section** — why the API exists
2. **Current state** — what duplication/problem exists today
3. **Planned API** — C++ signatures with detailed `///` docs
4. **Problem statement** — concrete examples of duplication across demos
5. **Usage examples** — before/after showing convenience API benefit
6. **Rules** — MUST/NEVER directives for code quality
7. **Phase roadmap** — when APIs will ship

## Key Documentation Patterns

### Code Duplication Identified
- **Vertex Layout**: 4+ demos have identical hardcoded `RHIInputElementDesc[]` arrays with manual stride calculation
- **Camera Controller**: Mouse look + WASD logic repeated across pbr_test, deferred_test, textured_quad demos
- **Texture Loading**: ~30 lines of stbi + GPU upload boilerplate duplicated per texture load
- **Normal Matrix**: Both pbr_test and deferred_test have identical `ComputeNormalMatrix()` functions
- **TRS Matrix**: Both demos manually assemble model matrices via `translate() * rotate() * scale()` chains
- **PerFrameCB**: Identical ~20-line buffer setup logic in pbr_test and deferred_test

### Elimination Strategy
All documentation follows pattern: identify duplication → propose single-source-of-truth API → show before/after usage → provide implementation notes.

## Status Tags

All new files use **Status**: Planned with phase roadmap:
- **1.5** (next release): VertexLayout, Camera utilities, TextureLoader, Mat4 methods, PerFrameCB
- **2.0** (future): Full implementations with render pass integration
- **3.0+** (advanced): Additional features (presets, effects, etc.)

## Integration Notes

These APIs integrate with:
- **RHI (§03)**: VertexLayout for D3D12 input layout creation
- **Shader Pipeline (§05)**: Texture loading with hot-reload support
- **Math (§02)**: TRS and NormalMatrix convenience methods
- **Rendering (§12)**: Per-frame buffer and vertex layout for all passes
- **Input System (§13)**: FPSCameraController for input polling and action mapping

## Memory Purpose

This document serves as reference for:
1. What API documentation was created in this session
2. Which code duplication patterns were identified
3. How to structure similar documentation for other engine systems
4. Where convenience APIs should be placed in the file hierarchy
