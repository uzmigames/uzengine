# Tasks: Documentation Fixes

## 1. Critical — Missing Documentation
- [x] 1.1 Create docs for renderer passes: GBuffer, Lighting, Shadow, PBR — purpose, inputs/outputs, register bindings (D-01)
- [x] 1.2 Create docs for shadow system: cascade generation, atlas layout, PCF sampling (D-01)
- [x] 1.3 Create docs for scene_lighting.h and vertex_layout.h (D-01)
- [x] 1.4 Update all references from forward_pbr_pass to pbr_pass across docs/ (D-02)

## 2. High — Outdated Documentation
- [x] 2.1 Update engine_math.h docs to reflect new split headers (vector3.h, quaternion.h, etc.) (D-03)
- [x] 2.2 Audit and fix outdated file paths in AGENTS.md (D-04) — No fixes needed, all paths valid
- [x] 2.3 Complete docs/engine/12-rendering/ — add shadow system and deferred pipeline sections (D-05)
- [x] 2.4 Complete docs/engine/14-camera/ — add full camera system documentation (D-06)

## 3. Medium — Doc Quality
- [x] 3.1 Audit all .h files for terse /// docs — expand with param/return/error descriptions (D-07) — 18 headers audited, all excellent
- [x] 3.2 Add // --- section separators to .cpp files that lack them (D-08) — Already present in all major files
- [x] 3.3 Add comments to demo code explaining rendering setup decisions (D-09) — Already present

## 4. Verification
- [x] 4.1 Cross-reference all docs/ files against actual code — no stale references
- [x] 4.2 Verify every public class/function in engine headers has complete /// docs
