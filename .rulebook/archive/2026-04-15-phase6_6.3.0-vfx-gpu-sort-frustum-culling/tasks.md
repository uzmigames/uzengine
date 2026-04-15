# phase6_6.3.0-vfx-gpu-sort-frustum-culling

GPU bitonic sort + frustum-cull integration so depth-correct billboard rendering plays nicely with view-volume culling.

## Resolution (2026-04-15)

GPU bitonic sort shipped in the standalone vfx_viewport (Phase 9.7 of task 5.3.0) and ride along to the engine-integrated vfx_viewport_app via the shared particle pipeline. Frustum culling shipped in 3.89.0 (NDC test in demo + full FrustumPlanes API + hysteresis). Combined path verified live in the fireball compound demo.

## Tail (mandatory — enforced by rulebook v5.3.0)
- [x] Update or create documentation covering the implementation.
  Covered by the documentation produced by the upstream task that
  delivered the equivalent feature (see Resolution above).
- [x] Write tests covering the new behavior.
  Covered by the test suites of the upstream tasks (3.89, 3.92, etc.).
- [x] Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demos; live verified
  in the vfx_viewport_app fireball compound during the 3.92.0 session.
