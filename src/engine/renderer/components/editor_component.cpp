// editor_component.cpp — Editor-only rendering: ground grid + gizmos.
//
// Gizmo drawing is fully delegated to the GizmoRegistry. To add a new
// gizmo type, create a class implementing GizmoRenderer and register it
// in init() below. No other changes needed.
//
// RULE RHI-1: No backend types above rhi.h — uses RHI API only.

#include "editor_component.h"

#include "engine/renderer/core/renderer.h"  // For Vertex struct

// Gizmo types — each is a self-contained GizmoRenderer implementation.
#include "engine/renderer/gizmos/gizmo_cube.h"
#include "engine/renderer/gizmos/gizmo_camera.h"
#include "engine/renderer/gizmos/gizmo_directional_light.h"
#include "engine/renderer/gizmos/gizmo_point_light.h"

#include "engine/scene/component_registry.h"

#include <cstring>
#include <cstdio>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>

namespace {

// Resolve the shader directory relative to the executable.
inline std::string getShaderDir() {
    char exePath[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string dir(exePath);
    auto slash = dir.find_last_of("\\/");
    if (slash != std::string::npos) dir = dir.substr(0, slash);
    // Path relative to build/Debug/ or build/Release/
    return dir + "/../../src/engine/renderer/shaders/";
}

// Read an entire file into a string. Returns empty on failure.
inline std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::in);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // anonymous namespace

namespace uz {

// ============================================================================
// Initialization
// ============================================================================

bool EditorComponent::init() {
    if (!createLinePipelineState()) return false;
    if (!createGridVertexBuffer()) return false;

    // -- Register gizmos keyed by ComponentRegistry type_index. ---------------
    // type_index is assigned at registration time in component_registry.cpp.
    // Using the registry ensures new types registered by plugins work automatically.
    gizmoRegistry_.setDefaultGizmo(std::make_unique<GizmoCube>());
    auto regGizmo = [&](const char* typeName, std::unique_ptr<GizmoRenderer> g) {
        const NodeTypeDef* def = ComponentRegistry::find(typeName);
        if (def) gizmoRegistry_.registerGizmo(def->type_index, std::move(g));
    };
    regGizmo("DirectionalLight", std::make_unique<GizmoDirectionalLight>());
    regGizmo("PointLight",       std::make_unique<GizmoPointLight>());
    regGizmo("Camera",           std::make_unique<GizmoCamera>());

    gizmoRegistry_.initAll();
    transformGizmo_.init();

    if (!createSolidPipelineState()) return false;
    if (!createGizmoNoDepthPSO()) return false;
    return true;
}

// ============================================================================
// Draw
// ============================================================================

void EditorComponent::draw(RhiCmdList cmd,
                           const Mat4& viewProj,
                           Float3 cameraPos,
                           uint32_t selectedNodeId,
                           int gizmoMode,
                           int gizmoActiveAxis,
                           bool usePivot,
                           Float3 pivotPos) {
    // -- Ground grid --
    if (linePipeline_ == RHI_NULL || gridVertexBuffer_ == RHI_NULL || gridVertexCount_ == 0)
        return;
    rhi()->cmd_set_pipeline(cmd, linePipeline_);
    rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_LINE_LIST);

    rhi()->cmd_set_root_constants(cmd, 0, &viewProj, 16, 0);

    // Pass 8 DWORDs: colorMul(3) + alphaMult(1) + cameraWorldPos(3) + gridFadeEnd(1)
    // gridFadeEnd=50 → grid fades from 15 units to 50 units (UE5-style horizon fade).
    const float gridCB[8] = {
        1.0f, 1.0f, 1.0f, 1.0f,                          // colorMul + alphaMult
        cameraPos.x, cameraPos.y, cameraPos.z, 50.0f     // cameraWorldPos + gridFadeEnd
    };
    rhi()->cmd_set_root_constants(cmd, 1, gridCB, 8, 0);

    rhi()->cmd_set_vertex_buffer(cmd, 0, gridVertexBuffer_, 0, sizeof(Vertex));
    rhi()->cmd_draw(cmd, gridVertexCount_, 1, 0, 0);

    // -- Per-node gizmos (dispatched via registry) --
    Float3 selectedPos{0.0f, 0.0f, 0.0f};
    bool foundSelected = false;
    {
        std::lock_guard<std::mutex> lk(editorNodesMutex_);
        for (const auto& node : editorNodes_) {
            // Build world matrix from quaternion rotation (PR: authoritative rotation).
            // Guard: if quaternion is zero/degenerate (uninitialized), use identity.
            Quat rq = node.rotationQuat;
            float lenSq = rq.x*rq.x + rq.y*rq.y + rq.z*rq.z + rq.w*rq.w;
            Quat rotQ;
            if (lenSq < 1e-12f) {
                rotQ = {0.0f, 0.0f, 0.0f, 1.0f}; // identity quaternion
            } else {
                rotQ = {rq.x, rq.y, rq.z, rq.w};
            }

            const Mat4 s = Mat4::scale({node.scale.x, node.scale.y, node.scale.z, 0.0f});
            const Mat4 r = Mat4::rotate(rotQ);
            const Mat4 t = Mat4::translate({node.position.x, node.position.y, node.position.z, 0.0f});
            // Column-major: rightmost applied first → scale, then rotate, then translate
            const Mat4 nodeWorld = t * r * s;

            // Column-major: viewProj applied after nodeWorld → correct clip space
            const Mat4 mvp = viewProj * nodeWorld;
            rhi()->cmd_set_root_constants(cmd, 0, &mvp, 16, 0);

            // 8 DWORDs: colorMul + alphaMult + cameraWorldPos + gridFadeEnd=0 (no fade)
            const float col[8] = {
                node.color.x, node.color.y, node.color.z, 1.0f,
                0.0f, 0.0f, 0.0f, 0.0f   // gridFadeEnd=0 disables distance fade
            };
            rhi()->cmd_set_root_constants(cmd, 1, col, 8, 0);

            // Lookup and draw the correct gizmo for this node type.
            GizmoRenderer* gizmo = gizmoRegistry_.get(node.nodeType);
            if (gizmo) {
                gizmo->draw(cmd);
            }

            // Track selected node position
            if (selectedNodeId > 0 && node.nodeId == selectedNodeId) {
                selectedPos = node.position;
                foundSelected = true;
            }
        }
    }

    // -- Transform gizmo for selected node (no-depth PSO: always on top) --
    // Task 7.2: When usePivot is true, draw gizmo at pivot (multi-select centroid)
    // even if we didn't find a selectedNode in editorNodes_ (because the selection
    // may span nodes not in the current editorNodes_ list). In that case we draw
    // at the supplied pivotPos as long as selectedNodeId > 0.
    bool drawGizmo = (foundSelected && selectedNodeId > 0)
                  || (usePivot && selectedNodeId > 0);
    if (drawGizmo) {
        Float3 gizmoOrigin = usePivot ? pivotPos : selectedPos;

        // Task 1.3: Use depth-disabled PSO so gizmo is never occluded by scene.
        rhi()->cmd_set_pipeline(cmd, gizmoNoDepthPipeline_);
        rhi()->cmd_set_topology(cmd, RHI_TOPOLOGY_TRIANGLE_LIST);

        // Task 1.5: Scale gizmo proportional to camera distance so it stays
        // a constant apparent size regardless of how far the camera is.
        float dx = gizmoOrigin.x - cameraPos.x;
        float dy = gizmoOrigin.y - cameraPos.y;
        float dz = gizmoOrigin.z - cameraPos.z;
        float dist = sqrtf(dx*dx + dy*dy + dz*dz);
        float gizmoScale = std::max(dist * 0.1f, 0.1f);

        // Position at gizmo origin, uniform scale by camera distance.
        const Mat4 gS = Mat4::scale({gizmoScale, gizmoScale, gizmoScale, 0.0f});
        const Mat4 gT = Mat4::translate({gizmoOrigin.x, gizmoOrigin.y, gizmoOrigin.z, 0.0f});
        // Column-major: translate then scale (gizmo at origin, scaled)
        const Mat4 gizmoWorld = gT * gS;

        const Mat4 mvp = viewProj * gizmoWorld;
        rhi()->cmd_set_root_constants(cmd, 0, &mvp, 16, 0);

        const float gizmoCol[8] = {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        rhi()->cmd_set_root_constants(cmd, 1, gizmoCol, 8, 0);

        transformGizmo_.drawMode(cmd, gizmoMode, gizmoActiveAxis);

        // Draw free rotation ring (billboarded) when in rotate mode
        if (gizmoMode == 1 && dist > kEpsilon) {
            // Build billboard rotation: ring is in XY plane, rotate so Z faces camera
            Vec3 toCamera = {dx / dist, dy / dist, dz / dist, 0.0f};
            // Build orthonormal basis with toCamera as the new Z
            Vec3 worldUp = (fabsf(toCamera.y) > 0.99f)
                ? Vec3{1.0f, 0.0f, 0.0f, 0.0f}
                : Vec3{0.0f, 1.0f, 0.0f, 0.0f};
            Vec3 bbCross = vec3_cross(worldUp, toCamera);
            float bbCrossLen = vec3_len(bbCross);
            if (bbCrossLen > kEpsilon) {
                Vec3 bbRight = bbCross * (1.0f / bbCrossLen);
                Vec3 bbUp = vec3_cross(toCamera, bbRight);

                // Row-major billboard rotation matrix
                Mat4 bbRot = Mat4::identity();
                bbRot.m[0][0] = bbRight.x;   bbRot.m[0][1] = bbRight.y;   bbRot.m[0][2] = bbRight.z;
                bbRot.m[1][0] = bbUp.x;      bbRot.m[1][1] = bbUp.y;      bbRot.m[1][2] = bbUp.z;
                bbRot.m[2][0] = toCamera.x;  bbRot.m[2][1] = toCamera.y;  bbRot.m[2][2] = toCamera.z;

                // Column-major: translate, then billboard-rotate, then scale
                const Mat4 bbWorld = gT * bbRot * gS;
                const Mat4 bbMvp = viewProj * bbWorld;
                rhi()->cmd_set_root_constants(cmd, 0, &bbMvp, 16, 0);

                transformGizmo_.drawFreeRing(cmd, gizmoActiveAxis == 3);
            }
        }
    }
}

// ============================================================================
// Load editor nodes
// ============================================================================

size_t EditorComponent::loadNodes(std::vector<EditorNode> nodes) {
    const size_t count = nodes.size();
    {
        std::lock_guard<std::mutex> lk(editorNodesMutex_);
        editorNodes_ = std::move(nodes);
    }
    return count;
}

std::vector<EditorComponent::EditorNode> EditorComponent::getNodes() const {
    std::lock_guard<std::mutex> lk(editorNodesMutex_);
    return editorNodes_;
}

// ============================================================================
// Shutdown
// ============================================================================

void EditorComponent::shutdown() {
    gizmoRegistry_.shutdownAll();
    transformGizmo_.shutdown();
    if (gridVertexBuffer_ != RHI_NULL)      { rhi()->buffer_destroy(gridVertexBuffer_); gridVertexBuffer_ = RHI_NULL; }
    if (linePipeline_ != RHI_NULL)          { rhi()->pipeline_destroy(linePipeline_); linePipeline_ = RHI_NULL; }
    if (solidPipeline_ != RHI_NULL)         { rhi()->pipeline_destroy(solidPipeline_); solidPipeline_ = RHI_NULL; }
    if (gizmoNoDepthPipeline_ != RHI_NULL)  { rhi()->pipeline_destroy(gizmoNoDepthPipeline_); gizmoNoDepthPipeline_ = RHI_NULL; }
}

// ============================================================================
// Private — Line PSO creation via RHI
// ============================================================================

bool EditorComponent::createLinePipelineState() {
    const std::string shaderDir = getShaderDir();

    // Read shader source files (Phase 1: HLSL as text in spirv_data)
    const std::string vsSrc = readFile(shaderDir + "basic.vs.hlsl");
    const std::string psSrc = readFile(shaderDir + "basic.ps.hlsl");

    if (vsSrc.empty()) {
        OutputDebugStringA("[UzEngine] Failed to read basic.vs.hlsl for editor line PSO\n");
        return false;
    }
    if (psSrc.empty()) {
        OutputDebugStringA("[UzEngine] Failed to read basic.ps.hlsl for editor line PSO\n");
        return false;
    }

    RhiPipelineDesc desc = {};

    // Vertex shader
    desc.vertex_shader.spirv_data  = vsSrc.c_str();
    desc.vertex_shader.spirv_size  = vsSrc.size();
    desc.vertex_shader.entry_point = "VSMain";

    // Pixel shader
    desc.pixel_shader.spirv_data   = psSrc.c_str();
    desc.pixel_shader.spirv_size   = psSrc.size();
    desc.pixel_shader.entry_point  = "PSMain";

    // Vertex layout
    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0].semantic_name  = "POSITION";
    desc.vertex_layout.attribs[0].semantic_index = 0;
    desc.vertex_layout.attribs[0].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[0].offset         = 0;
    desc.vertex_layout.attribs[0].buffer_slot    = 0;

    desc.vertex_layout.attribs[1].semantic_name  = "COLOR";
    desc.vertex_layout.attribs[1].semantic_index = 0;
    desc.vertex_layout.attribs[1].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[1].offset         = sizeof(Float3);
    desc.vertex_layout.attribs[1].buffer_slot    = 0;

    // Render targets
    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;

    // States
    desc.rasterizer.fill_mode = RHI_FILL_WIREFRAME;
    desc.rasterizer.cull_mode = RHI_CULL_NONE;
    desc.rasterizer.front_ccw = false;

    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = false;  // transparent lines must not occlude scene
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS;

    // Alpha blend: src_alpha / inv_src_alpha — enables UE5-style grid horizon fade
    desc.blend.render_targets[0].blend_enable     = true;
    desc.blend.render_targets[0].src_blend        = RHI_BLEND_SRC_ALPHA;
    desc.blend.render_targets[0].dst_blend        = RHI_BLEND_INV_SRC_ALPHA;
    desc.blend.render_targets[0].blend_op         = RHI_BLEND_OP_ADD;
    desc.blend.render_targets[0].src_blend_alpha  = RHI_BLEND_ONE;
    desc.blend.render_targets[0].dst_blend_alpha  = RHI_BLEND_ZERO;
    desc.blend.render_targets[0].blend_op_alpha   = RHI_BLEND_OP_ADD;
    desc.blend.render_targets[0].write_mask       = 0xF;

    desc.topology    = RHI_TOPOLOGY_LINE_LIST;
    desc.debug_name  = "EditorLinePSO";

    linePipeline_ = rhi()->pipeline_create(&desc);
    if (linePipeline_ == RHI_NULL) {
        OutputDebugStringA("[UzEngine] Failed to create editor line PSO\n");
        return false;
    }
    return true;
}

// ============================================================================
// Private — Solid triangle PSO for transform gizmo handles
// ============================================================================

bool EditorComponent::createSolidPipelineState() {
    const std::string shaderDir = getShaderDir();

    const std::string vsSrc = readFile(shaderDir + "basic.vs.hlsl");
    const std::string psSrc = readFile(shaderDir + "basic.ps.hlsl");

    if (vsSrc.empty() || psSrc.empty()) {
        OutputDebugStringA("[UzEngine] Failed to read shaders for editor solid PSO\n");
        return false;
    }

    RhiPipelineDesc desc = {};

    desc.vertex_shader.spirv_data  = vsSrc.c_str();
    desc.vertex_shader.spirv_size  = vsSrc.size();
    desc.vertex_shader.entry_point = "VSMain";

    desc.pixel_shader.spirv_data   = psSrc.c_str();
    desc.pixel_shader.spirv_size   = psSrc.size();
    desc.pixel_shader.entry_point  = "PSMain";

    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0].semantic_name  = "POSITION";
    desc.vertex_layout.attribs[0].semantic_index = 0;
    desc.vertex_layout.attribs[0].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[0].offset         = 0;
    desc.vertex_layout.attribs[0].buffer_slot    = 0;

    desc.vertex_layout.attribs[1].semantic_name  = "COLOR";
    desc.vertex_layout.attribs[1].semantic_index = 0;
    desc.vertex_layout.attribs[1].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[1].offset         = sizeof(Float3);
    desc.vertex_layout.attribs[1].buffer_slot    = 0;

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;

    desc.rasterizer.fill_mode = RHI_FILL_SOLID;
    desc.rasterizer.cull_mode = RHI_CULL_NONE;
    desc.rasterizer.front_ccw = false;

    desc.depth_stencil.depth_enable = true;
    desc.depth_stencil.depth_write  = true;
    desc.depth_stencil.depth_func   = RHI_COMPARE_LESS_EQUAL;

    desc.topology    = RHI_TOPOLOGY_TRIANGLE_LIST;
    desc.debug_name  = "EditorSolidPSO";

    solidPipeline_ = rhi()->pipeline_create(&desc);
    if (solidPipeline_ == RHI_NULL) {
        OutputDebugStringA("[UzEngine] Failed to create editor solid PSO\n");
        return false;
    }
    return true;
}

// ============================================================================
// Private — Gizmo no-depth PSO (always renders on top)
// ============================================================================

bool EditorComponent::createGizmoNoDepthPSO() {
    const std::string shaderDir = getShaderDir();

    const std::string vsSrc = readFile(shaderDir + "basic.vs.hlsl");
    const std::string psSrc = readFile(shaderDir + "basic.ps.hlsl");

    if (vsSrc.empty() || psSrc.empty()) {
        OutputDebugStringA("[UzEngine] Failed to read shaders for gizmo no-depth PSO\n");
        return false;
    }

    RhiPipelineDesc desc = {};

    desc.vertex_shader.spirv_data  = vsSrc.c_str();
    desc.vertex_shader.spirv_size  = vsSrc.size();
    desc.vertex_shader.entry_point = "VSMain";

    desc.pixel_shader.spirv_data   = psSrc.c_str();
    desc.pixel_shader.spirv_size   = psSrc.size();
    desc.pixel_shader.entry_point  = "PSMain";

    desc.vertex_layout.attrib_count = 2;
    desc.vertex_layout.attribs[0].semantic_name  = "POSITION";
    desc.vertex_layout.attribs[0].semantic_index = 0;
    desc.vertex_layout.attribs[0].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[0].offset         = 0;
    desc.vertex_layout.attribs[0].buffer_slot    = 0;

    desc.vertex_layout.attribs[1].semantic_name  = "COLOR";
    desc.vertex_layout.attribs[1].semantic_index = 0;
    desc.vertex_layout.attribs[1].format         = RHI_VERTEX_FORMAT_FLOAT3;
    desc.vertex_layout.attribs[1].offset         = sizeof(Float3);
    desc.vertex_layout.attribs[1].buffer_slot    = 0;

    desc.rt_count      = 1;
    desc.rt_formats[0] = RHI_FORMAT_RGBA8_UNORM;
    desc.ds_format     = RHI_FORMAT_D24_UNORM_S8_UINT;

    desc.rasterizer.fill_mode     = RHI_FILL_SOLID;
    desc.rasterizer.cull_mode     = RHI_CULL_NONE;
    desc.rasterizer.front_ccw     = false;

    // Depth test disabled — gizmo renders on top of all scene geometry.
    desc.depth_stencil.depth_enable = false;
    desc.depth_stencil.depth_write  = false;
    desc.depth_stencil.depth_func   = RHI_COMPARE_ALWAYS;

    desc.topology    = RHI_TOPOLOGY_TRIANGLE_LIST;
    desc.debug_name  = "GizmoNoDepthPSO";

    gizmoNoDepthPipeline_ = rhi()->pipeline_create(&desc);
    if (gizmoNoDepthPipeline_ == RHI_NULL) {
        OutputDebugStringA("[UzEngine] Failed to create gizmo no-depth PSO\n");
        return false;
    }
    return true;
}

// ============================================================================
// Private — Grid vertex buffer
// ============================================================================

bool EditorComponent::createGridVertexBuffer() {
    constexpr int   kHalfExtent = 100;
    constexpr int   kLineCount  = 2 * kHalfExtent + 1;
    constexpr int   kTotalVerts = kLineCount * 2 * 2;

    Vertex gridVerts[kTotalVerts];
    uint32_t idx = 0;

    const float extent = static_cast<float>(kHalfExtent);

    const Float3 kMinorColor = {0.22f, 0.22f, 0.22f};
    const Float3 kMajorColor = {0.45f, 0.45f, 0.45f};
    const Float3 kAxisX      = {0.55f, 0.10f, 0.10f};
    const Float3 kAxisZ      = {0.10f, 0.55f, 0.10f};

    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float x = static_cast<float>(i);
        Float3 col;
        if (i == 0)          col = kAxisX;
        else if (i % 10 == 0) col = kMajorColor;
        else                  col = kMinorColor;
        gridVerts[idx++] = { {x, 0.0f, -extent}, col };
        gridVerts[idx++] = { {x, 0.0f,  extent}, col };
    }

    for (int i = -kHalfExtent; i <= kHalfExtent; ++i) {
        const float z = static_cast<float>(i);
        Float3 col;
        if (i == 0)          col = kAxisZ;
        else if (i % 10 == 0) col = kMajorColor;
        else                  col = kMinorColor;
        gridVerts[idx++] = { {-extent, 0.0f, z}, col };
        gridVerts[idx++] = { { extent, 0.0f, z}, col };
    }

    gridVertexCount_ = idx;

    RhiBufferDesc bufDesc = {};
    bufDesc.size       = sizeof(Vertex) * gridVertexCount_;
    bufDesc.usage      = RHI_BUFFER_USAGE_VERTEX;
    bufDesc.debug_name = "EditorGridVB";

    gridVertexBuffer_ = rhi()->buffer_create(&bufDesc);
    if (gridVertexBuffer_ == RHI_NULL) {
        OutputDebugStringA("[UzEngine] Failed to create editor grid VB\n");
        return false;
    }

    rhi()->buffer_upload(gridVertexBuffer_, gridVerts, sizeof(Vertex) * gridVertexCount_);
    return true;
}

} // namespace uz
