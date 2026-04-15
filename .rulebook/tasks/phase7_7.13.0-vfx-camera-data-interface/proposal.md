# Proposal: phase7_7.13.0-vfx-camera-data-interface

## Why
Simulation shaders often need camera state (position, forward, fov, near/far, view matrix) for screen-space effects, LOD hysteresis, or billboard orientation math. Currently camera data only reaches the billboard VS via the mesh CB; the update/spawn CS paths have no access. Niagara's Camera DI exposes this everywhere.

## What Changes
Add a VfxCameraDI CB that any CS can bind: packs cameraPos, cameraForward, viewMatrix, projMatrix, fovY, nearZ, farZ, screenSize. Bound once per frame via the render graph.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/, renderer/frame/frame_particles.cpp
- Breaking change: NO
- User benefit: Camera-aware sim shaders (screen-space fade, LOD hysteresis, camera-oriented spawn).

## Source
Reference: UE5 UNiagaraDataInterfaceCamera.
