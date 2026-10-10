param([string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid label.' }
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts = Join-Path $PSScriptRoot 'test_gdf_raw_composite_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl')
$struct = [regex]::Match($source, '(?ms)^struct MeshSdf \{.*?^\};').Value
$distance = [regex]::Match($source, '(?ms)^float MeshDistance\(.*?^\}').Value
$main = [regex]::Match($source, '(?ms)^void main\(.*?^\}').Value
if (!$struct -or !$distance -or !$main) { throw 'Missing production composite functions.' }
$distance = $distance -replace 'out float ', 'float& ' -replace 'Texture3D<float> volume = ResourceDescriptorHeap\[NonUniformResourceIndex\(mesh.srvIndex\)\];', 'TestVolume volume{mesh.srvIndex};'
$main = $main -replace 'RWTexture3D<float4> output = ResourceDescriptorHeap\[outputUav\];', 'ExpandedOutput output;' -replace 'RWTexture3D<float> rawOutput = ResourceDescriptorHeap\[rawOutputUav\];', 'RawOutput rawOutput;'
$main = $main -replace 'void main\(', 'void ProductionComposite(' -replace ' : SV_DispatchThreadID', ''
$generated = $struct + "`n" + 'float3 DiffuseReflectance(MeshSdf mesh) { return mesh.baseColorTint; }' + "`n" + $distance + "`n" + 'MeshSdf meshes[512];' + "`n" + $main
[IO.File]::WriteAllText((Join-Path $artifacts ($Label + '.generated.h')), $generated)
$template = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'test_gdf_raw_composite.cpp')
$cpp = Join-Path $artifacts ($Label + '.cpp')
[IO.File]::WriteAllText($cpp, $template.Replace('PRODUCTION_HEADER', $Label + '.generated.h'))
$exe = Join-Path $artifacts ($Label + '.exe')
& zig c++ -std=c++23 -O0 -DCHECK_ORIGINAL_EXPANDED $cpp -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Producer fixture compile failed.' }
& $exe | Tee-Object -FilePath (Join-Path $artifacts ($Label + '.log'))
exit $LASTEXITCODE
