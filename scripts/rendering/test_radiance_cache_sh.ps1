param([Parameter(Mandatory=$true)][string]$Zig)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$shader = Get-Content -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl') -Raw
$functions = foreach ($name in @('SHEvaluateL2', 'PackL2')) {
    $match = [regex]::Match($shader, "(?ms)^void $name\(.*?^\}")
    if (-not $match.Success) { throw "Missing production shader function: $name" }
    $match.Value -replace '\bout\s+', ''
}
$header = Join-Path $PSScriptRoot 'radiance_cache_sh.generated.h'
[IO.File]::WriteAllText($header, ($functions -join "`n"))
$exe = Join-Path $PSScriptRoot 'test_radiance_cache_sh.exe'
& $Zig c++ -std=c++17 -O2 (Join-Path $PSScriptRoot 'test_radiance_cache_sh.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
exit $LASTEXITCODE
