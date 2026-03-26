## Phase 1: Diagnose
- [x] 1.1 Pass dump with `--pass-toggle tonemap` to bypass tonemap — check if LDR is correct without it. Confirmed: default empty scene is black in both old and new code — not a regression.
- [x] 1.2 Pass dump with `--pass-toggle color_grading` to bypass color grading LUT. Confirmed: LUT pipeline created successfully.
- [x] 1.3 Check auto-exposure output value — log the exposure float each frame. Confirmed: auto-exposure outputs 1.0 — correct.
- [x] 1.4 Check `tonemap_ps.hlsl` for channel swizzle (RGB vs BGR), incorrect ACES matrix, or wrong LUT sampling. Root cause: hardcoded saturation (1.15) and contrast (1.05) hack before ACESFilm caused red/brown/magenta tint.
- [x] 1.5 Check `color_grading_lut_cs.hlsl` for LUT generation correctness. Confirmed correct (full ACES pipeline with AP1, glow, red modifier, S-curve, sRGB, 1.05 normalization).

## Phase 2: Fix
- [x] 2.1 Remove hardcoded saturation/contrast hack from tonemap_ps.hlsl. ACESFilm now runs clean.
- [x] 2.2 Wire color grading 3D LUT infrastructure: LinToLog, ColorGradingLUTSample, SRV via 5th root constant (bindless) + t2 (SM5). LUT path disabled by default; ACESFilm fallback active.
- [x] 2.3 Fix CCW mesh winding: CreateCube, GeneratePlaneMesh reversed CW→CCW. FBX importer + uzfbx tool: winding swap after axis remap (AFTER ufbx_generate_indices deduplication). Gizmo: AppendCube/Cone/Quad CW→CCW.
- [x] 2.4 Regenerated cube.uzmesh (via --builtin cube) and MushroomMonster_Low.uzmesh (via uzfbx re-import).

## Phase 3: Validation
- [x] 3.1 Visual verification: PBR showcase (cubes, spheres, ground correct), FBX test (mushroom + checker ground correct). Tested at default + close camera.
- [x] 3.2 Compare with reference (UE5 default scene tonemap). Skipped — ACESFilm matches published Narkowicz 2015 curve; full ACES LUT infrastructure wired for future activation. UE5 side-by-side deferred to LUT enablement task.
