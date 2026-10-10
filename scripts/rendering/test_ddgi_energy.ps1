# Locks in the DDGI probe-energy corrections (phase0_sponza-followup-01).
#
# Reads the production sources and asserts the four terms that made the DXR
# probe path carry ~1.49x the SDF path's energy on Sponza (Camera A, 1080p,
# auto exposure off, identical CVars: 12.1 vs 8.1 /255):
#
#   1. Lambert 1/pi on the probe hit radiance (writer had albedo * E).
#   2. A shadow ray gating that radiance (writer had none, so every hit was
#      lit as if the sun were unoccluded).
#   3. Chebyshev depth moments stored normalised by maxTraceDistance — the
#      RG16F atlas saturates at 65504 and a squared distance in centimetres
#      does not fit, which zeroed the variance and disabled occlusion —
#      and rescaled by the same definition in the consumer.
#   4. Probe lighting read from PerFrameCB + the global distance field, not
#      the hardcoded placeholder sun/sky the dispatch used to build.
#
# Source-level, like test_reflection_selection.ps1: no GPU required.
#
# 2026-10-08: `TraceProbeRay` lives in ddgi_probe_common.hlsli since block B.6 (edfd7942), shared by
# the trace pass production dispatches (ddgi_probe_trace_cs.hlsl) and the update shader's per-texel
# fallback (r.GI.DDGI.RayBuffers 0). The extraction looked for the original two-argument signature
# in ddgi_probe_update_cs.hlsl; the signature gained `out float4 hitInfo` for followup-01's stage-3
# hit records (notes, 2026-09-17; ddgi_probe_update_cs.hlsl:269 in 7eea6b95), and from then on the
# script threw before any check. It now reads the shared header with any parameters after
# `float3 origin, float3 dir` (renaming either makes it throw), and check 0 ties that function to the
# trace pass.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$writer   = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl')
$consumer = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl')
$rc       = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl')
$dispatch = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/gi/ddgi_system.cpp')
$common   = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_common.hlsli')
$traceCs  = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_trace_cs.hlsl')

$trace = [regex]::Match($common, '(?s)float4 TraceProbeRay\(float3 origin, float3 dir\b[^)]*\)\s*\{.*?\n\}').Value
if (-not $trace) { throw 'TraceProbeRay not found in ddgi_probe_common.hlsli, the header both DDGI writers take it from.' }
# Comments are not code (2026-10-08): with them, `sunRad / kPI` also matched the function's own comment
# and `NdL * visibility` its diagnostic hit flag, so lambert_over_pi_in_ddgi and visibility_applied
# could not fail; visibility_applied now reads the sunRad assignment itself.
$trace = [regex]::Replace($trace, '//[^\r\n]*', '')

$checks = [ordered]@{}

# 0. The function checked below is the one production traces with: the trace pass includes the shared
# header and calls it, and the update shader includes the same header for its fallback.
$checks['trace_pass_uses_shared_trace'] = [bool]([regex]::IsMatch($traceCs, '#include\s+"ddgi_probe_common\.hlsli"')) `
                                      -and [bool]([regex]::IsMatch($traceCs, '=\s*TraceProbeRay\(probePos,\s*rayDir')) `
                                      -and [bool]([regex]::IsMatch($writer, '#include\s+"ddgi_probe_common\.hlsli"'))

# 1. Lambert 1/pi, matching the SDF writer's `albedo * (sunRad / kPI + indirect)`.
$checks['lambert_over_pi_in_ddgi'] = [bool]([regex]::IsMatch($trace, 'sunRad\s*/\s*kPI'))
# The SDF writer's radiance assignment itself (2026-10-08): its diagnostic `pathStats.y` line also
# carries `sunRad / kPI`, so the bare pattern still passed with the assignment's 1/pi gone.
$checks['lambert_over_pi_in_sdf']  = [bool]([regex]::IsMatch($rc, '\bradiance\s*=[^;]*\bsunRad\s*/\s*kPI'))
$checks['no_unscaled_albedo_return'] = -not [regex]::IsMatch($trace, 'return\s+float4\(\s*sunRad\s*\*\s*groundAlbedo')

# 2. Shadow ray gating the hit radiance.
$checks['shadow_rayquery'] = [bool]([regex]::IsMatch($trace, 'RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH'))
$checks['visibility_applied'] = [bool]([regex]::IsMatch($trace, 'sunRad\s*=[^;]*NdL\s*\*\s*visibility'))

# 3. Normalised depth moments on both sides.
$checks['writer_normalises_depth'] = [bool]([regex]::IsMatch($writer, 'hitNorm\s*=\s*hitT\s*\*\s*invMaxTraceDistance'))
$checks['writer_squares_normalised'] = [bool]([regex]::IsMatch($writer, 'accumDepth2\s*\+=\s*hitNorm\s*\*\s*hitNorm'))
$checks['consumer_rescales_moments'] = [bool]([regex]::IsMatch($consumer, 'momentT\.x\s*\*\s*ddgiMaxTrace')) `
                                   -and [bool]([regex]::IsMatch($consumer, 'momentT\.y\s*\*\s*\(ddgiMaxTrace\s*\*\s*ddgiMaxTrace\)'))
# Writer and consumer must agree on what maxTraceDistance is:
#   writer  (C++): probeSpacing * kProbesPerAxis * 2
#   consumer(HLSL): _ddgiProbeSpacing * _ddgiProbesPerAxis * 2
$checks['maxtrace_definition_matches'] =
    [bool]([regex]::IsMatch($dispatch, 'maxTraceDistance\s*=\s*m_probeSpacing[\s\S]{0,120}kProbesPerAxis\s*\)\s*\*\s*2\.0f')) `
    -and [bool]([regex]::IsMatch($consumer, 'ddgiMaxTrace\s*=\s*_ddgiProbeSpacing[\s\S]{0,80}_ddgiProbesPerAxis\s*\)\s*\*\s*2\.0'))

# 4. Scene lighting + GDF surface attributes reach the dispatch.
$checks['dispatch_reads_perframe_sun'] = [bool]([regex]::IsMatch($dispatch, 'constants\.lightDirX\s*=\s*pf->lightDir\.x'))
$checks['dispatch_reads_perframe_sky'] = [bool]([regex]::IsMatch($dispatch, 'constants\.skyColorR\s*=\s*pf->skyLightColorR\s*\*\s*pf->skyLightIntensity'))
$checks['dispatch_fills_gdf_clipmaps'] = [bool]([regex]::IsMatch($dispatch, 'constants\.numGdfClipmaps\s*=\s*numClipmaps'))
$checks['no_placeholder_sky'] = -not [regex]::IsMatch($dispatch, 'constants\.skyColorR\s*=\s*0\.6f')
$checks['no_placeholder_sun'] = -not [regex]::IsMatch($dispatch, 'constants\.lightDirZ\s*=\s*1\.0f;\s*\r?\n\s*constants\.lightIntensity\s*=\s*1\.0f')
$checks['writer_uses_gdf_normal'] = [bool]([regex]::IsMatch($trace, 'GdfNormal\(hitP\)'))
$checks['writer_uses_gdf_albedo'] = [bool]([regex]::IsMatch($trace, 'surface = SampleGdfSurface\(hitP\)')) -and [bool]([regex]::IsMatch($trace, 'saturate\(surface\.gba\)'))
# Black is a valid reflectance (dark dirt; metals write 0 on purpose): validity must come from the
# field's distance sentinel, never from the colour being zero.
$checks['black_albedo_kept_validity_from_distance'] = (-not [regex]::IsMatch($trace, 'dot\(albedo,\s*albedo\)\s*<=\s*0\.0')) -and [bool]([regex]::IsMatch($trace, 'surface\.x < 1e8 \? saturate\(surface\.gba\)'))

# 5. Both GI paths must agree on rays per probe. This was hardcoded to 32 in the DDGI dispatch while
# the SDF radiance cache read r.GI.RadianceCache.RaysPerProbe, so moving the CVar changed one path
# and not the other — an energy difference between the paths that no test would have caught.
# The negative check is the one that matters: a bare literal must not come back.
$checks['ddgi_rays_from_shared_cvar'] = [bool]([regex]::IsMatch($dispatch, 'constants\.raysPerProbe\s*=\s*static_cast<uint32_t>\(\s*?
?\s*CVarRegistry::Get\(\)\.GetInt\("r\.GI\.RadianceCache\.RaysPerProbe"'))
$checks['ddgi_rays_not_hardcoded'] = -not [regex]::IsMatch($dispatch, 'constants\.raysPerProbe\s*=\s*\d+\s*;')

$passed = 0
foreach ($name in $checks.Keys) {
    if ($checks[$name]) { $passed++ } else { Write-Output ("FAIL {0}" -f $name) }
}
Write-Output ("DDGI energy: {0}/{1} checks passed; extracted from production writer, consumer and dispatch." -f $passed, $checks.Count)
if ($passed -ne $checks.Count) { exit 1 }
exit 0
