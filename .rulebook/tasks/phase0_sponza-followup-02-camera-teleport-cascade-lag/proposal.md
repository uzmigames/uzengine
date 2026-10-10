# Proposal: phase0_sponza-followup-02-camera-teleport-cascade-lag

## Why

Found while hunting the white flash in task 05: when the camera teleports (the old
`fly_camera.lua` jumped from `Camera_A` to `(0,-250,130)` on its first update), the **first frame
after the jump renders with the previous frame's shadow cascades**. Evidence (auto exposure off,
1080p, task 05 notes): the ground-floor corridor measured **43.0/255 mean** on that frame — a flat,
unshadowed floor — and **1.2/255** one frame later, i.e. the sun leaked through the whole corridor
for one frame. Combined with an exposure seeded for the previous view this became a 40-frame white
flash; on its own it is a one-frame light leak on every cut, teleport or level-start camera move.

Suspects: `frame_shadows.cpp` shadow cache (`ctx.shadowCacheEnabled`,
`m_shadowCacheManager.UpdateCache(...)` deciding `cascadeModes[i]`, the `ShadowCSMScrolling` pass
that reuses static pages) and/or the cascade fit running before the camera update is visible
(`frame_setup.cpp` camera → cascade order). The fly-cam fix in task 05 only removed the trigger at
startup; the engine behaviour is unchanged.

## What Changes

A deterministic repro (Lua QA scene that teleports at a known frame), a diagnosis naming the line
that keeps the stale cascades, and a fix: on a camera jump beyond a threshold (or when a cascade's
bounds move by more than one cache page) every cascade re-renders fully that frame and the cache is
rebuilt. A gtest on the invalidation decision.

## Impact
- Affected specs: none
- Affected code: `runtime/engine/renderer/frame/frame_shadows.cpp`, the shadow cache manager
  (`runtime/engine/renderer/shadows/shadow_cache_manager.{h,cpp}` or wherever `UpdateCache` lives),
  `scripts/sponza-teleport-camera.{uzproject,uzscene,lua}` (new QA scene)
- Breaking change: NO
- User benefit: no one-frame sun leak after camera cuts/teleports (cinematics, respawns, editor "go to")
