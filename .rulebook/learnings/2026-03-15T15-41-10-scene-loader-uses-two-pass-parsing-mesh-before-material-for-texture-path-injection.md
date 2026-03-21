# Scene loader uses two-pass parsing: mesh before material for texture path injection
**Source**: manual
**Date**: 2026-03-15
**Tags**: scene, loader, json, two-pass, hierarchy, parsing
The JSON scene loader processes components in two passes: first pass creates entities and attaches structural components (transform, camera, light, mesh, script), second pass processes material components which write texture paths into already-existing StaticMeshComponent. This ordering is necessary because 'material' JSON block needs to inject albedoPath, normalPath, etc. into the mesh component that must already exist. Hierarchy is handled recursively via 'children' array with SceneComponent::AttachTo(). Unknown component keys are logged as warnings but don't fail the load.