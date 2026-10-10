#include "wpo_material_extension.h"

namespace uz {

bool WPOMaterialExtension::EmitHLSL(MaterialInjectionPoint point,
                                      std::string& out) const {
    // When WPO is disabled, emit nothing at every point — the compiler
    // sees no permutation tag and no spliced HLSL, producing identical
    // shader bytes to a WPO-less build.
    if (!attrs_.IsEnabled()) return false;

    switch (point) {
        case MaterialInjectionPoint::FunctionScope:
            // Emit the EvaluateWPO function body wrapped in the proper
            // function signature expected by the VS splice calls.
            out =
                "float3 EvaluateWPO(float3 objectPos, float3 worldPos, "
                "float time, float normalizedAge) {\n"
                "    " + VfxWPOEmitEvaluateBody(attrs_) + "\n"
                "}\n";
            return true;

        case MaterialInjectionPoint::VS_PreViewProj:
            out = VfxWPOSpliceVS(attrs_);
            return true;

        case MaterialInjectionPoint::VS_PrevFrame:
            out = VfxWPOSplicePrevVS(attrs_);
            return true;

        case MaterialInjectionPoint::PS_PostLighting:
            return false; // WPO is VS-only

        default:
            return false;
    }
}

} // namespace uz
