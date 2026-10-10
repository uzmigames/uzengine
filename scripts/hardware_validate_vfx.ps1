# Hardware validation runbook for the 4 VFX hardware-pending follow-ups.
#
# Run from the repo root in a real Windows PowerShell terminal (NOT inside
# the Claude Code sandbox — DXGI fails there). Each section validates one
# follow-up task and prints PASS/FAIL.
#
# Usage:
#   cd F:\UzmiEngine\uzengine
#   powershell -ExecutionPolicy Bypass -File scripts\hardware_validate_vfx.ps1

$ErrorActionPreference = "Continue"
$Repo = "F:\UzmiEngine\uzengine"
$Bin  = "$Repo\runtime\demos\vfx_viewport_app\build\bin"
$Test = "$Repo\runtime\build\bin\test_runtime.exe"
$App  = "$Bin\vfx_viewport_app.exe"
$Shots = "$env:LOCALAPPDATA\UzEngine\screenshots"

function Section($name) {
    Write-Host ""
    Write-Host "===== $name =====" -ForegroundColor Cyan
}

function Check($label, $condition) {
    if ($condition) {
        Write-Host "  [PASS] $label" -ForegroundColor Green
    } else {
        Write-Host "  [FAIL] $label" -ForegroundColor Red
    }
}

# ── Build sanity ───────────────────────────────────────────────────────────────
Section "0. Sanity — runtime + viewport binary present"
Check "test_runtime.exe exists"      (Test-Path $Test)
Check "vfx_viewport_app.exe exists"  (Test-Path $App)

# ── Sort pipeline (sort_cs alive detection + mesh deterministic slot) ─────────
Section "1. Sort pipeline (phase16_vfx-sort-pipeline-hardware-validation)"
& $App --frames 60 --screenshot vfx_sort_baseline.png --screenshot-frame 30 2>&1 | Out-Null
Check "Baseline screenshot captured"  (Test-Path "$Shots\vfx_sort_baseline.png")

& $App --frames 60 --screenshot vfx_sort_diag.png --screenshot-frame 30 --diag 2>&1 | Tee-Object -Variable diagOut
$diagJson = ($diagOut | Where-Object { $_ -match 'DIAG_JSON:' })
Check "Diag JSON emitted"             ($null -ne $diagJson)
Check "No FAIL_NAN/FAIL_INF reported" ($diagJson -notmatch 'FAIL_NAN|FAIL_INF')

# ── Scalability CVars (live runtime knobs) ────────────────────────────────────
Section "2. Scalability (phase16_vfx-scalability-hardware-validation)"
& $App --frames 30 --screenshot vfx_paused_a.png --screenshot-frame 15 `
       --cvar r.VFX.SimulatePaused=1 2>&1 | Out-Null
& $App --frames 30 --screenshot vfx_paused_b.png --screenshot-frame 25 `
       --cvar r.VFX.SimulatePaused=1 2>&1 | Out-Null
Check "Paused frame N captured"  (Test-Path "$Shots\vfx_paused_a.png")
Check "Paused frame N+10 captured" (Test-Path "$Shots\vfx_paused_b.png")

# Compare the two paused frames — particles should be identical.
$a = (Get-FileHash "$Shots\vfx_paused_a.png" -Algorithm MD5).Hash
$b = (Get-FileHash "$Shots\vfx_paused_b.png" -Algorithm MD5).Hash
Check "Paused frames are identical (MD5 match)" ($a -eq $b)

& $App --frames 30 --screenshot vfx_timescale_2x.png --screenshot-frame 20 `
       --cvar r.VFX.TimeScale=2.0 2>&1 | Out-Null
Check "TimeScale=2.0 frame captured"   (Test-Path "$Shots\vfx_timescale_2x.png")

& $App --frames 30 --screenshot vfx_quality_low.png --screenshot-frame 20 `
       --cvar r.VFX.QualityLevel=0 2>&1 | Out-Null
Check "QualityLevel=0 frame captured"  (Test-Path "$Shots\vfx_quality_low.png")

# ── Spline data interface (CB plumbing pending — this only smoke-tests load) ──
Section "3. Spline data interface (phase16_vfx-spline-data-interface-hardware-validation)"
Write-Host "  NOTE: spawn/update CS CB extension is hardware-pending —" -ForegroundColor Yellow
Write-Host "        modules load + register but do not yet drive position." -ForegroundColor Yellow
& $App --frames 30 --screenshot vfx_spline_smoke.png --screenshot-frame 20 2>&1 | Out-Null
Check "Smoke frame captured (no crash)" (Test-Path "$Shots\vfx_spline_smoke.png")

# ── Lumen GI emissive injection (probe-update CS reads pending) ───────────────
Section "4. Lumen GI emissive injection (phase16_vfx-lumen-gi-hardware-validation)"
Write-Host "  NOTE: DDGI probe-update CS does not yet read the SRV — gather" -ForegroundColor Yellow
Write-Host "        + upload run, but bounce light is still placeholder." -ForegroundColor Yellow
& $App --frames 60 --screenshot vfx_ddgi_off.png --screenshot-frame 50 `
       --cvar r.VFX.DDGIInjection=0 2>&1 | Out-Null
& $App --frames 60 --screenshot vfx_ddgi_on.png  --screenshot-frame 50 `
       --cvar r.VFX.DDGIInjection=1 2>&1 | Out-Null
Check "DDGI off baseline captured"  (Test-Path "$Shots\vfx_ddgi_off.png")
Check "DDGI on captured"            (Test-Path "$Shots\vfx_ddgi_on.png")

# ── Test suite ────────────────────────────────────────────────────────────────
Section "5. Full unit test sweep"
& $Test --gtest_filter="*VFX*:*Sort*:*Mesh*:*Spline*:*DDGI*:UZ_*:Uzfx*" 2>&1 | Tee-Object -Variable testOut
$summary = ($testOut | Where-Object { $_ -match '\[==========\]' } | Select-Object -Last 1)
Write-Host "  Summary: $summary" -ForegroundColor Cyan

Section "Done"
Write-Host "Screenshots saved to: $Shots" -ForegroundColor Cyan
Write-Host "Open the folder and inspect the captures to confirm visual A/B."
