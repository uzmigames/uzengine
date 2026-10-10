#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
struct bool2 {bool x,y;};struct bool3 {bool x,y,z;};
bool any(bool2 v){return v.x||v.y;}bool all(bool3 v){return v.x&&v.y&&v.z;}
struct uint2{unsigned x,y;};
struct int2 {int x,y;int2(int X,int Y):x(X),y(Y){}explicit int2(uint2 a):x(a.x),y(a.y){}};
int2 operator+(int2 a,int2 b){return{a.x+b.x,a.y+b.y};}int2 operator-(int2 a){return{-a.x,-a.y};}int2 operator*(int2 a,int b){return{a.x*b,a.y*b};}
bool2 operator<(int2 a,int b){return{a.x<b,a.y<b};}bool2 operator>=(int2 a,int2 b){return{a.x>=b.x,a.y>=b.y};}
struct int3{int2 xy;int z;int3(int2 p,int Z):xy(p),z(Z){}};
struct float3 {float x,y,z;float3(float a=0):x(a),y(a),z(a){}float3(float X,float Y,float Z):x(X),y(Y),z(Z){}float3& operator*=(float a){x*=a;y*=a;z*=a;return *this;}};
float3 operator-(float3 a,float3 b){return{a.x-b.x,a.y-b.y,a.z-b.z};}float3 operator-(float3 a){return{-a.x,-a.y,-a.z};}float3 operator*(float3 a,float b){return{a.x*b,a.y*b,a.z*b};}
float dot(float3 a,float3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}float3 cross(float3 a,float3 b){return{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
float rsqrt(float a){return 1/std::sqrt(a);}template<class A,class B> float max(A a,B b){return std::max(float(a),float(b));}
bool3 isfinite(float3 a){return{isfinite(a.x),isfinite(a.y),isfinite(a.z)};}
template<class T> struct Texture2D {std::array<T,64> data;T Load(int3 p)const{return data[p.xy.x+p.xy.y*8];}};
struct{float3 xyz{4,4,100};}cameraPos;
bool IsSkyDepth(float d){return d<0.00001f;}
float3 SurfacePosition(int2 p,float d,uint2){return{float(p.x),float(p.y),d*100};}
PRODUCTION_GEOMETRY
int main(){
 unsigned cases=0,failures=0;auto check=[&](bool ok,const char* label){++cases;if(!ok){++failures;std::printf("FAIL %s\n",label);}};
 Texture2D<float> depth;uint2 size{8,8};int2 center{4,4};
 for(float slope:{0.f,0.001f,0.005f,0.02f}){
  for(int y=0;y<8;++y)for(int x=0;x<8;++x)depth.data[x+y*8]=0.5f+(x-4)*slope+(y-4)*slope*0.5f;
  float3 p=SurfacePosition(center,depth.Load({center,0}),size);
  float3 n=SurfaceGeometricNormal(depth,center,size,depth.Load({center,0}),p);
  float3 expected{-100*slope,-50*slope,1};expected*=rsqrt(dot(expected,expected));
  check(dot(n,expected)>0.99999f,"affine tilted raster plane reconstructs geometric normal");
  cameraPos.xyz.z=0;float3 opposite=SurfaceGeometricNormal(depth,center,size,depth.Load({center,0}),p);
  check(dot(n,opposite)<-0.99999f,"normal faces current view");cameraPos.xyz.z=100;
 }
 depth.data.fill(0.5f);depth.data[5+4*8]=0.2f;depth.data[6+4*8]=0.2f;
 float3 p=SurfacePosition(center,0.5f,size);float3 n=SurfaceGeometricNormal(depth,center,size,0.5f,p);
 check(n.z>0.99999f,"right silhouette selects connected left derivative");
 depth.data[3+4*8]=0.2f;depth.data[2+4*8]=0.2f;
 n=SurfaceGeometricNormal(depth,center,size,0.5f,p);
 check(dot(n,n)==0,"two disconnected sides return unknown geometry");
 depth.data.fill(0);depth.data[4+4*8]=0.5f;n=SurfaceGeometricNormal(depth,center,size,0.5f,p);
 check(dot(n,n)==0,"isolated receiver surrounded by sky returns unknown geometry");
 std::printf("Surface geometric normal: %u/%u passed\n",cases-failures,cases);return failures?1:0;
}
