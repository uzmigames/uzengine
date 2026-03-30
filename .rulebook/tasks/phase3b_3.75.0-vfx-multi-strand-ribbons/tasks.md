## Phase 3b-3.75.0 — VFX Multi-Strand Ribbons & Per-Particle Trails

Extend the trail/ribbon system to support multiple parallel strands and per-particle
trail emission. Niagara reference: Ribbon Renderer with multiple strands.

## 1. Multi-Strand Ribbon Geometry

- [ ] 1.1 Add `uint32_t ribbonStrands = 1` to TrailAsset/TrailComponent — number of parallel ribbon strips (1 = single, 2-8 = multi-strand).
- [ ] 1.2 In `trail_mesh_builder.cpp`: for each point, emit N sets of vertices offset by right vector. Strand offset = `strandIndex / (numStrands-1) * ribbonWidth - halfWidth`.
- [ ] 1.3 Each strand gets unique UV.v offset for texture variation across strands.
- [ ] 1.4 Trail VS: pass strand index for per-strand color or width modulation in PS.

## 2. Per-Particle Trails

- [ ] 2.1 Add `bool emitTrail = false` to VFXEmitterConfig — when true, each alive particle contributes a trail point every N frames.
- [ ] 2.2 GPU readback: read top-K particle positions each frame (via readback buffer, max 64 particles with trails).
- [ ] 2.3 CPU side: maintain per-particle trail ring buffers, feed into TrailSystem.
- [ ] 2.4 Trail color inherits from particle color for visual coherence.

## 3. Ribbon Rendering Enhancements

- [ ] 3.1 Add ribbon width over lifetime — wide at head, tapered at tail.
- [ ] 3.2 Add ribbon color over lifetime — bright at head, fading at tail.
- [ ] 3.3 Add UV distortion (scroll + noise) for animated ribbon textures.
- [x] 3.4 Fix trail PSO creation error (currently failing with E_INVALIDARG). Converted trail shaders from Input Assembler vertex fetch to bindless StructuredBuffer fetch via SV_VertexID, matching the engine's SM6.6 root signature. Removed input layout from PSO desc, changed VB/IB to StructuredBuffers with SRV indices, updated root constants to carry VB/IB SRV indices, changed DrawIndexed to Draw.

## 4. Demo Integration

- [ ] 4.1 Fireball preset: per-particle trails enabled (fiery streaks behind each ember).
- [ ] 4.2 Magic Orb: 3-strand ribbon with blue→cyan gradient.
- [ ] 4.3 Add trail strand toggle key (Shift+Y: cycle 1/2/4/8 strands).

## 5. Visual Verification

- [ ] 5.1 Screenshot multi-strand ribbon — verify parallel strips with UV variation.
- [ ] 5.2 Screenshot per-particle trails — each particle leaves its own tail.
- [ ] 5.3 Verify width/color tapering looks correct.

## 6. Documentation

- [ ] 6.1 Document multi-strand and per-particle trail configuration.
- [ ] 6.2 Update VFX guide with ribbon rendering details.
