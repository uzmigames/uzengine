# VFX Viewport App Default Scene - Spec Delta

## ADDED Requirements

### Requirement: Default Scene Has Zero Asset-File Dependencies

`runtime/demos/vfx_viewport_app/vfx_viewport_app.exe` launched with no
CLI arguments MUST produce a working scene without referencing any
non-existent asset files. The startup log MUST contain ZERO
`[VFX] [error]` lines reporting missing files.

#### Scenario: No-arg launch produces no missing-file errors

Given the demo is launched with no arguments other than the platform default `--vfs-root`
When `OnStart()` completes
Then `%LOCALAPPDATA%/UzEngine/logs/uzengine_<timestamp>.log` contains zero `[VFX] [error]` lines reporting any missing `.uzvfxsys`, `.uzvfx`, `.uzmesh`, or `.uzmat` file
And `m_emitterEntities.size() >= 1`
And the standalone emitter renders visible particles in the viewport at frame 60+

### Requirement: All 22 Preset Slots Are Cycle-Functional

The preset cycling list (`kPresetFns[]` / `kPresetNames[]`) MUST have
22 entries that all produce a visible particle effect when cycled to.
NO slot may depend on a `.uzvfxsys` compound or any other external
asset file.

#### Scenario: Cycling forward through all 22 presets renders particles for each

Given the demo runs in default mode at 60 fps
When the user (or test harness) presses Tab 22 times in sequence
Then the overlay status updates the preset name on each press matching `kPresetNames[]`
And the standalone emitter visibly responds (different particle count / color / motion) for at least 18 of the 22 presets within 60 frames of the cycle

### Requirement: `--vfxsys` CLI Flag Becomes No-Op

If a user passes `--vfxsys <path>` (legacy CLI flag for the removed
compound system), the demo MUST log a one-time deprecation warning
and continue startup normally without attempting to load the file.

#### Scenario: Legacy --vfxsys flag is gracefully ignored

Given the demo is launched with `--vfxsys vfx/anything.uzvfxsys`
When `OnStart()` runs
Then the log contains exactly one `[App] [warning]` line about `--vfxsys` being deprecated
And the rest of the startup proceeds identically to a no-arg launch
And no attempt is made to read the file at the supplied path

## REMOVED Requirements

### Requirement: Fireball Compound via VfxSystemComponent

The previous design (commit `5641dbb6`, 2026-04-03) created
`m_vfxEntity` with a `VfxSystemComponent` referencing
`vfx/systems/fireball.uzvfxsys`. The asset was never authored and the
compound never worked. This requirement is REMOVED. The demo MUST NOT
attempt to load any `.uzvfxsys` file in the default scene; in-code
preset functions handle every preset slot.

#### Scenario: No VfxSystemComponent created in default scene

Given the demo runs `OnStart()` in default mode (no `--vfx-asset`)
When the entity list is iterated
Then NO entity in the world has a `VfxSystemComponent` attached
