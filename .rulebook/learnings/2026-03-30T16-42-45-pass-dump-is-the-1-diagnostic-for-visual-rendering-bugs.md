# Pass-dump is the #1 diagnostic for visual rendering bugs
**Source**: manual
**Date**: 2026-03-30
**Tags**: rendering, debugging, pass-dump, diagnostics, GTAO, shadows, visual-bugs, best-practice
On 2026-03-30, a "pitch black shadows" bug in the FBX test scene was investigated for hours using pixel-debug, 3-agent investigation teams, and shader code reading. Multiple hypotheses were explored (CB ring stale data, IBL SRV sentinel guard, skylight intensity, energy conservation). None revealed the true root cause.

A single `--pass-dump` command revealed the problem in 2 seconds: the GTAO result pass showed AO ~0.4 across the entire ground plane (should be ~1.0 for a flat unoccluded surface). This over-occlusion was darkening ambient by 60%, making shadows appear pitch black.

LESSON: ALWAYS run `--pass-dump` FIRST before any other diagnostic when investigating visual rendering bugs. The pass-dump generates PNGs of every render target with an INDEX.html gallery. Visually inspecting each pass output immediately reveals which pass produces wrong data.

Command: `uzengine.exe --pass-dump --frames 200 --project <project> --editor`
Output: `.screenshots/pass-dump-f{N}/` with PNGs + INDEX.html

Diagnostic priority order:
1. --pass-dump (visual inspection of ALL passes)
2. --diag (JSON health check)
3. --vis <target> (zoom into one pass)
4. --pixel-debug X Y (numerical values at one pixel)
5. --pass-toggle <name> (A/B comparison)

NEVER: read shader code first, use pixel-debug as primary tool, or guess at the problem.