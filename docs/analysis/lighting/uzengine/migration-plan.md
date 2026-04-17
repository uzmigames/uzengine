# UzEngine · Lighting Migration Plan (Rollout)

How to move from the **current** monolithic deferred_lighting_ps.hlsl
to the **new** pipeline described in
[aaa-default-pipeline.md](./aaa-default-pipeline.md) **without breaking
existing scenes, demos, or tests**.

Principles:

1. **Additive first, subtractive last.** New passes run alongside old
   ones until they produce equivalent or better output. Only then
   remove the old.
2. **CVar-gated flips.** Every major behavior change is behind a CVar
   defaulted to the current behavior. Stable main branch at all times.
3. **Per-scene validation** against the path-traced reference before
   promoting the new path to default.
4. **PR-sized increments.** Each phase is 1–3 weeks and ships standalone.

---

## 1 · Milestone tree

```
Phase 0 (1 wk): DDGI works end-to-end on Sponza
                  └── Gate: Sponza shadowed area has warm bounce color

Phase 1 (2 wk): Indirect Integration pass added
                  └── Gate: deferred PS ≤ 400 lines; PSNR parity with old path

Phase 2 (2 wk): Stochastic SSR + hit distance + world fallback
                  └── Gate: SSR PSNR ≥ 35 dB on Sponza glossy floor

Phase 3 (3 wk): NRD-inspired denoiser stack
                  └── Gate: temporal variance ≤ 0.002 in static capture

Phase 4 (4 wk): CSM → VSM migration (CVar-gated)
                  └── Gate: r.Shadow.Mode=1 enables VSM; defaults stay CSM

Phase 5 (2 wk): LTC area lights
                  └── Gate: rectangle/disc/tube lights work in Sponza

Phase 6 (4 wk): Jolt HWRT backend
                  └── Gate: r.RT.Mode=1 enables RT fallbacks end-to-end

Phase 7 (3 wk): ReSTIR DI
                  └── Gate: 256-light test runs at same frame time as 8-light

Phase 8 (3 wk): Character lighting pipeline (SSS + eye + hair-cards)
                  └── Gate: hero character quality matches reference

Phase 9 (4 wk): NGX / FSR RR / XeSS AI plug-in slot
                  └── Gate: r.Denoise.Neural=1 gives clean output at 1 spp
```

Total: ~28 weeks. Parallelizable to ~18 weeks with 2 engineers.

---

## 2 · Per-phase PR structure

Each phase is one or more PRs following this template:

### PR 1 — Add new code (doesn't remove anything)

- New files added.
- New CVars registered with safe defaults.
- New pass registered in render graph behind the CVar.
- `rulebook_task_update` to reflect progress.
- Unit tests for new code.

**Merge criteria**: build passes, old code path unchanged, new path gated off.

### PR 2 — Enable + validate

- Change CVar default to enable new path (or add to quality preset).
- Update validation PSNR thresholds if needed.
- Update gap-analysis doc with new state.
- `rulebook_memory_save` a note about the change.

**Merge criteria**: validation suite passes (PSNR ≥ 35 dB on Sponza).

### PR 3 — Remove old code

- Old code path removed (guarded by CVar == 0 previously; now just gone).
- CVars for old path removed.
- Documentation updated.

**Merge criteria**: build passes, all tests pass.

---

## 3 · Phase 0 detail (the Sponza fix)

### 3.1 Scope

Just the minimum to get Sponza lit correctly. No new passes, no
architectural changes.

### 3.2 Changes

1. `runtime/engine/renderer/gi/ddgi_system.cpp`:
   - Ensure directional light + skylight are reachable by the probe update CS.
   - Verify probe world-space coords are correct (not inside geometry).
   - Make sure the atlas is written every frame, not skipped after first.

2. `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`:
   - Enable directional-light sampling (if disabled).
   - Sample sky LUT for probes whose rays exit upward.

3. `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`:
   - Change DDGI sampling so it's the primary indirect-diffuse source,
     not a gated addition.
   - Apply GTAO to full indirect sum.
   - Expose PCSS light size as CVar.

4. `runtime/engine/renderer/frame/frame_constants.cpp`:
   - Verify `indirectLightingIntensity = 1.0` default.
   - Hook up the PCSS light size CVar.

### 3.3 Tests

- Sponza `--pass-dump`: verify `ddgi.irradiance` is non-empty and colored.
- Sponza pixel-debug at shadowed spot: verify HDR has warm tint (R > G > B).
- Final screenshot visibly different from pre-fix baseline.

### 3.4 Duration

~1 week.

---

## 4 · Phase 1 detail (Indirect Integration)

### 4.1 Scope

New compute pass `IndirectIntegrationCS` that takes SSGI + DDGI + IBL
and outputs unified `IndirectDiffuse` + `IndirectSpecular` buffers.

Deferred PS simplifies: reads the integrated buffers instead of
sampling each source individually.

### 4.2 Changes

1. New file `runtime/shaders/runtime/lighting/indirect_integration_cs.hlsl` — per [shader-sketches.md](./shader-sketches.md) §1.

2. New file `runtime/engine/renderer/passes/lighting/indirect_integration_pass.{cpp,h}` — registers the pass in the render graph.

3. `runtime/engine/renderer/passes/lighting/lighting_pass.cpp`:
   - Add dependency on `IndirectDiffuse` + `IndirectSpec` buffers.
   - Pass new textures to the PS.

4. `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`:
   - Remove SSGI sampling.
   - Remove DDGI sampling.
   - Remove IBL diffuse convolution sampling.
   - Just read `IndirectDiffuse` and `IndirectSpec`.
   - Shader should drop from ~1100 lines to ~400.

5. New constant buffer `IndirectIntegrationCB` — per [cb-layouts.md](./cb-layouts.md) §2.

### 4.3 Gate CVar

`r.Lighting.UseIntegrationPass` (default 0 during Phase 1 PR 1,
flipped to 1 in Phase 1 PR 2).

### 4.4 Tests

- PSNR parity on Sponza vs baseline (old deferred PS path).
- Performance: deferred PS cost drops noticeably (less texture sampling, less math).
- Integration pass cost comparable (~0.8 ms at 1440p).
- Total lighting cost: ≤ baseline.

### 4.5 Duration

~2 weeks.

---

## 5 · Risk management

### 5.1 Regression risk

Every phase has an "exit criterion" based on validation metrics. If
any regresses, the PR doesn't merge.

### 5.2 Dependent phases

- Phase 4 (VSM) depends on understanding the shadow pipeline — schedule after Phase 0 to know the baseline state.
- Phase 6 (HWRT) depends on Jolt/BVH infrastructure. Budget 1 week for BVH manager alone.
- Phase 7 (ReSTIR) depends on Phase 6 (needs HWRT shadow rays) OR Phase 0 (SDF shadow rays).
- Phase 9 (RR) depends on Phase 3 (clean demodulated signal from our own denoiser).

### 5.3 Blockers

- **DDGI probe update never worked correctly** → Phase 0 takes 2 weeks instead of 1.
- **NRD SDK integration hurdles** → skip NRD integration, build our own throughout Phase 3.
- **Sponza content bug** → document and re-test with other scenes.
- **Vendor-specific SDK compatibility** → schedule Phase 9 as strictly optional.

### 5.4 Roll-back procedure

Every CVar-gated change can be rolled back:

```bash
# Emergency: disable new pipeline, re-enable old
--cvar r.Lighting.UseIntegrationPass=0
--cvar r.DDGI.Enabled=0
# etc.
```

Keep the "old path" code for one full release cycle after enabling the
new path, in case of late-discovered regression.

---

## 6 · Communication plan

After each phase merges:

1. Update `.rulebook/PLANS.md` session summary.
2. `rulebook_memory_save` — record the phase outcome + PSNR numbers.
3. Update [gap-analysis.md](./gap-analysis.md) status of fixed items.
4. Update this file's milestone tree (check off phase).
5. Update [aaa-default-pipeline.md](./aaa-default-pipeline.md) if
   design changed during implementation.

---

## 7 · Sanity checks before each phase

Before starting a phase:

1. Read the phase description in [aaa-default-pipeline.md](./aaa-default-pipeline.md).
2. Read the relevant technique doc in [../modern-techniques/](../modern-techniques/) or [../lumen/](../lumen/) or [../crimson-desert/](../crimson-desert/).
3. Read the relevant files in the repo (grep the codebase for existing primitives).
4. Check knowledge base / memory for prior work on the topic.
5. Write a 1-paragraph plan for the phase PR(s).
6. `rulebook_task_create` for the phase.

---

## 8 · References

- [aaa-default-pipeline.md](./aaa-default-pipeline.md) — the target architecture.
- [gap-analysis.md](./gap-analysis.md) — current state.
- [shader-sketches.md](./shader-sketches.md) — code templates.
- [cb-layouts.md](./cb-layouts.md) — data layouts.
- [validation-methodology.md](./validation-methodology.md) — how to prove each phase passed.
- [sponza-debug-plan.md](./sponza-debug-plan.md) — day-1 Sponza diagnosis.
