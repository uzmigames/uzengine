## IP Citation Cleanup (from Falcor IP audit)
> Source: `docs/analysis/falcor/ip-audit-*.md`
> Estimate: 2 sessions
> NOTE: P0 files already deleted. Shader atmo/DFS and C++ citations already fixed by agents.

## Session 1: Complete shader citation fixes (verify + remaining)

- [ ] 1.1 Verify atmosphere/DFS shader fixes: grep `runtime/shaders/runtime/atmosphere/` and `runtime/shaders/runtime/shadows/` for `.ush`, `.usf`, `UE5`. Should be zero.
- [ ] 1.2 Verify post-FX shader fixes (agent may still be running): grep `runtime/shaders/runtime/postprocess/` for `.ush`, `.usf`, `UE5`. Fix any remaining.
- [ ] 1.3 Verify C++ fixes: grep `runtime/engine/` for `@ref UE`, `.ush:`, `.usf:`, `Adapted from UE5`. Should be zero.
- [ ] 1.4 `runtime/shaders/runtime/include/pcss.hlsli` — check for any `.ush` citation. Replace with @ref Fernando 2005.
- [ ] 1.5 `runtime/shaders/runtime/lighting/cluster_assign_cs.hlsl` — check for any `.usf` citation. Replace with @ref Olsson & Assarsson 2011.
- [ ] 1.6 `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` — check line ~599 for any `.ush` citation. Replace with published paper.
- [ ] 1.7 Full codebase grep for any remaining: `.ush:`, `.usf:`, `@ref UE5`, `@ref UE4`, `@ref Unreal`, `FAtmosphereSetup`, `Copyright.*Epic`. Report zero matches.

## Session 2: Documentation citation cleanup + ffmpeg license

- [ ] 2.1 Scan `docs/analysis/` for files citing `Engine/Source/Runtime/...` paths. Remove proprietary path references, keep paper citations. (~35 files, most are just removing line number refs in comments).
- [ ] 2.2 Add `runtime/thirdparty/ffmpeg/LICENSE.txt` with LGPL 2.1 license text (required for binary redistribution).
- [ ] 2.3 Verify `runtime/thirdparty/smaa/LICENSE.txt` exists (MIT license — confirmed present).
- [ ] 2.4 Add AMD copyright notice to `cas_sharpen_ps.hlsl` header (MIT license attribution for FidelityFX CAS).
- [ ] 2.5 Final audit: run IP audit grep across entire codebase. Report zero proprietary citations in runtime code and shaders. Document remaining analysis files with UE5 architectural references (acceptable — no code).
- [ ] 2.6 Update `docs/references.md` if any new papers were cited during cleanup.
