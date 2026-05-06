#!/usr/bin/env bash
# Verify that docs/engine/45-slate-ui/00-decisions.md contains the six
# locked decisions, each with a "Alternative rejected" subsection. Used
# as the test step for phase18_slate-ui-decisions (no code is written
# in that phase, so structure validation replaces unit tests).
set -euo pipefail

DOC="docs/engine/45-slate-ui/00-decisions.md"

if [[ ! -f "$DOC" ]]; then
  echo "FAIL: $DOC not found" >&2
  exit 1
fi

EXPECTED_DECISIONS=(
  "Decision 1 — Core language: C++23 (no scripting in the core)"
  "Decision 2 — Memory model: \`std::shared_ptr<Widget>\` + \`std::weak_ptr\`"
  "Decision 3 — Data model: Pull via \`Attribute<T>\`"
  "Decision 4 — Threading: UI single-thread, draw lists immutable"
  "Decision 5 — Coordinates: DPI-aware, Slate unit = virtual pixel @ 96 DPI"
  "Decision 6 — Text shaping: FreeType + HarfBuzz with ASCII bitmap fallback"
)

failed=0
for d in "${EXPECTED_DECISIONS[@]}"; do
  if ! grep -F -- "$d" "$DOC" >/dev/null; then
    echo "FAIL: missing section header: $d" >&2
    failed=1
  fi
done

# Each decision must have an "Alternative rejected" sentence.
alt_count=$(grep -c "^\*\*Alternative rejected\.\*\*" "$DOC" || true)
if [[ "$alt_count" -lt 6 ]]; then
  echo "FAIL: expected 6 'Alternative rejected.' subsections, found $alt_count" >&2
  failed=1
fi

# Deferred decisions table must be present.
if ! grep -F "Deferred decisions (revisit later)" "$DOC" >/dev/null; then
  echo "FAIL: missing 'Deferred decisions (revisit later)' section" >&2
  failed=1
fi

# README index must enumerate all 11 phases.
README="docs/engine/45-slate-ui/README.md"
if [[ ! -f "$README" ]]; then
  echo "FAIL: $README not found" >&2
  exit 1
fi

for slug in slate-ui-decisions slate-ui-mvp-renderer slate-ui-widget-base \
            slate-ui-layout-boxes slate-ui-input-button slate-ui-invalidation \
            slate-ui-style-brush slate-ui-composite-widgets slate-ui-declarative-layer \
            slate-ui-window-focus-dnd slate-ui-vfx-editor-rebuild; do
  if ! grep -F "\`$slug\`" "$README" >/dev/null; then
    echo "FAIL: README missing entry for $slug" >&2
    failed=1
  fi
done

if [[ "$failed" -ne 0 ]]; then
  exit 1
fi

echo "OK: 6 decisions present, 6 'Alternative rejected.' subsections, deferred section, 11 README entries"
exit 0
