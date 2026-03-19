# Advanced Post-Processing — Spec Delta

## ADDED Requirements

### Requirement: GTAO Screen-Space Ambient Occlusion
The engine MUST implement Ground Truth Ambient Occlusion using horizon-angle integration at half resolution with bilateral blur upsampling and temporal filtering.

#### Scenario: Corner Occlusion
Given two perpendicular surfaces meeting at a corner
When GTAO evaluates the contact region
Then occlusion values SHALL be high (dark) at the crease
And occlusion SHALL fall off smoothly away from the corner

#### Scenario: Open Sky
Given a flat surface with no nearby geometry
When GTAO evaluates the surface
Then occlusion SHALL be near 1.0 (no darkening)

### Requirement: Screen-Space Reflections
The engine MUST implement Hi-Z accelerated ray marching for screen-space reflections with roughness-based mip selection, Fresnel weighting, and screen-edge fade.

#### Scenario: Mirror Surface
Given a surface with roughness 0.05
When SSR ray marches from the surface
Then reflected geometry SHALL be sharp and clearly visible

#### Scenario: Rough Surface Cutoff
Given a surface with roughness above 0.7
When SSR evaluates
Then SSR contribution SHALL be zero (fall back to IBL probes)

### Requirement: Temporal Anti-Aliasing
The engine MUST implement TAA with Halton 2,3 jitter sequence, velocity-based history reprojection, YCoCg neighborhood clamping, and configurable current/history blend ratio.

#### Scenario: Static Scene Convergence
Given a static camera and scene with TAA active
When 8 or more frames accumulate
Then aliased edges SHALL be visually smooth
And the image SHALL be temporally stable (no flickering)

#### Scenario: Fast Motion Rejection
Given a fast-moving object crossing the screen
When TAA reprojects history
Then ghosting artifacts SHALL be rejected via velocity-based clamping

### Requirement: Depth of Field
The engine MUST implement thin-lens depth of field with CoC (Circle of Confusion) computed from focus distance, aperture, and focal length, applied via separable blur weighted by CoC radius.

#### Scenario: Focused Subject
Given focus distance matching the subject distance
When DoF is applied
Then the subject SHALL remain sharp (CoC near zero)
And background/foreground SHALL be blurred proportionally to depth difference

### Requirement: Auto-Exposure
The engine MUST implement histogram-based auto-exposure with exponential convergence from current to target EV, supporting configurable min/max EV range and adaptation speed.

#### Scenario: Dark-to-Light Transition
Given camera moves from a dark interior to bright exterior
When auto-exposure adapts
Then exposure SHALL converge toward the bright target EV
And convergence SHALL take multiple frames (smooth transition, not instant)

### Requirement: PostProcessVolume Blending
The engine MUST support overlapping PostProcessVolume AABBs with priority-based blending, where the camera position determines blend weight and higher-priority volumes override lower ones.

#### Scenario: Entering Volume
Given camera at the boundary of a PostProcessVolume
When camera moves inside
Then effect parameters SHALL interpolate from outside to inside values
And transition SHALL be smooth (no popping)
