# Cluster spot cone

## Purpose
A spot light occupies the clusters its cone can light, and nothing else changes.

## ADDED Requirements

### Requirement: Spots are assigned by their cone
The light cull SHALL keep a spot light in a cluster only when its cone reaches the cluster (a
conservative test), and the lighting SHALL be identical to the range-sphere assignment.

#### Scenario: Identity
Given the lights reference and its dense variant
When the frame with the cone test is compared with the sphere-only assignment on the same binary
Then taa_output and direct_light are identical (max |diff| at most 1e-5).

#### Scenario: Fewer slots
Given the dense variant (100 spots)
When the mean cluster light count over lit pixels is compared
Then it drops by at least 20 % with the cone test.

#### Scenario: Cost
Given the dense variant
When the LightCull and DeferredLighting passes are timed
Then LightCull costs at most 0.05 ms more and DeferredLighting is not slower (at most 1.02 x).
