# Sponza recalibration

## Purpose
Sponza's sky-lit ambient is back at its calibrated level, with the factor in the scene file.

## ADDED Requirements

### Requirement: The sky share is restored by the scene
The three Sponza scenes calibrated with the old capture (the user's, the Camera C and the Camera A
harness scenes) SHALL carry the √3 the sky capture used to hide in their SkyLight's intensity and
indirect-lighting intensity (no path reads both: the SH fallback reads the indirect one, every other
sky-lit term the intensity), and the engine SHALL NOT change for it.

#### Scenario: The indirect knob with GI on
Given Sponza Camera A and C with the radiance cache and the final gather on
When the frames with √3 in both knobs are compared with the frames with √3 in the intensity only
Then positions and direct light are identical and the medians of the diffuse indirect, the specular
indirect and the colour agree within 0.5 %.

#### Scenario: The ambient level
Given Sponza Camera A and C
When the frames are compared with the captures of the build before the sky fix
Then the diffuse indirect and the colour are reported against them (the registered 5 % bound is not
met: the mesh-decal commit's occluder removal moved the level by × 1.4-1.7, the proposal's second
amendment), and against this binary's captures of the scene before the edit the sky-lit terms rise and
nothing else moves.

#### Scenario: Only the sky-lit terms move
Given Sponza Camera A and C
When the frames are compared with this binary's captures of the scene before the edit
Then the positions and the direct light are identical.
