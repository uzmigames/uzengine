// CPU contract for the shipped shader: both fallback sky tints carry intensity.
#include "engine_shaders.h"
#include <gtest/gtest.h>
#include <regex>
#include <string>

TEST(SkyLightIntensity, DiffuseAndSpecularFallbackScaleOnce) {
    const std::string source(uz::EngineShaders::GetSource(uz::EngineShader::DeferredLightingPS));
    const std::string code = std::regex_replace(
        source, std::regex(R"(//[^\r\n]*|/\*[\s\S]*?\*/)"), " ");
    // Match the complete machine-consumed expression, not comments or a CPU mirror
    // that could keep passing when the actual shader drops or squares intensity.
    for (const char* tint : {"skyTintDiffuse", "skyTintSpecular"}) {
        const std::regex assignment(std::string(R"(float3\s+)") + tint
            + R"(\s*=\s*skyColor\s*\*\s*skyLightIntensity\s*;)");
        EXPECT_TRUE(std::regex_search(code, assignment)) << tint;
    }
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(if\s*\(\s*irradianceFromGI\s*\)\s*skyTintDiffuse\s*=\s*1\.0\s*;)")));
}
