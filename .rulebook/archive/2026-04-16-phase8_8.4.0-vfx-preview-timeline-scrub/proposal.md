# Proposal: phase8_8.4.0-vfx-preview-timeline-scrub

## Why
Authoring timing-critical effects (impact then flash then debris then smoke) without a seek/scrub timeline forces authors to watch the effect play in real time. Niagara exposes a scrubber with freeze + step-frame, cutting iteration time 5-10x on short effects.

## What Changes
Add a timeline widget with play / pause, scrub slider (0..maxDuration), frame-step buttons, and freeze toggle. Backed by a VfxPreviewClock the runtime reads in place of wall-clock dt when scrubbing.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/editor/vfx_editor_overlay, new vfx_preview_clock.h/.cpp
- Breaking change: NO
- User benefit: 5-10x faster iteration on timing-critical effects.
- Estimated effort: M

## Source
UE5 SNiagaraSystemViewportToolBar + FNiagaraTimelineController.
