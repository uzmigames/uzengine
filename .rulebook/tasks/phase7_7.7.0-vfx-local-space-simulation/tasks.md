# Phase 7-7.7.0 - VFX Local-Space Simulation Mode

Per-emitter LocalSpace vs WorldSpace toggle. Particles stored in local frame; draw applies world transform.

Reference: UE5 UNiagaraEmitter::bLocalSpace

## 1. Mode

- [ ] 1.1 Add simulationSpace enum to ParticleEmitter (default WorldSpace for backward compat).
- [ ] 1.2 Expose in .uzfx JSON (simulation_space field).

## 2. Spawn and update CS

- [ ] 2.1 Spawn CS: when LocalSpace, write positions in emitter-local frame (subtract emitter world-pos).
- [ ] 2.2 Update CS: rotate gravity/external forces into local frame before integration.

## 3. Render

- [ ] 3.1 Billboard VS multiplies by worldFromLocal before view transform when emitter is LocalSpace.
- [ ] 3.2 Trail/ribbon paths updated analogously.

## 4. Demo

- [ ] 4.1 Attach emitter to moving entity; compare World vs Local trails visually.

## 5. Docs and tests

- [ ] 5.1 Extend 18-vfx-system-compound.md with a Simulation Space section.
- [ ] 5.2 Tests: round-trip world<->local transform, gravity rotation correctness.

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
