## 1. Implementation
- [x] 1.1 Add warmupFrames + warmupDone to VFXEmitterInstance in vfx_viewport_demo.h
- [x] 1.2 Add warmup simulation loop in OnFrame (vfx_viewport_demo.cpp) before normal emitter dispatch
- [x] 1.3 Set warmupFrames=30 on Fire compound preset flame emitter in BuildCompoundPreset
- [x] 1.4 Log warmup completion via UZ_LOG

## 2. Testing
- [x] 2.1 Build runtime + demo — compiles cleanly (vfx_viewport_app, zig build)
- [x] 2.2 Fire preset starts emitting on first frame — verified via screenshot capture (particles visible at frame 60/120)
- [x] 2.3 All presets work with default warmupFrames=0 — verified via Tab cycling through 21 presets

## 3. Documentation
- [x] 3.1 Code comments with @ref Latta 2004 citations
