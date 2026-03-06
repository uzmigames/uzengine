# capture_frame.ps1 -- Capture a frame from UzEngine via file-based IPC.
#
# Usage:
#   pwsh tools/capture_frame.ps1                          # Save to preview/
#   pwsh tools/capture_frame.ps1 -Output "my_frame.png"   # Save to custom path
#   pwsh tools/capture_frame.ps1 -Timeout 8000            # Custom timeout
#
# Requires: UzEngine running with IPC enabled.

param(
    [string]$Output = "",
    [int]$Timeout = 4000,
    [string]$PreviewDir = ""
)

$ErrorActionPreference = "Stop"

# Resolve preview directory
if (-not $PreviewDir) {
    $projectRoot = Split-Path -Parent $PSScriptRoot
    $PreviewDir = Join-Path $projectRoot "build" "Release" "preview"
}

$commandFile  = Join-Path $PreviewDir "command.json"
$responseFile = Join-Path $PreviewDir "response.json"

if (-not (Test-Path $PreviewDir)) {
    Write-Error "Preview directory not found: $PreviewDir. Is UzEngine running?"
    exit 1
}

# Generate unique ID for this request
$id = [System.Guid]::NewGuid().ToString()

# Write command
$command = @{
    id       = $id
    type     = "call"
    function = "engine.screenshot"
    args     = @{ id = $id }
} | ConvertTo-Json -Compress

$tmpFile = "$commandFile.tmp"
Set-Content -Path $tmpFile -Value $command -NoNewline
Move-Item -Path $tmpFile -Destination $commandFile -Force

# Poll for response
$deadline = (Get-Date).AddMilliseconds($Timeout)
$pngPath  = Join-Path $PreviewDir "frame_$id.png"
$found    = $false

while ((Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 100

    # Check if PNG file appeared
    if (Test-Path $pngPath) {
        $found = $true
        break
    }
}

if (-not $found) {
    Write-Error "Timeout: PNG not written within ${Timeout}ms"
    exit 1
}

# Copy to output if specified
if ($Output) {
    Copy-Item -Path $pngPath -Destination $Output -Force
    $result = @{
        ok   = $true
        path = (Resolve-Path $Output).Path
        size_bytes = (Get-Item $Output).Length
    }
} else {
    $result = @{
        ok   = $true
        path = (Resolve-Path $pngPath).Path
        size_bytes = (Get-Item $pngPath).Length
    }
}

$result | ConvertTo-Json -Compress
