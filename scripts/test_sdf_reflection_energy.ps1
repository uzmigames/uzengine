# GPU energy measurement for the SDF reflection estimator (phase0_sponza-followup-08, item 2).
#
# Runs the PRODUCTION `GatherSdfReflections` (sdf_reflections.hlsli) inside the shared GPU
# fixture (scripts/test_sdf_gpu.cpp: enclosed 100 cm room, reflectance 0.8/0.2/0.1, probe
# irradiance history) with the same GGX rays twice per pixel — once with the Karis
# 1/(1+luma) weight (production default) and once as a plain mean (the unbiased estimator) —
# and prints the ratio of the two means in LINEAR radiance:
#   * uniform history (DC = 1 everywhere): both estimators must agree to 1 %;
#   * layered history (DC 0.05 / 1 / 20 by z layer): the ratio IS the energy retained by
#     the Karis weight in a bright/dark environment — a measurement, reported as-is.
# The plain mean is reachable in production with `r.GI.Reflections.HdrWeight=0`.
param([switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'test_sdf_gpu.ps1') -CompileOnly
if ($LASTEXITCODE -ne 0) { throw 'Shared GPU fixture compilation failed.' }
$shared = Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
$artifacts = Join-Path $PSScriptRoot 'test_sdf_reflection_energy_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null

# Reuse the shared wrapper's cbuffers/scene fill; replace the per-thread body.
$wrapper = Get-Content -Raw -LiteralPath (Join-Path $shared 'sdf_reflections_test.hlsl')
$bodyStart = $wrapper.IndexOf('    float3 direction = 0;')
if ($bodyStart -lt 0) { throw 'Shared wrapper body anchor not found.' }
$head = $wrapper.Substring(0, $bodyStart)
$head = $head.Replace('if (id.x >= 7) return;', 'if (id.x >= 8) return;')
$body = @'
    scene.traceSteps = 128;
    // Shading point on the floor of the room (inner face at z = -100 around the clipmap
    // centre), looking up-and-across so the GGX lobe reaches the ceiling (bright layer),
    // the walls (mid layer) and, at grazing rays, the floor (dark layer).
    float3 position = scene.clipmaps[0].xyz + float3(0.0, 0.0, -99.0);
    float3 normal = float3(0.0, 0.0, 1.0);
    float3 reflection = normalize(float3(0.6, 0.3, 1.0));
    const float roughness = 0.5;
    float3 sumKaris = 0.0;
    float3 sumPlain = 0.0;
    [loop] for (uint k = 0; k < 8; ++k) {
        uint2 pixel = uint2(id.x * 8 + k, 0);
        scene.plainMean = 0u;
        sumKaris += GatherSdfReflections(scene, position, normal, normal, reflection, roughness, pixel, frame, 8);
        scene.plainMean = 1u;
        sumPlain += GatherSdfReflections(scene, position, normal, normal, reflection, roughness, pixel, frame, 8);
    }
    RWStructuredBuffer<float4> output = ResourceDescriptorHeap[outputSrv];
    output[id.x]     = float4(sumKaris, 8.0);
    output[8 + id.x] = float4(sumPlain, 8.0);
}
'@
$source = $head + $body
$sourcePath = Join-Path $artifacts 'reflection-energy.hlsl'
$shaderPath = Join-Path $artifacts 'reflection-energy.dxil'
$source | Set-Content -LiteralPath $sourcePath
$dxc = Join-Path $root 'runtime/thirdparty/dxc/bin/x64/dxc.exe'
$include = Join-Path $root 'runtime/shaders/runtime/gi'
& $dxc -T cs_6_6 -E main -I $include $sourcePath -Fo $shaderPath
if ($LASTEXITCODE -ne 0) { throw 'Reflection energy wrapper compilation failed.' }
if ($CompileOnly) { exit 0 }

$exe = Join-Path $shared 'test_sdf_gpu.exe'
$mesh = Join-Path $shared 'mesh_sdf_bake.dxil'
$composite = Join-Path $shared 'global_sdf_composite.dxil'
$failures = 0
foreach ($scenario in @('--reflection-energy-uniform', '--reflection-energy')) {
    $output = & $exe $scenario $mesh $composite $shaderPath
    $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $artifacts ($scenario.TrimStart('-') + '.log'))
    $line = $output | Where-Object { $_ -match 'reflection energy:' } | Select-Object -Last 1
    if (-not $line) { Write-Output "FAIL $scenario`: no result line (exit $code)"; $output | Select-Object -Last 5; $failures++; continue }
    Write-Output $line
    if ($code -ne 0 -or $line -notmatch '^PASS') { $failures++ }
}
if ($failures -gt 0) { Write-Output "reflection energy: $failures scenario(s) failed"; exit 1 }
Write-Output 'reflection energy: uniform agreement and layered measurement recorded.'
