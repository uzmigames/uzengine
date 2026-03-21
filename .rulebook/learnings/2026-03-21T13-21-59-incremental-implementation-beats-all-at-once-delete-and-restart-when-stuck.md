# Incremental implementation beats all-at-once — delete and restart when stuck
**Source**: manual
**Date**: 2026-03-21
**Tags**: implementation, debugging, rendering, methodology, critical-lesson
On 2026-03-20, volumetric clouds implementation failed when attempted all at once — spent an entire day fighting self-inflicted bugs in 1000+ lines of untested code. When forced to delete everything and reimplement step-by-step (solid color → ray intersection → constant density → noise → lighting), it worked perfectly in one session.

Key rules:
1. Start with the absolute minimum (solid color on screen)
2. Add ONE thing at a time, test visually after each step
3. Do NOT proceed if current step doesn't work
4. If stuck for 2-3 attempts on same problem, DELETE everything and restart from scratch
5. Research reference implementations BEFORE writing any code
6. The line between persistence and stupidity is thin — know when to abandon a failing approach

This applies to ALL complex features, not just rendering. The step-by-step template for rendering:
Step 1: Pipeline test (solid color output)
Step 2: Geometry (ray intersection / coordinate system)
Step 3: Basic content (constant density / gradient)
Step 4: Full content (noise / textures)
Step 5: Lighting (shadows / phase / ambient)
Step 6: Polish (temporal, detail, optimizations)