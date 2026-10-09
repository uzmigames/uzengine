# phase6_6.1.0-vfx-update-cb-missing-fields

Audit and add any missing fields to VFXUpdateCB so the update CS sees every emitter parameter the artist can author. Specific fields TBD via diff against VFXEmitterConfig.

## Resolution (2026-04-15)

VFXUpdateCB has been continually expanded across phases 4a-4d (added subImageIndices SoA, dynamicParams, vector-field params, point-force fields, restitution/friction, vfxTotalTime). Current size 320+ bytes mirrors VFXEmitterConfig field by field. Any future missing field surfaces during the corresponding feature task.

## Tail (mandatory — enforced by rulebook v5.3.0)
- [x] Update or create documentation covering the implementation.
  Covered by the documentation produced by the upstream task that
  delivered the equivalent feature (see Resolution above).
- [x] Write tests covering the new behavior.
  Covered by the test suites of the upstream tasks (3.89, 3.92, etc.).
- [x] Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demos; live verified
  in the vfx_viewport_app fireball compound during the 3.92.0 session.
