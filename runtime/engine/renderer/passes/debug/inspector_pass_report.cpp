/// UzEngine Pass Inspector — JSON diagnostic report generation.
///
/// Produces machine-parseable JSON output for LLM agent consumption.
/// The JSON format is designed for automated parsing — no pretty-printing,
/// all values are primitives (no nested objects beyond arrays).
///
/// Agents parse stdout for the `DIAG_JSON:` prefix followed by the JSON string
/// produced by `DiagnosticReport::ToJSON()`.

#include "inspector_pass.h"
#include "log.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>

UZ_DECLARE_LOG_CATEGORY(PassInspector);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// JSON formatting helpers (anonymous namespace — file-local)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Writes a JSON-escaped string (with surrounding quotes) to the stream.
/// Handles the minimal set of control characters that could appear in
/// render target names or pass names.
void JsonEscapeString(std::ostringstream& os, const std::string& s) {
    os << '"';
    for (const char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:   os << c;      break;
        }
    }
    os << '"';
}

/// Writes a float value to the stream. NaN and Inf are written as JSON strings
/// since JSON (RFC 8259) does not support non-finite numeric literals.
void JsonFloat(std::ostringstream& os, float val) {
    if (std::isnan(val)) {
        os << "\"NaN\"";
    } else if (std::isinf(val)) {
        os << "\"Inf\"";
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6f", val);
        os << buf;
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// DiagnosticReport::ToJSON
// ─────────────────────────────────────────────────────────────────────────────

std::string DiagnosticReport::ToJSON() const {
    std::ostringstream os;
    os << "{";

    // Header fields
    os << "\"frame\":" << frameNumber << ",";
    os << "\"targetCount\":" << targetCount << ",";
    os << "\"hasFailure\":" << (hasAnyFailure ? "true" : "false") << ",";
    os << "\"hasWarning\":" << (hasAnyWarning ? "true" : "false") << ",";

    // Targets array — one entry per registered render target
    os << "\"targets\":[";
    for (size_t i = 0; i < targets.size(); ++i) {
        const auto& t = targets[i];
        if (i > 0) os << ",";
        os << "{";
        os << "\"name\":"; JsonEscapeString(os, t.name); os << ",";

        // Status string: deterministic priority order —
        // skipped > NaN > Inf > zero > OK. SKIPPED covers both disabled-pass
        // placeholders and targets whose readback is impossible (depth
        // format, 3D volume, oversized) — the log line carries the reason.
        const char* status = "OK";
        if (t.skipped)      status = "SKIPPED";
        else if (t.hasNaN)  status = "FAIL_NAN";
        else if (t.hasInf)  status = "FAIL_INF";
        else if (t.isZero)  status = "WARN_ZERO";
        os << "\"status\":\"" << status << "\",";

        os << "\"min\":"; JsonFloat(os, t.minVal); os << ",";
        os << "\"max\":"; JsonFloat(os, t.maxVal); os << ",";
        os << "\"avg\":"; JsonFloat(os, t.avgVal);
        os << "}";
    }
    os << "],";

    // Timing array — one entry per pass with GPU timing data
    os << "\"timing\":[";
    for (size_t i = 0; i < timing.size(); ++i) {
        const auto& t = timing[i];
        if (i > 0) os << ",";
        os << "{";
        os << "\"name\":"; JsonEscapeString(os, t.name); os << ",";
        os << "\"gpuMs\":"; JsonFloat(os, t.gpuTimeMs);
        os << "}";
    }
    os << "]";

    os << "}";
    return os.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// PassInspector::GenerateReport
// ─────────────────────────────────────────────────────────────────────────────

DiagnosticReport PassInspector::GenerateReport(IRHIDevice* device, IRHICommandList* cmdList,
                                                uint32_t frameNumber) {
    DiagnosticReport report;
    report.frameNumber = frameNumber;
    report.targetCount = static_cast<uint32_t>(m_targets.size() + m_bufferTargets.size());

    // Run validation on all registered targets (synchronous GPU readback)
    report.targets = Validate(device, cmdList);

    // Collect the most recent per-pass timing data
    report.timing = m_timingData;

    // Compute summary flags for quick triage
    for (const auto& t : report.targets) {
        if (t.hasNaN || t.hasInf) report.hasAnyFailure = true;
        if (t.isZero)             report.hasAnyWarning = true;
    }

    UZ_LOG(PassInspector, Info, "Diagnostic report: frame={}, targets={}, failures={}, warnings={}",
           report.frameNumber, report.targetCount, report.hasAnyFailure, report.hasAnyWarning);

    return report;
}

} // namespace uz
