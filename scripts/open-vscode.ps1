# Opens VSCode with UzEngine extension in development mode
# Usage: .\scripts\open-vscode.ps1 [-Port 9229] [-Folder .]

param(
    [int]$Port = 9229,
    [string]$Folder = (Resolve-Path "$PSScriptRoot\..")
)

$vscodePath = "f:/IDEs/Microsoft VS Code/Code.exe"
$extPath = Resolve-Path "$PSScriptRoot\..\extension"

Start-Process $vscodePath -ArgumentList `
    "--extensionDevelopmentPath=`"$extPath`"", `
    "--remote-debugging-port=$Port", `
    "`"$Folder`""
