# Volumetric Effects — Spec Delta

## ADDED Requirements

### Requirement: Cloud Noise Baking
The engine MUST bake 3D noise textures (shape 128^3, detail 32^3) and a 2D weather map (512x512) via compute shaders, cached to disk after first generation.

#### Scenario: First Run Bake
Given no cached noise textures exist
When the engine initializes with VolumetricClouds enabled
Then noise textures SHALL be baked via compute shaders
And results SHALL be cached for subsequent runs

### Requirement: Cloud Ray Marching
The cloud renderer MUST ray-march through a configurable altitude band sampling 3D noise for density, applying Beer-Lambert absorption for light extinction, and accumulating in-scatter with a multi-scatter ambient approximation.

#### Scenario: Zero Coverage
Given VolumetricClouds with coverage set to 0.0
When the cloud pass executes
Then all cloud pixels SHALL be fully transparent

#### Scenario: Full Coverage
Given VolumetricClouds with coverage set to 1.0 and density 0.5
When the cloud pass ray-marches
Then the cloud layer SHALL be opaque with visible density variation from noise

### Requirement: Cloud Temporal Filtering
The cloud renderer MUST apply temporal reprojection and accumulation to denoise the ray-marched output, using motion vectors for history rejection on camera movement.

#### Scenario: Static Camera Accumulation
Given a static camera viewing clouds
When 4+ frames accumulate
Then cloud noise SHALL be visually smooth (temporal convergence)

### Requirement: God Rays
The engine MUST implement screen-space god rays via radial blur from the sun's screen position with configurable intensity, decay, and density parameters.

#### Scenario: Sun Visible
Given the sun is visible on screen with god rays enabled
When the god rays pass executes
Then radial light shafts SHALL emanate from the sun position
And shafts SHALL be occluded by scene geometry

#### Scenario: Sun Off-Screen
Given the sun is outside the viewport
When the god rays pass executes
Then no god rays SHALL be rendered (pass culled)
