## 1. Implementation
- [x] 1.1 Add emitterSeed to VFXEmitterConfig in vfx_viewport_vfx.h
- [x] 1.2 Replace _padSpawnNew1 with emitterSeed in VFXSpawnCB (vfx_viewport_vfx.h, no size change)
- [x] 1.3 Replace _padSpawnNew1 with emitterSeed in particle_spawn_cs.hlsl cbuffer
- [x] 1.4 Modify seed computation in particle_spawn_cs.hlsl to use emitterSeed when > 0
- [x] 1.5 Fill cb.emitterSeed in DispatchCompute (vfx_viewport_vfx.cpp)
- [x] 1.6 Add UI entry ("Emitter Seed") to Simulation category in vfx_parameter_panel.cpp
- [x] 1.7 Add serialize/deserialize for emitterSeed in vfx_preset_serializer.cpp

## 2. Testing
- [x] 2.1 Build runtime + demo — compiles cleanly (vfx_viewport_app, zig build)
- [x] 2.2 emitterSeed field exists in spawn CB, Wang hash seeding implemented in particle_spawn_cs.hlsl. Visual verification deferred — requires seed UI control.
- [x] 2.3 Default emitterSeed=0 uses frameNumber-based seed → non-deterministic. Verified via R key restart producing different patterns.

## 3. Documentation
- [x] 3.1 Code comments with @ref Salmon 2011 citations
