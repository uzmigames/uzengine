param([string]$Label='current')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/sdf_reflections.hlsli')
$trace=[regex]::Match($source,'(?ms)^float TraceSdfRay\(.*?^}').Value
if(-not $trace){throw 'Production dynamic trace missing'}
$code=@'
#include <algorithm>
#include <cmath>
#include <cstdio>
struct float2 {float x,y;};
struct float3 {float x,y,z;};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool all(bool a){return a;}
bool isfinite(float2 a){return std::isfinite(a.x)&&std::isfinite(a.y);}
float max(float a,float b){return std::max(a,b);}
float min(float a,float b){return std::min(a,b);}
struct SdfReflectionScene {float voxelSize=1;unsigned traceSteps=256;};
int geometry=0,samples=0;float gap=22;
float2 SampleSdfRayDistances(SdfReflectionScene,float3 p){
    ++samples;float raw=std::abs(p.z);
    if(geometry==1)raw=std::min(raw,std::abs(p.z-gap));
    if(geometry==2)raw=std::abs(100-std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z)}));
    return {raw,raw-10};
}
TRACE
int main(){
    int failed=0,total=0;
    auto test=[&](const char* name,unsigned kind,float3 origin,float3 dir,float3 normal,float maxT,int expectation,float limit){
        samples=0;const float t=TraceSdfRay({},origin,dir,maxT,kind,normal);
        bool pass=expectation==0?(t>=0&&t<=limit):t==float(expectation);
        ++total;if(!pass)++failed;
        std::printf("%s %s type=%u t=%g samples=%d maxT=%g\n",pass?"PASS":"FAIL",name,kind,t,samples,maxT);
    };
    for(unsigned kind:{0u,1u,2u}){
        geometry=0;test("perpendicular plane escape",kind,{0,0,0},{0,0,1},{0,0,1},100,-1,0);
        geometry=1;for(float width:{2.f,22.f}){gap=width;test("adjacent plane blocks",kind,{0,0,0},{0,0,1},{0,0,1},100,0,gap);}
        geometry=2;test("closed box from surface",kind,{-100,0,0},{1,0,0},{1,0,0},400,0,200);
    }
    geometry=0;
    test("wrong geometric hemisphere blocked",0,{0,0,0},{0,0,-1},{0,0,1},100,-2,0);
    test("moderate grazing reflection",1,{0,0,0},{0.8660254f,0,0.5f},{0,0,1},100,-1,0);
    test("extreme grazing reflection budget",1,{0,0,0},{0.99995f,0,0.01f},{0,0,1},100,-1,0);
    std::printf("Dynamic trace physics: %d/%d passed\n",total-failed,total);
    return failed?1:0;
}
'@
$code=$code.Replace('TRACE',($trace -replace '\[loop\]','' -replace '\buint\b','unsigned'))
$artifacts=Join-Path $PSScriptRoot 'test_sdf_dynamic_origin_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$cpp=Join-Path $artifacts ('trace-'+$Label+'.cpp');$exe=Join-Path $artifacts ('trace-'+$Label+'.exe')
[IO.File]::WriteAllText($cpp,$code)
$zig=(Get-Command zig).Source
& $zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE-ne0){throw 'Trace regression compile failed'}
& $exe | Tee-Object -FilePath (Join-Path $artifacts ('trace-'+$Label+'.log'))
exit $LASTEXITCODE
