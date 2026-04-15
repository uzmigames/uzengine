# Proposal: phase7_7.10.0-vfx-static-mesh-sampling-di

## Why
Niagara's Static Mesh DI samples the surface of a mesh asset - random triangles, closest point, UV-driven spawn. Enables effects like sparks covering a whole mesh surface (impact glows), debris bursts matching silhouettes, or paint splatter aligned to geometry. UzEngine has no such DI - only procedural primitives.

## What Changes
Add VfxStaticMeshDI that ingests a MeshAsset handle, uploads vertex/index/triangle-area CDF as structured buffers, exposes HLSL SampleSurface(rand) returning pos/normal/uv.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/
- Breaking change: NO
- User benefit: Mesh-silhouette-aware particle spawning (impact glows, surface scatter, paint splatter).

## Source
Reference: UE5 UNiagaraDataInterfaceStaticMesh + FNiagaraMeshVertexBuffer.
