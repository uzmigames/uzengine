#include "pathtracing/light_bvh.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
using namespace uz;
LightBVHLight Panel(float x, Vec3 normal) {
    LightBVHLight l;
    l.v0={x-1,-1,10}; l.v1={x+1,-1,10}; l.v2={x,1,10};
    l.normal=normal; l.area=2; l.emission={1,1,1};
    return l;
}
LightBVHNode Cap(Vec3 axis,float angle) {
    LightBVHNode n;
    n.aabbMin={-1,-1,-1}; n.aabbMax={1,1,1};
    n.coneAxis=axis; n.coneCosHalfAngle=std::cos(angle); n.totalEmissionPower=1;
    return n;
}
}

TEST(LightBVHStage2, SAOHCostIsMonotoneInPowerAreaAndSpread) {
    auto n=Cap({0,0,1},0);
    float base=LightBVH::SplitCost(n);
    n.totalEmissionPower=2;
    EXPECT_FLOAT_EQ(LightBVH::SplitCost(n),2*base);
    n=Cap({0,0,1},0); n.aabbMax={3,3,3};
    EXPECT_FLOAT_EQ(LightBVH::SplitCost(n),4*base);
    float previous=base;
    for (float angle : {.1f,.5f,1.f,2.f,3.14159265f}) {
        float cost=LightBVH::SplitCost(Cap({0,0,1},angle));
        EXPECT_GE(cost,previous-1e-4f);
        previous=cost;
    }
}

TEST(LightBVHStage2, ConeUnionBoundsBothChildrenIncludingAntipodesAndContainment) {
    for (Vec3 axis : {Vec3{0,0,1},Vec3{0,0,-1},Vec3{1,0,0},Vec3{.6f,0,.8f}})
        for (float a : {0.f,.2f,1.f,3.14159265f})
            for (float b : {0.f,.3f,1.5f}) {
                auto left=Cap({0,0,1},a), right=Cap(axis,b);
                auto parent=LightBVH::Union(left,right);
                float theta=std::acos(std::clamp(parent.coneCosHalfAngle,-1.f,1.f));
                for (const auto& child : {left,right}) {
                    float required=std::acos(std::clamp(parent.coneAxis.Dot(child.coneAxis),-1.f,1.f))
                        +std::acos(std::clamp(child.coneCosHalfAngle,-1.f,1.f));
                    EXPECT_GE(theta+7e-4f,std::min(required,3.14159265f));
                }
            }
}

TEST(LightBVHStage2, SAOHSeparatesKnownSpatialClusters) {
    LightBVH bvh;
    bvh.Build({Panel(-100,{0,0,1}),Panel(100,{0,0,1}),
               Panel(-99,{0,0,1}),Panel(99,{0,0,1})});
    const auto& root=bvh.nodes[0];
    const auto& left=bvh.nodes[root.leftChild];
    const auto& right=bvh.nodes[root.rightChild];
    EXPECT_LT(left.aabbMax.x,right.aabbMin.x);
    EXPECT_LT(left.aabbMax.x-left.aabbMin.x,4);
    EXPECT_LT(right.aabbMax.x-right.aabbMin.x,4);
    EXPECT_GT(left.coneCosHalfAngle,.999f);
    EXPECT_GT(right.coneCosHalfAngle,.999f);
}

TEST(LightBVHStage2, TwoSidedConeDownweightsEdgeOnNotBackFacingAndKeepsSupport) {
    LightBVH bvh;
    bvh.Build({Panel(0,{0,0,1}),Panel(0,{1,0,0})});
    float aligned=bvh.Importance(bvh.lights[0].leaf,{0,0,0},{0,0,1});
    float edge=bvh.Importance(bvh.lights[1].leaf,{0,0,0},{0,0,1});
    EXPECT_GT(aligned,edge*3);
    EXPECT_GT(edge,0);
    EXPECT_FLOAT_EQ(aligned,bvh.Importance(bvh.lights[0].leaf,{0,0,20},{0,0,-1}));
    double sum=0;
    for (uint32_t i=0;i<2;++i) sum+=bvh.EvaluatePDF({0,0,0},{0,0,1},i);
    EXPECT_NEAR(sum,1,1e-6);
}

TEST(LightBVHStage2, DirectionalClusterEstimatorIsUnbiasedAndBeatsMortonVariance) {
    std::vector<LightBVHLight> input;
    for (unsigned i=0;i<32;++i) {
        float angle=float(i%8)*.2f;
        input.push_back(Panel(float(i/8)*2-3,{std::sin(angle),0,std::cos(angle)}));
    }
    LightBVH sah,morton;
    sah.Build(input); morton.Build(input,true);
    auto moments=[](const LightBVH& bvh) {
        double target=0,mean=0,second=0,pdfSum=0;
        for (uint32_t i=0;i<bvh.lights.size();++i) {
            const auto& l=bvh.lights[i];
            Vec3 d=(l.v0+l.v1+l.v2)/3.f;
            double f=l.area*std::abs(l.normal.Dot(d.Normalized()))*d.Normalized().z/d.LengthSq();
            double pdf=bvh.EvaluatePDF({0,0,0},{0,0,1},i);
            EXPECT_GT(pdf,0);
            target+=f; mean+=pdf*(f/pdf); second+=f*f/pdf; pdfSum+=pdf;
        }
        EXPECT_NEAR(pdfSum,1,1e-6);
        EXPECT_NEAR(mean,target,1e-12);
        return second-target*target;
    };
    double a=moments(sah),b=moments(morton);
    std::printf("LightBVH stage2 analytic variance SAOH=%.12g Morton=%.12g delta=%.6f%%\n",a,b,100*(a/b-1));
    EXPECT_LE(a,b);
}
