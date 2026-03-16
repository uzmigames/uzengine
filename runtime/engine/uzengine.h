/// UzEngine — Single umbrella header for engine consumers.
///
/// Include this one header to access all public engine APIs:
/// ```cpp
/// #include "uzengine.h"
/// ```
///
/// Platform and RHI backend headers are included conditionally via
/// `_WIN32`, `__APPLE__`, `__linux__` and `UZ_BACKEND_*` defines.
/// ECS headers require EnTT — define `UZ_HAS_ENTT` before including
/// if EnTT is in your include path.

#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Core Systems
// ─────────────────────────────────────────────────────────────────────────────
#include "log.h"
#include "engine_assert.h"
#include "config.h"
#include "paths.h"
#include "profiler.h"
#include "profile_capture.h"
#include "random.h"

// ─────────────────────────────────────────────────────────────────────────────
// Math
// ─────────────────────────────────────────────────────────────────────────────
#include "math_defs.h"
#include "math_funcs.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "mat4.h"
#include "quaternion.h"
#include "transform.h"
#include "color.h"
#include "geometry.h"
#include "basis.h"
#include "rect2.h"

// ─────────────────────────────────────────────────────────────────────────────
// Containers
// ─────────────────────────────────────────────────────────────────────────────
#include "containers.h"
#include "handle_map.h"
#include "free_list.h"
#include "ring_buffer.h"
#include "frame_array.h"
#include "fnv_hash.h"

// ─────────────────────────────────────────────────────────────────────────────
// Memory
// ─────────────────────────────────────────────────────────────────────────────
#include "allocators.h"
#include "ownership.h"

// ─────────────────────────────────────────────────────────────────────────────
// Event System
// ─────────────────────────────────────────────────────────────────────────────
#include "delegate.h"
#include "event_queue.h"

// ─────────────────────────────────────────────────────────────────────────────
// Application Framework
// ─────────────────────────────────────────────────────────────────────────────
#include "time_manager.h"
#include "subsystem.h"
#include "app_config.h"
#include "application.h"

// ─────────────────────────────────────────────────────────────────────────────
// Platform
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include "platform_window.h"
#include "input.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// RHI — Public Interface
// ─────────────────────────────────────────────────────────────────────────────
#include "rhi.h"
#include "rhi_types.h"
#include "rhi_config.h"
#include "rhi_backend.h"
#include "rhi_pso_cache.h"
#include "rhi_resource_pool.h"
#include "rhi_frame_limiter.h"

// ─────────────────────────────────────────────────────────────────────────────
// RHI — DX12 Backend
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include "dx12_device.h"
#include "dx12_command_list.h"
#include "dx12_swapchain.h"
#include "dx12_fence.h"
#include "dx12_resources.h"
#include "dx12_descriptor_allocator.h"
#include "dx12_resource_creation.h"
#include "dx12_gpu_profiler.h"
#include "dx12_gpu_breadcrumbs.h"
#include "dx12_compat.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// RHI — DX11 Backend
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include "dx11_device.h"
#include "dx11_command_list.h"
#include "dx11_swapchain.h"
#include "dx11_fence.h"
#include "dx11_resources.h"
#include "dx11_gpu_profiler.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// RHI — Vulkan Backend
// ─────────────────────────────────────────────────────────────────────────────
#if defined(_WIN32) || defined(__linux__)
#include "vulkan_device.h"
#include "vulkan_command_list.h"
#include "vulkan_swapchain.h"
#include "vulkan_fence.h"
#include "vulkan_resources.h"
#include "vulkan_gpu_profiler.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// RHI — Metal Backend
// ─────────────────────────────────────────────────────────────────────────────
#ifdef __APPLE__
#include "metal_device.h"
#include "metal_command_list.h"
#include "metal_swapchain.h"
#include "metal_fence.h"
#include "metal_resources.h"
#include "metal_gpu_profiler.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Shader System
// ─────────────────────────────────────────────────────────────────────────────
#include "engine_shaders.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "shader_pipeline.h"
#include "shader_cache.h"
#include "shader_hot_reload.h"
#include "shader_permutation.h"
#include "shader_reflection.h"
#include "shader_source_manager.h"
#include "shader_vfs.h"
#include "shader_archive.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Camera
// ─────────────────────────────────────────────────────────────────────────────
#include "camera.h"
#include "fps_camera_controller.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Core
// ─────────────────────────────────────────────────────────────────────────────
#include "shader_manager.h"
#include "pipeline_library.h"
#include "gbuffer.h"
#include "constant_buffers.h"
#include "cb_ring_allocator.h"
#include "draw_list.h"
#include "multi_light.h"
#include "scene_lighting.h"
#include "render_path.h"
#include "root_slots.h"
#include "render_world.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Mesh
// ─────────────────────────────────────────────────────────────────────────────
#include "mesh.h"
#include "gpu_mesh.h"
#include "vertex.h"
#include "vertex_layout.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Upload
// ─────────────────────────────────────────────────────────────────────────────
#include "texture_loader.h"
#include "upload_context.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Material
// ─────────────────────────────────────────────────────────────────────────────
#include "material.h"
#include "material_buffer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Passes
// ─────────────────────────────────────────────────────────────────────────────
#include "pbr_pass.h"
#include "gbuffer_pass.h"
#include "gbuffer_debug_pass.h"
#include "debug_view_pass.h"
#include "lighting_pass.h"
#include "velocity_pass.h"
#include "shadow_pass.h"
#include "postprocess_stack.h"
#include "postprocess_math.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Shadows
// ─────────────────────────────────────────────────────────────────────────────
#include "shadow_cascade.h"
#include "shadow_atlas.h"

// ─────────────────────────────────────────────────────────────────────────────
// Renderer — Render Graph
// ─────────────────────────────────────────────────────────────────────────────
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "render_graph_resources.h"
#include "render_graph_types.h"

// ─────────────────────────────────────────────────────────────────────────────
// Asset System
// ─────────────────────────────────────────────────────────────────────────────
#include "vfs.h"
#include "asset_cache.h"
#include "asset_manifest.h"
#include "import_sidecar.h"

// ─────────────────────────────────────────────────────────────────────────────
// Asset Formats
// ─────────────────────────────────────────────────────────────────────────────
#include "pak_format.h"
#include "uzmesh_format.h"
#include "uzshader_format.h"
#include "uztexture_format.h"

// ─────────────────────────────────────────────────────────────────────────────
// Asset Importers
// ─────────────────────────────────────────────────────────────────────────────
#include "asset_importer.h"
#include "asset_registry.h"
#include "mesh_importer.h"
#include "shader_importer.h"
#include "texture_importer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Property System
// ─────────────────────────────────────────────────────────────────────────────
#include "property_types.h"
#include "property_macros.h"
#include "property_info.h"
#include "property_serializer.h"
#include "class_info.h"
#include "class_registry.h"

// ─────────────────────────────────────────────────────────────────────────────
// ECS (requires EnTT — define UZ_HAS_ENTT before including uzengine.h)
// ─────────────────────────────────────────────────────────────────────────────
#ifdef UZ_HAS_ENTT
#include "entity.h"
#include "component.h"
#include "world.h"
#include "world_subsystem.h"
#include "camera_component.h"
#include "light_component.h"
#include "primitive_component.h"
#include "scene_component.h"
#include "static_mesh_component.h"
#include "tick_system.h"
#include "tick_subsystem.h"
#include "tickable_component.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Job System (lightweight types only — include job_system.h for full API)
// ─────────────────────────────────────────────────────────────────────────────
#include "job_types.h"

// ─────────────────────────────────────────────────────────────────────────────
// Editor Utilities
// ─────────────────────────────────────────────────────────────────────────────
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "stats_overlay.h"
#include "bitmap_font.h"
#include "text_renderer.h"
