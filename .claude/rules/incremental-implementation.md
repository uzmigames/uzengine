# Incremental Implementation — Step by Step, Test Each Step

## Rule

When implementing any complex feature (rendering pass, new system, multi-file change):

1. **Start with the absolute minimum** — e.g., write a solid color to a texture and verify it shows on screen
2. **Add ONE thing at a time** — each step should be independently testable
3. **Build + test visually after EACH step** — do NOT proceed if the current step doesn't work
4. **Research before coding** — read reference implementations, understand the full pipeline BEFORE writing any code
5. **If stuck for 2-3 attempts, DELETE and restart from scratch** — try a different approach entirely

## Why

On 2026-03-20, the volumetric clouds implementation failed when attempted all at once — spent an entire day fighting self-inflicted bugs in 1000+ lines of untested code. When forced to delete everything and reimplementing step-by-step (solid color → ray intersection → constant density → noise → lighting), it worked perfectly in one pass.

**The line between persistence and stupidity is thin.** Debugging code you wrote wrong is slower than rewriting it correctly.

## Step Template for Rendering Features

```
Step 1: Pipeline test — solid color output, verify pixel appears on screen
Step 2: Geometry — ray intersection / UV / coordinate system, verify correct area
Step 3: Basic content — constant density / simple gradient, verify visual shape
Step 4: Full content — noise / textures / real data, verify quality
Step 5: Lighting — shadows / phase / ambient, verify brightness variation
Step 6: Polish — temporal, detail noise, optimizations
```

## Anti-Patterns (FORBIDDEN)

- Writing 500+ lines of shader code without testing any of it
- "I'll fix it after I finish writing everything"
- Adding complexity to debug a problem instead of simplifying
- Spending 3+ attempts on the same approach without stepping back
- Guessing at coordinate systems instead of reading existing working code