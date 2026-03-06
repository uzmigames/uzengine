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

void setup_mock_rhi() {
    g_mock_rhi.texture_create = mock_texture_create;
    g_mock_rhi.texture_destroy = mock_texture_destroy;
    g_mock_rhi.texture_upload = mock_texture_upload;
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
    
    ASSERT_TRUE(mat->texture_handles.count("base_tex"));
    EXPECT_NE(mat->texture_handles.at("base_tex"), RHI_NULL);
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
