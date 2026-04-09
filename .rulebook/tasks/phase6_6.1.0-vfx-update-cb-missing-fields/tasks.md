## Phase 6.1.0 — Wire Missing VFX Update CB Fields

Add all simulation parameters that exist in the standalone's VFXUpdateCB but are
not yet written in the Application's frame_vfx_inline.cpp.

## 1. Point Force (Attract/Repel)
- [ ] 1.1 Add pointForceEnabled, pointForceX/Y/Z, pointForceStrength, pointForceRadius to ParticleEmitter
- [ ] 1.2 Add same fields to VfxEmitterComponent
- [ ] 1.3 Wire fields in SyncVfxEmitters (initial creation + restart copy)
- [ ] 1.4 Write 6 fields to ParticleUpdateConstants in frame_vfx_inline.cpp
- [ ] 1.5 Add ApplyPreset_Attract and ApplyPreset_Repel to emitter_presets.h
- [ ] 1.6 Add Attract/Repel to kPresetFns[] and kPresetNames[] in vfx_viewport_app.cpp
- [ ] 1.7 Add Point Force module to VfxEditorOverlay BuildModuleList

## 2. Vector Field
- [ ] 2.1 Add vectorFieldEnabled, vectorFieldStrength, vectorFieldTightness, vectorFieldScale to ParticleEmitter
- [ ] 2.2 Add same fields to VfxEmitterComponent
- [ ] 2.3 Wire in SyncVfxEmitters
- [ ] 2.4 Write 4 fields to ParticleUpdateConstants in frame_vfx_inline.cpp
- [ ] 2.5 Add ApplyPreset_VectorFlow to emitter_presets.h
- [ ] 2.6 Add VectorFlow to kPresetFns/Names
- [ ] 2.7 Add Vector Field module to VfxEditorOverlay BuildModuleList

## 3. Terrain Collision
- [ ] 3.1 Add terrainCollisionEnabled, terrainAmplitude, terrainFrequency, terrainBaseHeight to ParticleEmitter
- [ ] 3.2 Add same fields to VfxEmitterComponent
- [ ] 3.3 Wire in SyncVfxEmitters
- [ ] 3.4 Write 4 fields to ParticleUpdateConstants in frame_vfx_inline.cpp
- [ ] 3.5 Add ApplyPreset_HillyTerrain to emitter_presets.h
- [ ] 3.6 Add HillyTerrain to kPresetFns/Names
- [ ] 3.7 Add Terrain Collision module to VfxEditorOverlay BuildModuleList

## 4. Collision Spawn
- [ ] 4.1 Add collisionSpawnEnabled, collisionSpawnCount to ParticleEmitter
- [ ] 4.2 Write 2 fields to ParticleUpdateConstants in frame_vfx_inline.cpp

## 5. Velocity Inheritance
- [ ] 5.1 Add velocityInheritance to ParticleEmitter (field may already exist)
- [ ] 5.2 Wire velocityInheritance + emitterVelocity in spawn CB (frame_vfx_inline.cpp)
- [ ] 5.3 Compute emitterVelocity from frame-to-frame worldPosition delta

## 6. Emitter Seed
- [ ] 6.1 Add emitterSeed (uint32) to ParticleEmitter
- [ ] 6.2 Wire emitterSeed to spawn CB in frame_vfx_inline.cpp

## 7. Fix colorRampLutSrvIndex
- [ ] 7.1 Set colorRampLutSrvIndex = 0xFFFFFFFF (UINT32_MAX) in billboard CB to disable LUT sampling

## 8. Build & Test
- [ ] 8.1 Build runtime + demo, zero compile errors
- [ ] 8.2 Tab to Attract preset — particles converge to a point
- [ ] 8.3 Tab to Repel preset — particles explode outward from a point
- [ ] 8.4 Tab to VectorFlow preset — particles follow procedural 3D flow
- [ ] 8.5 Tab to HillyTerrain preset — particles collide with sine-wave ground

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
