/// UzEngine Material Graph — Implementation
///
/// Full implementation of the material graph: connection management,
/// cycle detection, topological sort, content hashing, and JSON serialization.

#include "material_graph.h"

#include "../logging/log.h"

#define XXH_INLINE_ALL
#include <xxhash.h>

#include <json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_set>

UZ_DEFINE_LOG_CATEGORY(Material, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Domain / ShadingModel string conversion
// ─────────────────────────────────────────────────────────────────────────────

const char* MaterialDomainToString(MaterialDomain domain) {
    switch (domain) {
        case MaterialDomain::Surface:     return "Surface";
        case MaterialDomain::PostProcess: return "PostProcess";
        case MaterialDomain::Decal:       return "Decal";
        case MaterialDomain::UI:          return "UI";
    }
    return "Surface";
}

MaterialDomain MaterialDomainFromString(const char* str) {
    if (std::strcmp(str, "PostProcess") == 0) return MaterialDomain::PostProcess;
    if (std::strcmp(str, "Decal") == 0)       return MaterialDomain::Decal;
    if (std::strcmp(str, "UI") == 0)          return MaterialDomain::UI;
    return MaterialDomain::Surface;
}

const char* MaterialShadingModelToString(MaterialShadingModel model) {
    switch (model) {
        case MaterialShadingModel::DefaultLit:  return "DefaultLit";
        case MaterialShadingModel::Unlit:       return "Unlit";
        case MaterialShadingModel::Subsurface:  return "Subsurface";
    }
    return "DefaultLit";
}

MaterialShadingModel MaterialShadingModelFromString(const char* str) {
    if (std::strcmp(str, "Unlit") == 0)      return MaterialShadingModel::Unlit;
    if (std::strcmp(str, "Subsurface") == 0) return MaterialShadingModel::Subsurface;
    return MaterialShadingModel::DefaultLit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

MaterialGraph::MaterialGraph() {
    // The PBR output node always exists as the first expression with id=1.
    expressions_.push_back(MaterialExpression::Create(MatExprType::PBROutput, 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// Expression management
// ─────────────────────────────────────────────────────────────────────────────

MaterialExpression& MaterialGraph::AddExpression(MatExprType type) {
    const uint32_t id = nextExprId_++;
    expressions_.push_back(MaterialExpression::Create(type, id));
    return expressions_.back();
}

void MaterialGraph::RemoveExpression(uint32_t exprId) {
    if (exprId == 1) {
        UZ_LOG(Material, Warning, "Cannot remove PBROutput expression (id=1)");
        return;
    }

    // Disconnect all input pins in other expressions that reference the removed expression.
    for (auto& expr : expressions_) {
        for (auto& pin : expr.inputs) {
            if (pin.connectedExprId == exprId) {
                pin.connectedExprId = 0;
                pin.connectedPinId  = 0;
            }
        }
    }

    // Erase the expression itself.
    auto it = std::find_if(expressions_.begin(), expressions_.end(),
                           [exprId](const MaterialExpression& e) { return e.id == exprId; });
    if (it != expressions_.end()) {
        expressions_.erase(it);
    }
}

MaterialExpression* MaterialGraph::FindExpression(uint32_t exprId) {
    for (auto& expr : expressions_) {
        if (expr.id == exprId) return &expr;
    }
    return nullptr;
}

const MaterialExpression* MaterialGraph::FindExpression(uint32_t exprId) const {
    for (const auto& expr : expressions_) {
        if (expr.id == exprId) return &expr;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Connection management
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialGraph::Connect(uint32_t srcExprId, uint32_t srcPinId,
                            uint32_t dstExprId, uint32_t dstPinId,
                            std::string* outError) {
    auto setError = [&](const std::string& msg) {
        if (outError) *outError = msg;
        return false;
    };

    // Find source expression and pin.
    MaterialExpression* srcExpr = FindExpression(srcExprId);
    if (!srcExpr) return setError("Source expression not found");

    MatPin* srcPin = nullptr;
    for (auto& pin : srcExpr->outputs) {
        if (pin.id == srcPinId) { srcPin = &pin; break; }
    }
    if (!srcPin) return setError("Source output pin not found");

    // Find destination expression and pin.
    MaterialExpression* dstExpr = FindExpression(dstExprId);
    if (!dstExpr) return setError("Destination expression not found");

    MatPin* dstPin = nullptr;
    for (auto& pin : dstExpr->inputs) {
        if (pin.id == dstPinId) { dstPin = &pin; break; }
    }
    if (!dstPin) return setError("Destination input pin not found");

    // Self-connection check.
    if (srcExprId == dstExprId) return setError("Cannot connect expression to itself");

    // Type compatibility check.
    if (!IsTypeCompatible(srcPin->type, dstPin->type)) {
        return setError("Incompatible pin types: " +
                        std::string(MatPinTypeToHlsl(srcPin->type)) + " -> " +
                        MatPinTypeToHlsl(dstPin->type));
    }

    // Cycle detection: if connecting src -> dst, check if dst can already
    // reach src through existing connections. If so, adding this edge
    // would create a cycle.
    if (WouldCreateCycle(srcExprId, dstExprId)) {
        return setError("Connection would create a cycle");
    }

    // Make the connection.
    dstPin->connectedExprId = srcExprId;
    dstPin->connectedPinId  = srcPinId;
    return true;
}

void MaterialGraph::Disconnect(uint32_t exprId, uint32_t inputPinId) {
    MaterialExpression* expr = FindExpression(exprId);
    if (!expr) return;

    for (auto& pin : expr->inputs) {
        if (pin.id == inputPinId) {
            pin.connectedExprId = 0;
            pin.connectedPinId  = 0;
            return;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Cycle detection
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialGraph::WouldCreateCycle(uint32_t srcExprId, uint32_t dstExprId) const {
    // Check if srcExprId is reachable from dstExprId by following existing
    // input connections backward from the source. If dstExprId already
    // depends (transitively) on srcExprId, then connecting src -> dst
    // would create a cycle.
    //
    // We trace from srcExprId backward through its inputs. If we reach
    // dstExprId, there is a path dst -> ... -> src, so adding src -> dst
    // closes the loop.
    std::unordered_set<uint32_t> visited;
    std::vector<uint32_t> stack;
    stack.push_back(srcExprId);

    while (!stack.empty()) {
        const uint32_t current = stack.back();
        stack.pop_back();

        if (current == dstExprId) return true;
        if (!visited.insert(current).second) continue;

        const MaterialExpression* expr = FindExpression(current);
        if (!expr) continue;

        for (const auto& pin : expr->inputs) {
            if (pin.IsConnected()) {
                stack.push_back(pin.connectedExprId);
            }
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Type compatibility
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialGraph::IsTypeCompatible(MatPinType src, MatPinType dst) const {
    // Same type is always compatible.
    if (src == dst) return true;

    // Texture pins only connect to Texture pins.
    if (src == MatPinType::Texture || dst == MatPinType::Texture) return false;

    // Float can promote to any vector type (splat: x -> xxxx).
    if (src == MatPinType::Float) return true;

    // Float2 can extend to Float3/Float4 (zero-extend).
    if (src == MatPinType::Float2 &&
        (dst == MatPinType::Float3 || dst == MatPinType::Float4)) {
        return true;
    }

    // Float3 can extend to Float4.
    if (src == MatPinType::Float3 && dst == MatPinType::Float4) return true;

    // Higher-component source to lower-component destination: allow truncation.
    // This matches reference behavior where a Float3 output can connect to a Float input
    // (takes the first component).
    const uint32_t srcComponents = MatPinTypeComponents(src);
    const uint32_t dstComponents = MatPinTypeComponents(dst);
    if (srcComponents > 0 && dstComponents > 0 && srcComponents > dstComponents) return true;

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialGraph::Validate(std::string& outError) const {
    // PBROutput must exist at expressions_[0] with id=1.
    if (expressions_.empty() || expressions_[0].id != 1 ||
        expressions_[0].type != MatExprType::PBROutput) {
        outError = "PBROutput expression (id=1) is missing or not first";
        return false;
    }

    // Check for duplicate IDs.
    std::unordered_set<uint32_t> ids;
    for (const auto& expr : expressions_) {
        if (!ids.insert(expr.id).second) {
            outError = "Duplicate expression ID: " + std::to_string(expr.id);
            return false;
        }
    }

    // Check for dangling connections (input pins referencing non-existent expressions/pins).
    for (const auto& expr : expressions_) {
        for (const auto& pin : expr.inputs) {
            if (!pin.IsConnected()) continue;

            const MaterialExpression* srcExpr = FindExpression(pin.connectedExprId);
            if (!srcExpr) {
                outError = "Expression " + std::to_string(expr.id) +
                           " input '" + pin.name + "' references non-existent expression " +
                           std::to_string(pin.connectedExprId);
                return false;
            }

            bool pinFound = false;
            for (const auto& outPin : srcExpr->outputs) {
                if (outPin.id == pin.connectedPinId) { pinFound = true; break; }
            }
            if (!pinFound) {
                outError = "Expression " + std::to_string(expr.id) +
                           " input '" + pin.name + "' references non-existent output pin " +
                           std::to_string(pin.connectedPinId) + " on expression " +
                           std::to_string(pin.connectedExprId);
                return false;
            }
        }
    }

    // Cycle detection via topological sort attempt.
    auto sorted = TopologicalSort();
    // TopologicalSort returns only reachable nodes; a cycle among reachable
    // nodes is detected inside TopoVisit via the inStack flag.
    // We verify by checking that the sort completed without hitting a cycle.
    // The sort itself handles cycle detection, and returns an empty vector
    // if a cycle was found among reachable expressions.
    // An empty result with connected inputs means a cycle exists.
    if (sorted.empty()) {
        // Check if PBROutput has any connected inputs. If it does, an empty
        // sort result means a cycle was detected.
        bool hasConnections = false;
        for (const auto& pin : expressions_[0].inputs) {
            if (pin.IsConnected()) { hasConnections = true; break; }
        }
        if (hasConnections) {
            outError = "Graph contains a cycle";
            return false;
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Topological sort
// ─────────────────────────────────────────────────────────────────────────────

void MaterialGraph::TopoVisit(uint32_t exprId, std::vector<bool>& visited,
                              std::vector<bool>& inStack, std::vector<uint32_t>& order,
                              bool& hasCycle) const {
    if (hasCycle) return;

    // Map expression ID to index in expressions_ vector.
    size_t idx = SIZE_MAX;
    for (size_t i = 0; i < expressions_.size(); ++i) {
        if (expressions_[i].id == exprId) { idx = i; break; }
    }
    if (idx == SIZE_MAX) return;

    if (inStack[idx]) {
        hasCycle = true;
        return;
    }
    if (visited[idx]) return;

    visited[idx] = true;
    inStack[idx] = true;

    // Visit all input dependencies first (leaf-first ordering).
    const auto& expr = expressions_[idx];
    for (const auto& pin : expr.inputs) {
        if (pin.IsConnected()) {
            TopoVisit(pin.connectedExprId, visited, inStack, order, hasCycle);
        }
    }

    inStack[idx] = false;
    order.push_back(exprId);
}

std::vector<uint32_t> MaterialGraph::TopologicalSort() const {
    const size_t n = expressions_.size();
    std::vector<bool> visited(n, false);
    std::vector<bool> inStack(n, false);
    std::vector<uint32_t> order;
    order.reserve(n);

    bool hasCycle = false;

    // Start DFS from PBROutput (id=1). Only reachable expressions are visited.
    TopoVisit(1, visited, inStack, order, hasCycle);

    if (hasCycle) return {};
    return order;
}

// ─────────────────────────────────────────────────────────────────────────────
// Content hash
// ─────────────────────────────────────────────────────────────────────────────

uint64_t MaterialGraph::ComputeContentHash() const {
    XXH64_state_t* state = XXH64_createState();
    XXH64_reset(state, 0);

    // Hash domain and shading model.
    auto domainVal = static_cast<uint8_t>(domain);
    XXH64_update(state, &domainVal, sizeof(domainVal));
    auto shadingVal = static_cast<uint8_t>(shadingModel);
    XXH64_update(state, &shadingVal, sizeof(shadingVal));

    // Hash MaterialParams — must be in the hash so PSO recompiles when the
    // author tweaks roughness/metallic/tint values.
    XXH64_update(state, &params, sizeof(params));

    // Sort expressions by ID for deterministic ordering.
    std::vector<const MaterialExpression*> sorted;
    sorted.reserve(expressions_.size());
    for (const auto& expr : expressions_) {
        sorted.push_back(&expr);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const MaterialExpression* a, const MaterialExpression* b) {
                  return a->id < b->id;
              });

    for (const auto* expr : sorted) {
        // Hash expression identity.
        XXH64_update(state, &expr->id, sizeof(expr->id));
        auto typeVal = static_cast<uint16_t>(expr->type);
        XXH64_update(state, &typeVal, sizeof(typeVal));

        // Hash input connections.
        for (const auto& pin : expr->inputs) {
            XXH64_update(state, &pin.connectedExprId, sizeof(pin.connectedExprId));
            XXH64_update(state, &pin.connectedPinId, sizeof(pin.connectedPinId));
            XXH64_update(state, pin.defaultValue.data(), sizeof(pin.defaultValue));
        }

        // Hash scalar parameters (sorted by key for determinism).
        std::vector<std::pair<std::string, float>> scalarsSorted(
            expr->scalarParams.begin(), expr->scalarParams.end());
        std::sort(scalarsSorted.begin(), scalarsSorted.end());
        for (const auto& [key, val] : scalarsSorted) {
            XXH64_update(state, key.data(), key.size());
            XXH64_update(state, &val, sizeof(val));
        }

        // Hash string parameters (sorted by key for determinism).
        std::vector<std::pair<std::string, std::string>> stringsSorted(
            expr->stringParams.begin(), expr->stringParams.end());
        std::sort(stringsSorted.begin(), stringsSorted.end());
        for (const auto& [key, val] : stringsSorted) {
            XXH64_update(state, key.data(), key.size());
            XXH64_update(state, val.data(), val.size());
        }

        // Hash integer parameters (sorted by key for determinism).
        std::vector<std::pair<std::string, int32_t>> intsSorted(
            expr->intParams.begin(), expr->intParams.end());
        std::sort(intsSorted.begin(), intsSorted.end());
        for (const auto& [key, val] : intsSorted) {
            XXH64_update(state, key.data(), key.size());
            XXH64_update(state, &val, sizeof(val));
        }
    }

    const uint64_t hash = XXH64_digest(state);
    XXH64_freeState(state);
    return hash;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization
// ─────────────────────────────────────────────────────────────────────────────

std::string MaterialGraph::ToJson() const {
    nlohmann::json root;
    root["version"]      = 1;
    root["name"]         = name;
    root["domain"]       = MaterialDomainToString(domain);
    root["shadingModel"] = MaterialShadingModelToString(shadingModel);

    // ── Standard PBR knobs (always serialized so authors can tweak) ───
    nlohmann::json jParams;
    jParams["baseColorTint"]      = {params.baseColorTint[0],
                                     params.baseColorTint[1],
                                     params.baseColorTint[2]};
    jParams["roughnessMultiplier"] = params.roughnessMultiplier;
    jParams["metallicMultiplier"]  = params.metallicMultiplier;
    jParams["emissiveMultiplier"]  = params.emissiveMultiplier;
    jParams["normalStrength"]      = params.normalStrength;
    jParams["specularMultiplier"]  = params.specularMultiplier;
    jParams["aoMultiplier"]        = params.aoMultiplier;
    root["params"] = std::move(jParams);

    auto& jExprs = root["expressions"];
    jExprs = nlohmann::json::array();

    for (const auto& expr : expressions_) {
        nlohmann::json jExpr;
        jExpr["id"]   = expr.id;
        jExpr["type"] = MatExprTypeToString(expr.type);
        jExpr["name"] = expr.name;
        jExpr["pos"]  = {expr.posX, expr.posY};

        // Serialize input connections (only connected pins).
        // Saved in the new named-pin format: dstPin/srcPin as strings.
        auto& jInputs = jExpr["inputs"];
        jInputs = nlohmann::json::array();
        for (const auto& pin : expr.inputs) {
            nlohmann::json jPin;
            jPin["dstPin"] = pin.name;
            jPin["type"]   = MatPinTypeToHlsl(pin.type);
            jPin["default"] = {pin.defaultValue[0], pin.defaultValue[1],
                               pin.defaultValue[2], pin.defaultValue[3]};
            if (pin.IsConnected()) {
                jPin["srcExpr"] = pin.connectedExprId;
                // Resolve numeric source pin id back to its name for stable
                // string-based serialization. Falls back to int if the source
                // expression cannot be located (defensive).
                std::string srcPinName;
                for (const auto& srcExpr : expressions_) {
                    if (srcExpr.id != pin.connectedExprId) continue;
                    for (const auto& srcOut : srcExpr.outputs) {
                        if (srcOut.id == pin.connectedPinId) {
                            srcPinName = srcOut.name;
                            break;
                        }
                    }
                    break;
                }
                if (!srcPinName.empty()) {
                    jPin["srcPin"] = srcPinName;
                } else {
                    jPin["srcPin"] = pin.connectedPinId;
                }
            }
            jInputs.push_back(std::move(jPin));
        }

        // Serialize output pin definitions.
        auto& jOutputs = jExpr["outputs"];
        jOutputs = nlohmann::json::array();
        for (const auto& pin : expr.outputs) {
            nlohmann::json jPin;
            jPin["pin"]  = pin.id;
            jPin["name"] = pin.name;
            jPin["type"] = MatPinTypeToHlsl(pin.type);
            jOutputs.push_back(std::move(jPin));
        }

        // Serialize node parameters.
        if (!expr.scalarParams.empty()) {
            jExpr["scalarParams"] = expr.scalarParams;
        }
        if (!expr.stringParams.empty()) {
            jExpr["stringParams"] = expr.stringParams;
        }
        if (!expr.intParams.empty()) {
            jExpr["intParams"] = expr.intParams;
        }

        jExprs.push_back(std::move(jExpr));
    }

    return root.dump(2);
}

/// Helper: parses a MatPinType from its HLSL name string.
static MatPinType PinTypeFromHlsl(const std::string& s) {
    if (s == "float")               return MatPinType::Float;
    if (s == "float2")              return MatPinType::Float2;
    if (s == "float3")              return MatPinType::Float3;
    if (s == "float4")              return MatPinType::Float4;
    if (s == "Texture2D<float4>")   return MatPinType::Texture;
    return MatPinType::Float;
}

bool MaterialGraph::FromJson(const std::string& jsonStr, std::string* outError) {
    auto setError = [&](const std::string& msg) {
        if (outError) *outError = msg;
        return false;
    };

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        return setError(std::string("JSON parse error: ") + e.what());
    }

    // Validate version.
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        return setError("Missing or invalid 'version' field");
    }
    const int version = root["version"].get<int>();
    if (version != 1) {
        return setError("Unsupported version: " + std::to_string(version));
    }

    // Read metadata.
    name = root.value("name", "");
    domain = MaterialDomainFromString(root.value("domain", "Surface").c_str());
    shadingModel = MaterialShadingModelFromString(root.value("shadingModel", "DefaultLit").c_str());

    // Read MaterialParams (UE5-style PBR knobs). All fields optional;
    // missing fields keep their constructor defaults.
    if (root.contains("params") && root["params"].is_object()) {
        const auto& jp = root["params"];
        if (jp.contains("baseColorTint") && jp["baseColorTint"].is_array()) {
            const auto& tint = jp["baseColorTint"];
            for (size_t i = 0; i < std::min(tint.size(), size_t(3)); ++i) {
                params.baseColorTint[i] = tint[i].get<float>();
            }
        }
        if (jp.contains("roughnessMultiplier")) params.roughnessMultiplier = jp["roughnessMultiplier"].get<float>();
        if (jp.contains("metallicMultiplier"))  params.metallicMultiplier  = jp["metallicMultiplier"].get<float>();
        if (jp.contains("emissiveMultiplier"))  params.emissiveMultiplier  = jp["emissiveMultiplier"].get<float>();
        if (jp.contains("normalStrength"))      params.normalStrength      = jp["normalStrength"].get<float>();
        if (jp.contains("specularMultiplier"))  params.specularMultiplier  = jp["specularMultiplier"].get<float>();
        if (jp.contains("aoMultiplier"))        params.aoMultiplier        = jp["aoMultiplier"].get<float>();
    }

    // Parse expressions.
    if (!root.contains("expressions") || !root["expressions"].is_array()) {
        return setError("Missing or invalid 'expressions' array");
    }

    expressions_.clear();
    uint32_t maxId = 0;

    // Pending named srcPin connections — resolved after all expressions
    // are loaded so that the source expression's output pins are visible.
    struct PendingSrcPinName {
        uint32_t    dstExprId;
        uint32_t    dstPinId;
        std::string srcPinName;
    };
    std::vector<PendingSrcPinName> pendingSrcPinNames;

    for (const auto& jExpr : root["expressions"]) {
        if (!jExpr.contains("id") || !jExpr.contains("type")) {
            return setError("Expression missing 'id' or 'type'");
        }

        const uint32_t id = jExpr["id"].get<uint32_t>();
        const std::string typeStr = jExpr["type"].get<std::string>();
        const MatExprType type = MatExprTypeFromString(typeStr.c_str());

        // Create expression with factory defaults, then overlay JSON data.
        MaterialExpression expr = MaterialExpression::Create(type, id);

        if (jExpr.contains("name") && jExpr["name"].is_string()) {
            expr.name = jExpr["name"].get<std::string>();
        }

        if (jExpr.contains("pos") && jExpr["pos"].is_array() && jExpr["pos"].size() >= 2) {
            expr.posX = jExpr["pos"][0].get<float>();
            expr.posY = jExpr["pos"][1].get<float>();
        }

        // Parse input pin connections and defaults.
        // Pin lookup follows the UE5 / Godot pattern: prefer NAMED pins.
        // Accepted JSON forms (in priority order):
        //   { "dstPin": "BaseColor", "srcExpr": 2, "srcPin": "RGB" }   ← preferred
        //   { "pin": 0,              "srcExpr": 2, "srcPin": 0     }   ← legacy
        //   { "pin": "BaseColor",    "srcExpr": 2, "srcPin": "RGB" }   ← also accepted
        // The loader resolves strings to pin IDs against the factory-created
        // pins (whose names are declared in material_expression.cpp Setup*).
        // Connecting an unknown pin name is a hard error to surface typos.
        if (jExpr.contains("inputs") && jExpr["inputs"].is_array()) {
            for (const auto& jPin : jExpr["inputs"]) {
                // Resolve target pin via "dstPin" (named) or "pin" (legacy int/string).
                MatPin* targetPin = nullptr;
                auto resolveByNameOrId = [&](const nlohmann::json& v) -> MatPin* {
                    if (v.is_string()) {
                        const std::string& nm = v.get_ref<const std::string&>();
                        for (auto& pin : expr.inputs) if (pin.name == nm) return &pin;
                        return nullptr;
                    }
                    if (v.is_number_unsigned() || v.is_number_integer()) {
                        const uint32_t pid = v.get<uint32_t>();
                        for (auto& pin : expr.inputs) if (pin.id == pid) return &pin;
                        return nullptr;
                    }
                    return nullptr;
                };
                if (jPin.contains("dstPin")) {
                    targetPin = resolveByNameOrId(jPin["dstPin"]);
                } else if (jPin.contains("pin")) {
                    targetPin = resolveByNameOrId(jPin["pin"]);
                }
                if (!targetPin) {
                    // Surface the typo so authors don't get silent dropped pins.
                    std::string what = jPin.contains("dstPin")
                        ? jPin["dstPin"].dump()
                        : (jPin.contains("pin") ? jPin["pin"].dump() : std::string("<missing>"));
                    UZ_LOG(Material, Warning,
                           "MaterialGraph: expression {} ({}): unknown input pin {} — skipping",
                           expr.id, MatExprTypeToString(expr.type), what);
                    continue;
                }

                // Override default values if present.
                if (jPin.contains("default") && jPin["default"].is_array()) {
                    const auto& defs = jPin["default"];
                    for (size_t i = 0; i < std::min(defs.size(), size_t(4)); ++i) {
                        targetPin->defaultValue[i] = defs[i].get<float>();
                    }
                }

                // Override type if present.
                if (jPin.contains("type") && jPin["type"].is_string()) {
                    targetPin->type = PinTypeFromHlsl(jPin["type"].get<std::string>());
                }

                // Set connection if present. srcPin can be int (legacy) or
                // string (new). Strings are resolved against the source
                // expression's output pins by name. Source resolution is
                // deferred since the source expression may not yet be parsed
                // — we store the raw value and patch up after the loop.
                if (jPin.contains("srcExpr") && jPin.contains("srcPin")) {
                    targetPin->connectedExprId = jPin["srcExpr"].get<uint32_t>();
                    const auto& jSrcPin = jPin["srcPin"];
                    if (jSrcPin.is_string()) {
                        // Encode the name as a sentinel: store hash in
                        // connectedPinId temporarily, mark with high bit set so
                        // the second pass knows it's a name (not an ID). Use a
                        // side-table instead to avoid bit collision.
                        targetPin->connectedPinId = UINT32_MAX; // placeholder
                        // Track for post-pass resolution.
                        pendingSrcPinNames.push_back({
                            expr.id, targetPin->id,
                            jSrcPin.get<std::string>()
                        });
                    } else {
                        targetPin->connectedPinId = jSrcPin.get<uint32_t>();
                    }
                }
            }
        }

        // Parse output pin overrides (type changes).
        if (jExpr.contains("outputs") && jExpr["outputs"].is_array()) {
            for (const auto& jPin : jExpr["outputs"]) {
                const uint32_t pinId = jPin.value("pin", UINT32_MAX);
                for (auto& pin : expr.outputs) {
                    if (pin.id == pinId && jPin.contains("type") && jPin["type"].is_string()) {
                        pin.type = PinTypeFromHlsl(jPin["type"].get<std::string>());
                    }
                }
            }
        }

        // Parse node-specific parameters, overlaying on factory defaults.
        if (jExpr.contains("scalarParams") && jExpr["scalarParams"].is_object()) {
            for (auto& [key, val] : jExpr["scalarParams"].items()) {
                if (val.is_number()) {
                    expr.scalarParams[key] = val.get<float>();
                }
            }
        }
        if (jExpr.contains("stringParams") && jExpr["stringParams"].is_object()) {
            for (auto& [key, val] : jExpr["stringParams"].items()) {
                if (val.is_string()) {
                    expr.stringParams[key] = val.get<std::string>();
                }
            }
        }
        if (jExpr.contains("intParams") && jExpr["intParams"].is_object()) {
            for (auto& [key, val] : jExpr["intParams"].items()) {
                if (val.is_number_integer()) {
                    expr.intParams[key] = val.get<int32_t>();
                }
            }
        }

        if (id > maxId) maxId = id;
        expressions_.push_back(std::move(expr));
    }

    nextExprId_ = maxId + 1;

    // ── Resolve pending named srcPin connections ───────────────────────
    // Names like "RGB" / "G" / "Result" are looked up against the source
    // expression's output pins. Done in a second pass so authors can put
    // expressions in any order in the JSON.
    for (const auto& pending : pendingSrcPinNames) {
        // Find the destination input pin first.
        MatPin* dstPin = nullptr;
        for (auto& e : expressions_) {
            if (e.id != pending.dstExprId) continue;
            for (auto& p : e.inputs) {
                if (p.id == pending.dstPinId) { dstPin = &p; break; }
            }
            break;
        }
        if (!dstPin) continue;

        // Find source expression and its output pin by name.
        MaterialExpression* srcExpr = nullptr;
        for (auto& e : expressions_) {
            if (e.id == dstPin->connectedExprId) { srcExpr = &e; break; }
        }
        if (!srcExpr) {
            UZ_LOG(Material, Warning,
                   "MaterialGraph: srcExpr {} for input '{}' not found",
                   dstPin->connectedExprId, dstPin->name);
            dstPin->connectedExprId = 0;
            dstPin->connectedPinId  = 0;
            continue;
        }
        bool resolved = false;
        for (const auto& outPin : srcExpr->outputs) {
            if (outPin.name == pending.srcPinName) {
                dstPin->connectedPinId = outPin.id;
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            UZ_LOG(Material, Warning,
                   "MaterialGraph: source expression {} ({}) has no output pin named '{}' "
                   "(input '{}' will be unconnected)",
                   srcExpr->id, MatExprTypeToString(srcExpr->type),
                   pending.srcPinName, dstPin->name);
            dstPin->connectedExprId = 0;
            dstPin->connectedPinId  = 0;
        }
    }

    // Verify PBROutput is present.
    if (expressions_.empty() || expressions_[0].type != MatExprType::PBROutput) {
        // Try to find it and move it to front.
        bool found = false;
        for (size_t i = 0; i < expressions_.size(); ++i) {
            if (expressions_[i].type == MatExprType::PBROutput) {
                if (i != 0) std::swap(expressions_[0], expressions_[i]);
                found = true;
                break;
            }
        }
        if (!found) {
            return setError("Graph must contain a PBROutput expression");
        }
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O
// ─────────────────────────────────────────────────────────────────────────────

bool MaterialGraph::SaveToFile(const std::string& path) const {
    std::ofstream file(path, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        UZ_LOG(Material, Error, "Cannot open file for writing: '{}'", path);
        return false;
    }

    const std::string json = ToJson();
    file.write(json.data(), static_cast<std::streamsize>(json.size()));
    file.close();

    if (file.fail()) {
        UZ_LOG(Material, Error, "Failed to write material file: '{}'", path);
        return false;
    }

    UZ_LOG(Material, Info, "Saved material graph: '{}' ({} expressions)", path, expressions_.size());
    return true;
}

bool MaterialGraph::LoadFromFile(const std::string& path, std::string* outError) {
    std::ifstream file(path, std::ios::in);
    if (!file.is_open()) {
        const std::string msg = "Cannot open material file: '" + path + "'";
        UZ_LOG(Material, Error, "{}", msg);
        if (outError) *outError = msg;
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    file.close();

    if (!FromJson(ss.str(), outError)) {
        UZ_LOG(Material, Error, "Failed to parse material file: '{}'", path);
        return false;
    }

    UZ_LOG(Material, Info, "Loaded material graph: '{}' ({} expressions)", path, expressions_.size());
    return true;
}

} // namespace uz
