# build_and_report.ps1 -- Build UzEngine and output structured JSON results.
#
# Usage:
#   pwsh tools/build_and_report.ps1                    # From project root
#   pwsh tools/build_and_report.ps1 -Config Debug      # Debug build
#
# Output: JSON to stdout with schema:
#   { "success": bool, "errors": [...], "warnings_count": int, "build_time_ms": int }

param(
    [string]$Config = "Release",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Continue"
$projectRoot = Split-Path -Parent $PSScriptRoot

# Ensure build directory exists
if (-not (Test-Path "$projectRoot/$BuildDir")) {
    $result = @{
        success       = $false
        errors        = @(@{
            severity = "error"
            code     = "BUILD_DIR_MISSING"
            message  = "Build directory '$BuildDir' not found. Run cmake -B $BuildDir first."
        })
        warnings_count = 0
        build_time_ms  = 0
    }
    $result | ConvertTo-Json -Depth 5 -Compress
    exit 1
}

# Run cmake build and capture output
$stopwatch = [System.Diagnostics.Stopwatch]::StartNew()

$buildOutput = & cmake --build "$projectRoot/$BuildDir" --config $Config 2>&1 | Out-String
$buildExitCode = $LASTEXITCODE

$stopwatch.Stop()
$buildTimeMs = [int]$stopwatch.ElapsedMilliseconds

# Parse MSVC error/warning output
# Format: file(line[,column]): error|warning CODE: message
$errors = @()
$warningsCount = 0

$lines = $buildOutput -split "`n"
foreach ($line in $lines) {
    # Match MSVC diagnostic format: path(line,col): severity code: message
    if ($line -match '^\s*(.+?)\((\d+)(?:,(\d+))?\)\s*:\s*(error|fatal error|warning)\s+(\w+)\s*:\s*(.+)$') {
        $severity = $Matches[4].Trim()
        $entry = @{
            file     = $Matches[1].Trim()
            line     = [int]$Matches[2]
            column   = if ($Matches[3]) { [int]$Matches[3] } else { 0 }
            severity = if ($severity -eq "fatal error") { "error" } else { $severity }
            code     = $Matches[5].Trim()
            message  = $Matches[6].Trim()
        }

        if ($severity -match "error") {
            $errors += $entry
        } elseif ($severity -eq "warning") {
            $warningsCount++
        }
    }
    # Also catch linker errors: LINK : fatal error LNKXXXX: ...
    elseif ($line -match 'LINK\s*:\s*(fatal error|error)\s+(\w+)\s*:\s*(.+)$') {
        $errors += @{
            file     = "LINK"
            line     = 0
            column   = 0
            severity = "error"
            code     = $Matches[2].Trim()
            message  = $Matches[3].Trim()
        }
    }
}

$result = @{
    success        = ($buildExitCode -eq 0)
    errors         = $errors
    warnings_count = $warningsCount
    build_time_ms  = $buildTimeMs
}

$result | ConvertTo-Json -Depth 5 -Compress

if ($buildExitCode -ne 0) {
    exit 1
} else {
    exit 0
}
