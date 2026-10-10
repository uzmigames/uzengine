# SSR exposure

## Purpose
Screen-space reflections read the scene colour in the units it is stored in.

## ADDED Requirements

### Requirement: No exposure ratio on a linear scene colour
The SSR trace SHALL read the previous frame's scene colour without scaling it by an exposure ratio, because
the engine's HDR target is linear and not pre-exposed.

#### Scenario: Auto-exposure adapting
Given SSR on and auto-exposure adapting after an exposure step
When a pixel's reflection ray hits the previous frame's scene colour
Then the hit colour equals that scene colour within 0.5 % on every frame of the adaptation.
