# Environment-MIS specular

## Purpose
The environment-MIS pass estimates the specular environment term of smooth metals without bias, with
the frame's constants, and hands the lighting a coverage so the pixels it does not cover keep the
split-sum term.

## ADDED Requirements

### Requirement: The estimate is the specular environment integral
The pass SHALL estimate the specular environment term with environment sampling and visible-normal
BRDF sampling combined by the balance heuristic, with a solid-angle environment pdf, and SHALL read the
camera position and the sky light tint from the frame's PerFrameCB bound for its dispatch.

#### Scenario: A mirror reflects the sky
Given the reflection reference (chrome floor, SSR off) and the camera mirrored through the floor plane
When the floor pixels whose reflected ray sees the sky are compared with F x the mirrored view
Then the median relative error is at most 0.15.

#### Scenario: Energy on rough metals
Given the metal floors at roughness 0.3 and 0.5 and the environment the sampler built from
When the estimate is compared with the CPU integral of the GGX BRDF over that environment
Then the median ratio lies within [0.85, 1.15].

#### Scenario: An HDRI environment
Given the chrome floor under the ballroom PMREM environment (reflection-reference-hdri)
When the floor's specular is compared with F x the dumped environment along the reflected ray
Then the median relative error is at most 0.2.

### Requirement: Coverage replaces, never discards
The pass SHALL write its coverage (1 on metals below the roughness threshold, fading to 0 across the
thresholds, 0 elsewhere) in alpha, and the lighting SHALL blend the estimate over the split-sum
specular by that coverage.

#### Scenario: Dielectrics keep the split-sum specular
Given the reflection reference's rough dielectric boxes
When their specular is compared between r.EnvMIS 1 and r.EnvMIS 0 over the boxes' interior
Then it is the same (max |diff| at most 1e-4) and not zero.

#### Scenario: Scenes with the pass inactive are unchanged
Given Sponza Camera C with r.GI.Reflections 1
When the frame is compared with the committed capture
Then positions and direct light are identical and the colour lies within the loading-state envelope.
