/// UzEngine Asset Importer Registry — Tests
///
/// Tests registration, lookup by extension/name, duplicate detection,
/// and edge cases (null, empty, clear).

#include "asset_registry.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Mock importer for testing
// ─────────────────────────────────────────────────────────────────────────────

class MockImporter final : public IAssetImporter {
public:
    MockImporter(const char* name, uint32_t version, std::vector<std::string> extensions)
        : m_name(name), m_version(version), m_extensions(std::move(extensions)) {}

    [[nodiscard]] const char* Name() const override { return m_name; }
    [[nodiscard]] uint32_t Version() const override { return m_version; }

    [[nodiscard]] const std::vector<std::string>& Extensions() const override {
        return m_extensions;
    }

    [[nodiscard]] AssetImportResult Import(
        const std::vector<uint8_t>& /*sourceData*/,
        const std::string& /*sourceName*/,
        const AssetImportOptions& /*options*/) override {
        return {true, {0xDE, 0xAD}, ""};
    }

private:
    const char* m_name;
    uint32_t m_version;
    std::vector<std::string> m_extensions;
};

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetImporterRegistry, RegisterSingleImporter) {
    AssetImporterRegistry registry;
    auto importer = std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"});
    EXPECT_TRUE(registry.Register(std::move(importer)));
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(AssetImporterRegistry, RegisterMultipleImporters) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    registry.Register(std::make_unique<MockImporter>("texture", 1, std::vector<std::string>{".png", ".jpg"}));
    EXPECT_EQ(registry.Count(), 2u);
}

TEST(AssetImporterRegistry, RegisterMultipleExtensions) {
    AssetImporterRegistry registry;
    auto importer = std::make_unique<MockImporter>(
        "texture", 1, std::vector<std::string>{".png", ".jpg", ".tga"});
    EXPECT_TRUE(registry.Register(std::move(importer)));

    EXPECT_NE(registry.FindByExtension(".png"), nullptr);
    EXPECT_NE(registry.FindByExtension(".jpg"), nullptr);
    EXPECT_NE(registry.FindByExtension(".tga"), nullptr);
}

TEST(AssetImporterRegistry, RejectNullImporter) {
    AssetImporterRegistry registry;
    EXPECT_FALSE(registry.Register(nullptr));
    EXPECT_EQ(registry.Count(), 0u);
}

TEST(AssetImporterRegistry, RejectDuplicateName) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    auto dup = std::make_unique<MockImporter>("shader", 2, std::vector<std::string>{".glsl"});
    EXPECT_FALSE(registry.Register(std::move(dup)));
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(AssetImporterRegistry, RejectDuplicateExtension) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    auto dup = std::make_unique<MockImporter>("shader2", 1, std::vector<std::string>{".hlsl"});
    EXPECT_FALSE(registry.Register(std::move(dup)));
    EXPECT_EQ(registry.Count(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetImporterRegistry, FindByExtensionReturnsCorrectImporter) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    registry.Register(std::make_unique<MockImporter>("texture", 1, std::vector<std::string>{".png"}));

    auto* shader = registry.FindByExtension(".hlsl");
    ASSERT_NE(shader, nullptr);
    EXPECT_STREQ(shader->Name(), "shader");

    auto* texture = registry.FindByExtension(".png");
    ASSERT_NE(texture, nullptr);
    EXPECT_STREQ(texture->Name(), "texture");
}

TEST(AssetImporterRegistry, FindByExtensionReturnsNullForUnknown) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    EXPECT_EQ(registry.FindByExtension(".unknown"), nullptr);
    EXPECT_EQ(registry.FindByExtension(""), nullptr);
}

TEST(AssetImporterRegistry, FindByNameReturnsCorrectImporter) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 3, std::vector<std::string>{".hlsl"}));

    auto* found = registry.FindByName("shader");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->Version(), 3u);
}

TEST(AssetImporterRegistry, FindByNameReturnsNullForUnknown) {
    AssetImporterRegistry registry;
    EXPECT_EQ(registry.FindByName("nonexistent"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetExtensions and Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetImporterRegistry, GetExtensionsReturnsAll) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    registry.Register(std::make_unique<MockImporter>("texture", 1, std::vector<std::string>{".png", ".jpg"}));

    auto exts = registry.GetExtensions();
    EXPECT_EQ(exts.size(), 3u);
}

TEST(AssetImporterRegistry, ClearRemovesEverything) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 1, std::vector<std::string>{".hlsl"}));
    registry.Register(std::make_unique<MockImporter>("texture", 1, std::vector<std::string>{".png"}));
    EXPECT_EQ(registry.Count(), 2u);

    registry.Clear();
    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_EQ(registry.FindByExtension(".hlsl"), nullptr);
    EXPECT_EQ(registry.FindByName("shader"), nullptr);
    EXPECT_TRUE(registry.GetExtensions().empty());
}

TEST(AssetImporterRegistry, EmptyRegistryHasZeroCount) {
    AssetImporterRegistry registry;
    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_TRUE(registry.GetExtensions().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Importer version and metadata
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetImporterRegistry, ImporterVersionAccessible) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("shader", 42, std::vector<std::string>{".hlsl"}));

    auto* importer = registry.FindByName("shader");
    ASSERT_NE(importer, nullptr);
    EXPECT_EQ(importer->Version(), 42u);
}

TEST(AssetImporterRegistry, GetImportersReturnsAll) {
    AssetImporterRegistry registry;
    registry.Register(std::make_unique<MockImporter>("a", 1, std::vector<std::string>{".a"}));
    registry.Register(std::make_unique<MockImporter>("b", 2, std::vector<std::string>{".b"}));

    const auto& importers = registry.GetImporters();
    EXPECT_EQ(importers.size(), 2u);
}

} // namespace uz::test
