param([string]$Label='after')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/sdf_reflections.hlsli')
$epsilon=[regex]::Match($source,'(?ms)^float SdfReflectionEpsilon\(.*?^}').Value
$resolve=[regex]::Match($source,'(?ms)^bool ResolveSdfRayOrigin\(.*?^}').Value
if(-not $epsilon -or -not $resolve){throw 'Production origin functions missing'}
$code=@'
#include <algorithm>
#include <cmath>
#include <cstdio>
struct float3 {float x,y,z;};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
bool all(bool a){return a;}
bool isfinite(float3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
float max(float a,float b){return std::max(a,b);}
struct SdfReflectionScene {float voxelSize=1,traceDistance=1000;};
struct Surface {float r;};
int geometry=0;
Surface SampleSdfReflectionSurface(SdfReflectionScene,float3 p){
    if(geometry==0)return {65504};
    if(geometry==1)return {std::abs(p.z)-10};
    if(geometry==2)return {std::min(std::abs(p.z),std::abs(p.z-22))-10};
    const float qr=std::abs(p.x)-30,qz=std::abs(p.z-100)-100;
    const float cylinder=std::hypot(std::max(qr,0.f),std::max(qz,0.f))+std::min(std::max(qr,qz),0.f);
    return {std::min(std::abs(cylinder),std::abs(p.z))-10};
}
EPSILON
RESOLVE
int main(){
    int failed=0;
    for(geometry=0;geometry<4;++geometry){
        float3 p=geometry==3?float3{30,0,50}:float3{0,0,0};
        float3 n=geometry==3?float3{0,0,-1}:float3{0,0,1};
        float3 origin{};bool valid=ResolveSdfRayOrigin({},p,n,origin);
        bool same=valid&&origin.x==p.x&&origin.y==p.y&&origin.z==p.z;
        if(!same)++failed;
        std::printf("%s geometry=%d receiver=(%g,%g,%g) origin=(%g,%g,%g) valid=%d\n",same?"PASS":"FAIL",geometry,p.x,p.y,p.z,origin.x,origin.y,origin.z,valid);
    }
    std::printf("Surface origin preservation: %d/4 passed\n",4-failed);
    return failed?1:0;
}
'@
$code=$code.Replace('EPSILON',$epsilon).Replace('RESOLVE',($resolve.Replace('out float3 origin','float3& origin') -replace '\[loop\]','' -replace '\buint\b','unsigned'))
$artifacts=Join-Path $PSScriptRoot 'test_sdf_dynamic_origin_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$cpp=Join-Path $artifacts ($Label+'.cpp');$exe=Join-Path $artifacts ($Label+'.exe')
[IO.File]::WriteAllText($cpp,$code)
$zig=(Get-Command zig -ErrorAction SilentlyContinue).Source
if(-not $zig){$zig=Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe')|Select-Object -First 1 -ExpandProperty FullName}
& $zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE-ne0){throw 'Origin regression compile failed'}
& $exe | Tee-Object -FilePath (Join-Path $artifacts ($Label+'.log'))
exit $LASTEXITCODE
