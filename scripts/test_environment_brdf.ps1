param([string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Label must contain letters, numbers, underscores or hyphens.' }
$repo = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_environment_brdf_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$shader = Get-Content -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl') -Raw
$ibl = Get-Content -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/lighting/ibl.hlsli') -Raw
function Get-BracedSource([string]$Source, [int]$Start) {
    $open = $Source.IndexOf('{', $Start)
    if ($open -lt 0) { throw 'Missing opening brace.' }
    $depth = 0
    for ($i = $open; $i -lt $Source.Length; ++$i) {
        if ($Source[$i] -eq '{') { ++$depth }
        if ($Source[$i] -eq '}') {
            --$depth
            if ($depth -eq 0) { return $Source.Substring($Start, $i - $Start + 1) }
        }
    }
    throw 'Unbalanced production function.'
}
$helper = [regex]::Match($ibl, 'float2\s+EnvironmentBRDFApprox\s*\(')
if ($helper.Success) {
    $function = Get-BracedSource $ibl $helper.Index
    $specular = [regex]::Match($shader, '(?:float2\s+)?envBRDF\s*=\s*EnvironmentBRDFApprox\([^;]+;').Value
    $diffuse = [regex]::Match($shader, '(?:float2\s+)?envBRDF_diff\s*=\s*EnvironmentBRDFApprox\([^;]+;').Value
    if (!$specular -or !$diffuse) { throw 'Both deferred consumers must call the production helper.' }
    $specular = $specular -replace '^float2\s+', ''
    $diffuse = $diffuse -replace '^float2\s+', ''
} else {
    $specStart = [regex]::Match($shader, '\{\s*float a = roughness;')
    $diffStart = [regex]::Match($shader, 'float2 envBRDF_diff;\s*')
    if (!$specStart.Success -or !$diffStart.Success) { throw 'Current inline BRDF blocks not found.' }
    $function = ''
    $specular = Get-BracedSource $shader $specStart.Index
    $diffuse = Get-BracedSource $shader ($diffStart.Index + $diffStart.Length)
}
$generated = "$function`nfloat2 ProductionSpecular(float roughness, float NoV) { float2 envBRDF;`n$specular`nreturn envBRDF; }`nfloat2 ProductionDiffuse(float roughness, float NoV) { float2 envBRDF_diff;`n$diffuse`nreturn envBRDF_diff; }"
$generated = $generated -replace '\.zw\b', '.zw()'
[IO.File]::WriteAllText((Join-Path $artifacts 'environment_brdf.generated.h'), $generated)
[IO.File]::WriteAllText((Join-Path $artifacts "$Label.source.txt"), $generated)
$zig = (Get-Command zig).Source
$exe = Join-Path $artifacts 'test_environment_brdf.exe'
& $zig c++ -std=c++23 -O2 -I $artifacts (Join-Path $PSScriptRoot 'test_environment_brdf.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe (Join-Path $artifacts "$Label.csv") | Tee-Object -FilePath (Join-Path $artifacts "$Label.log")
exit $LASTEXITCODE
