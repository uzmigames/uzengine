$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts=Join-Path $PSScriptRoot 'test_surface_geometric_normal_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$source=Get-Content -Raw (Join-Path $root 'runtime/shaders/runtime/ssgi/ssgi_surface.hlsli')
$start=$source.IndexOf('bool SurfaceDerivativeSide(');$end=$source.LastIndexOf('#endif')
if($start -lt 0 -or $end -le $start){throw 'Production geometric normal functions missing.'}
$geometry=$source.Substring($start,$end-$start) -replace 'out float3 ([a-zA-Z]+)', 'float3& $1' -replace 'out float ([a-zA-Z]+)', 'float& $1'
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'test_surface_geometric_normal.cpp')
$cpp=Join-Path $artifacts 'geometry.cpp';$exe=Join-Path $artifacts 'geometry.exe'
[IO.File]::WriteAllText($cpp,$template.Replace('PRODUCTION_GEOMETRY',$geometry))
& zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE -ne 0){throw 'Geometry harness compile failed.'}
& $exe | Tee-Object (Join-Path $artifacts 'geometry.log')
exit $LASTEXITCODE
