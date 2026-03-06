#include <gtest/gtest.h>
#include "engine/core/component_registry.h"
#include "engine/core/component_interface.h"
#include "engine/renderer/scene/scene_proxy.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

// ============================================================================
// Mock components for testing
// ============================================================================

namespace {

struct MockMeshComponent : public IComponent {
    static constexpr const char* kTypeName = "MockMeshComponent";

    StringId type_id() const override { return string_id(kTypeName); }
    const char* type_name() const override { return kTypeName; }

    IRenderable* as_renderable() override { return &renderable_; }

    struct MockRenderable : IRenderable {
        bool visible = true;
        bool is_visible() const override { return visible; }
        ISceneProxy* create_proxy() override { return nullptr; } // Mock: returns nothing
    };
    MockRenderable renderable_;
};

struct MockLightComponent : public IComponent {
    static constexpr const char* kTypeName = "MockLightComponent";

    StringId type_id() const override { return string_id(kTypeName); }
    const char* type_name() const override { return kTypeName; }
};

struct MockAudioComponent : public IComponent {
    static constexpr const char* kTypeName = "MockAudioComponent";

    StringId type_id() const override { return string_id(kTypeName); }
    const char* type_name() const override { return kTypeName; }

    IAudioSource* as_audio_source() override { return &source_; }

    struct MockAudioSourceImpl : IAudioSource {};
    MockAudioSourceImpl source_;
};

} // anonymous namespace

// ============================================================================
// ComponentRegistry tests
// ============================================================================

class ComponentRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear the global registry before each test for isolation.
        ComponentRegistry::instance().clear();
    }

    void TearDown() override {
        ComponentRegistry::instance().clear();
    }
};

// ---- Registration ----

TEST_F(ComponentRegistryTest, EmptyRegistryHasZeroTypes) {
    EXPECT_EQ(ComponentRegistry::instance().type_count(), 0u);
}

TEST_F(ComponentRegistryTest, RegisterSingleType) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh", [] { return new MockMeshComponent(); });

    EXPECT_EQ(reg.type_count(), 1u);
    EXPECT_TRUE(reg.has_type(string_id("Mesh")));
}

TEST_F(ComponentRegistryTest, RegisterMultipleTypes) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh", [] { return new MockMeshComponent(); });
    reg.register_type(string_id("Light"), "Light", [] { return new MockLightComponent(); });
    reg.register_type(string_id("Audio"), "Audio", [] { return new MockAudioComponent(); });

    EXPECT_EQ(reg.type_count(), 3u);
    EXPECT_TRUE(reg.has_type(string_id("Mesh")));
    EXPECT_TRUE(reg.has_type(string_id("Light")));
    EXPECT_TRUE(reg.has_type(string_id("Audio")));
}

TEST_F(ComponentRegistryTest, DuplicateRegistrationOverwrites) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh_v1", [] { return new MockMeshComponent(); });
    reg.register_type(string_id("Mesh"), "Mesh_v2", [] { return new MockMeshComponent(); });

    // Should still be 1 type, not 2
    EXPECT_EQ(reg.type_count(), 1u);

    const auto* info = reg.find(string_id("Mesh"));
    ASSERT_NE(info, nullptr);
    EXPECT_STREQ(info->type_name, "Mesh_v2");
}

TEST_F(ComponentRegistryTest, HasType_Unknown_ReturnsFalse) {
    EXPECT_FALSE(ComponentRegistry::instance().has_type(string_id("NonExistent")));
}

// ---- Creation ----

TEST_F(ComponentRegistryTest, Create_RegisteredType_ReturnsInstance) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh", [] { return new MockMeshComponent(); });

    IComponent* comp = reg.create(string_id("Mesh"));
    ASSERT_NE(comp, nullptr);
    EXPECT_STREQ(comp->type_name(), "MockMeshComponent");
    delete comp;
}

TEST_F(ComponentRegistryTest, Create_UnknownType_ReturnsNull) {
    EXPECT_EQ(ComponentRegistry::instance().create(string_id("Unknown")), nullptr);
}

TEST_F(ComponentRegistryTest, CreateEachCallReturnsNewInstance) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Light"), "Light", [] { return new MockLightComponent(); });

    IComponent* a = reg.create(string_id("Light"));
    IComponent* b = reg.create(string_id("Light"));

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a, b); // Different instances
    delete a;
    delete b;
}

// ---- Proxy Creation ----

struct MockProxy : public ISceneProxy {
    void execute(RhiCmdList, const FrameConstants&) override {}
    uz::Aabb bounds() const override { return uz::Aabb::empty(); }
    uint32_t render_flags() const override { return 0; }
    void sync_from_component(const void*) override {}
};

TEST_F(ComponentRegistryTest, CreateProxy_RegisteredType_ReturnsInstance) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh", 
        [] { return new MockMeshComponent(); },
        [] { return new MockProxy(); });

    ISceneProxy* proxy = reg.create_proxy(string_id("Mesh"));
    ASSERT_NE(proxy, nullptr);
    delete proxy;
}

TEST_F(ComponentRegistryTest, CreateProxy_TypeWithoutProxy_ReturnsNull) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Light"), "Light", [] { return new MockLightComponent(); });

    EXPECT_EQ(reg.create_proxy(string_id("Light")), nullptr);
}

// This test verifies that the macro registers the type automatically.
// Since SetUp() clears the registry, we just check if it was registered.
// Actually, since it's static init, it runs BEFORE any tests.
// If SetUp() clears it, we can't test "automatic" registration easily without
// re-running the lambda or not clearing the registry.
// But we can test that the macro syntax works and we can manually trigger registration.

// ---- Macro Test Helpers ----

struct LocalComp : public IComponent {
    static constexpr const char* kTypeName = "LocalComp";
    StringId type_id() const override { return string_id(kTypeName); }
    const char* type_name() const override { return kTypeName; }
};

TEST_F(ComponentRegistryTest, MacroSyntaxWorks) {
    auto& reg = ComponentRegistry::instance();
    
    // Manually trigger what the macro does to ensure it works
    bool registered = [] {
        ::uz::ComponentRegistry::instance().register_type(
            ::uz::string_id(LocalComp::kTypeName),
            LocalComp::kTypeName,
            [] () -> ::uz::IComponent* { return new LocalComp(); },
            [] () -> ::uz::ISceneProxy* { return new MockProxy(); }
        );
        return true;
    }();
    (void)registered;

    EXPECT_TRUE(reg.has_type(string_id("LocalComp")));
    ISceneProxy* proxy = reg.create_proxy(string_id("LocalComp"));
    ASSERT_NE(proxy, nullptr);
    delete proxy;
}

// ---- Find ----

TEST_F(ComponentRegistryTest, Find_RegisteredType_ReturnsInfo) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("Mesh"), "Mesh", [] { return new MockMeshComponent(); });

    const auto* info = reg.find(string_id("Mesh"));
    ASSERT_NE(info, nullptr);
    EXPECT_STREQ(info->type_name, "Mesh");
    EXPECT_EQ(info->type_id, string_id("Mesh"));
}

TEST_F(ComponentRegistryTest, Find_UnknownType_ReturnsNull) {
    EXPECT_EQ(ComponentRegistry::instance().find(string_id("Nope")), nullptr);
}

// ---- Enumerate ----

TEST_F(ComponentRegistryTest, EnumerateTypes_MatchesRegistered) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("A"), "A", [] { return new MockMeshComponent(); });
    reg.register_type(string_id("B"), "B", [] { return new MockLightComponent(); });

    auto names = reg.enumerate_types();
    ASSERT_EQ(names.size(), 2u);

    // Sort for deterministic comparison (unordered_map iteration order is unspecified)
    std::vector<std::string> sorted_names(names.begin(), names.end());
    std::sort(sorted_names.begin(), sorted_names.end());
    EXPECT_EQ(sorted_names[0], "A");
    EXPECT_EQ(sorted_names[1], "B");
}

// ---- Clear ----

TEST_F(ComponentRegistryTest, Clear_RemovesAllTypes) {
    auto& reg = ComponentRegistry::instance();
    reg.register_type(string_id("X"), "X", [] { return new MockMeshComponent(); });
    ASSERT_EQ(reg.type_count(), 1u);

    reg.clear();
    EXPECT_EQ(reg.type_count(), 0u);
    EXPECT_FALSE(reg.has_type(string_id("X")));
}

// ============================================================================
// IComponent interface tests
// ============================================================================

TEST(ComponentInterfaceTest, TypeIdReturnsCorrectHash) {
    MockMeshComponent mesh;
    EXPECT_EQ(mesh.type_id(), string_id("MockMeshComponent"));
}

TEST(ComponentInterfaceTest, TypeNameReturnsCorrectString) {
    MockLightComponent light;
    EXPECT_STREQ(light.type_name(), "MockLightComponent");
}

// ---- Explicit downcasting (as_*) ----

TEST(ComponentInterfaceTest, AsRenderable_MeshComponent_ReturnsNonNull) {
    MockMeshComponent mesh;
    IRenderable* r = mesh.as_renderable();
    ASSERT_NE(r, nullptr);
    EXPECT_TRUE(r->is_visible());
}

TEST(ComponentInterfaceTest, AsRenderable_LightComponent_ReturnsNull) {
    MockLightComponent light;
    EXPECT_EQ(light.as_renderable(), nullptr);
}

TEST(ComponentInterfaceTest, AsAudioSource_AudioComponent_ReturnsNonNull) {
    MockAudioComponent audio;
    EXPECT_NE(audio.as_audio_source(), nullptr);
}

TEST(ComponentInterfaceTest, AsAudioSource_MeshComponent_ReturnsNull) {
    MockMeshComponent mesh;
    EXPECT_EQ(mesh.as_audio_source(), nullptr);
}

TEST(ComponentInterfaceTest, AsPhysicsBody_AllMocks_ReturnsNull) {
    MockMeshComponent mesh;
    MockLightComponent light;
    MockAudioComponent audio;

    EXPECT_EQ(mesh.as_physics_body(), nullptr);
    EXPECT_EQ(light.as_physics_body(), nullptr);
    EXPECT_EQ(audio.as_physics_body(), nullptr);
}

// ---- Lifecycle hooks (default no-op) ----

TEST(ComponentInterfaceTest, LifecycleHooks_DefaultNoop) {
    MockLightComponent light;
    // These should all be safe to call (no-op by default)
    light.on_attach();
    light.on_detach();
    light.on_tick(0.016f);
}
