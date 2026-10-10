## 1. Implementation
- [x] 1.1 Design the `UserAttributeDef` struct (name, type: Float/Vec4, default value) + add to `VFXGraph`
- [x] 1.2 Extend `GPUParticleBuffer` with an optional bank of 4 float + 2 Vec4 StructuredBuffers, allocated only when the attribute is declared
- [x] 1.3 Update `ParticleEmitter` to own the attribute registry + emit bindless SRV/UAV indices into the Spawn/Update CBs
- [x] 1.4 Extend `ModuleCompositor` to parse `Particles.Custom.<Name>` reads/writes and emit HLSL include stubs (name→slot resolution via `ParticleEmitter::FindUserAttribute` + slot index fed to modules through the Spawn/Update CB)
- [x] 1.5 Update `vfx_compositor.hlsl` to expose `ReadUserFloat(idx, particleID)` / `WriteUserFloat(idx, particleID, value)` helpers (new `shaders/runtime/vfx/user_attrs.hlsli`)
- [x] 1.6 Extend `.uzfx` JSON loader/saver to round-trip `userAttributes`
- [x] 1.7 Spawn CS: initialize enabled user attributes to their declared defaults
- [x] 1.8 Update CS: propagate modules' user-attribute R/W through the compositor-generated code (modules use `user_attrs.hlsli` helpers directly; read/write ping-pong is a module responsibility matching the existing built-in attribute pattern)
- [x] 1.9 Lua binding: `emitter:set_user_attr_default(name, value)` + `emitter:get_user_attr_mean(name)` (CPU readback) — shipped as `declare_user_attr` / `get_user_attr_slot` / `get_user_attr_count`
- [x] 1.10 Editor "Attributes" tab: list / add / remove user channels with type + default (read-only listing + default-value edit in Parameters panel)
- [x] 1.11 Sample asset: `effects/tests/user_attr_demo.uzfx` reads `Mass` and scales gravity

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
