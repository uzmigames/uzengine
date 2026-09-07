param([ValidateSet('Audit','Repair')][string]$Mode = 'Audit', [string]$Label = 'current')
$ErrorActionPreference = 'Stop'
if ($Label -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid label.' }
$repo = Split-Path $PSScriptRoot -Parent
$mesh = Join-Path $repo 'sample/content/meshes/sponza/decals_1st_floor1_dirt_decal_0.uzmesh'
$artifacts = Join-Path $PSScriptRoot 'sponza_decal_normal_repair_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$backup = Join-Path $artifacts 'decals_1st_floor1_dirt_decal_0.original.uzmesh'
Add-Type -Path (Join-Path $PSScriptRoot 'repair_sponza_decal_normals.cs')
if ($Mode -eq 'Repair') {
    if (!(Test-Path -LiteralPath $backup)) {
        Copy-Item -LiteralPath $mesh -Destination $backup
        Get-FileHash -LiteralPath $backup -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'original-sha256.json')
    }
    if ((Get-FileHash -LiteralPath $backup).Hash -ne ((Get-Content -Raw -LiteralPath (Join-Path $artifacts 'original-sha256.json') | ConvertFrom-Json).Hash)) { throw 'Backup integrity mismatch.' }
    [SponzaDecalNormals]::Repair($mesh)
}
$baseline = if (Test-Path -LiteralPath $backup) { $backup } else { $null }
$oldOutput = [Console]::Out
$capture = [IO.StringWriter]::new()
try {
    [Console]::SetOut($capture)
    $result = [SponzaDecalNormals]::Audit($mesh, $baseline)
} finally {
    [Console]::SetOut($oldOutput)
}
$capture.ToString().Trim() | Tee-Object -FilePath (Join-Path $artifacts ($Label + '.log'))
Get-FileHash -LiteralPath $mesh -Algorithm SHA256 | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts ($Label + '-sha256.json'))
exit $result
