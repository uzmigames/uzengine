. (Join-Path $PSScriptRoot 'quality_common.ps1')

$withCoverage = $args -contains '--coverage'
$compiler = 'zig'
foreach ($arg in $args) {
    if ($arg -match '^--compiler=(.+)$') { $compiler = $Matches[1] }
    elseif ($arg -ne '--coverage') { throw "Unknown test argument: $arg" }
}
if ($compiler -notin @('zig', 'msvc', 'clang')) { throw "Unknown compiler: $compiler" }

Push-Location (Join-Path $Root 'runtime')
try {
    if ($compiler -eq 'zig') {
        Invoke-QualityCommand (Get-QualityZig) @('build', 'test')
    } else {
        $cmake = Get-QualityTool 'cmake'
        Invoke-QualityCommand $cmake @('--preset', "$compiler-debug")
        Invoke-QualityCommand $cmake @('--build', "build/$compiler-debug", '--config', 'Debug')
        $testExe = if ($compiler -eq 'msvc') { 'build/msvc-debug/Debug/test_runtime.exe' } else { 'build/clang-debug/test_runtime.exe' }
        Invoke-QualityCommand (Join-Path (Get-Location) $testExe)
    }
} finally { Pop-Location }

foreach ($module in @('extension', 'mcp')) {
    $testArgs = @('run')
    if ($withCoverage) { $testArgs += '--coverage' }
    Invoke-TypeScriptQuality $module 'vitest/vitest.mjs' $testArgs
}
Write-Host 'All required test suites passed.' -ForegroundColor Green
