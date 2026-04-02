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
- [ ] 2.1 Build runtime + demo, verify no compile errors
- [ ] 2.2 Enable ground collision + collision spawn, verify child particles spawn at impact
- [ ] 2.3 Enable terrain collision + collision spawn, verify child particles spawn at terrain hits

## 3. Documentation
- [x] 3.1 Code comments with @ref Shopf 2008 citations
