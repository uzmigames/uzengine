# VFX Application Feature Parity with Standalone Demo

## ADDED Requirements

### Requirement: Billboard Rendering Feature Passthrough
The VFX Application's inline dispatch and billboard render pass SHALL pass all VFXBillboardCB fields from the emitter configuration to the billboard vertex and pixel shaders. This MUST include emissive intensity, soft fade, color/size over lifetime, facing modes, stretching, and blend mode selection.

#### Scenario: Emissive glow particles
Given an emitter with emissiveIntensity=2.0 and white initialColor
When the billboard PS renders the particle
Then the output color SHALL include additive emissive contribution creating a visible glow effect

#### Scenario: Additive blend mode
Given an emitter with blendMode=1 (Additive)
When the billboard render pass selects the PSO
Then it MUST use the Additive blend PSO (SrcColor=One, DstColor=One) instead of AlphaBlend

### Requirement: Simulation Force Passthrough
The VFX Application's update shader dispatch SHALL pass all force parameters from the emitter configuration to the update compute shader CB. This MUST include curl noise, vortex force, wind zones, ground collision, orbit, drag, and death spawn parameters.

#### Scenario: Curl noise smoke effect
Given an emitter with curlStrength=2.0 and curlFrequency=0.5
When the update compute shader processes alive particles
Then particle velocities SHALL be perturbed by 3D curl noise producing organic smoke-like motion

#### Scenario: Ground collision bounce
Given an emitter with groundCollision=1 and restitution=0.5
When a particle's Z position reaches 0 (ground plane)
Then the particle velocity Z component SHALL be reflected with restitution factor 0.5

### Requirement: Spawn Shape Configuration
The VFX Application SHALL support all 8 spawn shapes (Point, Sphere, Box, Ring, Cone, Cylinder, Disc, Line) configurable per emitter via spawnShape and shape parameters.

#### Scenario: Sphere spawn shape
Given an emitter with spawnShape=1 and spawnRadius=200
When particles are spawned
Then each particle position SHALL be randomized within a 200cm radius sphere around the emitter origin

### Requirement: Preset Library
The VFX Application SHALL include a preset library with at least 20 emitter configurations matching the standalone demo presets (fire, explosion, smoke, sparks, magic, rain, snow, etc.).

#### Scenario: Fire preset selection
Given the preset library is loaded
When the user selects the "Fire" preset
Then the emitter configuration SHALL match the standalone demo's fire preset parameters

### Requirement: Multi-Emitter Compound Effects
The VFX Application SHALL support compound effects composed of multiple simultaneous emitters with independent configurations and delayed start timers.

#### Scenario: Explosion compound effect
Given a compound preset "Explosion" with 3 sub-emitters (burst, smoke, debris)
When the compound effect is activated
Then all 3 emitters SHALL spawn particles with their individual configurations and timing offsets

### Requirement: Editor UI
The VFX Application SHALL provide a clickable UI overlay for emitter parameter editing, preset selection, and module stack toggling, using the engine's TextRenderer system.

#### Scenario: Preset selector dropdown
Given the editor UI is visible
When the user clicks the preset dropdown
Then a list of available presets SHALL be displayed and selectable
