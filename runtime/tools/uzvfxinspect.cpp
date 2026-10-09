/// uzvfxinspect — CLI for offline inspection of `.uzvfxsnap` emitter snapshots.
///
/// Prints the metadata and per-channel particle values captured by the
/// `VfxSnapshotCapture` flow. Has no GPU / renderer dependencies — reads a
/// `.uzvfxsnap` file, parses with the phase 8.19.0 loader, and emits either
/// a human-readable dump or a JSON document for downstream tooling.
///
/// Usage:
///   uzvfxinspect <path>                 # default dump (VfxSnapshotDump)
///   uzvfxinspect <path> --json          # one big JSON object
///   uzvfxinspect <path> --channel <id>  # focus on a single channel
///   uzvfxinspect <path> --summary       # header + channel counts only
///   uzvfxinspect --help
///
/// Exit codes:
///   0 — success
///   1 — parse / load failure
///   2 — bad arguments
///
/// @ref engine/vfx/baker/vfx_snapshot.h
/// @ref docs/engine/25-vfx/42-uzvfxinspect-cli.md

#include "engine/vfx/baker/vfx_snapshot.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace uz;

namespace {

struct CliArgs {
    std::string path;
    std::string channelFilter;
    bool        jsonOutput = false;
    bool        summaryOnly = false;
    bool        help = false;
    bool        error = false;
    std::string errorMessage;
};

void PrintHelp() {
    std::printf(
        "uzvfxinspect — VFX emitter snapshot inspector\n"
        "\n"
        "Usage:\n"
        "  uzvfxinspect <path>                   human-readable dump\n"
        "  uzvfxinspect <path> --json            JSON output\n"
        "  uzvfxinspect <path> --channel <name>  focus on one channel\n"
        "  uzvfxinspect <path> --summary         header + channel counts\n"
        "  uzvfxinspect --help                   print this message\n"
        "\n"
        "Reads a .uzvfxsnap file produced by VfxSnapshotCapture and\n"
        "prints its metadata / channel data. Input path can be relative.\n");
}

CliArgs ParseArgs(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            args.help = true;
        } else if (std::strcmp(a, "--json") == 0) {
            args.jsonOutput = true;
        } else if (std::strcmp(a, "--summary") == 0) {
            args.summaryOnly = true;
        } else if (std::strcmp(a, "--channel") == 0) {
            if (i + 1 >= argc) {
                args.error = true;
                args.errorMessage = "--channel requires a name argument";
                return args;
            }
            args.channelFilter = argv[++i];
        } else if (a[0] == '-') {
            args.error = true;
            args.errorMessage = std::string("unknown flag: ") + a;
            return args;
        } else if (args.path.empty()) {
            args.path = a;
        } else {
            args.error = true;
            args.errorMessage = std::string("unexpected positional: ") + a;
            return args;
        }
    }
    if (!args.help && args.path.empty() && !args.error) {
        args.error = true;
        args.errorMessage = "missing <path> argument";
    }
    return args;
}

// ─────────────────────────────────────────────────────────────────────────────
// JSON formatting — simple enough to keep inline (no nlohmann dep needed
// here since we're a single self-contained tool).
// ─────────────────────────────────────────────────────────────────────────────

void PrintJsonEscaped(const std::string& s) {
    std::putchar('"');
    for (char c : s) {
        switch (c) {
            case '"':  std::printf("\\\""); break;
            case '\\': std::printf("\\\\"); break;
            case '\n': std::printf("\\n");  break;
            case '\r': std::printf("\\r");  break;
            case '\t': std::printf("\\t");  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::printf("\\u%04x", static_cast<unsigned char>(c));
                } else {
                    std::putchar(c);
                }
        }
    }
    std::putchar('"');
}

/// Emits the numeric values of a channel as a JSON array. Bool channels
/// become a JSON array of `true`/`false`; ints become integer literals;
/// floats use `%g` to avoid spurious trailing zeros.
void PrintChannelData(const VfxSnapshotChannel& c) {
    const size_t elemSize = VfxSnapshotElementSize(c.elementType);
    if (elemSize == 0u || c.data.empty()) { std::printf("[]"); return; }
    std::printf("[");
    switch (c.elementType) {
        case VfxSnapshotElementType::Float: {
            const float* p = reinterpret_cast<const float*>(c.data.data());
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf("%g", p[i]);
            }
            break;
        }
        case VfxSnapshotElementType::Float2:
        case VfxSnapshotElementType::Float3:
        case VfxSnapshotElementType::Float4: {
            const uint32_t lane = static_cast<uint32_t>(elemSize / 4u);
            const float* p = reinterpret_cast<const float*>(c.data.data());
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf("[");
                for (uint32_t j = 0; j < lane; ++j) {
                    if (j > 0) std::printf(",");
                    std::printf("%g", p[i * lane + j]);
                }
                std::printf("]");
            }
            break;
        }
        case VfxSnapshotElementType::Uint32: {
            const uint32_t* p = reinterpret_cast<const uint32_t*>(c.data.data());
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf("%u", p[i]);
            }
            break;
        }
        case VfxSnapshotElementType::Int32: {
            const int32_t* p = reinterpret_cast<const int32_t*>(c.data.data());
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf("%d", p[i]);
            }
            break;
        }
        case VfxSnapshotElementType::Half: {
            // Print raw 16-bit values as uint; full half→float conversion
            // is outside this tool's scope.
            const uint16_t* p = reinterpret_cast<const uint16_t*>(c.data.data());
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf("%u", p[i]);
            }
            break;
        }
        case VfxSnapshotElementType::Bool: {
            const uint8_t* p = c.data.data();
            for (uint32_t i = 0; i < c.elementCount; ++i) {
                if (i > 0) std::printf(",");
                std::printf(p[i] != 0 ? "true" : "false");
            }
            break;
        }
    }
    std::printf("]");
}

void PrintJsonChannel(const VfxSnapshotChannel& c, bool withData) {
    std::printf("{\"name\":");
    PrintJsonEscaped(c.name);
    std::printf(",\"elementType\":\"%s\",\"elementCount\":%u",
                VfxSnapshotElementTypeName(c.elementType), c.elementCount);
    if (withData) {
        std::printf(",\"data\":");
        PrintChannelData(c);
    }
    std::printf("}");
}

void PrintJson(const VfxSnapshot& s, const std::string& channelFilter,
               bool summaryOnly) {
    std::printf("{\"emitterName\":");
    PrintJsonEscaped(s.header.emitterName);
    std::printf(",\"frameIndex\":%llu,\"wallClockNs\":%llu,\"rngSeed\":%llu,"
                "\"capacity\":%u,\"aliveCount\":%u,\"channels\":[",
                static_cast<unsigned long long>(s.header.frameIndex),
                static_cast<unsigned long long>(s.header.wallClockNs),
                static_cast<unsigned long long>(s.header.rngSeed),
                s.header.capacity, s.header.aliveCount);
    bool firstChannel = true;
    for (const auto& c : s.channels) {
        if (!channelFilter.empty() && c.name != channelFilter) continue;
        if (!firstChannel) std::printf(",");
        PrintJsonChannel(c, /*withData=*/ !summaryOnly);
        firstChannel = false;
    }
    std::printf("]}\n");
}

} // namespace

int main(int argc, char** argv) {
    const auto args = ParseArgs(argc, argv);
    if (args.help) {
        PrintHelp();
        return 0;
    }
    if (args.error) {
        std::fprintf(stderr, "uzvfxinspect: %s\n\n", args.errorMessage.c_str());
        PrintHelp();
        return 2;
    }

    VfxSnapshot snap;
    std::string loadError;
    if (!VfxSnapshotLoadFromFile(args.path, snap, loadError)) {
        std::fprintf(stderr, "uzvfxinspect: failed to load '%s': %s\n",
                     args.path.c_str(), loadError.c_str());
        return 1;
    }

    // Validate that --channel name exists (when provided).
    if (!args.channelFilter.empty()) {
        bool found = false;
        for (const auto& c : snap.channels) {
            if (c.name == args.channelFilter) { found = true; break; }
        }
        if (!found) {
            std::fprintf(stderr,
                         "uzvfxinspect: channel '%s' not found in snapshot\n",
                         args.channelFilter.c_str());
            return 1;
        }
    }

    if (args.jsonOutput) {
        PrintJson(snap, args.channelFilter, args.summaryOnly);
        return 0;
    }

    // Default human-readable: reuse the 8.19.0 dump helper for the header
    // and channel-summary lines, then print per-channel data when not in
    // summary mode.
    std::printf("%s", VfxSnapshotDump(snap).c_str());

    if (args.summaryOnly) return 0;

    for (const auto& c : snap.channels) {
        if (!args.channelFilter.empty() && c.name != args.channelFilter) continue;
        std::printf("\n  [channel] %s (%s, %u elems)\n", c.name.c_str(),
                    VfxSnapshotElementTypeName(c.elementType), c.elementCount);
        if (c.elementCount == 0u) continue;
        std::printf("    data = ");
        PrintChannelData(c);
        std::printf("\n");
    }
    return 0;
}
