# Froxel Fog Correctness

## Purpose
The froxel volumetric fog places its froxels along the camera rays at the slice depths, lights them
with the sun once and with the sun's shadow, and its constant buffers mean the same bytes on both
sides of the GPU boundary.

## ADDED Requirements

### Requirement: Constant buffers agree
Every constant buffer shared between C++ and HLSL in the fog and cloud passes SHALL have the same
field offsets on both sides, and the layout SHALL be checked by a harness that reads the offsets
from the shader compiler.

#### Scenario: A float3 that HLSL would pad
Given a C++ struct places three floats at a 4-byte-aligned offset
When the shader declares them
Then it declares three scalars, and the harness reports the same offsets for both sides.

### Requirement: Froxels lie on the camera rays
The inject shader SHALL compute each froxel's world position as the camera position plus the
pixel's ray direction scaled to the slice's view depth, so that a froxel at slice depth d is at
view-space depth d.

#### Scenario: Sponza camera
Given near 5 cm, far 10000 cm and the fog distance of 64 m (the component's centimetres, no longer multiplied by 100)
When slice 63 is placed
Then its view-space depth is the slice depth (tens of metres), not tens of centimetres.

### Requirement: The composite reads the volume at the pixel's depth
The composite SHALL convert the reversed-Z device depth to the view depth with the finite-far-plane
inverse (near × far / (near + depth × (far − near))) and SHALL expose, under a debug switch, the depth,
the slice coordinate and the transmittance it used, so a capture can verify them per pixel.

#### Scenario: Floor at 3-30 m
Given the fog shaft reference and `r.VolumetricFog.Debug 1`
When the written depth is compared with the view depth of the dump's world position per floor pixel
Then the median relative error is at most 3 %, and the depth_common formula evaluated for the same
pixels is off by at least 90 % (it returns about the near plane).

### Requirement: The sun is applied once and shadowed
The sun in-scatter SHALL use the sun colour (which carries the intensity) once, multiplied by the
cascaded shadow factor at the froxel, with a switch to disable the shadow for comparisons.

#### Scenario: Shadowed floor
Given Sponza with the fog enabled and the sun shadow on
When the fog in-scatter over sun-shadowed floor is compared with the shadow off
Then it drops by at least 40 %.

#### Scenario: Analytic shadow volume
Given the fog shaft reference (a floor, one wall, the sun at 40 degrees, uniform fog)
When the fog in-scatter with the shadow on is compared with the shadow off per pixel
Then rays that never cross the wall's shadow keep at least 90 % of it, rays crossing it follow the
scatter integral with the analytic visibility within a mean absolute error of 0.1, and the analytic
footprint matches the rendered floor shadow on at least 95 % of the classified floor pixels.
