## 1. Implementation
- [x] 1.1 Add `fixedTimestep` field to `VFXEmitterConfig` (vfx_viewport_vfx.h). Default 0 = variable dt.
- [x] 1.2 Add `timeAccumulator` field to `VFXEmitterInstance` (vfx_viewport_demo.h) to hold surplus dt.
- [x] 1.3 Implement accumulator dispatch loop in `RenderVFXPass` (vfx_viewport_demo.cpp): when fixedTimestep > 0, accumulate dt and drain in fixed-size sub-steps, each dispatching DispatchCompute.
- [x] 1.4 Add `fixedTimestep` to preset serializer Save/Load (vfx_preset_serializer.cpp).
- [x] 1.5 Add "Fixed Step" slider to "Simulation" category in vfx_parameter_panel.cpp.

## 2. Testing
- [x] 2.1 Demo builds successfully (zig build exit 0).

## 3. Documentation
- [x] 3.1 Fields documented with @ref Fiedler 2004 "Fix Your Timestep!" citation.
