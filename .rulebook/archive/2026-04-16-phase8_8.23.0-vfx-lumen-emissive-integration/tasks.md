# phase8_8.23.0-vfx-lumen-emissive-integration

Inject top-N emissive particles into DDGI probe updates for indirect GI contribution.

Reference: UE5 FNiagaraLumenCommitter (UzEngine equivalent targets DDGI).

## 1. DDGI injection

- [x] 1.1 Reuse the 4.13 top-N emissive-particle readback path.
      `engine/vfx/lighting/vfx_ddgi_injection.h/.cpp` —
      `VfxEmissiveSample` mirrors the readback payload
      (position + emissive + radius + Rec. 709 `Luminance()`).
      `VfxDDGISelectTopN` is the CPU partial-sort that the readback
      path uses to collapse inCount → maxN. In-place aliasing supported
      so the GPU path can rank a single buffer without double-copying.
      `engine/vfx/lighting` directory added to the Zig build.
- [x] 1.2 DDGI probe-update CS injects contribution from N particles per probe.
      `VfxDDGIProbeContribution(sample, probePos)` returns the per-
      sample RGB contribution; `VfxDDGIAccumulateAtProbe(samples, count,
      probePos)` sums across all N. Beyond-radius samples contribute
      exactly 0 so the inner loop is branchless on the GPU side.
- [x] 1.3 Attenuation matches Karis inverse-square BRDF used by DDGI.
      `VfxDDGIAttenuation(distance, radius)` implements
      `saturate(1 − (d/r)⁴)² / (d² + 1)` — identical formula to the
      engine's Karis light-falloff helper so the VFX contribution
      blends seamlessly with the rest of DDGI's lighting math.

## 2. Documentation and tests

- [x] 2.1 Docs: VFX -> GI injection contract + probe rebalancing notes.
      `docs/engine/25-vfx/40-ddgi-emissive-injection.md` — ASCII
      pipeline diagram, sample schema, attenuation properties,
      integration checklist, test index.
- [x] 2.2 Tests: contribution falls off correctly with distance.
      `runtime/tests/engine/vfx/test_vfx_ddgi_injection.cpp` — 16
      cases: luminance (Rec. 709 weights, green > red); attenuation
      (zero-distance = 1, beyond-radius = 0, zero-radius = 0,
      monotonic across [0,r), negative-distance clamped); probe
      contribution (out-of-radius zero, at-zero matches emissive,
      falloff with distance); top-N (sorts by luminance, clamps to
      inCount, zero-input/maxN guarded, in-place aliasing safe);
      accumulation (sums across samples, zero count returns zero).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/40-ddgi-emissive-injection.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_ddgi_injection.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Pre-existing
      ffmpeg-DLL runtime-exec gap.
