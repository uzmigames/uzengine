/// UzEngine Shader Permutation System — Tests

#include <gtest/gtest.h>

#include "shader_permutation.h"

namespace uz {
namespace {

class ShaderPermutationTest : public ::testing::Test {};

// ─────────────────────────────────────────────────────────────────────────────
// ParsePermutations
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPermutationTest, ParseEmpty) {
    const auto perms = ParsePermutations("");
    EXPECT_TRUE(perms.empty());
}

TEST_F(ShaderPermutationTest, ParseEmptyNoPermutations) {
    const auto perms = ParsePermutations(
        "float4 main() { return float4(1,1,1,1); }\n"
        "#include \"common.hlsli\"\n");
    EXPECT_TRUE(perms.empty());
}

TEST_F(ShaderPermutationTest, ParseSingleBool) {
    const auto perms = ParsePermutations("#pragma uz_permutation HAS_NORMAL_MAP\n");
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "HAS_NORMAL_MAP");
    ASSERT_EQ(perms[0].values.size(), 2u);
    EXPECT_EQ(perms[0].values[0], "0");
    EXPECT_EQ(perms[0].values[1], "1");
}

TEST_F(ShaderPermutationTest, ParseSingleEnum) {
    const auto perms = ParsePermutations(
        "#pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH\n");
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "SHADOW_QUALITY");
    ASSERT_EQ(perms[0].values.size(), 3u);
    EXPECT_EQ(perms[0].values[0], "LOW");
    EXPECT_EQ(perms[0].values[1], "MEDIUM");
    EXPECT_EQ(perms[0].values[2], "HIGH");
}

TEST_F(ShaderPermutationTest, ParseMultiple) {
    const std::string source =
        "#pragma uz_permutation HAS_NORMAL_MAP\n"
        "#pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH\n";
    const auto perms = ParsePermutations(source);
    ASSERT_EQ(perms.size(), 2u);
    EXPECT_EQ(perms[0].name, "HAS_NORMAL_MAP");
    EXPECT_EQ(perms[0].values.size(), 2u);
    EXPECT_EQ(perms[1].name, "SHADOW_QUALITY");
    EXPECT_EQ(perms[1].values.size(), 3u);
}

TEST_F(ShaderPermutationTest, IgnoreNonPragmaLines) {
    const std::string source =
        "// #pragma uz_permutation COMMENTED_OUT\n"
        "float4 main() { return 0; }\n"
        "#pragma uz_permutation REAL_ONE\n"
        "#define FOO 1\n";
    const auto perms = ParsePermutations(source);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "REAL_ONE");
}

TEST_F(ShaderPermutationTest, IgnoreNonUzPermutationPragmas) {
    const std::string source =
        "#pragma once\n"
        "#pragma pack(push, 1)\n"
        "#pragma uz_permutation VALID\n"
        "#pragma warning(disable: 4996)\n";
    const auto perms = ParsePermutations(source);
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "VALID");
}

TEST_F(ShaderPermutationTest, ParseHandlesExtraWhitespace) {
    const auto perms = ParsePermutations(
        "  #pragma   uz_permutation   SPACED_OUT   A   B  \n");
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "SPACED_OUT");
    ASSERT_EQ(perms[0].values.size(), 2u);
    EXPECT_EQ(perms[0].values[0], "A");
    EXPECT_EQ(perms[0].values[1], "B");
}

TEST_F(ShaderPermutationTest, ParseHandlesTabs) {
    const auto perms = ParsePermutations(
        "\t#pragma\tuz_permutation\tTABBED\tX\tY\tZ\n");
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "TABBED");
    ASSERT_EQ(perms[0].values.size(), 3u);
}

TEST_F(ShaderPermutationTest, ParseNoTrailingNewline) {
    const auto perms = ParsePermutations("#pragma uz_permutation NO_NEWLINE");
    ASSERT_EQ(perms.size(), 1u);
    EXPECT_EQ(perms[0].name, "NO_NEWLINE");
}

TEST_F(ShaderPermutationTest, ParseMixedWithShaderCode) {
    const std::string source =
        "// PBR shader\n"
        "#pragma uz_permutation HAS_NORMAL_MAP\n"
        "\n"
        "struct VSInput {\n"
        "    float3 position : POSITION;\n"
        "};\n"
        "\n"
        "#pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH\n"
        "\n"
        "float4 PSMain(VSOutput input) : SV_TARGET {\n"
        "    return float4(1,1,1,1);\n"
        "}\n";
    const auto perms = ParsePermutations(source);
    ASSERT_EQ(perms.size(), 2u);
    EXPECT_EQ(perms[0].name, "HAS_NORMAL_MAP");
    EXPECT_EQ(perms[1].name, "SHADOW_QUALITY");
}

// ─────────────────────────────────────────────────────────────────────────────
// VariantCount
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPermutationTest, VariantCountEmpty) {
    EXPECT_EQ(VariantCount({}), 1u);
}

TEST_F(ShaderPermutationTest, VariantCountSingleBool) {
    std::vector<ShaderPermutation> perms = {{"A", {"0", "1"}}};
    EXPECT_EQ(VariantCount(perms), 2u);
}

TEST_F(ShaderPermutationTest, VariantCountSingleEnum) {
    std::vector<ShaderPermutation> perms = {{"Q", {"LOW", "MED", "HIGH"}}};
    EXPECT_EQ(VariantCount(perms), 3u);
}

TEST_F(ShaderPermutationTest, VariantCountMultiple) {
    std::vector<ShaderPermutation> perms = {
        {"A", {"0", "1"}},
        {"B", {"X", "Y", "Z"}},
    };
    EXPECT_EQ(VariantCount(perms), 6u);
}

TEST_F(ShaderPermutationTest, VariantCountLarge) {
    std::vector<ShaderPermutation> perms = {
        {"A", {"0", "1"}},          // 2
        {"B", {"0", "1"}},          // 2
        {"C", {"L", "M", "H"}},    // 3
        {"D", {"0", "1", "2", "3"}}, // 4
    };
    EXPECT_EQ(VariantCount(perms), 48u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ExpandPermutations
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPermutationTest, ExpandEmpty) {
    const auto result = ExpandPermutations({});
    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(result.variants.size(), 1u);
    EXPECT_TRUE(result.variants[0].empty());
}

TEST_F(ShaderPermutationTest, ExpandSingleBool) {
    std::vector<ShaderPermutation> perms = {{"FLAG", {"0", "1"}}};
    const auto result = ExpandPermutations(perms);
    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(result.variants.size(), 2u);

    ASSERT_EQ(result.variants[0].size(), 1u);
    EXPECT_EQ(result.variants[0][0].first, "FLAG");
    EXPECT_EQ(result.variants[0][0].second, "0");

    ASSERT_EQ(result.variants[1].size(), 1u);
    EXPECT_EQ(result.variants[1][0].first, "FLAG");
    EXPECT_EQ(result.variants[1][0].second, "1");
}

TEST_F(ShaderPermutationTest, ExpandSingleEnum) {
    std::vector<ShaderPermutation> perms = {{"Q", {"LOW", "MEDIUM", "HIGH"}}};
    const auto result = ExpandPermutations(perms);
    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(result.variants.size(), 3u);
    EXPECT_EQ(result.variants[0][0].second, "LOW");
    EXPECT_EQ(result.variants[1][0].second, "MEDIUM");
    EXPECT_EQ(result.variants[2][0].second, "HIGH");
}

TEST_F(ShaderPermutationTest, ExpandCrossProduct) {
    std::vector<ShaderPermutation> perms = {
        {"HAS_NORMAL_MAP", {"0", "1"}},
        {"SHADOW_QUALITY", {"LOW", "MEDIUM", "HIGH"}},
    };
    const auto result = ExpandPermutations(perms);
    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(result.variants.size(), 6u);

    // Each variant should have 2 defines, sorted by name
    for (const auto& variant : result.variants) {
        ASSERT_EQ(variant.size(), 2u);
        // Sorted: HAS_NORMAL_MAP < SHADOW_QUALITY
        EXPECT_EQ(variant[0].first, "HAS_NORMAL_MAP");
        EXPECT_EQ(variant[1].first, "SHADOW_QUALITY");
    }

    // Verify all 6 combinations exist
    std::vector<std::string> formatted;
    for (const auto& variant : result.variants) {
        formatted.push_back(FormatDefines(variant));
    }
    std::sort(formatted.begin(), formatted.end());

    EXPECT_EQ(formatted[0], "HAS_NORMAL_MAP=0;SHADOW_QUALITY=HIGH");
    EXPECT_EQ(formatted[1], "HAS_NORMAL_MAP=0;SHADOW_QUALITY=LOW");
    EXPECT_EQ(formatted[2], "HAS_NORMAL_MAP=0;SHADOW_QUALITY=MEDIUM");
    EXPECT_EQ(formatted[3], "HAS_NORMAL_MAP=1;SHADOW_QUALITY=HIGH");
    EXPECT_EQ(formatted[4], "HAS_NORMAL_MAP=1;SHADOW_QUALITY=LOW");
    EXPECT_EQ(formatted[5], "HAS_NORMAL_MAP=1;SHADOW_QUALITY=MEDIUM");
}

TEST_F(ShaderPermutationTest, ExpandVariantsSorted) {
    // Names not in alphabetical order — output should still be sorted per variant
    std::vector<ShaderPermutation> perms = {
        {"ZEBRA", {"0", "1"}},
        {"ALPHA", {"X", "Y"}},
    };
    const auto result = ExpandPermutations(perms);
    ASSERT_TRUE(result.Ok());
    for (const auto& variant : result.variants) {
        ASSERT_EQ(variant.size(), 2u);
        EXPECT_EQ(variant[0].first, "ALPHA");
        EXPECT_EQ(variant[1].first, "ZEBRA");
    }
}

TEST_F(ShaderPermutationTest, ExpandExplosionGuard) {
    // 9 bool permutations = 512 variants > 256
    std::vector<ShaderPermutation> perms;
    for (int i = 0; i < 9; ++i) {
        perms.push_back({"PERM_" + std::to_string(i), {"0", "1"}});
    }
    EXPECT_EQ(VariantCount(perms), 512u);

    const auto result = ExpandPermutations(perms);
    EXPECT_FALSE(result.Ok());
    EXPECT_TRUE(result.variants.empty());
    EXPECT_NE(result.error.find("512"), std::string::npos);
    EXPECT_NE(result.error.find("256"), std::string::npos);
}

TEST_F(ShaderPermutationTest, ExpandExactlyAtLimit) {
    // 8 bool permutations = 256 variants — exactly at limit, should succeed
    std::vector<ShaderPermutation> perms;
    for (int i = 0; i < 8; ++i) {
        perms.push_back({"PERM_" + std::to_string(i), {"0", "1"}});
    }
    EXPECT_EQ(VariantCount(perms), 256u);

    const auto result = ExpandPermutations(perms);
    EXPECT_TRUE(result.Ok());
    EXPECT_EQ(result.variants.size(), 256u);
}

// ─────────────────────────────────────────────────────────────────────────────
// FormatDefines
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPermutationTest, FormatDefinesEmpty) {
    EXPECT_EQ(FormatDefines({}), "");
}

TEST_F(ShaderPermutationTest, FormatDefinesSingle) {
    std::vector<std::pair<std::string, std::string>> defines = {{"FOO", "1"}};
    EXPECT_EQ(FormatDefines(defines), "FOO=1");
}

TEST_F(ShaderPermutationTest, FormatDefinesMultiple) {
    std::vector<std::pair<std::string, std::string>> defines = {
        {"A", "1"},
        {"B", "HIGH"},
        {"C", "0"},
    };
    EXPECT_EQ(FormatDefines(defines), "A=1;B=HIGH;C=0");
}

TEST_F(ShaderPermutationTest, FormatDefinesDeterministic) {
    // Same input always produces same output
    std::vector<std::pair<std::string, std::string>> defines = {
        {"HAS_NORMAL_MAP", "0"},
        {"SHADOW_QUALITY", "LOW"},
    };
    const auto a = FormatDefines(defines);
    const auto b = FormatDefines(defines);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, "HAS_NORMAL_MAP=0;SHADOW_QUALITY=LOW");
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: Parse → Expand → Format
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderPermutationTest, EndToEnd) {
    const std::string source =
        "#pragma uz_permutation HAS_NORMAL_MAP\n"
        "#pragma uz_permutation SHADOW_QUALITY LOW MEDIUM HIGH\n";

    const auto perms = ParsePermutations(source);
    ASSERT_EQ(perms.size(), 2u);
    EXPECT_EQ(VariantCount(perms), 6u);

    const auto result = ExpandPermutations(perms);
    ASSERT_TRUE(result.Ok());
    ASSERT_EQ(result.variants.size(), 6u);

    // All formatted strings should be unique
    std::vector<std::string> formatted;
    for (const auto& variant : result.variants) {
        formatted.push_back(FormatDefines(variant));
    }
    std::sort(formatted.begin(), formatted.end());
    auto it = std::unique(formatted.begin(), formatted.end());
    EXPECT_EQ(it, formatted.end());
}

} // namespace
} // namespace uz
