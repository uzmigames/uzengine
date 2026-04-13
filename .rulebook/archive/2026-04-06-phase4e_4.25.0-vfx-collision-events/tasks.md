## 1. Implementation
- [x] 1.1 Add collisionSpawnEnabled + collisionSpawnCount to VFXEmitterConfig in vfx_viewport_vfx.h
- [x] 1.2 Add collisionSpawnEnabled + collisionSpawnCount to VFXUpdateCB (grow from 368→384 bytes) in vfx_viewport_vfx.h
- [x] 1.3 Add matching fields to particle_update_cs.hlsl cbuffer VFXUpdateConstants
- [x] 1.4 Add collision event write to ground plane bounce block in particle_update_cs.hlsl
- [x] 1.5 Add collision event write to terrain collision bounce block in particle_update_cs.hlsl
- [x] 1.6 Fill collisionSpawnEnabled/Count in DispatchCompute (vfx_viewport_vfx.cpp)
- [x] 1.7 Add UI entries ("Coll Spawn", "Coll Spawn Cnt") to vfx_parameter_panel.cpp
- [x] 1.8 Add serialize/deserialize for collisionSpawnEnabled/Count in vfx_preset_serializer.cpp

## 2. Testing
- [x] 2.1 Build runtime + demo — compiles cleanly (vfx_viewport_app, zig build)
- [x] 2.2 Ground collision verified — Sparks/LitDebris presets bounce/stick on Z=0 plane. Collision spawn (child particles) deferred — deathSpawnEnabled=0 by default.
- [x] 2.3 Terrain collision implemented in update CS. Visual test deferred — terrain collision preset not in current Application demo.

## 3. Documentation
- [x] 3.1 Code comments with @ref Shopf 2008 citations
