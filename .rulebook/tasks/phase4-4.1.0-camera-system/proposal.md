# Proposal: phase4-4.1.0-camera-system

## Why
UzEngine has a basic fly camera but no gameplay camera system. Third-person games need a SpringArm with collision avoidance, orbit cameras need smooth input-driven rotation, and cinematic cameras need spline paths. Camera shake and FOV interpolation are essential for game feel. This system provides all standard camera modes in a single configurable framework.

## What Changes
- SpringArm component: boom arm with length, socket offset, lag smoothing, sphere-cast collision
- Camera modes: ThirdPerson, FirstPerson, Orbit, Cinematic (spline-based)
- CameraManager for active camera selection and blending
- Camera shake with Perlin noise + exponential decay
- FOV interpolation (default ↔ ADS ↔ sprint)
- .uzcam preset file for camera configuration
- Lua API: uz.camera.set_mode, shake, set_fov, etc.

## Impact
- Depends on: phase4-4.0.0-enhanced-input-system (mouse/gamepad look), phase4-4.2.0-physics-jolt-core (sphere cast for collision)
- Modified: runtime/engine/renderer/camera/, runtime/engine/ecs/components/
- Affected spec: camera-system.md
- Breaking change: NO (fly camera still works)
