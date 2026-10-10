$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts=Join-Path $PSScriptRoot 'test_filtered_gather_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$surface=Get-Content -Raw (Join-Path $root 'runtime/shaders/runtime/ssgi/ssgi_surface.hlsli')
function ExtractFunction([string]$Source,[string]$Signature) {
 $start=$Source.IndexOf($Signature)
 if($start -lt 0){throw "Function missing: $Signature"}
 $brace=$Source.IndexOf('{',$start); $depth=1; $end=$brace+1
 while($depth -gt 0){ if($Source[$end] -eq '{'){$depth++}; if($Source[$end] -eq '}'){$depth--}; $end++ }
 return $Source.Substring($start,$end-$start)
}
$functions=@('float SurfacePlaneTolerance','bool SurfaceCompatible','float SurfaceWeight') | ForEach-Object { ExtractFunction $surface $_ }
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'test_filtered_gather.cpp')
$code=$template.Replace('PRODUCTION_FUNCTIONS',($functions -join "`n"))
$code=$code.Replace('max(1.0,','max(1.0f,').Replace('* 0.001)', '* 0.001f)')
$cpp=Join-Path $artifacts 'surface.cpp'; $exe=Join-Path $artifacts 'surface.exe'
[IO.File]::WriteAllText($cpp,$code)
& zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE -ne 0){throw 'Surface contract harness failed to compile.'}
& $exe | Tee-Object (Join-Path $artifacts 'surface.log')
if($LASTEXITCODE -ne 0){throw 'Surface contracts failed.'}
$dxc=Join-Path $root 'runtime/thirdparty/dxc/bin/x64/dxc.exe'
foreach($file in @('ssgi_trace_cs','ssgi_spatial_denoise_cs','ssgi_temporal_accum_cs','ssgi_upsample_cs')){
 & $dxc -T cs_6_6 -E main -D UZ_BINDLESS=1 (Join-Path $root "runtime/shaders/runtime/ssgi/$file.hlsl") -Fo (Join-Path $artifacts "$file.dxil")
 if($LASTEXITCODE -ne 0){throw "Shader failed: $file"}
 Write-Output "PASS shader $file"
}
