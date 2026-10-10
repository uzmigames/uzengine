param([string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid artifact label.' }
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts = Join-Path $PSScriptRoot 'test_ssr_resolve_filter_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$shader = Get-Content -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/postprocess/ssr_resolve_cs.hlsl') -Raw
$center = [regex]::Match($shader, '(?s)    float centerLum\s*=.*?(?=    // 15-sample)').Value
$sample = [regex]::Match($shader, '(?s)        float sampleLum\s*=.*?(?=\n    \})').Value
$resolve = [regex]::Match($shader, '(?s)    float3 color\s*=.*?outputSSR\[DTid.xy\]\s*=\s*float4\(color, alpha\);').Value
if (!$center -or !$sample -or !$resolve) { throw 'Production SSR filter blocks were not found.' }
$resolve = $resolve -replace 'outputSSR\[DTid.xy\]\s*=', 'return'
$generated = @"
float4 ProductionResolve(float4 traceResult) {
$center
    for (unsigned i = 0; i < 15; ++i) {
        const float4 sampleSSR = traceResult;
        const float w = 0.2f + 0.05f * float(i);
$sample
    }
$resolve
}
"@
[IO.File]::WriteAllText((Join-Path $artifacts 'ssr_resolve_filter.generated.h'), $generated)
[IO.File]::WriteAllText((Join-Path $artifacts "$Label.source.txt"), $generated)
$zig = (Get-Command zig).Source
$exe = Join-Path $artifacts 'test_ssr_resolve_filter.exe'
& $zig c++ -std=c++23 -O2 -I $artifacts (Join-Path $PSScriptRoot 'test_ssr_resolve_filter.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe (Join-Path $artifacts "$Label.csv") | Tee-Object -FilePath (Join-Path $artifacts "$Label.log")
exit $LASTEXITCODE
