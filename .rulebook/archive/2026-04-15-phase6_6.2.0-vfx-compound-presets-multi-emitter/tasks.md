# phase6_6.2.0-vfx-compound-presets-multi-emitter

Niagara-style compound presets that bundle multiple emitters as a single asset.

## Resolution (2026-04-15)

Delivered in 3.92.0 as the VfxSystem / VfxSystemComponent / VfxSystemInstance trio. Fireball compound (head + sparks + embers + smoke) shipped as the reference asset (runtime/content/vfx/systems/fireball.uzvfxsys). Architecture documented in docs/engine/25-vfx/18-vfx-system-compound.md.

## Tail (mandatory — enforced by rulebook v5.3.0)
- [x] Update or create documentation covering the implementation.
  Covered by the documentation produced by the upstream task that
  delivered the equivalent feature (see Resolution above).
- [x] Write tests covering the new behavior.
  Covered by the test suites of the upstream tasks (3.89, 3.92, etc.).
- [x] Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demos; live verified
  in the vfx_viewport_app fireball compound during the 3.92.0 session.
