param([switch]$CheckEmptyBaseline, [switch]$CompileOnly)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_sdf_dynamic_gpu_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$zigCommand = Get-Command zig -ErrorAction SilentlyContinue
$zig = if ($zigCommand) { $zigCommand.Source } else {
    Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
if (!$zig) { throw 'Zig was not found on PATH or in the WinGet installation.' }
$dxc = Join-Path $repo 'runtime/thirdparty/dxc/bin/x64/dxc.exe'
$exe = Join-Path $artifacts 'test_sdf_gpu.exe'
$baker = Get-Content -Raw -LiteralPath (Join-Path $repo 'runtime/engine/renderer/gi/mesh_sdf_baker.cpp')
$orientation = [regex]::Match($baker, '(?ms)^static bool OrientSdfFaceNormal\b.*?^}')
$geometric = [regex]::Match($baker, '(?s)XMVECTOR v0 = XMLoadFloat3.*?XMStoreFloat3\(reinterpret_cast<XMFLOAT3\*>\(tri.normal\), normal\);')
if (!$orientation.Success -or !$geometric.Success -or ([regex]::Matches($baker, 'OrientSdfFaceNormal\(')).Count -lt 2) {
    throw 'Production triangle normal implementation or its call could not be extracted.'
}
$generated = $orientation.Value + @'

static std::array<float,3> ProductionTriangleNormal(const float* p0, const float* p1, const float* p2, uint32_t vertexNormalOffset) {
    using namespace DirectX;
    struct { float normal[3]; } tri{};
'@ + $geometric.Value + @'

    OrientSdfFaceNormal(tri.normal, p0, p1, p2, vertexNormalOffset);
    return {tri.normal[0], tri.normal[1], tri.normal[2]};
}
'@
$generated | Set-Content -LiteralPath (Join-Path $artifacts 'mesh_normal.generated.h')
& $zig c++ -std=c++17 -O2 -fms-extensions '-D__REQUIRED_RPCNDR_H_VERSION__=475' (Join-Path $PSScriptRoot 'test_sdf_gpu.cpp') '-I' (Join-Path $repo 'runtime/thirdparty/directx_headers/include/directx') '-I' (Join-Path $repo 'runtime/thirdparty/directxmath/Inc') -ld3d12 -ldxgi -ldxguid -o $exe
if ($LASTEXITCODE -ne 0) { throw 'GPU regression harness compilation failed.' }
$shader = Join-Path $artifacts 'mesh_sdf_bake.dxil'
& $dxc -T cs_6_6 -E main (Join-Path $repo 'runtime/shaders/runtime/gi/mesh_sdf_bake_cs.hlsl') -Fo $shader
if ($LASTEXITCODE -ne 0) { throw 'Production mesh shader compilation failed.' }
if ($CheckEmptyBaseline -and !$CompileOnly) {
    $baselineSource = Join-Path $artifacts 'empty_baseline.hlsl'
    '[numthreads(4,4,4)] void main(uint3 id : SV_DispatchThreadID) {}' | Set-Content -LiteralPath $baselineSource
    $baselineShader = Join-Path $artifacts 'empty_baseline.dxil'
    & $dxc -T cs_6_6 -E main $baselineSource -Fo $baselineShader
    if ($LASTEXITCODE -ne 0) { throw 'Baseline shader compilation failed.' }
    & $exe $baselineShader
    if ($LASTEXITCODE -ne 1) { throw 'The empty baseline must fail semantic validation with exit code 1.' }
    Write-Output 'PASS regression sensitivity: empty shader rejected.'
}
$composite = Join-Path $artifacts 'global_sdf_composite.dxil'
& $dxc -T cs_6_6 -E main (Join-Path $repo 'runtime/shaders/runtime/gi/global_sdf_composite_cs.hlsl') -Fo $composite
if ($LASTEXITCODE -ne 0) { throw 'Production global SDF shader compilation failed.' }
$radiance = Join-Path $artifacts 'radiance_cache_trace.dxil'
& $dxc -T cs_6_6 -E main (Join-Path $repo 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl') -Fo $radiance
if ($LASTEXITCODE -ne 0) { throw 'Production radiance cache shader compilation failed.' }
$traceSource = Get-Content -Raw -LiteralPath (Join-Path $repo 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl')
$traceLayout = [regex]::Match($traceSource, '(?s)cbuffer TraceConstants : register\(b2\) \{.*?\};')
if (!$traceLayout.Success) { throw 'Production trace constant layout could not be extracted.' }
$reflectionSource = @'
#include "sdf_reflections.hlsli"
cbuffer TestRoots : register(b0) {
    uint outputSrv, previousSrv, frame, probeOffset, probeBudget, skySrv, pad0, pad1;
};
'@ + $traceLayout.Value + @'

[numthreads(8,1,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= 7) return;
    SdfReflectionScene scene = (SdfReflectionScene)0;
    scene.clipmaps[0] = float4(gdfCenter0X,gdfCenter0Y,gdfCenter0Z,gdfExtent0);
    scene.clipmaps[1] = float4(gdfCenter1X,gdfCenter1Y,gdfCenter1Z,gdfExtent1);
    scene.clipmaps[2] = float4(gdfCenter2X,gdfCenter2Y,gdfCenter2Z,gdfExtent2);
    scene.clipmaps[3] = float4(gdfCenter3X,gdfCenter3Y,gdfCenter3Z,gdfExtent3);
    scene.clipmapSrv = uint4(gdfClipmapSrv0,gdfClipmapSrv1,gdfClipmapSrv2,gdfClipmapSrv3);
    scene.rawClipmapSrv = 9;
    scene.skySrv = skySrv;
    scene.probeSrv = firstWrite != 0 ? 0xffffffffu : previousSrv;
    scene.probesPerAxis = probesPerAxis;
    scene.traceSteps = id.x == 6 ? 0 : 128;
    scene.probeOrigin = float3(gridOriginX,gridOriginY,gridOriginZ);
    scene.probeExtent = probeSpacing * float(probesPerAxis - 1) * 0.5;
    scene.voxelSize = gdfExtent0 * 2.0 / 32.0;
    scene.traceDistance = maxTraceDistance;
    scene.sunDirection = float3(lightDirX,lightDirY,lightDirZ);
    scene.sunColor = float3(lightColorR,lightColorG,lightColorB) * lightIntensity;
    scene.skyMultiplier = float3(skyColorR,skyColorG,skyColorB);
    float3 direction = 0;
    direction[min(id.x / 2,2)] = (id.x & 1) != 0 ? -1.0 : 1.0;
    float3 origin = scene.clipmaps[0].xyz;
    float3 radiance = TraceSdfReflectionRadiance(scene,origin,direction);
    float distance = TraceSdfReflection(scene,origin,direction,scene.traceDistance);
    RWStructuredBuffer<float4> output = ResourceDescriptorHeap[outputSrv];
    output[probeOffset * 7 + id.x] = float4(radiance,distance);
}
'@
$reflectionWrapper = Join-Path $artifacts 'sdf_reflections_test.hlsl'
$reflectionSource | Set-Content -LiteralPath $reflectionWrapper
$reflections = Join-Path $artifacts 'sdf_reflections_test.dxil'
& $dxc -T cs_6_6 -E main -I (Join-Path $repo 'runtime/shaders/runtime/gi') $reflectionWrapper -Fo $reflections
if ($LASTEXITCODE -ne 0) { throw 'Production reflection helper wrapper compilation failed.' }
if ($CompileOnly) { exit 0 }
& $exe $shader $composite $radiance
if ($LASTEXITCODE -ne 0) { throw 'Production mesh SDF GPU regression failed.' }
& $exe --reversed-winding-without-normals $shader
if ($LASTEXITCODE -ne 1) { throw 'Reversed winding without supplied normal orientation must fail signed distance validation.' }
Write-Output 'PASS regression sensitivity: reversed winding without vertex normals rejected.'
& $exe --reversed-winding $shader $composite $radiance
if ($LASTEXITCODE -ne 0) { throw 'Production baker normal orientation regression failed.' }
& $exe --signed-plane $shader
if ($LASTEXITCODE -ne 1) { throw 'Signed open plane must fail unsigned distance validation on its back side.' }
Write-Output 'PASS regression sensitivity: signed open plane rejects back-side false interior.'
& $exe --two-sided-plane $shader $composite
if ($LASTEXITCODE -ne 0) { throw 'Production two-sided open-plane regression failed.' }
& $exe --feedback-room-without-history $shader $composite $radiance
if ($LASTEXITCODE -ne 1) { throw 'Dark enclosed room without previous irradiance must fail the reflected-light expectation.' }
Write-Output 'PASS regression sensitivity: missing previous irradiance rejected.'
& $exe --feedback-room-first-write $shader $composite $radiance
if ($LASTEXITCODE -ne 0) { throw 'First-write enclosed room must ignore NaN history.' }
& $exe --feedback-room $shader $composite $radiance
if ($LASTEXITCODE -ne 0) { throw 'Previous irradiance must produce colored reflected light in an enclosed room.' }
foreach ($scenario in @('--reflection-empty','--reflection-dark-room','--reflection-room','--reflection-transformed-room','--reflection-mixed-validity-room')) {
    & $exe $scenario $shader $composite $reflections
    if ($LASTEXITCODE -ne 0) { throw "Production world reflection regression failed: $scenario" }
}

& $exe --feedback-room-mixed-validity $shader $composite $radiance
if ($LASTEXITCODE -ne 0) { throw 'Invalid probes must be excluded and valid probe feedback renormalized.' }
