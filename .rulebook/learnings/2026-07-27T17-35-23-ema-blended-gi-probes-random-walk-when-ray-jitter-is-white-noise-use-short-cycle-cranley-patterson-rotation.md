# EMA-blended GI probes random-walk when ray jitter is white noise — use short-cycle Cranley-Patterson rotation
**Source**: manual
**Date**: 2026-07-27
**Related Task**: phase0_lumen-lite-gi-core
**Tags**: gi, ddgi, radiance-cache, temporal-stability, flicker, diagnostics
Symptom: "lighting flickers/pisca" on Sponza with RC+DDGI on. Measured signature: frame-diff grows monotonically with temporal distance (d1=1.07, d2=1.50, d4=2.72, d40=3.57 mean luma) — a RANDOM WALK, not a strobe (period hypotheses ruled out: same-phase d3 pairs were LARGER than d2). DDGI atlas --vis diffs were exactly 0 (8-bit vis quantization hides small HDR wander — don't trust vis PNG diffs for sub-1% instability).

Root cause (ddgi_probe_update_cs.hlsl + radiance_cache_trace_cs.hlsl): `Hammersley(i + (LCG(frame) & 0xFFFF), nRays + (LCG(frame) & 0xFF))` — (a) white-noise jitter makes every frame's estimate statistically independent so the EMA state never stations, it random-walks with amplitude ~sigma*sqrt(alpha/(2-alpha)) forever; (b) varying the Hammersley DENOMINATOR per frame changes the sample distribution's bias per frame — bias wobble passes through any EMA.

Fix: fixed Hammersley set of nRays + per-frame Cranley-Patterson toroidal shift cycling over 16 phases (R2 constants 0.75487767/0.56984029, @ref Roberts 2018). Periodic input → EMA converges to a stationary limit cycle of amplitude ~alpha*sigma. Measured after: d1=0.19, d40=0.36, floor wander gone (10x). Commit c0fe1c0a.

Also: isolation matrix via --cvar toggles found it (RC=0 stable / DXR=0 mild / both=flicker), BUT note r.RadianceCache=0 also disables the DDGI dispatch block (it lives inside RegisterSDFGIPasses) — that test isolates "static atlases", not "DDGI alone". And uzengine.exe now has --hidden (HWND without ShowWindow) for captures that don't steal focus — --headless remains a device smoke test that never renders.