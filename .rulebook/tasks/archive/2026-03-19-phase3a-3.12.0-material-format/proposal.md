# Proposal: Material Format — .uzmat Files & Material Manager (Phase 3A)

## Why
The 0.1 POC had .uzmat INI-based material files with named shader assignment, PBR scalar/vector parameters, texture slot references, a MaterialManager with path-based caching, and auto-reload on disk change. Currently 0.2 hardcodes material parameters in demo code — there's no file format for materials, no manager, and no hot-reload. A proper material system is essential for: scene serialization (3.4b), content pipelines, and AI-driven material authoring. The format must be human-readable for LLM manipulation (key design goal).

## What Changes

### New Systems
1. **`.uzmat` file format** — Human-readable material definition with shader reference, PBR scalar parameters (baseColor, metallic, roughness, ao, emissive), and texture map paths (albedo, normal, roughness, metallic, AO, emissive)
2. **MaterialManager** — Singleton that loads, caches, and manages materials by path with reference counting
3. **Material hot-reload** — FileWatcher integration that detects .uzmat changes on disk and reloads affected materials without restart
4. **Material-to-GPU pipeline** — MaterialManager resolves texture paths to SRV indices and uploads PBRMaterial struct to MaterialBuffer
5. **Default materials** — Built-in fallback materials (default_lit, default_unlit) loaded from engine resources

### Refactoring
- Demo code replaces hardcoded PBR parameters with .uzmat file references
- ForwardPBRPass queries MaterialManager for per-object material data

## Impact
- Affected specs: `docs/engine/12-rendering/05-material-system.md`, `docs/engine/12-rendering/06-pbr-material-system.md`
- Affected code: `runtime/engine/renderer/material.h`, `runtime/engine/asset/`
- Breaking change: NO — existing hardcoded materials continue to work, .uzmat is additive
- User benefit: Define materials as files, hot-reload during development, AI-editable format
- Enables: Scene loader (3.4b) can reference materials by path, editor material inspector
