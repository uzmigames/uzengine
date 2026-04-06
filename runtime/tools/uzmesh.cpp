/// uzmesh — Unified 3D model to .uzmesh converter
///
/// Single CLI tool that imports any supported 3D format into UzEngine's
/// runtime .uzmesh binary format. Detects format from file extension.
///
/// # Supported Formats
/// - `.fbx`         — via ufbx (Autodesk FBX)
/// - `.gltf`/`.glb` — via fastgltf (Khronos glTF 2.0)
/// - `.obj`         — built-in Wavefront OBJ parser
/// - `.stl`         — built-in binary/ASCII STL parser
///
/// # Pipeline (shared across all formats)
/// Parse → Axis conversion (Y-up → Z-up) → Dedup → Winding fix →
/// Tangent generation → AABB → Auto-LOD (meshopt_simplify) → Save .uzmesh
///
/// # Usage
/// ```
/// uzmesh --input <model.fbx|.gltf|.glb|.obj|.stl> --output <mesh.uzmesh>
/// uzmesh --builtin <cube|sphere> --output <mesh.uzmesh>
/// ```

#include "mesh.h"
#include "auto_lod.h"
#include "log.h"
#include "profiler.h"

#include <meshoptimizer.h>
#include <ufbx.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Cook);
UZ_DEFINE_LOG_CATEGORY(Cook, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

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

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/// Intermediate mesh representation — output of every format parser.
struct RawMesh {
    std::vector<uz::Vertex> vertices;
    std::vector<uint32_t> indices;
    bool success = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// FBX Parser (ufbx)
// ─────────────────────────────────────────────────────────────────────────────

static RawMesh ParseFBX(const std::string& inputPath) {
    RawMesh result;

    auto fbxBytes = ReadFileBytes(inputPath);
    if (fbxBytes.empty()) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return result;
    }

    ufbx_load_opts opts = {};
    opts.target_unit_meters = 0.01f;
    opts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
    opts.generate_missing_normals = true;

    ufbx_error err = {};
    ufbx_scene* scene = ufbx_load_memory(fbxBytes.data(), fbxBytes.size(), &opts, &err);
    if (!scene) {
        std::cerr << "Error: ufbx parse failed: "
                  << std::string(err.description.data, err.description.length) << "\n";
        return result;
    }

    if (scene->meshes.count == 0) {
        std::cerr << "Error: no meshes in FBX\n";
        ufbx_free_scene(scene);
        return result;
    }

    const ufbx_mesh* mesh = scene->meshes.data[0];
    const bool hasNormals = mesh->vertex_normal.exists;
    const bool hasUVs = mesh->vertex_uv.exists;

    std::vector<uint32_t> triBuffer(256);
    for (size_t fi = 0; fi < mesh->num_faces; ++fi) {
        const ufbx_face& face = mesh->faces.data[fi];
        const size_t maxTri = static_cast<size_t>(face.num_indices - 2) * 3;
        if (triBuffer.size() < maxTri) triBuffer.resize(maxTri);

        uint32_t numTri = ufbx_triangulate_face(triBuffer.data(), triBuffer.size(), mesh, face);
        const uint32_t numIndices = numTri * 3;

        for (uint32_t ti = 0; ti < numIndices; ++ti) {
            uint32_t ci = triBuffer[ti];
            uz::Vertex v{};
            ufbx_vec3 pos = ufbx_get_vertex_vec3(&mesh->vertex_position, ci);
            v.position = {static_cast<float>(pos.z), static_cast<float>(pos.x), static_cast<float>(pos.y)};
            if (hasNormals) {
                ufbx_vec3 n = ufbx_get_vertex_vec3(&mesh->vertex_normal, ci);
                v.normal = {static_cast<float>(n.z), static_cast<float>(n.x), static_cast<float>(n.y)};
            }
            if (hasUVs) {
                ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ci);
                v.texcoord = {static_cast<float>(uv.x), 1.0f - static_cast<float>(uv.y)};
            }
            uint32_t idx = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(v);
            result.indices.push_back(idx);
        }
    }

    std::cout << "FBX: " << mesh->num_vertices << " logical verts, "
              << mesh->num_triangles << " triangles\n";

    ufbx_free_scene(scene);

    // Deduplicate vertices
    {
        ufbx_vertex_stream stream;
        stream.data = result.vertices.data();
        stream.vertex_count = result.vertices.size();
        stream.vertex_size = sizeof(uz::Vertex);
        result.indices.resize(result.vertices.size());
        ufbx_error genErr = {};
        size_t uniqueCount = ufbx_generate_indices(&stream, 1,
            result.indices.data(), result.vertices.size(), nullptr, &genErr);
        std::cout << "Deduplicated: " << result.vertices.size() << " → " << uniqueCount << " vertices\n";
        result.vertices.resize(uniqueCount);
    }

    // Reverse winding (FBX axis remap is odd permutation)
    for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
        std::swap(result.indices[t + 1], result.indices[t + 2]);
    }

    result.success = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// glTF/GLB Parser (fastgltf)
// ─────────────────────────────────────────────────────────────────────────────

static RawMesh ParseGLTF(const std::string& inputPath) {
    RawMesh result;

    auto parentDir = std::filesystem::path(inputPath).parent_path();
    fastgltf::Parser parser;
    auto dataResult = fastgltf::GltfDataBuffer::FromPath(inputPath);
    if (dataResult.error() != fastgltf::Error::None) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return result;
    }

    auto type = fastgltf::determineGltfFileType(dataResult.get());
    fastgltf::Expected<fastgltf::Asset> assetResult(fastgltf::Error::None);
    auto options = fastgltf::Options::LoadExternalBuffers
                 | fastgltf::Options::GenerateMeshIndices;

    if (type == fastgltf::GltfType::glTF) {
        assetResult = parser.loadGltf(dataResult.get(), parentDir, options);
    } else if (type == fastgltf::GltfType::GLB) {
        assetResult = parser.loadGltfBinary(dataResult.get(), parentDir, options);
    } else {
        std::cerr << "Error: unrecognized glTF format\n";
        return result;
    }

    if (assetResult.error() != fastgltf::Error::None) {
        std::cerr << "Error: fastgltf parse failed\n";
        return result;
    }

    auto& gltf = assetResult.get();
    if (gltf.meshes.empty()) {
        std::cerr << "Error: no meshes in glTF\n";
        return result;
    }

    const auto& mesh = gltf.meshes[0];
    for (size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
        const auto& prim = mesh.primitives[primIdx];
        auto posIt = prim.findAttribute("POSITION");
        if (posIt == prim.attributes.end()) continue;

        const auto& posAccessor = gltf.accessors[posIt->accessorIndex];
        const size_t vertCount = posAccessor.count;
        const uint32_t baseVertex = static_cast<uint32_t>(result.vertices.size());
        result.vertices.resize(baseVertex + vertCount);

        // Y-up RH → Z-up LH: Engine.X = glTF.Z, Engine.Y = glTF.X, Engine.Z = glTF.Y
        // glTF spec: coordinates are in meters. UzEngine uses centimeters → ×100.
        fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
            gltf, posAccessor, [&](fastgltf::math::fvec3 pos, std::size_t idx) {
                result.vertices[baseVertex + idx].position = {pos[2] * 100.0f, pos[0] * 100.0f, pos[1] * 100.0f};
            });

        auto normIt = prim.findAttribute("NORMAL");
        if (normIt != prim.attributes.end()) {
            const auto& normAccessor = gltf.accessors[normIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
                gltf, normAccessor, [&](fastgltf::math::fvec3 n, std::size_t idx) {
                    // Normals are unit vectors — axis swap only, no scale.
                    result.vertices[baseVertex + idx].normal = {n[2], n[0], n[1]};
                });
        }

        auto uvIt = prim.findAttribute("TEXCOORD_0");
        if (uvIt != prim.attributes.end()) {
            const auto& uvAccessor = gltf.accessors[uvIt->accessorIndex];
            fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
                gltf, uvAccessor, [&](fastgltf::math::fvec2 uv, std::size_t idx) {
                    result.vertices[baseVertex + idx].texcoord = {uv[0], uv[1]};
                });
        }

        if (prim.indicesAccessor.has_value()) {
            const auto& idxAccessor = gltf.accessors[prim.indicesAccessor.value()];
            const size_t baseIndex = result.indices.size();
            result.indices.resize(baseIndex + idxAccessor.count);
            fastgltf::iterateAccessorWithIndex<uint32_t>(
                gltf, idxAccessor, [&](uint32_t val, std::size_t idx) {
                    result.indices[baseIndex + idx] = baseVertex + val;
                });
        } else {
            for (uint32_t i = 0; i < vertCount; ++i)
                result.indices.push_back(baseVertex + i);
        }

        // Generate flat normals if missing
        if (normIt == prim.attributes.end()) {
            for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
                auto& v0 = result.vertices[result.indices[t]];
                auto& v1 = result.vertices[result.indices[t + 1]];
                auto& v2 = result.vertices[result.indices[t + 2]];
                float e1x = v1.position.x - v0.position.x, e1y = v1.position.y - v0.position.y, e1z = v1.position.z - v0.position.z;
                float e2x = v2.position.x - v0.position.x, e2y = v2.position.y - v0.position.y, e2z = v2.position.z - v0.position.z;
                float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
                float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
                v0.normal = v1.normal = v2.normal = {nx, ny, nz};
            }
        }
    }

    std::cout << "glTF: " << result.vertices.size() << " vertices, "
              << result.indices.size() << " indices\n";

    // Reverse winding (axis remap changes handedness)
    for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
        std::swap(result.indices[t + 1], result.indices[t + 2]);
    }

    result.success = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// OBJ Parser (built-in)
// ─────────────────────────────────────────────────────────────────────────────

static RawMesh ParseOBJ(const std::string& inputPath) {
    RawMesh result;

    std::ifstream file(inputPath);
    if (!file.is_open()) {
        std::cerr << "Error: cannot read " << inputPath << "\n";
        return result;
    }

    // OBJ uses separate position/normal/UV arrays referenced by face indices.
    std::vector<uz::Vec3> positions;
    std::vector<uz::Vec3> normals;
    std::vector<float> uvs; // pairs: u, v

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string prefix;
        iss >> prefix;

        if (prefix == "v") {
            float x, y, z;
            iss >> x >> y >> z;
            // OBJ is typically Y-up RH, in meters.
            // Axis: Z-up LH (X=Z, Y=X, Z=Y). Units: meters → cm (×100).
            positions.push_back({z * 100.0f, x * 100.0f, y * 100.0f});
        } else if (prefix == "vn") {
            float x, y, z;
            iss >> x >> y >> z;
            // Normals: axis swap only, no scale (unit vectors).
            normals.push_back({z, x, y});
        } else if (prefix == "vt") {
            float u, v;
            iss >> u >> v;
            uvs.push_back(u);
            uvs.push_back(1.0f - v); // Flip V: OBJ V=0 at bottom → DX V=0 at top
        } else if (prefix == "f") {
            // Parse face vertices: v, v/vt, v/vt/vn, v//vn
            struct FaceVert { int vi = 0, ti = 0, ni = 0; };
            std::vector<FaceVert> faceVerts;

            std::string token;
            while (iss >> token) {
                FaceVert fv;
                // Replace '/' with spaces for easier parsing
                for (auto& c : token) { if (c == '/') c = ' '; }
                std::istringstream tss(token);
                tss >> fv.vi;
                if (tss.peek() != EOF) tss >> fv.ti;
                // Handle v//vn case: ti was actually 0, read ni
                if (token.find("  ") != std::string::npos) {
                    // v//vn: ti is 0, next is ni
                    fv.ni = fv.ti;
                    fv.ti = 0;
                } else if (tss.peek() != EOF) {
                    tss >> fv.ni;
                }
                faceVerts.push_back(fv);
            }

            // Triangulate fan: 0-1-2, 0-2-3, 0-3-4, ...
            for (size_t i = 2; i < faceVerts.size(); ++i) {
                const FaceVert* tri[3] = { &faceVerts[0], &faceVerts[i - 1], &faceVerts[i] };
                for (int k = 0; k < 3; ++k) {
                    uz::Vertex v{};
                    int pi = tri[k]->vi - 1; // OBJ is 1-indexed
                    if (pi >= 0 && pi < static_cast<int>(positions.size()))
                        v.position = positions[pi];

                    int ni = tri[k]->ni - 1;
                    if (ni >= 0 && ni < static_cast<int>(normals.size()))
                        v.normal = normals[ni];

                    int ti = tri[k]->ti - 1;
                    if (ti >= 0 && static_cast<size_t>(ti * 2 + 1) < uvs.size())
                        v.texcoord = {uvs[ti * 2], uvs[ti * 2 + 1]};

                    uint32_t idx = static_cast<uint32_t>(result.vertices.size());
                    result.vertices.push_back(v);
                    result.indices.push_back(idx);
                }
            }
        }
    }

    std::cout << "OBJ: " << positions.size() << " positions, "
              << normals.size() << " normals, "
              << uvs.size() / 2 << " UVs, "
              << result.indices.size() / 3 << " triangles\n";

    // Deduplicate via meshopt
    if (!result.vertices.empty()) {
        std::vector<uint32_t> remap(result.vertices.size());
        const size_t uniqueCount = meshopt_generateVertexRemap(
            remap.data(), result.indices.data(), result.indices.size(),
            result.vertices.data(), result.vertices.size(), sizeof(uz::Vertex));

        std::vector<uint32_t> newIndices(result.indices.size());
        meshopt_remapIndexBuffer(newIndices.data(), result.indices.data(),
                                 result.indices.size(), remap.data());

        std::vector<uz::Vertex> newVertices(uniqueCount);
        meshopt_remapVertexBuffer(newVertices.data(), result.vertices.data(),
                                  result.vertices.size(), sizeof(uz::Vertex), remap.data());

        std::cout << "Deduplicated: " << result.vertices.size() << " → " << uniqueCount << " vertices\n";
        result.vertices = std::move(newVertices);
        result.indices = std::move(newIndices);
    }

    // Generate flat normals for faces that had none
    bool hasAnyNormal = false;
    for (const auto& v : result.vertices) {
        if (v.normal.x != 0.0f || v.normal.y != 0.0f || v.normal.z != 0.0f) {
            hasAnyNormal = true;
            break;
        }
    }
    if (!hasAnyNormal) {
        for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
            auto& v0 = result.vertices[result.indices[t]];
            auto& v1 = result.vertices[result.indices[t + 1]];
            auto& v2 = result.vertices[result.indices[t + 2]];
            float e1x = v1.position.x - v0.position.x, e1y = v1.position.y - v0.position.y, e1z = v1.position.z - v0.position.z;
            float e2x = v2.position.x - v0.position.x, e2y = v2.position.y - v0.position.y, e2z = v2.position.z - v0.position.z;
            float nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 1e-8f) { nx /= len; ny /= len; nz /= len; }
            v0.normal = v1.normal = v2.normal = {nx, ny, nz};
        }
    }

    // Reverse winding (Y-up RH → Z-up LH is odd permutation)
    for (size_t t = 0; t + 2 < result.indices.size(); t += 3) {
        std::swap(result.indices[t + 1], result.indices[t + 2]);
    }

    result.success = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// STL Parser (built-in, binary + ASCII)
// ─────────────────────────────────────────────────────────────────────────────

static RawMesh ParseSTL(const std::string& inputPath) {
    RawMesh result;

    auto bytes = ReadFileBytes(inputPath);
    if (bytes.size() < 84) {
        std::cerr << "Error: STL file too small: " << inputPath << "\n";
        return result;
    }

    // Detect ASCII vs binary: ASCII starts with "solid" (but some binary files
    // also start with "solid" in the header). Use triangle count heuristic.
    bool isBinary = true;
    if (bytes.size() > 5 && std::memcmp(bytes.data(), "solid", 5) == 0) {
        // Check if expected binary size matches
        uint32_t triCount = 0;
        std::memcpy(&triCount, bytes.data() + 80, 4);
        const size_t expectedBinarySize = 84 + static_cast<size_t>(triCount) * 50;
        if (expectedBinarySize != bytes.size()) {
            isBinary = false; // Probably ASCII
        }
    }

    if (isBinary) {
        // Binary STL: 80-byte header, 4-byte triangle count, then 50 bytes/tri
        uint32_t triCount = 0;
        std::memcpy(&triCount, bytes.data() + 80, 4);

        if (84 + static_cast<size_t>(triCount) * 50 > bytes.size()) {
            std::cerr << "Error: truncated binary STL\n";
            return result;
        }

        const uint8_t* ptr = bytes.data() + 84;
        for (uint32_t t = 0; t < triCount; ++t) {
            float normal[3], v0[3], v1[3], v2[3];
            std::memcpy(normal, ptr, 12); ptr += 12;
            std::memcpy(v0, ptr, 12); ptr += 12;
            std::memcpy(v1, ptr, 12); ptr += 12;
            std::memcpy(v2, ptr, 12); ptr += 12;
            ptr += 2; // attribute byte count

            // STL has no standard unit — files may be mm, cm, inches, or meters.
            // Keep raw coordinates; user scales in scene. Z-up, no axis swap.
            auto makeVert = [&](const float* p, const float* n) -> uz::Vertex {
                uz::Vertex v{};
                v.position = {p[0], p[1], p[2]};
                v.normal = {n[0], n[1], n[2]};
                return v;
            };

            uint32_t baseIdx = static_cast<uint32_t>(result.vertices.size());
            result.vertices.push_back(makeVert(v0, normal));
            result.vertices.push_back(makeVert(v1, normal));
            result.vertices.push_back(makeVert(v2, normal));
            result.indices.push_back(baseIdx);
            result.indices.push_back(baseIdx + 1);
            result.indices.push_back(baseIdx + 2);
        }

        std::cout << "STL (binary): " << triCount << " triangles\n";
    } else {
        // ASCII STL: parse "facet normal" and "vertex" lines
        std::string content(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        std::istringstream iss(content);
        std::string line;
        float curNormal[3] = {0, 0, 0};

        while (std::getline(iss, line)) {
            std::istringstream lss(line);
            std::string kw;
            lss >> kw;

            if (kw == "facet") {
                std::string nkw;
                lss >> nkw; // "normal"
                lss >> curNormal[0] >> curNormal[1] >> curNormal[2];
            } else if (kw == "vertex") {
                float x, y, z;
                lss >> x >> y >> z;
                uz::Vertex v{};
                // STL Z-up, raw units (no conversion).
                v.position = {x, y, z};
                v.normal = {curNormal[0], curNormal[1], curNormal[2]};
                uint32_t idx = static_cast<uint32_t>(result.vertices.size());
                result.vertices.push_back(v);
                result.indices.push_back(idx);
            }
        }

        std::cout << "STL (ASCII): " << result.indices.size() / 3 << " triangles\n";
    }

    // Deduplicate via meshopt
    if (!result.vertices.empty()) {
        std::vector<uint32_t> remap(result.vertices.size());
        const size_t uniqueCount = meshopt_generateVertexRemap(
            remap.data(), result.indices.data(), result.indices.size(),
            result.vertices.data(), result.vertices.size(), sizeof(uz::Vertex));

        std::vector<uint32_t> newIndices(result.indices.size());
        meshopt_remapIndexBuffer(newIndices.data(), result.indices.data(),
                                 result.indices.size(), remap.data());

        std::vector<uz::Vertex> newVertices(uniqueCount);
        meshopt_remapVertexBuffer(newVertices.data(), result.vertices.data(),
                                  result.vertices.size(), sizeof(uz::Vertex), remap.data());

        std::cout << "Deduplicated: " << result.vertices.size() << " → " << uniqueCount << " vertices\n";
        result.vertices = std::move(newVertices);
        result.indices = std::move(newIndices);
    }

    // STL is already Z-up with no axis remap → no winding reverse needed.
    // FBX/glTF/OBJ do axis remap (odd permutation) which flips handedness,
    // requiring a winding reverse. STL stays native.

    result.success = true;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Common Pipeline: tangents → AABB → auto-LOD → save
// ─────────────────────────────────────────────────────────────────────────────

static int ProcessAndSave(RawMesh& raw, const std::string& outputPath) {
    if (!raw.success || raw.vertices.empty() || raw.indices.empty()) {
        std::cerr << "Error: no geometry to process\n";
        return 1;
    }

    const uint32_t vertexCount = static_cast<uint32_t>(raw.vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(raw.indices.size());

    // Generate smooth normals — average face normals at shared vertices.
    // This replaces flat per-face normals with smooth interpolated normals,
    // matching what ufbx does for FBX with generate_missing_normals=true.
    {
        // Zero all normals first
        for (auto& v : raw.vertices) {
            v.normal = {0.0f, 0.0f, 0.0f};
        }

        // Accumulate area-weighted face normals per vertex
        for (size_t t = 0; t + 2 < raw.indices.size(); t += 3) {
            const uint32_t i0 = raw.indices[t];
            const uint32_t i1 = raw.indices[t + 1];
            const uint32_t i2 = raw.indices[t + 2];

            const auto& p0 = raw.vertices[i0].position;
            const auto& p1 = raw.vertices[i1].position;
            const auto& p2 = raw.vertices[i2].position;

            // Cross product (area-weighted normal)
            const float e1x = p1.x - p0.x, e1y = p1.y - p0.y, e1z = p1.z - p0.z;
            const float e2x = p2.x - p0.x, e2y = p2.y - p0.y, e2z = p2.z - p0.z;
            const float nx = e1y * e2z - e1z * e2y;
            const float ny = e1z * e2x - e1x * e2z;
            const float nz = e1x * e2y - e1y * e2x;

            // Accumulate to all 3 vertices of this face
            raw.vertices[i0].normal.x += nx; raw.vertices[i0].normal.y += ny; raw.vertices[i0].normal.z += nz;
            raw.vertices[i1].normal.x += nx; raw.vertices[i1].normal.y += ny; raw.vertices[i1].normal.z += nz;
            raw.vertices[i2].normal.x += nx; raw.vertices[i2].normal.y += ny; raw.vertices[i2].normal.z += nz;
        }

        // Normalize
        for (auto& v : raw.vertices) {
            const float len = std::sqrt(v.normal.x * v.normal.x +
                                         v.normal.y * v.normal.y +
                                         v.normal.z * v.normal.z);
            if (len > 1e-8f) {
                v.normal.x /= len;
                v.normal.y /= len;
                v.normal.z /= len;
            }
        }
    }

    // Optimize vertex cache ordering
    meshopt_optimizeVertexCache(
        raw.indices.data(), raw.indices.data(),
        raw.indices.size(), raw.vertices.size());

    // Generate tangents
    uz::GenerateTangents(raw.vertices, raw.indices);

    // Compute AABB
    uz::Vec3 bMin = {std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    uz::Vec3 bMax = {std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest()};
    for (const auto& v : raw.vertices) {
        bMin.x = std::min(bMin.x, v.position.x); bMax.x = std::max(bMax.x, v.position.x);
        bMin.y = std::min(bMin.y, v.position.y); bMax.y = std::max(bMax.y, v.position.y);
        bMin.z = std::min(bMin.z, v.position.z); bMax.z = std::max(bMax.z, v.position.z);
    }

    // Auto-LOD generation
    uz::AutoLODResult lodResult = uz::GenerateAutoLODs(
        raw.vertices.data(), vertexCount, raw.indices.data(), indexCount);
    for (uint32_t i = 0; i < lodResult.generatedCount; ++i) {
        std::cout << "LOD" << (i + 1) << ": " << indexCount << " → "
                  << lodResult.lodIndices[i].size() << " indices\n";
    }

    // Build Mesh
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
    outMesh.header.indexDataOffset  = sizeof(uz::MeshHeader) +
        static_cast<uint64_t>(vertexCount) * sizeof(uz::Vertex);

    outMesh.vertexData.resize(static_cast<size_t>(vertexCount) * sizeof(uz::Vertex));
    std::memcpy(outMesh.vertexData.data(), raw.vertices.data(), outMesh.vertexData.size());

    outMesh.indexData.resize(static_cast<size_t>(indexCount) * sizeof(uint32_t));
    std::memcpy(outMesh.indexData.data(), raw.indices.data(), outMesh.indexData.size());

    uz::ApplyAutoLODs(outMesh, lodResult);

    if (!uz::SaveMesh(outputPath, outMesh)) {
        std::cerr << "Error: failed to write " << outputPath << "\n";
        return 1;
    }

    std::cout << "Output: " << vertexCount << " vertices, " << indexCount
              << " indices, " << outMesh.header.lodCount << " LODs, bounds=["
              << bMin.x << "," << bMin.y << "," << bMin.z
              << "]→[" << bMax.x << "," << bMax.y << "," << bMax.z
              << "] → " << outputPath << "\n";
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    std::string inputPath, outputPath, builtinName;
    bool forceYUp = false; // --yup: force Y-up → Z-up conversion (default for FBX/glTF/OBJ)
    bool forceZUp = false; // --zup: skip axis conversion (source already Z-up)

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc)   inputPath = argv[++i];
        if (arg == "--output" && i + 1 < argc)  outputPath = argv[++i];
        if (arg == "--builtin" && i + 1 < argc) builtinName = argv[++i];
        if (arg == "--yup") forceYUp = true;
        if (arg == "--zup") forceZUp = true;
    }

    // Builtin mesh generation mode
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
        std::cerr << "uzmesh — Unified 3D model importer\n\n"
                  << "Usage:\n"
                  << "  uzmesh --input <model.fbx|.gltf|.glb|.obj|.stl> --output <mesh.uzmesh>\n"
                  << "  uzmesh --builtin <cube|sphere> --output <mesh.uzmesh>\n\n"
                  << "Supported formats:\n"
                  << "  .fbx         Autodesk FBX (via ufbx)\n"
                  << "  .gltf/.glb   Khronos glTF 2.0 (via fastgltf)\n"
                  << "  .obj         Wavefront OBJ\n"
                  << "  .stl         STL (binary + ASCII)\n\n"
                  << "Features:\n"
                  << "  Auto-LOD generation (LOD1=50%, LOD2=25%, LOD3=12.5%)\n"
                  << "  MikkTSpace tangent generation\n"
                  << "  Vertex cache optimization (meshoptimizer)\n"
                  << "  Vertex deduplication\n"
                  << "  Y-up → Z-up axis conversion\n";
        return 1;
    }

    // Detect format from extension
    const std::string ext = ToLower(
        std::filesystem::path(inputPath).extension().string());

    RawMesh raw;
    if (ext == ".fbx") {
        raw = ParseFBX(inputPath);
    } else if (ext == ".gltf" || ext == ".glb") {
        raw = ParseGLTF(inputPath);
    } else if (ext == ".obj") {
        raw = ParseOBJ(inputPath);
    } else if (ext == ".stl") {
        raw = ParseSTL(inputPath);
    } else {
        std::cerr << "Error: unsupported format '" << ext << "'\n"
                  << "Supported: .fbx, .gltf, .glb, .obj, .stl\n";
        return 1;
    }

    return ProcessAndSave(raw, outputPath);
}
