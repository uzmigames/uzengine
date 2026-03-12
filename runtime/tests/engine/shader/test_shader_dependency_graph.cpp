/// Tests for ShaderDependencyGraph — forward/reverse dependency tracking.

#include "shader_dependency_graph.h"
#include <gtest/gtest.h>
#include <algorithm>

// ─────────────────────────────────────────────────────────────────────────────
// Basic Operations
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderDepGraphTest, EmptyGraphHasNoCounts) {
    uz::ShaderDependencyGraph graph;
    EXPECT_EQ(graph.GetFileCount(), 0u);
    EXPECT_EQ(graph.GetEdgeCount(), 0u);
    EXPECT_FALSE(graph.Contains("anything"));
}

TEST(ShaderDepGraphTest, SetDependenciesAddToGraph) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli", "/brdf.hlsli"});

    EXPECT_TRUE(graph.Contains("/pbr.hlsl"));
    EXPECT_EQ(graph.GetEdgeCount(), 2u);
    EXPECT_EQ(graph.GetFileCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward Queries (GetDependencies)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderDepGraphTest, GetDependenciesReturnsDirect) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli", "/brdf.hlsli"});

    auto deps = graph.GetDependencies("/pbr.hlsl");
    EXPECT_EQ(deps.size(), 2u);
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/common.hlsli") != deps.end());
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/brdf.hlsli") != deps.end());
}

TEST(ShaderDepGraphTest, GetDependenciesEmptyForLeaf) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli"});

    auto deps = graph.GetDependencies("/common.hlsli");
    EXPECT_TRUE(deps.empty());
}

TEST(ShaderDepGraphTest, GetDependenciesEmptyForUnknownFile) {
    uz::ShaderDependencyGraph graph;
    auto deps = graph.GetDependencies("/unknown.hlsl");
    EXPECT_TRUE(deps.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Reverse Queries (GetDependents / GetDirectDependents)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderDepGraphTest, GetDirectDependentsReturnsOneLevel) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli"});
    graph.SetDependencies("/toon.hlsl", {"/common.hlsli"});

    auto deps = graph.GetDirectDependents("/common.hlsli");
    EXPECT_EQ(deps.size(), 2u);
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/pbr.hlsl") != deps.end());
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/toon.hlsl") != deps.end());
}

TEST(ShaderDepGraphTest, GetDependentsReturnsTransitive) {
    uz::ShaderDependencyGraph graph;

    // Chain: pbr → brdf → common
    graph.SetDependencies("/pbr.hlsl", {"/brdf.hlsli"});
    graph.SetDependencies("/brdf.hlsli", {"/common.hlsli"});

    auto deps = graph.GetDependents("/common.hlsli");
    EXPECT_EQ(deps.size(), 2u);
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/brdf.hlsli") != deps.end());
    EXPECT_TRUE(std::find(deps.begin(), deps.end(), "/pbr.hlsl") != deps.end());
}

TEST(ShaderDepGraphTest, GetDependentsReturnsSortedResults) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/c.hlsl", {"/shared.hlsli"});
    graph.SetDependencies("/a.hlsl", {"/shared.hlsli"});
    graph.SetDependencies("/b.hlsl", {"/shared.hlsli"});

    auto deps = graph.GetDependents("/shared.hlsli");
    ASSERT_EQ(deps.size(), 3u);
    EXPECT_EQ(deps[0], "/a.hlsl");
    EXPECT_EQ(deps[1], "/b.hlsl");
    EXPECT_EQ(deps[2], "/c.hlsl");
}

TEST(ShaderDepGraphTest, GetDependentsHandlesDiamond) {
    uz::ShaderDependencyGraph graph;

    // Diamond: pbr → {brdf, lighting} → common
    graph.SetDependencies("/pbr.hlsl", {"/brdf.hlsli", "/lighting.hlsli"});
    graph.SetDependencies("/brdf.hlsli", {"/common.hlsli"});
    graph.SetDependencies("/lighting.hlsli", {"/common.hlsli"});

    auto deps = graph.GetDependents("/common.hlsli");
    EXPECT_EQ(deps.size(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Remove / Update
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderDepGraphTest, RemoveFileRemovesForwardEdges) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli"});

    graph.RemoveFile("/pbr.hlsl");
    EXPECT_FALSE(graph.Contains("/pbr.hlsl"));
    EXPECT_EQ(graph.GetEdgeCount(), 0u);
}

TEST(ShaderDepGraphTest, RemoveFileUpdatesReverseEdges) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli"});
    graph.SetDependencies("/toon.hlsl", {"/common.hlsli"});

    graph.RemoveFile("/pbr.hlsl");

    auto deps = graph.GetDirectDependents("/common.hlsli");
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], "/toon.hlsl");
}

TEST(ShaderDepGraphTest, UpdateDependenciesReplacesOld) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli"});

    // Update: pbr now depends on brdf instead of common
    graph.SetDependencies("/pbr.hlsl", {"/brdf.hlsli"});

    auto deps = graph.GetDependencies("/pbr.hlsl");
    ASSERT_EQ(deps.size(), 1u);
    EXPECT_EQ(deps[0], "/brdf.hlsli");

    // common should have no dependents now
    auto commonDeps = graph.GetDirectDependents("/common.hlsli");
    EXPECT_TRUE(commonDeps.empty());
}

TEST(ShaderDepGraphTest, ClearRemovesEverything) {
    uz::ShaderDependencyGraph graph;
    graph.SetDependencies("/pbr.hlsl", {"/common.hlsli", "/brdf.hlsli"});
    graph.SetDependencies("/toon.hlsl", {"/common.hlsli"});

    graph.Clear();
    EXPECT_EQ(graph.GetFileCount(), 0u);
    EXPECT_EQ(graph.GetEdgeCount(), 0u);
}
