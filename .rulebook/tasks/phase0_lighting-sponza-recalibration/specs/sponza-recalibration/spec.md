# Sponza recalibration

## Purpose
Sponza's sky-lit ambient is back at its calibrated level, with the factor in the scene file.

## ADDED Requirements

### Requirement: The sky share is restored by the scene
The three Sponza scenes calibrated with the old capture (the user's, the Camera C and the Camera A
harness scenes) SHALL carry the √3 the sky capture used to hide in their SkyLight's intensity (the
indirect-lighting intensity stays 1: the SH fallback and the fog read both), and the engine SHALL NOT
change for it.

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
