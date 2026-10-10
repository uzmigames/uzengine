# Design: DDGI dedup

## Before
`ddgi_probe_update_cs.hlsl` was dispatched twice per frame (irradiance atlas, depth atlas), one
thread per atlas texel. Each thread decoded its texel's octahedral direction, then traced ALL of its
probe's rays (`raysPerProbe`, 32) with an inline `RayQuery`, a shadow ray per lit hit and seven
distance-field samples per hit (six for the normal, one for the albedo), and integrated them with the
cosine weight `max(dot(texelDir, rayDir), 0)` (squared for the depth moments). A probe has 36
irradiance texels and 256 depth texels, so every ray was traced 292 times. The TLAS was rebuilt
every frame from the same instance descriptors. The DDGI hit radiance was `albedo × sunRad / π`
alone; the SDF writer's is `albedo × (sunRad / π + indirect)` with `indirect` from the previous
frame's probes (`SamplePreviousDiffuse`).

## After
1. `ddgi_probe_trace_cs.hlsl`, `[numthreads(64,1,1)]`, thread `t = probe × nRays + i`: traces ray i
   of the probe (`ProbeRayDirection(i, nRays, probe, frame)`: a full stratified Hammersley set rotated
   per frame and per probe, the construction both shaders used already), shades the hit, adds the
   indirect term, writes `rayRadiance[t] = (rgb, hitT)` and `rayInfo[t] = (lit, albedo luminance,
   front-facing, visibility)`.
2. The update shader reads `rayRadiance[probe × nRays + i]` per ray; nothing else changed in it.
   `nRays = min(raysPerProbe, kMaxRaysPerProbe)` in both shaders from the same constants.
3. The indirect term (`SamplePreviousIrradiance`): the eight probes around `hitP + N × 2 ×
   kSurfaceEpsilon`, trilinear weight × `saturate(dot(N, toProbe))²`, zero outside the grid, each
   probe's irradiance read at the texel the normal maps to in its 6 × 6 tile. The atlas is read
   through its UAV (the writers run with the atlases in that state) and holds the previous frame's
   completed EMA because the trace runs before the atlas passes; `g_historyValid` is 0 on the first
   frame. The atlas stores cosine-weighted mean radiance (E/π), the quantity the deferred lighting
   multiplies by the albedo, so `albedo × atlas` is the radiance leaving the hit toward the probe.
4. Host: `DDGISystem::CreateResources` adds the two buffers (StructuredBuffer | UnorderedAccess,
   4096 × 64 × 16 B each); `DispatchUpdate` transitions them to UAV, dispatches the trace after the
   atlas transition and the one-time clear, transitions them to SRV, then runs the two atlas passes
   as before. The buffers' SRVs travel in the `shBufferUav` / `firstWrite` slots of
   `RadianceCacheTraceConstants` (unused by the DDGI writer until now); the sentinel keeps the
   per-texel trace. `r.GI.DDGI.RayBuffers` (default 1) and `r.GI.DDGI.IndirectTerm` (default 1).
5. `TLASManager::Rebuild` hashes the 64-byte descriptors as it writes them (FNV-1a 64). Same count
   and hash: no build, no barrier (the TLAS still describes these instances). Same count, different
   hash: `BuildTLAS(update = true)` (the TLAS was created with `AllowUpdate`; the DX12 backend sets
   `PERFORM_UPDATE` with the source = destination), with a full build every 64 refits. Otherwise a
   full build. `r.RayTracing.TLAS.Reuse 0` rebuilds every frame. Counters are logged at shutdown
   ("TLASManager: builds full=F refit=R skipped=S") and once at the first skip.

## Harness
`scripts/rendering/test_ddgi_dedup.py`: eight Sponza Camera C captures with DXR GI (defaults, indirect term
off, per-texel trace, TLAS rebuilt every frame, SDF writer only, and three with the final gather off:
defaults / indirect term off / per-texel), two engine runs with the writers' diagnostics (the energy
harness's configuration, indirect term off / per-texel), the energy parity harness, three captures of
the fog project without DXR (two warm, one with the shader cache set aside) and the committed build's
captures and per-probe values as references: D1a equivalence of the writer, D1b equivalence of the
frame, D2 cost increment, D3 TLAS time and skip counts, D4 energy parity, D5 the SDF path untouched;
reported: the indirect term's contribution and a montage.

## Two findings of method
- With `r.GI.FinalGather 1` (the QA project) the deferred lighting's probe irradiance (DDGI or SDF)
  is replaced in every pixel by the SDF final gather (`sdf_diffuse_gather` 1.0 in the path mask), and
  the dump's `diffuse_indirect` is ~0.0006: the DDGI atlases are not visible in that configuration.
  Any gate on the DDGI consumer needs the gather off.
- Sponza's fog project at frame 60 reproduces itself exactly between two warm runs of a build (1 px)
  while the first run on a fresh build differs on ~90 k pixels (median 3 %): the first run compiles
  the shaders the cache does not hold, the loading frames take longer, and the asynchronous field and
  probe systems reach frame 60 in a different state. Block B.5 saw the same ~90 k pixels between its
  first and third Sponza runs and recorded it as run-to-run spread.
