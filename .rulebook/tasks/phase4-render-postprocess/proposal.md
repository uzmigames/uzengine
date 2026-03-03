# Proposal: phase2-render-postprocess

## Why

The current renderer outputs raw HDR linear color directly to the swapchain — no bloom, no tone mapping, no exposure control. Games look flat and blown-out. Post-process effects are the final step that makes rendered frames look cinematic. Bloom makes bright lights glow; tone mapping converts HDR to LDR with artistically controllable contrast; auto exposure adjusts to lighting conditions; chromatic aberration and vignette add lens character.

The render graph from `phase1-render-graph` makes this straightforward: each effect is a `RgPass` that reads the previous HDR color buffer and writes to a new one. Settings are configured per-project in `.uzproj` and hot-reloadable.

## What Changes

- **`src/engine/renderer/passes/`** — one file per post-process pass:
  - `bloom_pass.h/cpp` — dual Kawase blur, luminance threshold, additive composite
  - `tone_mapping_pass.h/cpp` — ACES filmic, Reinhard, exposure control
  - `auto_exposure_pass.h/cpp` — compute luminance histogram, adapt over time
  - `chromatic_aberration_pass.h/cpp` — barrel distortion of RGB channels
  - `vignette_pass.h/cpp` — radial darkening
  - `color_lut_pass.h/cpp` — 3D LUT texture application
- **`.uzproj` post_process section** — enable/disable passes, configure parameters
- **Hot-reload** — `.uzproj` changes apply immediately
- **Tests** — each pass: shader compiles, output texture produced, parameter ranges validated

## Impact

- Affected specs: `docs/specs/renderer.md`, `docs/specs/screen-space-effects.md`
- Dependencies: `phase1-render-graph` (RgPass API must exist), `phase2-pbr-renderer` (HDR color buffer)
- Breaking change: NO — passes can be disabled; existing pyramid renderer unaffected
- User benefit: cinematic image quality; configurable visual style per project; zero code changes to enable/disable effects
