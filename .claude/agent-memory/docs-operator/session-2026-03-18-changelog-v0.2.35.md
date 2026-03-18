---
name: Documentation Update Session 2026-03-18
description: Completed audit report Fix Status tables, v0.2.35 CHANGELOG, and BRDF documentation updates
type: observation
---

## Session Summary (2026-03-18)

**Objective**: Update deep audit reports with fix status, write CHANGELOG v0.2.35, and document BRDF corrections.

### Files Updated

1. **`docs/analysis/rendering/deep-audit-taa-ssr.md`**
   - Added "Fix Status (2026-03-18)" table with 22 issues
   - Marked 5 items as FIXED (motion vector support, bicubic history, velocity magnitude, Fresnel removal, blend weight)
   - 17 items marked NOT FIXED (deferred or low-priority)

2. **`docs/analysis/rendering/deep-audit-postprocess.md`**
   - Added "Fix Status (2026-03-18)" table with 14 issues
   - Marked 6 items as FIXED (bloom tint, vignette HDR, exposure adaptation, GTAO taps, denoise kernel)
   - 8 items marked as NO CHANGE NEEDED or DOCUMENTED

3. **`docs/analysis/rendering/deep-audit-cpp-pipeline.md`**
   - Added "Fix Status (2026-03-18)" table with 5 issues
   - Marked 3 items as FIXED (TAA velocity buffer, motion blur neighbor max, first-frame guard)
   - 2 items marked NOT FIXED

4. **`docs/CHANGELOG-v0.2.35.md`** (NEW — 66 lines)
   - CRITICAL section: BRDF (Vis_SmithJointApprox, F_Schlick F90, NdotV), TAA motion vectors, SSR Fresnel removal
   - HIGH section: vignette HDR, auto-exposure log2, per-light scales, motion blur neighbor max
   - MEDIUM & LOW sections: GTAO, bloom tint, minor fixes
   - Quality improvements: 16x anisotropic, FXAA, mipmap generation
   - Complete file list (17 files modified)

5. **`docs/engine/12-rendering/10-cook-torrance-brdf.md`**
   - Added subsection "Critical: Vis_SmithJointApprox Input (Roughness⁴)" (24 lines)
   - Documented the critical roughness⁴ requirement (alpha², not alpha)
   - Explained visual impact of incorrect input
   - Cited UE5 ShadingModels.ush:215, BRDF.ush:340–360
   - Referenced v0.2.35 release date

### Task Status Updates

**`.rulebook/tasks/phase0-0.9.0-rendering-audit-corrections/tasks.md`**
- Item 12.1: Already marked [x] — audit reports with fix status
- Item 12.2: Updated [x] — CHANGELOG-v0.2.35.md created
- Item 12.3: Updated [x] — BRDF formula documentation updated

### Key Facts for Future Sessions

- **CHANGELOG pattern**: v0.2.X format under `docs/CHANGELOG-v0.2.XX.md` (not in a single CHANGELOG.md)
- **Audit report structure**: Each audit file should end with "## Fix Status (YYYY-MM-DD)" containing a pipe-delimited table
- **BRDF documentation location**: `docs/engine/12-rendering/10-cook-torrance-brdf.md` — update "Smith Combined Form" section with subsections
- **Rulebook tasks**: Active phase tasks under `.rulebook/tasks/phase0-0.9.0-rendering-audit-corrections/` — update tasks.md with [x] status and brief description
