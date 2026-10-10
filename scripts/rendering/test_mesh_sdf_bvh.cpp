#include "mesh_sdf_bvh.h"
#include <array>
#include <cstring>
#include <cstdio>
#include <random>
#include <span>
#include <stdexcept>

using uint = uint32_t;
struct float2 { float x, y; float2(float a, float b) : x(a), y(b) {} };
struct float3 {
    float x, y, z;
    float3(float a = 0) : x(a), y(a), z(a) {}
    float3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct uint3 { uint x, y, z; };
struct uint4 { uint x, y, z, w; };
float3 operator+(float3 a, float3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
float3 operator-(float3 a, float3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
float3 operator*(float3 a, float b) { return {a.x*b,a.y*b,a.z*b}; }
float dot(float3 a, float3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float3 cross(float3 a, float3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
float3 min(float3 a, float3 b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)}; }
float3 max(float3 a, float3 b) { return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}; }
float saturate(float a) { return std::clamp(a,0.f,1.f); }

template<class T> struct StructuredBuffer {
    std::span<const std::byte> bytes;
    size_t* reads;
    T operator[](uint index) const {
        if (static_cast<size_t>(index) >= bytes.size()/sizeof(T)) throw std::runtime_error("Shader buffer index out of bounds");
        ++*reads;
        T result;
        std::memcpy(&result,bytes.data()+static_cast<size_t>(index)*sizeof(T),sizeof(T));
        return result;
    }
};

#include "mesh_sdf_bvh.generated.h"
static_assert(sizeof(Triangle)==sizeof(uz::MeshSdfGpuTriangle));
static_assert(sizeof(BvhNode)==sizeof(uz::MeshSdfGpuBvhNode));

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static uz::MeshSdfGpuTriangle MakeTriangle(float3 a, float3 b, float3 c, float3 normal) {
    uz::MeshSdfGpuTriangle triangle{};
    std::memcpy(triangle.v0,&a,12); std::memcpy(triangle.v1,&b,12);
    std::memcpy(triangle.v2,&c,12); std::memcpy(triangle.normal,&normal,12);
    return triangle;
}

static size_t CheckFixture(const std::vector<uz::MeshSdfGpuTriangle>& original,
                           const std::vector<float3>& queries, bool measurePruning) {
    auto triangles=original;
    std::vector<uz::MeshSdfGpuBvhNode> nodes;
    Require(uz::BuildMeshSdfBvh(triangles,nodes),"BVH build rejected valid fixture");
    Require(nodes.front().escapeIndex==nodes.size(),"Root escape must terminate traversal");
    std::vector<bool> covered(triangles.size());
    for (size_t i=0;i<nodes.size();++i) {
        const auto& node=nodes[i];
        Require(node.escapeIndex>i && node.escapeIndex<=nodes.size(),"Invalid forward escape");
        if (node.triangleCount==0) {
            Require(i+1<node.escapeIndex && nodes[i+1].escapeIndex<node.escapeIndex,"Missing binary children");
            Require(nodes[nodes[i+1].escapeIndex].escapeIndex==node.escapeIndex,"Right child escape mismatch");
        } else {
            Require(node.triangleCount<=4,"Leaf exceeds four triangles");
            Require(size_t(node.firstTriangle)+node.triangleCount<=triangles.size(),"Leaf range out of bounds");
            for (uint j=0;j<node.triangleCount;++j) {
                const size_t index=node.firstTriangle+j;
                Require(!covered[index],"Triangle appears in multiple leaves");
                covered[index]=true;
                const auto& triangle=triangles[index];
                for (uint axis=0;axis<3;++axis) for (const float* vertex:{triangle.v0,triangle.v1,triangle.v2})
                    Require(vertex[axis]>=node.boundsMin[axis] && vertex[axis]<=node.boundsMax[axis],"Leaf bounds exclude geometry");
                Require(std::memcmp(triangle.v0,original[triangle.originalIndex].v0,12)==0,"Original triangle index lost");
            }
        }
    }
    Require(std::all_of(covered.begin(),covered.end(),[](bool value){return value;}),"Uncovered triangle");
    const size_t triangleBytes=triangles.size()*sizeof(uz::MeshSdfGpuTriangle);
    std::vector<std::byte> packed(triangleBytes+nodes.size()*sizeof(uz::MeshSdfGpuBvhNode));
    std::memcpy(packed.data(),triangles.data(),triangleBytes);
    std::memcpy(packed.data()+triangleBytes,nodes.data(),nodes.size()*sizeof(uz::MeshSdfGpuBvhNode));
    const auto bruteBytes=std::as_bytes(std::span(original));
    size_t bruteReads=0, bvhReads=0, nodeReads=0, unused=0;
    for (float3 p:queries) {
        auto expected=ClosestMeshTriangle(p,{bruteBytes,&bruteReads},{bruteBytes,&unused},static_cast<uint>(original.size()));
        auto actual=ClosestMeshTriangle(p,{packed,&bvhReads},{packed,&nodeReads},static_cast<uint>(original.size())|uz::kMeshSdfBvhFlag);
        Require(actual.x==expected.x,"BVH distance differs from brute force");
        Require(actual.y==expected.y,"BVH sign differs from original triangle order");
    }
    if (measurePruning) Require(bvhReads*10<bruteReads,"BVH failed to prune at least 90% of triangle reads");
    std::printf("triangles=%zu nodes=%zu queries=%zu bruteReads=%zu bvhTriangleReads=%zu nodeReads=%zu reduction=%.2f%%\n",
        triangles.size(),nodes.size(),queries.size(),bruteReads,bvhReads,nodeReads,100.0*(1.0-double(bvhReads)/bruteReads));
    return queries.size();
}

int main() {
    try {
        size_t total=0;
        std::mt19937 random(173);
        std::uniform_real_distribution<float> coordinate(-100.f,100.f);
        std::vector<uz::MeshSdfGpuTriangle> triangles;
        for (uint i=0;i<513;++i) {
            float3 a{coordinate(random),coordinate(random),coordinate(random)};
            float3 b{coordinate(random),coordinate(random),coordinate(random)};
            float3 c{coordinate(random),coordinate(random),coordinate(random)};
            triangles.push_back(MakeTriangle(a,b,c,cross(b-a,c-a)));
        }
        std::vector<float3> queries;
        for (uint i=0;i<2048;++i) queries.emplace_back(coordinate(random),coordinate(random),coordinate(random));
        total+=CheckFixture(triangles,queries,false);

        triangles.clear(); queries.clear();
        for (uint i=0;i<1024;++i) {
            float3 p{float(i%32)*10.f,float(i/32)*10.f,0};
            triangles.push_back(MakeTriangle(p,p+float3{1,0,0},p+float3{0,1,0},{0,0,1}));
            queries.push_back(p+float3{.2f,.2f,.5f});
        }
        total+=CheckFixture(triangles,queries,true);

        triangles={MakeTriangle({0,-1,0},{2,-1,0},{0,1,0},{0,0,-1}),
                   MakeTriangle({-2,-1,0},{0,-1,0},{0,1,0},{0,0,1})};
        for (int i=0;i<16;++i) {
            float3 p{float(i-8)*20.f,10,10};
            triangles.push_back(MakeTriangle(p,p,p,{}));
        }
        total+=CheckFixture(triangles,{{0,0,1},{0,0,-1},{0,0,0},{-2,-1,0},{2,-1,0}},false);
        triangles.assign(37,MakeTriangle({1,2,3},{1,2,3},{1,2,3},{}));
        total+=CheckFixture(triangles,{{0,0,0},{1,2,3},{100,100,100}},false);
        triangles.resize(1);
        total+=CheckFixture(triangles,{{0,0,0},{1,2,3}},false);
        std::vector<uz::MeshSdfGpuBvhNode> nodes;
        triangles[0].v0[0]=NAN;
        Require(!uz::BuildMeshSdfBvh(triangles,nodes),"Non-finite geometry accepted");
        triangles.clear();
        Require(!uz::BuildMeshSdfBvh(triangles,nodes),"Empty geometry accepted");
        std::printf("PASS %zu exact distance/sign comparisons; tree structure, ties, degenerate triangles and invalid geometry checked\n",total);
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL %s\n",error.what());
        return 1;
    }
}
