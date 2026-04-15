# Phase 7-7.2.0 - VFX System-Level Parameter Store

Named user parameters on VfxSystemComponent, writable from Lua/C++ and consumed by emitters via binding overrides. Depends on 7.1.0.

Reference: UE5 FNiagaraParameterStore + FNiagaraUserRedirectionParameterStore

## 1. Parameter store

- [ ] 1.1 Design VfxParameterStore with SetFloat/SetVec3/SetVec4/SetColor(name, value) and GetParameterNames() reflection.
- [ ] 1.2 Wire into VfxSystemInstance - one store per instance, flushed into the System scope each frame.
- [ ] 1.3 Add VfxSystemComponent::parameters map serialised in .uzvfxsys JSON (optional parameters object).

## 2. Lua API

- [ ] 2.1 Add ctx.vfx:set_param(entity, name, value) and ctx.vfx:get_param(entity, name).
- [ ] 2.2 Type-safe variants per scalar/vec3/vec4/color.

## 3. Binding overrides

- [ ] 3.1 Per-emitter binding overrides in .uzvfxsys (emitter_ref + binding object with expression strings).
- [ ] 3.2 Minimal expression evaluator (constants, global.X reference, arithmetic *, +, /).

## 4. Documentation and tests

- [ ] 4.1 Extend docs/engine/25-vfx/18-vfx-system-compound.md with a Parameters section.
- [ ] 4.2 Tests: JSON round-trip, Lua set/get, binding expression evaluation.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
