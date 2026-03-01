# Phase 3 — Animation System

## Goal

Full animation pipeline: clip sampling, state machine, blending (full-body/layered/additive), root motion, AnimNotify events (audio, VFX, scripts), two-bone IK, FABRIK, and the `.uzanim_sm` state machine format.

## Spec Reference

[Animation System Spec](../../../docs/specs/animation-system.md)

## Dependencies

- phase3-skeletal-mesh (bone palette, pose evaluation)
- phase3-input-system (state machine conditions driven by input params)
