/// UzEngine Temporal Upscaler Factory
///
/// Creates the appropriate ITemporalUpscaler implementation based on the
/// requested UpscalerType. FSR2 is integrated via AMD FidelityFX SDK.
/// DLSS and XeSS are not yet integrated and fall back to TSR.
///
/// @ref implementation detail

#include "temporal_upscaler.h"
#include "taa_upscaler.h"
#include "tsr_upscaler.h"
#include "fsr2_upscaler.h"

#include "log.h"

namespace uz {

std::unique_ptr<ITemporalUpscaler> CreateTemporalUpscaler(UpscalerType type) {
    switch (type) {
    case UpscalerType::None:
        return nullptr;

    case UpscalerType::TAA:
        return std::make_unique<TaaUpscaler>();

    case UpscalerType::TSR:
        return std::make_unique<TsrUpscaler>();

    case UpscalerType::FSR2:
        return std::make_unique<FSR2Upscaler>();

    case UpscalerType::DLSS:
    case UpscalerType::XeSS:
        UZ_LOG(Renderer, Warning,
               "Upscaler type {} not yet implemented, falling back to TSR",
               static_cast<uint32_t>(type));
        return std::make_unique<TsrUpscaler>();
    }

    // Unreachable for a well-formed UpscalerType, but handles corrupt values.
    UZ_LOG(Renderer, Error,
           "Unknown UpscalerType {}, returning null upscaler",
           static_cast<uint32_t>(type));
    return nullptr;
}

} // namespace uz
