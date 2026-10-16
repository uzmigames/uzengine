# Path-tracer visual regression gate (phase0_path-tracer-runtime-mode, tasks 11.4/11.6).
# 1) Falcor PSNR roughness-row gate (compare_pt.py); 2) reflection-reference PT golden PSNR.
# Requires the RTX GPU + a built uzengine.exe. Non-zero exit on any failure.
$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$exe  = Join-Path $repo "runtime/build/bin/uzengine.exe"
$py   = "python"

Write-Host "== PT Falcor PSNR roughness-row gate =="
& $py -I (Join-Path $repo "scripts/falcor/compare_pt.py") --render
if ($LASTEXITCODE -ne 0) { Write-Error "Falcor PSNR gate FAILED"; exit 1 }

Write-Host "== PT reflection-reference golden regression =="
$golden = Join-Path $repo ".sandbox/golden/path_tracer/reflection_reference.png"
$outDir = Join-Path $repo ".screenshots/pt-visual-test"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$fresh  = Join-Path $outDir "reflref.png"
& $exe --project (Join-Path $repo "scripts/rendering/reflection-reference.uzproject") `
       --screenshot $fresh --screenshot-frame 300 --frames 320 `
       --cvar r.PathTracer.Enable=1 --cvar r.RayTracing=1
& $py -I (Join-Path $repo "scripts/falcor/reflref_regression.py") $golden $fresh 30
if ($LASTEXITCODE -ne 0) { Write-Error "reflection-reference regression FAILED"; exit 1 }

Write-Host "PT visual regression gate: PASS"
