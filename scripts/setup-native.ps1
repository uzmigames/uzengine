# Locked Windows x64 baseline. Vendored source updates are separate from provisioning.
[CmdletBinding()]
param(
    [string]$Destination,
    [string]$Cache,
    [string]$WindowsSdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10",
    [string]$VcVars,
    [switch]$Offline,
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
foreach ($name in @('Destination', 'Cache', 'WindowsSdkRoot', 'VcVars')) {
    $value = Get-Variable -Name $name -ValueOnly
    if ($value) {
        Set-Variable -Name $name -Value $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($value)
    }
}
if (-not $VcVars) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Install Visual Studio C++ Build Tools.' }
    $VcVars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
}
$arguments = @('-I', '-X', 'utf8', (Join-Path $PSScriptRoot 'tools/provision_native.py'), '--root', $root,
    '--sdk', $WindowsSdkRoot, '--vcvars', $VcVars)
if ($Destination) { $arguments += @('--destination', $Destination) }
if ($Cache) { $arguments += @('--cache', $Cache) }
if ($Offline) { $arguments += '--offline' }
if ($VerifyOnly) { $arguments += '--verify-only' }
& python @arguments
if ($LASTEXITCODE -ne 0) { throw "Native provisioning failed (exit $LASTEXITCODE)." }
