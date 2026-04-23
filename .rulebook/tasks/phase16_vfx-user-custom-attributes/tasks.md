## 1. Implementation
- [ ] 1.1 Design the `UserAttributeDef` struct (name, type: Float/Vec4, default value) + add to `VFXGraph`
- [ ] 1.2 Extend `GPUParticleBuffer` with an optional bank of 4 float + 2 Vec4 StructuredBuffers, allocated only when the attribute is declared
- [ ] 1.3 Update `ParticleEmitter` to own the attribute registry + emit bindless SRV/UAV indices into the Spawn/Update CBs
- [ ] 1.4 Extend `ModuleCompositor` to parse `Particles.Custom.<Name>` reads/writes and emit HLSL include stubs
- [ ] 1.5 Update `vfx_compositor.hlsl` to expose `ReadUserFloat(idx, particleID)` / `WriteUserFloat(idx, particleID, value)` helpers
- [ ] 1.6 Extend `.uzfx` JSON loader/saver to round-trip `userAttributes`
- [ ] 1.7 Spawn CS: initialize enabled user attributes to their declared defaults
- [ ] 1.8 Update CS: propagate modules' user-attribute R/W through the compositor-generated code
- [ ] 1.9 Lua binding: `emitter:set_user_attr_default(name, value)` + `emitter:get_user_attr_mean(name)` (CPU readback)
- [ ] 1.10 Editor "Attributes" tab: list / add / remove user channels with type + default
- [ ] 1.11 Sample asset: `effects/tests/user_attr_demo.uzfx` reads `Mass` and scales gravity

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
