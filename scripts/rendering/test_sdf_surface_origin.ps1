param([string]$Label='axis',[switch]$Tilted,[switch]$Reflections)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$shared=Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
$artifacts=Join-Path $PSScriptRoot 'test_sdf_dynamic_origin_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$wrapper=Get-Content -Raw -LiteralPath (Join-Path $shared 'sdf_reflections_test.hlsl')
$reference=@'
#include "sdf_diffuse_gather.hlsli"
// Analytic constant-room reference: directions outside the receiver's geometric
// hemisphere have zero incident radiance. Evaluate that exact condition at the
// same quadrature samples, preserving the existing channel-error tolerance.
float GeometricCoverage(float3 shading,float3 geometry,uint2 pixel,uint frame,bool glossy) {
    float3 up=abs(shading.z)<0.999?float3(0,0,1):float3(1,0,0);
    float3 tangent=normalize(cross(up,shading)),bitangent=cross(shading,tangent);
    uint count=glossy?16:32;
    uint seed=pixel.x*1664525u+pixel.y*1013904223u+frame*747796405u;
    float2 rotation=float2(RadicalInverseVdC(seed),RadicalInverseVdC(seed^0x9e3779b9u));
    float accepted=0,total=0;
    for(uint i=0;i<count;i++) {
        float3 direction;float weight=1;
        float2 sample=frac(Hammersley(i,count)+rotation);
        if(glossy) {
            float3 halfTangent=ImportanceSampleGGX(sample,1.0);
            float3 halfVector=tangent*halfTangent.x+bitangent*halfTangent.y+shading*halfTangent.z;
            direction=reflect(-shading,halfVector);
            weight=saturate(dot(shading,direction));
        } else {
            float pdf;float3 local=SampleCosineHemisphere(sample,pdf);
            direction=tangent*local.x+bitangent*local.y+shading*local.z;
        }
        total+=weight;
        if(dot(geometry,direction)>0)accepted+=weight;
    }
    return total>0?accepted/total:0;
}
'@
$call=@'
    float3 shadingNormal=direction;
    SHADING_OVERRIDE
    float3 receiver=origin-100.0*direction;
    float3 radiance=GATHER_CALL;
    float coverage=GeometricCoverage(shadingNormal,direction,uint2(id.x,0),frame,GLOSSY);
    // The harness expects reflectance for every channel; divide by the analytic
    // sample coverage so any lost valid ray still fails at the original tolerance.
    radiance=coverage>0?radiance/coverage:0;
'@
$override=if($Tilted){'float3 tangent=abs(direction.z)<0.999?normalize(cross(direction,float3(0,0,1))):float3(1,0,0); shadingNormal=normalize(0.5*direction+0.866025403784*tangent);'}else{''}
$gather=if($Reflections){'GatherSdfReflections(scene,receiver,shadingNormal,direction,shadingNormal,1.0,uint2(id.x,0),frame,16)'}else{'GatherSdfDiffuse(scene,receiver,shadingNormal,direction,uint2(id.x,0),frame,32,0u)'}
$glossy=if($Reflections){'true'}else{'false'}
$call=$call.Replace('SHADING_OVERRIDE',$override).Replace('GATHER_CALL',$gather).Replace('GLOSSY',$glossy)
$old='float3 radiance = TraceSdfReflectionRadiance(scene,origin,direction);'
if(-not $wrapper.Contains($old)){throw 'Shared fixture call missing'}
$source=$wrapper.Replace('cbuffer TestRoots',$reference+"`ncbuffer TestRoots").Replace($old,$call)
$sourcePath=Join-Path $artifacts ($Label+'.hlsl');$shaderPath=Join-Path $artifacts ($Label+'.dxil')
[IO.File]::WriteAllText($sourcePath,$source)
& (Join-Path $root 'runtime/thirdparty/dxc/bin/x64/dxc.exe') -T cs_6_6 -E main -WX -I (Join-Path $root 'runtime/shaders/runtime/gi') $sourcePath -Fo $shaderPath
if($LASTEXITCODE-ne0){throw 'Surface fixture compilation failed'}
$output=& (Join-Path $shared 'test_sdf_gpu.exe') --reflection-room (Join-Path $shared 'mesh_sdf_bake.dxil') (Join-Path $shared 'global_sdf_composite.dxil') $shaderPath
$code=$LASTEXITCODE
$output | Tee-Object -FilePath (Join-Path $artifacts ($Label+'.log'))
exit $code
