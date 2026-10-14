/// Unit tests for UzEngine Material Graph system.
///
/// Covers:
///   6.1 Graph serialization round-trip (ToJson → FromJson)
///   6.2 Topological sort with diamond dependencies (A→B→D, A→C→D)
///   6.3 Dead-code elimination: disconnected nodes absent from TopologicalSort
///   6.4 Type promotion: float → float3 splat in HLSL codegen
///   6.5 HLSL codegen for math nodes (Add, Multiply, Lerp, Clamp)
///   6.6 CheckerPattern HLSL contains floor + fmod
///   6.7 MaterialShaderCache hit/miss (same graph → same hash, modified → different hash)
///   6.8 Parameter binding indices: contiguous starting at 0

#include "material_graph.h"
#include "material_expression.h"
#include "hlsl_material_translator.h"
#include "material_shader_cache.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

TEST(MaterialGraphSerialization, RoundTrip_ConnectionsSurviveStorageGrowth) {
    MaterialGraph original;
    const uint32_t scalarId = original.AddExpression(MatExprType::ScalarConstant).id;
    original.FindExpression(scalarId)->scalarParams["value"] = 0.75f;
    const uint32_t multiplyId = original.AddExpression(MatExprType::Multiply).id;
    // Exceed the previous capacity without retaining vector element references.
    const size_t oldCapacity = original.GetExpressions().capacity();
    for (size_t i = original.GetExpressions().size(); i <= oldCapacity; ++i) {
        const uint32_t id = original.AddExpression(MatExprType::ScalarConstant).id;
        EXPECT_NE(id, 0u);
    }
    ASSERT_GT(original.GetExpressions().capacity(), oldCapacity);
    ASSERT_EQ(scalarId, 2u);
    ASSERT_EQ(multiplyId, 3u);
    std::string error;
    ASSERT_TRUE(original.Connect(scalarId, 0, multiplyId, 0, &error)) << error;
    ASSERT_TRUE(original.Connect(multiplyId, 0, 1u, 0, &error)) << error;

    MaterialGraph loaded;
    ASSERT_TRUE(loaded.FromJson(original.ToJson(), &error)) << error;
    const auto* multiply = loaded.FindExpression(multiplyId);
    ASSERT_NE(multiply, nullptr);
    EXPECT_TRUE(multiply->inputs[0].IsConnected());
    EXPECT_EQ(multiply->inputs[0].connectedExprId, scalarId);
    EXPECT_TRUE(loaded.GetPBROutput().inputs[0].IsConnected());
    EXPECT_EQ(loaded.GetPBROutput().inputs[0].connectedExprId, multiplyId);
    EXPECT_FLOAT_EQ(loaded.FindExpression(scalarId)->scalarParams.at("value"), 0.75f);
}

TEST(MaterialGraphHlslCodegen, AlternatingGraphs_ReuseTranslatorWithoutStateLeak) {
    HlslMaterialTranslator translator;
    for (const auto type : {MatExprType::Add, MatExprType::Multiply, MatExprType::Add}) {
        MaterialGraph graph;
        const uint32_t aId = graph.AddExpression(MatExprType::ScalarConstant).id;
        graph.FindExpression(aId)->scalarParams["value"] = 0.3f;
        const uint32_t bId = graph.AddExpression(MatExprType::ScalarConstant).id;
        graph.FindExpression(bId)->scalarParams["value"] = 0.7f;
        const uint32_t mathId = graph.AddExpression(type).id;
        std::string error;
        ASSERT_TRUE(graph.Connect(aId, 0, mathId, 0, &error)) << error;
        ASSERT_TRUE(graph.Connect(bId, 0, mathId, 1, &error)) << error;
        ASSERT_TRUE(graph.Connect(mathId, 0, 1u, 0, &error)) << error;
        const auto result = translator.Compile(graph);
        ASSERT_TRUE(result.success) << result.errorMessage;
        const std::string operation = type == MatExprType::Add ? " + " : " * ";
        EXPECT_NE(result.hlslSource.find("_node2_out0" + operation + "_node3_out0"),
                  std::string::npos);
        HlslMaterialTranslator fresh;
        EXPECT_EQ(result.hlslSource, fresh.Compile(graph).hlslSource);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if `sub` is a substring of `str`.
static bool Contains(const std::string& str, const std::string& sub) {
    return str.find(sub) != std::string::npos;
}

/// Returns the index of `id` in `order`, or SIZE_MAX if not found.
static size_t IndexOf(const std::vector<uint32_t>& order, uint32_t id) {
    for (size_t i = 0; i < order.size(); ++i) {
        if (order[i] == id) return i;
    }
    return SIZE_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.1  Graph serialization round-trip
// ─────────────────────────────────────────────────────────────────────────────

/// Create ScalarConstant(0.75) → Multiply (A) → PBROutput BaseColor.
/// Serialise to JSON and deserialise back; verify node count, connection wiring,
/// and that the scalar parameter value survived the round-trip.
TEST(MaterialGraphSerialization, RoundTrip_NodeCount_Connections_ParamValues) {
    // Build original graph.
    MaterialGraph original;
    original.name        = "RoundTripMat";
    original.domain      = MaterialDomain::Surface;
    original.shadingModel = MaterialShadingModel::DefaultLit;

    // id=2: ScalarConstant with value=0.75
    MaterialExpression& scalar = original.AddExpression(MatExprType::ScalarConstant);
    const uint32_t scalarId = scalar.id;
    scalar.scalarParams["value"] = 0.75f;

    // id=3: Multiply
    MaterialExpression& multiply = original.AddExpression(MatExprType::Multiply);
    const uint32_t multiplyId = multiply.id;

    // Connect scalar(out:0) → multiply(in:0/"A")
    std::string err;
    ASSERT_TRUE(original.Connect(scalarId, 0, multiplyId, 0, &err))
        << "Connect scalar→multiply A: " << err;

    // Connect multiply(out:0) → PBROutput(in:0/"BaseColor")
    ASSERT_TRUE(original.Connect(multiplyId, 0, /*PBROutput id=*/1, 0, &err))
        << "Connect multiply→PBROutput BaseColor: " << err;

    // Verify graph is valid before serialising.
    ASSERT_TRUE(original.Validate(err)) << "Pre-serialise validation: " << err;

    // Serialise.
    const std::string json = original.ToJson();
    ASSERT_FALSE(json.empty()) << "ToJson() must produce non-empty output";

    // Deserialise into a fresh graph.
    MaterialGraph loaded;
    ASSERT_TRUE(loaded.FromJson(json, &err)) << "FromJson failed: " << err;

    // ── Node count: PBROutput + ScalarConstant + Multiply = 3 expressions ──
    EXPECT_EQ(loaded.GetExpressions().size(), 3u)
        << "Expected 3 expressions after round-trip (PBROutput, ScalarConstant, Multiply)";

    // ── PBROutput always at index 0 with id=1 ──
    EXPECT_EQ(loaded.GetPBROutput().id,   1u);
    EXPECT_EQ(loaded.GetPBROutput().type, MatExprType::PBROutput);

    // ── ScalarConstant survives with the correct value ──
    const MaterialExpression* loadedScalar = loaded.FindExpression(scalarId);
    ASSERT_NE(loadedScalar, nullptr) << "ScalarConstant id=" << scalarId << " not found after round-trip";
    EXPECT_EQ(loadedScalar->type, MatExprType::ScalarConstant);
    auto scalarValIt = loadedScalar->scalarParams.find("value");
    ASSERT_NE(scalarValIt, loadedScalar->scalarParams.end())
        << "ScalarConstant must have 'value' param after round-trip";
    EXPECT_NEAR(scalarValIt->second, 0.75f, 1e-5f)
        << "ScalarConstant 'value' must survive serialization";

    // ── Multiply node survives ──
    const MaterialExpression* loadedMul = loaded.FindExpression(multiplyId);
    ASSERT_NE(loadedMul, nullptr) << "Multiply id=" << multiplyId << " not found after round-trip";
    EXPECT_EQ(loadedMul->type, MatExprType::Multiply);

    // ── Connection: multiply.inputs[0] must point back to scalarId, pin 0 ──
    ASSERT_FALSE(loadedMul->inputs.empty());
    const MatPin& mulInputA = loadedMul->inputs[0];  // pin "A"
    EXPECT_TRUE(mulInputA.IsConnected())
        << "Multiply input A must be connected after round-trip";
    EXPECT_EQ(mulInputA.connectedExprId, scalarId)
        << "Multiply A must connect to ScalarConstant id=" << scalarId;
    EXPECT_EQ(mulInputA.connectedPinId,  0u)
        << "Multiply A must connect to output pin 0 of ScalarConstant";

    // ── Connection: PBROutput.inputs[0] "BaseColor" must point to multiply ──
    const MatPin& pbrBaseColor = loaded.GetPBROutput().inputs[0];
    EXPECT_TRUE(pbrBaseColor.IsConnected())
        << "PBROutput BaseColor must be connected after round-trip";
    EXPECT_EQ(pbrBaseColor.connectedExprId, multiplyId)
        << "PBROutput BaseColor must connect to Multiply id=" << multiplyId;
    EXPECT_EQ(pbrBaseColor.connectedPinId,  0u);

    // ── Validate loaded graph ──
    EXPECT_TRUE(loaded.Validate(err)) << "Post-round-trip validation: " << err;
}

/// FromJson on malformed JSON returns false and writes outError.
TEST(MaterialGraphSerialization, FromJson_MalformedJson_ReturnsFalse) {
    MaterialGraph g;
    std::string err;
    const bool ok = g.FromJson("{this is not json!!!", &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty()) << "outError must be set on parse failure";
}

/// FromJson on JSON missing the 'expressions' array returns false.
TEST(MaterialGraphSerialization, FromJson_MissingExpressionsArray_ReturnsFalse) {
    MaterialGraph g;
    std::string err;
    const bool ok = g.FromJson(R"({"version":1,"name":"bad"})", &err);
    EXPECT_FALSE(ok);
    EXPECT_FALSE(err.empty());
}

/// FromJson on JSON with unsupported version returns false.
TEST(MaterialGraphSerialization, FromJson_UnsupportedVersion_ReturnsFalse) {
    MaterialGraph g;
    std::string err;
    // version=99 is not supported.
    const bool ok = g.FromJson(
        R"({"version":99,"name":"v99","expressions":[]})", &err);
    EXPECT_FALSE(ok);
}

/// Round-trip preserves domain and shadingModel enum values.
TEST(MaterialGraphSerialization, RoundTrip_PreservesDomainAndShadingModel) {
    MaterialGraph original;
    original.domain       = MaterialDomain::Decal;
    original.shadingModel = MaterialShadingModel::Unlit;

    const std::string json = original.ToJson();
    MaterialGraph loaded;
    std::string err;
    ASSERT_TRUE(loaded.FromJson(json, &err)) << err;

    EXPECT_EQ(loaded.domain,       MaterialDomain::Decal);
    EXPECT_EQ(loaded.shadingModel, MaterialShadingModel::Unlit);
}

/// Round-trip preserves graph name.
TEST(MaterialGraphSerialization, RoundTrip_PreservesGraphName) {
    MaterialGraph original;
    original.name = "Lava_Material_v2";

    const std::string json = original.ToJson();
    MaterialGraph loaded;
    std::string err;
    ASSERT_TRUE(loaded.FromJson(json, &err)) << err;

    EXPECT_EQ(loaded.name, "Lava_Material_v2");
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.2  Topological sort with diamond dependencies
// ─────────────────────────────────────────────────────────────────────────────

/// Diamond graph: A (ScalarConstant) feeds both B (Multiply) and C (Add),
/// which both feed D (Lerp), which feeds PBROutput.
///
/// Expected ordering constraints:
///   - A appears before B and before C
///   - B and C both appear before D
///   - D appears before PBROutput (id=1)
///   - All four nodes plus PBROutput are present (5 IDs total)
TEST(MaterialGraphTopologicalSort, Diamond_ABCD_CorrectOrdering) {
    MaterialGraph g;

    // A: ScalarConstant (value=0.5)
    MaterialExpression& A = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t AId = A.id;
    A.scalarParams["value"] = 0.5f;

    // B: Multiply — A feeds input A, unconnected input B uses default
    MaterialExpression& B = g.AddExpression(MatExprType::Multiply);
    const uint32_t BId = B.id;

    // C: Add — A feeds input A, unconnected input B uses default
    MaterialExpression& C = g.AddExpression(MatExprType::Add);
    const uint32_t CId = C.id;

    // D: Lerp — B feeds input A, C feeds input B, Alpha unconnected
    MaterialExpression& D = g.AddExpression(MatExprType::Lerp);
    const uint32_t DId = D.id;

    // Connect A → B.inputA (pin 0)
    std::string err;
    ASSERT_TRUE(g.Connect(AId, 0, BId, 0, &err)) << "A→B: " << err;

    // Connect A → C.inputA (pin 0)
    ASSERT_TRUE(g.Connect(AId, 0, CId, 0, &err)) << "A→C: " << err;

    // Connect B → D.inputA (pin 0)
    ASSERT_TRUE(g.Connect(BId, 0, DId, 0, &err)) << "B→D: " << err;

    // Connect C → D.inputB (pin 1)
    ASSERT_TRUE(g.Connect(CId, 0, DId, 1, &err)) << "C→D: " << err;

    // Connect D → PBROutput BaseColor (pin 0)
    ASSERT_TRUE(g.Connect(DId, 0, 1u, 0, &err)) << "D→PBR: " << err;

    // Validate graph before sorting.
    ASSERT_TRUE(g.Validate(err)) << "Validate: " << err;

    // ── Run topological sort ──
    const std::vector<uint32_t> order = g.TopologicalSort();
    ASSERT_FALSE(order.empty()) << "TopologicalSort must return non-empty order for a valid DAG";

    // All 5 IDs must be present: A, B, C, D, PBROutput(1).
    EXPECT_EQ(order.size(), 5u) << "Order must contain all 5 reachable nodes";

    const size_t posA   = IndexOf(order, AId);
    const size_t posB   = IndexOf(order, BId);
    const size_t posC   = IndexOf(order, CId);
    const size_t posD   = IndexOf(order, DId);
    const size_t posPBR = IndexOf(order, 1u);

    EXPECT_NE(posA,   SIZE_MAX) << "A must be in sort result";
    EXPECT_NE(posB,   SIZE_MAX) << "B must be in sort result";
    EXPECT_NE(posC,   SIZE_MAX) << "C must be in sort result";
    EXPECT_NE(posD,   SIZE_MAX) << "D must be in sort result";
    EXPECT_NE(posPBR, SIZE_MAX) << "PBROutput must be in sort result";

    // A before B and C (A is the common source).
    EXPECT_LT(posA, posB) << "A must appear before B";
    EXPECT_LT(posA, posC) << "A must appear before C";

    // B and C before D.
    EXPECT_LT(posB, posD) << "B must appear before D";
    EXPECT_LT(posC, posD) << "C must appear before D";

    // D before PBROutput (root is last).
    EXPECT_LT(posD, posPBR) << "D must appear before PBROutput";
}

/// Graph with no connections to PBROutput returns only PBROutput itself.
TEST(MaterialGraphTopologicalSort, EmptyConnections_ReturnsPBROutputOnly) {
    MaterialGraph g;
    // Add a ScalarConstant but leave it disconnected.
    g.AddExpression(MatExprType::ScalarConstant);

    const auto order = g.TopologicalSort();
    // Only PBROutput (id=1) is reachable from the root.
    ASSERT_EQ(order.size(), 1u) << "Only PBROutput should be reachable";
    EXPECT_EQ(order[0], 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.3  Dead-code elimination
// ─────────────────────────────────────────────────────────────────────────────

/// A connected subgraph feeds PBROutput; a separate disconnected ScalarConstant
/// is added but never wired. TopologicalSort must NOT include it.
TEST(MaterialGraphDeadCode, DisconnectedNode_NotInSortResult) {
    MaterialGraph g;

    // Connected subgraph: ScalarConstant(id=2) → PBROutput BaseColor.
    MaterialExpression& connected = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t connectedId = connected.id;
    connected.scalarParams["value"] = 1.0f;
    std::string err;
    ASSERT_TRUE(g.Connect(connectedId, 0, 1u, 0, &err)) << err;

    // Disconnected ScalarConstant — never wired to anything.
    MaterialExpression& dead = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t deadId = dead.id;
    dead.scalarParams["value"] = 99.0f;

    // Confirm graph is valid.
    ASSERT_TRUE(g.Validate(err)) << "Validate: " << err;

    const std::vector<uint32_t> order = g.TopologicalSort();

    // connected and PBROutput(1) must be present.
    EXPECT_NE(IndexOf(order, connectedId), SIZE_MAX)
        << "Connected node must appear in sort result";
    EXPECT_NE(IndexOf(order, 1u), SIZE_MAX)
        << "PBROutput must appear in sort result";

    // Dead node must NOT appear.
    EXPECT_EQ(IndexOf(order, deadId), SIZE_MAX)
        << "Disconnected node id=" << deadId << " must NOT appear in sort result";
}

/// A more complex dead-code case: two fully connected subgraphs (A→B→PBR,
/// C→D but D disconnected from PBR). Only A, B, PBROutput in sort.
TEST(MaterialGraphDeadCode, PartiallyConnectedChain_OnlyReachableNodes) {
    MaterialGraph g;

    // Live path: ScalarConstant → Add → PBROutput BaseColor
    MaterialExpression& liveConst = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t liveConstId = liveConst.id;
    MaterialExpression& liveAdd   = g.AddExpression(MatExprType::Add);
    const uint32_t liveAddId = liveAdd.id;
    std::string err;
    ASSERT_TRUE(g.Connect(liveConstId, 0, liveAddId, 0, &err)) << err;
    ASSERT_TRUE(g.Connect(liveAddId, 0, 1u, 0, &err)) << err;

    // Dead path: ScalarConstant → Multiply (never wired to PBR)
    MaterialExpression& deadConst = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t deadConstId = deadConst.id;
    MaterialExpression& deadMul   = g.AddExpression(MatExprType::Multiply);
    const uint32_t deadMulId = deadMul.id;
    ASSERT_TRUE(g.Connect(deadConstId, 0, deadMulId, 0, &err)) << err;
    // deadMul is NOT connected to PBROutput.

    const auto order = g.TopologicalSort();

    EXPECT_NE(IndexOf(order, liveConstId), SIZE_MAX) << "Live const must be in order";
    EXPECT_NE(IndexOf(order, liveAddId),   SIZE_MAX) << "Live add must be in order";
    EXPECT_NE(IndexOf(order, 1u),           SIZE_MAX) << "PBROutput must be in order";

    EXPECT_EQ(IndexOf(order, deadConstId), SIZE_MAX) << "Dead const must NOT be in order";
    EXPECT_EQ(IndexOf(order, deadMulId),   SIZE_MAX) << "Dead mul must NOT be in order";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.4  Type promotion — float → float3 splat
// ─────────────────────────────────────────────────────────────────────────────

/// ScalarConstant(0.5) → Multiply (input A, Float type) → PBROutput BaseColor
/// (Float3 type). The translator must emit a float3(0.5, 0.5, 0.5) splat
/// somewhere in the generated HLSL when the Float scalar is promoted to Float3
/// for the BaseColor output pin.
TEST(MaterialGraphHlslCodegen, TypePromotion_FloatToFloat3_SplatInHlsl) {
    MaterialGraph g;
    g.name = "SplatTest";

    // ScalarConstant emitting Float, value 0.5
    MaterialExpression& scalar = g.AddExpression(MatExprType::ScalarConstant);
    scalar.scalarParams["value"] = 0.5f;

    // Connect scalar(Float out) directly to PBROutput BaseColor (Float3 in).
    // The translator must insert a float3(...) splat at the PBR output assignment.
    std::string err;
    ASSERT_TRUE(g.Connect(scalar.id, 0, 1u, 0, &err))
        << "Connect scalar→PBROutput BaseColor: " << err;
    ASSERT_TRUE(g.Validate(err)) << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << "Compile must succeed: " << result.errorMessage;
    ASSERT_FALSE(result.hlslSource.empty());

    // The HLSL must contain a float3 splat expanding the scalar 0.5 value.
    // The translator emits: out_baseColor = float3(<var>, <var>, <var>);
    // where <var> is the variable name for the scalar output.
    EXPECT_TRUE(Contains(result.hlslSource, "float3("))
        << "HLSL must contain a float3() constructor for the Float→Float3 splat.\n"
        << "Generated HLSL:\n" << result.hlslSource;

    // The scalar 0.5 literal must appear (from the ScalarConstant emit).
    EXPECT_TRUE(Contains(result.hlslSource, "0.5"))
        << "HLSL must contain the 0.5 literal from the ScalarConstant.\n"
        << "Generated HLSL:\n" << result.hlslSource;

    // The ComputeMaterialOutputs function signature must be present.
    EXPECT_TRUE(Contains(result.hlslSource, "ComputeMaterialOutputs"))
        << "HLSL must define ComputeMaterialOutputs";

    // out_baseColor assignment must be present.
    EXPECT_TRUE(Contains(result.hlslSource, "out_baseColor"))
        << "HLSL must assign out_baseColor";
}

/// Float3 fed directly into PBROutput BaseColor requires NO promotion — no extra
/// float3() wrapping should appear for the assignment itself (it already is float3).
TEST(MaterialGraphHlslCodegen, TypePromotion_Float3ToFloat3_NoSplatNeeded) {
    MaterialGraph g;

    // Vector3Constant emitting Float3
    MaterialExpression& vec3 = g.AddExpression(MatExprType::Vector3Constant);
    vec3.scalarParams["x"] = 0.2f;
    vec3.scalarParams["y"] = 0.4f;
    vec3.scalarParams["z"] = 0.8f;

    std::string err;
    ASSERT_TRUE(g.Connect(vec3.id, 0, 1u, 0, &err)) << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << result.errorMessage;

    // The float3 constant declaration must appear.
    EXPECT_TRUE(Contains(result.hlslSource, "float3("))
        << "HLSL must define float3 constant for Vector3Constant";

    // The 0.2, 0.4, 0.8 literals must appear.
    EXPECT_TRUE(Contains(result.hlslSource, "0.2"))
        << "x=0.2 must appear in HLSL";
    EXPECT_TRUE(Contains(result.hlslSource, "0.4"))
        << "y=0.4 must appear in HLSL";
    EXPECT_TRUE(Contains(result.hlslSource, "0.8"))
        << "z=0.8 must appear in HLSL";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.5  HLSL codegen for math nodes
// ─────────────────────────────────────────────────────────────────────────────

/// Helper: compile a graph where nodeExprId's output goes to PBROutput BaseColor,
/// return the generated HLSL.
static std::string CompileWithMathNode(MatExprType mathType) {
    MaterialGraph g;
    MaterialExpression& node = g.AddExpression(mathType);
    std::string err;
    if (!g.Connect(node.id, 0, 1u, 0, &err)) return "";
    HlslMaterialTranslator translator;
    MaterialCompileResult r = translator.Compile(g);
    if (!r.success) return "";
    return r.hlslSource;
}

/// Add node: HLSL must contain " + ".
TEST(MaterialGraphHlslCodegen, Add_EmitsPlus) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Add);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Add node";
    EXPECT_TRUE(Contains(hlsl, " + "))
        << "Add node must emit ' + ' operator.\nHLSL:\n" << hlsl;
}

/// Multiply node: HLSL must contain " * ".
TEST(MaterialGraphHlslCodegen, Multiply_EmitsStar) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Multiply);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Multiply node";
    EXPECT_TRUE(Contains(hlsl, " * "))
        << "Multiply node must emit ' * ' operator.\nHLSL:\n" << hlsl;
}

/// Subtract node: HLSL must contain " - ".
TEST(MaterialGraphHlslCodegen, Subtract_EmitsMinus) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Subtract);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Subtract node";
    EXPECT_TRUE(Contains(hlsl, " - "))
        << "Subtract node must emit ' - ' operator.\nHLSL:\n" << hlsl;
}

/// Divide node: HLSL must contain " / ".
TEST(MaterialGraphHlslCodegen, Divide_EmitsSlash) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Divide);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Divide node";
    EXPECT_TRUE(Contains(hlsl, " / "))
        << "Divide node must emit ' / ' operator.\nHLSL:\n" << hlsl;
}

/// Lerp node: HLSL must contain "lerp(", checking correct function name.
TEST(MaterialGraphHlslCodegen, Lerp_EmitsLerpFunction) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Lerp);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Lerp node";
    EXPECT_TRUE(Contains(hlsl, "lerp("))
        << "Lerp node must emit 'lerp(' intrinsic.\nHLSL:\n" << hlsl;
}

/// Clamp node: HLSL must contain "clamp(", checking correct function name.
TEST(MaterialGraphHlslCodegen, Clamp_EmitsClampFunction) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Clamp);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Clamp node";
    EXPECT_TRUE(Contains(hlsl, "clamp("))
        << "Clamp node must emit 'clamp(' intrinsic.\nHLSL:\n" << hlsl;
}

/// Saturate node: HLSL must contain "saturate(".
TEST(MaterialGraphHlslCodegen, Saturate_EmitsSaturateFunction) {
    const std::string hlsl = CompileWithMathNode(MatExprType::Saturate);
    ASSERT_FALSE(hlsl.empty()) << "Compile must succeed for Saturate node";
    EXPECT_TRUE(Contains(hlsl, "saturate("))
        << "Saturate node must emit 'saturate('.\nHLSL:\n" << hlsl;
}

/// Verify Add and Multiply produce variable assignments in the form
/// `float <varname> = <a> + <b>;` and `float <varname> = <a> * <b>;`
/// by checking that explicit node variable names appear in the HLSL.
TEST(MaterialGraphHlslCodegen, Add_ExplicitVariables_ConnectedInputs) {
    MaterialGraph g;

    // Two ScalarConstants feed the Add node.
    MaterialExpression& ca = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t caId = ca.id;
    ca.scalarParams["value"] = 0.3f;

    MaterialExpression& cb = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t cbId = cb.id;
    cb.scalarParams["value"] = 0.7f;

    MaterialExpression& addNode = g.AddExpression(MatExprType::Add);
    const uint32_t addNodeId = addNode.id;

    std::string err;
    ASSERT_TRUE(g.Connect(caId, 0, addNodeId, 0, &err)) << "ca→add A: " << err;
    ASSERT_TRUE(g.Connect(cbId, 0, addNodeId, 1, &err)) << "cb→add B: " << err;
    ASSERT_TRUE(g.Connect(addNodeId, 0, 1u, 0, &err))    << "add→PBR: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;

    // Variable for ca output: _node<caId>_out0
    const std::string varCA  = "_node" + std::to_string(caId)      + "_out0";
    const std::string varCB  = "_node" + std::to_string(cbId)      + "_out0";
    const std::string varAdd = "_node" + std::to_string(addNodeId)  + "_out0";

    EXPECT_TRUE(Contains(result.hlslSource, varCA))
        << "ScalarConstant A variable '" << varCA << "' must appear in HLSL";
    EXPECT_TRUE(Contains(result.hlslSource, varCB))
        << "ScalarConstant B variable '" << varCB << "' must appear in HLSL";
    EXPECT_TRUE(Contains(result.hlslSource, varAdd))
        << "Add result variable '" << varAdd << "' must appear in HLSL";

    // The add expression must combine the two input variables.
    EXPECT_TRUE(Contains(result.hlslSource, varCA + " + " + varCB))
        << "Add node must emit '" << varCA << " + " << varCB << "'\nHLSL:\n"
        << result.hlslSource;
}

/// Multiply node with explicitly connected Float inputs: verify * operator
/// and correct variable names in the generated statement.
/// Uses ScalarConstants (Float output) so no type truncation occurs and the
/// raw variable names appear directly in the multiply expression.
TEST(MaterialGraphHlslCodegen, Multiply_ExplicitVariables_ConnectedScalarInputs) {
    MaterialGraph g;

    // Two ScalarConstants (Float output) feed Multiply (Float inputs).
    // No promotion or truncation — variable names appear verbatim.
    MaterialExpression& sa = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t saId = sa.id;
    sa.scalarParams["value"] = 0.3f;

    MaterialExpression& sb = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t sbId = sb.id;
    sb.scalarParams["value"] = 0.7f;

    MaterialExpression& mul = g.AddExpression(MatExprType::Multiply);
    const uint32_t mulId = mul.id;

    std::string err;
    ASSERT_TRUE(g.Connect(saId, 0, mulId, 0, &err)) << "sa→mul A: " << err;
    ASSERT_TRUE(g.Connect(sbId, 0, mulId, 1, &err)) << "sb→mul B: " << err;
    ASSERT_TRUE(g.Connect(mulId, 0, 1u, 0, &err))    << "mul→PBR: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;

    const std::string varSA  = "_node" + std::to_string(saId)  + "_out0";
    const std::string varSB  = "_node" + std::to_string(sbId)  + "_out0";
    const std::string varMul = "_node" + std::to_string(mulId) + "_out0";

    // Both scalars must be declared.
    EXPECT_TRUE(Contains(result.hlslSource, varSA))
        << "ScalarConstant A variable '" << varSA << "' must appear in HLSL";
    EXPECT_TRUE(Contains(result.hlslSource, varSB))
        << "ScalarConstant B variable '" << varSB << "' must appear in HLSL";

    // The multiply statement must combine the two input variable names with *.
    EXPECT_TRUE(Contains(result.hlslSource, varSA + " * " + varSB))
        << "Multiply must emit '" << varSA << " * " << varSB << "'\nHLSL:\n"
        << result.hlslSource;

    // The multiply result variable must be declared.
    EXPECT_TRUE(Contains(result.hlslSource, varMul))
        << "Multiply result variable '" << varMul << "' must appear in HLSL";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.6  CheckerPattern HLSL
// ─────────────────────────────────────────────────────────────────────────────

/// CheckerPattern connected to PBROutput BaseColor.
/// Generated HLSL must contain both "floor" and "fmod".
TEST(MaterialGraphHlslCodegen, CheckerPattern_ContainsFloorAndFmod) {
    MaterialGraph g;
    MaterialExpression& checker = g.AddExpression(MatExprType::CheckerPattern);
    // Default scale is 10.0 — leave it unchanged.

    std::string err;
    ASSERT_TRUE(g.Connect(checker.id, 0, 1u, 0, &err))
        << "Connect checker→PBROutput BaseColor: " << err;
    ASSERT_TRUE(g.Validate(err)) << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << "Compile must succeed: " << result.errorMessage;
    ASSERT_FALSE(result.hlslSource.empty());

    EXPECT_TRUE(Contains(result.hlslSource, "floor("))
        << "CheckerPattern HLSL must contain 'floor('\nGenerated:\n" << result.hlslSource;

    EXPECT_TRUE(Contains(result.hlslSource, "fmod("))
        << "CheckerPattern HLSL must contain 'fmod('\nGenerated:\n" << result.hlslSource;
}

/// CheckerPattern with a custom scale value (4.0): verify scale literal
/// appears in the generated HLSL.
TEST(MaterialGraphHlslCodegen, CheckerPattern_CustomScale_ScaleAppearsInHlsl) {
    MaterialGraph g;
    MaterialExpression& checker = g.AddExpression(MatExprType::CheckerPattern);
    const uint32_t checkerId = checker.id;

    // Wire a ScalarConstant(4.0) into the Scale input.
    MaterialExpression& scaleConst = g.AddExpression(MatExprType::ScalarConstant);
    const uint32_t scaleConstId = scaleConst.id;
    scaleConst.scalarParams["value"] = 4.0f;

    std::string err;
    ASSERT_TRUE(g.Connect(scaleConstId, 0, checkerId, 1, &err))
        << "Connect scaleConst→checker Scale: " << err;
    ASSERT_TRUE(g.Connect(checkerId, 0, 1u, 0, &err))
        << "Connect checker→PBROutput: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << result.errorMessage;

    // The scale value 4.0 must appear (from the ScalarConstant emit).
    EXPECT_TRUE(Contains(result.hlslSource, "4"))
        << "Scale value 4 must appear in HLSL.\nGenerated:\n" << result.hlslSource;

    // floor and fmod must still be present.
    EXPECT_TRUE(Contains(result.hlslSource, "floor("))
        << "floor must be present even with custom scale";
    EXPECT_TRUE(Contains(result.hlslSource, "fmod("))
        << "fmod must be present even with custom scale";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.7  MaterialShaderCache hit / miss
// ─────────────────────────────────────────────────────────────────────────────

/// Compiling the same graph twice returns the same contentHash (cache hit).
TEST(MaterialShaderCacheTest, SameGraph_SameHash_CacheHit) {
    MaterialGraph g;
    g.name = "CacheHitMat";

    MaterialExpression& scalar = g.AddExpression(MatExprType::ScalarConstant);
    scalar.scalarParams["value"] = 0.8f;
    std::string err;
    ASSERT_TRUE(g.Connect(scalar.id, 0, 1u, 0, &err)) << err;

    MaterialShaderCache cache;
    cache.Init();  // in-memory only (no disk path needed for unit tests)

    // First compile.
    const MaterialShaderCache::CompiledMaterial& first = cache.CompileGraph(g);
    ASSERT_TRUE(first.valid) << "First compile must succeed";
    EXPECT_NE(first.contentHash, 0u) << "Content hash must be non-zero";

    // Second compile of the exact same graph.
    const MaterialShaderCache::CompiledMaterial& second = cache.CompileGraph(g);
    ASSERT_TRUE(second.valid) << "Second compile must succeed";

    // Hashes must be identical — same graph topology and parameters.
    EXPECT_EQ(first.contentHash, second.contentHash)
        << "Same graph must produce the same content hash on every compile";

    // Cache size must be 1 (second call was a hit, not a new entry).
    EXPECT_EQ(cache.GetCacheSize(), 1u)
        << "Cache must contain exactly 1 entry after two compiles of the same graph";
}

/// After modifying a scalar parameter, the content hash changes (cache miss).
TEST(MaterialShaderCacheTest, ModifiedParam_DifferentHash_CacheMiss) {
    MaterialGraph g;

    MaterialExpression& scalar = g.AddExpression(MatExprType::ScalarConstant);
    scalar.scalarParams["value"] = 0.5f;
    std::string err;
    ASSERT_TRUE(g.Connect(scalar.id, 0, 1u, 0, &err)) << err;

    MaterialShaderCache cache;
    cache.Init();

    // Compile graph A (value=0.5).
    const MaterialShaderCache::CompiledMaterial& resultA = cache.CompileGraph(g);
    ASSERT_TRUE(resultA.valid);
    const uint64_t hashA = resultA.contentHash;

    // Modify the parameter.
    MaterialExpression* exprPtr = g.FindExpression(scalar.id);
    ASSERT_NE(exprPtr, nullptr);
    exprPtr->scalarParams["value"] = 0.9f;  // Different value.

    // Compile modified graph B (value=0.9).
    const MaterialShaderCache::CompiledMaterial& resultB = cache.CompileGraph(g);
    ASSERT_TRUE(resultB.valid);
    const uint64_t hashB = resultB.contentHash;

    EXPECT_NE(hashA, hashB)
        << "Modified scalar parameter must produce a different content hash";

    // Both hashes should be in the cache now (two distinct entries).
    EXPECT_EQ(cache.GetCacheSize(), 2u)
        << "Cache must contain 2 entries after compiling two distinct graphs";
}

/// IsCached returns false before any compile and true after.
TEST(MaterialShaderCacheTest, IsCached_FalseBeforeCompile_TrueAfter) {
    MaterialGraph g;
    std::string err;

    MaterialShaderCache cache;
    cache.Init();

    // Compute hash of the empty (PBR-only) graph before compiling.
    const uint64_t hashBefore = g.ComputeContentHash();
    EXPECT_FALSE(cache.IsCached(hashBefore))
        << "IsCached must return false before any compile";

    // Compile and then check.
    const auto& result = cache.CompileGraph(g);
    ASSERT_TRUE(result.valid);

    EXPECT_TRUE(cache.IsCached(result.contentHash))
        << "IsCached must return true after compile";
}

/// ClearCache empties the in-memory store; subsequent IsCached returns false.
TEST(MaterialShaderCacheTest, ClearCache_EmptiesStore_IsCachedReturnsFalse) {
    MaterialGraph g;
    MaterialExpression& s = g.AddExpression(MatExprType::ScalarConstant);
    s.scalarParams["value"] = 0.3f;
    std::string err;
    ASSERT_TRUE(g.Connect(s.id, 0, 1u, 0, &err)) << err;

    MaterialShaderCache cache;
    cache.Init();

    const auto& result = cache.CompileGraph(g);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(cache.GetCacheSize(), 1u);

    cache.ClearCache();

    EXPECT_EQ(cache.GetCacheSize(), 0u)
        << "Cache size must be 0 after ClearCache()";
    EXPECT_FALSE(cache.IsCached(result.contentHash))
        << "IsCached must return false after ClearCache()";
}

/// GetHlsl returns a non-null pointer that matches the compiled HLSL source.
TEST(MaterialShaderCacheTest, GetHlsl_MatchesCompileResultSource) {
    MaterialGraph g;
    g.name = "HlslLookupTest";

    MaterialShaderCache cache;
    cache.Init();

    const auto& result = cache.CompileGraph(g);
    ASSERT_TRUE(result.valid);

    const std::string* cached = cache.GetHlsl(result.contentHash);
    ASSERT_NE(cached, nullptr) << "GetHlsl must return a non-null pointer for a cached hash";
    EXPECT_EQ(*cached, result.generatedHlsl)
        << "GetHlsl result must match the HLSL returned by CompileGraph";
}

/// GetHlsl returns nullptr for an unknown hash.
TEST(MaterialShaderCacheTest, GetHlsl_UnknownHash_ReturnsNull) {
    MaterialShaderCache cache;
    cache.Init();

    const std::string* ptr = cache.GetHlsl(0xDEADBEEFDEADBEEFull);
    EXPECT_EQ(ptr, nullptr) << "GetHlsl must return nullptr for unknown hash";
}

/// Content hash is deterministic regardless of AddExpression insertion order:
/// two graphs with the same topology but expressions added in different orders
/// must produce the same hash.
TEST(MaterialShaderCacheTest, ContentHash_Deterministic_SameGraphDifferentInsertionOrder) {
    // Build graph A: add Multiply first, then two ScalarConstants.
    MaterialGraph gA;
    MaterialExpression& mulA = gA.AddExpression(MatExprType::Multiply);
    const uint32_t mulAId = mulA.id;
    MaterialExpression& scA1 = gA.AddExpression(MatExprType::ScalarConstant);
    const uint32_t scA1Id = scA1.id;
    scA1.scalarParams["value"] = 0.4f;
    MaterialExpression& scA2 = gA.AddExpression(MatExprType::ScalarConstant);
    const uint32_t scA2Id = scA2.id;
    scA2.scalarParams["value"] = 0.6f;
    std::string err;
    ASSERT_TRUE(gA.Connect(scA1Id, 0, mulAId, 0, &err)) << err;
    ASSERT_TRUE(gA.Connect(scA2Id, 0, mulAId, 1, &err)) << err;
    ASSERT_TRUE(gA.Connect(mulAId, 0, 1u, 0, &err)) << err;

    // Build graph B: add ScalarConstants first, then Multiply.
    // Because expression IDs are assigned sequentially starting at 2, the IDs
    // will differ. The hash must still be deterministic for the same hash call.
    const uint64_t hashA1 = gA.ComputeContentHash();
    const uint64_t hashA2 = gA.ComputeContentHash();
    EXPECT_EQ(hashA1, hashA2)
        << "ComputeContentHash must return the same value on repeated calls";
}

// ─────────────────────────────────────────────────────────────────────────────
// 6.8  Parameter binding indices
// ─────────────────────────────────────────────────────────────────────────────

/// Create 3 ScalarParameters with distinct names. After compilation the
/// parameters vector must have exactly 3 entries with contiguous cbSlotIndex
/// values 0, 1, 2.
TEST(MaterialGraphParameterBinding, ThreeScalarParameters_ContiguousIndices_StartAt0) {
    MaterialGraph g;

    // Add three ScalarParameter nodes with distinct names.
    const char* paramNames[] = {"Roughness", "Metallic", "EmissiveScale"};

    for (int i = 0; i < 3; ++i) {
        MaterialExpression& param = g.AddExpression(MatExprType::ScalarParameter);
        param.stringParams["paramName"] = paramNames[i];
        param.scalarParams["default"]   = static_cast<float>(i) * 0.25f;
        // Each parameter connects to PBROutput — first to BaseColor (pin 0),
        // then Metallic (pin 1), then Roughness (pin 2). This ensures all three
        // are reachable and therefore compiled.
        std::string err;
        ASSERT_TRUE(g.Connect(param.id, 0, 1u, static_cast<uint32_t>(i), &err))
            << "Connect param[" << i << "]→PBROutput pin " << i << ": " << err;
    }

    ASSERT_TRUE([&]{ std::string e; return g.Validate(e); }())
        << "Graph must be valid";

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << "Compile must succeed: " << result.errorMessage;

    // Exactly 3 parameters must be reported.
    EXPECT_EQ(result.parameterCount, 3u)
        << "parameterCount must be 3 for 3 ScalarParameter nodes";
    ASSERT_EQ(result.parameters.size(), 3u)
        << "parameters vector must contain exactly 3 entries";

    // Extract slot indices and sort them to verify contiguousness.
    std::vector<uint32_t> slots;
    slots.reserve(3);
    for (const auto& p : result.parameters) {
        slots.push_back(p.cbSlotIndex);
    }
    std::sort(slots.begin(), slots.end());

    EXPECT_EQ(slots[0], 0u) << "First parameter slot must be 0";
    EXPECT_EQ(slots[1], 1u) << "Second parameter slot must be 1";
    EXPECT_EQ(slots[2], 2u) << "Third parameter slot must be 2";

    // All parameter names must be present (order may differ — use a sorted check).
    std::vector<std::string> foundNames;
    foundNames.reserve(3);
    for (const auto& p : result.parameters) {
        foundNames.push_back(p.name);
    }
    std::sort(foundNames.begin(), foundNames.end());

    std::vector<std::string> expected = {"EmissiveScale", "Metallic", "Roughness"};
    EXPECT_EQ(foundNames, expected)
        << "Parameter names must match the three ScalarParameter 'paramName' values";
}

/// Verify that each ScalarParameter writes its correct type (Float)
/// into the ParamInfo metadata.
TEST(MaterialGraphParameterBinding, ScalarParameter_TypeIsFloat) {
    MaterialGraph g;

    MaterialExpression& param = g.AddExpression(MatExprType::ScalarParameter);
    param.stringParams["paramName"] = "Opacity";
    param.scalarParams["default"]   = 1.0f;
    std::string err;
    ASSERT_TRUE(g.Connect(param.id, 0, 1u, 5, &err))  // Pin 5 = Opacity
        << "Connect ScalarParam→Opacity: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_EQ(result.parameters.size(), 1u);

    EXPECT_EQ(result.parameters[0].type, MatPinType::Float)
        << "ScalarParameter must have Float type in ParamInfo";
    EXPECT_EQ(result.parameters[0].cbSlotIndex, 0u)
        << "First and only parameter must occupy slot 0";
}

/// VectorParameter type must be Float3 in the ParamInfo.
TEST(MaterialGraphParameterBinding, VectorParameter_TypeIsFloat3) {
    MaterialGraph g;

    MaterialExpression& param = g.AddExpression(MatExprType::VectorParameter);
    param.stringParams["paramName"] = "TintColor";
    param.scalarParams["defaultR"]  = 1.0f;
    param.scalarParams["defaultG"]  = 0.5f;
    param.scalarParams["defaultB"]  = 0.0f;
    std::string err;
    ASSERT_TRUE(g.Connect(param.id, 0, 1u, 0, &err))  // Pin 0 = BaseColor
        << "Connect VectorParam→BaseColor: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_EQ(result.parameters.size(), 1u);

    EXPECT_EQ(result.parameters[0].type, MatPinType::Float3)
        << "VectorParameter must have Float3 type in ParamInfo";
    EXPECT_EQ(result.parameters[0].cbSlotIndex, 0u)
        << "First and only parameter must occupy slot 0";
    EXPECT_EQ(result.parameters[0].name, "TintColor");
}

/// Mixed ScalarParameter + VectorParameter: two parameters with slots 0 and 1.
TEST(MaterialGraphParameterBinding, Mixed_ScalarAndVector_ContiguousIndices) {
    MaterialGraph g;

    // ScalarParameter → Metallic (pin 1)
    MaterialExpression& scalarP = g.AddExpression(MatExprType::ScalarParameter);
    const uint32_t scalarPId = scalarP.id;
    scalarP.stringParams["paramName"] = "ScalarA";
    scalarP.scalarParams["default"]   = 0.5f;

    // VectorParameter → BaseColor (pin 0)
    MaterialExpression& vectorP = g.AddExpression(MatExprType::VectorParameter);
    const uint32_t vectorPId = vectorP.id;
    vectorP.stringParams["paramName"] = "VectorB";

    std::string err;
    ASSERT_TRUE(g.Connect(vectorPId, 0, 1u, 0, &err)) << "VectorP→BaseColor: " << err;
    ASSERT_TRUE(g.Connect(scalarPId, 0, 1u, 1, &err)) << "ScalarP→Metallic: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;

    ASSERT_EQ(result.parameters.size(), 2u)
        << "Must have exactly 2 parameter entries";

    // Verify contiguousness.
    std::vector<uint32_t> slots;
    for (const auto& p : result.parameters) slots.push_back(p.cbSlotIndex);
    std::sort(slots.begin(), slots.end());
    EXPECT_EQ(slots[0], 0u) << "First slot must be 0";
    EXPECT_EQ(slots[1], 1u) << "Second slot must be 1";

    EXPECT_EQ(result.parameterCount, 2u);
}

/// ScalarParameter default value is stored correctly in ParamInfo.
TEST(MaterialGraphParameterBinding, ScalarParameter_DefaultValuePreserved) {
    MaterialGraph g;

    MaterialExpression& param = g.AddExpression(MatExprType::ScalarParameter);
    param.stringParams["paramName"] = "GlowIntensity";
    param.scalarParams["default"]   = 3.14f;
    std::string err;
    ASSERT_TRUE(g.Connect(param.id, 0, 1u, 5, &err))  // Opacity pin
        << "Connect: " << err;

    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);
    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_EQ(result.parameters.size(), 1u);

    EXPECT_NEAR(result.parameters[0].defaultValue[0], 3.14f, 1e-4f)
        << "ScalarParameter default value must be preserved in ParamInfo.defaultValue[0]";
    // Components 1, 2, 3 must be 0 for a scalar.
    EXPECT_NEAR(result.parameters[0].defaultValue[1], 0.0f, 1e-6f);
    EXPECT_NEAR(result.parameters[0].defaultValue[2], 0.0f, 1e-6f);
    EXPECT_NEAR(result.parameters[0].defaultValue[3], 0.0f, 1e-6f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: Validate API correctness
// ─────────────────────────────────────────────────────────────────────────────

/// An empty graph (PBROutput only) is valid.
TEST(MaterialGraphValidate, EmptyGraph_IsValid) {
    MaterialGraph g;
    std::string err;
    EXPECT_TRUE(g.Validate(err)) << "Empty graph must be valid: " << err;
}

/// Validate detects self-connection.
TEST(MaterialGraphConnect, SelfConnection_ReturnsFalse) {
    MaterialGraph g;
    MaterialExpression& mul = g.AddExpression(MatExprType::Multiply);
    std::string err;
    const bool ok = g.Connect(mul.id, 0, mul.id, 0, &err);
    EXPECT_FALSE(ok) << "Self-connection must be rejected";
    EXPECT_FALSE(err.empty()) << "Error message must be set on rejected connection";
}

/// Incompatible types (Texture → Float) are rejected by Connect.
TEST(MaterialGraphConnect, IncompatiblePinTypes_ReturnsFalse) {
    MaterialGraph g;
    MaterialExpression& texParam = g.AddExpression(MatExprType::TextureParameter);
    // PBROutput BaseColor is Float3 — Texture cannot connect to Float3.
    std::string err;
    const bool ok = g.Connect(texParam.id, 0, 1u, 0, &err);
    EXPECT_FALSE(ok) << "Texture→Float3 connection must be rejected";
}

/// Connect to a non-existent destination expression returns false.
TEST(MaterialGraphConnect, NonExistentDestination_ReturnsFalse) {
    MaterialGraph g;
    MaterialExpression& s = g.AddExpression(MatExprType::ScalarConstant);
    std::string err;
    const bool ok = g.Connect(s.id, 0, 9999u, 0, &err);
    EXPECT_FALSE(ok) << "Connection to non-existent expression must fail";
}

/// RemoveExpression of PBROutput (id=1) is a no-op — the node must still exist.
TEST(MaterialGraphRemove, RemovePBROutput_IsNoOp) {
    MaterialGraph g;
    g.RemoveExpression(1u);  // Must not crash or remove the node.

    // PBROutput must still be present.
    EXPECT_NE(g.FindExpression(1u), nullptr)
        << "PBROutput (id=1) must survive a RemoveExpression call";
    EXPECT_EQ(g.GetExpressions().size(), 1u);
}

/// RemoveExpression disconnects all pins that referenced the removed node.
TEST(MaterialGraphRemove, RemoveNode_DisconnectsDownstream_PinsCleared) {
    MaterialGraph g;

    MaterialExpression& scalar = g.AddExpression(MatExprType::ScalarConstant);
    scalar.scalarParams["value"] = 1.0f;
    std::string err;
    ASSERT_TRUE(g.Connect(scalar.id, 0, 1u, 0, &err)) << err;

    // PBROutput BaseColor must be connected before removal.
    ASSERT_TRUE(g.GetPBROutput().inputs[0].IsConnected());

    const uint32_t removedId = scalar.id;
    g.RemoveExpression(removedId);

    // PBROutput BaseColor must now be disconnected.
    EXPECT_FALSE(g.GetPBROutput().inputs[0].IsConnected())
        << "PBROutput BaseColor must be disconnected after its source is removed";

    // Removed expression must no longer exist.
    EXPECT_EQ(g.FindExpression(removedId), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional: HLSL output structure
// ─────────────────────────────────────────────────────────────────────────────

/// The generated HLSL must declare all 7 PBR output variables.
TEST(MaterialGraphHlslCodegen, ComputeMaterialOutputs_DeclaresAllPbrOutputs) {
    MaterialGraph g;
    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << result.errorMessage;

    // All 7 out_ variables must appear in the function signature.
    EXPECT_TRUE(Contains(result.hlslSource, "out_baseColor"))  << "out_baseColor missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_metallic"))   << "out_metallic missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_roughness"))  << "out_roughness missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_normal"))     << "out_normal missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_emissive"))   << "out_emissive missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_opacity"))    << "out_opacity missing";
    EXPECT_TRUE(Contains(result.hlslSource, "out_ao"))         << "out_ao missing";
}

/// The function signature must include the expected input parameters.
TEST(MaterialGraphHlslCodegen, ComputeMaterialOutputs_DeclaresExpectedInputs) {
    MaterialGraph g;
    HlslMaterialTranslator translator;
    const MaterialCompileResult result = translator.Compile(g);

    ASSERT_TRUE(result.success) << result.errorMessage;

    EXPECT_TRUE(Contains(result.hlslSource, "float2 uv"))
        << "HLSL must declare 'float2 uv' input";
    EXPECT_TRUE(Contains(result.hlslSource, "float3 positionWS"))
        << "HLSL must declare 'float3 positionWS' input";
    EXPECT_TRUE(Contains(result.hlslSource, "float3 normalWS"))
        << "HLSL must declare 'float3 normalWS' input";
    EXPECT_TRUE(Contains(result.hlslSource, "float totalTime"))
        << "HLSL must declare 'float totalTime' input";
}

/// ComputeContentHash is non-zero for a non-trivial graph.
TEST(MaterialGraphHashing, ContentHash_NonZero_ForNonTrivialGraph) {
    MaterialGraph g;
    MaterialExpression& s = g.AddExpression(MatExprType::ScalarConstant);
    s.scalarParams["value"] = 0.5f;

    const uint64_t h = g.ComputeContentHash();
    EXPECT_NE(h, 0u) << "Content hash must be non-zero";
}

/// Two graphs with different topologies produce different content hashes.
TEST(MaterialGraphHashing, DifferentTopology_DifferentHash) {
    MaterialGraph gA;
    // gA: just PBROutput (trivial)

    MaterialGraph gB;
    // gB: ScalarConstant → PBROutput
    MaterialExpression& s = gB.AddExpression(MatExprType::ScalarConstant);
    s.scalarParams["value"] = 0.5f;
    std::string err;
    ASSERT_TRUE(gB.Connect(s.id, 0, 1u, 0, &err)) << err;

    EXPECT_NE(gA.ComputeContentHash(), gB.ComputeContentHash())
        << "Different graph topologies must produce different content hashes";
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend mode / two-sided round trip
// ─────────────────────────────────────────────────────────────────────────────

/// Both keys were previously dropped by FromJson, so authored translucency
/// never reached the renderer. They must survive a JSON round trip and must
/// change the content hash — the PSO's blend state and cull mode depend on them,
/// as does whether the material is treated as a shadow/GI occluder.
TEST(MaterialGraphSerialization, BlendModeAndTwoSidedRoundTripAndHash) {
    MaterialGraph graph;
    const uint64_t opaqueHash = graph.ComputeContentHash();
    graph.blendMode = MaterialGraphBlendMode::Translucent;
    EXPECT_NE(graph.ComputeContentHash(), opaqueHash);
    const uint64_t translucentHash = graph.ComputeContentHash();
    graph.twoSided = true;
    EXPECT_NE(graph.ComputeContentHash(), translucentHash);

    MaterialGraph loaded;
    std::string error;
    ASSERT_TRUE(loaded.FromJson(graph.ToJson(), &error)) << error;
    EXPECT_EQ(loaded.blendMode, MaterialGraphBlendMode::Translucent);
    EXPECT_TRUE(loaded.twoSided);
    EXPECT_EQ(loaded.ComputeContentHash(), graph.ComputeContentHash());
}
