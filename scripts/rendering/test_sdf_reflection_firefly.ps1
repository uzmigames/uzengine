# Locks in the firefly suppression of the indirect specular/diffuse paths:
# SDF reflection gather (Karis weight + horizon weight) and the radiance-cache
# consumer (probe selection by geometric normal).
#
# `GatherSdfReflections` (runtime/shaders/runtime/gi/sdf_reflections.hlsli)
# averages up to 16 GGX rays per pixel with no temporal pass behind it. With a
# plain mean, one ray landing on a sun-lit surface (`albedo * sun * NdL / pi`,
# tens of times a shadowed wall's radiance) dominated the pixel and showed as a
# white speck; at grazing angles Fresnel turned whole walls into sparkle, and
# more rays barely helped because the bright hit is systematic for that pixel's
# lobe (16 rays: 708 -> 600 isolated pixels on the corridor test pose).
#
# Two corrections. (a) A ray below the shading horizon keeps its lobe weight in
# the denominator instead of being dropped, so one surviving sun-lit ray among
# three rejected ones is no longer renormalised into a 4x brighter pixel — the
# actual firefly mechanism at grazing angles on normal-mapped walls.
# (b) The gather applies the same Karis HDR weight the SSR resolve uses,
# `1 / (1 + luma)`, dividing by the accumulated weights (no inverse step).
# This test extracts the production loop and evaluates it on the CPU:
#   1. one bright ray among dark ones is compressed, not averaged linearly;
#   2. a constant environment is returned exactly (all weights equal);
#   3. the shader source carries the weight on both accumulators.
# Source-level, no GPU. Exits non-zero on any failure.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$src  = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/sdf_reflections.hlsli')

$gather = [regex]::Match($src, '(?s)float3 GatherSdfReflections\(.*?\n\}').Value
if (-not $gather) { throw 'GatherSdfReflections not found.' }

$checks = [ordered]@{}
$checks['karis_weight_defined']  = [bool]([regex]::IsMatch($gather, 'karisW\s*=\s*scene\.plainMean\s*!=\s*0u\s*\?\s*1\.0\s*:\s*rcp\(1\.0\s*\+\s*dot\(sample,\s*float3\(0\.2126,\s*0\.7152,\s*0\.0722\)\)\)'))
# The plain mean must stay reachable (r.GI.Reflections.HdrWeight=0 / fixture energy measurement).
$checks['plain_mean_toggle']     = [bool]([regex]::IsMatch($src, 'uint plainMean;'))
$checks['radiance_uses_karis']   = [bool]([regex]::IsMatch($gather, 'radiance\s*\+=\s*weight\s*\*\s*karisW\s*\*\s*sample'))
$checks['weight_sum_uses_karis'] = [bool]([regex]::IsMatch($gather, 'totalWeight\s*\+=\s*weight\s*\*\s*karisW'))
$checks['no_plain_mean_left']    = -not [regex]::IsMatch($gather, 'radiance\s*\+=\s*weight\s*\*\s*TraceSdfSurfaceRadiance')
$checks['normalised_by_weights'] = [bool]([regex]::IsMatch($gather, 'radiance\s*/\s*totalWeight'))
# Horizon handling: a below-horizon ray keeps its lobe weight in the denominator
# instead of being dropped, so the surviving rays are not renormalised upward.
$checks['rejected_ray_keeps_weight'] = [bool]([regex]::IsMatch($gather, 'if \(dot\(normal,\s*direction\)\s*<=\s*0\.0\)\s*\{\s*totalWeight\s*\+=\s*weight;\s*continue;\s*\}'))
$checks['no_weight_zeroing_on_horizon'] = -not [regex]::IsMatch($gather, 'if \(dot\(normal,\s*direction\)\s*<=\s*0\.0\)\s*weight\s*=\s*0\.0;')
# Radiance-cache consumer: probe selection weighted by the GEOMETRIC normal (surface
# facing), with the shading normal reserved for EvalSH_L2. Weighting by the
# normal-mapped N let neighbouring texels admit different probe sets and pick up a
# bright atrium probe alone — the sun/reflection/AO-independent white specks.
$consumer = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl')
$rcBlock = [regex]::Match($consumer, '(?s)Radiance cache \(SDF sphere-traced spatially-varying SH\).*?irradianceFromGI = true;').Value
if (-not $rcBlock) { throw 'Radiance cache consumer block not found.' }
$checks['rc_probe_weight_uses_geometric_normal'] = [bool]([regex]::IsMatch($rcBlock, 'probeFacing\s*=\s*dot\(geometricNormal,\s*geometricNormal\)\s*>\s*0\.5\s*\?\s*geometricNormal\s*:\s*N'))
$checks['rc_probe_weight_not_shading_normal'] = -not [regex]::IsMatch($rcBlock, 'saturate\(dot\(N,\s*toProbe\s*/\s*probeDistance\)\)')
$checks['rc_sh_eval_still_shading_normal'] = [bool]([regex]::IsMatch($rcBlock, 'EvalSH_L2\(N,\s*shArr\)'))
# Radiance-cache readiness must not be gated on the recentre flag: the cache is
# retraced in full before lighting in the same frame, and gating on it dropped
# the whole indirect path (RC, gather, reflections) for one frame per grid cell
# the camera crossed — the "whole scene flickers while flying" report.
$rcHeader = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/gi/radiance_cache.h')
$hasProduced = [regex]::Match($rcHeader, '(?s)bool HasProducedResult\(\) const noexcept \{.*?\}').Value
if (-not $hasProduced) { throw 'HasProducedResult not found.' }
$checks['rc_ready_not_gated_on_dirty'] = -not [regex]::IsMatch($hasProduced, 'm_dirty')
$checks['rc_ready_still_gated_on_first_write'] = [bool]([regex]::IsMatch($hasProduced, 'm_firstWritePending'))
$checks['mirror_path_bypasses']  = [bool]([regex]::IsMatch($gather, 'roughness\s*<=\s*0\.04\)\s*return\s+TraceSdfSurfaceRadiance'))

# CPU evaluation of the estimator with the production formula.
function Karis-Mean([double[]]$samples, [double[]]$weights) {
    $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt $samples.Count; $i++) {
        $k = 1.0 / (1.0 + $samples[$i])   # luma of a grey sample is the sample itself
        $num += $weights[$i] * $k * $samples[$i]
        $den += $weights[$i] * $k
    }
    return $num / $den
}
$w = @(1.0, 1.0, 1.0, 1.0)
$spike = Karis-Mean @(20.0, 0.05, 0.05, 0.05) $w        # one sun-lit hit among three shadowed hits
$plain = (20.0 + 3 * 0.05) / 4.0                          # what the old mean produced
$checks['spike_compressed_vs_plain_mean'] = ($spike -lt 0.5) -and ($plain -gt 5.0)
$const = Karis-Mean @(0.7, 0.7, 0.7, 0.7) $w
$checks['constant_environment_exact'] = [math]::Abs($const - 0.7) -lt 1e-9
$spike16 = Karis-Mean (@(20.0, 20.0, 20.0, 20.0) + (1..12 | ForEach-Object { 0.05 })) (1..16 | ForEach-Object { 1.0 })
$checks['same_bright_fraction_same_result'] = [math]::Abs($spike16 - $spike) -lt 1e-6

function Horizon-Mean([double[]]$samples, [bool[]]$aboveHorizon, [double[]]$weights) {
    $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt $samples.Count; $i++) {
        if (-not $aboveHorizon[$i]) { $den += $weights[$i]; continue }
        $k = 1.0 / (1.0 + $samples[$i])
        $num += $weights[$i] * $k * $samples[$i]; $den += $weights[$i] * $k
    }
    return $num / $den
}
$oneSurvivor = Horizon-Mean @(20.0, 0.0, 0.0, 0.0) @($true, $false, $false, $false) @(1.0, 1.0, 1.0, 1.0)
$checks['one_survivor_not_inflated'] = $oneSurvivor -lt 5.0   # old renormalisation returned 20.0

$passed = 0
foreach ($name in $checks.Keys) { if ($checks[$name]) { $passed++ } else { Write-Output ("FAIL {0}" -f $name) } }
Write-Output ("SDF reflection firefly guard: {0}/{1} checks passed; spike 20+3x0.05 -> plain {2:N3}, Karis {3:N3}." -f `
    $passed, $checks.Count, $plain, $spike)
if ($passed -ne $checks.Count) { exit 1 }
exit 0
