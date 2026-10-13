/// Unit tests for PBRMaterial, DrawList, and PerFrameCB lighting fields.
///
/// Validates struct sizes, default values, sort key correctness, draw list
/// ordering, and that the modified PerFrameCB still satisfies D3D12 alignment.

#include "constant_buffers.h"
#include "draw_list.h"
#include "material.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace uz;

TEST(DrawCommand, GIOccluderMembership) {
    DrawCommand draw{};
    EXPECT_TRUE(IsGIOccluder(draw));
    draw.materialFlags = 1u; // Masked.
    EXPECT_TRUE(IsGIOccluder(draw));
    draw.materialFlags = 2u; // Translucent.
    EXPECT_FALSE(IsGIOccluder(draw));
    draw.materialFlags = 3u; // Additive.
    EXPECT_FALSE(IsGIOccluder(draw));
    draw.materialFlags = 0x10u; // Opaque mesh decal.
    EXPECT_FALSE(IsGIOccluder(draw));
    draw.materialFlags = 0x11u; // Masked mesh decal.
    EXPECT_FALSE(IsGIOccluder(draw));
    draw.materialFlags = 0x4u; // Opaque with packed ORM textures.
    EXPECT_TRUE(IsGIOccluder(draw));
}

// ─────────────────────────────────────────────────────────────────────────────
// PBRMaterial — default values
// ─────────────────────────────────────────────────────────────────────────────

TEST(PBRMaterial, DefaultBaseColorIsWhiteOpaque) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat.baseColor.x, 1.0F);
    EXPECT_FLOAT_EQ(mat.baseColor.y, 1.0F);
    EXPECT_FLOAT_EQ(mat.baseColor.z, 1.0F);
    EXPECT_FLOAT_EQ(mat.baseColor.w, 1.0F);
}

TEST(PBRMaterial, DefaultMetallicIsZero) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat.metallic, 0.0F);
}

TEST(PBRMaterial, DefaultRoughnessIsHalf) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat.roughness, 0.5F);
}

TEST(PBRMaterial, DefaultAmbientOcclusionIsOne) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat.ao, 1.0F);
}

TEST(PBRMaterial, DefaultPaddingIsZero) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat._pad0, 0.0F);
}

TEST(PBRMaterial, DefaultEmissiveIsBlackZeroIntensity) {
    PBRMaterial mat{};
    EXPECT_FLOAT_EQ(mat.emissive.x, 0.0F);
    EXPECT_FLOAT_EQ(mat.emissive.y, 0.0F);
    EXPECT_FLOAT_EQ(mat.emissive.z, 0.0F);
    EXPECT_FLOAT_EQ(mat.emissive.w, 0.0F);
}

// ─────────────────────────────────────────────────────────────────────────────
// PBRMaterial — size
// ─────────────────────────────────────────────────────────────────────────────

TEST(PBRMaterial, SizeIs48Bytes) {
    EXPECT_EQ(sizeof(PBRMaterial), 48u);
}

TEST(PBRMaterial, FieldOffsets) {
    // baseColor at 0, metallic at 16, roughness at 20, ao at 24, _pad0 at 28, emissive at 32
    EXPECT_EQ(offsetof(PBRMaterial, baseColor),  0u);
    EXPECT_EQ(offsetof(PBRMaterial, metallic),   16u);
    EXPECT_EQ(offsetof(PBRMaterial, roughness),  20u);
    EXPECT_EQ(offsetof(PBRMaterial, ao),         24u);
    EXPECT_EQ(offsetof(PBRMaterial, _pad0),      28u);
    EXPECT_EQ(offsetof(PBRMaterial, emissive),   32u);
}

TEST(PBRMaterial, WriteAndReadBack) {
    PBRMaterial mat{};
    mat.baseColor  = {0.8F, 0.2F, 0.1F, 1.0F};
    mat.metallic   = 1.0F;
    mat.roughness  = 0.1F;
    mat.ao         = 0.9F;
    mat.emissive   = {1.0F, 0.5F, 0.0F, 2.5F};

    EXPECT_FLOAT_EQ(mat.baseColor.x, 0.8F);
    EXPECT_FLOAT_EQ(mat.metallic,    1.0F);
    EXPECT_FLOAT_EQ(mat.roughness,   0.1F);
    EXPECT_FLOAT_EQ(mat.ao,          0.9F);
    EXPECT_FLOAT_EQ(mat.emissive.w,  2.5F);
}

// ─────────────────────────────────────────────────────────────────────────────
// SortKey::Opaque — bit field encoding
// ─────────────────────────────────────────────────────────────────────────────

TEST(SortKeyOpaque, PipelineIdInMostSignificantBits) {
    // A command with pipelineId=1 must sort after one with pipelineId=0,
    // regardless of material, depth, or mesh values.
    uint64_t low  = SortKey::Opaque(0, 255, 1.0F, 0x00FFFFFFu);
    uint64_t high = SortKey::Opaque(1, 0,   0.0F, 0u);
    EXPECT_LT(low, high);
}

TEST(SortKeyOpaque, MaterialIdBreaksTiesAfterPipeline) {
    // Same pipeline, material 0 sorts before material 1.
    uint64_t low  = SortKey::Opaque(5, 0, 1.0F, 0x00FFFFFFu);
    uint64_t high = SortKey::Opaque(5, 1, 0.0F, 0u);
    EXPECT_LT(low, high);
}

TEST(SortKeyOpaque, DepthBreaksTiesAfterMaterial) {
    // Same pipeline + material. Smaller depth (closer) sorts before larger depth.
    uint64_t near = SortKey::Opaque(3, 7, 0.1F, 0u);
    uint64_t far  = SortKey::Opaque(3, 7, 0.9F, 0u);
    EXPECT_LT(near, far);
}

TEST(SortKeyOpaque, MeshIdBreaksFinalTies) {
    // Same pipeline, material, and depth — mesh 0 sorts before mesh 1.
    uint64_t a = SortKey::Opaque(0, 0, 0.5F, 0u);
    uint64_t b = SortKey::Opaque(0, 0, 0.5F, 1u);
    EXPECT_LT(a, b);
}

TEST(SortKeyOpaque, DepthClampedAtZero) {
    // Negative depth must clamp to 0.
    uint64_t clamped  = SortKey::Opaque(0, 0, -1.0F, 0u);
    uint64_t atZero   = SortKey::Opaque(0, 0,  0.0F, 0u);
    EXPECT_EQ(clamped, atZero);
}

TEST(SortKeyOpaque, DepthClampedAtOne) {
    // Depth > 1 must clamp to 1 (max 24-bit depth value).
    uint64_t clamped  = SortKey::Opaque(0, 0, 2.0F,  0u);
    uint64_t atOne    = SortKey::Opaque(0, 0, 1.0F,  0u);
    EXPECT_EQ(clamped, atOne);
}

TEST(SortKeyOpaque, MeshIdMaskedTo24Bits) {
    // Bits above 24 in meshId must be ignored.
    uint64_t a = SortKey::Opaque(0, 0, 0.5F, 0x00000001u);
    uint64_t b = SortKey::Opaque(0, 0, 0.5F, 0xFF000001u); // upper 8 bits set
    EXPECT_EQ(a, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// SortKey::Transparent — depth inversion
// ─────────────────────────────────────────────────────────────────────────────

TEST(SortKeyTransparent, FarObjectSortsBeforeNear) {
    // For transparency, depth is inverted: far (1.0) → small key → sorts first.
    uint64_t far  = SortKey::Transparent(0, 0, 1.0F, 0u);
    uint64_t near = SortKey::Transparent(0, 0, 0.0F, 0u);
    EXPECT_LT(far, near);
}

TEST(SortKeyTransparent, DepthClampedAtZero) {
    uint64_t clamped = SortKey::Transparent(0, 0, -1.0F, 0u);
    uint64_t atZero  = SortKey::Transparent(0, 0,  0.0F, 0u);
    EXPECT_EQ(clamped, atZero);
}

TEST(SortKeyTransparent, DepthClampedAtOne) {
    uint64_t clamped = SortKey::Transparent(0, 0, 2.0F, 0u);
    uint64_t atOne   = SortKey::Transparent(0, 0, 1.0F, 0u);
    EXPECT_EQ(clamped, atOne);
}

TEST(SortKeyTransparent, OpaqueAndTransparentKeysDifferForSameDepth) {
    // Opaque front-to-back and transparent back-to-front should produce
    // different keys for non-symmetric depths (depth != 1 - depth).
    // At depth=0.5 the formulas coincide (0.5 == 1-0.5), so we use 0.3.
    uint64_t opaque      = SortKey::Opaque(0, 0, 0.3F, 0u);
    uint64_t transparent = SortKey::Transparent(0, 0, 0.3F, 0u);
    EXPECT_NE(opaque, transparent);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawList — lifecycle and sorting
// ─────────────────────────────────────────────────────────────────────────────

TEST(DrawList, StartsEmpty) {
    DrawList list;
    EXPECT_EQ(list.Count(), 0u);
    EXPECT_TRUE(list.GetCommands().empty());
}

TEST(DrawList, AddIncreasesCount) {
    DrawList list;
    list.Add(DrawCommand{});
    EXPECT_EQ(list.Count(), 1u);
    list.Add(DrawCommand{});
    EXPECT_EQ(list.Count(), 2u);
}

TEST(DrawList, ClearResetsCountToZero) {
    DrawList list;
    list.Add(DrawCommand{});
    list.Add(DrawCommand{});
    list.Clear();
    EXPECT_EQ(list.Count(), 0u);
    EXPECT_TRUE(list.GetCommands().empty());
}

TEST(DrawList, SortProducesAscendingOrder) {
    DrawList list;

    DrawCommand c1;
    c1.sortKey = SortKey::Opaque(2, 0, 0.5F, 0u); // far pipeline

    DrawCommand c2;
    c2.sortKey = SortKey::Opaque(0, 0, 0.5F, 0u); // near pipeline

    DrawCommand c3;
    c3.sortKey = SortKey::Opaque(1, 0, 0.5F, 0u); // middle pipeline

    list.Add(c1);
    list.Add(c2);
    list.Add(c3);
    list.Sort();

    const auto& cmds = list.GetCommands();
    ASSERT_EQ(cmds.size(), 3u);
    EXPECT_LE(cmds[0].sortKey, cmds[1].sortKey);
    EXPECT_LE(cmds[1].sortKey, cmds[2].sortKey);
}

TEST(DrawList, SortOrderingMatchesSortKeySemantics) {
    DrawList list;

    // Pipeline 0, material 0, near depth → should be first after sort
    DrawCommand first;
    first.sortKey   = SortKey::Opaque(0, 0, 0.1F, 0u);
    first.meshId    = 1;
    first.albedoSrvIndex = 0;

    // Pipeline 1 → should be last
    DrawCommand last;
    last.sortKey    = SortKey::Opaque(1, 0, 0.0F, 0u);
    last.meshId     = 2;
    last.albedoSrvIndex = 0;

    // Pipeline 0, material 0, far depth → should be between first and last
    DrawCommand middle;
    middle.sortKey   = SortKey::Opaque(0, 0, 0.9F, 0u);
    middle.meshId    = 3;
    middle.albedoSrvIndex = 0;

    list.Add(last);
    list.Add(first);
    list.Add(middle);
    list.Sort();

    const auto& cmds = list.GetCommands();
    ASSERT_EQ(cmds.size(), 3u);
    EXPECT_EQ(cmds[0].meshId, 1u);  // first (pipeline 0, near)
    EXPECT_EQ(cmds[1].meshId, 3u);  // middle (pipeline 0, far)
    EXPECT_EQ(cmds[2].meshId, 2u);  // last (pipeline 1)
}

TEST(DrawList, ClearAfterSortResetsState) {
    DrawList list;
    list.Add(DrawCommand{.sortKey = 100});
    list.Sort();
    list.Clear();
    EXPECT_EQ(list.Count(), 0u);

    // Adding after clear must work normally.
    list.Add(DrawCommand{.sortKey = 5});
    EXPECT_EQ(list.Count(), 1u);
}

TEST(DrawList, GetCommandsReturnsCorrectReference) {
    DrawList list;
    DrawCommand cmd{};
    cmd.sortKey    = 42;
    cmd.meshId     = 7;
    cmd.albedoSrvIndex = 3;
    list.Add(cmd);

    const auto& cmds = list.GetCommands();
    ASSERT_EQ(cmds.size(), 1u);
    EXPECT_EQ(cmds[0].sortKey,    42u);
    EXPECT_EQ(cmds[0].meshId,     7u);
    EXPECT_EQ(cmds[0].albedoSrvIndex, 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB — post-modification layout validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(PerFrameCBLighting, SizeIs1024Bytes) {
    EXPECT_EQ(sizeof(PerFrameCB), 1024u);
}

TEST(PerFrameCBLighting, LightingFieldOffsets) {
    // viewProj=0 (64), cameraPos=64 (16), timeParams=80 (16)
    // lightDir=96, lightColor=112, ambientColor=128
    EXPECT_EQ(offsetof(PerFrameCB, lightDir),     96u);
    EXPECT_EQ(offsetof(PerFrameCB, lightColor),   112u);
    EXPECT_EQ(offsetof(PerFrameCB, ambientColor), 128u);
}

TEST(PerFrameCBLighting, DefaultLightingFieldsAreZero) {
    PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.lightDir.x,      0.0F);
    EXPECT_FLOAT_EQ(cb.lightDir.y,      0.0F);
    EXPECT_FLOAT_EQ(cb.lightDir.z,      0.0F);
    EXPECT_FLOAT_EQ(cb.lightDir.w,      0.0F);
    EXPECT_FLOAT_EQ(cb.lightColor.x,    0.0F);
    EXPECT_FLOAT_EQ(cb.lightColor.y,    0.0F);
    EXPECT_FLOAT_EQ(cb.lightColor.z,    0.0F);
    EXPECT_FLOAT_EQ(cb.ambientColor.x,  0.0F);
    EXPECT_FLOAT_EQ(cb.ambientColor.y,  0.0F);
    EXPECT_FLOAT_EQ(cb.ambientColor.z,  0.0F);
}

TEST(PerFrameCBLighting, WriteAndReadBackLightingFields) {
    PerFrameCB cb{};
    cb.lightDir     = {0.0F, -1.0F, 0.0F, 0.0F}; // straight down
    cb.lightColor   = {3.14F, 2.72F, 1.41F, 0.0F};
    cb.ambientColor = {0.1F,  0.1F,  0.15F, 0.0F};

    EXPECT_FLOAT_EQ(cb.lightDir.y,      -1.0F);
    EXPECT_FLOAT_EQ(cb.lightColor.x,     3.14F);
    EXPECT_FLOAT_EQ(cb.ambientColor.z,   0.15F);
}
