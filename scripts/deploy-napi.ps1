# Deploy N-API addon + DLLs to extension/native/
# Run this AFTER closing VSCode (to unlock uzengine.node)
#
# Usage: powershell -ExecutionPolicy Bypass -File scripts/deploy-napi.ps1

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = "$root/napi/build/Release"
$dst = "$root/extension/native"

if (-not (Test-Path "$src/uzengine.node")) {
    Write-Host "ERROR: $src/uzengine.node not found - build napi first" -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $dst | Out-Null
New-Item -ItemType Directory -Force -Path "$dst/D3D12" | Out-Null

Write-Host "Deploying N-API addon..." -ForegroundColor Yellow

Copy-Item "$src/uzengine.node" "$dst/" -Force
Write-Host "  uzengine.node ($('{0:N0} KB' -f ((Get-Item "$dst/uzengine.node").Length / 1KB)))" -ForegroundColor Green

Get-ChildItem "$src/*.dll" | ForEach-Object {
    Copy-Item $_.FullName "$dst/" -Force
    Write-Host "  $($_.Name)" -ForegroundColor Green
}

if (Test-Path "$src/D3D12") {
    Get-ChildItem "$src/D3D12/*.dll" | ForEach-Object {
        Copy-Item $_.FullName "$dst/D3D12/" -Force
        Write-Host "  D3D12/$($_.Name)" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "Done. Open VSCode and press F5 to test." -ForegroundColor Cyan
