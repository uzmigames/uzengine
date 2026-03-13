/// UzEngine .uzshader Binary Format — Serialization and Parsing
///
/// Writes and reads the .uzshader binary format defined in uzshader_format.h.
/// All operations are little-endian and platform-independent.

#include "uzshader_format.h"
#include "profiler.h"

#include <cstring>

namespace uz {

std::vector<uint8_t> WriteUzShader(
    UzShaderStage stage,
    uint32_t packedModel,
    UzShaderFormat format,
    const std::vector<uint8_t>& bytecode,
    const std::vector<uint8_t>& reflection) {
    UZ_PROFILE_FUNCTION();

    UzShaderHeader header;
    header.stage = static_cast<uint32_t>(stage);
    header.model = packedModel;
    header.format = static_cast<uint32_t>(format);
    header.bytecodeSize = static_cast<uint32_t>(bytecode.size());
    header.reflectSize = static_cast<uint32_t>(reflection.size());

    std::vector<uint8_t> out;
    out.resize(sizeof(UzShaderHeader) + bytecode.size() + reflection.size());

    std::memcpy(out.data(), &header, sizeof(header));
    if (!bytecode.empty()) {
        std::memcpy(out.data() + sizeof(header), bytecode.data(), bytecode.size());
    }
    if (!reflection.empty()) {
        std::memcpy(out.data() + sizeof(header) + bytecode.size(),
                    reflection.data(), reflection.size());
    }

    return out;
}

UzShaderParseResult ReadUzShader(const std::vector<uint8_t>& data) {
    UZ_PROFILE_FUNCTION();
    UzShaderParseResult result;

    if (data.size() < sizeof(UzShaderHeader)) {
        result.error = "Data too small for .uzshader header";
        return result;
    }

    std::memcpy(&result.file.header, data.data(), sizeof(UzShaderHeader));

    if (result.file.header.magic != kUzShaderMagic) {
        result.error = "Invalid magic number — expected 'UZSH'";
        return result;
    }

    if (result.file.header.version != kUzShaderVersion) {
        result.error = "Unsupported .uzshader version: " +
                       std::to_string(result.file.header.version);
        return result;
    }

    const size_t expectedSize = sizeof(UzShaderHeader) +
                                result.file.header.bytecodeSize +
                                result.file.header.reflectSize;

    if (data.size() < expectedSize) {
        result.error = "Data truncated — expected " + std::to_string(expectedSize) +
                       " bytes, got " + std::to_string(data.size());
        return result;
    }

    const uint8_t* bytecodeStart = data.data() + sizeof(UzShaderHeader);
    result.file.bytecode.assign(bytecodeStart,
                                bytecodeStart + result.file.header.bytecodeSize);

    if (result.file.header.reflectSize > 0) {
        const uint8_t* reflectStart = bytecodeStart + result.file.header.bytecodeSize;
        result.file.reflection.assign(reflectStart,
                                      reflectStart + result.file.header.reflectSize);
    }

    result.success = true;
    return result;
}

} // namespace uz
