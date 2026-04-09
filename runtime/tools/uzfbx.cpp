/// uzfbx — FBX to .uzmesh converter
///
/// Standalone CLI tool: reads .fbx via ufbx, extracts geometry, generates
/// tangents, and writes the engine's runtime .uzmesh format directly.
/// Does NOT go through FbxImporter (avoids SDF bake overhead).
///
/// # Usage
/// ```
/// uzfbx --input <path.fbx> --output <path.uzmesh>
/// ```

#include "mesh.h"
#include "auto_lod.h"
#include "log.h"
#include "profiler.h"

#include <ufbx.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Cook);
UZ_DEFINE_LOG_CATEGORY(Cook, Info);

static std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    const auto size = file.tellg();
    if (size <= 0) return {};
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return file.good() ? data : std::vector<uint8_t>{};
}

int main(int argc, char** argv) {
    std::string inputPath, outputPath, builtinName;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc)   inputPath = argv[++i];
        if (arg == "--output" && i + 1 < argc)  outputPath = argv[++i];
        if (arg == "--builtin" && i + 1 < argc) builtinName = argv[++i];
    }

    // ── Builtin mesh generation mode ─────────────────────────────────────
    if (!builtinName.empty() && !outputPath.empty()) {
        uz::Mesh cpuMesh{};
        if (builtinName == "cube")        cpuMesh = uz::MeshBuilder::CreateCube();
        else if (builtinName == "sphere") cpuMesh = uz::MeshBuilder::CreateSphere();
        else {
            std::cerr << "Unknown builtin: " << builtinName << " (supported: cube, sphere)\n";
            return 1;
        }
        if (!uz::SaveMesh(outputPath, cpuMesh)) {
            std::cerr << "Error: failed to write " << outputPath << "\n";
            return 1;
        }
        std::cout << "Generated builtin:" << builtinName << " → " << outputPath
                  << " (" << cpuMesh.header.vertexCount << " verts, "
                  << cpuMesh.header.indexCount << " indices)\n";
        return 0;
    }

    if (inputPath.empty() || outputPath.empty()) {
        std::cerr << "Usage: uzfbx --input <path.fbx> --output <path.uzmesh>\n"
                  << "       uzfbx --builtin <cube|sphere> --output <path.uzmesh>\n";
        return 1;
    }

    // 1. Read FBX bytes
    auto fbxBytes = ReadFileBytes(inputPath);
    if (fbxBytes.empty()) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return 1;
    }

    // 2. Parse FBX via ufbx — convert to UzEngine coordinate system
    //    UzEngine: Z-up, left-handed, centimeters (0.01 meters)
    //    FBX files can be Y-up, Z-up, right-handed, any unit scale.
    //    ufbx handles the conversion automatically with target_axes + target_unit_meters.
    ufbx_load_opts opts = {};
    opts.target_unit_meters = 0.01f; // centimeters (UzEngine standard)
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;
    // Axis conversion done manually below (FBX Y-up LH → UzEngine Z-up LH).

    ufbx_error err = {};
    ufbx_scene* scene = ufbx_load_memory(fbxBytes.data(), fbxBytes.size(), &opts, &err);
    if (!scene) {
        std::cerr << "Error: ufbx parse failed: "
                  << std::string(err.description.data, err.description.length) << "\n";
        return 1;
    }

    std::cout << "FBX source: unit_meters=" << scene->settings.unit_meters
              << ", axes.up=" << scene->settings.axes.up
              << ", axes.front=" << scene->settings.axes.front
              << ", axes.right=" << scene->settings.axes.right << "\n";

    // Debug: print node hierarchy and mesh instances
    for (size_t ni = 0; ni < scene->nodes.count; ++ni) {
        const ufbx_node* node = scene->nodes.data[ni];
        std::cout << "Node[" << ni << "] '" << std::string(node->name.data, node->name.length)
                  << "' mesh=" << (node->mesh ? "YES" : "no")
                  << " parent='" << (node->parent ? std::string(node->parent->name.data, node->parent->name.length) : "ROOT") << "'"
                  << " pos=(" << node->local_transform.translation.x << "," << node->local_transform.translation.y << "," << node->local_transform.translation.z << ")"
                  << " bones=" << (node->bone ? "YES" : "no")
                  << "\n";
    }
    std::cout << "skin_deformers: " << scene->skin_deformers.count << "\n";

    if (scene->meshes.count == 0) {
        std::cerr << "Error: no meshes in FBX\n";
        ufbx_free_scene(scene);
        return 1;
    }

    const ufbx_mesh* mesh = scene->meshes.data[0];
    const bool hasNormals = mesh->vertex_normal.exists;
    const bool hasUVs = mesh->vertex_uv.exists;

    // 3. Triangulate all faces → build Vertex array directly (48B)
    std::vector<uz::Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> triBuffer(256);

    for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
        const ufbx_face& face = mesh->faces.data[fi];
        const size_t maxTri = static_cast<size_t>(face.num_indices - 2) * 3;
        if (triBuffer.size() < maxTri) triBuffer.resize(maxTri);

        // ufbx_triangulate_face returns the number of TRIANGLES, not indices.
        // Each triangle has 3 corner indices in the output buffer.
        uint32_t numTri = ufbx_triangulate_face(triBuffer.data(), triBuffer.size(), mesh, face);
        const uint32_t numIndices = numTri * 3;

        for (uint32_t ti = 0; ti < numIndices; ++ti) {
            uint32_t ci = triBuffer[ti]; // corner index

            uz::Vertex v{};
            ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ci);
            // FBX Y-up LH (X-right, Y-up, Z-forward) → UzEngine Z-up LH (X-forward, Y-right, Z-up)
            // Engine.X = FBX.Z, Engine.Y = FBX.X, Engine.Z = FBX.Y
            v.position = {static_cast<float>(pos.z), static_cast<float>(pos.x), static_cast<float>(pos.y)};

            if (hasNormals) {
                ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ci);
                v.normal = {static_cast<float>(n.z), static_cast<float>(n.x), static_cast<float>(n.y)};
            }

            if (hasUVs) {
                ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ci);
                // FBX UV: V=0 at bottom (OpenGL convention).
                // stb_image loads textures with origin at top-left (DX convention).
                // Flip V to match: v' = 1.0 - v.
                v.texcoord = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
            }

            uint32_t idx = static_cast<uint32_t>(vertices.size());
            vertices.push_back(v);
            indices.push_back(idx);
        }

    }

    // Print mesh stats before freeing scene
    std::cout << "ufbx: " << mesh->num_vertices << " logical verts, "
              << mesh->num_indices << " corners, "
              << mesh->num_faces << " faces, "
              << mesh->num_triangles << " triangles\n";

    ufbx_free_scene(scene);

    // Deduplicate vertices using ufbx_generate_indices — creates a proper
    // indexed mesh from the flat per-corner vertex array.
    {
        ufbx_vertex_stream stream;
        stream.data = vertices.data();
        stream.vertex_count = vertices.size();
        stream.vertex_size = sizeof(uz::Vertex);

        indices.resize(vertices.size());
        ufbx_error genErr = {};
        size_t uniqueCount = ufbx_generate_indices(&stream, 1,
            indices.data(), vertices.size(), nullptr, &genErr);

        std::cout << "Deduplicated: " << vertices.size() << " → " << uniqueCount << " vertices\n";
        vertices.resize(uniqueCount);
    }

    // Reverse triangle winding from CW to CCW (AFTER deduplication).
    // The manual axis remap (FBX Y-up → UzEngine Z-up) is an odd permutation
    // that flips handedness, inverting the winding order.
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        std::swap(indices[t + 1], indices[t + 2]);
    }

    const uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(indices.size());

    // 4. Generate tangents
    uz::GenerateTangents(vertices, indices);

    // 5. Compute AABB
    uz::Vec3 bMin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    uz::Vec3 bMax = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const auto& v : vertices) {
        bMin.x = std::min(bMin.x, v.position.x); bMax.x = std::max(bMax.x, v.position.x);
        bMin.y = std::min(bMin.y, v.position.y); bMax.y = std::max(bMax.y, v.position.y);
        bMin.z = std::min(bMin.z, v.position.z); bMax.z = std::max(bMax.z, v.position.z);
    }

    // 6. Auto-LOD generation (shared utility)
    uz::AutoLODResult lodResult = uz::GenerateAutoLODs(
        vertices.data(), vertexCount, indices.data(), indexCount);
    for (uint32_t i = 0; i < lodResult.generatedCount; ++i) {
        std::cout << "LOD" << (i + 1) << ": " << indexCount << " → "
                  << lodResult.lodIndices[i].size() << " indices\n";
    }

    // 7. Build Mesh with LOD data and save
    uz::Mesh outMesh;
    std::memset(&outMesh.header, 0, sizeof(uz::MeshHeader));
    outMesh.header.magic            = uz::kMeshMagic;
    outMesh.header.version          = uz::kMeshVersion;
    outMesh.header.vertexFormat     = uz::MeshVertexFormat::Standard;
    outMesh.header.vertexStride     = sizeof(uz::Vertex);
    outMesh.header.vertexCount      = vertexCount;
    outMesh.header.indexFormat      = uz::MeshIndexFormat::UInt32;
    outMesh.header.indexCount       = indexCount;
    outMesh.header.submeshCount     = 0;
    outMesh.header.boundsMin        = bMin;
    outMesh.header.boundsMax        = bMax;
    outMesh.header.vertexDataOffset = sizeof(uz::MeshHeader);
    outMesh.header.indexDataOffset  = sizeof(uz::MeshHeader) + static_cast<uint64_t>(vertexCount) * sizeof(uz::Vertex);

    outMesh.vertexData.resize(vertexCount * sizeof(uz::Vertex));
    std::memcpy(outMesh.vertexData.data(), vertices.data(), outMesh.vertexData.size());

    outMesh.indexData.resize(static_cast<size_t>(indexCount) * sizeof(uint32_t));
    std::memcpy(outMesh.indexData.data(), indices.data(), outMesh.indexData.size());

    uz::ApplyAutoLODs(outMesh, lodResult);

    if (!uz::SaveMesh(outputPath, outMesh)) {
        std::cerr << "Error: failed to write " << outputPath << "\n";
        return 1;
    }

    std::cout << "Converted: " << vertexCount << " vertices, " << indexCount
              << " indices, " << outMesh.header.lodCount << " LODs, bounds=["
              << bMin.x << "," << bMin.y << "," << bMin.z
              << "]→[" << bMax.x << "," << bMax.y << "," << bMax.z
              << "] → " << outputPath << "\n";
    return 0;
}
