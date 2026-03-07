#pragma once

#include "engine/renderer/rhi/rhi.h"

namespace uz {

// Generate a 256x256 RG16F BRDF integration LUT on the CPU.
// X axis = NdotV [0,1], Y axis = roughness [0,1].
// R channel = scale (F0-independent), G channel = bias.
// Used by split-sum IBL approximation (Karis 2013).
// Call once at renderer init — the result is environment-independent.
RhiTexture generate_brdf_lut();

} // namespace uz
