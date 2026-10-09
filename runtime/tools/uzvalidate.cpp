/// uzvalidate — CLI for VFX asset validation against the 8.9.0 baseline rules.
///
/// Scans a content directory for `.uzvfx` and `.uzvfxsys` files, runs the
/// 8 baseline validation rules (`VfxValidateAsset`) against each, and emits
/// JUnit XML for CI consumption or a human-readable summary for local runs.
///
/// Usage:
///   uzvalidate <content-dir>              # human summary, exit 1 on error
///   uzvalidate <content-dir> --junit <out>  # JUnit XML to file
///   uzvalidate <content-dir> --junit -    # JUnit XML to stdout
///   uzvalidate --help
///
/// Exit codes:
///   0 — every asset passed
///   1 — one or more assets emitted Error-severity entries
///   2 — bad arguments / IO failure
///
/// @ref engine/vfx/asset/vfx_validation_rules.h
/// @ref docs/engine/25-vfx/26-validator-ci-rules.md

#include "engine/vfx/asset/vfx_validation_rules.h"
#include "engine/vfx/asset/vfx_graph.h"
#include "engine/vfx/system/vfx_system.h"
#include "engine/vfx/modules/module_registry.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace uz;

namespace {

struct CliArgs {
    std::string contentDir;
    std::string junitPath;   // empty = no JUnit; "-" = stdout
    bool        help = false;
    bool        error = false;
    std::string errorMessage;
};

void PrintHelp() {
    std::printf(
        "uzvalidate — VFX asset CI validator\n"
        "\n"
        "Usage:\n"
        "  uzvalidate <content-dir>             human summary\n"
        "  uzvalidate <content-dir> --junit <out>  JUnit XML to file\n"
        "  uzvalidate <content-dir> --junit -   JUnit XML to stdout\n"
        "  uzvalidate --help\n"
        "\n"
        "Scans <content-dir> recursively for .uzvfx / .uzvfxsys assets,\n"
        "runs the phase 8.9.0 validation rules, and reports any error\n"
        "or warning entries. Exit code 1 when any Error-severity entry\n"
        "fires — suitable as a CI gate.\n");
}

CliArgs ParseArgs(int argc, char** argv) {
    CliArgs args;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            args.help = true;
        } else if (std::strcmp(a, "--junit") == 0) {
            if (i + 1 >= argc) {
                args.error = true;
                args.errorMessage = "--junit requires an output path";
                return args;
            }
            args.junitPath = argv[++i];
        } else if (a[0] == '-') {
            args.error = true;
            args.errorMessage = std::string("unknown flag: ") + a;
            return args;
        } else if (args.contentDir.empty()) {
            args.contentDir = a;
        } else {
            args.error = true;
            args.errorMessage = std::string("unexpected positional: ") + a;
            return args;
        }
    }
    if (!args.help && args.contentDir.empty() && !args.error) {
        args.error = true;
        args.errorMessage = "missing <content-dir> argument";
    }
    return args;
}

struct FileReport {
    std::string            path;
    VfxValidationReport    report;
    bool                   loadFailed = false;
    std::string            loadError;
};

std::string ReadFile(const std::filesystem::path& p) {
    std::ifstream ifs(p);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

void ValidateGraph(const std::filesystem::path& path,
                    const std::string& rel,
                    std::vector<FileReport>& out) {
    FileReport fr;
    fr.path = rel;

    ModuleRegistry registry; // empty — rules don't need modules; they only look at metadata
    const std::string content = ReadFile(path);
    if (content.empty()) {
        fr.loadFailed = true;
        fr.loadError = "empty or unreadable file";
        out.push_back(std::move(fr));
        return;
    }
    auto graph = VFXGraph::LoadFromString(content, registry);
    if (!graph) {
        fr.loadFailed = true;
        fr.loadError = "VFXGraph parse failure";
        out.push_back(std::move(fr));
        return;
    }

    VfxValidationContext ctx;
    ctx.graph = graph.get();
    ctx.assetPath = rel;
    fr.report = VfxValidateAsset(ctx);
    out.push_back(std::move(fr));
}

void ValidateSystem(const std::filesystem::path& path,
                     const std::string& rel,
                     std::vector<FileReport>& out) {
    FileReport fr;
    fr.path = rel;

    const std::string content = ReadFile(path);
    if (content.empty()) {
        fr.loadFailed = true;
        fr.loadError = "empty or unreadable file";
        out.push_back(std::move(fr));
        return;
    }
    auto sys = VfxSystem::LoadFromString(content);
    if (!sys) {
        fr.loadFailed = true;
        fr.loadError = "VfxSystem parse failure";
        out.push_back(std::move(fr));
        return;
    }

    VfxValidationContext ctx;
    ctx.system = sys.get();
    ctx.assetPath = rel;
    fr.report = VfxValidateAsset(ctx);
    out.push_back(std::move(fr));
}

[[nodiscard]] std::vector<FileReport> ScanAndValidate(const std::string& dir) {
    std::vector<FileReport> all;
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) {
        std::fprintf(stderr,
                      "uzvalidate: content directory does not exist: '%s'\n",
                      dir.c_str());
        return all;
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto ext = entry.path().extension().string();
        const std::string rel = std::filesystem::relative(entry.path(), dir, ec).generic_string();
        if (ext == ".uzvfx") {
            ValidateGraph(entry.path(), rel, all);
        } else if (ext == ".uzvfxsys") {
            ValidateSystem(entry.path(), rel, all);
        }
    }
    return all;
}

void PrintHumanSummary(const std::vector<FileReport>& reports) {
    uint32_t fileCount = 0;
    uint32_t errorFiles = 0;
    uint32_t warningFiles = 0;
    uint32_t loadFailures = 0;

    for (const auto& fr : reports) {
        ++fileCount;
        if (fr.loadFailed) { ++loadFailures; ++errorFiles; continue; }
        if (fr.report.ErrorCount() > 0u)   ++errorFiles;
        if (fr.report.WarningCount() > 0u) ++warningFiles;
    }

    std::printf("uzvalidate: scanned %u files (errors: %u, warnings: %u, load fails: %u)\n",
                fileCount, errorFiles, warningFiles, loadFailures);

    for (const auto& fr : reports) {
        if (fr.loadFailed) {
            std::printf("  [LOAD FAIL] %s: %s\n", fr.path.c_str(), fr.loadError.c_str());
            continue;
        }
        if (fr.report.TotalCount() == 0u) continue;

        std::printf("  %s:\n", fr.path.c_str());
        for (const auto& e : fr.report.Entries()) {
            std::printf("    [%s] %s: %s\n",
                         VfxValidationSeverityName(e.severity),
                         e.ruleName.c_str(), e.message.c_str());
        }
    }
}

void PrintJUnitAggregated(std::ostream& os,
                           const std::vector<FileReport>& reports) {
    uint32_t totalTests = 0, totalFailures = 0, totalWarnings = 0;
    for (const auto& fr : reports) {
        if (fr.loadFailed) { ++totalTests; ++totalFailures; continue; }
        totalTests    += fr.report.TotalCount();
        totalFailures += fr.report.ErrorCount();
        totalWarnings += fr.report.WarningCount();
    }

    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    os << "<testsuites tests=\"" << totalTests
       << "\" failures=\"" << totalFailures
       << "\" warnings=\"" << totalWarnings << "\">\n";

    for (const auto& fr : reports) {
        if (fr.loadFailed) {
            os << "  <testsuite name=\"" << fr.path << "\" tests=\"1\" failures=\"1\">\n";
            os << "    <testcase name=\"LoadFailure\" classname=\"" << fr.path << "\">\n";
            os << "      <failure message=\"" << fr.loadError << "\"/>\n";
            os << "    </testcase>\n";
            os << "  </testsuite>\n";
            continue;
        }
        os << "  <testsuite name=\"" << fr.path
           << "\" tests=\"" << fr.report.TotalCount()
           << "\" failures=\"" << fr.report.ErrorCount()
           << "\" warnings=\"" << fr.report.WarningCount() << "\">\n";
        for (const auto& e : fr.report.Entries()) {
            os << "    <testcase name=\"" << e.ruleName
               << "\" classname=\"" << fr.path << "\"";
            if (e.severity == VfxValidationSeverity::Error) {
                os << ">\n      <failure message=\"" << e.message << "\"/>\n";
                os << "    </testcase>\n";
            } else if (e.severity == VfxValidationSeverity::Warning) {
                os << ">\n      <system-out>" << e.message << "</system-out>\n";
                os << "    </testcase>\n";
            } else {
                os << "/>\n";
            }
        }
        os << "  </testsuite>\n";
    }
    os << "</testsuites>\n";
}

} // namespace

int main(int argc, char** argv) {
    const auto args = ParseArgs(argc, argv);
    if (args.help) { PrintHelp(); return 0; }
    if (args.error) {
        std::fprintf(stderr, "uzvalidate: %s\n\n", args.errorMessage.c_str());
        PrintHelp();
        return 2;
    }

    auto reports = ScanAndValidate(args.contentDir);

    if (args.junitPath.empty()) {
        PrintHumanSummary(reports);
    } else if (args.junitPath == "-") {
        PrintJUnitAggregated(std::cout, reports);
    } else {
        std::ofstream ofs(args.junitPath);
        if (!ofs) {
            std::fprintf(stderr, "uzvalidate: cannot open '%s' for writing\n",
                         args.junitPath.c_str());
            return 2;
        }
        PrintJUnitAggregated(ofs, reports);
    }

    // Exit code: 1 when any Error entry or load failure occurred.
    for (const auto& fr : reports) {
        if (fr.loadFailed) return 1;
        if (fr.report.ErrorCount() > 0u) return 1;
    }
    return 0;
}
