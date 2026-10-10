#include <algorithm>
#include <cmath>
#include <cstdio>
using std::max; using std::abs; using std::exp; using std::pow;
struct float3 { float x,y,z; float3(float X,float Y,float Z):x(X),y(Y),z(Z){} };
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
float length(float3 a){return std::sqrt(dot(a,a));}
float saturate(float x){return std::clamp(x,0.f,1.f);}
struct {float3 xyz{0,0,0};} cameraPos;
PRODUCTION_FUNCTIONS
int main(){
 unsigned cases=0,failures=0;
 auto check=[&](bool pass,const char* name){++cases;if(!pass){++failures;std::printf("FAIL %s\n",name);}};
 const float3 p{0,0,100},n{0,0,1};
 check(SurfaceCompatible(p,n,p,n),"stationary history");
 check(SurfaceCompatible(p,n,{20,0,100},n),"same receiver plane");
 check(!SurfaceCompatible(p,n,{0,0,104},n),"parallel wall reject");
 check(!SurfaceCompatible(p,n,p,{0,0,-1}),"opposite normal reject");
 check(!SurfaceCompatible(p,n,p,{1,0,0}),"perpendicular normal reject");
 unsigned support=0;
 for(int x=0;x<2;++x)for(int y=0;y<2;++y)
   support+=SurfaceCompatible(p,n,{float(x),float(y),110},n)?1u:0u;
 check(support==0,"thin receiver has no unrelated support");
 check(SurfaceWeight(p,n,{0,0,110},n,32)>0,"sensitivity: old exponential accepts unsupported plane");
 check(std::abs(SurfaceWeight(p,n,p,n,32)-1.f)<1e-6f,"normalized center weight");
 for(unsigned width: {1u,2u,3u,959u,960u,961u})for(unsigned height:{1u,2u,3u,539u,540u,541u}){
   unsigned hw=(width+1)/2,hh=(height+1)/2;
   check((hw-1)*2<width && (hh-1)*2<height,"ceil dimensions stay within full guide");
   for(unsigned x=0;x<hw;++x){float uv=(2.f*x+0.5f)/width;float reprojection=(uv*width-0.5f)*0.5f;
     check(std::abs(reprojection-x)<0.0001f,"stationary half lattice roundtrip");}
 }
 std::printf("Filtered gather: %u/%u passed\n",cases-failures,cases);
 return failures?1:0;
}
