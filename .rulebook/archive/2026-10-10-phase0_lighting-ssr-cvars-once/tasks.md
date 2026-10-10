## 1. Baseline and gates
- [x] 1.1 **Done (the audit listed nine doubles, not five)** Record the before state (doc "Before"): the audit on build 513e2983 lists the five doubles.
- [x] 1.2 **Registered; results in the doc gate table** Pre-register C1-C4 (proposal).

## 2. The cleanup
- [x] 2.1 (C1 PASS: eight copies removed, r.RayTracing.GI read through the registry) `frame_lighting.cpp`, `frame_gi_passes.cpp`: the unused second registrations removed, the owner named at each site.
- [x] 2.2 (C2 PASS, C3 PASS) `scripts/rendering/test_cvar_registrations.py` and its fixture.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-cvars-once.md`, the known-issues entry and the A.8 line, the block-B index) Update or create documentation covering the implementation (`docs/analysis/lighting/uzengine/sponza-lighting-2026-10/lighting-cvars-once.md`; the known-issues block A.8 list; the block-B index).
- [x] 3.2 (the audit, the fixture, the header rule) Write tests covering the new behavior with behavioral assertions and negative controls (`scripts/rendering/test_cvar_registrations.py`: the audit, the fixture as the negative control, the header rule).
- [x] 3.3 Run tests and confirm they pass; validate this task through Rulebook; report pre-existing failures separately. **Done** — `test_cvar_registrations.py --gpu` 5/5: 269 names each registered once, the fixture's duplicate reported, no header registration, the SSR passes run on the reference scene with SSR on (0.119 ms total) and the reported r.SSR.* values are the surviving defaults. renderer gtests 7471 passed.
