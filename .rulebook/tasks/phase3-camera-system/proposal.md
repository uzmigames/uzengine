# Proposal: phase3-camera-system

## Why

The engine currently uses a hardcoded view matrix with no camera component in the scene graph. This makes it impossible to implement any standard game camera behavior — third-person follow, first-person, cinematic cuts, or scripted camera paths. Every game genre requires a flexible camera system, and the absence of one blocks all gameplay camera work.

## What Changes

- Add `CCamera` scene node with FOV, near/far clip, projection mode (perspective/orthographic), and priority field for multi-camera blending
- Add `CSpringArm` node: target attachment, arm length, lag speed, and sphere-cast collision avoidance against the physics world
- Implement third-person, first-person, orbit, and cinematic camera modes selectable per `CCamera` node
- Add camera shake: Perlin noise offset applied to position/rotation with amplitude, frequency, and decay parameters
- Add depth-of-field parameters on `CCamera`: focal distance, focal range, bokeh intensity passed to post-process pass
- Define `.uzcam` preset file (INI-like): stores all camera parameters for instant scene application
- Expose full Lua API: `ctx.camera:set_active()`, `ctx.camera:shake()`, `ctx.camera:lerp_to_preset(name, duration)`
- Add IPC MCP tools `camera.set_active` and `camera.set_fov` for AI agent camera control during scene generation

## Impact

- Affected specs: `docs/specs/camera-system.md`, `docs/specs/scene-system.md`, `docs/specs/screen-space-effects.md`, `docs/specs/ai-native-features.md`
- Affected code: `src/scene/camera_node.cpp`, `src/scene/spring_arm_node.cpp`, `src/renderer/renderer.cpp`, `src/ipc/mcp_tools.cpp`
- Breaking change: YES — replaces hardcoded view matrix; existing IPC `setRotation` demo will need camera node in scene
- User benefit: Enables all standard game camera modes out of the box, allowing developers to implement any camera behavior without writing engine-level code
