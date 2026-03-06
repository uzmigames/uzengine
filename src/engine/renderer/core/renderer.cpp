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
#include "engine/renderer/resources/mesh_primitives.h"
#include "engine/renderer/resources/texture.h"
#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/scene/scene.h"
#include "engine/scene/component_data.h"

namespace uz {

// ===========================================================================
// Public API
// ===========================================================================

bool Renderer::init(void* window_handle, uint32_t width, uint32_t height) {
    if (initialized_) return true;

    width_  = width;
    height_ = height;

    // Register the DX12 backend before using rhi().
    rhi_set_backend(&rhi_dx12_backend);
    DIAG("=== renderer.init START ===");

    // Initialize the RHI backend (device, swapchain, heaps, cmd queue).
    DIAG("rhi()->init()...");
    if (!rhi()->init(window_handle, width, height)) {
        DIAG("FATAL: rhi()->init() returned false");
        return false;
    }
    DIAG("rhi()->init() OK");

    // Initialize shader library with builtin shaders.
    DIAG("shaderLib_.register_builtin_shaders()...");
    shaderLib_.register_builtin_shaders();

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

    return true;
}

void Renderer::update(double /*totalTime*/, double deltaTime) {
    fpsOverlay_.update(deltaTime);

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

    // Use fly-cam by default for editor control.
    const float dt = std::min(static_cast<float>(deltaTime), 0.1f);
    
    // Fly-cam reads position/yaw/pitch from sceneParams_, which is pre-synced
    // from the [editor] section of the .uzscene file by scene_bridge.cpp.
    CameraState camState = camera_update_fly(sceneParams_, aspect, dt);
    viewProj_ = camState.view_proj;

    // Collect primary directional light from the scene.
    cachedLight_ = light_collect_primary(scene_);

    // -----------------------------------------------------------------------
    // Sync Scene Proxies (§COMPONENT-INTERFACE) — Incremental Strategy
    // -----------------------------------------------------------------------
    if (scene_) {
        // Detect if the proxy list needs a full rebuild.
        // Triggers: first time, node count changed, or explicit dirty flag.
        const uint32_t current_count = scene_->node_count;
        if (scene_dirty_ || current_count != last_node_count_ || proxies_.empty()) {
            // Full rebuild — but iterate only live slots, not all 65536.
            for (auto* p : proxies_) delete p;
            proxies_.clear();
            proxy_node_ids_.clear();

            component_store_register<StaticMeshData>(&scene_->components);

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

                // Load material and mesh ONCE at proxy creation.
                if (data->material_path[0] != '\0') {
                    std::string resolved = resolve_path(data->material_path);
                    proxy->material = materialManager_.load(resolved);
                    if (!proxy->material) {
                        DIAG_FMT("Renderer: FAILED to load material '%s' (resolved='%s')",
                                 data->material_path, resolved.c_str());
                    }
                }

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

                // Cache material parameters into flat fields (no map lookups per frame).
                proxy->cache_material();

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
}

void Renderer::render(bool readback) {
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

    // --- Pass 1: Scene Draw ------------------------------------------------
    // Clears and draws the scene into the backbuffer + depth.
    renderGraph_.add_pass("scene_draw", RgPassType::Graphics,
        [&](RgPassBuilder& builder) {
            builder.write(rgBackbuffer);
            builder.write(rgDepth);
            builder.force_run();
        },
        [this](RhiCmdList cmd) {
            rhi()->cmd_set_pipeline(cmd, rhiPipeline_);

            rhi()->cmd_set_viewport(cmd, 0.0f, 0.0f,
                                   static_cast<float>(width_),
                                   static_cast<float>(height_),
                                   0.0f, 1.0f);
            rhi()->cmd_set_scissor(cmd, 0, 0, width_, height_);

            RhiTexture bb = rhi()->swapchain_backbuffer();

            rhi()->cmd_barrier_texture(cmd, bb,
                                       RHI_STATE_PRESENT, RHI_STATE_RENDER_TARGET);

            rhi()->cmd_clear_render_target(cmd, bb,
                                           clearR_, clearG_, clearB_, 1.0f);
            rhi()->cmd_clear_depth_stencil(cmd, rhiDepthBuf_, 1.0f, 0);

            rhi()->cmd_set_render_targets(cmd, &bb, 1, rhiDepthBuf_);

            // Light data via root constants.
            struct {
                float dir[3]; float intensity;
                float color[3]; float _pad;
            } lightCB;
            lightCB.dir[0]    = cachedLight_.direction.x;
            lightCB.dir[1]    = cachedLight_.direction.y;
            lightCB.dir[2]    = cachedLight_.direction.z;
            lightCB.intensity = cachedLight_.intensity;
            lightCB.color[0]  = cachedLight_.color.x;
            lightCB.color[1]  = cachedLight_.color.y;
            lightCB.color[2]  = cachedLight_.color.z;
            lightCB._pad      = 0.0f;
            rhi()->cmd_set_root_constants(cmd, 2, &lightCB, 8, 0);

            // --- Draw PBR meshes FIRST (UE5/Godot pattern) ---
            // Meshes must render before editor overlays so gizmos
            // draw on top (transform gizmo uses no-depth PSO).
            rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

            // Draw PBR meshes (spheres with materials).
            meshRenderer_.draw(cmd, viewProj_);

            // Draw new Scene Proxies
            if (rhiPbrPipeline_ != RHI_NULL) {
                rhi()->cmd_set_pipeline(cmd, rhiPbrPipeline_);
                for (auto* proxy : proxies_) {
                    proxy->execute(cmd, frameConstants_);
                }
            }

            // --- Selection Outline (stencil mark + inflated mesh) ---
            {
                uint32_t selNode = sceneParams_
                    ? sceneParams_->selectedNodeId.load(std::memory_order_relaxed) : 0;

                if (selNode != 0 && rhiStencilMarkPSO_ != RHI_NULL && rhiOutlinePSO_ != RHI_NULL) {
                    // Find the proxy matching the selected node
                    MeshProxy* selProxy = nullptr;
                    for (auto* proxy : proxies_) {
                        auto* mp = static_cast<MeshProxy*>(proxy);
                        if (mp->node_id == selNode) {
                            selProxy = mp;
                            break;
                        }
                    }

                    if (selProxy && selProxy->mesh && selProxy->mesh->vertex_buffer != RHI_NULL) {
                        // Pass 1: Stencil Mark — write 1 to stencil, no color output
                        rhi()->cmd_set_pipeline(cmd, rhiStencilMarkPSO_);
                        rhi()->cmd_set_stencil_ref(cmd, 1);
                        rhi()->cmd_set_vertex_buffer(cmd, 0, selProxy->mesh->vertex_buffer, 0, selProxy->mesh->vertex_stride);
                        rhi()->cmd_set_index_buffer(cmd, selProxy->mesh->index_buffer, 0, true);
                        rhi()->cmd_set_root_constants(cmd, 0, &selProxy->transform, 16, 0);
                        rhi()->cmd_set_root_constants(cmd, 3, &frameConstants_.view_proj, 16, 0);
                        for (const auto& sub : selProxy->mesh->submeshes) {
                            rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
                        }

                        // Pass 2: Outline Draw — inflated mesh, stencil test != 1
                        rhi()->cmd_set_pipeline(cmd, rhiOutlinePSO_);
                        rhi()->cmd_set_stencil_ref(cmd, 1);

                        // b0: world matrix
                        rhi()->cmd_set_root_constants(cmd, 0, &selProxy->transform, 16, 0);

                        // b1: outline params — color (slot 0-3) + width (slot 4-7)
                        float outlineCB[12] = {
                            1.0f, 0.55f, 0.0f, 1.0f,  // Outline color: orange #FF8C00
                            0.03f, 0.0f, 0.0f, 0.0f,  // Outline width (model space units)
                            0.0f, 0.0f, 0.0f, 0.0f
                        };
                        rhi()->cmd_set_root_constants(cmd, 1, outlineCB, 12, 0);

                        // b3: viewProj
                        rhi()->cmd_set_root_constants(cmd, 3, &frameConstants_.view_proj, 16, 0);

                        for (const auto& sub : selProxy->mesh->submeshes) {
                            rhi()->cmd_draw_indexed(cmd, sub.index_count, 1, sub.index_offset, 0, 0);
                        }

                        // Restore PBR pipeline for subsequent draws
                        rhi()->cmd_set_pipeline(cmd, rhiPbrPipeline_);
                    }
                }
            }

            // --- Editor overlays AFTER scene geometry ---
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
                if (sceneParams_) {
                    camPos = {sceneParams_->camPosX, sceneParams_->camPosY, sceneParams_->camPosZ};
                }

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

void Renderer::shutdown() {
    if (!initialized_) return;

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

    rhi()->shutdown();
    initialized_ = false;
}

// ===========================================================================
// Private -- pipeline state via RHI
// ===========================================================================

bool Renderer::createPipelineStates() {
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
    desc.rasterizer.cull_mode = RHI_CULL_BACK;

    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = true;
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS;

    desc.blend.render_targets[0].write_mask = 0xF;
    desc.debug_name = "BasicPSO";

    rhiPipeline_ = shaderLib_.get_or_create_pipeline("basic", desc);
    DIAG_FMT("basic PSO => handle=%llu", (unsigned long long)rhiPipeline_);

    // PBR PSO for Scene Proxies
    RhiPipelineDesc pbrDesc = desc; // Start with basic and adjust
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

    // ---- Outline PSO (inflated mesh, stencil test != 1, cull front) ----
    {
        RhiPipelineDesc olDesc = pbrDesc;
        olDesc.rasterizer.cull_mode = RHI_CULL_FRONT; // Cull front faces for inflation
        olDesc.depth_stencil.depth_enable = false;
        olDesc.depth_stencil.depth_write  = false;
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
    desc.usage      = RHI_TEXTURE_USAGE_DEPTH_STENCIL;
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
// Private -- GPU -> CPU readback
// ===========================================================================

void Renderer::performReadback() {
    ++readbackFenceValue_;
    rhi()->fence_signal(rhiReadbackFence_, readbackFenceValue_);
    rhi()->fence_wait(rhiReadbackFence_, readbackFenceValue_, UINT64_MAX);

    const uint32_t actualRowBytes = width_ * 4;
    const uint32_t alignedPitch   = (actualRowBytes + 255u) & ~255u;

    void* mappedData = rhi()->buffer_map(rhiReadbackBufs_[frameIndex_]);
    if (!mappedData) return;

    const uint64_t timestamp = streamTimer_.elapsedMicros();

    {
        std::lock_guard<std::mutex> lock(frameMutex_);

        latestFrame_.width       = width_;
        latestFrame_.height      = height_;
        latestFrame_.frameNumber = ++frameNumber_;
        latestFrame_.timestamp   = timestamp;

        latestFrame_.pixels.resize(
            static_cast<size_t>(actualRowBytes) * height_);

        const auto* src = static_cast<const uint8_t*>(mappedData);
        uint8_t*    dst = latestFrame_.pixels.data();

        for (uint32_t row = 0; row < height_; ++row) {
            std::memcpy(dst, src, actualRowBytes);
            src += alignedPitch;
            dst += actualRowBytes;
        }

        fpsOverlay_.renderInto(latestFrame_.pixels.data(),
                               width_, height_, actualRowBytes);
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

void Renderer::setScene(Scene* scene) {
    scene_ = scene;
    scene_dirty_ = true;
}

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
