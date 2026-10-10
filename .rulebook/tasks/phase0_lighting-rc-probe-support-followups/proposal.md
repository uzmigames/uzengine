# Radiance-cache probe support in the shipped configuration

## Why

sample/projects/sponza.uzproject ships ProbeClassification 3 + ReachRelocate + ReachVisibleSeeds since 6ee6163f,
but scripts/rendering/test_probe_classification.py covers rules 0 and 1 in the QA configuration only; far probes across walls
(the cache query has no visibility test) were never measured on a reference with an opening; P2 was never measured
against an irradiance solution; the H-71 hang's second suspect (the G-buffer position read the cache pass declares
for the visible seeds) was never examined, and the DDGI diagnostic readback still waits idle; the wall-flicker
candidate of H-62b was never tested in motion.

Origin: followup-01 close-out (2026-10-08), item 6.4's open points.

## What Changes

- A pass/fail harness for the shipped configuration in the closing sweep (reference R3r, P1, P3, C; Sponza S2/S3).
- A reference wall with an opening for the far-probe question; measure, then decide.
- P2 against an irradiance solution of the reference.
- The H-71 hang: the G-buffer position read; the DDGI readback without WaitIdle (the 4-frame guard the radiance cache uses).
- H-62b's wall-flicker candidate on the shipped configuration in motion, with rc_support recorded per frame.

## Impact
- Affected specs: lighting-rc-probe-support-followups (new)
- Affected code: see the items; gates are pre-registered per item before its first run
- Breaking change: NO until a gate passes
- User benefit: the open points of the closed followups have an owner and a plan.
