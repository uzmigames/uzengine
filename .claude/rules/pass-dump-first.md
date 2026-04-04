# Pass Dump FIRST — Before ANY Visual Bug Investigation

## Rule (applies to ALL agents touching rendering)

When a visual bug is reported (wrong colors, dark shadows, missing effects, artifacts):

**STEP 1: Run `--pass-dump` BEFORE anything else.**

```bash
uzengine.exe --pass-dump --frames 200 --project <project> --editor
```

Output: `.screenshots/pass-dump-f{N}/` with PNGs of every render target + INDEX.html

**STEP 2: Open INDEX.html and visually inspect EACH pass output.**

Look for:
- **Albedo**: correct base colors? gamma issues?
- **Normal**: smooth gradients? encoding artifacts?
- **Shadow atlas**: geometry visible? all black?
- **GTAO/AO**: reasonable occlusion? over-darkening?
- **HDR (before tonemap)**: lit correctly? ambient present?
- **Final LDR**: after tonemap — matches expectation?

**STEP 3: Identify WHICH pass produces wrong output.** The bug is in THAT pass or its inputs.

## Why This Rule Exists

On 2026-03-30, a "pitch black shadows" bug was investigated for hours via pixel-debug, agent teams, and shader code reading. The root cause was **GTAO over-occlusion** (AO ~0.4 on flat ground). A single `--pass-dump` revealed this in 2 seconds by showing the GTAO result pass as a dark rectangle covering half the screen.

**Pass-dump is the most powerful diagnostic tool. Use it FIRST, not last.**

## Available Diagnostic Commands (in order of usefulness)

| Priority | Command | What it shows | When to use |
|----------|---------|---------------|-------------|
| **1st** | `--pass-dump` | ALL passes as PNGs + INDEX.html | **ALWAYS first** for visual bugs |
| 2nd | `--diag` | JSON health check (NaN/Inf/zero per target) | Quick automated check |
| 3rd | `--vis <target>` | Fullscreen view of one target | Zoom into a specific pass |
| 4th | `--pixel-debug X Y` | 13 GBuffer values at one pixel | Numerical verification |
| 5th | `--view-mode <mode>` | GBuffer channel visualization | Quick channel inspection |
| 6th | `--pass-toggle <name>` | Disable a pass to isolate | A/B comparison |

## FORBIDDEN

- Investigating visual bugs by reading shader code first
- Using pixel-debug as the primary diagnostic tool
- Guessing at the problem without running diagnostics
- Editing shaders to add debug output (`return float4(value,0,0,1)`)
