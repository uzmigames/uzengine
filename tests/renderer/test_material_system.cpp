#include <gtest/gtest.h>
#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/texture.h"
#include "engine/renderer/rhi/rhi.h"
#include "engine/core/platform.h"
#include <cstdio>

using namespace uz;

// ============================================================================
// Mock RHI for testing MaterialManager without a real GPU
// ============================================================================

static rhi_backend_t g_mock_rhi;
static uint64_t g_next_handle = 1;

static RhiTexture mock_texture_create(const RhiTextureDesc* desc) {
    (void)desc;
    return g_next_handle++;
}

static void mock_texture_destroy(RhiTexture tex) {
    (void)tex;
}

static void mock_texture_upload(RhiTexture tex, const void* data, size_t size) {
    (void)tex; (void)data; (void)size;
}

static void mock_texture_upload_batch(const RhiTextureUploadItem* items, uint32_t count) {
    (void)items; (void)count;
}

void setup_mock_rhi() {
    g_mock_rhi.texture_create = mock_texture_create;
    g_mock_rhi.texture_destroy = mock_texture_destroy;
    g_mock_rhi.texture_upload = mock_texture_upload;
    g_mock_rhi.texture_upload_batch = mock_texture_upload_batch;
    rhi_set_backend(&g_mock_rhi);
}

// ============================================================================
// Mock Texture Functions to satisfy linker without including texture.cpp
// ============================================================================

namespace uz {
    GpuTexture load_texture_from_file(const char* path, const char* debug_name) {
        (void)path; (void)debug_name;
        GpuTexture tex;
        tex.handle = g_next_handle++;
        tex.width = 128;
        tex.height = 128;
        return tex;
    }

    void destroy_texture(GpuTexture& tex) {
        tex.handle = RHI_NULL;
    }

    DecodedTexture decode_texture_from_file(const char* path) {
        (void)path;
        DecodedTexture d;
        d.width = 128;
        d.height = 128;
        d.pixel_size = 128 * 128 * 4;
        d.pixels = static_cast<uint8_t*>(malloc(d.pixel_size));
        if (d.pixels) memset(d.pixels, 0xFF, d.pixel_size);
        return d;
    }

    void free_decoded_texture(DecodedTexture& decoded) {
        free(decoded.pixels);
        decoded.pixels = nullptr;
    }

    void free_decoded_texture_pixels(uint8_t* pixels) {
        free(pixels);
    }
}

// ============================================================================
// Material System Tests
// ============================================================================

class MaterialSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        setup_mock_rhi();
        // Create a dummy texture file for testing
        FILE* f = fopen("test_tex.png", "wb");
        if (f) {
            uint8_t dummy[4] = {0, 0, 0, 0};
            fwrite(dummy, 1, 4, f);
            fclose(f);
        }

        // Create a dummy material file
        f = fopen("test.uzmat", "w");
        if (f) {
            fprintf(f, "[material]\n");
            fprintf(f, "name = TestMaterial\n");
            fprintf(f, "shader = pbr\n");
            fprintf(f, "albedo = [1.0, 0.5, 0.0]\n");
            fprintf(f, "roughness = 0.5\n");
            fprintf(f, "base_tex = test_tex.png\n");
            fclose(f);
        }
    }

    void TearDown() override {
        remove("test_tex.png");
        remove("test.uzmat");
        rhi_set_backend(nullptr);
    }

    MaterialManager manager_;
};

TEST_F(MaterialSystemTest, LoadMaterial_ParsesBasicProperties) {
    Material* mat = manager_.load("test.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->name, "TestMaterial");
    EXPECT_EQ(mat->shader_name, "pbr");
}

TEST_F(MaterialSystemTest, LoadMaterial_ParsesParameters) {
    Material* mat = manager_.load("test.uzmat");
    ASSERT_NE(mat, nullptr);
    
    // Scalar
    ASSERT_TRUE(mat->parameters.count("roughness"));
    EXPECT_FLOAT_EQ(mat->parameters.at("roughness"), 0.5f);
    
    // Vector
    ASSERT_TRUE(mat->vec_parameters.count("albedo"));
    auto& albedo = mat->vec_parameters.at("albedo");
    ASSERT_EQ(albedo.size(), 3);
    EXPECT_FLOAT_EQ(albedo[0], 1.0f);
    EXPECT_FLOAT_EQ(albedo[1], 0.5f);
    EXPECT_FLOAT_EQ(albedo[2], 0.0f);
}

TEST_F(MaterialSystemTest, LoadMaterial_ResolvesTextures) {
    Material* mat = manager_.load("test.uzmat");
    ASSERT_NE(mat, nullptr);

    ASSERT_TRUE(mat->textures.count("base_tex"));
    EXPECT_EQ(mat->textures.at("base_tex"), "test_tex.png");

    // Texture handle is RHI_NULL initially because textures load asynchronously.
    // The handle becomes valid after texture_tick() processes the async decode.
    ASSERT_TRUE(mat->texture_handles.count("base_tex"));
    EXPECT_EQ(mat->texture_handles.at("base_tex"), RHI_NULL);
}

TEST_F(MaterialSystemTest, LoadMaterial_CachesInstances) {
    Material* mat1 = manager_.load("test.uzmat");
    Material* mat2 = manager_.load("test.uzmat");
    
    EXPECT_NE(mat1, nullptr);
    EXPECT_EQ(mat1, mat2);
}

TEST_F(MaterialSystemTest, ReloadMaterial_UpdatesProperties) {
    manager_.load("test.uzmat");

    // Update file
    FILE* f = fopen("test.uzmat", "w");
    fprintf(f, "[material]\nshader = unlit\nroughness = 0.9\n");
    fclose(f);

    EXPECT_TRUE(manager_.reload("test.uzmat"));

    Material* mat = manager_.get("test.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->shader_name, "unlit");
    EXPECT_FLOAT_EQ(mat->parameters.at("roughness"), 0.9f);
}

// ============================================================================
// PBR Material Feature Tests (phase2-pbr-quality-improvements D1)
// ============================================================================

// --- Displacement / POM parameters ---

class PBRMaterialTest : public ::testing::Test {
protected:
    void SetUp() override {
        setup_mock_rhi();
        // Create dummy texture files for all PBR slots
        for (auto* name : {"albedo.jpg", "normal.jpg", "roughness.jpg",
                           "ao.jpg", "displacement.jpg", "emission.png"}) {
            FILE* f = fopen(name, "wb");
            if (f) { uint8_t d[4] = {}; fwrite(d, 1, 4, f); fclose(f); }
        }
    }

    void TearDown() override {
        for (auto* name : {"albedo.jpg", "normal.jpg", "roughness.jpg",
                           "ao.jpg", "displacement.jpg", "emission.png",
                           "pbr_full.uzmat", "pbr_minimal.uzmat",
                           "pbr_emission.uzmat", "pbr_uv.uzmat"}) {
            remove(name);
        }
        rhi_set_backend(nullptr);
    }

    void write_material(const char* path, const char* content) {
        FILE* f = fopen(path, "w");
        ASSERT_NE(f, nullptr);
        fputs(content, f);
        fclose(f);
    }

    MaterialManager manager_;
};

TEST_F(PBRMaterialTest, ParsesDisplacementAndParallaxScale) {
    write_material("pbr_full.uzmat",
        "[material]\n"
        "name = StoneBricks\n"
        "shader = pbr\n"
        "albedo = [0.8, 0.75, 0.65]\n"
        "metallic = 0.0\n"
        "roughness = 0.85\n"
        "ao = 1.0\n"
        "albedo_texture = albedo.jpg\n"
        "normal_texture = normal.jpg\n"
        "roughness_texture = roughness.jpg\n"
        "ao_texture = ao.jpg\n"
        "displacement_texture = displacement.jpg\n"
        "parallax_scale = 0.05\n"
    );

    Material* mat = manager_.load("pbr_full.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->name, "StoneBricks");
    EXPECT_EQ(mat->shader_name, "pbr");

    // Scalar params
    ASSERT_TRUE(mat->parameters.count("metallic"));
    EXPECT_FLOAT_EQ(mat->parameters.at("metallic"), 0.0f);
    ASSERT_TRUE(mat->parameters.count("roughness"));
    EXPECT_FLOAT_EQ(mat->parameters.at("roughness"), 0.85f);
    ASSERT_TRUE(mat->parameters.count("ao"));
    EXPECT_FLOAT_EQ(mat->parameters.at("ao"), 1.0f);
    ASSERT_TRUE(mat->parameters.count("parallax_scale"));
    EXPECT_FLOAT_EQ(mat->parameters.at("parallax_scale"), 0.05f);

    // Vector params
    ASSERT_TRUE(mat->vec_parameters.count("albedo"));
    auto& alb = mat->vec_parameters.at("albedo");
    ASSERT_EQ(alb.size(), 3);
    EXPECT_FLOAT_EQ(alb[0], 0.8f);
    EXPECT_FLOAT_EQ(alb[1], 0.75f);
    EXPECT_FLOAT_EQ(alb[2], 0.65f);

    // All 5 PBR textures queued for async loading
    EXPECT_TRUE(mat->texture_handles.count("albedo_texture"));
    EXPECT_TRUE(mat->texture_handles.count("normal_texture"));
    EXPECT_TRUE(mat->texture_handles.count("roughness_texture"));
    EXPECT_TRUE(mat->texture_handles.count("ao_texture"));
    EXPECT_TRUE(mat->texture_handles.count("displacement_texture"));
}

TEST_F(PBRMaterialTest, ParsesPerMaterialShaderParams) {
    write_material("pbr_full.uzmat",
        "[material]\n"
        "name = DetailMat\n"
        "shader = pbr\n"
        "roughness = 0.5\n"
        "env_multiplier = 0.8\n"
        "normal_strength = 1.5\n"
        "specular_multiplier = 1.2\n"
        "self_shadow_strength = 0.7\n"
        "cavity_strength = 0.4\n"
    );

    Material* mat = manager_.load("pbr_full.uzmat");
    ASSERT_NE(mat, nullptr);

    ASSERT_TRUE(mat->parameters.count("env_multiplier"));
    EXPECT_FLOAT_EQ(mat->parameters.at("env_multiplier"), 0.8f);

    ASSERT_TRUE(mat->parameters.count("normal_strength"));
    EXPECT_FLOAT_EQ(mat->parameters.at("normal_strength"), 1.5f);

    ASSERT_TRUE(mat->parameters.count("specular_multiplier"));
    EXPECT_FLOAT_EQ(mat->parameters.at("specular_multiplier"), 1.2f);

    ASSERT_TRUE(mat->parameters.count("self_shadow_strength"));
    EXPECT_FLOAT_EQ(mat->parameters.at("self_shadow_strength"), 0.7f);

    ASSERT_TRUE(mat->parameters.count("cavity_strength"));
    EXPECT_FLOAT_EQ(mat->parameters.at("cavity_strength"), 0.4f);
}

TEST_F(PBRMaterialTest, ParsesEmissionParams) {
    write_material("pbr_emission.uzmat",
        "[material]\n"
        "name = NeonGlow\n"
        "shader = pbr\n"
        "emission = [1.0, 0.3, 0.0]\n"
        "emission_energy = 5.0\n"
        "emission_texture = emission.png\n"
    );

    Material* mat = manager_.load("pbr_emission.uzmat");
    ASSERT_NE(mat, nullptr);

    // Emission color vector
    ASSERT_TRUE(mat->vec_parameters.count("emission"));
    auto& em = mat->vec_parameters.at("emission");
    ASSERT_EQ(em.size(), 3);
    EXPECT_FLOAT_EQ(em[0], 1.0f);
    EXPECT_FLOAT_EQ(em[1], 0.3f);
    EXPECT_FLOAT_EQ(em[2], 0.0f);

    // Emission energy scalar
    ASSERT_TRUE(mat->parameters.count("emission_energy"));
    EXPECT_FLOAT_EQ(mat->parameters.at("emission_energy"), 5.0f);

    // Emission texture queued for async loading
    EXPECT_TRUE(mat->texture_handles.count("emission_texture"));
}

TEST_F(PBRMaterialTest, ParsesUVTilingScalar) {
    write_material("pbr_uv.uzmat",
        "[material]\n"
        "name = TiledFloor\n"
        "shader = pbr\n"
        "uv_tiling = 3.0\n"
    );

    Material* mat = manager_.load("pbr_uv.uzmat");
    ASSERT_NE(mat, nullptr);

    // Single-float uv_tiling stored as scalar parameter
    ASSERT_TRUE(mat->parameters.count("uv_tiling"));
    EXPECT_FLOAT_EQ(mat->parameters.at("uv_tiling"), 3.0f);
}

TEST_F(PBRMaterialTest, ParsesUVTilingVector) {
    write_material("pbr_uv.uzmat",
        "[material]\n"
        "name = StretchedMat\n"
        "shader = pbr\n"
        "uv_tiling = [2.0, 4.0]\n"
    );

    Material* mat = manager_.load("pbr_uv.uzmat");
    ASSERT_NE(mat, nullptr);

    // Vec2 uv_tiling stored as vec_parameters
    ASSERT_TRUE(mat->vec_parameters.count("uv_tiling"));
    auto& tiling = mat->vec_parameters.at("uv_tiling");
    ASSERT_GE(tiling.size(), 2);
    EXPECT_FLOAT_EQ(tiling[0], 2.0f);
    EXPECT_FLOAT_EQ(tiling[1], 4.0f);
}

TEST_F(PBRMaterialTest, MinimalMaterial_DefaultsWork) {
    write_material("pbr_minimal.uzmat",
        "[material]\n"
        "name = Bare\n"
        "shader = pbr\n"
    );

    Material* mat = manager_.load("pbr_minimal.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->name, "Bare");
    EXPECT_EQ(mat->shader_name, "pbr");

    // No scalar params set — maps should be empty for these keys
    EXPECT_FALSE(mat->parameters.count("roughness"));
    EXPECT_FALSE(mat->parameters.count("metallic"));
    EXPECT_FALSE(mat->parameters.count("parallax_scale"));
    EXPECT_FALSE(mat->parameters.count("emission_energy"));

    // No textures
    EXPECT_TRUE(mat->texture_handles.empty());
}

TEST_F(PBRMaterialTest, CommentsAndEmptyLinesIgnored) {
    write_material("pbr_full.uzmat",
        "[material]\n"
        "# This is a comment\n"
        "; This is also a comment\n"
        "\n"
        "name = Commented\n"
        "shader = pbr\n"
        "roughness = 0.3\n"
        "\n"
        "# metallic = 0.9  <- this should NOT be parsed\n"
    );

    Material* mat = manager_.load("pbr_full.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->name, "Commented");
    EXPECT_FLOAT_EQ(mat->parameters.at("roughness"), 0.3f);
    EXPECT_FALSE(mat->parameters.count("metallic"));
}

TEST_F(PBRMaterialTest, LoadNonexistentFile_ReturnsNull) {
    Material* mat = manager_.load("does_not_exist.uzmat");
    EXPECT_EQ(mat, nullptr);
}

TEST_F(PBRMaterialTest, ReloadNonexistentMaterial_ReturnsFalse) {
    EXPECT_FALSE(manager_.reload("never_loaded.uzmat"));
}

TEST_F(PBRMaterialTest, GetBeforeLoad_ReturnsNull) {
    EXPECT_EQ(manager_.get("not_loaded.uzmat"), nullptr);
}

TEST_F(PBRMaterialTest, ClearRemovesAllMaterials) {
    write_material("pbr_full.uzmat",
        "[material]\nname = A\nshader = pbr\n");
    manager_.load("pbr_full.uzmat");
    EXPECT_NE(manager_.get("pbr_full.uzmat"), nullptr);

    manager_.clear();
    EXPECT_EQ(manager_.get("pbr_full.uzmat"), nullptr);
}

TEST_F(PBRMaterialTest, FullSandMaterial_MatchesExpectedFormat) {
    // Replicate the actual sand.uzmat format used in pbr_test.uzscene
    write_material("pbr_full.uzmat",
        "[material]\n"
        "name = Sand\n"
        "shader = pbr\n"
        "albedo = [1.05, 0.95, 0.82]\n"
        "metallic = 0.0\n"
        "roughness = 1.0\n"
        "ao = 1.0\n"
        "albedo_texture = albedo.jpg\n"
        "normal_texture = normal.jpg\n"
        "roughness_texture = roughness.jpg\n"
        "ao_texture = ao.jpg\n"
        "displacement_texture = displacement.jpg\n"
        "parallax_scale = 0.03\n"
        "normal_strength = 1.3\n"
        "self_shadow_strength = 1.0\n"
        "cavity_strength = 0.4\n"
        "uv_tiling = 3.0\n"
    );

    Material* mat = manager_.load("pbr_full.uzmat");
    ASSERT_NE(mat, nullptr);
    EXPECT_EQ(mat->name, "Sand");

    // Verify all scalar params
    EXPECT_FLOAT_EQ(mat->parameters.at("metallic"), 0.0f);
    EXPECT_FLOAT_EQ(mat->parameters.at("roughness"), 1.0f);
    EXPECT_FLOAT_EQ(mat->parameters.at("ao"), 1.0f);
    EXPECT_FLOAT_EQ(mat->parameters.at("parallax_scale"), 0.03f);
    EXPECT_FLOAT_EQ(mat->parameters.at("normal_strength"), 1.3f);
    EXPECT_FLOAT_EQ(mat->parameters.at("self_shadow_strength"), 1.0f);
    EXPECT_FLOAT_EQ(mat->parameters.at("cavity_strength"), 0.4f);
    EXPECT_FLOAT_EQ(mat->parameters.at("uv_tiling"), 3.0f);

    // Verify albedo vec3 with values > 1.0
    auto& alb = mat->vec_parameters.at("albedo");
    EXPECT_FLOAT_EQ(alb[0], 1.05f);
    EXPECT_FLOAT_EQ(alb[1], 0.95f);
    EXPECT_FLOAT_EQ(alb[2], 0.82f);

    // All 5 textures present
    EXPECT_EQ(mat->texture_handles.size(), 5);
}
