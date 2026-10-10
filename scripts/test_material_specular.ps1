param([string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid artifact label.' }
$root = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_material_specular_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$sync = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/engine/renderer/scene/scene_sync.cpp')
$shader = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl')
$start = $sync.IndexOf('const auto& graph = *graphForDraw;')
$end = $sync.IndexOf('const auto textureInput', $start)
if ($start -lt 0 -or $end -lt 0) { throw 'Graph metadata production block not found.' }
$graph = $sync.Substring($start, $end - $start)
function Assignment([string]$Name) {
    $match = [regex]::Match($shader, '(?m)^\s*float[23]?\s+' + $Name + '\s*=\s*[^;]+;')
    if (!$match.Success) { throw "Missing shader assignment: $Name" }
    return $match.Value
}
$decode = if ($shader -match 'float\s+materialSpecular\s*=') { Assignment 'materialSpecular' } else { 'float materialSpecular = 0.5f;' }
$f90 = if ($shader -match 'float\s+dielectricF90\s*=') { Assignment 'dielectricF90' } else { 'float dielectricF90 = 1.0f;' }
$direct90 = if ($shader -match 'float[3]?\s+F90\s*=') { Assignment 'F90' } else { 'float F90 = 1.0f;' }
$direct = [regex]::Match($shader, 'float3 F = F_Schlick\([^;]+;').Value
if (!$direct) { throw 'Direct Fresnel expression not found.' }
$material = @($decode, (Assignment 'dielectricF0'), $f90, (Assignment 'F0'), $direct90) -join "`n"
$indirect = @('FssEss_d','Ess_d','Ems_d','Favg_d','Fms_d') | ForEach-Object { Assignment $_ }
$indirect = $indirect -join "`n"
function Scalarize([string]$Code) {
    $Code = $Code -replace 'float3\(\s*([0-9.f]+)\s*,\s*\1\s*,\s*\1\s*\)', '$1'
    return $Code -replace '\bfloat3\b', 'float' -replace 'dielectricF0\.g', 'dielectricF0'
}
$template = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'test_material_specular.cpp')
$code = $template.Replace('PRODUCTION_GRAPH', $graph).Replace('PRODUCTION_MATERIAL', (Scalarize $material)).Replace('PRODUCTION_DIRECT', (Scalarize $direct)).Replace('PRODUCTION_INDIRECT', (Scalarize $indirect))
$cpp = Join-Path $artifacts ($Label + '.cpp')
$exe = Join-Path $artifacts ($Label + '.exe')
[IO.File]::WriteAllText($cpp, $code)
& zig c++ -std=c++23 -O0 $cpp -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Material specular harness compilation failed.' }
& $exe | Tee-Object -FilePath (Join-Path $artifacts ($Label + '.log'))
exit $LASTEXITCODE
