. (Join-Path $PSScriptRoot 'quality_common.ps1')
Push-Location (Join-Path $Root 'runtime')
try { Invoke-QualityCommand (Get-QualityZig) (@('build', 'test') + $args) }
finally { Pop-Location }
