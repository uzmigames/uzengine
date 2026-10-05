#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
using uint = uint32_t;
struct uint3 { uint x,y,z; uint3():x(0),y(0),z(0){} uint3(uint a,uint b,uint c):x(a),y(b),z(c){} };
uint3 operator+(uint3 a,uint3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
uint3 operator%(uint3 a,uint b){return {a.x%b,a.y%b,a.z%b};}
bool operator>=(uint3 a,uint3 b){return a.x>=b.x||a.y>=b.y||a.z>=b.z;}
struct float3 {
    float x,y,z;
    float3(float v=0):x(v),y(v),z(v){}
    float3(float a,float b,float c):x(a),y(b),z(c){}
    float3(uint3 v):x(v.x),y(v.y),z(v.z){}
};
struct float4 {
    union { float x; float r; }; float y,z,w;
    float4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}
    float4(float3 v,float d):x(v.x),y(v.y),z(v.z),w(d){}
    float4(float d,float3 v):x(d),y(v.x),z(v.y),w(v.z){}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float3 operator-(float3 a){return {-a.x,-a.y,-a.z};}
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float3 operator*(float a,float3 b){return b*a;}
float3 operator/(float3 a,float3 b){return {a.x/b.x,a.y/b.y,a.z/b.z};}
bool operator>(float3 a,float3 b){return a.x>b.x||a.y>b.y||a.z>b.z;}
bool operator>=(uint3 a,uint b){return a.x>=b||a.y>=b||a.z>=b;}
bool any(bool v){return v;}
float3 abs(float3 v){return {std::abs(v.x),std::abs(v.y),std::abs(v.z)};}
float3 max(float3 a,float3 b){return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};}
float max(float a,float b){return std::max(a,b);}
float min(float a,float b){return std::min(a,b);}
float clamp(float x,float a,float b){return std::clamp(x,a,b);}
float3 clamp(float3 x,float3 a,float3 b){return {clamp(x.x,a.x,b.x),clamp(x.y,a.y,b.y),clamp(x.z,a.z,b.z)};}
float length(float3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
float dot(float4 a,float4 b){return a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;}
float sampledDistance[512]{};
struct TestVolume { uint index; float SampleLevel(int,float3,int){return sampledDistance[index];} };
float4 expandedValue;
float rawValue=123;
struct ExpandedOutput { float4& operator[](uint3){return expandedValue;} };
struct RawOutput { float& operator[](uint3){return rawValue;} };
uint outputUav=0,rawOutputUav=1,meshCount=0,accumulate=0,resolution=1;
uint regionOrigin=0,regionSize=(1u|(1u<<9)|(1u<<18)),windowOrigin=0;   // one voxel, texel 0 (block B.2 region constants)
float band=0;   // untruncated, as the original shader
float3 clipmapCenter;
float clipmapExtent=1;
int linearClamp=0;
#include "PRODUCTION_HEADER"
#ifdef CHECK_ORIGINAL_EXPANDED
namespace Original {
#include "../fixtures/gdf_expanded_before_raw.h"
}
#endif
int failures=0,checks=0;
void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
MeshSdf Make(uint srv,float raw,float bias,float scale,float material){
    sampledDistance[srv]=raw;
    MeshSdf mesh{};mesh.extent=100;mesh.distanceScale=scale;mesh.surfaceBias=bias;mesh.resolution=32;mesh.srvIndex=srv;
    mesh.worldToLocalX={1,0,0,0};mesh.worldToLocalY={0,1,0,0};mesh.worldToLocalZ={0,0,1,0};mesh.baseColorTint={material,0,0};return mesh;
}
void Run(){ProductionComposite({0,0,0});}
int main(){
    meshes[0]=Make(0,20,15,1,.2f);meshes[1]=Make(1,10,1,1,.8f);meshCount=2;Run();
    Check(expandedValue.x==4 && expandedValue.y==.2f,"expanded distance and material winner preserved");
    Check(rawValue==10,"raw minimum belongs to different mesh");
    meshes[0]=Make(2,30,29,1,.6f);meshCount=1;accumulate=1;Run();
    Check(expandedValue.x==0 && expandedValue.y==.6f && rawValue==10,"expanded winner changes without overwriting raw winner");
    meshes[0]=Make(0,20,100,1,.2f);meshes[1]=Make(1,5,0,1,.8f);meshes[1].extent=1;meshes[1].worldToLocalX.w=6;meshCount=2;accumulate=0;Run();
    Check(expandedValue.x==-81 && expandedValue.y==.2f,"expanded bound winner preserved");
    Check(rawValue==5,"expanded pruning cannot discard raw winner outside mesh bounds");
    meshes[0]=Make(0,12,3,.5f,.3f);meshCount=1;accumulate=0;Run();
    Check(expandedValue.x==3.5f && rawValue==6,"scaled raw excludes mesh and global bias");
    meshes[0]=Make(1,2,.25f,2,.7f);accumulate=1;Run();
    Check(expandedValue.x==2.5f && expandedValue.y==.7f && rawValue==4,"batch accumulation preserves independent minima");
    meshes[0]=Make(0,-4,0,.5f,.4f);accumulate=0;Run();
    Check(expandedValue.x==-2 && rawValue==-2,"closed signed field has no expansion");
    meshCount=0;accumulate=1;Run();Check(expandedValue.x==-2 && rawValue==-2,"empty accumulating batch preserves values");
    accumulate=0;Run();Check(expandedValue.x==65504 && rawValue==65504 && expandedValue.y==0,"empty scene resets both outputs");
#ifdef CHECK_ORIGINAL_EXPANDED
    std::mt19937 random(71209);
    std::uniform_real_distribution<float> value(-10,30),positive(.1f,8);
    meshCount=8;
    for(uint fixture=0;fixture<200;++fixture){
        clipmapCenter={value(random),value(random),value(random)};
        clipmapExtent=positive(random);
        for(uint i=0;i<meshCount;++i){
            meshes[i]=Make(i,value(random),positive(random),positive(random),i*.125f);
            meshes[i].extent=positive(random);
            meshes[i].worldToLocalX.w=value(random);
            meshes[i].worldToLocalY.w=value(random);
            meshes[i].worldToLocalZ.w=value(random);
            std::memcpy(&Original::meshes[i],&meshes[i],sizeof(MeshSdf));
        }
        Run();const auto actual=expandedValue;
        Original::ProductionComposite({0,0,0});
        Check(std::memcmp(&actual,&expandedValue,sizeof(float4))==0,"expanded RGBA bit-identical to original production shader");
    }
#endif
    std::printf("Raw composite producer: %d/%d passed\n",checks-failures,checks);return failures?1:0;
}
