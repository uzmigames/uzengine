# Proposal: phase10_10.8.0-material-compiler-custom-node-api

## Why
Phase 9.13 (WPO material integration) needs the material compiler to accept a custom VS-stage injection point. The compiler today only emits a fixed shader template — no extension API exists for inserting authored HLSL splices. 8.18.0 already ships the splice emitters (`VfxWPOSpliceVS`, `VfxWPOSplicePrevVS`); they're waiting for the compiler hook.

## What Changes
Add a `MaterialCompilerExtension` interface: implementations provide `(insertionPoint, hlslSnippet)` tuples the compiler splices during codegen. Insertion points cover the VS (pre-viewProj for WPO current frame, prev-frame version for motion vectors), PS (post-lighting for emissive augmentation), and function scope (for helper functions). The WPO attribute becomes one such extension.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/material/compiler/*
- Breaking change: NO
- User benefit: Materials can author VS deformation + PS effects without hand-editing shader files.
- Estimated effort: L

## Source
UE5 FMaterialAttributes + FMaterialCompiler extension pattern.
