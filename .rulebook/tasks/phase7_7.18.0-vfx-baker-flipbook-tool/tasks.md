# Phase 7-7.18.0 - VFX Baker (Flipbook Offline Capture)

Offline tool that bakes an effect to a flipbook atlas for cheap high-LOD fallback rendering.

Reference: UE5 FNiagaraBakerRenderer

## 1. CLI

- [ ] 1.1 Extend uzbake tool with a vfx subcommand (effect path, frame count, resolution, output).
- [ ] 1.2 Headless render mode with deterministic seed.

## 2. Capture loop

- [ ] 2.1 Render effect frame by frame into an offscreen target.
- [ ] 2.2 Capture HDR backbuffer each frame, compose into an atlas texture.
- [ ] 2.3 BC7 compression for final output.

## 3. Asset format

- [ ] 3.1 .uzvfxbake JSON metadata: atlas path, frame count, rows/cols, original effect reference.
- [ ] 3.2 Engine loader.

## 4. Runtime LOD swap

- [ ] 4.1 LOD 3 emitter swaps to a billboard sampling the bake (single draw call).
- [ ] 4.2 Fade between live sim and baked version across a hysteresis band.

## 5. Demo

- [ ] 5.1 Bake the fireball effect. Show LOD 3 using the baked atlas.

## 6. Docs and tests

- [ ] 6.1 Artist/developer guide for the bake workflow.
- [ ] 6.2 Tests: bake determinism, atlas layout correctness.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
