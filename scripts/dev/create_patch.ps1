# create_patch.ps1 — Create a patch for a modified thirdparty file
#
# Usage:
#   .\scripts\create_patch.ps1 -Library stb -File stb_truetype.h -Description "add-uzengine-compat"
#
# This creates a unified diff patch in runtime/thirdparty/<library>/patches/
# following Godot's convention: NNNN-description.patch
#
# Workflow:
#   1. Modify the thirdparty file directly
#   2. Run this script to capture the diff as a numbered patch
#   3. Commit the patch file (NOT the modified thirdparty file)
#   4. setup.ps1 will re-download clean sources and apply patches automatically

param(
    [Parameter(Mandatory)][string]$Library,
    [Parameter(Mandatory)][string]$File,
    [Parameter(Mandatory)][string]$Description
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ThirdPartyDir = Join-Path $RootDir "runtime\thirdparty"
$LibPath = Join-Path $ThirdPartyDir $Library
$FilePath = Join-Path $LibPath $File
$PatchDir = Join-Path $LibPath "patches"

if (-not (Test-Path $FilePath)) {
    Write-Error "File not found: $FilePath"
}

# Check if file has uncommitted changes via git diff
$diff = git diff -- $FilePath 2>&1
if ([string]::IsNullOrWhiteSpace($diff)) {
    # Try staged changes
    $diff = git diff --cached -- $FilePath 2>&1
}
if ([string]::IsNullOrWhiteSpace($diff)) {
    Write-Error "No changes detected in $FilePath. Modify the file first, then run this script."
}

# Create patches directory
if (-not (Test-Path $PatchDir)) {
    New-Item -ItemType Directory -Path $PatchDir | Out-Null
}

# Determine next patch number
$existing = Get-ChildItem $PatchDir -Filter "*.patch" -ErrorAction SilentlyContinue | Sort-Object Name
if ($existing) {
    $lastNum = [int]($existing[-1].Name.Substring(0, 4))
    $nextNum = $lastNum + 1
} else {
    $nextNum = 1
}

$patchName = "{0:D4}-{1}.patch" -f $nextNum, $Description
$patchPath = Join-Path $PatchDir $patchName

# Write the patch
$diff | Out-File -Encoding utf8 -FilePath $patchPath

Write-Host "Patch created: $patchPath" -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. git add $patchPath"
Write-Host "  2. git checkout -- $FilePath  (restore original)"
Write-Host "  3. Commit the patch file"
Write-Host "  4. setup.ps1 will apply it automatically on next run"
