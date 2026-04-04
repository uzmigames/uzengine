#!/usr/bin/env python3
"""UzEngine Documentation Audit — Automated violation scanner.

Scans runtime C++ headers and source files for documentation violations
against the project's Rust-style /// standard. Produces a structured report
with per-file violation counts and actionable line references.

Usage:
    python scripts/audit_docs.py                    # scan all
    python scripts/audit_docs.py runtime/engine/rhi # scan subdirectory
    python scripts/audit_docs.py --fix              # auto-fix V1/V5/V7 (safe)
    python scripts/audit_docs.py --json             # machine-readable output
    python scripts/audit_docs.py --severity V1,V2   # filter by violation type
"""

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Optional


# ---------------------------------------------------------------------------
# Violation types
# ---------------------------------------------------------------------------

class VType(str, Enum):
    V1 = "V1"   # Missing file header
    V2 = "V2"   # Undocumented public struct/class
    V3 = "V3"   # Undocumented public method
    V4 = "V4"   # Shallow one-liner (missing # sections)
    V5 = "V5"   # Undocumented enum values
    V6 = "V6"   # Undocumented macro
    V7 = "V7"   # Wrong comment style (/* */ or //!)
    V8 = "V8"   # Name-restating doc
    V9 = "V9"   # Undocumented internal function >10 lines (cpp only)
    V10 = "V10" # Magic numbers (cpp only)


@dataclass
class Violation:
    vtype: VType
    file: str
    line: int
    text: str
    context: str = ""  # the offending line content


@dataclass
class FileReport:
    path: str
    violations: list = field(default_factory=list)

    @property
    def status(self) -> str:
        return "HAS_VIOLATIONS" if self.violations else "COMPLIANT"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_RE_CLASS_STRUCT = re.compile(
    r"^\s*(?:template\s*<[^>]*>\s*)?(class|struct)\s+(\w+)"
)
_RE_FORWARD_DECL = re.compile(r"^\s*(class|struct)\s+\w+\s*;")
_RE_ENUM = re.compile(r"^\s*enum\s+(class\s+)?(\w+)")
_RE_ENUM_VALUE = re.compile(r"^\s*(\w+)\s*(?:=\s*[^,]+)?\s*,?\s*$")
_RE_DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)")
_RE_INCLUDE_GUARD = re.compile(r"^\s*#\s*define\s+\w+_H\b")
_RE_SIMPLE_CONST = re.compile(r"^\s*#\s*define\s+\w+\s+\d+\s*$")
# Platform/standard macros that don't need documentation
_KNOWN_MACROS = {
    "WIN32_LEAN_AND_MEAN", "NOMINMAX", "NOGDI", "NOCOMM", "NOKANJI",
    "NOHELP", "NOPROFILER", "VC_EXTRALEAN", "STRICT", "UNICODE",
    "_UNICODE", "_CRT_SECURE_NO_WARNINGS", "_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS",
    "VULKAN_HPP_NO_EXCEPTIONS", "VK_USE_PLATFORM_WIN32_KHR", "VK_NO_PROTOTYPES",
}
_RE_FUNC_DECL = re.compile(
    r"^\s*(?:virtual\s+|static\s+|inline\s+|explicit\s+|constexpr\s+|"
    r"\[\[nodiscard\]\]\s+)*"
    r"(?:[\w:*&<>,\s]+?)\s+(\w+)\s*\("
)
_RE_DESTRUCTOR = re.compile(r"^\s*~?\w+\s*\(")
_RE_DELETED_DEFAULT = re.compile(r"=\s*(delete|default)\s*;")
_RE_OPERATOR = re.compile(r"\boperator\b")
_RE_DOC_COMMENT = re.compile(r"^\s*///")
_RE_BLOCK_COMMENT = re.compile(r"^\s*/\*")
_RE_BANG_COMMENT = re.compile(r"^\s*//!")
_RE_ARGS_SECTION = re.compile(r"///\s*#\s*Arguments")
_RE_RETURNS_SECTION = re.compile(r"///\s*#\s*Returns")
_RE_TRAILING_DOC = re.compile(r"///<")
_RE_MAGIC_NUMBER = re.compile(
    r"(?<!\w)(\d+\.\d+[fF]?|\d{2,}(?:U|UL|ULL|L|LL)?)\b"
)
_RE_STATIC_FUNC = re.compile(
    r"^\s*(?:static\s+)?(?:[\w:*&<>,\s]+?)\s+(\w+)\s*\([^)]*\)\s*\{"
)
_RE_ANON_NS_START = re.compile(r"^\s*namespace\s*\{")
_RE_NS_END = re.compile(r"^\s*\}")

_TRIVIAL_NUMBERS = {"0", "1", "-1", "0.0f", "1.0f", "0.0", "1.0",
                     "0u", "1u", "0ULL", "0xFFFFFFFF", "nullptr",
                     "true", "false", "UINT32_MAX", "UINT64_MAX",
                     "FLT_MAX", "DBL_MAX", "INT_MAX", "INT_MIN",
                     "SIZE_MAX"}


def _prev_content_line(lines: list[str], idx: int) -> tuple[int, str]:
    """Return (line_number, content) of the previous non-blank line."""
    i = idx - 1
    while i >= 0 and lines[i].strip() == "":
        i -= 1
    if i < 0:
        return (-1, "")
    return (i, lines[i])


def _is_doc_line(line: str) -> bool:
    return bool(_RE_DOC_COMMENT.match(line))


def _has_params(line: str) -> bool:
    """Check if a function declaration has parameters (excluding void/empty)."""
    m = re.search(r"\(([^)]*)\)", line)
    if not m:
        return False
    params = m.group(1).strip()
    return params != "" and params != "void"


def _has_non_void_return(line: str) -> bool:
    """Check if a function has a non-void return type."""
    stripped = line.strip()
    # Remove leading keywords
    for kw in ("virtual", "static", "inline", "explicit", "constexpr",
               "[[nodiscard]]", "friend"):
        stripped = stripped.replace(kw, "").strip()
    # The return type is everything before the function name
    m = re.match(r"([\w:*&<>,\s]+?)\s+\w+\s*\(", stripped)
    if not m:
        return False
    ret = m.group(1).strip()
    return ret not in ("void", "")


def _collect_doc_block(lines: list[str], end_idx: int) -> list[str]:
    """Collect consecutive /// lines ending at end_idx (exclusive)."""
    block = []
    i = end_idx - 1
    while i >= 0 and lines[i].strip() == "":
        i -= 1
    while i >= 0 and _is_doc_line(lines[i]):
        block.insert(0, lines[i])
        i -= 1
    return block


def _is_name_restating(doc_block: list[str], identifier: str) -> bool:
    """Check if the doc just restates the identifier name."""
    if not doc_block:
        return False
    # Get the summary line (first /// line)
    summary = doc_block[0]
    summary = re.sub(r"^\s*///\s*", "", summary).strip().rstrip(".")
    if not summary:
        return False

    # Normalize: split camelCase/PascalCase/snake_case into words
    ident_words = set()
    # camelCase split
    parts = re.sub(r"([a-z])([A-Z])", r"\1 \2", identifier)
    parts = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", parts)
    for w in re.split(r"[_\s]+", parts):
        if w:
            ident_words.add(w.lower())

    # Remove articles and common prefixes from summary
    summary_lower = summary.lower()
    for noise in ("the ", "a ", "an ", "get ", "set ", "is ", "has ",
                   "returns ", "return "):
        summary_lower = summary_lower.replace(noise, "")
    summary_words = set(re.split(r"[\s_\-]+", summary_lower))
    summary_words.discard("")

    # If all summary words are just the identifier words, it's restating
    if summary_words and summary_words.issubset(ident_words):
        return True
    # Also check if summary is very short and matches
    if len(summary_words) <= 2 and ident_words.intersection(summary_words):
        overlap = len(ident_words.intersection(summary_words))
        if overlap / max(len(summary_words), 1) >= 0.8:
            return True
    return False


# ---------------------------------------------------------------------------
# Scanners
# ---------------------------------------------------------------------------

def scan_header(filepath: str) -> FileReport:
    """Scan a .h file for violations V1-V8."""
    report = FileReport(path=filepath)
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return report

    if not lines:
        return report

    # --- V1: Missing file header ---
    first_content = -1
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "" or stripped == "#pragma once":
            continue
        first_content = i
        break

    if first_content >= 0 and not _is_doc_line(lines[first_content]):
        # Check if there's a /// before this line (after #pragma once)
        has_header = False
        for j in range(first_content):
            if _is_doc_line(lines[j]):
                has_header = True
                break
        if not has_header:
            report.violations.append(Violation(
                VType.V1, filepath, first_content + 1,
                "Missing file header (first content line is not ///)",
                lines[first_content].rstrip()
            ))

    # Track public/private/protected scope for V3
    in_public = False
    in_enum = False
    enum_depth = 0
    brace_depth = 0

    for i, line in enumerate(lines):
        stripped = line.strip()

        # --- V7: Wrong comment style ---
        if _RE_BLOCK_COMMENT.match(line) or _RE_BANG_COMMENT.match(line):
            # Check if next non-blank line is a declaration
            for j in range(i + 1, min(i + 5, len(lines))):
                nxt = lines[j].strip()
                if nxt == "":
                    continue
                if (_RE_CLASS_STRUCT.match(nxt) or _RE_FUNC_DECL.match(nxt)
                        or _RE_ENUM.match(nxt) or _RE_DEFINE.match(nxt)):
                    report.violations.append(Violation(
                        VType.V7, filepath, i + 1,
                        "Wrong comment style (use /// instead of /* */ or //!)",
                        stripped
                    ))
                break

        # Track brace depth for enum scope
        brace_depth += stripped.count("{") - stripped.count("}")

        # Track access specifiers
        if re.match(r"^\s*public\s*:", stripped):
            in_public = True
        elif re.match(r"^\s*(private|protected)\s*:", stripped):
            in_public = False

        # --- V2: Undocumented public struct/class ---
        m = _RE_CLASS_STRUCT.match(stripped)
        if m and not _RE_FORWARD_DECL.match(stripped):
            # Skip structs inside macro bodies (line ends with \)
            if line.rstrip().endswith("\\"):
                pass
            # Skip local structs inside function bodies (brace_depth > 1)
            elif brace_depth > 1:
                pass
            else:
                prev_idx, prev_line = _prev_content_line(lines, i)
                if prev_idx >= 0 and not _is_doc_line(prev_line):
                    report.violations.append(Violation(
                        VType.V2, filepath, i + 1,
                        f"Undocumented {m.group(1)} '{m.group(2)}'",
                        stripped
                    ))

        # --- V5: Enum tracking ---
        if _RE_ENUM.match(stripped):
            in_enum = True
            enum_depth = brace_depth

        if in_enum and brace_depth < enum_depth:
            in_enum = False

        if in_enum and _RE_ENUM_VALUE.match(stripped):
            if not _RE_TRAILING_DOC.search(line) and not _is_doc_line(line):
                # Skip if previous line is ///
                prev_idx, prev_line = _prev_content_line(lines, i)
                if prev_idx < 0 or not _is_doc_line(prev_line):
                    # Skip the opening brace or enum name line
                    if not stripped.startswith("{") and not _RE_ENUM.match(stripped):
                        report.violations.append(Violation(
                            VType.V5, filepath, i + 1,
                            f"Undocumented enum value '{stripped.split('=')[0].strip().rstrip(',')}'",
                            stripped
                        ))

        # --- V3: Undocumented public method ---
        if in_public and _RE_FUNC_DECL.match(stripped):
            if (_RE_DESTRUCTOR.match(stripped) or _RE_DELETED_DEFAULT.search(stripped)
                    or _RE_OPERATOR.search(stripped)):
                continue
            # Skip member initializer lists (: m_foo(...))
            if re.match(r"^\s*[:,]\s*\w+\s*\(", stripped):
                continue
            # Skip return statements (return Foo(...))
            if re.match(r"^\s*return\s+", stripped):
                continue
            # Skip placement new (new (ptr) T(...))
            if re.match(r"^\s*return\s+new\s*\(", stripped) or "new (" in stripped:
                continue
            # Skip lines ending with \ (macro body continuations)
            if line.rstrip().endswith("\\"):
                continue
            prev_idx, prev_line = _prev_content_line(lines, i)
            # Bridge template<...> declarations — the doc block is above the template line
            if prev_idx >= 0 and re.match(r"^\s*template\s*<", prev_line.strip()):
                prev_idx, prev_line = _prev_content_line(lines, prev_idx)
            if prev_idx >= 0 and not _is_doc_line(prev_line):
                func_m = _RE_FUNC_DECL.match(stripped)
                fname = func_m.group(1) if func_m else "?"
                report.violations.append(Violation(
                    VType.V3, filepath, i + 1,
                    f"Undocumented public method '{fname}'",
                    stripped
                ))
            else:
                # --- V4: Shallow doc check ---
                doc_block = _collect_doc_block(lines, i)
                if doc_block:
                    doc_text = "\n".join(doc_block)
                    has_args = bool(_RE_ARGS_SECTION.search(doc_text))
                    has_returns = bool(_RE_RETURNS_SECTION.search(doc_text))
                    needs_args = _has_params(stripped)
                    needs_returns = _has_non_void_return(stripped)
                    missing = []
                    if needs_args and not has_args:
                        missing.append("# Arguments")
                    if needs_returns and not has_returns:
                        missing.append("# Returns")
                    if missing and len(doc_block) <= 2:
                        func_m = _RE_FUNC_DECL.match(stripped)
                        fname = func_m.group(1) if func_m else "?"
                        report.violations.append(Violation(
                            VType.V4, filepath, i + 1,
                            f"Shallow doc on '{fname}' — missing {', '.join(missing)}",
                            stripped
                        ))

                    # --- V8: Name-restating check ---
                    func_m = _RE_FUNC_DECL.match(stripped)
                    if func_m:
                        fname = func_m.group(1)
                        if _is_name_restating(doc_block, fname):
                            report.violations.append(Violation(
                                VType.V8, filepath, i + 1,
                                f"Name-restating doc on '{fname}'",
                                doc_block[0].strip()
                            ))

        # --- V6: Undocumented macro ---
        m = _RE_DEFINE.match(stripped)
        if (m and not _RE_INCLUDE_GUARD.match(stripped)
                and not _RE_SIMPLE_CONST.match(stripped)
                and m.group(1) not in _KNOWN_MACROS):
            prev_idx, prev_line = _prev_content_line(lines, i)
            if prev_idx >= 0 and not _is_doc_line(prev_line):
                report.violations.append(Violation(
                    VType.V6, filepath, i + 1,
                    f"Undocumented macro '{m.group(1)}'",
                    stripped
                ))

    return report


def scan_source(filepath: str) -> FileReport:
    """Scan a .cpp file for violations V1, V9, V10."""
    report = FileReport(path=filepath)
    try:
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return report

    if not lines:
        return report

    # --- V1: Missing file header ---
    first_content = -1
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == "" or stripped.startswith("#include") or stripped.startswith("#pragma"):
            continue
        first_content = i
        break

    if first_content >= 0 and not _is_doc_line(lines[first_content]):
        has_header = False
        for j in range(first_content):
            if _is_doc_line(lines[j]):
                has_header = True
                break
        if not has_header:
            report.violations.append(Violation(
                VType.V1, filepath, first_content + 1,
                "Missing file header",
                lines[first_content].rstrip()
            ))

    # --- V9: Undocumented internal functions >10 lines ---
    in_anon_ns = False
    anon_ns_depth = 0
    brace_depth = 0

    for i, line in enumerate(lines):
        stripped = line.strip()
        brace_depth += stripped.count("{") - stripped.count("}")

        if _RE_ANON_NS_START.match(stripped):
            in_anon_ns = True
            anon_ns_depth = brace_depth

        if in_anon_ns and brace_depth < anon_ns_depth:
            in_anon_ns = False

        # Detect static or anonymous-namespace function definitions
        if (stripped.startswith("static ") or in_anon_ns) and "{" in stripped:
            m = _RE_STATIC_FUNC.match(stripped)
            if m:
                # Count function body lines
                func_brace = 1
                body_lines = 0
                for j in range(i + 1, len(lines)):
                    body_stripped = lines[j].strip()
                    func_brace += body_stripped.count("{") - body_stripped.count("}")
                    body_lines += 1
                    if func_brace <= 0:
                        break

                if body_lines > 10:
                    prev_idx, prev_line = _prev_content_line(lines, i)
                    if prev_idx < 0 or not _is_doc_line(prev_line):
                        report.violations.append(Violation(
                            VType.V9, filepath, i + 1,
                            f"Undocumented internal function '{m.group(1)}' ({body_lines} lines)",
                            stripped
                        ))

    # --- V10: Magic numbers ---
    for i, line in enumerate(lines):
        stripped = line.strip()
        # Skip comments, includes, constexpr, defines, enums
        if (stripped.startswith("//") or stripped.startswith("#")
                or "constexpr" in stripped or "enum" in stripped):
            continue
        # Skip lines with inline explanatory comments
        code_part = stripped.split("//")[0] if "//" in stripped else stripped
        if "//" in stripped and not _RE_MAGIC_NUMBER.search(code_part):
            continue
        # Skip string literals — remove them from analysis
        code_no_str = re.sub(r'"[^"]*"', '""', code_part)
        # Skip array/table data lines (many commas = data)
        if code_no_str.count(",") >= 4:
            continue
        # Skip lines that are clearly data initializers (e.g. {255, 255, ...})
        if re.match(r"^\s*\{?\s*\d+\s*,", code_no_str):
            continue

        for m in _RE_MAGIC_NUMBER.finditer(code_no_str):
            num_str = m.group(1)
            num_base = num_str.rstrip("fFuUlL")
            # Skip trivial values
            if num_base in ("0", "1", "0.0", "1.0", "2", "3", "4",
                            "5", "6", "7", "8", "10", "16", "32", "64",
                            "128", "255", "256", "512", "1024", "2048",
                            "4096", "8192", "100", "1000", "0.5"):
                continue
            # Skip hex literals (already named)
            if "0x" in stripped[:m.start() + 10]:
                continue
            # Skip array sizes, bit shifts
            before = code_no_str[:m.start()]
            if before.rstrip().endswith(("[", "<<", ">>", "=")):
                continue
            # Skip named constant assignments
            if re.match(r"^\s*(const|constexpr|static const|static constexpr)", stripped):
                continue
            # Skip assert/check/log lines (numbers in messages are fine)
            if re.match(r"^\s*(UZ_LOG|UZ_CHECK|UZ_ENSURE|assert|static_assert)", stripped):
                continue
            # Skip return of version strings
            if "return" in before and '"' in stripped:
                continue

            report.violations.append(Violation(
                VType.V10, filepath, i + 1,
                f"Magic number '{num_str}'",
                stripped
            ))

    return report


# ---------------------------------------------------------------------------
# Report formatting
# ---------------------------------------------------------------------------

def format_report_text(reports: list[FileReport], root: str) -> str:
    """Human-readable text report."""
    total_files = len(reports)
    violated = [r for r in reports if r.violations]
    compliant = [r for r in reports if not r.violations]

    # Count by type
    counts: dict[str, int] = {}
    for vt in VType:
        counts[vt.value] = 0
    for r in reports:
        for v in r.violations:
            counts[v.vtype.value] += 1

    total_violations = sum(counts.values())
    lines = []
    lines.append("=" * 60)
    lines.append("  UzEngine Documentation Audit Report")
    lines.append("=" * 60)
    lines.append("")
    lines.append(f"Files scanned:    {total_files}")
    lines.append(f"  Headers (.h):   {sum(1 for r in reports if r.path.endswith('.h'))}")
    lines.append(f"  Sources (.cpp):  {sum(1 for r in reports if r.path.endswith('.cpp'))}")
    lines.append(f"Files compliant:  {len(compliant)}")
    lines.append(f"Files with issues: {len(violated)}")
    lines.append(f"Total violations: {total_violations}")
    lines.append("")

    lines.append("--- Violations by Type ---")
    descs = {
        "V1": "Missing file header",
        "V2": "Undocumented class/struct",
        "V3": "Undocumented public method",
        "V4": "Shallow doc (missing # sections)",
        "V5": "Undocumented enum values",
        "V6": "Undocumented macro",
        "V7": "Wrong comment style",
        "V8": "Name-restating doc",
        "V9": "Undocumented internal fn >10 lines",
        "V10": "Magic numbers",
    }
    for vt in VType:
        c = counts[vt.value]
        marker = " ***" if c > 0 else ""
        lines.append(f"  {vt.value:4s} ({descs[vt.value]:40s}): {c:4d}{marker}")
    lines.append("")

    if violated:
        lines.append("--- Per-File Violations ---")
        lines.append("")
        for r in sorted(violated, key=lambda x: (-len(x.violations), x.path)):
            rel = os.path.relpath(r.path, root)
            v_summary = {}
            for v in r.violations:
                v_summary[v.vtype.value] = v_summary.get(v.vtype.value, 0) + 1
            summary_str = ", ".join(f"{k} x{c}" for k, c in sorted(v_summary.items()))
            lines.append(f"FILE: {rel}  ({summary_str})")
            for v in r.violations:
                lines.append(f"  L{v.line:4d} [{v.vtype.value}] {v.text}")
                if v.context:
                    ctx = v.context[:100].encode("ascii", errors="replace").decode("ascii")
                    lines.append(f"         | {ctx}")
            lines.append("")

    lines.append("=" * 60)
    return "\n".join(lines)


def format_report_json(reports: list[FileReport], root: str) -> str:
    """Machine-readable JSON report."""
    data = {
        "total_files": len(reports),
        "compliant_files": sum(1 for r in reports if not r.violations),
        "violated_files": sum(1 for r in reports if r.violations),
        "total_violations": sum(len(r.violations) for r in reports),
        "by_type": {},
        "files": [],
    }
    for vt in VType:
        data["by_type"][vt.value] = sum(
            1 for r in reports for v in r.violations if v.vtype == vt
        )
    for r in reports:
        if r.violations:
            data["files"].append({
                "path": os.path.relpath(r.path, root),
                "violations": [
                    {
                        "type": v.vtype.value,
                        "line": v.line,
                        "text": v.text,
                        "context": v.context,
                    }
                    for v in r.violations
                ],
            })
    return json.dumps(data, indent=2)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="UzEngine documentation audit scanner"
    )
    parser.add_argument(
        "path", nargs="?", default=None,
        help="Directory or file to scan (default: runtime/engine/)"
    )
    parser.add_argument(
        "--json", action="store_true",
        help="Output machine-readable JSON instead of text"
    )
    parser.add_argument(
        "--severity", type=str, default=None,
        help="Comma-separated violation types to show (e.g. V1,V2,V3)"
    )
    parser.add_argument(
        "--headers-only", action="store_true",
        help="Only scan .h files"
    )
    parser.add_argument(
        "--sources-only", action="store_true",
        help="Only scan .cpp files"
    )
    parser.add_argument(
        "--top", type=int, default=0,
        help="Show only the top N worst files"
    )
    args = parser.parse_args()

    # Resolve root
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    if args.path:
        scan_dir = Path(args.path)
        if not scan_dir.is_absolute():
            scan_dir = project_root / scan_dir
    else:
        scan_dir = project_root / "runtime" / "engine"

    if not scan_dir.exists():
        print(f"Error: {scan_dir} does not exist", file=sys.stderr)
        sys.exit(1)

    # Severity filter
    severity_filter: Optional[set[str]] = None
    if args.severity:
        severity_filter = set(args.severity.upper().split(","))

    # Collect files
    h_files = sorted(scan_dir.rglob("*.h")) if not args.sources_only else []
    cpp_files = sorted(scan_dir.rglob("*.cpp")) if not args.headers_only else []

    print(f"Scanning {len(h_files)} headers + {len(cpp_files)} sources in {scan_dir}...",
          file=sys.stderr)

    reports: list[FileReport] = []

    for hf in h_files:
        r = scan_header(str(hf))
        if severity_filter:
            r.violations = [v for v in r.violations if v.vtype.value in severity_filter]
        reports.append(r)

    for cf in cpp_files:
        r = scan_source(str(cf))
        if severity_filter:
            r.violations = [v for v in r.violations if v.vtype.value in severity_filter]
        reports.append(r)

    # Top-N filter
    if args.top > 0:
        reports_with = [r for r in reports if r.violations]
        reports_without = [r for r in reports if not r.violations]
        reports_with.sort(key=lambda x: -len(x.violations))
        reports = reports_with[:args.top] + reports_without

    # Output
    root_str = str(project_root)
    if args.json:
        print(format_report_json(reports, root_str))
    else:
        print(format_report_text(reports, root_str))

    # Exit code: 1 if any violations found
    total = sum(len(r.violations) for r in reports)
    sys.exit(1 if total > 0 else 0)


if __name__ == "__main__":
    # Force UTF-8 output on Windows
    if sys.platform == "win32":
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    main()
