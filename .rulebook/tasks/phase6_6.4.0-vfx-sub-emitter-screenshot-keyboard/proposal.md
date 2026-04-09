# Proposal: phase6_6.4.0-vfx-sub-emitter-screenshot-keyboard

## Why
The standalone demo has 40+ keyboard shortcuts, CPU sub-emitter on restart, screenshot
capture (F5), background color cycling, lit particle toggle, and VFX material cycling.
The Application has only 8 keyboard bindings and none of these features, making it a
significantly less capable authoring tool.

## What Changes
- CPU timer-based sub-emitter (secondary sparks on restart, E key toggle)
- F5 screenshot capture via Application::RequestScreenshot
- C key background color cycling (5 presets)
- J/B/N/Insert/H/F1-F8 keyboard shortcuts matching standalone
- M key VFX material variant cycling with globalColorTint

## Impact
- Affected code: vfx_viewport_app.cpp/.h
- Breaking change: NO (additive keyboard bindings)
- User benefit: Full standalone keyboard parity for VFX authoring workflow
