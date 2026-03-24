/// UzEngine Pass Inspector — Dump implementation.
///
/// ExecuteDump() reads back all registered targets to CPU memory, converts
/// to RGBA8, writes each as a PNG file, and generates an INDEX.html for
/// browser-based browsing. Uses a shared readback buffer to avoid VRAM
/// exhaustion from rapid sequential allocations.

#include "inspector_pass.h"
#include "inspector_pass_utils.h"

#include "rhi.h"
#include "log.h"

// stb_image_write — implementation lives in screenshot_capture.cpp.
// We only need the function declarations here for stbi_write_png.
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(PassInspector);

namespace uz {

namespace {

/// Reads back a full texture to CPU memory.
/// Returns an RGBA8 pixel buffer (width * height * 4 bytes) suitable for stbi_write_png.
/// Returns empty vector on failure.
[[nodiscard]] std::vector<uint8_t> ReadbackTextureToRGBA8(
    IRHIDevice* device, IRHICommandList* cmdList,
    const RegisteredTarget& target,
    RHIBufferHandle sharedReadbackBuffer, uint64_t sharedBufferSize)
{
    std::vector<uint8_t> pixels;

    const RHITextureDesc texDesc = device->GetTextureDesc(target.texture);
    const RHIFormat format       = texDesc.format;
    const uint32_t bytesPerTexel = BytesPerTexel(format);
    const uint32_t channels      = detail::ChannelCount(format);

    if (bytesPerTexel == 0 || channels == 0 || detail::IsDepthFormat(format)) {
        return pixels;
    }

    const uint32_t rowPitch = ((target.width * bytesPerTexel + 255) & ~255u);
    const uint64_t totalBytes = static_cast<uint64_t>(rowPitch) * target.height;

    // Skip if target exceeds the shared readback buffer size.
    if (totalBytes > sharedBufferSize) {
        return pixels;
    }

    RHIBufferHandle readbackBuffer = sharedReadbackBuffer;

    // Record barrier + copy + restore barrier.
    cmdList->Begin();
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = target.texture;
        barrier.stateBefore = target.lastState;
        barrier.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);

        cmdList->CopyTextureToBuffer(target.texture, 0,
                                      readbackBuffer, 0, rowPitch, 0);

        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter  = target.lastState;
        cmdList->ResourceBarrier(&barrier, 1);
    }
    cmdList->End();
    cmdList->Execute();
    device->WaitIdle();

    void* mapped = device->MapBuffer(readbackBuffer);
    if (!mapped) {
        return pixels;
    }

    // Convert to RGBA8 for PNG output.
    pixels.resize(static_cast<size_t>(target.width) * target.height * 4u);

    for (uint32_t y = 0; y < target.height; ++y) {
        const auto* rowBase = static_cast<const uint8_t*>(mapped)
                            + static_cast<uint64_t>(y) * rowPitch;

        for (uint32_t x = 0; x < target.width; ++x) {
            const auto* texelBase = rowBase + x * bytesPerTexel;
            const size_t pixelOffset = (static_cast<size_t>(y) * target.width + x) * 4u;

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;

            if (channels >= 1) r = detail::ReadChannel(texelBase, 0, format);
            if (channels >= 2) g = detail::ReadChannel(texelBase, 1, format);
            if (channels >= 3) b = detail::ReadChannel(texelBase, 2, format);
            if (channels >= 4) a = detail::ReadChannel(texelBase, 3, format);

            // Handle NaN/Inf by replacing with magenta (diagnostic color).
            if (std::isnan(r) || std::isinf(r)) r = 1.0f;
            if (std::isnan(g) || std::isinf(g)) g = 0.0f;
            if (std::isnan(b) || std::isinf(b)) b = 1.0f;
            if (std::isnan(a) || std::isinf(a)) a = 1.0f;

            // For grayscale formats, replicate R across RGB.
            if (channels == 1) { g = r; b = r; }
            if (channels == 2) { b = 0.0f; }

            // HDR tonemap for display: simple Reinhard per channel.
            r = r / (1.0f + r);
            g = g / (1.0f + g);
            b = b / (1.0f + b);

            // Clamp and convert to 8-bit.
            auto clamp8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::min(std::max(v, 0.0f), 1.0f) * 255.0f + 0.5f);
            };

            pixels[pixelOffset + 0] = clamp8(r);
            pixels[pixelOffset + 1] = clamp8(g);
            pixels[pixelOffset + 2] = clamp8(b);
            pixels[pixelOffset + 3] = clamp8(a);
        }
    }

    device->UnmapBuffer(readbackBuffer);
    // Buffer is shared — caller owns lifetime.

    return pixels;
}

/// Generates an INDEX.html file for browsing dump PNGs.
void GenerateIndexHtml(const std::string& dir, const std::vector<std::string>& filenames,
                       uint32_t frameNumber)
{
    const std::string htmlPath = dir + "/INDEX.html";
    std::ofstream ofs(htmlPath);
    if (!ofs.is_open()) {
        UZ_LOG(PassInspector, Warning, "PassInspector: failed to create {}", htmlPath);
        return;
    }

    ofs << "<!DOCTYPE html>\n<html>\n<head>\n<title>Pass Dump — Frame " << frameNumber << "</title>\n";
    ofs << "<style>\n";
    ofs << "body { background: #1a1a1a; color: #e0e0e0; font-family: monospace; margin: 20px; }\n";
    ofs << "h1 { color: #80ff80; }\n";
    ofs << ".grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(300px, 1fr)); gap: 10px; }\n";
    ofs << ".cell { background: #2a2a2a; padding: 10px; border-radius: 6px; }\n";
    ofs << ".cell img { width: 100%; cursor: pointer; }\n";
    ofs << ".cell .label { font-size: 13px; margin-bottom: 5px; color: #80ff80; }\n";
    ofs << ".fullscreen { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; ";
    ofs << "background: rgba(0,0,0,0.9); z-index: 100; cursor: pointer; }\n";
    ofs << ".fullscreen img { max-width: 95%; max-height: 95%; margin: auto; display: block; ";
    ofs << "position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); }\n";
    ofs << "</style>\n</head>\n<body>\n";
    ofs << "<h1>Pass Dump — Frame " << frameNumber << " (" << filenames.size() << " targets)</h1>\n";
    ofs << "<div class=\"grid\">\n";

    for (const auto& fn : filenames) {
        ofs << "  <div class=\"cell\">\n";
        ofs << "    <div class=\"label\">" << fn << "</div>\n";
        ofs << "    <img src=\"" << fn << "\" onclick=\"showFull(this.src)\">\n";
        ofs << "  </div>\n";
    }

    ofs << "</div>\n";
    ofs << "<div class=\"fullscreen\" id=\"fullview\" onclick=\"this.style.display='none'\">\n";
    ofs << "  <img id=\"fullimg\">\n</div>\n";
    ofs << "<script>\nfunction showFull(src) {\n";
    ofs << "  document.getElementById('fullimg').src = src;\n";
    ofs << "  document.getElementById('fullview').style.display = 'block';\n";
    ofs << "}\n</script>\n</body>\n</html>\n";
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Pass Dump (Phase 3)
// ─────────────────────────────────────────────────────────────────────────────

void PassInspector::ExecuteDump(IRHIDevice* device, IRHICommandList* cmdList,
                                 uint32_t frameNumber, const std::string& outputDir) {
    if (!device || !cmdList || m_targets.empty()) {
        UZ_LOG(PassInspector, Warning, "PassInspector: dump failed — no targets registered");
        return;
    }

    // Create the output directory: outputDir/pass-dump-f{N}/
    std::ostringstream dirStream;
    dirStream << outputDir << "/pass-dump-f" << frameNumber;
    const std::string dumpDir = dirStream.str();

    std::error_code ec;
    std::filesystem::create_directories(dumpDir, ec);
    if (ec) {
        UZ_LOG(PassInspector, Error, "PassInspector: failed to create directory '{}': {}",
               dumpDir, ec.message());
        return;
    }

    UZ_LOG(PassInspector, Info, "PassInspector: dumping {} targets to '{}'",
           m_targets.size(), dumpDir);

    // Create a single shared readback buffer large enough for the biggest target.
    // Reusing one buffer avoids VRAM exhaustion from rapid sequential allocations.
    constexpr uint64_t kMaxReadbackSize = 1920u * 1080u * 8u; // RGBA16F @ 1080p = ~16MB
    RHIBufferDesc rbDesc{};
    rbDesc.sizeBytes = kMaxReadbackSize;
    rbDesc.stride    = 1;
    rbDesc.usage     = RHIBufferUsage::CopyDst;
    rbDesc.memory    = RHIMemoryType::Readback;
    rbDesc.debugName = "PassInspector_SharedReadback";
    RHIBufferHandle sharedReadback = device->CreateBuffer(rbDesc);
    if (!sharedReadback.IsValid()) {
        UZ_LOG(PassInspector, Error, "PassInspector: failed to create shared readback buffer");
        return;
    }

    std::vector<std::string> filenames;
    filenames.reserve(m_targets.size());

    for (uint32_t i = 0; i < static_cast<uint32_t>(m_targets.size()); ++i) {
        const auto& target = m_targets[i];

        // Build filename: XX-name.png (dots replaced with dashes)
        std::ostringstream fnStream;
        fnStream << std::setfill('0') << std::setw(2) << i << "-";
        std::string safeName = target.name;
        std::replace(safeName.begin(), safeName.end(), '.', '-');
        fnStream << safeName << ".png";
        const std::string filename = fnStream.str();

        // DEBUG: Log handle info for cloud targets
        if (target.name.find("cloud") != std::string::npos) {
            UZ_LOG(PassInspector, Info,
                   "PassInspector: dumping '{}' — texHandle=(idx={}, gen={}), srvIndex={}, "
                   "size={}x{}, state={}",
                   target.name, target.texture.index, target.texture.generation,
                   target.srvIndex, target.width, target.height,
                   static_cast<int>(target.lastState));
        }

        // Readback the texture to RGBA8 using the shared buffer.
        auto pixels = ReadbackTextureToRGBA8(device, cmdList, target, sharedReadback, kMaxReadbackSize);
        if (pixels.empty()) {
            const RHITextureDesc td = device->GetTextureDesc(target.texture);
            UZ_LOG(PassInspector, Warning,
                   "PassInspector: dump skipped '{}' (readback failed — fmt={}, {}x{}, depth={}, bpp={}, ch={})",
                   target.name, static_cast<int>(td.format), target.width, target.height,
                   detail::IsDepthFormat(td.format), BytesPerTexel(td.format), detail::ChannelCount(td.format));
            continue;
        }

        // Write PNG.
        const std::string fullPath = dumpDir + "/" + filename;
        const int writeResult = stbi_write_png(
            fullPath.c_str(),
            static_cast<int>(target.width),
            static_cast<int>(target.height),
            4,
            pixels.data(),
            static_cast<int>(target.width * 4));

        if (writeResult) {
            filenames.push_back(filename);
            UZ_LOG(PassInspector, Info, "PassInspector: dumped {} ({}x{})",
                   filename, target.width, target.height);
        } else {
            UZ_LOG(PassInspector, Warning, "PassInspector: failed to write '{}'", fullPath);
        }
    }

    // Release the shared readback buffer.
    device->DestroyBuffer(sharedReadback);

    // Generate INDEX.html for browser-based viewing.
    GenerateIndexHtml(dumpDir, filenames, frameNumber);

    UZ_LOG(PassInspector, Info, "PassInspector: dump complete — {} PNGs + INDEX.html in '{}'",
           filenames.size(), dumpDir);
}

} // namespace uz
