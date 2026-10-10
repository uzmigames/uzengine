param([switch]$CompileOnly, [switch]$BvhCompare)
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$artifacts = Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
& (Join-Path $PSScriptRoot 'test_sdf_gpu.ps1') -CompileOnly
if ($LASTEXITCODE -ne 0) { throw 'Shared harness compilation failed.' }
$zigCommand = Get-Command zig -ErrorAction SilentlyContinue
$zig = if ($zigCommand) { $zigCommand.Source } else {
    Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
$exe = Join-Path $artifacts 'test_sdf_window.exe'
& $zig c++ -std=c++17 -O2 -fms-extensions '-D__REQUIRED_RPCNDR_H_VERSION__=475' (Join-Path $PSScriptRoot 'test_sdf_window.cpp') '-I' (Join-Path $repo 'runtime/thirdparty/directx_headers/include/directx') '-I' (Join-Path $repo 'runtime/thirdparty/directxmath/Inc') -ld3d12 -ldxgi -ldxguid -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Window harness compilation failed.' }
$wrapper = Join-Path $artifacts 'sdf_window_test.hlsl'
@'
#include "sdf_reflections.hlsli"
cbuffer TestRoots : register(b0) { uint outputSrv, previousSrv, frame, probeOffset, probeBudget, skySrv, pad0, pad1; };
[numthreads(8,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 7) return;
    SdfReflectionScene scene = (SdfReflectionScene)0;
    [unroll] for (uint i=0;i<4;++i) scene.clipmaps[i]=float4(0,0,0,2500);
    scene.clipmapSrv=6;
    scene.rawClipmapSrv=9;
    scene.skySrv=skySrv;
    scene.probeSrv=0xffffffffu;
    scene.traceSteps=id.x==6 ? 0 : 128;
    Texture3D<float4> globalVolume=ResourceDescriptorHeap[6];
    uint width,height,depth,levels;
    globalVolume.GetDimensions(0,width,height,depth,levels);
    scene.voxelSize=5000.0/float(width);
    scene.traceDistance=400;
    scene.skyMultiplier=1;
    float3 direction=float3(0,(id.x & 1) != 0 ? -1.0 : 1.0,0);
    float3 origin=-direction*200;
    float3 radiance=TraceSdfReflectionRadiance(scene,origin,direction);
    float distance=TraceSdfReflection(scene,origin,direction,400);
    RWStructuredBuffer<float4> output=ResourceDescriptorHeap[outputSrv];
    output[id.x]=float4(radiance,distance);
}
'@ | Set-Content -LiteralPath $wrapper
$dxc = Join-Path $repo 'runtime/thirdparty/dxc/bin/x64/dxc.exe'
$shader = Join-Path $artifacts 'sdf_window_test.dxil'
& $dxc -T cs_6_6 -E main -I (Join-Path $repo 'runtime/shaders/runtime/gi') $wrapper -Fo $shader
if ($LASTEXITCODE -ne 0) { throw 'Production window trace wrapper compilation failed.' }
if ($CompileOnly) { exit 0 }
if ($BvhCompare) {
    $meshShader = Join-Path $artifacts 'mesh_sdf_bake.dxil'
    $gdfShader = Join-Path $artifacts 'global_sdf_composite.dxil'
    & $exe 128 $meshShader $gdfShader $shader 256
    if ($LASTEXITCODE -ne 0) { throw 'Brute-force mesh128/GDF256 must preserve the aperture.' }
    & $exe 128 $meshShader $gdfShader $shader 256 --bvh
    if ($LASTEXITCODE -ne 0) { throw 'BVH mesh128/GDF256 must preserve the aperture.' }
    $brute = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $artifacts 'window_mesh_128_brute.r16')
    $bvh = Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $artifacts 'window_mesh_128_bvh.r16')
    if ($brute.Hash -ne $bvh.Hash) { throw 'BVH and brute-force GPU fields differ.' }
    Write-Output 'PASS BVH equivalence: all 2097152 fp16 voxels are bit-identical.'
    exit 0
}
$failed = $false
$needsMesh256 = $true
foreach ($pair in @(@(32,128),@(128,128),@(128,256),@(256,256))) {
    if ($pair[0] -eq 256 -and !$needsMesh256) { continue }
    $resolution=$pair[0]
    & $exe $resolution (Join-Path $artifacts 'mesh_sdf_bake.dxil') (Join-Path $artifacts 'global_sdf_composite.dxil') $shader $pair[1]
    if ($LASTEXITCODE -gt 1) { throw "Window GPU execution failed: resolution $resolution" }
    if ($pair[0] -eq 128 -and $pair[1] -eq 256) { $needsMesh256 = $LASTEXITCODE -eq 1 }
    if ($LASTEXITCODE -eq 1) { $failed = $true }
}
if ($failed) { exit 1 }
