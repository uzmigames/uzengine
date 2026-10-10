param([string]$Label = 'after')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source = Get-Content -Raw -LiteralPath (Join-Path $root 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl')
$trace = [regex]::Match($source, '(?ms)^float TraceGDF\(.*?^}').Value
$constants = [regex]::Matches($source, '(?m)^static const (?:float|uint)\s+k(?:SurfaceEpsilon|MinStep|MaxStepsPerRay)\s*=.*?;')
$primary = [regex]::Match($source, '(?s)float t = TraceGDF\(probePos.*?if \((t [^)]*)\)').Groups[1].Value
$surfaceMatch = [regex]::Match($source, '(?s)radiance = sky.SampleLevel.*?}\s*}\s*else(?: if \(([^)]*)\))? \{\s*float3 hitPosition')
$shadow = [regex]::Match($source, 'visibility = (TraceGDF\(shadowOrigin, L, maxTraceDistance\).*?);').Groups[1].Value
if (-not $trace -or $constants.Count -ne 3 -or -not $primary -or -not $surfaceMatch.Success -or -not $shadow) {
    throw 'Production trace or consumer branches could not be extracted.'
}
$surface = if ($surfaceMatch.Groups[1].Success) { $surfaceMatch.Groups[1].Value } else { 'true' }
$dir = Join-Path $PSScriptRoot 'test_rc_trace_exhaustion_artifacts'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$code = @'
#include <algorithm>
#include <cstdio>
using uint = unsigned;
struct float3 { float x,y,z; };
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float max(float a,float b){return std::max(a,b);}
CONSTANTS
float sampledDistance=0;
uint samples=0;
float SampleGdfDistance(float3){++samples;return sampledDistance;}
TRACE
bool ReceivesSky(float t){return PRIMARY;}
bool ShadesHit(float t){if(ReceivesSky(t))return false;return SURFACE;}
float ShadowVisibility(){float3 shadowOrigin{0,0,0},L{0,0,1};float maxTraceDistance=4096;return SHADOW;}
int main(){
    struct Case {const char* name;float field;float expected;bool sky;bool hit;float visibility;};
    const Case cases[]={
        {"surface hit",0,2,false,true,0},
        {"verified distance escape",65504,-1,true,false,1},
        {"parallel near-wall budget exhaustion",6,-2,false,false,0}
    };
    int failed=0,total=0;
    for(const auto& c:cases){
        sampledDistance=c.field;samples=0;
        float t=TraceGDF({0,0,0},{0,0,1},4096);
        const bool checks[]={t==c.expected,ReceivesSky(t)==c.sky,ShadesHit(t)==c.hit,ShadowVisibility()==c.visibility};
        for(bool ok:checks){++total;if(!ok)++failed;}
        std::printf("%s %s: trace=%g expected=%g sky=%d hit=%d shadow=%g samples=%u\n",
            checks[0]&&checks[1]&&checks[2]&&checks[3]?"PASS":"FAIL",c.name,t,c.expected,
            ReceivesSky(t),ShadesHit(t),ShadowVisibility(),samples);
    }
    std::printf("RC trace exhaustion: %d/%d passed\n",total-failed,total);
    return failed?1:0;
}
'@
$translated = $trace -replace '\[loop\]', ''
$code = $code.Replace('CONSTANTS', ($constants.Value -join "`n")).Replace('TRACE', $translated).Replace('PRIMARY', $primary).Replace('SURFACE', $surface).Replace('SHADOW', $shadow)
$cpp = Join-Path $dir ($Label + '.cpp')
$exe = Join-Path $dir ($Label + '.exe')
[IO.File]::WriteAllText($cpp, $code)
$zig = (Get-Command zig -ErrorAction SilentlyContinue).Source
if (-not $zig) { $zig = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName }
if (-not $zig) { throw 'Zig compiler not found.' }
& $zig c++ -std=c++23 -O0 $cpp -o $exe
if ($LASTEXITCODE -ne 0) { throw 'RC exhaustion harness compilation failed.' }
& $exe | Tee-Object -FilePath (Join-Path $dir ($Label + '.log'))
exit $LASTEXITCODE
