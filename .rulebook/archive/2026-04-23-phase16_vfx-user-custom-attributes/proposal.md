# User-Defined Per-Particle Custom Attributes

## Why

Niagara's power comes largely from user-defined per-particle channels (`Particles.CustomColor`, `Particles.UserScale`, `Particles.MyFloat`) that scripts/modules can read and write. Our current system hardcodes every SoA channel (position, velocity, age, size, color, rotation) in `GPUParticleBuffer` — adding a new behavior requires modifying core C++ and shaders. Users cannot extend without engine changes.

Exposing user-defined float/Vec4 channels unlocks scriptable modules, designer-driven behaviors, and feature parity with Niagara's "Parameters" system.

## What Changes

- Add a fixed-size bank of user attribute buffers to `GPUParticleBuffer` (e.g., 4 x `StructuredBuffer<float>` + 2 x `StructuredBuffer<float4>`), each optional and enabled via an `enabledUserAttrMask`.
- Per-emitter attribute registry: `userAttributes: [{ name, type, defaultValue }]` defines which channels are live.
- Spawn CS reads defaults from a new user-attribute constants block and writes to enabled buffers.
- Update CS: modules can declare read/write on user attributes via the compositor. The compositor emits bindless SRV/UAV handles into the CS signature.
- Asset format: `.uzfx` gains a `"userAttributes"` array.
- Lua / ECS API: `emitter.set_user_attr_default("MyFloat", 1.5f)` + `emitter.get_user_attr_mean("Mass")` (readback for debug).
- Editor UI: new "Attributes" tab listing user-defined channels with name / type / default / remove.

## Impact

- Affected specs: VFX compositor, GPU particle buffer layout, asset format
- Affected code: `gpu_particle_buffer.h/cpp`, `module_compositor.h/cpp`, `vfx_graph.h`, `vfx_compositor.hlsl`, editor Attributes tab
- Breaking change: NO (emitters without `userAttributes` declaration behave identically)
- User benefit: extensible per-particle state without engine C++ modifications
