param([switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
& (Join-Path $PSScriptRoot 'test_sdf_gpu.ps1') -CompileOnly
if ($LASTEXITCODE -ne 0) { throw 'Shared GPU fixture compilation failed.' }
$shared = Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
$artifacts = Join-Path $PSScriptRoot 'test_sdf_dynamic_diffuse_gpu_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$wrapper = Get-Content -Raw -LiteralPath (Join-Path $shared 'sdf_reflections_test.hlsl')
$oldCall = 'TraceSdfReflectionRadiance(scene,origin,direction)'
if (-not $wrapper.Contains($oldCall)) { throw 'Shared radiance call not found.' }
$dxc = Join-Path $root 'runtime/thirdparty/dxc/bin/x64/dxc.exe'
$include = Join-Path $root 'runtime/shaders/runtime/gi'
$variants = @(
    @{Name='probe-only';Call='SdfReflectionIrradiance(scene,origin,direction)'},
    @{Name='surface-gather';Call='GatherSdfDiffuse(scene,origin,direction,direction,uint2(id.x,0),frame,32)'}
)
foreach ($variant in $variants) {
    $text = $wrapper.Replace('sdf_reflections.hlsli','sdf_diffuse_gather.hlsli').Replace($oldCall, $variant.Call)
    $sourcePath = Join-Path $artifacts ($variant.Name + '.hlsl')
    $shaderPath = Join-Path $artifacts ($variant.Name + '.dxil')
    $text | Set-Content -LiteralPath $sourcePath
    & $dxc -T cs_6_6 -E main -I $include $sourcePath -Fo $shaderPath
    if ($LASTEXITCODE -ne 0) { throw "Diffuse fixture compilation failed: $($variant.Name)" }
}
if ($CompileOnly) { exit 0 }
$exe = Join-Path $shared 'test_sdf_gpu.exe'
$mesh = Join-Path $shared 'mesh_sdf_bake.dxil'
$composite = Join-Path $shared 'global_sdf_composite.dxil'
$baselineOutput = & $exe --reflection-room $mesh $composite (Join-Path $artifacts 'probe-only.dxil')
$baselineExit = $LASTEXITCODE
$baselineOutput | Set-Content -LiteralPath (Join-Path $artifacts 'probe-only.log')
if ($baselineExit -ne 1) { throw 'Probe-only interpolation must fail the local surface transport fixture.' }
Write-Output 'PASS sensitivity: probe-only interpolation rejected by surface transport fixture.'
foreach ($scenario in @('--reflection-empty','--reflection-dark-room','--reflection-room','--reflection-transformed-room','--reflection-mixed-validity-room')) {
    $output = & $exe $scenario $mesh $composite (Join-Path $artifacts 'surface-gather.dxil')
    $code = $LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $artifacts ($scenario.TrimStart('-') + '.log'))
    Write-Output $output
    if ($code -ne 0) { throw "Surface diffuse gather failed: $scenario" }
}
