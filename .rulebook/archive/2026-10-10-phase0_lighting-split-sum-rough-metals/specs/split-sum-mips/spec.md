# Split-sum prefiltered mips

## Purpose
The lighting reads the prefiltered cubemap at the mip the bake wrote for the surface's roughness.

## ADDED Requirements

### Requirement: The mip follows the bake's convention
The deferred lighting SHALL select the prefiltered mip linearly in roughness for the engine's own bake
and SHALL keep the PMREM curve for PMREM environments, the renderer telling it which is bound.

#### Scenario: Rough metals under the sky
Given the roughness 0.3 and 0.5 metal floors lit by the engine's sky bake with the environment-MIS off
When their specular is compared with the CPU integral of the GGX BRDF over the dumped environment
Then the median ratio lies within [0.75, 1.3].

#### Scenario: Mirrors and PMREM unchanged
Given the chrome floor under the sky bake and under the PMREM
When their specular is compared with the build before
Then the median ratio lies within [0.95, 1.05].

### Requirement: The sky SH is E / π in the cubemap's frame
The packed sky SH SHALL evaluate to the cosine-weighted mean radiance (E / π) of the sky cubemap for
the normal expressed in the cubemap's frame, and SH the shader evaluates with the world normal SHALL be
converted with `SHCubemapToWorld`.

#### Scenario: Box tops under the sky
Given the dielectric boxes of the reflection reference lit by the sky SH alone
When their `diffuse_indirect / albedo` is compared with E / π of the dumped environment for the upward
normal
Then every channel lies within [0.85, 1.15] of it.

#### Scenario: Hemisphere light
Given a cubemap of radiance 1 over its upper hemisphere
When it is projected and the packed SH is evaluated at the up, down and sideways normals
Then E / π reads 1, 0 and 0.5 within 0.03.

### Requirement: Only the image-based specular changes
The change SHALL leave the positions and the direct light of every scene untouched.
Given Sponza Camera A and C
When the frames are compared with the build before
Then the positions and the direct light are identical.
