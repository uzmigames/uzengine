# Phase 4 — Build Pipeline (Cook, Pack, Stage, Package)

## Goal

Full game export pipeline: incremental asset cooking (BC7 textures, mesh optimization, shader pre-compilation), PAK packaging, shipping binary (no editor code), Windows NSIS installer via CPack, platform abstraction layer ready for Linux/macOS/Android.

## Spec Reference

[Build System Spec](../../../docs/specs/build-system.md)

## Dependencies

- phase2-texture-tools, phase2-fbx-import, phase2-asset-pak (all cooking produces outputs for the PAK)
