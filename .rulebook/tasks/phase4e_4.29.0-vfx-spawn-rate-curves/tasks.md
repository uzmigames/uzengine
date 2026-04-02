## 1. Implementation
- [x] 1.1 Add `spawnRateRampUpTime` and `spawnRateRampDownTime` to `VFXEmitterConfig` (vfx_viewport_vfx.h). Both default 0 = instant (constant rate).
- [x] 1.2 Implement rateMultiplier computation in both the variable-dt and fixed-timestep dispatch paths (vfx_viewport_demo.cpp): linear ramp-up when emitterAge < rampUpTime, linear ramp-down when timeToEnd < rampDownTime.
- [x] 1.3 Add both fields to preset serializer Save/Load (vfx_preset_serializer.cpp).
- [x] 1.4 Add "Ramp Up (s)" and "Ramp Down (s)" sliders to "Simulation" category in vfx_parameter_panel.cpp.

## 2. Testing
- [x] 2.1 Demo builds successfully (zig build exit 0).

## 3. Documentation
- [x] 3.1 Fields documented with @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" citation.
