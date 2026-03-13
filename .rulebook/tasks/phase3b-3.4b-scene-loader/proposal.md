# Proposal: 3.4b — Scene Loader

## Why
Load .uzscene files into World. Headless-testable (no renderer needed). Hard gate for editor scene editing and the render bridge merge point.

## What Changes
- .uzscene JSON format: entities array with name + parent + components
- SceneLoader: parse JSON → create entities → deserialize via PropertySystem
- Parent-child hierarchy setup from scene file
- Scene save: World → .uzscene JSON roundtrip

## Impact
- Affected specs: `docs/engine/11-scene/`
- Affected code: `runtime/core/scene/`
- Breaking change: NO (new system)
- User benefit: Data-driven scenes, save/load, editor scene editing foundation
