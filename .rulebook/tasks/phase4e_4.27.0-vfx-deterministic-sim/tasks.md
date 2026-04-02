## 1. Implementation
- [x] 1.1 Add emitterSeed to VFXEmitterConfig in vfx_viewport_vfx.h
- [x] 1.2 Replace _padSpawnNew1 with emitterSeed in VFXSpawnCB (vfx_viewport_vfx.h, no size change)
- [x] 1.3 Replace _padSpawnNew1 with emitterSeed in particle_spawn_cs.hlsl cbuffer
- [x] 1.4 Modify seed computation in particle_spawn_cs.hlsl to use emitterSeed when > 0
- [x] 1.5 Fill cb.emitterSeed in DispatchCompute (vfx_viewport_vfx.cpp)
- [x] 1.6 Add UI entry ("Emitter Seed") to Simulation category in vfx_parameter_panel.cpp
- [x] 1.7 Add serialize/deserialize for emitterSeed in vfx_preset_serializer.cpp

## 2. Testing
- [ ] 2.1 Build runtime + demo, verify no compile errors
- [ ] 2.2 Set emitterSeed=42, fire burst twice, verify identical spawn patterns
- [ ] 2.3 Set emitterSeed=0, fire burst twice, verify different patterns (non-deterministic)

## 3. Documentation
- [x] 3.1 Code comments with @ref Salmon 2011 citations
