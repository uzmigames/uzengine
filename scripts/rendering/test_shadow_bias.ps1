param([string]$Label = 'after')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/lighting/shadow.hlsli')
$offset = [regex]::Match($source, 'float normalBias = GetCascadeNormalBias\(cascadeIdx\);[\s\S]*?(?=\s*float4 lsp)').Value
$depth = [regex]::Match($source, 'float depthBias = [^;]+;').Value
if (-not $offset -or -not $depth) { throw 'Production shadow bias expressions not found.' }
$dir = Join-Path $PSScriptRoot 'test_shadow_bias_artifacts'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$code = @'
#include <algorithm>
#include <cmath>
#include <cstdio>
float max(float a,float b){return std::max(a,b);}
float min(float a,float b){return std::min(a,b);}
float clamp(float v,float a,float b){return std::clamp(v,a,b);}
float saturate(float v){return clamp(v,0,1);}
struct float3 {float x,y,z;};
float3 operator*(float3 v,float s){return {v.x*s,v.y*s,v.z*s};}
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float GetCascadeNormalBias(unsigned){return 2.0f;}
float requestedDepthBias=0;
float GetCascadeDepthBias(unsigned){return requestedDepthBias;}
void Calculate(float NdotL,float& displacement,float& slope,float& depthResult){
    unsigned cascadeIdx=0;
    float3 N{0,0,1},worldPos{0,0,0};
    OFFSET_EXPRESSION
    DEPTH_EXPRESSION
    displacement=biasedPos.z; slope=slopeScale; depthResult=depthBias;
}
int main(){
    const float angles[]={1,0.9f,0.7f,0.5f,0.1f,0.01f,0,-0.1f,1.1f};
    int failed=0,total=0;
    for(float c:angles){
        float displacement,slope,depth;
        requestedDepthBias=0.00001f;
        Calculate(c,displacement,slope,depth);
        const double cosine=std::clamp(double(c),0.0,1.0);
        const double expected=2.0*std::sqrt(1.0-cosine*cosine);
        const bool ok=std::isfinite(displacement)&&std::abs(displacement-expected)<1e-5&&slope>=0&&slope<=2;
        ++total; if(!ok)++failed;
        std::printf("%s NoL=%.3f normalOffset=%.6f expected=%.6f slope=%.6f\n",ok?"PASS":"FAIL",c,displacement,expected,slope);
    }
    float displacement,slope,depth;
    requestedDepthBias=0.00001f;
    Calculate(0.5f,displacement,slope,depth);
    ++total; if(std::abs(depth-requestedDepthBias)>1e-8)++failed;
    std::printf("Shadow bias: %d/%d passed; requestedDepth=%.8f actualDepth=%.8f\n",total-failed,total,requestedDepthBias,depth);
    return failed?1:0;
}
'@
$cpp = Join-Path $dir ($Label + '.cpp')
$exe = Join-Path $dir ($Label + '.exe')
[IO.File]::WriteAllText($cpp, $code.Replace('OFFSET_EXPRESSION', $offset).Replace('DEPTH_EXPRESSION', $depth))
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) { $zig = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName }
if (-not $zig) { throw 'Zig compiler not found.' }
& $zig c++ -std=c++23 -O0 $cpp -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Shadow bias harness compilation failed.' }
& $exe | Tee-Object -FilePath (Join-Path $dir ($Label + '.log'))
exit $LASTEXITCODE
