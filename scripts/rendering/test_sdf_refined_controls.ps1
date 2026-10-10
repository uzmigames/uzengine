param([switch]$SkipWindow)
$ErrorActionPreference = 'Stop'
$artifacts = Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
$exe = Join-Path $artifacts 'test_sdf_gpu.exe'
$mesh = Join-Path $artifacts 'mesh_sdf_bake.dxil'
$composite = Join-Path $artifacts 'global_sdf_composite.dxil'
$reflection = Join-Path $artifacts 'sdf_reflections_test.dxil'
foreach ($scenario in @('--reflection-empty', '--reflection-dark-room', '--reflection-room', '--reflection-transformed-room', '--reflection-mixed-validity-room')) {
    $log = Join-Path $artifacts ('refined-' + $scenario.Substring(2) + '.log')
    & $exe $scenario $mesh $composite $reflection 2>&1 | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "Refined reflection control failed: $scenario (exit $LASTEXITCODE)" }
}
if (!$SkipWindow) {
    $window = Join-Path $artifacts 'test_sdf_window.exe'
    $shader = Join-Path $artifacts 'sdf_window_test.dxil'
    $log = Join-Path $artifacts 'refined-window-mesh128-gdf256.log'
    & $window 128 $mesh $composite $shader 256 2>&1 | Tee-Object -FilePath $log
    if ($LASTEXITCODE -ne 0) { throw "Refined narrow-window control failed (exit $LASTEXITCODE)" }
}
