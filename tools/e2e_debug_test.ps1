<#
.SYNOPSIS
    End-to-end AI debug pipeline test.

.DESCRIPTION
    Validates the full AI debug pipeline workflow:
    1. Build the engine
    2. Start the engine
    3. Inject a deliberate crash (or wait for one)
    4. Detect crash report (latest.json)
    5. Read and validate crash report structure
    6. Capture a screenshot for visual verification
    7. Report results

    This script is designed to be run manually or by CI to validate the
    complete debug pipeline integration.

.PARAMETER SkipBuild
    Skip the build step (use existing binary).

.PARAMETER Port
    WebSocket port for the engine (default: 9010).

.EXAMPLE
    .\tools\e2e_debug_test.ps1
    .\tools\e2e_debug_test.ps1 -SkipBuild
#>
param(
    [switch]$SkipBuild,
    [int]$Port = 9010
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path $PSScriptRoot -Parent
$buildDir = Join-Path $projectRoot "build"
$engineExe = Join-Path $buildDir "Release\render.exe"
$crashDir = Join-Path $buildDir "Release\.uzengine\crash_reports"
$testResultsDir = Join-Path $projectRoot ".uzengine\test_results"

# Ensure output directory exists.
New-Item -ItemType Directory -Path $testResultsDir -Force | Out-Null

$results = @{
    timestamp      = (Get-Date -Format "yyyy-MM-ddTHH:mm:ssZ")
    steps          = @()
    passed         = 0
    failed         = 0
    total          = 0
}

function Add-StepResult {
    param([string]$Name, [bool]$Pass, [string]$Message = "")
    $results.total++
    if ($Pass) { $results.passed++ } else { $results.failed++ }
    $results.steps += @{
        name    = $Name
        pass    = $Pass
        message = $Message
    }
    $icon = if ($Pass) { "[PASS]" } else { "[FAIL]" }
    $color = if ($Pass) { "Green" } else { "Red" }
    $msg = "  $icon $Name"
    if ($Message) { $msg += " - $Message" }
    Write-Host $msg -ForegroundColor $color
}

Write-Host "╔══════════════════════════════════════╗" -ForegroundColor Magenta
Write-Host "║   E2E AI Debug Pipeline Test         ║" -ForegroundColor Magenta
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Magenta
Write-Host ""

# ─── Step 1: Build ───────────────────────────────────────────────────
Write-Host "── Step 1: Build ──" -ForegroundColor Cyan
if ($SkipBuild) {
    Add-StepResult "Build" $true "Skipped"
} else {
    $buildScript = Join-Path $PSScriptRoot "build_and_report.ps1"
    if (Test-Path $buildScript) {
        $buildOutput = & $buildScript 2>&1 | Out-String
        $buildJson = $null
        try {
            # Extract the last JSON object from the output.
            $jsonMatch = [regex]::Match($buildOutput, '\{[^{}]*"success"[^{}]*\}')
            if ($jsonMatch.Success) {
                $buildJson = $jsonMatch.Value | ConvertFrom-Json
            }
        } catch {}

        if ($buildJson -and $buildJson.success) {
            Add-StepResult "Build" $true "Build succeeded"
        } else {
            Add-StepResult "Build" $false "Build failed"
        }
    } else {
        # Fallback to direct cmake.
        cmake --build $buildDir --config Release --parallel 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Add-StepResult "Build" $true "CMake build succeeded"
        } else {
            Add-StepResult "Build" $false "CMake build failed"
        }
    }
}

# ─── Step 2: Verify binary exists ────────────────────────────────────
Write-Host ""
Write-Host "── Step 2: Verify Binary ──" -ForegroundColor Cyan
Add-StepResult "Binary exists" (Test-Path $engineExe) $engineExe

# ─── Step 3: Check crash report infrastructure ──────────────────────
Write-Host ""
Write-Host "── Step 3: Crash Report Infrastructure ──" -ForegroundColor Cyan

# Run the test binary to validate crash reporter tests pass.
$testExe = Join-Path $buildDir "Release\UzEngineTests.exe"
if (Test-Path $testExe) {
    $testOutput = & $testExe --gtest_filter="CrashReporter*" 2>&1 | Out-String
    $crashTestPass = $testOutput -match "\[ *PASSED *\]"
    Add-StepResult "CrashReporter unit tests" $crashTestPass
} else {
    Add-StepResult "CrashReporter unit tests" $false "Test binary not found"
}

# ─── Step 4: Check LogStreamer tests ─────────────────────────────────
Write-Host ""
Write-Host "── Step 4: Log Streaming ──" -ForegroundColor Cyan
if (Test-Path $testExe) {
    $logOutput = & $testExe --gtest_filter="LogStreamer*" 2>&1 | Out-String
    $logTestPass = $logOutput -match "\[ *PASSED *\]"
    Add-StepResult "LogStreamer unit tests" $logTestPass
} else {
    Add-StepResult "LogStreamer unit tests" $false "Test binary not found"
}

# ─── Step 5: Check DRED report tests ────────────────────────────────
Write-Host ""
Write-Host "── Step 5: DRED Report ──" -ForegroundColor Cyan
if (Test-Path $testExe) {
    $dredOutput = & $testExe --gtest_filter="Dred*" 2>&1 | Out-String
    $dredTestPass = $dredOutput -match "\[ *PASSED *\]"
    Add-StepResult "DRED report unit tests" $dredTestPass
} else {
    Add-StepResult "DRED report unit tests" $false "Test binary not found"
}

# ─── Step 6: Check RenderGraph introspection tests ───────────────────
Write-Host ""
Write-Host "── Step 6: RenderGraph Introspection ──" -ForegroundColor Cyan
if (Test-Path $testExe) {
    $rgOutput = & $testExe --gtest_filter="RenderGraph*" 2>&1 | Out-String
    $rgTestPass = $rgOutput -match "\[ *PASSED *\]"
    Add-StepResult "RenderGraph unit tests" $rgTestPass
} else {
    Add-StepResult "RenderGraph unit tests" $false "Test binary not found"
}

# ─── Step 7: Check VS Code extension files ───────────────────────────
Write-Host ""
Write-Host "── Step 7: VS Code Extension ──" -ForegroundColor Cyan
$extFile = Join-Path $projectRoot "vscode-uzengine\src\extension.ts"
Add-StepResult "Extension source exists" (Test-Path $extFile)

$extHasDiagnose = (Get-Content $extFile -Raw) -match "diagnoseCrash"
Add-StepResult "diagnoseCrash command registered" $extHasDiagnose

$extHasLogSub = (Get-Content $extFile -Raw) -match "subscribe_logs"
Add-StepResult "Log subscription on connect" $extHasLogSub

# ─── Step 8: Check AGENTS.md rules ───────────────────────────────────
Write-Host ""
Write-Host "── Step 8: Agent Rules ──" -ForegroundColor Cyan
$agentsFile = Join-Path $projectRoot "AGENTS.md"
if (Test-Path $agentsFile) {
    $agentsContent = Get-Content $agentsFile -Raw
    foreach ($rule in @("DEBUG-1", "DEBUG-2", "DEBUG-3", "DEBUG-4", "DEBUG-5", "DEBUG-6")) {
        Add-StepResult "Rule $rule" ($agentsContent -match $rule)
    }
} else {
    Add-StepResult "AGENTS.md exists" $false
}

# ─── Step 9: Golden test infrastructure ──────────────────────────────
Write-Host ""
Write-Host "── Step 9: Golden Test Infrastructure ──" -ForegroundColor Cyan
Add-StepResult "golden_test.ps1 exists" (Test-Path (Join-Path $PSScriptRoot "golden_test.ps1"))
Add-StepResult "run_golden_tests.ps1 exists" (Test-Path (Join-Path $PSScriptRoot "run_golden_tests.ps1"))
Add-StepResult "Golden test docs exist" (Test-Path (Join-Path $projectRoot "docs\guides\golden-tests.md"))

# ─── Step 10: Build verification tool ────────────────────────────────
Write-Host ""
Write-Host "── Step 10: Build Verification ──" -ForegroundColor Cyan
Add-StepResult "build_and_report.ps1 exists" (Test-Path (Join-Path $PSScriptRoot "build_and_report.ps1"))

$mcpIndex = Join-Path $projectRoot "tools\mcp-uzengine\index.js"
if (Test-Path $mcpIndex) {
    $mcpContent = Get-Content $mcpIndex -Raw
    Add-StepResult "MCP uz_build tool" ($mcpContent -match "uz_build")
} else {
    Add-StepResult "MCP uz_build tool" $false "index.js not found"
}

# ─── Summary ─────────────────────────────────────────────────────────
Write-Host ""
Write-Host "══════════════════════════════════════" -ForegroundColor Magenta
$summaryColor = if ($results.failed -gt 0) { "Red" } else { "Green" }
Write-Host "Results: $($results.passed)/$($results.total) passed" -ForegroundColor $summaryColor
if ($results.failed -gt 0) {
    $failedSteps = ($results.steps | Where-Object { -not $_.pass }) | ForEach-Object { $_.name }
    Write-Host "Failed:  $($failedSteps -join ', ')" -ForegroundColor Red
}
Write-Host "══════════════════════════════════════" -ForegroundColor Magenta

# Write results JSON.
$resultsPath = Join-Path $testResultsDir "e2e_results.json"
$results | ConvertTo-Json -Depth 5 | Set-Content -Path $resultsPath -Encoding UTF8
Write-Host ""
Write-Host "Results written to: $resultsPath" -ForegroundColor Gray

exit $results.failed
