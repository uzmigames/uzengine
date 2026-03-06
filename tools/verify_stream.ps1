# verify_stream.ps1 — Start UzEngine and verify if WebSocket stream is active.

param(
    [string]$ExePath = "$PSScriptRoot\..\build\Debug\render.exe",
    [string]$Scene = "assets/scenes/materials.uzscene",
    [int]$Port = 9002,
    [int]$TimeoutSec = 10
)

Write-Host "[verify] Starting engine with scene: $Scene"
$proc = Start-Process -FilePath $ExePath -ArgumentList "--scene $Scene --ws-port $Port" -PassThru -WindowStyle Hidden

$startTime = Get-Date
$connected = $false
$receivedData = $false

Write-Host "[verify] Waiting for WebSocket on port $Port..."

while (((Get-Date) - $startTime).TotalSeconds -lt $TimeoutSec) {
    try {
        $client = New-Object System.Net.Sockets.TcpClient("localhost", $Port)
        if ($client.Connected) {
            $connected = $true
            $client.Close()
            break
        }
    } catch {
        Start-Sleep -Milliseconds 500
    }
}

if (-not $connected) {
    Write-Host "[verify] ERROR: Could not connect to port $Port within $TimeoutSec seconds."
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    exit 1
}

Write-Host "[verify] Port $Port is open. Engine seems to be running."
Write-Host "[verify] Success. The viewport-test.html should be able to connect."

# Clean up
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
exit 0
