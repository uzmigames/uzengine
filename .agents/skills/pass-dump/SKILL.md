---
name: pass-dump
description: Dump all render pass outputs as PNGs + INDEX.html for visual bug diagnosis. ALWAYS use this FIRST when investigating any visual rendering bug. Reveals per-pass issues instantly.
---

Run pass-dump on a scene and analyze every render target visually.

## Usage

If user specifies a project, use it. Otherwise default to `pbr_showcase`.

```bash
cd e:/UzEngine
./runtime/build/bin/uzengine.exe --pass-dump --frames 200 \
  --project ./sample/projects/PROJECTNAME.uzproject --editor
```

Output goes to `.screenshots/pass-dump-f{N}/` with PNGs + INDEX.html.

## After Capture

1. List all generated PNGs in `.screenshots/pass-dump-f*/`
2. Read and visually inspect EACH key target using the Read tool:
   - `00-gbuffer-albedo.png` — base colors, no lighting. Dark = gamma bug.
   - `01-gbuffer-normal.png` — smooth gradients. Blocky = encoding error.
   - `06-gtao-result.png` — subtle grey. Half-screen dark = radius/depth bug.
   - `10-hdr.png` — fully lit before tonemap. Dark shadows = ambient/IBL missing.
   - `19-final-ldr.png` — after tonemap. Should match window output.
3. Shadow atlas and depth targets are skipped (depth format not readable).
4. Report which pass output looks wrong and what the likely cause is.

## Key Diagnostic Patterns

| Pass Output | Normal | Bug Indicator |
|-------------|--------|---------------|
| Albedo | Material colors | Too dark = sRGB/gamma issue |
| GTAO | Light grey, white = unoccluded | Dark rectangle = over-occlusion |
| HDR | Bright lit scene | Black shadows = ambient zero |
| Shadow atlas | Skipped (depth format) | Use --diag for shadow health |
| Final LDR | Tonemapped output | Washed out = exposure bug |

## Why This Exists

On 2026-03-30, "pitch black shadows" was investigated for hours via pixel-debug, 3-agent teams, and shader code reading. A single pass-dump revealed **GTAO over-occlusion** (AO ~0.4 on flat ground) in 2 seconds. Always dump first.
