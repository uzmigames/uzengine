# Post-process chain

## Purpose
The post-process passes and their CVars say what actually runs.

## ADDED Requirements

### Requirement: Inert post-process CVars are labelled inert
A CVar whose pass is not registered SHALL say so in its description, and a Lua setter for a CVar-backed renderer
member SHALL set the CVar.

#### Scenario: The lens flare toggle
Given uz.renderer.set_lens_flare_enabled(true)
When five and twenty frames have passed
Then r.LensFlare reads 1.

#### Scenario: No rendering change
Given the default and the all-off configurations of the reflection reference
When the frame is compared with the build before
Then it is bit-identical.
