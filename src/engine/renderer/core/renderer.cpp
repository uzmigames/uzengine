// ---------------------------------------------------------------------------
// renderer.cpp -- Renderer implementation using the RHI abstraction layer
//
// All GPU work goes through rhi()->* calls.  Zero backend-specific includes.
//
// RULE RHI-1: No backend types stored above rhi.h
// RULE RHI-2: No DirectXMath above rhi.h — uses uz::Mat4
// RULE CS-7:  No exceptions — returns bool for fallible operations
// ---------------------------------------------------------------------------

#include "renderer.h"

// Only need the backend vtable symbol for rhi_set_backend() in init().
extern "C" {
extern const rhi_backend_t rhi_dx12_backend;
}

#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/safe_string.h"

#include "engine/core/diag.h"
#include "engine/core/profiler.h"
#include "engine/renderer/resources/mesh_primitives.h"
#include "engine/renderer/resources/texture.h"
#include "engine/renderer/resources/material.h"
#include "engine/renderer/resources/brdf_lut.h"
#include "engine/renderer/resources/sky_cubemap.h"
#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/scene/scene.h"
#include "engine/scene/component_data.h"
#include "engine/scene/component_store.h"

namespace uz {

// ===========================================================================
// Public API
// ===========================================================================

bool Renderer::init(void* window_handle, uint32_t width, uint32_t height) {
    UZ_PROFILE_ZONE("Renderer::init");
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    // Register the DX12 backend before using rhi().
    rhi_set_backend(&rhi_dx12_backend);
    DIAG("=== renderer.init START ===");

    // Initialize the RHI backend (device, swapchain, heaps, cmd queue).
    DIAG("rhi()->init()...");
    {
        UZ_PROFILE_ZONE("init/rhi_init");
        if (!rhi()->init(window_handle, width, height)) {
            DIAG("FATAL: rhi()->init() returned false");
            return false;
        }
    }
    DIAG("rhi()->init() OK");

    // Create 1x1 placeholder textures so materials render immediately.
    texture_init_placeholders();

    // Initialize shader library with builtin shaders.
    DIAG("shaderLib_.register_builtin_shaders()...");
    {
        UZ_PROFILE_ZONE("init/register_shaders");
        shaderLib_.register_builtin_shaders();
    }

    // Create pipelines via shader library.
    DIAG("createPipelineStates()...");
    if (!createPipelineStates()) {
        DIAG("FATAL: createPipelineStates() failed");
        return false;
    }
    DIAG("createPipelineStates() OK");


    DIAG("createDepthBuffer()...");
    if (!createDepthBuffer()) {
        DIAG("FATAL: createDepthBuffer() failed");
        return false;
    }

    DIAG("createReadbackBuffers()...");
    if (!createReadbackBuffers()) {
        DIAG("FATAL: createReadbackBuffers() failed");
        return false;
    }

    // EditorComponent — now fully RHI, no bridge needed.
    DIAG("editor_.init()...");
    editor_.init();

    // StaticMeshRenderer — PBR lit meshes.
    DIAG("meshRenderer_.init()...");
    if (!meshRenderer_.init(shaderLib_)) {
        DIAG("WARNING: meshRenderer_.init() failed — PBR disabled");
    }

    // Setup test scene with PBR spheres.
    // setupTestScene();

    // Engine-math identity matrix.
    viewProj_ = Mat4::identity();

    // Record the stream start time.
    streamTimer_.reset();

    // Create shared sphere mesh for proxies
    MeshData sphereData = generate_sphere(32, 16, 1.0f);
    sphereMesh_ = create_gpu_mesh(sphereData);

    // Create fallback textures
    RhiTextureDesc texDesc{};
    texDesc.depth = 1;
    texDesc.mip_levels = 1;
    texDesc.array_size = 1;
    texDesc.format = RHI_FORMAT_RGBA8_UNORM;
    texDesc.dimension = RHI_TEXTURE_DIM_2D;
    texDesc.usage = RHI_TEXTURE_USAGE_SHADER_READ | RHI_TEXTURE_USAGE_COPY_DST;

    const uint32_t whitePixel = 0xFFFFFFFF;
    texDesc.width = 1; texDesc.height = 1; texDesc.debug_name = "Renderer_WhiteTex";
    whiteTex_ = rhi()->texture_create(&texDesc);
    if (whiteTex_ != RHI_NULL) {
        rhi()->texture_upload(whiteTex_, &whitePixel, sizeof(uint32_t));
        frameConstants_.white_texture = whiteTex_;
    }

    const uint32_t blackPixel = 0xFF000000;
    texDesc.debug_name = "Renderer_BlackTex";
    blackTex_ = rhi()->texture_create(&texDesc);
    if (blackTex_ != RHI_NULL) {
        rhi()->texture_upload(blackTex_, &blackPixel, sizeof(uint32_t));
        frameConstants_.black_texture = blackTex_;
    }

    const uint32_t normalPixel = 0xFFFF8080; // (128, 128, 255)
    texDesc.debug_name = "Renderer_NormalTex";
    normalTex_ = rhi()->texture_create(&texDesc);
    if (normalTex_ != RHI_NULL) {
        rhi()->texture_upload(normalTex_, &normalPixel, sizeof(uint32_t));
        frameConstants_.normal_texture = normalTex_;
    }

    // Create shadow map texture (D32_FLOAT, shader-readable for PCF sampling)
    DIAG("createShadowMap()...");
    {
        RhiTextureDesc smDesc{};
        smDesc.width      = kShadowAtlasSize;
        smDesc.height     = kShadowAtlasSize;
        smDesc.depth      = 1;
        smDesc.mip_levels = 1;
        smDesc.array_size = 1;
        smDesc.format     = RHI_FORMAT_D32_FLOAT;
        smDesc.dimension  = RHI_TEXTURE_DIM_2D;
        smDesc.usage      = RHI_TEXTURE_USAGE_DEPTH_STENCIL | RHI_TEXTURE_USAGE_SHADER_READ;
        smDesc.debug_name = "ShadowAtlas_CSM";
        shadowMap_ = rhi()->texture_create(&smDesc);
        if (shadowMap_ == RHI_NULL)
            DIAG("WARNING: shadowMap creation failed — shadows disabled");
    }

    // Create per-frame CBVs (b1 and b2 are root CBVs).
    // 512 bytes = extended LightFrameCB (directional + 8 punctual lights). D3D12 CBV 256-aligned.
    {
        RhiBufferDesc cbvDesc{};
        cbvDesc.size       = 512;
        cbvDesc.usage      = RHI_BUFFER_USAGE_CONSTANT | RHI_BUFFER_USAGE_UPLOAD;

        cbvDesc.debug_name = "LightFrameCBV";
        lightFrameCBV_ = rhi()->buffer_create(&cbvDesc);
        if (lightFrameCBV_ == RHI_NULL)
            DIAG("WARNING: LightFrameCBV creation failed");

        cbvDesc.debug_name = "SSAO_B1_CBV";
        ssaoB1CBV_ = rhi()->buffer_create(&cbvDesc);

        cbvDesc.debug_name = "Outline_B1_CBV";
        outlineCBV_ = rhi()->buffer_create(&cbvDesc);

        cbvDesc.debug_name = "PostProcessCBV";
        postProcessCBV_ = rhi()->buffer_create(&cbvDesc);
        if (postProcessCBV_ == RHI_NULL)
            DIAG("WARNING: PostProcessCBV creation failed");
    }

    // Initialize GPU compute IBL pipeline.
    DIAG("IblBaker::init()...");
    if (!iblBaker_.init())
        DIAG("WARNING: IblBaker init failed — falling back to CPU IBL");

    // Generate BRDF integration LUT (256x256 RG16F, environment-independent).
    {
        UZ_PROFILE_ZONE("init/brdf_lut");
        DIAG("bake_brdf_lut()...");
        brdfLut_ = iblBaker_.bake_brdf_lut();
        if (brdfLut_ == RHI_NULL) {
            DIAG("WARNING: GPU BRDF LUT failed, trying CPU fallback");
            brdfLut_ = generate_brdf_lut();
        }
        if (brdfLut_ == RHI_NULL)
            DIAG("WARNING: BRDF LUT generation failed — IBL specular disabled");
    }

    // IBL cubemaps are deferred to setScene() which is called immediately after
    // init(). This avoids generating them twice (init defaults + scene values).
    // envCubemap_, irradianceCube_, prefilteredCube_ remain RHI_NULL until then;
    // the PBR shader handles RHI_NULL IBL textures gracefully (no IBL contribution).
    DIAG("IBL cubemaps deferred to setScene() — skipping init bake");

    // Create post-process render targets (HDR, SSAO, bloom chain)
    DIAG("createPostProcessTargets()...");
    if (!createPostProcessTargets())
        DIAG("WARNING: post-process targets failed — falling back to no post-processing");

    DIAG("=== renderer.init COMPLETE ===");
    initialized_ = true;
    return true;
}

bool Renderer::resize(int width, int height) {
    if (width < 64 || width > 7680 || height < 64 || height > 4320)
        return false;
    if (!initialized_)
        return false;

    const uint32_t newW = static_cast<uint32_t>(width);
    const uint32_t newH = static_cast<uint32_t>(height);

    if (newW == width_ && newH == height_)
        return true;

    // Destroy old depth and readback resources.
    if (rhiDepthBuf_ != RHI_NULL) {
        rhi()->texture_destroy(rhiDepthBuf_);
        rhiDepthBuf_ = RHI_NULL;
    }
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (rhiReadbackBufs_[i] != RHI_NULL) {
            rhi()->buffer_destroy(rhiReadbackBufs_[i]);
            rhiReadbackBufs_[i] = RHI_NULL;
        }
    }

    rhi()->resize(newW, newH);

    width_  = newW;
    height_ = newH;

    createDepthBuffer();
    createReadbackBuffers();
    destroyPostProcessTargets();
    createPostProcessTargets();

    return true;
}

void Renderer::update(double totalTime, double deltaTime) {
    // Store previous-frame UNJITTERED viewProj for velocity buffer pass.
    // Velocity must capture real motion only, not TAA jitter difference (UE4/UE5 approach).
    prevViewProj_ = viewProjUnjittered_;

    fpsOverlay_.update(deltaTime);
    lastDeltaTime_ = std::min(static_cast<float>(deltaTime), 0.1f);

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);

    // Update transforms BEFORE using them for camera or proxies
    if (scene_) {
        scene_update_world_transforms(scene_);
    }

    // Check for pending viewport resize.
    if (sceneParams_) {
        auto [rw, rh] = sceneParams_->consumeResize();
        if (rw > 0 && rh > 0 &&
            (static_cast<uint32_t>(rw) != width_ ||
             static_cast<uint32_t>(rh) != height_)) {
            resize(rw, rh);
        }
    }

    if (sceneParams_) {
        std::lock_guard<std::mutex> lk(sceneParams_->bgMutex);
        clearR_ = sceneParams_->bgR;
        clearG_ = sceneParams_->bgG;
        clearB_ = sceneParams_->bgB;
    }

    // Camera mode: orbit benchmark overrides fly-cam when active.
    const float dt = std::min(static_cast<float>(deltaTime), 0.1f);
    const bool orbitMode = sceneParams_ &&
        sceneParams_->benchmarkOrbit.load(std::memory_order_relaxed);

    CameraState camState = orbitMode
        ? camera_update_orbit(static_cast<float>(totalTime), aspect)
        : camera_update_fly(sceneParams_, aspect, dt);
    viewProj_ = camState.view_proj;

    // Build projection matrix for SSAO depth reconstruction
    // Matches camera_update_fly: fov = pi/4, near = 0.1, far = 500
    {
        constexpr float fov  = 3.14159265f / 4.0f;
        constexpr float zn   = 0.1f;
        constexpr float zf   = 500.0f;
        const float tanHalf  = std::tan(fov * 0.5f);
        projMatrix_ = Mat4{};
        projMatrix_.m[0][0] = 1.0f / (aspect * tanHalf);
        projMatrix_.m[1][1] = 1.0f / tanHalf;
        projMatrix_.m[2][2] = zf / (zf - zn);
        projMatrix_.m[2][3] = 1.0f;
        projMatrix_.m[3][2] = -(zn * zf) / (zf - zn);
    }

    // Extract view matrix: viewProj = proj * view (column-major)
    // → view = inv(proj) * viewProj.  Done BEFORE jitter so view matrix is clean.
    viewMatrix_ = mat4_inverse(projMatrix_) * viewProj_;

    // Save unjittered viewProj for velocity buffer (must exclude TAA jitter).
    viewProjUnjittered_ = viewProj_;

    // Apply Halton 2,3 jitter to projection for TAA sub-pixel sampling.
    // Jitter must be applied to the projection matrix (not the combined viewProj)
    // because column 2 of proj maps view-space z to the NDC offset.
    // projMatrix_ stays unjittered (SSAO needs it clean for depth reconstruction).
    if (taaEnabled_) {
        // Halton sequence base-2 and base-3, 8-frame cycle
        auto halton = [](uint32_t index, uint32_t base) -> float {
            float f = 1.0f, result = 0.0f;
            uint32_t i = index;
            while (i > 0) {
                f /= static_cast<float>(base);
                result += f * static_cast<float>(i % base);
                i /= base;
            }
            return result;
        };
        const uint32_t jitterIdx = (frameNumber_ % 8) + 1;  // 1-based for non-zero first sample
        const float jx = halton(jitterIdx, 2) - 0.5f;  // center around 0 [-0.5, 0.5)
        const float jy = halton(jitterIdx, 3) - 0.5f;

        // Store jitter in UV space for TAA resolve shader
        taaJitterX_ = jx / static_cast<float>(width_);
        taaJitterY_ = jy / static_cast<float>(height_);

        // Apply sub-pixel offset to projection column 2, then recompute viewProj.
        // proj[2][0] += jitterX * 2 / width adds jitter * viewZ to clip.x
        Mat4 jitteredProj = projMatrix_;
        jitteredProj.m[2][0] += jx * 2.0f / static_cast<float>(width_);
        jitteredProj.m[2][1] += jy * 2.0f / static_cast<float>(height_);
        viewProj_ = jitteredProj * viewMatrix_;
    } else {
        taaJitterX_ = 0.0f;
        taaJitterY_ = 0.0f;
    }

    // Collect primary directional light from the scene.
    cachedLight_ = light_collect_primary(scene_);

    // -----------------------------------------------------------------------
    // Sync Scene Proxies (§COMPONENT-INTERFACE) — Incremental Strategy
    // -----------------------------------------------------------------------
    if (scene_) {
        UZ_PROFILE_ZONE("update/proxy_sync");
        // Detect if the proxy list needs a full rebuild.
        // Triggers: first time, node count changed, or explicit dirty flag.
        const uint32_t current_count = scene_->node_count;
        if (scene_dirty_ || current_count != last_node_count_ || proxies_.empty()) {
            // Full rebuild — but iterate only live slots, not all 65536.
            for (auto* p : proxies_) delete p;
            proxies_.clear();
            proxy_node_ids_.clear();

            // Derive project base directory from scene source path.
            // Scene path is e.g. "E:/UzEngine/assets/scenes/materials.uzscene"
            // or relative "../../assets/scenes/materials.uzscene" from build/Release.
            // We go up from the scene dir to find the project root.
            std::string base_dir;
            if (scene_->source_path[0] != '\0') {
                std::string sp(scene_->source_path);
                // Normalize backslashes
                for (auto& c : sp) if (c == '\\') c = '/';
                auto lastSlash = sp.rfind('/');
                if (lastSlash != std::string::npos) {
                    // scene dir = "E:/UzEngine/assets/scenes" → go up 2 levels → "E:/UzEngine/"
                    std::string scene_dir = sp.substr(0, lastSlash);
                    auto s2 = scene_dir.rfind('/');
                    if (s2 != std::string::npos) {
                        std::string parent = scene_dir.substr(0, s2);
                        auto s3 = parent.rfind('/');
                        if (s3 != std::string::npos) {
                            base_dir = parent.substr(0, s3 + 1); // "E:/UzEngine/"
                        }
                    }
                }
            }

            for (uint32_t i = 1; i < scene_->next_id; ++i) {
                if (!component_has<StaticMeshData>(&scene_->components, i))
                    continue;
                auto* data = component_get<StaticMeshData>(&scene_->components, i);
                if (!data) continue;

                auto* proxy = new MeshProxy();

                // Resolve asset paths: if relative, prepend project base dir.
                auto resolve_path = [&](const char* p) -> std::string {
                    std::string path(p);
                    if (path.empty()) return path;
                    // Already absolute (drive letter or UNC)
                    if ((path.size() >= 2 && path[1] == ':') || (path.size() >= 2 && path[0] == '/' && path[1] == '/'))
                        return path;
                    if (!base_dir.empty())
                        return base_dir + path;
                    return path;
                };

                // Load mesh first (needed to determine material slot count).
                if (data->mesh_path[0] != '\0') {
                    std::string resolved = resolve_path(data->mesh_path);
                    proxy->mesh = meshManager_.load(resolved);
                    if (!proxy->mesh) {
                        DIAG_FMT("Renderer: failed to load mesh '%s' (resolved='%s')",
                                 data->mesh_path, resolved.c_str());
                    }
                }

                // Fallback to internal sphere if mesh failed to load or path empty.
                if (!proxy->mesh) {
                    proxy->mesh = &sphereMesh_;
                }

                // Populate material slots from mesh default materials.
                // Each submesh's material_index maps into this vector.
                {
                    uint32_t slot_count = 1;
                    if (proxy->mesh) {
                        for (const auto& sub : proxy->mesh->submeshes) {
                            if (sub.material_index + 1 > slot_count)
                                slot_count = sub.material_index + 1;
                        }
                    }
                    proxy->material_slots_.resize(slot_count);

                    // Slot 0 override: scene material_path or MaterialInstance.
                    if (data->material_instance_id != 0) {
                        proxy->material_slots_[0].instance =
                            materialInstanceManager_.get(data->material_instance_id);
                        if (proxy->material_slots_[0].instance) {
                            proxy->material_slots_[0].material =
                                proxy->material_slots_[0].instance->parent;
                        }
                    }
                    if (!proxy->material_slots_[0].instance &&
                        data->material_path[0] != '\0') {
                        std::string resolved = resolve_path(data->material_path);
                        proxy->material_slots_[0].material = materialManager_.load(resolved);
                        if (!proxy->material_slots_[0].material) {
                            DIAG_FMT("Renderer: FAILED to load material '%s' (resolved='%s')",
                                     data->material_path, resolved.c_str());
                        }
                    }
                }

                // Cache all material slot parameters (no map lookups per frame).
                proxy->cache_materials();

                proxy->transform = scene_get_world_transform(scene_, i);

                proxy->node_id = i;
                proxies_.push_back(proxy);
                proxy_node_ids_.push_back(i);
            }

            scene_dirty_ = false;
            last_node_count_ = current_count;

            if (!proxies_.empty()) {
                fprintf(stderr, "[UzEngine] Renderer: created %zu scene proxies\n", proxies_.size());
                fflush(stderr);
            }
        } else {
            // Fast path: only update transforms (no allocations, no map lookups).
            for (size_t pi = 0; pi < proxies_.size(); ++pi) {
                static_cast<MeshProxy*>(proxies_[pi])->transform =
                    scene_get_world_transform(scene_, proxy_node_ids_[pi]);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Incremental texture loading — decode + upload up to 4 textures/frame.
    // Poll async texture loading — texture_replace() swaps resources in-place,
    // so materials auto-see updated textures (zero rebinding needed).
    // -----------------------------------------------------------------------
    {
        UZ_PROFILE_ZONE("update/texture_tick");
        texture_tick(4);
    }

    // Update FrameConstants
    frameConstants_.view = Mat4::identity(); // TODO: get from camState
    frameConstants_.proj = Mat4::identity();
    frameConstants_.view_proj = viewProj_;
    frameConstants_.camera_world_pos = camState.eye_pos;
    frameConstants_.time = static_cast<float>(streamTimer_.elapsedMicros() / 1000000.0);
    frameConstants_.delta_time = dt;
    frameConstants_.light_dir = cachedLight_.direction;
    frameConstants_.light_color = cachedLight_.color;
    frameConstants_.light_intensity = cachedLight_.intensity;
    frameConstants_.shadow_bias       = 0.003f;  // slope-based bias for soft shadow edges
    frameConstants_.shadow_ortho_size = 12.0f;  // ±12 world units — covers sphere + ground area
    frameConstants_.shadow_light_dist = 25.0f;  // light eye 25 units from scene center
    frameConstants_.shadow_map        = shadowMap_;
    frameConstants_.brdf_lut          = brdfLut_;
    frameConstants_.light_frame_cbv   = lightFrameCBV_;
    frameConstants_.irradiance_cube   = irradianceCube_;
    frameConstants_.prefiltered_cube  = prefilteredCube_;

    // Gather punctual lights from scene into CPU buffer
    gatherPunctualLights();

    // Upload LightFrameCB to GPU constant buffer (b2 root CBV)
    // Layout: 16 DWORDs directional + 4 DWORDs (count+pad) + 12*8 DWORDs punctual = 116 floats
    if (lightFrameCBV_ != RHI_NULL) {
        float lightFrameData[128] = {};  // 512 bytes, zero-init
        // DWORDs 0-15: directional light + camera + env
        lightFrameData[0]  = frameConstants_.light_dir.x;
        lightFrameData[1]  = frameConstants_.light_dir.y;
        lightFrameData[2]  = frameConstants_.light_dir.z;
        lightFrameData[3]  = frameConstants_.light_intensity;
        lightFrameData[4]  = frameConstants_.light_color.x;
        lightFrameData[5]  = frameConstants_.light_color.y;
        lightFrameData[6]  = frameConstants_.light_color.z;
        lightFrameData[7]  = 0.0f;
        lightFrameData[8]  = frameConstants_.camera_world_pos.x;
        lightFrameData[9]  = frameConstants_.camera_world_pos.y;
        lightFrameData[10] = frameConstants_.camera_world_pos.z;
        lightFrameData[11] = frameConstants_.time;
        lightFrameData[12] = 0.65f;  // envIntensity
        lightFrameData[13] = frameConstants_.shadow_bias;
        lightFrameData[14] = frameConstants_.shadow_ortho_size;
        lightFrameData[15] = frameConstants_.shadow_light_dist;
        // DWORDs 16-19: punctual light count + padding
        uint32_t plCount = punctualLightCount_;
        std::memcpy(&lightFrameData[16], &plCount, sizeof(uint32_t));
        lightFrameData[17] = 0.0f;
        lightFrameData[18] = 0.0f;
        lightFrameData[19] = 0.0f;
        // DWORDs 20+: punctual light data (12 floats per light)
        std::memcpy(&lightFrameData[20], punctualLights_,
                     punctualLightCount_ * sizeof(GpuPunctualLight));
        rhi()->buffer_upload(lightFrameCBV_, lightFrameData, sizeof(lightFrameData));
    }

    // Build sky CB — camera basis vectors + perspective params for ray reconstruction.
    // World-space up is always (0,1,0) for the fly-cam.
    {
        Vec3 fwd = camState.forward;
        Vec3 worldUp = {0.0f, 1.0f, 0.0f, 0.0f};

        // Guard against degenerate forward (looking straight up/down)
        float upDot = std::abs(fwd.x * worldUp.x + fwd.y * worldUp.y + fwd.z * worldUp.z);
        Vec3 refUp = (upDot > 0.99f)
            ? Vec3{0.0f, 0.0f, 1.0f, 0.0f}
            : worldUp;

        Vec3 right = vec3_normalize(vec3_cross(refUp, fwd));
        Vec3 up    = vec3_cross(fwd, right);

        // tan(fov/2) — camera always uses kPiDiv4 = 45 deg, so half-fov = 22.5 deg
        constexpr float kTanHalfFov45 = 0.41421356f; // tan(pi/8)
        const float aspectF = static_cast<float>(width_) / static_cast<float>(height_);

        skyCB_.camForward[0] = fwd.x;   skyCB_.camForward[1] = fwd.y;   skyCB_.camForward[2] = fwd.z;
        skyCB_.tanHalfFovY   = kTanHalfFov45;
        skyCB_.camRight[0]   = right.x; skyCB_.camRight[1]   = right.y; skyCB_.camRight[2]   = right.z;
        skyCB_.aspect        = aspectF;
        skyCB_.camUp[0]      = up.x;    skyCB_.camUp[1]      = up.y;    skyCB_.camUp[2]      = up.z;
        skyCB_._pad          = 0.0f;
        // Sun direction + intensity for sky disk/corona (matches directional light)
        skyCB_.sunDir[0]     = frameConstants_.light_dir.x;
        skyCB_.sunDir[1]     = frameConstants_.light_dir.y;
        skyCB_.sunDir[2]     = frameConstants_.light_dir.z;
        skyCB_.sunIntensity  = frameConstants_.light_intensity * 0.25f;  // scaled for corona
    }

    // Sync SkyAtmosphere component from scene (if present).
    // Looks for the first node with a SkyAtmosphereData component and copies its colors.
    if (scene_) {
        for (uint32_t i = 1; i < scene_->next_id; ++i) {
            SceneNode* n = scene_get_node(scene_, i);
            if (!n || !n->alive) continue;
            if (!component_has<SkyAtmosphereData>(&scene_->components, i)) continue;
            auto* sky = component_get<SkyAtmosphereData>(&scene_->components, i);
            if (!sky) break;
            skyColorsCB_.skyZenith[0]  = sky->sky_zenith[0];
            skyColorsCB_.skyZenith[1]  = sky->sky_zenith[1];
            skyColorsCB_.skyZenith[2]  = sky->sky_zenith[2];
            skyColorsCB_.skyHorizon[0] = sky->sky_horizon[0];
            skyColorsCB_.skyHorizon[1] = sky->sky_horizon[1];
            skyColorsCB_.skyHorizon[2] = sky->sky_horizon[2];
            skyColorsCB_.skyHaze[0]    = sky->sky_haze[0];
            skyColorsCB_.skyHaze[1]    = sky->sky_haze[1];
            skyColorsCB_.skyHaze[2]    = sky->sky_haze[2];
            skyColorsCB_.skyGround[0]  = sky->sky_ground[0];
            skyColorsCB_.skyGround[1]  = sky->sky_ground[1];
            skyColorsCB_.skyGround[2]  = sky->sky_ground[2];
            skyColorsCB_.fogDensity    = sky->fog_density;

            // Dirty-flag: regenerate IBL cubemaps when sky colors or sun direction change.
            // Compare against last-known values; skip the expensive reconvolution if unchanged.
            float curSunDir[3] = {
                frameConstants_.light_dir.x,
                frameConstants_.light_dir.y,
                frameConstants_.light_dir.z
            };
            bool skyDirty = false;
            for (int c = 0; c < 3; ++c) {
                if (std::abs(sky->sky_zenith[c]  - lastSkyZenith_[c])  > 1e-4f) skyDirty = true;
                if (std::abs(sky->sky_horizon[c] - lastSkyHorizon_[c]) > 1e-4f) skyDirty = true;
                if (std::abs(sky->sky_ground[c]  - lastSkyGround_[c])  > 1e-4f) skyDirty = true;
                if (std::abs(curSunDir[c]        - lastSunDir_[c])     > 1e-4f) skyDirty = true;
            }
            if (skyDirty) {
                for (int c = 0; c < 3; ++c) {
                    lastSkyZenith_[c]  = sky->sky_zenith[c];
                    lastSkyHorizon_[c] = sky->sky_horizon[c];
                    lastSkyGround_[c]  = sky->sky_ground[c];
                    lastSunDir_[c]     = curSunDir[c];
                }
                SkyCubemapParams skyParams{};
                skyParams.sky_zenith[0]  = sky->sky_zenith[0];
                skyParams.sky_zenith[1]  = sky->sky_zenith[1];
                skyParams.sky_zenith[2]  = sky->sky_zenith[2];
                skyParams.sky_horizon[0] = sky->sky_horizon[0];
                skyParams.sky_horizon[1] = sky->sky_horizon[1];
                skyParams.sky_horizon[2] = sky->sky_horizon[2];
                skyParams.sky_ground[0]  = sky->sky_ground[0];
                skyParams.sky_ground[1]  = sky->sky_ground[1];
                skyParams.sky_ground[2]  = sky->sky_ground[2];
                skyParams.sun_dir[0]     = curSunDir[0];
                skyParams.sun_dir[1]     = curSunDir[1];
                skyParams.sun_dir[2]     = curSunDir[2];
                skyParams.sun_intensity  = frameConstants_.light_intensity * 5.0f;
                DIAG("SkyAtmosphere changed — regenerating IBL cubemaps (GPU compute)...");
                SkyCubemapResult skyResult = generate_sky_cubemaps(skyParams);
                if (envCubemap_ != RHI_NULL)
                    rhi()->texture_destroy(envCubemap_);
                envCubemap_ = skyResult.env_cubemap;

                IblBakeResult bakeResult = iblBaker_.bake(envCubemap_, 256);
                if (bakeResult.irradiance_cube != RHI_NULL)
                    irradianceCube_ = bakeResult.irradiance_cube;
                if (bakeResult.prefiltered_cube != RHI_NULL)
                    prefilteredCube_ = bakeResult.prefiltered_cube;
            }
            break;
        }
    }

    // Build CSM cascade light-space VP matrices (PSSM split scheme, lambda=0.75).
    // 4 cascades rendered into 4096x4096 atlas (2x2 grid of 1024x1024).
    {
        Vec3 ld = cachedLight_.direction;
        Vec3 lightFwd = vec3_normalize({ld.x, ld.y, ld.z, 0.0f});
        Vec3 worldUp  = {0.0f, 1.0f, 0.0f, 0.0f};
        float upDotL  = std::abs(lightFwd.x*worldUp.x + lightFwd.y*worldUp.y + lightFwd.z*worldUp.z);
        Vec3 refUp2   = (upDotL > 0.99f) ? Vec3{1.0f, 0.0f, 0.0f, 0.0f} : worldUp;
        Vec3 lightRight = vec3_normalize(vec3_cross(refUp2, lightFwd));
        Vec3 lightUp    = vec3_cross(lightFwd, lightRight);

        // PSSM split distances (lambda=0.75 blending log/linear, near=0.5, far=100)
        constexpr float kNear = 0.5f;
        constexpr float kFar  = 100.0f;
        constexpr float kLambda = 0.75f;
        float splits[kCascadeCount + 1];
        splits[0] = kNear;
        for (uint32_t i = 1; i <= kCascadeCount; ++i) {
            float p = static_cast<float>(i) / kCascadeCount;
            float logSplit = kNear * std::pow(kFar / kNear, p);
            float linSplit = kNear + (kFar - kNear) * p;
            splits[i] = kLambda * logSplit + (1.0f - kLambda) * linSplit;
        }

        // Camera position for cascade centering
        Vec3 camPos = frameConstants_.camera_world_pos;

        for (uint32_t c = 0; c < kCascadeCount; ++c) {
            cascadeSplitDist_[c] = splits[c + 1];

            // Ortho size grows per cascade (tightest at cascade 0)
            float orthoSize = splits[c + 1] * 1.2f;  // 120% of far split for margin
            float shadowDist = orthoSize * 2.5f;      // light distance from center

            // Light eye positioned above camera looking along light direction
            Vec3 lightEye = {
                camPos.x - ld.x * shadowDist,
                camPos.y - ld.y * shadowDist,
                camPos.z - ld.z * shadowDist,
                0.0f
            };

            // Light view matrix
            Mat4 lightView{};
            lightView.m[0][0] = lightRight.x; lightView.m[1][0] = lightRight.y; lightView.m[2][0] = lightRight.z;
            lightView.m[0][1] = lightUp.x;    lightView.m[1][1] = lightUp.y;    lightView.m[2][1] = lightUp.z;
            lightView.m[0][2] = lightFwd.x;   lightView.m[1][2] = lightFwd.y;   lightView.m[2][2] = lightFwd.z;
            lightView.m[3][0] = -(lightRight.x*lightEye.x + lightRight.y*lightEye.y + lightRight.z*lightEye.z);
            lightView.m[3][1] = -(lightUp.x*lightEye.x    + lightUp.y*lightEye.y    + lightUp.z*lightEye.z);
            lightView.m[3][2] = -(lightFwd.x*lightEye.x   + lightFwd.y*lightEye.y   + lightFwd.z*lightEye.z);
            lightView.m[3][3] = 1.0f;

            // Ortho projection per cascade
            Mat4 lightProj{};
            lightProj.m[0][0] = 1.0f / orthoSize;
            lightProj.m[1][1] = 1.0f / orthoSize;
            lightProj.m[2][2] = 1.0f / (shadowDist * 2.0f);
            lightProj.m[3][2] = -0.1f / (shadowDist * 2.0f);
            lightProj.m[3][3] = 1.0f;

            cascadeVP_[c] = lightProj * lightView;
        }
        // Backward compat: cascade 0 = primary shadow matrix
        lightSpaceMat_ = cascadeVP_[0];

        // frameConstants shadow params: use cascade 0 dimensions
        frameConstants_.shadow_ortho_size = splits[1] * 1.2f;
        frameConstants_.shadow_light_dist = frameConstants_.shadow_ortho_size * 2.5f;
    }
}

void Renderer::render(bool readback) {
    UZ_PROFILE_ZONE("Renderer::render");
    RhiCmdList cmd = rhi()->cmd_begin();
    if (cmd == RHI_NULL) return;

    // -----------------------------------------------------------------------
    // Build the frame-local render graph (§RENDER-PIPELINE)
    // -----------------------------------------------------------------------
    renderGraph_.reset();

    // Import external (persistent) resources into the graph.
    RhiTexture backbuffer = rhi()->swapchain_backbuffer();
    auto rgBackbuffer = renderGraph_.import_texture("backbuffer", backbuffer);
    auto rgDepth      = renderGraph_.import_texture("depth",      rhiDepthBuf_);
    auto rgShadowMap  = renderGraph_.import_texture("shadow_map", shadowMap_);

    // --- Pass 0: CSM Shadow Depth (4 cascades into atlas quadrants) ---------
    if (shadowMap_ != RHI_NULL && rhiShadowPipeline_ != RHI_NULL) {
        renderGraph_.add_pass("shadow_depth", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.write(rgShadowMap);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, shadowMap_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_DEPTH_WRITE);

                rhi()->cmd_set_pipeline(cmd, rhiShadowPipeline_);
                rhi()->cmd_clear_depth_stencil(cmd, shadowMap_, 1.0f, 0);
                rhi()->cmd_set_render_targets(cmd, nullptr, 0, shadowMap_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

                // Atlas layout: 2x2 grid, each cascade = kCascadeSize x kCascadeSize
                // [0][1]
                // [2][3]
                static const uint32_t offX[4] = { 0, kCascadeSize, 0, kCascadeSize };
                static const uint32_t offY[4] = { 0, 0, kCascadeSize, kCascadeSize };

                for (uint32_t c = 0; c < kCascadeCount; ++c) {
                    rhi()->cmd_set_viewport(cmd,
                        static_cast<float>(offX[c]), static_cast<float>(offY[c]),
                        static_cast<float>(kCascadeSize), static_cast<float>(kCascadeSize),
                        0.0f, 1.0f);
                    rhi()->cmd_set_scissor(cmd, offX[c], offY[c], kCascadeSize, kCascadeSize);

                    // b3 = cascade VP matrix (16 DWORDs)
                    rhi()->cmd_set_root_constants(cmd, 3, &cascadeVP_[c], 16, 0);

                    for (auto* proxy : proxies_) {
                        auto* mp = static_cast<MeshProxy*>(proxy);
                        if (!mp->mesh || mp->mesh->vertex_buffer == RHI_NULL) continue;
                        rhi()->cmd_set_root_constants(cmd, 0, &mp->transform, 16, 0);
                        rhi()->cmd_set_vertex_buffer(cmd, 0, mp->mesh->vertex_buffer, 0, mp->mesh->vertex_stride);
                        rhi()->cmd_set_index_buffer(cmd, mp->mesh->index_buffer, 0, true);
                        for (const auto& sub : mp->mesh->submeshes) {
                            rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
                        }
                    }
                }

                rhi()->cmd_barrier_texture(cmd, shadowMap_,
                                           RHI_STATE_DEPTH_WRITE, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 1: Scene Draw (Forward or Deferred) ----------------------------
    auto rgHdr = renderGraph_.import_texture("hdr_rt", hdrRT_);

    // Lazy creation of deferred resources (triggered by setRenderPath from IPC thread).
    if (renderPath_ == RenderPath::Deferred && (gbufferPSO_ == RHI_NULL || gbufferRT_[0] == RHI_NULL)) {
        if (gbufferRT_[0] == RHI_NULL) {
            destroyPostProcessTargets();
            createPostProcessTargets();
        }
        if (gbufferPSO_ == RHI_NULL) createPipelineStates();
    }

    if (renderPath_ == RenderPath::Deferred && gbufferPSO_ != RHI_NULL && deferredLightPSO_ != RHI_NULL) {
        // =================================================================
        // DEFERRED PATH: Sky → G-Buffer write → Deferred lighting
        // =================================================================

        // Pass 1a: Clear HDR + depth + draw sky (into hdrRT_)
        renderGraph_.add_pass("sky_draw", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.write(rgHdr);
                builder.write(rgDepth);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

                rhi()->cmd_clear_render_target(cmd, hdrRT_, 0.0f, 0.0f, 0.0f, 1.0f);
                rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);
                rhi()->cmd_set_render_targets(cmd, &hdrRT_, 1, rhiDepthBuf_);

                if (rhiSkyPipeline_ != RHI_NULL) {
                    rhi()->cmd_set_pipeline(cmd, rhiSkyPipeline_);
                    rhi()->cmd_set_root_constants(cmd, 0, &skyColorsCB_, 16, 0);
                    rhi()->cmd_set_root_constants(cmd, 3, &skyCB_, 16, 0);
                    rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                    rhi()->cmd_draw(cmd, 3, 1, 0, 0);
                }

                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });

        // Pass 1b: G-Buffer write (opaque geometry → 4 MRT + depth)
        renderGraph_.add_pass("gbuffer_write", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.write(rgDepth);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                // First frame: G-Buffer textures are in COMMON state (just created).
                // Subsequent frames: they're in SHADER_READ from the previous frame's end.
                const RhiResourceState gbBefore = gbufferReady_
                    ? RHI_STATE_SHADER_READ : RHI_STATE_COMMON;
                for (uint32_t i = 0; i < kGBufferCount; ++i)
                    rhi()->cmd_barrier_texture(cmd, gbufferRT_[i],
                                               gbBefore, RHI_STATE_RENDER_TARGET);
                gbufferReady_ = true;

                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

                for (uint32_t i = 0; i < kGBufferCount; ++i)
                    rhi()->cmd_clear_render_target(cmd, gbufferRT_[i], 0.0f, 0.0f, 0.0f, 0.0f);

                rhi()->cmd_set_render_targets(cmd, gbufferRT_, kGBufferCount, rhiDepthBuf_);
                rhi()->cmd_set_pipeline(cmd, gbufferPSO_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

                if (lightFrameCBV_ != RHI_NULL)
                    rhi()->cmd_set_constant_buffer(cmd, 2, lightFrameCBV_);

                for (auto* proxy : proxies_) {
                    proxy->execute(cmd, frameConstants_);
                }

                for (uint32_t i = 0; i < kGBufferCount; ++i)
                    rhi()->cmd_barrier_texture(cmd, gbufferRT_[i],
                                               RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
                rhi()->cmd_barrier_texture(cmd, rhiDepthBuf_,
                                           RHI_STATE_DEPTH_WRITE, RHI_STATE_SHADER_READ);
            });

        // Pass 1c: Deferred Lighting (fullscreen — reads G-Buffer + depth → hdrRT_)
        renderGraph_.add_pass("deferred_lighting", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.write(rgHdr);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_pipeline(cmd, deferredLightPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
                rhi()->cmd_set_render_targets(cmd, &hdrRT_, 1, RHI_NULL);

                // b0: invViewProj (16 DWORDs)
                Mat4 invVP = mat4_inverse(viewProj_);
                rhi()->cmd_set_root_constants(cmd, 0, &invVP, 16, 0);

                // b1: punctual light CBV (same buffer as forward path LightFrameCBV)
                if (lightFrameCBV_ != RHI_NULL) {
                    rhi()->cmd_set_constant_buffer(cmd, 1, lightFrameCBV_);
                }

                // b3: light params (16 DWORDs) — matches LightParamsCB in deferred_lighting.ps.hlsl
                float lightParams[16] = {
                    frameConstants_.light_dir.x, frameConstants_.light_dir.y, frameConstants_.light_dir.z,
                    frameConstants_.light_intensity,
                    frameConstants_.light_color.x, frameConstants_.light_color.y, frameConstants_.light_color.z,
                    0.0f,
                    frameConstants_.camera_world_pos.x, frameConstants_.camera_world_pos.y, frameConstants_.camera_world_pos.z,
                    0.65f,  // envIntensity (matches forward path LightFrameCB upload)
                    frameConstants_.shadow_bias,
                    frameConstants_.shadow_ortho_size,
                    frameConstants_.shadow_light_dist,
                    0.0f
                };
                rhi()->cmd_set_root_constants(cmd, 3, lightParams, 16, 0);

                // t0-t4: G-Buffer + depth
                for (uint32_t i = 0; i < kGBufferCount; ++i)
                    rhi()->cmd_set_texture(cmd, i, gbufferRT_[i]);
                rhi()->cmd_set_texture(cmd, 4, rhiDepthBuf_);

                // t5-t8: shadow, BRDF LUT, irradiance, prefiltered
                rhi()->cmd_set_texture(cmd, 5, (shadowMap_ != RHI_NULL)       ? shadowMap_       : whiteTex_);
                rhi()->cmd_set_texture(cmd, 6, (brdfLut_ != RHI_NULL)         ? brdfLut_         : whiteTex_);
                rhi()->cmd_set_texture(cmd, 7, (irradianceCube_ != RHI_NULL)  ? irradianceCube_  : whiteTex_);
                rhi()->cmd_set_texture(cmd, 8, (prefilteredCube_ != RHI_NULL) ? prefilteredCube_ : whiteTex_);

                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });

    } else {
        // =================================================================
        // FORWARD PATH: Sky + PBR meshes in one pass → hdrRT_
        // =================================================================
        renderGraph_.add_pass("scene_hdr", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.write(rgHdr);
                builder.write(rgDepth);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_viewport(cmd, 0.0f, 0.0f,
                                       static_cast<float>(width_),
                                       static_cast<float>(height_),
                                       0.0f, 1.0f);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

                rhi()->cmd_clear_render_target(cmd, hdrRT_, 0.0f, 0.0f, 0.0f, 1.0f);
                rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);
                rhi()->cmd_set_render_targets(cmd, &hdrRT_, 1, rhiDepthBuf_);

                if (rhiSkyPipeline_ != RHI_NULL) {
                    rhi()->cmd_set_pipeline(cmd, rhiSkyPipeline_);
                    rhi()->cmd_set_root_constants(cmd, 0, &skyColorsCB_, 16, 0);
                    rhi()->cmd_set_root_constants(cmd, 3, &skyCB_, 16, 0);
                    rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                    rhi()->cmd_draw(cmd, 3, 1, 0, 0);
                }

                if (lightFrameCBV_ != RHI_NULL)
                    rhi()->cmd_set_constant_buffer(cmd, 2, lightFrameCBV_);

                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                meshRenderer_.draw(cmd, viewProj_, lightFrameCBV_);

                if (rhiPbrPipeline_ != RHI_NULL) {
                    rhi()->cmd_set_pipeline(cmd, rhiPbrPipeline_);
                    for (auto* proxy : proxies_) {
                        proxy->execute(cmd, frameConstants_);
                    }
                }

                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
                rhi()->cmd_barrier_texture(cmd, rhiDepthBuf_,
                                           RHI_STATE_DEPTH_WRITE, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 1b: Volumetric Clouds ------------------------------------------
    // Alpha-blend procedural clouds over the sky. Runs after all geometry so
    // the depth buffer is fully written — cloud shader skips solid-geometry pixels.
    if (cloudsEnabled_ && cloudsPSO_ != RHI_NULL && hdrRT_ != RHI_NULL) {
        renderGraph_.add_pass("clouds", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.write(rgHdr);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                rhi()->cmd_set_pipeline(cmd, cloudsPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
                rhi()->cmd_set_render_targets(cmd, &hdrRT_, 1, RHI_NULL);

                // b0: cloud params (16 root constants)
                float cp[16] = {
                    cloudCoverage_, cloudDensity_, cloudBase_, cloudTop_,
                    cloudWindSpeed_, cloudWindDir_[0], cloudWindDir_[1], cloudAbsorption_,
                    cloudAmbient_, 0.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 0.0f
                };
                rhi()->cmd_set_root_constants(cmd, 0, cp, 16, 0);

                // b2: LightFrameCB (sun dir/color, camera pos, time)
                if (lightFrameCBV_ != RHI_NULL)
                    rhi()->cmd_set_constant_buffer(cmd, 2, lightFrameCBV_);

                // b3: invViewProj — unjittered for stable cloud positions
                Mat4 invVP = mat4_inverse(viewProjUnjittered_);
                rhi()->cmd_set_root_constants(cmd, 3, &invVP, 16, 0);

                // t0: depth buffer for geometry occlusion masking
                rhi()->cmd_set_texture(cmd, 0, rhiDepthBuf_);

                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, hdrRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 1c: Velocity Buffer --------------------------------------------
    // Depth-based per-pixel motion vectors for TAA, GTAO, SSR.
    auto rgVelocity = renderGraph_.import_texture("velocity_rt", velocityRT_);
    if (velocityPSO_ != RHI_NULL && velocityRT_ != RHI_NULL && taaEnabled_) {
        renderGraph_.add_pass("velocity", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.write(rgVelocity);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, velocityRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                rhi()->cmd_set_pipeline(cmd, velocityPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
                rhi()->cmd_set_render_targets(cmd, &velocityRT_, 1, RHI_NULL);

                // b0: invViewProj (16 DWORDs) — unjittered to match prevViewProj
                Mat4 invVP = mat4_inverse(viewProjUnjittered_);
                rhi()->cmd_set_root_constants(cmd, 0, &invVP, 16, 0);
                // b3: prevViewProj (16 DWORDs)
                rhi()->cmd_set_root_constants(cmd, 3, &prevViewProj_, 16, 0);
                // t0: depth buffer
                rhi()->cmd_set_texture(cmd, 0, rhiDepthBuf_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, velocityRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 1c: TAA Resolve ------------------------------------------------
    // Reproject history, YCoCg neighborhood clamp, temporal blend.
    const uint32_t taaWriteIdx = taaWriteIdx_;
    const uint32_t taaReadIdx  = 1 - taaWriteIdx;
    auto rgTaaWrite = renderGraph_.import_texture("taa_write", taaHistory_[taaWriteIdx]);
    if (taaPSO_ != RHI_NULL && taaEnabled_ &&
        taaHistory_[0] != RHI_NULL && taaHistory_[1] != RHI_NULL) {
        renderGraph_.add_pass("taa_resolve", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgHdr);
                builder.read(rgVelocity);
                builder.write(rgTaaWrite);
                builder.force_run();
            },
            [this, taaWriteIdx, taaReadIdx](RhiCmdList cmd) {
                RhiTexture dst = taaHistory_[taaWriteIdx];
                RhiTexture src = taaHistory_[taaReadIdx];

                rhi()->cmd_barrier_texture(cmd, dst,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_pipeline(cmd, taaPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
                rhi()->cmd_set_render_targets(cmd, &dst, 1, RHI_NULL);

                // b0: TAAParams (8 floats)
                float taaParams[8] = {
                    static_cast<float>(width_), static_cast<float>(height_),
                    1.0f / static_cast<float>(width_), 1.0f / static_cast<float>(height_),
                    taaJitterX_, taaJitterY_,
                    0.04f,  // blendFactor (UE5 default: 4% current frame)
                    0.0f    // _pad
                };
                rhi()->cmd_set_root_constants(cmd, 0, taaParams, 8, 0);

                // t0: current HDR (jittered), t1: history, t2: velocity, t3: depth
                // t4: SSAO (prev frame, half-res) — baked into TAA so AO is
                //     temporally accumulated. Frame 0: whiteTex (AO=1, safe).
                rhi()->cmd_set_texture(cmd, 0, hdrRT_);
                rhi()->cmd_set_texture(cmd, 1, src);
                rhi()->cmd_set_texture(cmd, 2, velocityRT_);
                rhi()->cmd_set_texture(cmd, 3, rhiDepthBuf_);
                rhi()->cmd_set_texture(cmd, 4,
                    (ssaoBlurRT_ != RHI_NULL && frameNumber_ > 0)
                        ? ssaoBlurRT_ : whiteTex_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, dst,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
        // Advance ping-pong for next frame
        taaWriteIdx_ = 1 - taaWriteIdx_;
    }

    // --- Pass 2: GTAO (Ground Truth Ambient Occlusion + Contact Shadows) ------
    auto rgSsao = renderGraph_.import_texture("ssao_rt", ssaoRT_);
    const bool useGTAO = true;
    const RhiPipeline aoPSO = (gtaoPSO_ != RHI_NULL) ? gtaoPSO_ : ssaoPSO_;
    if (aoPSO != RHI_NULL && ssaoRT_ != RHI_NULL) {
        renderGraph_.add_pass("ssao", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.write(rgSsao);
                builder.force_run();
            },
            [this, aoPSO, useGTAO](RhiCmdList cmd) {
                uint32_t hw = width_ / 2, hh = height_ / 2;
                rhi()->cmd_barrier_texture(cmd, ssaoRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                rhi()->cmd_set_pipeline(cmd, aoPSO);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)hw, (float)hh, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, hw, hh);
                rhi()->cmd_set_render_targets(cmd, &ssaoRT_, 1, RHI_NULL);

                // b0: projMatrix (16 DWORDs)
                float ssaoParams[16];
                std::memcpy(ssaoParams, &projMatrix_, 64);
                rhi()->cmd_set_root_constants(cmd, 0, ssaoParams, 16, 0);

                // b1: AO params — different layout for SSAO vs GTAO
                if (ssaoB1CBV_ != RHI_NULL) {
                    if (useGTAO) {
                        // GTAO cbuffer: screenSize, near, far, aoRadius, aoIntensity,
                        //               aoFalloff, frameCount, lightDirView(3), contactShadowLen
                        Vec3 ld = frameConstants_.light_dir;
                        float ldvX = viewMatrix_.m[0][0]*ld.x + viewMatrix_.m[1][0]*ld.y + viewMatrix_.m[2][0]*ld.z;
                        float ldvY = viewMatrix_.m[0][1]*ld.x + viewMatrix_.m[1][1]*ld.y + viewMatrix_.m[2][1]*ld.z;
                        float ldvZ = viewMatrix_.m[0][2]*ld.x + viewMatrix_.m[1][2]*ld.y + viewMatrix_.m[2][2]*ld.z;
                        float ldvLen = std::sqrt(ldvX*ldvX + ldvY*ldvY + ldvZ*ldvZ);
                        if (ldvLen > 0.001f) { ldvX/=ldvLen; ldvY/=ldvLen; ldvZ/=ldvLen; }

                        float aoB1[12] = {
                            static_cast<float>(width_), static_cast<float>(height_),
                            0.1f, 500.0f,
                            aoRadius_, aoIntensity_, 2.0f,
                            static_cast<float>(frameNumber_),
                            ldvX, ldvY, ldvZ,
                            contactShadowsEnabled_ ? contactShadowLength_ : 0.0f
                        };
                        rhi()->buffer_upload(ssaoB1CBV_, aoB1, sizeof(aoB1));
                    } else {
                        // Old SSAO cbuffer: screenSize, near, far, aoRadius, aoIntensity, aoBias, pad
                        float aoB1[8] = {
                            static_cast<float>(width_), static_cast<float>(height_),
                            0.1f, 500.0f,
                            aoRadius_, aoIntensity_, 0.025f, 0.0f
                        };
                        rhi()->buffer_upload(ssaoB1CBV_, aoB1, sizeof(aoB1));
                    }
                    rhi()->cmd_set_constant_buffer(cmd, 1, ssaoB1CBV_);
                }

                rhi()->cmd_set_texture(cmd, 0, rhiDepthBuf_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, ssaoRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 3: SSAO Blur ---------------------------------------------------
    auto rgSsaoBlur = renderGraph_.import_texture("ssao_blur_rt", ssaoBlurRT_);
    if (ssaoBlurPSO_ != RHI_NULL && ssaoBlurRT_ != RHI_NULL) {
        renderGraph_.add_pass("ssao_blur", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgSsao);
                builder.read(rgDepth);
                builder.write(rgSsaoBlur);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                uint32_t hw = width_ / 2, hh = height_ / 2;
                rhi()->cmd_barrier_texture(cmd, ssaoBlurRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                rhi()->cmd_set_pipeline(cmd, ssaoBlurPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)hw, (float)hh, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, hw, hh);
                rhi()->cmd_set_render_targets(cmd, &ssaoBlurRT_, 1, RHI_NULL);

                float blurParams[4] = { 1.0f / hw, 1.0f / hh, 0.05f, 0.0f };
                rhi()->cmd_set_root_constants(cmd, 0, blurParams, 4, 0);
                rhi()->cmd_set_texture(cmd, 0, ssaoRT_);
                rhi()->cmd_set_texture(cmd, 1, rhiDepthBuf_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, ssaoBlurRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
    }

    // --- Determine composite/bloom color source (TAA-resolved or raw HDR) ---
    // taaWriteIdx captured the WRITE target index before the flip, so resolved is there.
    const RhiTexture compositeColorSrc = (taaEnabled_ && taaHistory_[taaWriteIdx] != RHI_NULL)
        ? taaHistory_[taaWriteIdx]
        : hdrRT_;

    // --- Pass 3.5: Hi-Z Mip Chain Generation ---------------------------------
    if (hizPSO_ != RHI_NULL && hizMips_[0] != RHI_NULL) {
        renderGraph_.add_pass("hiz_generate", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_set_pipeline(cmd, hizPSO_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

                RhiTexture srcTex = rhiDepthBuf_;
                uint32_t srcW = width_, srcH = height_;

                for (uint32_t i = 0; i < kHiZMips; ++i) {
                    uint32_t dstW = std::max(srcW / 2, 1u);
                    uint32_t dstH = std::max(srcH / 2, 1u);

                    rhi()->cmd_barrier_texture(cmd, hizMips_[i],
                                               RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                    rhi()->cmd_set_viewport(cmd, 0, 0, (float)dstW, (float)dstH, 0, 1);
                    rhi()->cmd_set_scissor(cmd, 0, 0, dstW, dstH);
                    rhi()->cmd_set_render_targets(cmd, &hizMips_[i], 1, RHI_NULL);

                    float params[4] = { 1.0f / srcW, 1.0f / srcH, 0.0f, 0.0f };
                    rhi()->cmd_set_root_constants(cmd, 0, params, 4, 0);
                    rhi()->cmd_set_texture(cmd, 0, srcTex);
                    rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                    rhi()->cmd_barrier_texture(cmd, hizMips_[i],
                                               RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);

                    srcTex = hizMips_[i];
                    srcW = dstW;
                    srcH = dstH;
                }
            });
    }

    // --- Pass 3.6: Screen-Space Reflections ----------------------------------
    if (ssrEnabled_ && ssrPSO_ != RHI_NULL && ssrRT_ != RHI_NULL
        && ssrCBV_ != RHI_NULL && renderPath_ == RenderPath::Deferred) {
        renderGraph_.add_pass("ssr", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgDepth);
                builder.read(rgHdr);
                builder.force_run();
            },
            [this, compositeColorSrc](RhiCmdList cmd) {
                rhi()->cmd_barrier_texture(cmd, ssrRT_,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_pipeline(cmd, ssrPSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
                rhi()->cmd_set_render_targets(cmd, &ssrRT_, 1, RHI_NULL);

                // Root constants: invProj-related params, screen size, max roughness
                float ssrParams[16] = {};
                ssrParams[0] = 1.0f / width_;      // texelWidth
                ssrParams[1] = 1.0f / height_;     // texelHeight
                ssrParams[2] = (float)width_;       // screenWidth
                ssrParams[3] = (float)height_;      // screenHeight
                ssrParams[4] = ssrMaxRoughness_;    // maxRoughness
                ssrParams[5] = 0.1f;                // nearPlane
                ssrParams[6] = 1000.0f;             // farPlane
                ssrParams[7] = (float)frameNumber_; // frameIndex for temporal jitter
                // ssrParams[8..15] reserved for invProj row data
                rhi()->cmd_set_root_constants(cmd, 0, ssrParams, 16, 0);

                rhi()->cmd_set_texture(cmd, 0, rhiDepthBuf_);      // depth
                rhi()->cmd_set_texture(cmd, 1, gbufferRT_[1]);     // G-Buffer normals
                rhi()->cmd_set_texture(cmd, 2, compositeColorSrc); // HDR scene color
                rhi()->cmd_set_texture(cmd, 3, gbufferRT_[2]);     // metallic/roughness
                rhi()->cmd_set_texture(cmd, 4, hizMips_[0]);       // Hi-Z mip 0

                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, ssrRT_,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
    }

    // --- Pass 4: Bloom Downsample Chain --------------------------------------
    // When TAA is active, bloom reads the TAA-resolved output for temporal stability.
    const RhiTexture bloomSrcTex = compositeColorSrc;
    if (bloomDownPSO_ != RHI_NULL && bloomMips_[0] != RHI_NULL) {
        renderGraph_.add_pass("bloom_down", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgHdr);
                if (taaEnabled_) builder.read(rgTaaWrite);
                builder.force_run();
            },
            [this, bloomSrcTex](RhiCmdList cmd) {
                rhi()->cmd_set_pipeline(cmd, bloomDownPSO_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

                uint32_t srcW = width_, srcH = height_;
                RhiTexture srcTex = bloomSrcTex;

                for (uint32_t i = 0; i < kBloomMips; ++i) {
                    uint32_t dstW = std::max(srcW / 2, 1u);
                    uint32_t dstH = std::max(srcH / 2, 1u);

                    rhi()->cmd_barrier_texture(cmd, bloomMips_[i],
                                               RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                    rhi()->cmd_set_viewport(cmd, 0, 0, (float)dstW, (float)dstH, 0, 1);
                    rhi()->cmd_set_scissor(cmd, 0, 0, dstW, dstH);
                    rhi()->cmd_set_render_targets(cmd, &bloomMips_[i], 1, RHI_NULL);

                    float params[4] = {
                        1.0f / srcW, 1.0f / srcH,
                        1.0f,   // bloom threshold
                        (i == 0) ? 1.0f : 0.0f  // isFirstMip
                    };
                    rhi()->cmd_set_root_constants(cmd, 0, params, 4, 0);
                    rhi()->cmd_set_texture(cmd, 0, srcTex);
                    rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                    rhi()->cmd_barrier_texture(cmd, bloomMips_[i],
                                               RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
                    srcTex = bloomMips_[i];
                    srcW = dstW;
                    srcH = dstH;
                }
            });
    }

    // --- Pass 5: Bloom Upsample Chain ----------------------------------------
    if (bloomUpPSO_ != RHI_NULL && bloomMips_[0] != RHI_NULL && kBloomMips >= 2) {
        renderGraph_.add_pass("bloom_up", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                rhi()->cmd_set_pipeline(cmd, bloomUpPSO_);
                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

                // Upsample from smallest mip back to mip 0
                for (int i = static_cast<int>(kBloomMips) - 2; i >= 0; --i) {
                    RhiTexture srcTex = bloomMips_[i + 1];
                    RhiTexture dstTex = bloomMips_[i];

                    // Calculate dst dimensions (same as what createPostProcessTargets made)
                    uint32_t dstW = width_ / 2, dstH = height_ / 2;
                    for (int j = 0; j < i; ++j) { dstW /= 2; dstH /= 2; }
                    dstW = std::max(dstW, 1u);
                    dstH = std::max(dstH, 1u);

                    // src dimensions
                    uint32_t srcW = std::max(dstW / 2, 1u);
                    uint32_t srcH = std::max(dstH / 2, 1u);

                    rhi()->cmd_barrier_texture(cmd, dstTex,
                                               RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);
                    rhi()->cmd_set_viewport(cmd, 0, 0, (float)dstW, (float)dstH, 0, 1);
                    rhi()->cmd_set_scissor(cmd, 0, 0, dstW, dstH);
                    rhi()->cmd_set_render_targets(cmd, &dstTex, 1, RHI_NULL);

                    float params[4] = { 1.0f / srcW, 1.0f / srcH, 0.7f, 0.0f };
                    rhi()->cmd_set_root_constants(cmd, 0, params, 4, 0);
                    rhi()->cmd_set_texture(cmd, 0, srcTex);
                    rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                    rhi()->cmd_barrier_texture(cmd, dstTex,
                                               RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
                }
            });
    }

    // --- Pass 5.5: Auto Exposure (meter scene luminance → 1x1 R16_FLOAT) -----
    const uint32_t expWriteIdx = exposureWriteIdx_;
    const uint32_t expReadIdx  = 1 - expWriteIdx;
    if (autoExposurePSO_ != RHI_NULL && autoExposureEnabled_
        && exposureRT_[0] != RHI_NULL && exposureCBV_ != RHI_NULL) {
        renderGraph_.add_pass("auto_exposure", RgPassType::Graphics,
            [&](RgPassBuilder& builder) {
                builder.read(rgHdr);
                builder.force_run();
            },
            [this, compositeColorSrc, expWriteIdx, expReadIdx](RhiCmdList cmd) {
                RhiTexture dst = exposureRT_[expWriteIdx];
                RhiTexture prev = exposureRT_[expReadIdx];

                rhi()->cmd_barrier_texture(cmd, dst,
                                           RHI_STATE_SHADER_READ, RHI_STATE_RENDER_TARGET);

                rhi()->cmd_set_pipeline(cmd, autoExposurePSO_);
                rhi()->cmd_set_viewport(cmd, 0, 0, 1.0f, 1.0f, 0, 1);
                rhi()->cmd_set_scissor(cmd, 0, 0, 1, 1);
                rhi()->cmd_set_render_targets(cmd, &dst, 1, RHI_NULL);

                // Upload ExposureParams CBV (b0 via b1 slot — we use root CBV at slot 1)
                float expParams[8] = {
                    -8.0f, 4.0f,     // minLogLum, maxLogLum
                    3.0f, 1.0f,      // adaptSpeedUp, adaptSpeedDown
                    lastDeltaTime_, 0.0f,  // deltaTime, targetExposure (0=auto)
                    0.1f, 0.98f      // lowPercentile, highPercentile
                };
                rhi()->cmd_set_root_constants(cmd, 0, expParams, 8, 0);

                rhi()->cmd_set_texture(cmd, 0, compositeColorSrc);  // HDR scene
                rhi()->cmd_set_texture(cmd, 1, prev);                // previous exposure

                rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
                rhi()->cmd_draw(cmd, 3, 1, 0, 0);

                rhi()->cmd_barrier_texture(cmd, dst,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_SHADER_READ);
            });
        exposureWriteIdx_ = 1 - exposureWriteIdx_;
    }

    // --- Pass 6: Composite (HDR + SSAO + Bloom → Tonemap → Backbuffer) -------
    renderGraph_.add_pass("composite", RgPassType::Graphics,
        [&](RgPassBuilder& builder) {
            builder.read(rgHdr);
            if (taaEnabled_) builder.read(rgTaaWrite);
            builder.read(rgSsaoBlur);
            builder.write(rgBackbuffer);
            builder.force_run();
        },
        [this, compositeColorSrc, expWriteIdx](RhiCmdList cmd) {
            RhiTexture bb = rhi()->swapchain_backbuffer();
            rhi()->cmd_barrier_texture(cmd, bb,
                                       RHI_STATE_PRESENT, RHI_STATE_RENDER_TARGET);
            rhi()->cmd_clear_render_target(cmd, bb, 0, 0, 0, 1);

            rhi()->cmd_set_pipeline(cmd, compositePSO_);
            rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
            rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
            rhi()->cmd_set_render_targets(cmd, &bb, 1, RHI_NULL);

            // When auto exposure is off, exposure constant = manualExposure, exposureTex = white (1.0).
            // When auto exposure is on, exposure constant = 1.0, exposureTex = auto-adapted value.
            float exposureVal = autoExposureEnabled_ ? 1.0f : manualExposure_;
            float compParams[16] = {};
            compParams[0]  = exposureVal;           // exposure
            compParams[1]  = 0.04f;                 // bloomIntensity
            compParams[2]  = 2.2f;                  // gamma
            compParams[3]  = 0.3f;                  // sharpenStrength
            compParams[4]  = colorTemperature_;      // temperature
            compParams[5]  = colorTint_;             // tint
            compParams[6]  = colorSaturation_;       // saturation
            compParams[7]  = colorContrast_;         // contrast
            // Cinematic post (slots 8-11)
            compParams[8]  = chromaticAberration_;
            compParams[9]  = vignetteIntensity_;
            compParams[10] = vignetteFalloff_;
            compParams[11] = filmGrainStrength_;
            // DoF params (slots 12-15)
            compParams[12] = dofFocusDistance_;
            compParams[13] = dofAperture_;
            compParams[14] = dofFocalLength_;
            compParams[15] = 0.0f;
            rhi()->cmd_set_root_constants(cmd, 0, compParams, 16, 0);

            rhi()->cmd_set_texture(cmd, 0, compositeColorSrc);
            // When TAA is active, AO is already baked into the TAA signal (t4),
            // so bind white (AO=1) to avoid double-applying occlusion.
            rhi()->cmd_set_texture(cmd, 1,
                (taaEnabled_ || ssaoBlurRT_ == RHI_NULL) ? whiteTex_ : ssaoBlurRT_);
            rhi()->cmd_set_texture(cmd, 2, (bloomMips_[0] != RHI_NULL) ? bloomMips_[0] : blackTex_);
            // t3: Auto exposure adapted value (1x1 R16F), or white when disabled
            rhi()->cmd_set_texture(cmd, 3,
                (autoExposureEnabled_ && exposureRT_[expWriteIdx] != RHI_NULL)
                    ? exposureRT_[expWriteIdx] : whiteTex_);
            // t4: SSR reflections (RGBA16F), or black when disabled/unavailable
            rhi()->cmd_set_texture(cmd, 4,
                (ssrEnabled_ && ssrRT_ != RHI_NULL) ? ssrRT_ : blackTex_);

            rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);
            rhi()->cmd_draw(cmd, 3, 1, 0, 0);
        });

    // --- Pass 7: Editor Overlays (on backbuffer, after tonemap) ---------------
    renderGraph_.add_pass("editor_overlays", RgPassType::Graphics,
        [&](RgPassBuilder& builder) {
            builder.read(rgBackbuffer);   // run after composite
            builder.write(rgBackbuffer);
            // NOTE: depth is reused but NOT declared to avoid cycle with SSAO.
            builder.force_run();
        },
        [this](RhiCmdList cmd) {
            RhiTexture bb = rhi()->swapchain_backbuffer();

            // Restore depth for stencil outline + editor depth testing
            rhi()->cmd_barrier_texture(cmd, rhiDepthBuf_,
                                       RHI_STATE_SHADER_READ, RHI_STATE_DEPTH_WRITE);

            rhi()->cmd_set_viewport(cmd, 0, 0, (float)width_, (float)height_, 0, 1);
            rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);
            rhi()->cmd_set_render_targets(cmd, &bb, 1, rhiDepthBuf_);

            // Selection outline
            {
                uint32_t selNode = sceneParams_
                    ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;

                if (selNode != 0 && rhiStencilMarkPSO_ != RHI_NULL && rhiOutlinePSO_ != RHI_NULL) {
                    MeshProxy* selProxy = nullptr;
                    for (auto* proxy : proxies_) {
                        auto* mp = static_cast<MeshProxy*>(proxy);
                        if (mp->node_id == selNode) { selProxy = mp; break; }
                    }

                    if (selProxy && selProxy->mesh && selProxy->mesh->vertex_buffer != RHI_NULL) {
                        rhi()->cmd_set_pipeline(cmd, rhiStencilMarkPSO_);
                        rhi()->cmd_set_stencil_ref(cmd, 1);
                        rhi()->cmd_set_vertex_buffer(cmd, 0, selProxy->mesh->vertex_buffer, 0, selProxy->mesh->vertex_stride);
                        rhi()->cmd_set_index_buffer(cmd, selProxy->mesh->index_buffer, 0, true);
                        rhi()->cmd_set_root_constants(cmd, 0, &selProxy->transform, 16, 0);
                        rhi()->cmd_set_root_constants(cmd, 3, &frameConstants_.view_proj, 16, 0);
                        for (const auto& sub : selProxy->mesh->submeshes)
                            rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);

                        rhi()->cmd_set_pipeline(cmd, rhiOutlinePSO_);
                        rhi()->cmd_set_stencil_ref(cmd, 1);
                        rhi()->cmd_set_root_constants(cmd, 0, &selProxy->transform, 16, 0);
                        if (outlineCBV_ != RHI_NULL) {
                            float outlineData[12] = {
                                1.0f, 0.55f, 0.0f, 1.0f,
                                0.08f, 0.0f, 0.0f, 0.0f,
                                0.0f, 0.0f, 0.0f, 0.0f
                            };
                            rhi()->buffer_upload(outlineCBV_, outlineData, sizeof(outlineData));
                            rhi()->cmd_set_constant_buffer(cmd, 1, outlineCBV_);
                        }
                        rhi()->cmd_set_root_constants(cmd, 3, &frameConstants_.view_proj, 16, 0);
                        for (const auto& sub : selProxy->mesh->submeshes)
                            rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
                    }
                }
            }

            // Editor gizmos + grid
            {
                uint32_t selNode = sceneParams_
                    ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;
                int gizMode = sceneParams_
                    ? sceneParams_->gizmoMode.load(std::memory_order_relaxed) : 0;
                int gizAxis = sceneParams_
                    ? sceneParams_->gizmoActiveAxis.load(std::memory_order_relaxed) : -1;
                if (gizAxis < 0 && sceneParams_)
                    gizAxis = sceneParams_->gizmoHoverAxis.load(std::memory_order_relaxed);

                Float3 camPos{0.0f, 0.0f, 0.0f};
                if (sceneParams_)
                    camPos = {sceneParams_->camPosX, sceneParams_->camPosY, sceneParams_->camPosZ};

                bool usePivot = false;
                Float3 pivotPos{0.0f, 0.0f, 0.0f};
                if (sceneParams_) {
                    usePivot = sceneParams_->useGizmoPivot.load(std::memory_order_relaxed);
                    if (usePivot) {
                        std::lock_guard<std::mutex> lk(sceneParams_->gizmoPivotMutex);
                        pivotPos = {sceneParams_->gizmoPivotX, sceneParams_->gizmoPivotY, sceneParams_->gizmoPivotZ};
                    }
                }

                editor_.draw(cmd, viewProj_, camPos, selNode, gizMode, gizAxis, usePivot, pivotPos);
            }
        });

    // --- Pass 2: Readback --------------------------------------------------
    // Copies the rendered backbuffer to a CPU-readable staging buffer.
    if (readback) {
        renderGraph_.add_pass("readback", RgPassType::Copy,
            [&](RgPassBuilder& builder) {
                builder.read(rgBackbuffer);
                builder.force_run();
            },
            [this](RhiCmdList cmd) {
                RhiTexture bb = rhi()->swapchain_backbuffer();

                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_COPY_SRC);

                const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
                RhiTextureFootprint footprint{};
                footprint.width     = width_;
                footprint.height    = height_;
                footprint.depth     = 1;
                footprint.row_pitch = alignedPitch;
                footprint.offset    = 0;
                footprint.format    = RHI_FORMAT_RGBA8_UNORM;

                rhi()->cmd_copy_texture_to_buffer(cmd, bb,
                                                  rhiReadbackBufs_[frameIndex_],
                                                  &footprint);

                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_COPY_SRC, RHI_STATE_PRESENT);
            });
    } else {
        // No readback — still need the present barrier.
        renderGraph_.add_pass("present_barrier", RgPassType::Copy,
            [&](RgPassBuilder& builder) {
                builder.read(rgBackbuffer);
                builder.force_run();
            },
            [](RhiCmdList cmd) {
                RhiTexture bb = rhi()->swapchain_backbuffer();
                rhi()->cmd_barrier_texture(cmd, bb,
                                           RHI_STATE_RENDER_TARGET, RHI_STATE_PRESENT);
            });
    }

    // -----------------------------------------------------------------------
    // Compile and execute the render graph
    // -----------------------------------------------------------------------
    if (!renderGraph_.compile()) {
        // Graph has a cycle or is invalid — skip execution, still present.
        rhi()->cmd_end(cmd);
        rhi()->cmd_submit(cmd);
        rhi()->swapchain_present(0);
        return;
    }
    renderGraph_.execute(cmd);

    rhi()->cmd_end(cmd);
    rhi()->cmd_submit(cmd);
    rhi()->swapchain_present(0);

    if (readback) {
        performReadback();
    }
}

FrameData Renderer::getLatestFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return latestFrame_;
}

FrameData Renderer::getLastFrameCopy() const {
    return getLatestFrame();
}

void Renderer::setSceneParams(SceneParams* params) {
    sceneParams_ = params;
}

void Renderer::setRenderPath(RenderPath path) {
    if (path == renderPath_) return;
    renderPath_ = path;
    // Mark that deferred resources need (re)creation on the render thread.
    // The render() method will lazily create G-Buffer targets + PSOs when
    // it detects they're missing (gbufferPSO_ == RHI_NULL check already exists).
    DIAG_FMT("Render path set to %s (lazy resource creation on next frame)",
             path == RenderPath::Forward ? "Forward" : "Deferred");
}

void Renderer::shutdown() {
    if (!initialized_) return;

    iblBaker_.shutdown();
    editor_.shutdown();
    meshRenderer_.shutdown();

    for (auto* p : proxies_) delete p;
    proxies_.clear();
    materialManager_.clear();
    destroy_gpu_mesh(sphereMesh_);

    if (whiteTex_ != RHI_NULL) {
        rhi()->texture_destroy(whiteTex_);
        whiteTex_ = RHI_NULL;
    }
    if (blackTex_ != RHI_NULL) {
        rhi()->texture_destroy(blackTex_);
        blackTex_ = RHI_NULL;
    }
    if (normalTex_ != RHI_NULL) {
        rhi()->texture_destroy(normalTex_);
        normalTex_ = RHI_NULL;
    }
    if (envCubemap_ != RHI_NULL) {
        rhi()->texture_destroy(envCubemap_);
        envCubemap_ = RHI_NULL;
    }

    if (lightFrameCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(lightFrameCBV_);
        lightFrameCBV_ = RHI_NULL;
    }
    if (ssaoB1CBV_ != RHI_NULL) {
        rhi()->buffer_destroy(ssaoB1CBV_);
        ssaoB1CBV_ = RHI_NULL;
    }
    if (outlineCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(outlineCBV_);
        outlineCBV_ = RHI_NULL;
    }
    if (postProcessCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(postProcessCBV_);
        postProcessCBV_ = RHI_NULL;
    }

    if (rhiReadbackFence_ != RHI_NULL) {
        rhi()->fence_destroy(rhiReadbackFence_);
        rhiReadbackFence_ = RHI_NULL;
    }
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (rhiReadbackBufs_[i] != RHI_NULL) {
            rhi()->buffer_destroy(rhiReadbackBufs_[i]);
            rhiReadbackBufs_[i] = RHI_NULL;
        }
    }
    if (rhiDepthBuf_ != RHI_NULL) {
        rhi()->texture_destroy(rhiDepthBuf_);
        rhiDepthBuf_ = RHI_NULL;
    }

    if (rhiPipeline_ != RHI_NULL) {
        rhi()->pipeline_destroy(rhiPipeline_);
        rhiPipeline_ = RHI_NULL;
    }
    if (rhiSkyPipeline_ != RHI_NULL) {
        rhi()->pipeline_destroy(rhiSkyPipeline_);
        rhiSkyPipeline_ = RHI_NULL;
    }
    if (rhiShadowPipeline_ != RHI_NULL) {
        rhi()->pipeline_destroy(rhiShadowPipeline_);
        rhiShadowPipeline_ = RHI_NULL;
    }
    if (shadowMap_ != RHI_NULL) {
        rhi()->texture_destroy(shadowMap_);
        shadowMap_ = RHI_NULL;
    }

    destroyPostProcessTargets();

    rhi()->shutdown();
    initialized_ = false;
}

// ===========================================================================
// Private -- pipeline state via RHI
// ===========================================================================

bool Renderer::createPipelineStates() {
    UZ_PROFILE_ZONE("init/createPipelineStates");
    // Pipeline desc: vertex layout + render state.
    // Shader source is filled by the ShaderLibrary.
    RhiPipelineDesc desc{};

    // Vertex layout: POSITION (float3) + COLOR (float3).
    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0] = {
        "POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3, 0, 0
    };
    desc.vertex_layout.attribs[1] = {
        "COLOR", 0, RHI_VERTEX_FORMAT_FLOAT3,
        3 * sizeof(float), 0   // 12 bytes offset after POSITION
    };

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;
    desc.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;

    desc.rasterizer.fill_mode = RHI_FILL_SOLID;
    desc.rasterizer.cull_mode = RHI_CULL_NONE;  // TEMP: disable culling to diagnose sphere winding

    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = true;
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS;

    desc.blend.render_targets[0].write_mask = 0xF;
    desc.debug_name = "BasicPSO";

    rhiPipeline_ = shaderLib_.get_or_create_pipeline("basic", desc);
    DIAG_FMT("basic PSO => handle=%llu", (unsigned long long)rhiPipeline_);

    // PBR PSO for Scene Proxies — renders to HDR intermediate RT (R11G11B10_FLOAT)
    RhiPipelineDesc pbrDesc = desc;
    pbrDesc.rt_formats[0] = RHI_FORMAT_R11G11B10_FLOAT; // HDR output
    pbrDesc.vertex_layout.attrib_count = 4;
    pbrDesc.vertex_layout.attribs[0] = { "POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3, 0, 0 };
    pbrDesc.vertex_layout.attribs[1] = { "NORMAL",   0, RHI_VERTEX_FORMAT_FLOAT3, 12, 0 };
    pbrDesc.vertex_layout.attribs[2] = { "TEXCOORD", 0, RHI_VERTEX_FORMAT_FLOAT2, 24, 0 };
    pbrDesc.vertex_layout.attribs[3] = { "TANGENT",  0, RHI_VERTEX_FORMAT_FLOAT4, 32, 0 };
    pbrDesc.debug_name = "PBR_PSO";
    
    rhiPbrPipeline_ = shaderLib_.get_or_create_pipeline("pbr", pbrDesc);
    DIAG_FMT("pbr PSO => handle=%llu", (unsigned long long)rhiPbrPipeline_);

    // ---- Stencil Mark PSO (writes 1 to stencil, no color) ----
    {
        RhiPipelineDesc stDesc = pbrDesc;  // Same vertex layout as PBR
        stDesc.depth_stencil.stencil_enable    = true;
        stDesc.depth_stencil.stencil_read_mask  = 0xFF;
        stDesc.depth_stencil.stencil_write_mask = 0xFF;
        stDesc.depth_stencil.front_face.fail_op       = RHI_STENCIL_OP_KEEP;
        stDesc.depth_stencil.front_face.depth_fail_op = RHI_STENCIL_OP_KEEP;
        stDesc.depth_stencil.front_face.pass_op       = RHI_STENCIL_OP_REPLACE;
        stDesc.depth_stencil.front_face.func          = RHI_COMPARE_ALWAYS;
        stDesc.depth_stencil.back_face = stDesc.depth_stencil.front_face;
        stDesc.blend.render_targets[0].write_mask = 0; // No color output
        stDesc.debug_name = "StencilMark_PSO";
        rhiStencilMarkPSO_ = shaderLib_.get_or_create_pipeline("pbr", stDesc);
        DIAG_FMT("stencil mark PSO => handle=%llu", (unsigned long long)rhiStencilMarkPSO_);
    }

    // ---- Outline PSO (inflated mesh, stencil test != 1, no cull) ----
    {
        RhiPipelineDesc olDesc = pbrDesc;
        olDesc.rasterizer.cull_mode = RHI_CULL_NONE;  // No culling — inflated silhouette needs both faces
        olDesc.depth_stencil.depth_enable = true;      // Read depth so outline respects scene occlusion
        olDesc.depth_stencil.depth_write  = false;     // Don't pollute depth buffer
        olDesc.depth_stencil.depth_func   = RHI_COMPARE_LESS_EQUAL;
        olDesc.depth_stencil.stencil_enable     = true;
        olDesc.depth_stencil.stencil_read_mask  = 0xFF;
        olDesc.depth_stencil.stencil_write_mask = 0x00;
        olDesc.depth_stencil.front_face.fail_op       = RHI_STENCIL_OP_KEEP;
        olDesc.depth_stencil.front_face.depth_fail_op = RHI_STENCIL_OP_KEEP;
        olDesc.depth_stencil.front_face.pass_op       = RHI_STENCIL_OP_KEEP;
        olDesc.depth_stencil.front_face.func          = RHI_COMPARE_NOT_EQUAL;
        olDesc.depth_stencil.back_face = olDesc.depth_stencil.front_face;
        olDesc.blend.render_targets[0].write_mask = 0xF;
        olDesc.debug_name = "Outline_PSO";
        rhiOutlinePSO_ = shaderLib_.get_or_create_pipeline("outline", olDesc);
        DIAG_FMT("outline PSO => handle=%llu", (unsigned long long)rhiOutlinePSO_);
    }

    // ---- Shadow depth PSO — POSITION-only VS, no PS, D32 depth ----
    {
        RhiPipelineDesc shadowDesc{};
        shadowDesc.vertex_layout.attrib_count = 1;
        shadowDesc.vertex_layout.attribs[0] = { "POSITION", 0, RHI_VERTEX_FORMAT_FLOAT3, 0, 0 };
        shadowDesc.rt_count      = 0;  // Depth only — no render targets
        shadowDesc.ds_format     = RHI_FORMAT_D32_FLOAT;
        shadowDesc.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;
        shadowDesc.rasterizer.fill_mode = RHI_FILL_SOLID;
        shadowDesc.rasterizer.cull_mode = RHI_CULL_BACK;  // Back-face culling avoids peter-panning
        shadowDesc.depth_stencil.depth_enable = true;
        shadowDesc.depth_stencil.depth_write  = true;
        shadowDesc.depth_stencil.depth_func   = RHI_COMPARE_LESS;
        shadowDesc.debug_name = "ShadowDepth_PSO";
        rhiShadowPipeline_ = shaderLib_.get_or_create_pipeline("shadow_depth", shadowDesc);
        DIAG_FMT("shadow depth PSO => handle=%llu", (unsigned long long)rhiShadowPipeline_);
    }

    // ---- Sky PSO — fullscreen triangle, no vertex buffer, depth disabled ----
    // Renders to HDR intermediate RT (R11G11B10_FLOAT).
    {
        RhiPipelineDesc skyDesc{};
        skyDesc.vertex_layout.attrib_count = 0;
        skyDesc.rt_count      = 1;
        skyDesc.rt_formats[0] = RHI_FORMAT_R11G11B10_FLOAT;
        skyDesc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;
        skyDesc.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;
        skyDesc.rasterizer.fill_mode = RHI_FILL_SOLID;
        skyDesc.rasterizer.cull_mode = RHI_CULL_NONE;
        skyDesc.depth_stencil.depth_enable = false;
        skyDesc.depth_stencil.depth_write  = false;
        skyDesc.blend.render_targets[0].write_mask = 0xF;
        skyDesc.debug_name = "Sky_PSO";
        rhiSkyPipeline_ = shaderLib_.get_or_create_pipeline("sky", skyDesc);
        DIAG_FMT("sky PSO => handle=%llu", (unsigned long long)rhiSkyPipeline_);
    }

    // ---- Post-process PSOs — all fullscreen triangles, no vertex buffer, no depth ----
    auto makePostProcDesc = [](RhiFormat rtFormat) {
        RhiPipelineDesc d{};
        d.vertex_layout.attrib_count = 0;
        d.rt_count      = 1;
        d.rt_formats[0] = rtFormat;
        d.ds_format     = RHI_FORMAT_UNKNOWN;
        d.topology      = RHI_TOPOLOGY_TRIANGLE_LIST;
        d.rasterizer.fill_mode = RHI_FILL_SOLID;
        d.rasterizer.cull_mode = RHI_CULL_NONE;
        d.depth_stencil.depth_enable = false;
        d.depth_stencil.depth_write  = false;
        d.blend.render_targets[0].write_mask = 0xF;
        return d;
    };

    {
        auto d = makePostProcDesc(RHI_FORMAT_R8_UNORM);
        d.debug_name = "SSAO_PSO";
        ssaoPSO_ = shaderLib_.get_or_create_pipeline("ssao", d);
        DIAG_FMT("ssao PSO => handle=%llu", (unsigned long long)ssaoPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_R8_UNORM);
        d.debug_name = "SSAO_Blur_PSO";
        ssaoBlurPSO_ = shaderLib_.get_or_create_pipeline("ssao_blur", d);
        DIAG_FMT("ssao_blur PSO => handle=%llu", (unsigned long long)ssaoBlurPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_R11G11B10_FLOAT);
        d.debug_name = "Bloom_Down_PSO";
        bloomDownPSO_ = shaderLib_.get_or_create_pipeline("bloom_down", d);
        DIAG_FMT("bloom_down PSO => handle=%llu", (unsigned long long)bloomDownPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_R11G11B10_FLOAT);
        d.debug_name = "Bloom_Up_PSO";
        bloomUpPSO_ = shaderLib_.get_or_create_pipeline("bloom_up", d);
        DIAG_FMT("bloom_up PSO => handle=%llu", (unsigned long long)bloomUpPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_RGBA8_UNORM);
        d.debug_name = "Composite_PSO";
        compositePSO_ = shaderLib_.get_or_create_pipeline("composite", d);
        DIAG_FMT("composite PSO => handle=%llu", (unsigned long long)compositePSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_RG16_FLOAT);
        d.debug_name = "Velocity_PSO";
        velocityPSO_ = shaderLib_.get_or_create_pipeline("velocity", d);
        DIAG_FMT("velocity PSO => handle=%llu", (unsigned long long)velocityPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_R11G11B10_FLOAT);
        d.debug_name = "TAA_PSO";
        taaPSO_ = shaderLib_.get_or_create_pipeline("taa", d);
        DIAG_FMT("taa PSO => handle=%llu", (unsigned long long)taaPSO_);
    }
    {
        auto d = makePostProcDesc(RHI_FORMAT_R8_UNORM);
        d.debug_name = "GTAO_PSO";
        gtaoPSO_ = shaderLib_.get_or_create_pipeline("gtao", d);
        DIAG_FMT("gtao PSO => handle=%llu", (unsigned long long)gtaoPSO_);
    }

    // ---- Auto Exposure PSO — 1x1 R16_FLOAT output ----
    {
        auto d = makePostProcDesc(RHI_FORMAT_R16_FLOAT);
        d.debug_name = "AutoExposure_PSO";
        autoExposurePSO_ = shaderLib_.get_or_create_pipeline("auto_exposure", d);
        DIAG_FMT("auto_exposure PSO => handle=%llu", (unsigned long long)autoExposurePSO_);
    }

    // ---- SSR PSO — R11G11B10_FLOAT output ----
    {
        auto d = makePostProcDesc(RHI_FORMAT_R11G11B10_FLOAT);
        d.debug_name = "SSR_PSO";
        ssrPSO_ = shaderLib_.get_or_create_pipeline("ssr", d);
        DIAG_FMT("ssr PSO => handle=%llu", (unsigned long long)ssrPSO_);
    }
    // ---- Hi-Z generation PSO — R32_FLOAT depth reduction ----
    {
        auto d = makePostProcDesc(RHI_FORMAT_R32_FLOAT);
        d.debug_name = "HiZ_PSO";
        hizPSO_ = shaderLib_.get_or_create_pipeline("hiz_generate", d);
        DIAG_FMT("hiz PSO => handle=%llu", (unsigned long long)hizPSO_);
    }
    // ---- Color Grading PSO — RGBA8 output (final pass) ----
    {
        auto d = makePostProcDesc(RHI_FORMAT_RGBA8_UNORM);
        d.debug_name = "ColorGrading_PSO";
        colorGradingPSO_ = shaderLib_.get_or_create_pipeline("color_grading", d);
        DIAG_FMT("color_grading PSO => handle=%llu", (unsigned long long)colorGradingPSO_);
    }

    // ---- Volumetric Clouds PSO — RGBA16F, alpha blending over sky ----
    {
        RhiPipelineDesc cd = makePostProcDesc(RHI_FORMAT_RGBA16_FLOAT);
        cd.debug_name = "Clouds_PSO";
        // Alpha blend: cloud color composited over sky (src_alpha, 1-src_alpha)
        cd.blend.render_targets[0].blend_enable    = true;
        cd.blend.render_targets[0].src_blend       = RHI_BLEND_SRC_ALPHA;
        cd.blend.render_targets[0].dst_blend       = RHI_BLEND_INV_SRC_ALPHA;
        cd.blend.render_targets[0].blend_op        = RHI_BLEND_OP_ADD;
        cd.blend.render_targets[0].src_blend_alpha = RHI_BLEND_ONE;
        cd.blend.render_targets[0].dst_blend_alpha = RHI_BLEND_INV_SRC_ALPHA;
        cd.blend.render_targets[0].blend_op_alpha  = RHI_BLEND_OP_ADD;
        cloudsPSO_ = shaderLib_.get_or_create_pipeline("clouds", cd);
        DIAG_FMT("clouds PSO => handle=%llu", (unsigned long long)cloudsPSO_);
    }

    // ---- Deferred Shading PSOs ----
    if (renderPath_ == RenderPath::Deferred) {
        // G-Buffer write PSO — same vertex layout as PBR, 4 MRT outputs, depth write
        {
            RhiPipelineDesc gbDesc = pbrDesc;  // inherit PBR vertex layout
            gbDesc.rt_count = 4;
            gbDesc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;      // Albedo+AO
            gbDesc.rt_formats[1] = RHI_FORMAT_RGB10A2_UNORM;    // Normal (oct)
            gbDesc.rt_formats[2] = RHI_FORMAT_RGBA8_UNORM;      // Material
            gbDesc.rt_formats[3] = RHI_FORMAT_R11G11B10_FLOAT;  // Emission HDR
            gbDesc.blend.independent_blend = false;
            for (int i = 0; i < 4; ++i) {
                gbDesc.blend.render_targets[i].blend_enable = false;
                gbDesc.blend.render_targets[i].write_mask = 0xF;
            }
            gbDesc.debug_name = "GBuffer_PSO";
            gbufferPSO_ = shaderLib_.get_or_create_pipeline("gbuffer_write", gbDesc);
            DIAG_FMT("gbuffer PSO => handle=%llu", (unsigned long long)gbufferPSO_);
        }

        // Deferred lighting PSO — fullscreen triangle, reads G-Buffer, outputs to hdrRT_
        {
            auto d = makePostProcDesc(RHI_FORMAT_R11G11B10_FLOAT);
            d.debug_name = "DeferredLighting_PSO";
            deferredLightPSO_ = shaderLib_.get_or_create_pipeline("deferred_lighting", d);
            DIAG_FMT("deferred lighting PSO => handle=%llu", (unsigned long long)deferredLightPSO_);
        }
    }

    return rhiPipeline_ != RHI_NULL && rhiPbrPipeline_ != RHI_NULL;
}


// ===========================================================================
// Private -- depth buffer via RHI
// ===========================================================================

bool Renderer::createDepthBuffer() {
    RhiTextureDesc desc{};
    desc.width      = width_;
    desc.height     = height_;
    desc.depth      = 1;
    desc.mip_levels = 1;
    desc.array_size = 1;
    desc.format     = RHI_FORMAT_D24_UNORM_S8_UINT;
    desc.dimension  = RHI_TEXTURE_DIM_2D;
    desc.usage      = RHI_TEXTURE_USAGE_DEPTH_STENCIL | RHI_TEXTURE_USAGE_SHADER_READ;
    desc.debug_name = "DepthBuffer";

    rhiDepthBuf_ = rhi()->texture_create(&desc);
    return rhiDepthBuf_ != RHI_NULL;
}

// ===========================================================================
// Private -- readback buffers via RHI
// ===========================================================================

bool Renderer::createReadbackBuffers() {
    const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;
    const uint64_t bufSize = static_cast<uint64_t>(alignedPitch) * height_;

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        RhiBufferDesc desc{};
        desc.size       = bufSize;
        desc.usage      = RHI_BUFFER_USAGE_READBACK;
        desc.debug_name = "ReadbackBuf";

        rhiReadbackBufs_[i] = rhi()->buffer_create(&desc);
        if (rhiReadbackBufs_[i] == RHI_NULL) return false;
    }

    rhiReadbackFence_ = rhi()->fence_create();
    if (rhiReadbackFence_ == RHI_NULL) return false;

    readbackFenceValue_ = 0;
    return true;
}

// ===========================================================================
// Private -- post-process render targets
// ===========================================================================

void Renderer::destroyPostProcessTargets() {
    auto destroy = [](RhiTexture& t) {
        if (t != RHI_NULL) { rhi()->texture_destroy(t); t = RHI_NULL; }
    };
    destroy(hdrRT_);
    destroy(ssaoRT_);
    destroy(ssaoBlurRT_);
    destroy(velocityRT_);
    for (auto& h : taaHistory_) destroy(h);
    for (auto& m : bloomMips_) destroy(m);
    for (auto& g : gbufferRT_) destroy(g);
    for (auto& e : exposureRT_) destroy(e);
    destroy(ssrRT_);
    for (auto& h : hizMips_) destroy(h);
    if (exposureCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(exposureCBV_);
        exposureCBV_ = RHI_NULL;
    }
    if (ssrCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(ssrCBV_);
        ssrCBV_ = RHI_NULL;
    }
    if (deferredLightCBV_ != RHI_NULL) {
        rhi()->buffer_destroy(deferredLightCBV_);
        deferredLightCBV_ = RHI_NULL;
    }
}

bool Renderer::createPostProcessTargets() {
    destroyPostProcessTargets();

    // HDR scene color — full resolution, R11G11B10_FLOAT
    {
        RhiTextureDesc d{};
        d.width = width_; d.height = height_; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R11G11B10_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "HDR_RT";
        hdrRT_ = rhi()->texture_create(&d);
        if (hdrRT_ == RHI_NULL) return false;
    }

    // SSAO — half resolution, R8
    {
        RhiTextureDesc d{};
        d.width = width_ / 2; d.height = height_ / 2; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R8_UNORM;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "SSAO_RT";
        ssaoRT_ = rhi()->texture_create(&d);
        if (ssaoRT_ == RHI_NULL) return false;
    }

    // SSAO blur — same size as SSAO
    {
        RhiTextureDesc d{};
        d.width = width_ / 2; d.height = height_ / 2; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R8_UNORM;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "SSAO_Blur_RT";
        ssaoBlurRT_ = rhi()->texture_create(&d);
        if (ssaoBlurRT_ == RHI_NULL) return false;
    }

    // Velocity buffer — full resolution, R16G16_FLOAT (motion vectors for TAA/GTAO/SSR)
    {
        RhiTextureDesc d{};
        d.width = width_; d.height = height_; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_RG16_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "Velocity_RT";
        velocityRT_ = rhi()->texture_create(&d);
        if (velocityRT_ == RHI_NULL) return false;
    }

    // TAA history buffers — full resolution, R11G11B10_FLOAT, ping-pong pair
    for (uint32_t i = 0; i < kTAAHistoryCount; ++i) {
        RhiTextureDesc d{};
        d.width = width_; d.height = height_; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R11G11B10_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = (i == 0) ? "TAA_History_0" : "TAA_History_1";
        taaHistory_[i] = rhi()->texture_create(&d);
        if (taaHistory_[i] == RHI_NULL) return false;
    }
    taaWriteIdx_ = 0;

    // Bloom mip chain — progressively halved from half-res
    uint32_t bw = width_ / 2;
    uint32_t bh = height_ / 2;
    for (uint32_t i = 0; i < kBloomMips; ++i) {
        bw = std::max(bw, 1u);
        bh = std::max(bh, 1u);
        RhiTextureDesc d{};
        d.width = bw; d.height = bh; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R11G11B10_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "BloomMip";
        bloomMips_[i] = rhi()->texture_create(&d);
        if (bloomMips_[i] == RHI_NULL) return false;
        bw /= 2;
        bh /= 2;
    }

    // Auto Exposure targets — 1x1 R16_FLOAT ping-pong pair
    for (uint32_t i = 0; i < kExposureHistoryCount; ++i) {
        RhiTextureDesc d{};
        d.width = 1; d.height = 1; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_R16_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = (i == 0) ? "Exposure_0" : "Exposure_1";
        exposureRT_[i] = rhi()->texture_create(&d);
        if (exposureRT_[i] == RHI_NULL) return false;
    }
    exposureWriteIdx_ = 0;

    // Exposure CBV (for auto_exposure pass params)
    if (exposureCBV_ == RHI_NULL) {
        RhiBufferDesc cbvDesc{};
        cbvDesc.size       = 256;
        cbvDesc.usage      = RHI_BUFFER_USAGE_CONSTANT | RHI_BUFFER_USAGE_UPLOAD;
        cbvDesc.debug_name = "ExposureCBV";
        exposureCBV_ = rhi()->buffer_create(&cbvDesc);
    }

    // SSR render target — full resolution, RGBA16F (reflection color + confidence)
    {
        RhiTextureDesc d{};
        d.width = width_; d.height = height_; d.depth = 1;
        d.mip_levels = 1; d.array_size = 1;
        d.format    = RHI_FORMAT_RGBA16_FLOAT;
        d.dimension = RHI_TEXTURE_DIM_2D;
        d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
        d.debug_name = "SSR_RT";
        ssrRT_ = rhi()->texture_create(&d);
        if (ssrRT_ == RHI_NULL) return false;
    }

    // SSR CBV (for SSR pass params)
    if (ssrCBV_ == RHI_NULL) {
        RhiBufferDesc cbvDesc{};
        cbvDesc.size       = 256;
        cbvDesc.usage      = RHI_BUFFER_USAGE_CONSTANT | RHI_BUFFER_USAGE_UPLOAD;
        cbvDesc.debug_name = "SSR_CBV";
        ssrCBV_ = rhi()->buffer_create(&cbvDesc);
    }

    // Hi-Z mip chain — each mip is half the previous, min-depth reduction
    {
        uint32_t hw = width_;
        uint32_t hh = height_;
        for (uint32_t i = 0; i < kHiZMips; ++i) {
            hw = std::max(hw / 2, 1u);
            hh = std::max(hh / 2, 1u);
            RhiTextureDesc d{};
            d.width = hw; d.height = hh; d.depth = 1;
            d.mip_levels = 1; d.array_size = 1;
            d.format    = RHI_FORMAT_R32_FLOAT;
            d.dimension = RHI_TEXTURE_DIM_2D;
            d.usage     = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
            d.debug_name = "HiZ_Mip";
            hizMips_[i] = rhi()->texture_create(&d);
            if (hizMips_[i] == RHI_NULL) return false;
        }
    }

    // G-Buffer render targets (deferred path)
    if (renderPath_ == RenderPath::Deferred) {
        static const RhiFormat gbFormats[kGBufferCount] = {
            RHI_FORMAT_RGBA8_UNORM,      // 0: Albedo (RGB) + AO (A)
            RHI_FORMAT_RGB10A2_UNORM,    // 1: Octahedral Normal (RG10) + flags (A2)
            RHI_FORMAT_RGBA8_UNORM,      // 2: Metallic (R) + Roughness (G) + EmissionScale (B) + flags (A)
            RHI_FORMAT_R11G11B10_FLOAT,  // 3: Emission HDR (RGB)
        };
        static const char* gbNames[kGBufferCount] = {
            "GBuffer0_AlbedoAO", "GBuffer1_Normal", "GBuffer2_Material", "GBuffer3_Emission"
        };
        for (uint32_t i = 0; i < kGBufferCount; ++i) {
            RhiTextureDesc d{};
            d.width = width_; d.height = height_; d.depth = 1;
            d.mip_levels = 1; d.array_size = 1;
            d.format     = gbFormats[i];
            d.dimension  = RHI_TEXTURE_DIM_2D;
            d.usage      = RHI_TEXTURE_USAGE_RENDER_TARGET | RHI_TEXTURE_USAGE_SHADER_READ;
            d.debug_name = gbNames[i];
            gbufferRT_[i] = rhi()->texture_create(&d);
            if (gbufferRT_[i] == RHI_NULL) return false;
        }

        // Deferred lighting CBV (for invViewProj, reused each frame)
        if (deferredLightCBV_ == RHI_NULL) {
            RhiBufferDesc cbvDesc{};
            cbvDesc.size       = 256;
            cbvDesc.usage      = RHI_BUFFER_USAGE_CONSTANT | RHI_BUFFER_USAGE_UPLOAD;
            cbvDesc.debug_name = "DeferredLightCBV";
            deferredLightCBV_ = rhi()->buffer_create(&cbvDesc);
        }

        gbufferReady_ = false;  // Reset barrier tracking after (re)creation
        DIAG_FMT("G-Buffer targets created: %ux%u (4 RTs)", width_, height_);
    }

    DIAG_FMT("Post-process targets created: HDR %ux%u, SSAO %ux%u, Bloom %u mips",
             width_, height_, width_ / 2, height_ / 2, kBloomMips);
    return true;
}

// ===========================================================================
// Private -- GPU -> CPU readback
// ===========================================================================

void Renderer::performReadback() {
    UZ_PROFILE_ZONE("render/readback");
    ++readbackFenceValue_;
    rhi()->fence_signal(rhiReadbackFence_, readbackFenceValue_);
    rhi()->fence_wait(rhiReadbackFence_, readbackFenceValue_, UINT64_MAX);

    const uint32_t alignedPitch = (width_ * 4 + 255u) & ~255u;

    void* mappedData = rhi()->buffer_map(rhiReadbackBufs_[frameIndex_]);
    if (!mappedData) return;

    const uint64_t timestamp = streamTimer_.elapsedMicros();

    const uint32_t rowBytes = width_ * 4;

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_.width       = width_;
        latestFrame_.height      = height_;
        latestFrame_.frameNumber = ++frameNumber_;
        latestFrame_.timestamp   = timestamp;

        latestFrame_.pixels.resize(static_cast<size_t>(rowBytes) * height_);

        const auto* src = static_cast<const uint8_t*>(mappedData);
        uint8_t*    dst = latestFrame_.pixels.data();

        for (uint32_t y = 0; y < height_; ++y) {
            std::memcpy(dst + y * rowBytes,
                        src + y * alignedPitch,
                        rowBytes);
        }

        fpsOverlay_.renderInto(latestFrame_.pixels.data(),
                               width_, height_, rowBytes);
    }

    rhi()->buffer_unmap(rhiReadbackBufs_[frameIndex_]);
}

// ===========================================================================
// Public -- editor scene loading
// ===========================================================================

size_t Renderer::loadEditorScene(std::vector<EditorNode> nodes) {
    const size_t count = editor_.loadNodes(std::move(nodes));
    return count;
}

void Renderer::gatherPunctualLights() {
    punctualLightCount_ = 0;
    if (!scene_) return;

    for (uint32_t i = 1; i < scene_->next_id && punctualLightCount_ < kMaxPunctualLights; ++i) {
        const SceneNode& sn = scene_->nodes[i];
        if (!sn.alive) continue;

        if (std::strcmp(sn.type_name, "PointLight") == 0) {
            const auto* data = component_get<PointLightData>(&scene_->components, i);
            if (!data) continue;

            auto& gpu = punctualLights_[punctualLightCount_++];
            gpu.posX = sn.world_transform.m[3][0];
            gpu.posY = sn.world_transform.m[3][1];
            gpu.posZ = sn.world_transform.m[3][2];
            gpu.radius = data->radius;
            gpu.colR = data->color.x;
            gpu.colG = data->color.y;
            gpu.colB = data->color.z;
            gpu.intensity = data->intensity;
            gpu.dirX = 0.0f; gpu.dirY = -1.0f; gpu.dirZ = 0.0f;
            gpu.spotParams = 0.0f;  // point light: no spot cone
        }
        else if (std::strcmp(sn.type_name, "SpotLight") == 0) {
            const auto* data = component_get<SpotLightData>(&scene_->components, i);
            if (!data) continue;

            auto& gpu = punctualLights_[punctualLightCount_++];
            gpu.posX = sn.world_transform.m[3][0];
            gpu.posY = sn.world_transform.m[3][1];
            gpu.posZ = sn.world_transform.m[3][2];
            gpu.radius = data->radius;
            gpu.colR = data->color.x;
            gpu.colG = data->color.y;
            gpu.colB = data->color.z;
            gpu.intensity = data->intensity;
            // Transform light direction by world rotation
            gpu.dirX = sn.world_transform.m[2][0] * data->direction.x
                     + sn.world_transform.m[2][1] * data->direction.y
                     + sn.world_transform.m[2][2] * data->direction.z;
            gpu.dirY = sn.world_transform.m[1][0] * data->direction.x
                     + sn.world_transform.m[1][1] * data->direction.y
                     + sn.world_transform.m[1][2] * data->direction.z;
            gpu.dirZ = sn.world_transform.m[0][0] * data->direction.x
                     + sn.world_transform.m[0][1] * data->direction.y
                     + sn.world_transform.m[0][2] * data->direction.z;
            // Normalize direction
            float len = std::sqrt(gpu.dirX*gpu.dirX + gpu.dirY*gpu.dirY + gpu.dirZ*gpu.dirZ);
            if (len > 0.001f) { gpu.dirX /= len; gpu.dirY /= len; gpu.dirZ /= len; }
            // spotParams = cos(outerAngle) — used for cone attenuation
            gpu.spotParams = std::cos(data->outer_angle * 3.14159265f / 180.0f);
        }
    }
}

void Renderer::invalidateMaterialCache() {
    for (auto* proxy : proxies_) {
        auto* mp = static_cast<MeshProxy*>(proxy);
        mp->cache_materials();
    }
}

void Renderer::setScene(Scene* scene) {
    UZ_PROFILE_ZONE("Renderer::setScene");
    scene_ = scene;
    scene_dirty_ = true;

    // Regenerate IBL cubemaps from the scene's SkyAtmosphere + DirectionalLight.
    // Matches UE5 SkyLight::RecaptureSky() / Godot Sky radiance_dirty pattern:
    // whenever the scene changes, the IBL must reflect the actual sky colors.
    if (scene_) {
        SkyCubemapParams skyParams{};
        // Defaults (UE5-style vibrant blue sky)
        skyParams.sky_zenith[0]  = 0.03f; skyParams.sky_zenith[1]  = 0.07f; skyParams.sky_zenith[2]  = 0.45f;
        skyParams.sky_horizon[0] = 0.40f; skyParams.sky_horizon[1] = 0.65f; skyParams.sky_horizon[2] = 1.10f;
        skyParams.sky_ground[0]  = 0.08f; skyParams.sky_ground[1]  = 0.07f; skyParams.sky_ground[2]  = 0.06f;
        skyParams.sun_dir[0]     = 0.3f;  skyParams.sun_dir[1]     = 0.8f;  skyParams.sun_dir[2]     = 0.3f;
        skyParams.sun_intensity  = 5.0f;

        // Read SkyAtmosphereData from scene (first node that has it)
        for (uint32_t i = 1; i < scene_->next_id; ++i) {
            SceneNode* n = scene_get_node(scene_, i);
            if (!n || !n->alive) continue;
            if (!component_has<SkyAtmosphereData>(&scene_->components, i)) continue;
            auto* sky = component_get<SkyAtmosphereData>(&scene_->components, i);
            if (!sky) break;
            skyParams.sky_zenith[0]  = sky->sky_zenith[0];
            skyParams.sky_zenith[1]  = sky->sky_zenith[1];
            skyParams.sky_zenith[2]  = sky->sky_zenith[2];
            skyParams.sky_horizon[0] = sky->sky_horizon[0];
            skyParams.sky_horizon[1] = sky->sky_horizon[1];
            skyParams.sky_horizon[2] = sky->sky_horizon[2];
            skyParams.sky_ground[0]  = sky->sky_ground[0];
            skyParams.sky_ground[1]  = sky->sky_ground[1];
            skyParams.sky_ground[2]  = sky->sky_ground[2];
            break;
        }

        // Read sun direction from DirectionalLight (first node with rotation)
        for (uint32_t i = 1; i < scene_->next_id; ++i) {
            SceneNode* n = scene_get_node(scene_, i);
            if (!n || !n->alive) continue;
            if (!component_has<DirectionalLightData>(&scene_->components, i)) continue;
            auto* light = component_get<DirectionalLightData>(&scene_->components, i);
            if (!light) break;
            // Use node's forward direction as sun direction
            skyParams.sun_dir[0] = n->world_transform.m[2][0];
            skyParams.sun_dir[1] = n->world_transform.m[2][1];
            skyParams.sun_dir[2] = n->world_transform.m[2][2];
            skyParams.sun_intensity = light->intensity * 5.0f;
            break;
        }

        DIAG("Regenerating IBL cubemaps from scene sky/light (GPU compute)...");
        SkyCubemapResult skyResult;
        {
            UZ_PROFILE_ZONE("setScene/sky_cubemap_gen");
            skyResult = generate_sky_cubemaps(skyParams);
        }
        if (envCubemap_ != RHI_NULL)
            rhi()->texture_destroy(envCubemap_);
        envCubemap_ = skyResult.env_cubemap;

        IblBakeResult bakeResult;
        {
            UZ_PROFILE_ZONE("setScene/ibl_bake");
            bakeResult = iblBaker_.bake(envCubemap_, 256);
        }
        if (bakeResult.irradiance_cube != RHI_NULL) {
            irradianceCube_ = bakeResult.irradiance_cube;
        }
        if (bakeResult.prefiltered_cube != RHI_NULL) {
            prefilteredCube_ = bakeResult.prefiltered_cube;
        }
        if (bakeResult.irradiance_cube == RHI_NULL || bakeResult.prefiltered_cube == RHI_NULL) {
            DIAG("WARNING: GPU IBL bake failed on setScene, IBL may be incomplete");
        }
    }
}

// ===========================================================================
// Pick buffer queries (UZ_EDITOR only)
// ===========================================================================
#ifdef UZ_EDITOR

uint32_t Renderer::pickNodeAtScreen(int x, int y) const {
    std::lock_guard<std::mutex> lk(pickMutex_);
    if (pickData_.empty() || pickW_ == 0 || pickH_ == 0) return 0;

    const int px = std::clamp(x / 4, 0, static_cast<int>(pickW_) - 1);
    const int py = std::clamp(y / 4, 0, static_cast<int>(pickH_) - 1);

    const size_t idx = static_cast<size_t>(py) * pickW_ + px;
    if (idx * 4 + 2 >= pickData_.size()) return 0;

    const uint32_t r = pickData_[idx * 4 + 0];
    const uint32_t g = pickData_[idx * 4 + 1];
    const uint32_t b = pickData_[idx * 4 + 2];
    return r | (g << 8) | (b << 16);
}

std::vector<uint32_t> Renderer::boxPickNodes(int x, int y, int w, int h) const {
    std::lock_guard<std::mutex> lk(pickMutex_);
    if (pickData_.empty() || pickW_ == 0 || pickH_ == 0) return {};

    const int x0 = std::clamp(x       / 4, 0, static_cast<int>(pickW_));
    const int y0 = std::clamp(y       / 4, 0, static_cast<int>(pickH_));
    const int x1 = std::clamp((x + w) / 4, 0, static_cast<int>(pickW_));
    const int y1 = std::clamp((y + h) / 4, 0, static_cast<int>(pickH_));

    std::vector<uint32_t> result;
    for (int row = y0; row < y1; ++row) {
        for (int col = x0; col < x1; ++col) {
            const size_t idx = static_cast<size_t>(row) * pickW_ + col;
            if (idx * 4 + 2 >= pickData_.size()) continue;

            const uint32_t r  = pickData_[idx * 4 + 0];
            const uint32_t g  = pickData_[idx * 4 + 1];
            const uint32_t b  = pickData_[idx * 4 + 2];
            const uint32_t id = r | (g << 8) | (b << 16);
            if (id == 0) continue;

            bool found = false;
            for (uint32_t ex : result) { if (ex == id) { found = true; break; } }
            if (!found) result.push_back(id);
        }
    }
    return result;
}

#endif // UZ_EDITOR

} // namespace uz

// ===========================================================================
// Private -- test scene setup
// ===========================================================================

void uz::Renderer::setupTestScene() {
    DIAG("setupTestScene: creating PBR sphere showcase");

    // Generate sphere mesh CPU data.
    MeshData sphereData = generate_sphere(32, 16, 1.0f);

    // Load textures from assets/textures/.
    auto loadTex = [](const char* path, const char* name) -> RhiTexture {
        GpuTexture tex = load_texture_from_file(path, name);
        return tex.handle;
    };

    RhiTexture goldTex     = loadTex("assets/textures/gold_albedo.png",     "gold_albedo");
    RhiTexture rustTex     = loadTex("assets/textures/rust_albedo.png",     "rust_albedo");
    RhiTexture marbleTex   = loadTex("assets/textures/marble_albedo.png",   "marble_albedo");
    RhiTexture woodTex     = loadTex("assets/textures/wood_albedo.png",     "wood_albedo");
    RhiTexture copperTex   = loadTex("assets/textures/copper_albedo.png",   "copper_albedo");
    RhiTexture concreteTex = loadTex("assets/textures/concrete_albedo.png", "concrete_albedo");

    // Helper to create a sphere instance.
    auto addSphere = [&](const char* name, float x, float y, float z,
                         float r, float g, float b,
                         float metallic, float roughness,
                         RhiTexture tex = RHI_NULL) {
        GpuMesh gpu = create_gpu_mesh(sphereData);
        MeshInstance inst;
        inst.name      = name;
        inst.transform = Mat4::translate(Vec3{x, y, z, 0.0f});
        inst.mesh      = gpu;
        inst.albedo[0] = r;
        inst.albedo[1] = g;
        inst.albedo[2] = b;
        inst.metallic  = metallic;
        inst.roughness = roughness;
        inst.albedo_tex = tex;
        meshRenderer_.add_instance(std::move(inst));
    };

    // Row 1: Textured metals (y=3)
    addSphere("Gold",             -6, 3, 0,  1.0f, 0.76f, 0.33f, 1.0f, 0.15f, goldTex);
    addSphere("Copper",           -3, 3, 0,  0.95f, 0.64f, 0.54f, 1.0f, 0.2f,  copperTex);
    addSphere("Silver",            0, 3, 0,  0.97f, 0.96f, 0.91f, 1.0f, 0.1f);
    addSphere("Chrome",            3, 3, 0,  0.77f, 0.78f, 0.78f, 1.0f, 0.05f);
    addSphere("Titanium",          6, 3, 0,  0.54f, 0.50f, 0.47f, 1.0f, 0.3f);

    // Row 2: Textured dielectrics (y=0)
    addSphere("Marble",           -6, 0, 0,  0.93f, 0.90f, 0.87f, 0.0f, 0.1f,  marbleTex);
    addSphere("Wood",             -3, 0, 0,  0.42f, 0.26f, 0.15f, 0.0f, 0.5f,  woodTex);
    addSphere("Concrete",          0, 0, 0,  0.55f, 0.55f, 0.52f, 0.0f, 0.9f,  concreteTex);
    addSphere("Rust",              3, 0, 0,  0.45f, 0.22f, 0.12f, 0.0f, 0.85f, rustTex);
    addSphere("Iron",              6, 0, 0,  0.56f, 0.57f, 0.58f, 1.0f, 0.4f);

    // Row 3: Roughness gradient — same material, varying roughness (y=-3)
    for (int i = 0; i < 5; ++i) {
        float rough = 0.1f + 0.2f * static_cast<float>(i);
        float x = -6.0f + 3.0f * static_cast<float>(i);
        char buf[32];
        snprintf(buf, sizeof(buf), "Gold r=%.1f", rough);
        addSphere(buf, x, -3, 0, 1.0f, 0.76f, 0.33f, 1.0f, rough, goldTex);
    }

    DIAG_FMT("setupTestScene: %zu sphere instances",
             meshRenderer_.instances().size());
}
