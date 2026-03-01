# Phase 2 — Asset PAK Format & Virtual Filesystem

## Goal

Implement the `.uzpak` packaging format (memory-mappable, footer-at-end, per-file zstd compression, hash-indexed directory). Implement a Virtual Filesystem that mounts PAKs + loose files with priority override. Used for shipping distribution and future patch support.

## Spec Reference

[Asset Pipeline — PAK Format](../../../docs/specs/asset-pipeline.md#6-asset-pak-format-uzpak)
