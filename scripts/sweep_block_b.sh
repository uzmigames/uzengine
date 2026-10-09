#!/usr/bin/env bash
# Lighting block B — closing sweep: every block-B harness on one binary, each into its own output
# directory under .screenshots/flicker-elimination/sweep-b/ so the committed reference captures the
# harnesses compare against (test-contact-shadows/sponza-on, test-layered-reflections/sponza-ssr-off,
# test-ddgi-dedup/nodxr, test-clustered-lights/before) stay untouched. Sequential: the timing gates
# need the GPU to themselves. Prints one summary line per harness at the end.
# Usage: bash scripts/sweep_block_b.sh [LOGDIR]
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO" || exit 1
LOGDIR="${1:-$REPO/.screenshots/flicker-elimination/sweep-b/logs}"
OUT="$REPO/.screenshots/flicker-elimination/sweep-b"
mkdir -p "$LOGDIR" "$OUT"
SUMMARY="$LOGDIR/summary.txt"
: > "$SUMMARY"

run_py() {   # name script [args...]
    local name="$1"; shift
    local t0=$(date +%s)
    echo "=== $name ($(date +%H:%M:%S))"
    python "$@" > "$LOGDIR/$name.log" 2>&1
    local rc=$?
    local line
    line=$(grep -E "checks passed|PASS|FAIL" "$LOGDIR/$name.log" | grep -E "^[0-9]+/[0-9]+ checks passed|^[0-9]+/[0-9]+" | tail -1)
    [ -z "$line" ] && line=$(grep -E "checks passed" "$LOGDIR/$name.log" | tail -1)
    local fails
    fails=$(grep -c "\[FAIL\]" "$LOGDIR/$name.log")
    echo "$name: exit $rc; $line; FAIL lines $fails; $(( $(date +%s) - t0 )) s" | tee -a "$SUMMARY"
}

run_ps() {   # name script
    local name="$1"; shift
    local t0=$(date +%s)
    echo "=== $name ($(date +%H:%M:%S))"
    powershell -NoProfile -ExecutionPolicy Bypass -File "$@" > "$LOGDIR/$name.log" 2>&1
    local rc=$?
    local line
    line=$(grep -aE "checks passed|passed|PASS|FAIL" "$LOGDIR/$name.log" | tail -1)
    echo "$name: exit $rc; $line; $(( $(date +%s) - t0 )) s" | tee -a "$SUMMARY"
}

run_py diag_timing        scripts/test_diag_timing.py        --out "$OUT/test-diag-timing"
run_py gdf_incremental    scripts/test_gdf_incremental.py    --out "$OUT/test-gdf-incremental"
run_py gdf_regional       scripts/test_gdf_regional.py       --out "$OUT/test-gdf-regional"
run_ps gdf_raw_composite  scripts/test_gdf_raw_composite.ps1
run_ps gdf_raw_lifecycle  scripts/test_gdf_raw_lifecycle.ps1
run_py sdf_bake_cache     scripts/test_sdf_bake_cache.py     --out "$OUT/test-sdf-bake-cache"
run_py froxel_fog         scripts/test_froxel_fog.py         --out "$OUT/test-froxel-fog"
run_py contact_shadows    scripts/test_contact_shadows.py    --out "$OUT/test-contact-shadows"
run_py layered_reflections scripts/test_layered_reflections.py --out "$OUT/test-layered-reflections"
run_py ddgi_dedup         scripts/test_ddgi_dedup.py         --out "$OUT/test-ddgi-dedup"
run_py clustered_lights   scripts/test_clustered_lights.py   --out "$OUT/test-clustered-lights"
run_py cbuffer_layouts    scripts/test_cbuffer_layouts.py

echo "=== sweep done ($(date +%H:%M:%S))"
cat "$SUMMARY"
