# Golden Image Tests — SSIM Comparison
# 
# Usage:
#   .\tools\golden_test.ps1 -TestName "materials_scene" -Scene "materials.uzscene" [-Threshold 0.98]
#
# Requires: ImageMagick (`compare` command) in PATH.
# Baseline dir: assets/test/golden/<TestName>/baseline.png
# On failure:   assets/test/golden/<TestName>/diff.png is generated.

param(
    [Parameter(Mandatory=$true)]
    [string]$TestName,

    [Parameter(Mandatory=$true)]
    [string]$Scene,

    [double]$Threshold = 0.98,

    [string]$EngineExe = ""
)

$ErrorActionPreference = "Stop"
$baseDir = "$PSScriptRoot\..\assets\test\golden\$TestName"
$baselinePath = "$baseDir\baseline.png"
$currentPath = "$baseDir\current.png"
$diffPath = "$baseDir\diff.png"

# Ensure golden directory exists.
if (-not (Test-Path $baseDir)) {
    New-Item -ItemType Directory -Path $baseDir -Force | Out-Null
}

# Step 1: Capture current frame.
Write-Host "[golden_test] Capturing frame for scene: $Scene ..."
$captureScript = "$PSScriptRoot\capture_frame.ps1"
if (Test-Path $captureScript) {
    & $captureScript -OutputPath $currentPath
} else {
    Write-Host "[golden_test] WARNING: capture_frame.ps1 not found. Using MCP screenshot."
    # Fallback: try to use the engine screenshot via MCP.
    Write-Error "capture_frame.ps1 not found at $captureScript"
    exit 1
}

# Step 2: Check if baseline exists.
if (-not (Test-Path $baselinePath)) {
    Write-Host "[golden_test] No baseline found. Saving current frame as baseline."
    Copy-Item $currentPath $baselinePath -Force
    $result = @{
        test_name  = $TestName
        scene      = $Scene
        status     = "baseline_created"
        baseline   = $baselinePath
        message    = "No existing baseline. Current frame saved as new baseline."
    }
    $result | ConvertTo-Json -Depth 5
    exit 0
}

# Step 3: Compute SSIM using ImageMagick.
Write-Host "[golden_test] Comparing against baseline..."

$compareExe = Get-Command "compare" -ErrorAction SilentlyContinue
if (-not $compareExe) {
    # Try magick compare as fallback.
    $compareExe = Get-Command "magick" -ErrorAction SilentlyContinue
    if ($compareExe) {
        $compareCmd = "magick compare -metric SSIM `"$currentPath`" `"$baselinePath`" `"$diffPath`" 2>&1"
    } else {
        Write-Error "ImageMagick 'compare' not found in PATH. Install ImageMagick first."
        exit 1
    }
} else {
    $compareCmd = "compare -metric SSIM `"$currentPath`" `"$baselinePath`" `"$diffPath`" 2>&1"
}

try {
    $output = Invoke-Expression $compareCmd 2>&1
    # SSIM value is typically on stderr.
    $ssimStr = ($output | Out-String).Trim()
    # Extract numeric SSIM value (may be on stderr).
    if ($ssimStr -match '[\d\.]+') {
        $ssim = [double]$Matches[0]
    } else {
        $ssim = 0.0
    }
} catch {
    # ImageMagick returns non-zero exit on difference.
    $ssimStr = $_.Exception.Message
    if ($ssimStr -match '[\d\.]+') {
        $ssim = [double]$Matches[0]
    } else {
        $ssim = 0.0
    }
}

# Step 4: Evaluate result.
$passed = $ssim -ge $Threshold

$result = @{
    test_name  = $TestName
    scene      = $Scene
    ssim       = $ssim
    threshold  = $Threshold
    passed     = $passed
    baseline   = $baselinePath
    current    = $currentPath
}

if ($passed) {
    $result.status = "passed"
    $result.message = "SSIM $ssim >= threshold $Threshold. Test PASSED."
    # Clean up diff on pass.
    if (Test-Path $diffPath) { Remove-Item $diffPath -Force }
} else {
    $result.status = "failed"
    $result.diff = $diffPath
    $result.message = "SSIM $ssim < threshold $Threshold. Test FAILED. Diff saved to: $diffPath"
}

$result | ConvertTo-Json -Depth 5
Write-Host $result.message

if (-not $passed) {
    exit 1
}
