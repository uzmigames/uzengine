# Mesh decals

## Purpose
A mesh whose material graph has the Decal domain blends its colour into the G-buffer by its opacity and
changes nothing else: no depth, no normal, no position, no shadow.

## ADDED Requirements

### Requirement: A Decal-domain mesh blends its albedo by opacity
The G-buffer pass SHALL draw Decal-domain meshes after every opaque and masked draw with the albedo
target blended SrcAlpha / InvSrcAlpha by the graph's opacity, the normal, position and emissive
targets and the depth unchanged, and the shadow passes SHALL NOT draw them.

#### Scenario: The blend identity
Given the mesh-decal reference (a grey wall, a decal quad with an opacity ramp, the wall alone as the
control)
When the lit colour along the ramp is compared with lerp(control, decal, alpha)
Then the median relative deviation is at most 0.03 and the fit's R² at least 0.99.

#### Scenario: No geometry, no shadow
Given the same reference
When the G-buffer under the decal and the direct light beside it are read
Then the positions lie on the wall plane (within 0.5 cm), the normals are the wall's, and the direct
light outside the footprint equals the control's (max |diff| at most 1e-4).

#### Scenario: Sponza
Given Sponza Camera A and C with `dirt_decal.uzmat` in the Decal domain
When the frames are compared with the build before
Then the positions change only inside the decal meshes' screen bounds and the G-buffer pass costs
within 10 % of before.
