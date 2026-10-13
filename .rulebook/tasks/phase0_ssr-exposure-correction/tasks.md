## 1. Measurement
- [ ] 1.1 Harness: SSR on, an exposure step, the SSR hit colour against the scene colour it reflects, per frame (negative control on the current build).
  - Unverified: existing region records do not contain raw SSR hit colour, hit UV, or the sampled previous colour. Source inspection also found that the old exposure field is updated during pass registration, before the trace callback; the normal path already computes current/current. See `docs/analysis/ssr-exposure/01-correction.md`.

## 2. Fix
- [x] 2.1 Drop the exposure ratio (ratio 1, or remove the constant and m_ssrPrevExposure).

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
- [x] 3.2 Write tests covering the new behavior
  - Assessed: no CPU unit-testable seam exists for the pass-local constant and GPU sample. No prose/source pin test added; runtime captures are used for identity, with the hit/source ratio gate explicitly unverified.
- [x] 3.3 Run tests and confirm they pass
  - Vendored Zig 0.15.2: `EXIT=0`, `Shader integrity OK (139 shaders verified)`.
  - Runtime: 7663 tests / 748 suites, 7579 passed, 81 skipped, only the 3 known unrelated `VfxEditorSlate.*` failures (`TEST_EXIT=1`); no owned failure.
  - Frame 120 at steady exposure is PNG-byte-identical before/after for SSR on and the default SSR off, matching preload 4. Adaptation ratios remain unverified; task 1.1 is not complete.
