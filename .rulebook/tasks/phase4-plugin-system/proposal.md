# Phase 4 — Plugin / Extension System

## Goal

A stable plugin system that allows third-party C++ DLLs and VSCode extensions to extend UzEngine without modifying engine source. Plugins are distributed as `.uzplugin` zip archives, declare capabilities in a TOML manifest, and communicate via a stable C ABI (`UzPluginAPI_t`). DLL crashes are isolated via Windows SEH. Four official optional plugins (`uz_terrain`, `uz_cinemachine`, `uz_dialogue`, `uz_tilemap`) will ship disabled by default, loadable on demand.

## Spec Reference

[Plugin / Extension System Spec](../../../docs/specs/plugin-system.md)

## Dependencies

- phase4-build-pipeline (plugin DLLs must be built and packaged in the shipping binary's plugin directory)
- phase1-scene-system (plugins register new node types via the stable C ABI)
- phase1-shader-hot-reload (DLL hot-reload reuses the shadow-copy hot-reload infrastructure)
