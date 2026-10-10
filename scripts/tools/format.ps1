. (Join-Path $PSScriptRoot 'quality_common.ps1')
$checkOnly = $args -contains '--check'
foreach ($arg in $args) { if ($arg -ne '--check') { throw "Unknown format argument: $arg" } }
$clangFormat = Get-QualityTool 'clang-format'
foreach ($module in @('runtime', 'napi')) {
    $sources = @(Get-QualitySources $module)
    $flags = if ($checkOnly) { @('--dry-run', '--Werror') } else { @('-i') }
    # Bound command-line length on Windows while preserving paths containing spaces.
    for ($offset = 0; $offset -lt $sources.Count; $offset += 25) {
        $last = [Math]::Min($offset + 24, $sources.Count - 1)
        Invoke-QualityCommand $clangFormat ($flags + @($sources[$offset..$last]))
    }
}
foreach ($module in @('extension', 'mcp')) {
    $action = if ($checkOnly) { '--check' } else { '--write' }
    Invoke-TypeScriptQuality $module 'prettier/bin/prettier.cjs' @($action, 'src/**/*.ts')
}
Write-Host 'All required format checks passed.' -ForegroundColor Green
