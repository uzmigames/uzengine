## 1. Decal Component

- [x] 1.1 Create `runtime/engine/vfx/decals/decal_component.h` — DecalComponent ECS component: position (Vec3), rotation (Quat), half_extents (Vec3, default {1,1,0.5}), material (ResourceHandle<Material>), normal_intensity (float 0-1, default 0.8), roughness_override (float, -1=no override), lifetime (float, seconds), age (float), fade_curve (VFXCurve*), blend_mode (enum: Opaque/Additive/Multiply/Screen), sort_order (int, higher=on top), is_active (bool).
- [x] 1.2 Create `runtime/engine/vfx/decals/decal_component.cpp` — DecalComponent::GetProjectionMatrix() returns world-to-decal-local Mat4. OnTick(dt) advances age, computes fade alpha from fade_curve at age/lifetime. FadeOut(duration) sets lifetime=age+duration. Destroy() marks for removal.

## 2. Decal Rendering

- [x] 2.1 Create `runtime/shaders/vfx/decal_deferred_vs.hlsl` — Vertex shader: transforms decal box mesh (unit cube) into world space using decal transform. Outputs world position for pixel shader.
- [x] 2.2 Create `runtime/shaders/vfx/decal_deferred_ps.hlsl` — Pixel shader: (a) reconstruct world position from depth buffer, (b) transform to decal local space via world_to_decal matrix, (c) discard if outside box (abs(local) > half_extents), (d) discard if backfacing (dot(surface_normal, decal_forward) < 0.1), (e) compute UV from local XY, (f) sample decal albedo/normal/roughness textures, (g) blend with G-Buffer based on blend_mode, (h) apply fade_alpha. Outputs modified albedo, normal, roughness.
- [x] 2.3 Create decal box mesh — Unit cube mesh (8 vertices, 12 triangles) used for all decal rendering. Single static mesh, instanced per decal.

## 3. Render Pass

- [x] 3.1 Create `runtime/engine/vfx/passes/decal_pass.h/.cpp` — DecalPass: render graph pass scheduled after G-Buffer, before lighting. Reads G-Buffer textures (albedo, normal, roughness, depth) as SRV. Writes modified G-Buffer (albedo, normal, roughness) as RTV. For each active decal: bind decal transform CB, bind decal textures, draw box mesh. Sort decals by sort_order, then by distance.
- [x] 3.2 Create decal PSO — Depth test: read-only (test against scene depth, no write). Stencil: optional stencil mask for selective decal application. Blend: depends on mode — Opaque uses replace, Additive uses add, Multiply uses blend factor. Backface cull: front-face culling (render inside of box).

## 4. Decal Pool

- [x] 4.1 Create `runtime/engine/vfx/decals/decal_pool.h/.cpp` — DecalPool: pre-allocates pool_size DecalComponents (default 1000). AcquireDecal(asset) → DecalComponent* (returns available or nullptr). ReturnDecal(ptr) resets and returns to pool. Active/available lists. Clear() destroys all. Stats: in_use_count, available_count.

## 5. Decal Atlas

- [x] 5.1 Create `runtime/engine/vfx/decals/decal_atlas.h/.cpp` — DecalAtlas: builds a 2048×2048 texture atlas from individual decal textures. AddDecal(texture_path) → AtlasEntry (uv_min, uv_max). Uses simple shelf-packing algorithm. GetAtlasTexture() → RHITextureHandle. Supports albedo atlas, normal atlas, roughness atlas (3 separate atlases).

## 6. Decal Manager

- [x] 6.1 Create `runtime/engine/vfx/decals/decal_manager.h/.cpp` — DecalManager: owns DecalPool and DecalAtlas. SpawnDecal(asset, position, normal, lifetime) creates placed decal facing surface normal. Update(dt) ticks all active decals (age, fade). Cleanup() removes expired decals and returns to pool. RenderDecals(renderer) schedules decal pass. FrustumCull(camera) marks out-of-view decals as skip-render.

## 7. Decal Asset

- [x] 7.1 Create `runtime/engine/vfx/decals/decal_asset.h/.cpp` — DecalAsset: loaded from .uzdecal JSON. Fields: material path, blend_mode, lifetime, fade_curve path, normal_intensity, sort_order. Static Load(path) factory.

## 8. Testing

- [x] 8.1 Unit tests for DecalComponent — projection matrix correctness, fade calculation, blend mode
- [x] 8.2 Unit tests for DecalPool — acquire/return cycle, pool exhaustion, clear
- [x] 8.3 Unit tests for DecalAtlas — add entries, UV bounds correctness, overflow handling
- [x] 8.4 Unit tests for DecalManager — spawn/update/cleanup lifecycle, frustum cull
- [x] 8.5 Unit tests for DecalAsset — load valid/invalid .uzdecal JSON

## 9. Demo

- [x] 9.1 Create `demos/decals/` — FPS-style demo: click to place bullet hole decals on walls. Random rotation per decal. Fade over 10s. Show 100+ simultaneous decals.
- [x] 9.2 Add blend mode showcase — 4 walls with Opaque, Additive, Multiply, Screen decals side by side
- [x] 9.3 Add performance overlay — active decal count, pool stats, decal pass time (ms)

## 10. Documentation

- [x] 10.1 Document .uzdecal JSON format specification
- [x] 10.2 Document deferred decal rendering pipeline
- [x] 10.3 Document decal pool/atlas usage guide
