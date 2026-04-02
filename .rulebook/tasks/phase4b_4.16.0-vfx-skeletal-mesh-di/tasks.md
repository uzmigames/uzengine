# Phase 4b — 4.16.0 VFX Skeletal Mesh DI (Spawn from Bone Positions)

Spawn particles from bone world-space positions of a skeletal mesh component.
Bone transforms are uploaded each frame as a GPU structured buffer. The spawn
CS randomly selects a bone and emits a particle near it. Enables joint-based
effects (sparks from robot joints, embers from fire-arm bones, etc.).

---

## 1. Bone Transform GPU Buffer

- [ ] 1.1 Create `runtime/engine/vfx/modules/skeletal_spawn_module.h/cpp`
      implementing `IVFXModule`. Fields:
      ```cpp
      uint32_t          skeletalMeshEntityId = 0;  // ECS entity owning the mesh
      float             spawnRadius          = 5.f; // cm random offset from bone
      bool              perBoneWeighted      = true; // weight by bone length
      RHIBufferHandle   boneTransformBuffer  = RHIBufferHandle::Invalid();
      uint32_t          boneCount            = 0;
      ```
- [ ] 1.2 Each frame in `SkeletalSpawnModule::Update(float dt, ECSWorld& world)`:
      - Query the skeletal mesh component for this entity: retrieve `boneCount`
        and an array of `float4x4` world-space bone matrices.
      - If `boneTransformBuffer` is invalid or `boneCount` changed, reallocate:
        `stride = 64, count = boneCount, flags = STRUCTURED_BUFFER | DYNAMIC`.
      - Upload bone matrices via `IRHIDevice::UpdateDynamicBuffer()`.
- [ ] 1.3 Register SRV: `boneSrvIndex = device->RegisterStructuredBufferSRV(boneTransformBuffer)`.

## 2. Spawn Compute Shader Modification

- [ ] 2.1 In `particle_spawn_cs.hlsl`, add conditional block guarded by
      `#define USE_SKELETAL_SPAWN 1` (permutation). When enabled:
      ```hlsl
      StructuredBuffer<float4x4> g_boneTransforms : register(t9);
      cbuffer SkeletalSpawnCB : register(b5) {
          uint  boneCount;
          float spawnRadius;
          uint  useWeightedBones; // 0 or 1
          float _pad;
      };
      ```
- [ ] 2.2 In the spawn body, replace the emitter position with:
      ```hlsl
      uint boneIdx = (particleSeed * 2654435761u) % boneCount;
      float4x4 boneMat = g_boneTransforms[boneIdx];
      float3 bonePos = float3(boneMat[0][3], boneMat[1][3], boneMat[2][3]);
      float3 offset = (RandomVec3(particleSeed) * 2.0 - 1.0) * spawnRadius;
      float3 spawnPos = bonePos + offset;
      ```
      Write `spawnPos` to `g_positionsW[newIndex]`.
- [ ] 2.3 Bind `g_boneTransforms` SRV at `t9` in the spawn CS dispatch (in
      `vfx_system.cpp`'s spawn dispatch helper).

## 3. ECS Skeletal Mesh Query Interface

- [ ] 3.1 Add to `ISkeletalMeshComponent` (or equivalent interface):
      ```cpp
      /// Returns world-space bone transforms (column-major float4x4 per bone).
      /// Pointer valid until next frame. Returns nullptr if skeleton not ready.
      [[nodiscard]] virtual const float* GetBoneTransformsData() const noexcept = 0;
      [[nodiscard]] virtual uint32_t     GetBoneCount()          const noexcept = 0;
      ```
      If `ISkeletalMeshComponent` does not yet exist, create a stub with these
      two methods returning `nullptr`/`0` until the full skeletal system is
      implemented.
- [ ] 3.2 In `SkeletalSpawnModule::Update()`, call `GetBoneTransformsData()` and
      upload only if the pointer is non-null and `GetBoneCount() > 0`.

## 4. Demo Integration

- [ ] 4.1 In `vfx_viewport_demo.cpp`, create a synthetic "skeleton" of 8 bones
      arranged in a humanoid T-pose (hard-coded bone world matrices, updated each
      frame to rotate arms). No real skeletal mesh system required for the demo.
- [ ] 4.2 Create a `SyntheticSkeletonComponent` class (demo-local, not in engine)
      implementing the two query methods from §3.1 using the T-pose matrices.
- [ ] 4.3 Add preset "Skeleton Sparks": `SkeletalSpawnModule` with the synthetic
      skeleton, `spawnRadius=8.0f`, emissive yellow particles, 200 particles/s.
- [ ] 4.4 Add ImGui controls: bone index highlight (draw sphere at selected bone),
      `spawnRadius` slider (0–50 cm), toggle `perBoneWeighted`.

## 5. Serialization

- [ ] 5.1 `Serialize`: emit `{ "skeletalMeshEntityId": 0, "spawnRadius": 5.0,
      "perBoneWeighted": true }`.
- [ ] 5.2 `Deserialize`: read all three fields; entity lookup deferred to first
      `Update()` call (entity may not exist at deserialization time).

## 6. Build and Verification

- [ ] 6.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 6.2 `cd demos/vfx_viewport && zig build --clean && zig build` — no errors.
- [ ] 6.3 Capture `--screenshot-stdout --screenshot-frame 10 --frames 15` with
      "Skeleton Sparks" preset. Verify sparks originate near all 8 bone positions
      (visible as 8 distinct spawn clusters in the frame).
- [ ] 6.4 Rotate synthetic skeleton arm bones each frame and verify the spawn
      positions follow the moving bones with no 1-frame lag (same-frame upload).

## 7. Documentation

- [ ] 7.1 In `skeletal_spawn_module.h` header doc, cite:
      `/// @ref Epic 2019 "Niagara VFX System" — skeletal mesh location DI pattern`.
- [ ] 7.2 Document the `particleSeed XOR boneIdx` random selection: why modulo
      is acceptable here (boneCount is small, bias negligible).
- [ ] 7.3 Document stub interface expectations for future real skeletal mesh integration.

## 8. Future-Proofing Notes (not tasks — informational)

The `ISkeletalMeshComponent` stub in §3.1 acts as a seam. When the full
skeletal mesh system ships, `SkeletalSpawnModule` requires no changes — only
the concrete implementation of `GetBoneTransformsData()` needs to be provided.
