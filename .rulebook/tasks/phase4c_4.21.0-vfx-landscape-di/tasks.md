# Phase 4c — 4.21.0 VFX Landscape DI (Terrain Height Queries)

Terrain-aware particles: the update CS samples a 2D heightmap texture at each
particle's XY position to find the ground Z, enabling terrain-surface
collision, terrain-surface spawning, and particles that flow over hills instead
of clipping through them.

---

## 1. Heightmap GPU Texture

- [ ] 1.1 Create `runtime/engine/vfx/modules/landscape_di_module.h/cpp`.
      Fields:
      ```cpp
      RHITextureHandle heightmapTexture  = RHITextureHandle::Invalid();
      int32_t          heightmapSrvIndex = -1;  // bindless SRV slot
      Vec2             terrainOriginXY   = Vec2(0.f, 0.f);   // cm
      Vec2             terrainExtentXY   = Vec2(2048.f, 2048.f); // cm
      float            terrainMinZ       = 0.f;    // cm height at texel=0
      float            terrainMaxZ       = 512.f;  // cm height at texel=1
      bool             collideWithTerrain = true;
      bool             spawnOnSurface     = false;
      float            collisionRestitution = 0.3f; // bounce factor 0..1
      ```
- [ ] 1.2 Implement `LandscapeDIModule::LoadHeightmap(std::string_view path,
      IRHIDevice* device)`:
      - Accept 16-bit grayscale PNG or a raw `.r16` binary file.
      - Upload as `DXGI_FORMAT_R16_UNORM` 2D texture via staging buffer.
      - Register SRV: `heightmapSrvIndex = device->RegisterTexture2DSRV(tex)`.
- [ ] 1.3 Implement `LandscapeDIModule::GenerateProceduralHeightmap(uint32_t size,
      float amplitude, IRHIDevice* device)`:
      - Generate `size × size` heightmap using summed octaves of 2D value noise
        (4 octaves, lacunarity=2.0, gain=0.5).
      - Upload as `R16_UNORM`.
      - Default: `size=128`, `amplitude=200.0 cm`.

## 2. Update CS — Terrain Height Sampling

- [ ] 2.1 In `particle_update_cs.hlsl`, add (guarded by `#define USE_LANDSCAPE_DI 1`):
      ```hlsl
      Texture2D<float>  g_heightmap    : register(t11);
      SamplerState      g_hmSampler    : register(s4); // bilinear clamp
      cbuffer LandscapeCB : register(b6) {
          float2 terrainOriginXY;
          float2 terrainExtentXY;
          float  terrainMinZ;
          float  terrainMaxZ;
          uint   collideWithTerrain;
          uint   spawnOnSurface; // unused in update CS — used in spawn CS only
          float  collisionRestitution;
          float3 _pad;
      };
      ```
- [ ] 2.2 In the update thread body, after all velocity forces, sample terrain:
      ```hlsl
      #if USE_LANDSCAPE_DI
      if (collideWithTerrain) {
          float2 uvw = (pos.xy - terrainOriginXY) / terrainExtentXY;
          float groundZ = terrainMinZ + g_heightmap.SampleLevel(g_hmSampler, uvw, 0)
                          * (terrainMaxZ - terrainMinZ);
          if (pos.z < groundZ) {
              pos.z = groundZ;
              vel.z = abs(vel.z) * collisionRestitution; // bounce
              vel.xy *= 0.85; // friction
          }
      }
      #endif
      ```
      Write corrected `pos` and `vel` to the write SoA buffers.
- [ ] 2.3 Bind `g_heightmap` SRV at `t11` in the VFX system update dispatch helper.

## 3. Spawn CS — Surface Spawn

- [ ] 3.1 In `particle_spawn_cs.hlsl`, add (guarded by `#define USE_SURFACE_SPAWN 1`):
      ```hlsl
      Texture2D<float>  g_heightmap : register(t11);
      SamplerState      g_hmSampler : register(s4);
      ```
      When `spawnOnSurface == 1`, after computing a random XY within the emitter
      radius, sample the heightmap to get the ground Z and use it as the spawn Z:
      ```hlsl
      if (spawnOnSurface) {
          float2 uv = (spawnXY - terrainOriginXY) / terrainExtentXY;
          float gh  = terrainMinZ
                      + g_heightmap.SampleLevel(g_hmSampler, uv, 0)
                        * (terrainMaxZ - terrainMinZ);
          spawnPos.z = gh;
      }
      ```
- [ ] 3.2 Bind `LandscapeCB` at `b6` in both the spawn and update dispatch calls.

## 4. Serialization

- [ ] 4.1 `Serialize`: emit `{ "heightmapPath": "content/terrain/hm.r16",
      "terrainOriginXY": [0,0], "terrainExtentXY": [2048,2048],
      "terrainMinZ": 0, "terrainMaxZ": 512, "collideWithTerrain": true,
      "spawnOnSurface": false, "collisionRestitution": 0.3 }`.
- [ ] 4.2 `Deserialize`: call `LoadHeightmap(heightmapPath)` if path is non-empty,
      otherwise `GenerateProceduralHeightmap(128, 200.0)`.

## 5. Demo Integration

- [ ] 5.1 In `vfx_viewport_demo.cpp`, generate a procedural 128×128 heightmap
      at startup: gentle hills, `terrainMinZ=0`, `terrainMaxZ=200 cm`.
      Draw the terrain as a grid mesh using the heightmap data (flat-shaded quads).
- [ ] 5.2 Add preset "Terrain Flow": particles spawned on terrain surface
      (`spawnOnSurface=true`), gravity=−980 cm/s², small restitution=0.2,
      friction. Particles should roll downhill and settle at valley bottoms.
- [ ] 5.3 Add preset "Rain on Hills": downward velocity (−500 cm/s), very short
      lifetime (0.4s), `collideWithTerrain=true`, `restitution=0.1`.
      Verify particles stop at terrain surface, not below it.
- [ ] 5.4 ImGui controls: `collideWithTerrain` checkbox, `spawnOnSurface` checkbox,
      `collisionRestitution` slider (0–1), `terrainMaxZ` slider (0–1000 cm).

## 6. Build and Verification

- [ ] 6.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 6.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 6.3 Capture "Terrain Flow" at frame 40: particles should be resting on
      hill surfaces, not floating or clipping below the mesh.
- [ ] 6.4 Capture "Rain on Hills" at frame 20: falling particles must stop at
      the terrain height, not at Z=0.
- [ ] 6.5 Test UV edge cases: particle at terrain boundary (UV outside [0,1])
      must receive `groundZ = terrainMinZ` (sampler clamp mode).

## 7. Documentation

- [ ] 7.1 In `landscape_di_module.h`:
      ```cpp
      /// @ref Génevaux et al. 2013 "Terrain Generation Using Procedural Models
      ///      Based on Hydrology" — terrain-aware simulation reference
      ```
- [ ] 7.2 Document `collisionRestitution`: 0.0 = fully inelastic (particles stick),
      1.0 = perfectly elastic (no energy loss on bounce).
- [ ] 7.3 Document `.r16` file format: `width × height` row-major uint16 values,
      big-endian, value range 0–65535 linearly mapped to `[terrainMinZ, terrainMaxZ]`.
