# Phase 3 — GPU Skinning & Morph Targets

## Goal

Implement GPU skinning: bone palette uploaded as StructuredBuffer, skinned vertex shader applies weighted bone transforms. Morph target accumulation via compute shader before skinning. Physics Asset editor for per-bone collider authoring.

## Spec Reference

[Skeletal Mesh Spec](../../../docs/specs/skeletal-mesh.md)

## Dependencies

- phase2-fbx-import (skeletal mesh binary format)
- phase2-pbr-renderer (vertex shader integration)
