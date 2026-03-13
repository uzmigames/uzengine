/// Tests for RenderWorld — generational proxy pool, handle validation, lifecycle.

#include <gtest/gtest.h>
#include "render_world.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static RenderProxyDesc MakeStaticMeshDesc() {
    RenderProxyDesc desc;
    desc.type = RenderProxyType::StaticMesh;
    desc.worldMatrix = Mat4::Identity();
    desc.visible = true;
    desc.castShadows = true;
    desc.renderLayer = 1;
    return desc;
}

static RenderProxyDesc MakeLightDesc(RenderProxyType lightType) {
    RenderProxyDesc desc;
    desc.type = lightType;
    desc.worldMatrix = Mat4::Identity();
    desc.visible = true;
    desc.castShadows = false;
    desc.renderLayer = 1;
    return desc;
}

// ─────────────────────────────────────────────────────────────────────────────
// Handle validity
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderProxyHandleTest, DefaultHandleIsInvalid) {
    RenderProxyHandle handle;
    EXPECT_FALSE(handle.IsValid());
}

TEST(RenderProxyHandleTest, CreatedHandleIsValid) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_TRUE(handle.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateProxy
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, CreateProxyReturnsNonNullProxy) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_NE(world.GetProxy(handle), nullptr);
}

TEST(RenderWorldTest, CreateProxyStoresType) {
    RenderWorld world;
    auto desc = MakeLightDesc(RenderProxyType::PointLight);
    auto handle = world.CreateProxy(desc);
    const RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type, RenderProxyType::PointLight);
}

TEST(RenderWorldTest, CreateProxyStoresVisibility) {
    RenderWorld world;
    auto desc = MakeStaticMeshDesc();
    desc.visible = false;
    auto handle = world.CreateProxy(desc);
    const RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->visible);
}

TEST(RenderWorldTest, CreateProxyStoresCastShadows) {
    RenderWorld world;
    auto desc = MakeStaticMeshDesc();
    desc.castShadows = false;
    auto handle = world.CreateProxy(desc);
    const RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->castShadows);
}

TEST(RenderWorldTest, CreateProxyStoresRenderLayer) {
    RenderWorld world;
    auto desc = MakeStaticMeshDesc();
    desc.renderLayer = 7u;
    auto handle = world.CreateProxy(desc);
    const RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->renderLayer, 7u);
}

TEST(RenderWorldTest, CreateMultipleProxiesGetsDifferentHandles) {
    RenderWorld world;
    auto h1 = world.CreateProxy(MakeStaticMeshDesc());
    auto h2 = world.CreateProxy(MakeStaticMeshDesc());
    auto h3 = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_NE(h1.index, h2.index);
    EXPECT_NE(h2.index, h3.index);
    EXPECT_NE(h1.index, h3.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyProxy
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, DestroyProxyMakesGetProxyReturnNull) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    ASSERT_NE(world.GetProxy(handle), nullptr);
    world.DestroyProxy(handle);
    EXPECT_EQ(world.GetProxy(handle), nullptr);
}

TEST(RenderWorldTest, DestroyProxyWithInvalidHandleIsNoOp) {
    RenderWorld world;
    RenderProxyHandle invalid; // default — index=UINT32_MAX
    // Must not crash or assert.
    world.DestroyProxy(invalid);
}

TEST(RenderWorldTest, DestroyProxyWithStaleHandleIsNoOp) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(handle);
    // Destroying again with the now-stale handle must not crash.
    world.DestroyProxy(handle);
}

// ─────────────────────────────────────────────────────────────────────────────
// Generational safety — slot reuse
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, SlotReuseNewHandleDifferentGeneration) {
    RenderWorld world;
    auto h1 = world.CreateProxy(MakeStaticMeshDesc());
    uint32_t firstGeneration = h1.generation;

    world.DestroyProxy(h1);

    // The next CreateProxy should reuse the freed slot.
    auto h2 = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(h2.index, h1.index); // same slot
    EXPECT_NE(h2.generation, firstGeneration); // but different generation

    // Old handle must no longer resolve.
    EXPECT_EQ(world.GetProxy(h1), nullptr);
    // New handle must resolve.
    EXPECT_NE(world.GetProxy(h2), nullptr);
}

TEST(RenderWorldTest, StaleHandleAfterSlotReuse) {
    RenderWorld world;
    auto old = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(old);

    auto fresh = world.CreateProxy(MakeStaticMeshDesc());
    (void)fresh;

    // The old handle points to the same index but wrong generation.
    EXPECT_EQ(world.GetProxy(old), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetTransform
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, SetTransformUpdatesWorldMatrix) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());

    Mat4 expected = Mat4::Identity();
    // Set a recognizable value in the matrix.
    expected(0, 3) = 42.0f;
    expected(1, 3) = -7.0f;
    expected(2, 3) = 3.14f;

    world.SetTransform(handle, expected);

    const RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);
    EXPECT_FLOAT_EQ(proxy->worldMatrix(0, 3), 42.0f);
    EXPECT_FLOAT_EQ(proxy->worldMatrix(1, 3), -7.0f);
    EXPECT_FLOAT_EQ(proxy->worldMatrix(2, 3), 3.14f);
}

TEST(RenderWorldTest, SetTransformWithInvalidHandleIsNoOp) {
    RenderWorld world;
    RenderProxyHandle invalid;
    // Must not crash.
    world.SetTransform(invalid, Mat4::Identity());
}

TEST(RenderWorldTest, SetTransformWithStaleHandleIsNoOp) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(handle);
    // Must not crash.
    world.SetTransform(handle, Mat4::Identity());
}

// ─────────────────────────────────────────────────────────────────────────────
// SetVisible
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, SetVisibleTogglesVisibility) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());

    world.SetVisible(handle, false);
    EXPECT_FALSE(world.GetProxy(handle)->visible);

    world.SetVisible(handle, true);
    EXPECT_TRUE(world.GetProxy(handle)->visible);
}

TEST(RenderWorldTest, SetVisibleWithInvalidHandleIsNoOp) {
    RenderWorld world;
    RenderProxyHandle invalid;
    world.SetVisible(invalid, false); // must not crash
}

TEST(RenderWorldTest, SetVisibleWithStaleHandleIsNoOp) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(handle);
    world.SetVisible(handle, false); // must not crash
}

// ─────────────────────────────────────────────────────────────────────────────
// ProxyCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, ProxyCountStartsAtZero) {
    RenderWorld world;
    EXPECT_EQ(world.ProxyCount(), 0u);
}

TEST(RenderWorldTest, ProxyCountIncrementsOnCreate) {
    RenderWorld world;
    (void)world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(world.ProxyCount(), 1u);
    (void)world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(world.ProxyCount(), 2u);
}

TEST(RenderWorldTest, ProxyCountDecrementsOnDestroy) {
    RenderWorld world;
    auto h1 = world.CreateProxy(MakeStaticMeshDesc());
    auto h2 = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(world.ProxyCount(), 2u);

    world.DestroyProxy(h1);
    EXPECT_EQ(world.ProxyCount(), 1u);

    world.DestroyProxy(h2);
    EXPECT_EQ(world.ProxyCount(), 0u);
}

TEST(RenderWorldTest, ProxyCountUnchangedByStaleDestroy) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(handle);
    EXPECT_EQ(world.ProxyCount(), 0u);

    // Destroying the now-stale handle again must not decrement below zero.
    world.DestroyProxy(handle);
    EXPECT_EQ(world.ProxyCount(), 0u);
}

TEST(RenderWorldTest, ProxyCountAfterSlotReuse) {
    RenderWorld world;
    auto h1 = world.CreateProxy(MakeStaticMeshDesc());
    world.DestroyProxy(h1);
    EXPECT_EQ(world.ProxyCount(), 0u);

    // Reuse the slot.
    (void)world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(world.ProxyCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, ClearRemovesAllProxies) {
    RenderWorld world;
    auto h1 = world.CreateProxy(MakeStaticMeshDesc());
    auto h2 = world.CreateProxy(MakeLightDesc(RenderProxyType::DirectionalLight));
    auto h3 = world.CreateProxy(MakeLightDesc(RenderProxyType::SpotLight));

    world.Clear();

    EXPECT_EQ(world.ProxyCount(), 0u);
    EXPECT_TRUE(world.GetProxies().empty());

    // All prior handles must be invalid after clear.
    EXPECT_EQ(world.GetProxy(h1), nullptr);
    EXPECT_EQ(world.GetProxy(h2), nullptr);
    EXPECT_EQ(world.GetProxy(h3), nullptr);
}

TEST(RenderWorldTest, ClearThenCreateWorksCorrectly) {
    RenderWorld world;
    (void)world.CreateProxy(MakeStaticMeshDesc());
    world.Clear();

    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_EQ(world.ProxyCount(), 1u);
    EXPECT_NE(world.GetProxy(handle), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple proxies — destroy middle, verify remaining
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, DestroyMiddleProxyDoesNotAffectOthers) {
    RenderWorld world;

    auto desc = MakeStaticMeshDesc();
    desc.renderLayer = 10u;
    auto h1 = world.CreateProxy(desc);

    desc.renderLayer = 20u;
    auto h2 = world.CreateProxy(desc);

    desc.renderLayer = 30u;
    auto h3 = world.CreateProxy(desc);

    world.DestroyProxy(h2);

    EXPECT_EQ(world.ProxyCount(), 2u);

    const RenderProxy* p1 = world.GetProxy(h1);
    const RenderProxy* p3 = world.GetProxy(h3);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(p1->renderLayer, 10u);
    EXPECT_EQ(p3->renderLayer, 30u);
    EXPECT_EQ(world.GetProxy(h2), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Const GetProxy
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, ConstGetProxyReturnsNonNullForLiveHandle) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());

    const RenderWorld& constWorld = world;
    EXPECT_NE(constWorld.GetProxy(handle), nullptr);
}

TEST(RenderWorldTest, ConstGetProxyReturnsNullForInvalidHandle) {
    RenderWorld world;
    RenderProxyHandle invalid;

    const RenderWorld& constWorld = world;
    EXPECT_EQ(constWorld.GetProxy(invalid), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetProxies — linear iteration
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, GetProxiesEmptyInitially) {
    RenderWorld world;
    EXPECT_TRUE(world.GetProxies().empty());
}

TEST(RenderWorldTest, GetProxiesHasEntryAfterCreate) {
    RenderWorld world;
    (void)world.CreateProxy(MakeStaticMeshDesc());
    EXPECT_FALSE(world.GetProxies().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Union type-specific data
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderWorldTest, StaticMeshUnionFieldsWriteableViaGetProxy) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeStaticMeshDesc());
    RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);

    proxy->staticMesh.meshId = 42u;
    proxy->staticMesh.materialId = 7u;

    EXPECT_EQ(world.GetProxy(handle)->staticMesh.meshId, 42u);
    EXPECT_EQ(world.GetProxy(handle)->staticMesh.materialId, 7u);
}

TEST(RenderWorldTest, LightUnionFieldsWriteableViaGetProxy) {
    RenderWorld world;
    auto handle = world.CreateProxy(MakeLightDesc(RenderProxyType::PointLight));
    RenderProxy* proxy = world.GetProxy(handle);
    ASSERT_NE(proxy, nullptr);

    proxy->light.intensity = 1500.0f;
    proxy->light.range = 20.0f;

    EXPECT_FLOAT_EQ(world.GetProxy(handle)->light.intensity, 1500.0f);
    EXPECT_FLOAT_EQ(world.GetProxy(handle)->light.range, 20.0f);
}

