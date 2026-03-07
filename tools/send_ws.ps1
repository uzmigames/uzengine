param([string]$json = '{"type":"benchmark.orbit","enable":true}', [int]$port = 9002)
$ws = New-Object System.Net.WebSockets.ClientWebSocket
$ct = New-Object System.Threading.CancellationTokenSource(5000)
$ws.ConnectAsync([Uri]"ws://127.0.0.1:$port", $ct.Token).Wait()
$bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
$buf = New-Object System.ArraySegment[byte](,$bytes)
$ws.SendAsync($buf, [System.Net.WebSockets.WebSocketMessageType]::Text, $true, $ct.Token).Wait()
Write-Output "Sent: $json"
Start-Sleep -Milliseconds 200
try { $ws.CloseOutputAsync([System.Net.WebSockets.WebSocketCloseStatus]::NormalClosure, '', $ct.Token).Wait() } catch {}
