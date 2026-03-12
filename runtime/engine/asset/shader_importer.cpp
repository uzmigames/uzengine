/// UzEngine Shader Importer — Implementation
///
/// Maps string options to ShaderCompiler enums, compiles via DXC,
/// and packages the result into .uzshader binary format.

#ifdef _WIN32

#include "shader_importer.h"
#include "log.h"
#include "profiler.h"
#include "shader_compiler.h"
#include "uzshader_format.h"

#include <algorithm>
#include <unordered_map>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// String-to-enum mappings
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Maps stage name strings to ShaderStageCompile and UzShaderStage.
struct StagePair {
    ShaderStageCompile compile;
    UzShaderStage format;
};

const std::unordered_map<std::string, StagePair>& StageMap() {
    static const std::unordered_map<std::string, StagePair> map = {
        {"vertex",        {ShaderStageCompile::Vertex,        UzShaderStage::Vertex}},
        {"pixel",         {ShaderStageCompile::Pixel,         UzShaderStage::Pixel}},
        {"compute",       {ShaderStageCompile::Compute,       UzShaderStage::Compute}},
        {"geometry",      {ShaderStageCompile::Geometry,       UzShaderStage::Geometry}},
        {"hull",          {ShaderStageCompile::Hull,           UzShaderStage::Hull}},
        {"domain",        {ShaderStageCompile::Domain,         UzShaderStage::Domain}},
        {"amplification", {ShaderStageCompile::Amplification,  UzShaderStage::Amplification}},
        {"mesh",          {ShaderStageCompile::Mesh,           UzShaderStage::Mesh}},
    };
    return map;
}

/// Maps shader model strings to ShaderModelCompile and packed model value.
struct ModelPair {
    ShaderModelCompile compile;
    uint32_t packed;
};

const std::unordered_map<std::string, ModelPair>& ModelMap() {
    static const std::unordered_map<std::string, ModelPair> map = {
        {"sm5_1", {ShaderModelCompile::SM5_1, PackShaderModel(5, 1)}},
        {"sm6_0", {ShaderModelCompile::SM6_0, PackShaderModel(6, 0)}},
        {"sm6_1", {ShaderModelCompile::SM6_1, PackShaderModel(6, 1)}},
        {"sm6_2", {ShaderModelCompile::SM6_2, PackShaderModel(6, 2)}},
        {"sm6_3", {ShaderModelCompile::SM6_3, PackShaderModel(6, 3)}},
        {"sm6_4", {ShaderModelCompile::SM6_4, PackShaderModel(6, 4)}},
        {"sm6_5", {ShaderModelCompile::SM6_5, PackShaderModel(6, 5)}},
        {"sm6_6", {ShaderModelCompile::SM6_6, PackShaderModel(6, 6)}},
        {"sm6_7", {ShaderModelCompile::SM6_7, PackShaderModel(6, 7)}},
    };
    return map;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// ShaderImporter
// ─────────────────────────────────────────────────────────────────────────────

ShaderImporter::ShaderImporter(ShaderCompiler& compiler)
    : m_compiler(compiler)
    , m_extensions({".hlsl"}) {}

const char* ShaderImporter::Name() const {
    return "shader";
}

uint32_t ShaderImporter::Version() const {
    return 1;
}

const std::vector<std::string>& ShaderImporter::Extensions() const {
    return m_extensions;
}

AssetImportResult ShaderImporter::Import(
    const std::vector<uint8_t>& sourceData,
    const std::string& sourceName,
    const AssetImportOptions& options) {
    UZ_PROFILE_FUNCTION();

    AssetImportResult result;

    if (!m_compiler.IsInitialized()) {
        result.error = "ShaderCompiler not initialized — DXC unavailable";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    if (sourceData.empty()) {
        result.error = "Empty source data";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Resolve stage
    std::string stageName = options.stage.empty() ? "vertex" : options.stage;
    auto stageIt = StageMap().find(stageName);
    if (stageIt == StageMap().end()) {
        result.error = "Unknown shader stage: '" + stageName + "'";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Resolve model
    std::string modelName = options.model.empty() ? "sm6_6" : options.model;
    auto modelIt = ModelMap().find(modelName);
    if (modelIt == ModelMap().end()) {
        result.error = "Unknown shader model: '" + modelName + "'";
        UZ_LOG(Asset, Error, "Import failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Resolve output format
    ShaderOutputFormat outputFormat = ShaderOutputFormat::DXIL;
    UzShaderFormat uzFormat = UzShaderFormat::DXIL;
    if (options.format == "spirv") {
        outputFormat = ShaderOutputFormat::SPIRV;
        uzFormat = UzShaderFormat::SPIRV;
    }

    // Build DXC compile descriptor
    ShaderCompileDesc desc;
    desc.source = std::string(sourceData.begin(), sourceData.end());
    desc.entryPoint = options.entryPoint.empty() ? "main" : options.entryPoint;
    desc.sourceName = sourceName;
    desc.stage = stageIt->second.compile;
    desc.model = modelIt->second.compile;
    desc.output = outputFormat;
    desc.defines = options.defines;
    desc.generateDebugInfo = options.debug;

    // Compile via DXC
    ShaderCompileResult compileResult = m_compiler.Compile(desc);
    if (!compileResult.success) {
        result.error = compileResult.errors;
        UZ_LOG(Asset, Error, "DXC compilation failed for '{}': {}", sourceName, result.error);
        return result;
    }

    // Package into .uzshader binary format
    result.data = WriteUzShader(
        stageIt->second.format,
        modelIt->second.packed,
        uzFormat,
        compileResult.bytecode);

    result.success = true;
    UZ_LOG(Asset, Info, "Imported shader '{}': stage={}, model={}, format={}, {} bytes",
           sourceName, stageName, modelName, options.format.empty() ? "dxil" : options.format,
           result.data.size());

    return result;
}

} // namespace uz

#endif // _WIN32
