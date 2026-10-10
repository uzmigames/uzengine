# Proposal: Validate flicker capture and measurement

## Why
Previous PNGs were shifted by one render frame and sparse sampling over-counted shadow changes. Trustworthy elimination tests require verified frame identity, readback lifetime, coordinate space and radiometric units.

## What Changes
Create reproducible elimination experiments, full-resolution evidence and a hypothesis ledger. Execute checklist items in order. This task is diagnostic until a correction is explicitly selected in task 14.

## Dependencies
Existing followup-08 instrumentation; verify it rather than trusting its earlier conclusions must finish before this task starts. Earlier evidence is reusable only with verified frame alignment and compatible settings.

## Impact
- Affected specs: rendering-flicker-validation
- Affected code: runtime/tools/uzengine.cpp; renderer frame, GI and TAA paths; scripts/ diagnostic harnesses.
- Breaking change: NO
- User benefit: attribute visible flicker without hiding it through brightness changes, blur or stale history.
- Existing tracking: followup-08 contains historical instrumentation; followup-06 retains reflection filtering; followup-01 retains DDGI/SDF energy parity. Do not duplicate or silently close those criteria.
