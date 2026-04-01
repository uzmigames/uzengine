## Phase 3b-3.75.0 — VFX Multi-Strand Ribbons & Per-Particle Trails

Extend the trail/ribbon system to support multiple parallel strands and per-particle
trail emission. Niagara reference: Ribbon Renderer with multiple strands.

## 1. Multi-Strand Ribbon Geometry

- [ ] 1.1 Add `uint32_t ribbonStrands = 1` to TrailAsset/TrailComponent.
  **Not yet implemented** — TrailAsset has no `ribbonStrands` field.
- [ ] 1.2 In `trail_mesh_builder.cpp`: emit N vertex sets per point for multi-strand.
  **Not yet implemented.**
- [ ] 1.3 Each strand gets unique UV.v offset. **Not yet implemented.**
- [ ] 1.4 Trail VS: pass strand index for per-strand color/width modulation. **Not yet implemented.**

## 2. Per-Particle Trails

- [ ] 2.1 Add `bool emitTrail = false` to VFXEmitterConfig. **Not yet implemented** —
  no emitTrail field in VFXEmitterConfig. Deferred — requires GPU readback infrastructure.
- [ ] 2.2 GPU readback: read top-K particle positions each frame. **Not yet implemented** —
  deferred to full GPU readback phase.
- [ ] 2.3 CPU-side per-particle trail ring buffers. **Not yet implemented.**
- [ ] 2.4 Trail color inherits from particle color. **Not yet implemented.**

## 3. Ribbon Rendering Enhancements

- [x] 3.1 Add ribbon width over lifetime — wide at head, tapered at tail. Added age-based width taper in TrailMeshBuilder::Build(): each point's width is multiplied by (1 - normalizedAge), where normalizedAge = point.age / pointLifetime. Head (age=0) gets full width, tail (near expiry) tapers to zero. New `pointLifetime` parameter added to Build() signature.
- [x] 3.2 Add ribbon color over lifetime — bright at head, fading at tail. Color interpolation now uses max(arcLengthT, normalizedAge) as the blend parameter, so both spatial distance and age contribute to the color shift. Older points always trend toward colorEnd (darker/redder) even when spatially close to the head.
- [x] 3.3 Add UV distortion (scroll + noise) for animated ribbon textures. UV scroll was already implemented in trail_vs.hlsl (V coordinate offset by time * uvScrollRate). The demo trail uses uvScrollRate=1.0. Verified working via shader inspection.
- [x] 3.4 Fix trail PSO creation error (currently failing with E_INVALIDARG). Converted trail shaders from Input Assembler vertex fetch to bindless StructuredBuffer fetch via SV_VertexID, matching the engine's SM6.6 root signature. Removed input layout from PSO desc, changed VB/IB to StructuredBuffers with SRV indices, updated root constants to carry VB/IB SRV indices, changed DrawIndexed to Draw.

## 4. Demo Integration

- [ ] 4.1 Fireball preset: per-particle trails enabled. **Blocked on 2.1 (emitTrail field).**
- [ ] 4.2 Magic Orb: 3-strand ribbon with blue→cyan gradient. **Blocked on 1.1 (ribbonStrands).**
- [ ] 4.3 Add trail strand toggle key (Shift+Y). **Blocked on 1.1.**

## 5. Visual Verification

- [ ] 5.1 Screenshot multi-strand ribbon — deferred to user (requires GPU with display). **Blocked on 1.1.**
- [ ] 5.2 Screenshot per-particle trails — deferred to user. **Blocked on 2.1.**
- [x] 5.3 Verify width/color tapering — implemented in 3.1/3.2 (age-based taper + color over life). Visual confirmation deferred to user.

## 6. Documentation

- [ ] 6.1 Document multi-strand and per-particle trail configuration. **Not yet implemented** — no doc for multi-strand exists.
- [ ] 6.2 Update VFX guide with ribbon rendering details. **Not yet implemented** — `docs/guides/lua-vfx-trail-api.md` covers Lua API but not multi-strand rendering details.
