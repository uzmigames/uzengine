## 1. Implementation
- [x] 1.1 Add `localSpaceEnabled` flag to `VFXEmitterConfig` (vfx_viewport_vfx.h). Default 0 = world space.
- [x] 1.2 In `RenderVFXPass` dispatch loop (vfx_viewport_demo.cpp): when localSpaceEnabled, force `velocityInheritance = 1.0f` each frame so particles co-move with the emitter — CPU-only, no shader changes.
- [x] 1.3 Add `localSpaceEnabled` to preset serializer Save/Load (vfx_preset_serializer.cpp).
- [x] 1.4 Add "Local Space" enum toggle to "Simulation" category in vfx_parameter_panel.cpp.

## 2. Testing
- [x] 2.1 Demo builds successfully (zig build exit 0).

## 3. Documentation
- [x] 3.1 Field documented with @ref GDC 2019 "Niagara: Building the Future of FX in Unreal Engine" citation.
