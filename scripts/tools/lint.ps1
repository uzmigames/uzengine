param([string]$RuntimeCompileDatabase, [string]$NapiCompileDatabase)
. (Join-Path $PSScriptRoot 'quality_common.ps1')

$clangTidy = Get-QualityTool 'clang-tidy'
foreach ($module in @('runtime', 'napi')) {
    $explicit = if ($module -eq 'runtime') { $RuntimeCompileDatabase } else { $NapiCompileDatabase }
    $database = Get-QualityCompilationDatabase $module $explicit
    $allowed = @(Get-QualitySources $module)
    $entries = @(Get-Content -LiteralPath $database -Raw | ConvertFrom-Json)
    $sources = @(foreach ($entry in $entries) {
        $directory = if ([IO.Path]::IsPathRooted($entry.directory)) { $entry.directory } else {
            Join-Path (Split-Path $database) $entry.directory
        }
        $file = if ([IO.Path]::IsPathRooted($entry.file)) { $entry.file } else { Join-Path $directory $entry.file }
        $file = [IO.Path]::GetFullPath($file)
        if ($file -in $allowed) { $file }
    }) | Sort-Object -Unique
    if (-not $sources) { throw "Compilation database contains no $module sources" }
    if ($module -eq 'runtime' -and -not ($sources | Where-Object { $_ -like "$(Join-Path $Root 'runtime/engine')*" })) {
        throw 'Runtime compilation database omits engine sources'
    }
    foreach ($source in $sources) {
        Invoke-QualityCommand $clangTidy @($source, '-p', (Split-Path $database), '--warnings-as-errors=*')
    }
}
foreach ($module in @('extension', 'mcp')) {
    Invoke-TypeScriptQuality $module 'eslint/bin/eslint.js' @('src/', '--max-warnings=0')
}
Write-Host 'All required lint checks passed.' -ForegroundColor Green
