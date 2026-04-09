## IP Citation Cleanup (from Falcor IP audit)
> Source: `docs/analysis/falcor/ip-audit-*.md`
> Estimate: 2 sessions
> NOTE: P0 files already deleted. Shader atmo/DFS and C++ citations already fixed by agents.

## Session 1: Complete shader citation fixes (verify + remaining)

- [x] 1.1 Verified atmosphere/DFS shaders: 1 hit in sky_cubemap_cs.hlsl ("UE5 default") → replaced with "typical default". Zero remaining.
- [x] 1.2 Verified post-FX shaders: zero `.ush`, `.usf`, `UE5` matches. Clean.
- [x] 1.3 Verified C++ engine: zero `@ref UE`, `.ush:`, `.usf:`, `Adapted from UE5` matches. Clean.
- [x] 1.4 pcss.hlsli:141 — replaced `PixelQuadMessagePassing.ush` reference with generic "quad-lane message passing" description.
- [x] 1.5 cluster_assign_cs.hlsl:385-390 — replaced `ThreadGroupPrefixSum.ush` reference with `@ref Hillis & Steele 1986 "Data Parallel Algorithms"`.
- [x] 1.6 deferred_lighting_ps.hlsl — clean, zero `.ush`/`.usf` matches. No fix needed.
- [x] 1.7 Full codebase grep: zero matches for `.ush:`, `.usf:`, `@ref UE5`, `@ref UE4`, `@ref Unreal`, `FAtmosphereSetup`, `Copyright.*Epic` in runtime/shaders/ and runtime/engine/.

## Session 2: Documentation citation cleanup + ffmpeg license

- [x] 2.1 Stripped `Engine/Source/Runtime/` prefix from 32 analysis docs (167 occurrences). 1 remaining is meta-comment in ip-audit-references.md (states "no paths in source code").
- [x] 2.2 Added `runtime/thirdparty/ffmpeg/LICENSE.txt` — LGPL 2.1 notice for dynamic linking.
- [x] 2.3 Verified `runtime/thirdparty/smaa/LICENSE.txt` exists (MIT, 1470 bytes).
- [x] 2.4 Added AMD copyright + MIT license URL to `cas_sharpen_ps.hlsl` header.
- [x] 2.5 Final IP audit: zero matches for `.ush`, `.usf`, `@ref UE5/UE4/Unreal`, `FAtmosphereSetup`, `Copyright.*Epic` in runtime/shaders/ and runtime/engine/.
- [x] 2.6 Added Hillis & Steele 1986 "Data Parallel Algorithms" to `docs/references.md` (cited in cluster_assign_cs.hlsl).
