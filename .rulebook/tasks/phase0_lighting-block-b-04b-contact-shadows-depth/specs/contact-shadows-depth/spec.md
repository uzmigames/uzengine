# Contact Shadows Depth

## Purpose
The shared reversed-Z depth linearisation returns the view depth, and the screen-space contact
shadow march finds the occluders it was written for without darkening open surfaces.

## ADDED Requirements

### Requirement: Reversed-Z linearisation
`LinearizeDepthReversedZ` SHALL return near × far / (near + depth × (far − near)), the inverse of
the reversed-Z projection of `Mat4::Perspective`.

#### Scenario: Round trip
Given the device depth of a point at view depth z for near 5 and far 10000
When the function is evaluated
Then it returns z within 1e-5 relative, for z from 6 cm to 90 m.

### Requirement: The contact march compares view depths along a clip-space ray
The contact shadow march SHALL interpolate the ray from the receiver toward the light in clip
space and SHALL, at each sample, compare the scene's view depth with the ray point's view depth,
reporting a hit when a surface lies in front of the ray point by more than a self-occlusion bias and
less than the thickness.

#### Scenario: Box on a floor
Given a 40 × 40 × 12 cm box on a lit floor under a sun at 40 degrees, seen from 2.2 m
When the march runs with the default length and thickness
Then the agreement with the box's analytic shadow within 10 cm of its base does not fall below the
cascaded shadow maps' and closes at least half of their gap, and at most 0.2 % of the open lit
floor pixels lose more than a quarter of their shadow visibility.

#### Scenario: Sponza
Given the Sponza camera A frame with and without the march
When the darkened lit pixels are counted
Then they are at most 15 % of the lit pixels, at least 70 % of them lie within 8 px of a depth
step of at least 0.2 % of the view depth (1.3 times the rate over all lit pixels), and the
DeferredLighting pass costs at most 0.5 ms more.

#### Scenario: Committed build
Given the committed build before this change
When Sponza is rendered with the march on and off
Then the frames are identical (the march never hit), and this build with the march off reproduces
that frame.
