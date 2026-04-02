## 1. Implementation
- [x] 1.1 Add warmupFrames + warmupDone to VFXEmitterInstance in vfx_viewport_demo.h
- [x] 1.2 Add warmup simulation loop in OnFrame (vfx_viewport_demo.cpp) before normal emitter dispatch
- [x] 1.3 Set warmupFrames=30 on Fire compound preset flame emitter in BuildCompoundPreset
- [x] 1.4 Log warmup completion via UZ_LOG

## 2. Testing
- [ ] 2.1 Build runtime + demo, verify no compile errors
- [ ] 2.2 Switch to Fire preset, verify particles are pre-filled on first visible frame
- [ ] 2.3 Verify Single preset (warmupFrames=0) still works normally without pre-fill

## 3. Documentation
- [x] 3.1 Code comments with @ref Latta 2004 citations
