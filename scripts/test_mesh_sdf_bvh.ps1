$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_mesh_sdf_bvh_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$shader = Get-Content -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl') -Raw
$parts = foreach ($name in @('Triangle','BvhNode')) {
    $match = [regex]::Match($shader, "(?ms)^struct $name \{.*?^\};")
    if (!$match.Success) { throw "Missing production struct $name" }
    $match.Value
}
foreach ($name in @('ClosestSegment','ClosestTriangle','UpdateMeshClosest','ClosestMeshTriangle')) {
    $match = [regex]::Match($shader, "(?ms)^(?:float[23]|void) $name\(.*?^\}")
    if (!$match.Success) { throw "Missing production function $name" }
    $parts += $match.Value
}
$generated = ($parts -join "`n") -replace '\[loop\]', '' -replace '\binout (float|uint) ', '$1& '
[IO.File]::WriteAllText((Join-Path $artifacts 'mesh_sdf_bvh.generated.h'),$generated)
$zig = (Get-Command zig).Source
$exe = Join-Path $artifacts 'test_mesh_sdf_bvh.exe'
& $zig c++ -std=c++23 -O2 -I $artifacts -I (Join-Path $repo 'runtime/engine/renderer/gi') (Join-Path $PSScriptRoot 'test_mesh_sdf_bvh.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe | Tee-Object -FilePath (Join-Path $artifacts 'cpu.log')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $repo 'runtime/thirdparty/dxc/bin/x64/dxc.exe') -T cs_6_6 -E main (Join-Path $repo 'runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl') -Fo (Join-Path $artifacts 'mesh_sdf_bake.dxil')
exit $LASTEXITCODE
