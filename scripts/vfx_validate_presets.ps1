# VFX Preset Validation - phase17_vfx-viewport-22-preset-validation
#
# Modes:
#   -Mode capture    Capture baseline screenshots for all 22 in-code presets
#                    of vfx_viewport_app. Writes to
#                    .sandbox/preset-baseline/NN-name.png + manifest.json.
#   -Mode validate   Re-capture all 22 and PSNR-diff against the baseline.
#                    PSNR < 35 dB on any preset = fail (exit code != 0).
#
# Usage:
#   pwsh scripts/vfx_validate_presets.ps1 -Mode capture
#   pwsh scripts/vfx_validate_presets.ps1 -Mode validate
#
# Hardcoded preset names mirror `kPresetNames[]` in
# runtime/demos/vfx_viewport_app/vfx_viewport_app.cpp.

[CmdletBinding()]
param(
    [ValidateSet('capture', 'validate')]
    [string]$Mode = 'validate',
    [string]$BaselineDir = 'runtime/tests/baselines/vfx-presets',
    [int]$ScreenshotFrame = 120,
    [int]$Frames = 130,
    [double]$PsnrThreshold = 35.0
)

$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path "$PSScriptRoot/..").Path
$Exe = Join-Path $RepoRoot 'runtime/demos/vfx_viewport_app/build/bin/vfx_viewport_app.exe'
$BaselineFull = Join-Path $RepoRoot $BaselineDir
$Manifest = Join-Path $BaselineFull 'manifest.json'

# Mirror of kPresetNames[] - keep in sync with vfx_viewport_app.cpp.
$Presets = @(
    'FireballProj', 'Fire', 'Smoke', 'Sparks', 'Fountain', 'MagicOrb', 'Burst',
    'ExpBurst', 'ExpSmoke', 'ExpDebris',
    'Fireball', 'RingOfFire', 'ConeJet', 'CylinderVol',
    'CurlSmoke', 'Tornado', 'Whirlpool', 'WindySmoke',
    'LitSmoke', 'LitDebris', 'Orbital', 'AirResist'
)

# Per-preset PSNR threshold overrides for presets that legitimately have
# higher RNG-driven frame-to-frame variance (e.g., physics-driven debris
# explosions, sparks with random ricochets). The default $PsnrThreshold
# (35 dB) is too strict for these. Any preset NOT in this list uses the
# global threshold.
# @ref phase17_vfx-viewport-22-preset-validation
$PerPresetThreshold = @{
    'ExpDebris' = 25.0   # debris explosion: random initial velocities
    'ExpBurst'  = 25.0   # burst with random spread
    'Sparks'    = 25.0   # ricochet randomness can exceed default tolerance
    'LitDebris' = 25.0   # same as ExpDebris
    'Burst'     = 30.0   # burst variants tend to PSNR ~36 dB
}

if (-not (Test-Path $Exe)) {
    Write-Error "vfx_viewport_app.exe not found at $Exe. Build first: 'cd runtime/demos/vfx_viewport_app; zig build -Doptimize=ReleaseFast'"
}

if (-not (Test-Path $BaselineFull)) {
    New-Item -ItemType Directory -Path $BaselineFull | Out-Null
}

function Capture-Preset {
    param([int]$Idx, [string]$Name, [string]$OutPath)
    $args = @(
        '--headless',
        '--start-preset', $Idx,
        '--screenshot', $OutPath,
        '--screenshot-frame', $ScreenshotFrame,
        '--frames', $Frames
    )
    & $Exe @args 2>&1 | Out-Null
    if (-not (Test-Path $OutPath)) {
        Write-Warning "Preset ${Idx}/${Name}: screenshot NOT written"
        return $false
    }
    $size = (Get-Item $OutPath).Length
    if ($size -lt 50000) {
        Write-Warning "Preset ${Idx}/${Name}: screenshot too small ($size bytes - likely fully black)"
    }
    return $true
}

function Compute-Psnr {
    param([string]$BaselinePath, [string]$CurrentPath)
    # Use the demo's --diff utility (Application::TryDiffImages). The
    # engine prints `DIFF_STATS: PSNR=NN.NN dB diff=NN.NN%% maxCh=NNN`
    # to STDOUT after generating the diff PNG. The diff PNG is discarded;
    # we only want the PSNR value.
    $diffOut = Join-Path $env:TEMP ("vfxdiff_" + [System.IO.Path]::GetRandomFileName() + ".png")
    $output = & $Exe --diff $diffOut $BaselinePath 'Baseline' $CurrentPath 'Current' 2>&1 | Out-String
    Remove-Item $diffOut -Force -ErrorAction SilentlyContinue
    if ($output -match 'DIFF_STATS:\s*PSNR=([\d.]+)\s*dB') {
        return [double]$Matches[1]
    }
    Write-Warning "Could not parse DIFF_STATS PSNR from --diff stdout"
    return -1.0
}

if ($Mode -eq 'capture') {
    Write-Host "== Capturing baseline for $($Presets.Count) presets ==" -ForegroundColor Cyan
    $entries = @()
    for ($i = 0; $i -lt $Presets.Count; $i++) {
        $name = $Presets[$i]
        $fileName = "{0:D2}-{1}.png" -f $i, $name
        $outPath = Join-Path $BaselineFull $fileName
        Write-Host ("[{0:D2}] {1}" -f $i, $name) -ForegroundColor Yellow
        $ok = Capture-Preset -Idx $i -Name $name -OutPath $outPath
        if ($ok) {
            $size = (Get-Item $outPath).Length
            $hash = (Get-FileHash $outPath -Algorithm SHA256).Hash
            $entries += [PSCustomObject]@{
                index = $i
                name  = $name
                file  = $fileName
                bytes = $size
                sha256 = $hash
            }
        }
    }
    $entries | ConvertTo-Json -Depth 3 | Set-Content -Path $Manifest -Encoding utf8
    Write-Host "Baseline manifest written: $Manifest" -ForegroundColor Green
    Write-Host ("Captured {0}/{1} presets." -f $entries.Count, $Presets.Count)
    exit 0
}

# --- Validate mode ---
if (-not (Test-Path $Manifest)) {
    Write-Error "Baseline manifest not found at $Manifest - run with -Mode capture first."
}
$baseline = Get-Content $Manifest | ConvertFrom-Json
Write-Host "== Validating $($baseline.Count) presets against baseline ==" -ForegroundColor Cyan

$tmpDir = Join-Path $env:TEMP "vfx_validate_$(Get-Random)"
New-Item -ItemType Directory -Path $tmpDir | Out-Null

$pass = 0
$fail = 0
$results = @()
foreach ($entry in $baseline) {
    $current = Join-Path $tmpDir $entry.file
    $baselinePath = Join-Path $BaselineFull $entry.file
    Capture-Preset -Idx $entry.index -Name $entry.name -OutPath $current | Out-Null
    if (-not (Test-Path $current)) {
        Write-Host ("[{0:D2}] {1,-15} FAIL (no screenshot)" -f $entry.index, $entry.name) -ForegroundColor Red
        $fail++
        continue
    }
    $psnr = Compute-Psnr -BaselinePath $baselinePath -CurrentPath $current
    $threshold = if ($PerPresetThreshold.ContainsKey($entry.name)) { $PerPresetThreshold[$entry.name] } else { $PsnrThreshold }
    $ok = ($psnr -ge $threshold)
    $color = if ($ok) { 'Green' } else { 'Red' }
    $verdict = if ($ok) { 'PASS' } else { 'FAIL' }
    $thresholdNote = if ($PerPresetThreshold.ContainsKey($entry.name)) { " (override={0:F1} dB)" -f $threshold } else { "" }
    Write-Host ("[{0:D2}] {1,-15} {2,-4}  PSNR={3:F2} dB{4}" -f $entry.index, $entry.name, $verdict, $psnr, $thresholdNote) -ForegroundColor $color
    if ($ok) { $pass++ } else { $fail++ }
    $results += [PSCustomObject]@{
        index = $entry.index
        name  = $entry.name
        psnr  = $psnr
        pass  = $ok
    }
}

Remove-Item $tmpDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host ""
$summaryColor = if ($fail -eq 0) { 'Green' } else { 'Red' }
Write-Host ("== Summary: {0} pass / {1} fail (threshold={2:F1} dB) ==" -f $pass, $fail, $PsnrThreshold) -ForegroundColor $summaryColor

if ($fail -gt 0) { exit 1 } else { exit 0 }
