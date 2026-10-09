## 1. Items
- [ ] 1.1 Fix the MaterialGraph id-0 sentinel collision (or the test), with the reason recorded.
- [ ] 1.2 Find the shared state behind the MaterialGraphHlslCodegen order dependence.
- [ ] 1.3 Decide each default-drift test (fix the test or the default): fog, sky, camera, project-loader CVar order.
- [ ] 1.4 ShaderCrossBackend: add gbuffer/ to the fixture; make the non-bindless deferred lighting compile with a real shadow-atlas register bound by the C++ side, or exclude that permutation with the reason.
- [ ] 1.5 Re-run the full test_runtime and publish a dated failure list with an owner per suite (the VFX / editor suites stay with the VFX work in progress).

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
