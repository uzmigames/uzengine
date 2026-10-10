# The in-engine PT golden was fake (sky-only, no occlusion/bounces) — every GI calibration chased it; real PT implemented, bounce validation pending
**Source**: manual
**Date**: 2026-07-27
**Related Task**: phase0_lumen-lite-gi-core
**Tags**: gi, path-tracer, ground-truth, debug-tooling, lighting
path_tracer_ps.hlsl "indirect" was skyCube.SampleLevel with NO occlusion and NO bounces — the 2400spp goldens gave full unoccluded sky to interior pixels and zero sun bounce. The "2pi units deficit" (GI.Intensity optimum 6.5) was an artifact: vs the REAL PT, intensity 1.0 scores 10.45 dB and 6.5 scores 5.58 dB. NEVER trust `.screenshots/gi-plan/golden-{a,b,c}.png` (old); golden3-a.png is the new reference (commit 0310cb9b).

Implemented (0310cb9b): real PT (RayQuery 4-bounce, sun NEE shadow rays at hits, gi_trace_common.hlsli shared with DDGI writer, FillGITraceHitConstants C++ helper, b2 built in the pass EXECUTION callback — PerFrameCB cpuPtr is NOT populated at graph-build time); r.Lighting.DebugMode 1-7 (direct/indirect/E/SSGI/AO/source-map/skyVis) capturable via --hidden --cvar; SSGI no longer double-adds over world GI.

Measured with the new tools (pose A --camera-pos 36,-611,102 --camera-look 36,400,60, overlays off): DDGI feeds ~whole frame (source map green); raster indirect uniformly brighter than PT truth (90.6% pixels >20 luma); floor patch (600-800,850-950): truth 1.4, raster 58, all from indirect (direct=0, SSGI=0), E(m3)=167 tonemapped.

OPEN INVESTIGATION: PT floor 1.4 vs analytic sun-strip-bounce estimate ~20 luma — PT bounce chain suspect. Facts: PT-no-TLAS floor=33 (cubemap is bright, so sky path works); graphics kSlotPerObject→b2 works engine-wide (fog/CAS/FXAA); DDGI compute with same b2 layout works; shader IS recompiling (behavior changed). Next: fix --pixel-debug returning 0 records in --editor mode, then instrument b2 contents in the PT pass. Also suspect: DDGI probes' brightness may come from flat-white sky fallback (skyCubemapSrv sentinel in frame_gi_passes fill?) vs PT sampling the real dim cubemap — check m_skyCubemapSrv validity at RegisterSDFGIPasses time. Raster recalibration BLOCKED until PT bounce validates.