$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$artifacts=Join-Path $PSScriptRoot 'test_gdf_raw_lifecycle_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$source=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/gi/global_distance_field.cpp')
$header=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/gi/global_distance_field.h')
function ExtractBlock([string]$Text,[string]$Signature){
 $start=$Text.IndexOf($Signature);if($start -lt 0){throw "Missing production block: $Signature"}
 $brace=$Text.IndexOf('{',$start);$depth=1;$end=$brace+1
 while($depth){if($Text[$end] -eq '{'){$depth++};if($Text[$end] -eq '}'){$depth--};$end++}
 return $Text.Substring($start,$end-$start)
}
$create=ExtractBlock $source 'bool GlobalDistanceField::CreateResources('
$destroy=ExtractBlock $source 'void GlobalDistanceField::DestroyResources('
$clip=(ExtractBlock $header 'struct ClipmapData')+';'
$constants=(ExtractBlock $header 'struct GdfCompositeConstants')+';'
$start=$source.IndexOf('    cmdList->SetPipeline(compositePipeline);');$end=$source.IndexOf('    if (m_meshDataHash != meshHash',$start)
$composite=$source.Substring($start,$end-$start)
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'test_gdf_raw_lifecycle.cpp')
$code=$template.Replace('PRODUCTION_CONSTANTS',$constants).Replace('PRODUCTION_CLIPMAP',$clip).Replace('PRODUCTION_CREATE',$create).Replace('PRODUCTION_DESTROY',$destroy).Replace('PRODUCTION_COMPOSITE',$composite)
$cpp=Join-Path $artifacts 'lifecycle.cpp';$exe=Join-Path $artifacts 'lifecycle.exe';[IO.File]::WriteAllText($cpp,$code)
& zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE -ne 0){throw 'Raw GDF lifecycle compile failed.'}
& $exe | Tee-Object (Join-Path $artifacts 'lifecycle.log')
exit $LASTEXITCODE
