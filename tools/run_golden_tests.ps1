<#
.SYNOPSIS
    Run golden image tests against the running UzEngine.

.DESCRIPTION
    Automates the golden test workflow:
    1. Starts the engine (if not running)
    2. Loads each test scene
    3. Captures a screenshot
    4. Compares against baseline with SSIM
    5. Reports pass/fail with diff images on failure

.PARAMETER Scene
    Specific scene to test (default: all scenes in assets/test/golden/)

.PARAMETER UpdateBaseline
    If set, captures new baselines instead of comparing.

.PARAMETER Threshold
    SSIM threshold for pass/fail (default: 0.98)

.EXAMPLE
    .\tools\run_golden_tests.ps1
    .\tools\run_golden_tests.ps1 -Scene "default" -Threshold 0.95
    .\tools\run_golden_tests.ps1 -UpdateBaseline
#>
param(
    [string]$Scene = "",
    [switch]$UpdateBaseline,
    [double]$Threshold = 0.98,
    [int]$WarmupFrames = 30,
    [int]$Port = 9002
)

$ErrorActionPreference = "Stop"
$goldenDir = Join-Path $PSScriptRoot "..\assets\test\golden"
$engineExe = Join-Path $PSScriptRoot "..\build\Release\render.exe"
$goldenScript = Join-Path $PSScriptRoot "golden_test.ps1"

# ─── Verify prerequisites ───────────────────────────────────────────
if (-not (Get-Command "magick" -ErrorAction SilentlyContinue)) {
    Write-Error "ImageMagick not found in PATH. Install from https://imagemagick.org/"
    exit 1
}

if (-not (Test-Path $engineExe)) {
    Write-Error "Engine not found at $engineExe. Build first with: cmake --build build --config Release"
    exit 1
}

# ─── Discover test scenes ───────────────────────────────────────────
$testScenes = @()
if ($Scene) {
    $testScenes += $Scene
} else {
    # Auto-discover from golden directory or default scenes
    if (Test-Path $goldenDir) {
        $testScenes = Get-ChildItem -Path $goldenDir -Directory | ForEach-Object { $_.Name }
    }
    if ($testScenes.Count -eq 0) {
        $testScenes = @("default", "materials")
    }
}

Write-Host "╔══════════════════════════════════════╗" -ForegroundColor Cyan
Write-Host "║     UzEngine Golden Image Tests      ║" -ForegroundColor Cyan
Write-Host "╚══════════════════════════════════════╝" -ForegroundColor Cyan
Write-Host ""
Write-Host "Scenes:    $($testScenes -join ', ')"
Write-Host "Threshold: $Threshold"
Write-Host "Mode:      $(if ($UpdateBaseline) { 'UPDATE BASELINE' } else { 'COMPARE' })"
Write-Host ""

# ─── Start engine ────────────────────────────────────────────────────
$engineProcess = $null
$weStartedEngine = $false

# Check if engine is already running on the port
try {
    $testConn = New-Object System.Net.Sockets.TcpClient
    $testConn.Connect("127.0.0.1", $Port)
    $testConn.Close()
    Write-Host "[OK] Engine already running on port $Port" -ForegroundColor Green
} catch {
    Write-Host "[..] Starting engine..." -ForegroundColor Yellow
    $engineProcess = Start-Process -FilePath $engineExe `
        -ArgumentList "--ws-port", $Port, "--headless" `
        -PassThru -WindowStyle Hidden
    $weStartedEngine = $true
    Start-Sleep -Seconds 3
    Write-Host "[OK] Engine started (PID: $($engineProcess.Id))" -ForegroundColor Green
}

# ─── Run tests ───────────────────────────────────────────────────────
$passed = 0
$failed = 0
$errors = @()

foreach ($testName in $testScenes) {
    Write-Host ""
    Write-Host "─── Testing: $testName ───" -ForegroundColor White

    $scenePath = "assets/scenes/$testName.uzscene"
    $baselineDir = Join-Path $goldenDir $testName

    if ($UpdateBaseline) {
        # Capture new baseline
        Write-Host "  Capturing baseline..." -ForegroundColor Yellow
        & $goldenScript -TestName $testName -Scene $scenePath `
            -UpdateBaseline -WarmupFrames $WarmupFrames
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [OK] Baseline updated" -ForegroundColor Green
            $passed++
        } else {
            Write-Host "  [FAIL] Could not capture baseline" -ForegroundColor Red
            $failed++
            $errors += $testName
        }
    } else {
        # Compare against baseline
        if (-not (Test-Path (Join-Path $baselineDir "baseline.png"))) {
            Write-Host "  [SKIP] No baseline found. Run with -UpdateBaseline first." -ForegroundColor Yellow
            continue
        }

        & $goldenScript -TestName $testName -Scene $scenePath `
            -Threshold $Threshold -WarmupFrames $WarmupFrames
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [PASS] SSIM >= $Threshold" -ForegroundColor Green
            $passed++
        } else {
            Write-Host "  [FAIL] SSIM < $Threshold (see diff at $baselineDir\diff.png)" -ForegroundColor Red
            $failed++
            $errors += $testName
        }
    }
}

# ─── Cleanup ─────────────────────────────────────────────────────────
if ($weStartedEngine -and $engineProcess) {
    Write-Host ""
    Write-Host "Stopping engine..." -ForegroundColor Yellow
    Stop-Process -Id $engineProcess.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 1
}

# ─── Summary ─────────────────────────────────────────────────────────
Write-Host ""
Write-Host "══════════════════════════════════════" -ForegroundColor Cyan
Write-Host "Results: $passed passed, $failed failed" -ForegroundColor $(if ($failed -gt 0) { "Red" } else { "Green" })
if ($errors.Count -gt 0) {
    Write-Host "Failed:  $($errors -join ', ')" -ForegroundColor Red
}
Write-Host "══════════════════════════════════════" -ForegroundColor Cyan

exit $failed
