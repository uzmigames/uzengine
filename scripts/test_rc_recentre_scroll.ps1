# Regression guard for phase0_sponza-followup-06 item 4.5: the radiance-cache grid
# recentre must scroll the probe history instead of retracing from scratch, and
# deferred lighting must index the completed SH buffer with the origin it was
# traced with. Source-level checks plus a CPU model of the index mapping.
#
# Symptom guarded against: a dark blink of the whole indirect lighting once per
# probe spacing (~267 cm on Sponza) of camera travel on ANY axis (reported while
# rising vertically at z ~ 267), recovering over ~20 frames (EMA alpha 0.12).
#
# Re-anchored 2026-10-08 (the script had not changed since 7eea6b95 and failed 7 of its
# 40 checks on the sources): block B-02 (0a9cc732) grew RadianceCacheTraceConstants to
# 464 bytes - the shift row's fourth field `_historyShiftPad` became `regionCount` and 16
# change-box rows follow the row - and gave probes inside a change box their own blend
# weight; block B-06 (edfd7942) moved the DDGI writers' mirror of the 13 rows into
# ddgi_probe_common.hlsli. The layout and blend checks follow those sources. What they
# guard is unchanged as long as change boxes come only from GDF instance changes
# (global_distance_field.cpp, frame_gi_passes.cpp, as read on 2026-10-08): a pure recentre
# then still blends every probe that keeps its history at temporalBlendAlpha. This script
# does not check where change boxes come from, nor InChangedRegion's body, nor what precedes
# the shift row in the HLSL mirrors. Since 2026-10-09 the blend check also pins
# `fieldChanged = hasHistory && InChangedRegion(probePos)`: without it, a shader that set
# fieldChanged for every probe with history (all of them replaced on a recentre) passed 42/42.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$rcHeader = Get-Content (Join-Path $root 'runtime/engine/renderer/gi/radiance_cache.h') -Raw -Encoding UTF8
$rcSource = Get-Content (Join-Path $root 'runtime/engine/renderer/gi/radiance_cache.cpp') -Raw -Encoding UTF8
$trace    = Get-Content (Join-Path $root 'runtime/shaders/runtime/gi/radiance_cache_trace_cs.hlsl') -Raw -Encoding UTF8
# The DDGI writers (update and trace) share the mirror in ddgi_probe_common.hlsli since B-06 (edfd7942).
$ddgi     = Get-Content (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_common.hlsli') -Raw -Encoding UTF8
$ddgiUpdate = Get-Content (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl') -Raw -Encoding UTF8
$ddgiTrace  = Get-Content (Join-Path $root 'runtime/shaders/runtime/gi/ddgi_probe_trace_cs.hlsl') -Raw -Encoding UTF8
$frameCb  = Get-Content (Join-Path $root 'runtime/engine/renderer/frame/frame_constants.cpp') -Raw -Encoding UTF8

$checks = [ordered]@{}

# ── CB layout: the grid-shift row is the 13th 16-byte row (bytes 192-207), declared by every shader that binds it ──
# Since B-02 (0a9cc732) the C++ struct and the trace mirror continue with 16 change-box rows (464 bytes)
# and the row's fourth field is `regionCount`; the DDGI mirror declares the 13 rows and ends there.
$checks['cb_size_464'] = $rcHeader -match 'static_assert\(sizeof\(RadianceCacheTraceConstants\) == 464'
$checks['cb_shift_row_offsets'] = ($rcHeader -match 'offsetof\(RadianceCacheTraceConstants, regionCount\) == 204') -and ($rcHeader -match 'offsetof\(RadianceCacheTraceConstants, regionLo\) == 208')
$checks['cb_has_shift_row_cpp'] = ($rcHeader -match 'int32_t\s+historyShiftX') -and ($rcHeader -match 'int32_t\s+historyShiftZ') -and ($rcHeader -match 'uint32_t\s+regionCount')
$checks['cb_has_shift_row_trace_hlsl'] = ($trace -match 'int\s+historyShiftX;') -and ($trace -match 'int\s+historyShiftZ;') -and ($trace -match 'uint\s+regionCount;')
$checks['cb_has_shift_row_ddgi_hlsl'] = ($ddgi -match 'int\s+historyShiftX;') -and ($ddgi -match 'uint\s+_historyShiftPad;')
# The C++ offsets above pin the row at bytes 192-207; in the trace mirror only the change-box rows may
# follow it, and the DDGI mirror ends with it. Both DDGI writers must include that mirror.
$checks['cb_shift_row_then_regions_trace'] = [regex]::IsMatch($trace, '(?s)historyShiftZ;\s*uint\s+regionCount;[^\r\n]*\s*float4\s+regionLo\[8\];[^\r\n]*\s*float4\s+regionHi\[8\];[^\r\n]*\s*\};')
$checks['cb_shift_row_is_last_ddgi']  = [regex]::IsMatch($ddgi,  '(?s)_historyShiftPad;\s*\};')
$checks['ddgi_writers_include_mirror'] = ([regex]::IsMatch($ddgiUpdate, '#include "ddgi_probe_common\.hlsli"')) -and ([regex]::IsMatch($ddgiTrace, '#include "ddgi_probe_common\.hlsli"'))

# ── C++: a recentre is a full retrace WITH history; only first write / invalidation drop it ──
$checks['reset_only_first_write_or_invalid'] = $rcSource -match 'const bool resetHistory\s*=\s*m_firstWritePending \|\| m_historyInvalid;'
$checks['full_retrace_includes_dirty']       = $rcSource -match 'const bool fullRetrace\s*=\s*resetHistory \|\| m_dirty;'
$checks['budget_all_probes_on_full_retrace'] = $rcSource -match 'probeBudget = fullRetrace \? kTotalProbes'
$checks['preserve_copy_skipped_on_full_retrace'] = $rcSource -match 'if \(!fullRetrace\) \{'
$checks['history_srv_kept_on_recentre'] = $rcSource -match 'rootConstants\[1\] = resetHistory \? UINT32_MAX : m_historyShBufferSrv;'
$checks['first_write_only_on_reset'] = $rcSource -match 'constants\.firstWrite\s*=\s*resetHistory \? 1u : 0u;'
$checks['shift_published_to_cb'] = ($rcSource -match 'constants\.historyShiftX\s*=\s*resetHistory \? 0 : m_pendingHistoryShift\[0\]') -and ($rcSource -match 'constants\.historyShiftZ\s*=\s*resetHistory \? 0 : m_pendingHistoryShift\[2\]')
$checks['shift_accumulated_in_update'] = [regex]::IsMatch($rcSource, 'm_pendingHistoryShift\[2\] \+= static_cast<int32_t>\(std::lround\(\(snappedCenter\.z - m_gridCenter\.z\) \* invSpacing\)\);')
$checks['shift_accumulated_before_center_moves'] = ($rcSource.IndexOf('m_pendingHistoryShift[2] +=') -lt $rcSource.IndexOf('m_gridCenter = snappedCenter;'))
$checks['shift_cleared_after_dispatch'] = [regex]::IsMatch($rcSource, '(?s)m_completedGridOrigin = m_gridOrigin;\s*m_pendingHistoryShift\[0\] = m_pendingHistoryShift\[1\] = m_pendingHistoryShift\[2\] = 0;\s*m_dirty = false;')
$checks['invalidate_sets_history_invalid'] = $rcHeader -match 'void InvalidateHistory\(\) noexcept \{ m_dirty = true; m_historyInvalid = true; \}'
$checks['has_produced_result_not_gated_on_dirty'] = -not ([regex]::Match($rcHeader, '(?s)bool HasProducedResult\(\) const noexcept \{.*?\}').Value -match 'm_dirty')

# ── Lighting indexes the completed buffer with the origin it was traced with ──
$rcBlock = [regex]::Match($frameCb, '(?s)perFrame\.radianceCacheEnabled = 1;.*?perFrame\.rcGiIntensity').Value
$checks['lighting_uses_completed_origin'] = $rcBlock -match 'm_radianceCache\.GetCompletedGridOrigin\(\)'
$checks['lighting_not_current_origin']    = -not ($rcBlock -match 'm_radianceCache\.GetGridOrigin\(\)')
$checks['completed_origin_accessor'] = $rcHeader -match 'Vec3 GetCompletedGridOrigin\(\) const noexcept \{ return m_completedGridOrigin; \}'

# ── Shader: history read through the previous grid, both in the blend and in the feedback ──
$feedback = [regex]::Match($trace, '(?s)float3 SamplePreviousDiffuse\(float3 position, float3 normal\) \{.*?\n\}').Value
$checks['feedback_found'] = [bool]$feedback
$checks['feedback_uses_history_origin'] = ($feedback -match 'HistoryGridOrigin\(\)') -and ($feedback -match 'cell = \(position - historyOrigin\) / probeSpacing') -and ($feedback -match 'probePosition = historyOrigin \+ float3\(index\) \* probeSpacing')
$checks['feedback_no_raw_grid_origin']  = -not ($feedback -match 'float3\(gridOriginX, gridOriginY, gridOriginZ\)')
$checks['history_origin_formula'] = $trace -match 'return float3\(gridOriginX, gridOriginY, gridOriginZ\) - float3\(HistoryShift\(\)\) \* probeSpacing;'
$blend = [regex]::Match($trace, '(?s)Hysteresis: temporal blend with previous SH.*?shBuf\[base \+ 6\]\.w = 1\.0;').Value
$checks['blend_found'] = [bool]$blend
$checks['blend_shifted_coord'] = $blend -match 'int3 historyCoord = int3\(int\(gx\), int\(gy\), int\(gz\)\) \+ HistoryShift\(\);'
$checks['blend_bounds_check']  = $blend -match 'all\(historyCoord >= 0\) && all\(historyCoord < int\(probesPerAxis\)\)'
$checks['blend_validity_check'] = $blend -match 'hasHistory = validity\[historyBase \+ 6\]\.w > 0\.0;'
$checks['blend_reads_shifted_history'] = $blend -match 'lerp\(history\[historyBase \+ k\], newPacked\[k\], a\)'
# Since B-02 (0a9cc732) a probe inside a change box (fieldChanged) blends at regionBlendAlpha (default 1 = replace);
# a probe without history still replaces and every other probe blends at temporalBlendAlpha.
$checks['blend_replace_without_history'] = ($blend -match 'const bool fieldChanged = hasHistory && InChangedRegion\(probePos\);') -and ($blend -match 'float a = hasHistory \? saturate\(fieldChanged \? regionBlendAlpha : temporalBlendAlpha\) : 1\.0;') -and ($blend -match 'const bool replace = !hasHistory \|\| a >= 1\.0;') -and ($blend -match 'if \(replace\) \{')

# ── CPU model of the mapping: probe c in the new grid == probe c + shift in the old grid ──
# Integer arithmetic on a 16^3 grid with spacing 1 (positions are integers), so the
# equality is exact. Also counts how many probes keep history for the three cases
# the sweep scenes exercise (one axis, two axes, none).
$P = 16
function ProbesKeepingHistory([int[]]$shift) {
    $kept = 0
    for ($z = 0; $z -lt $P; $z++) { for ($y = 0; $y -lt $P; $y++) { for ($x = 0; $x -lt $P; $x++) {
        $h = @(($x + $shift[0]), ($y + $shift[1]), ($z + $shift[2]))
        $inRange = ($h[0] -ge 0) -and ($h[0] -lt $P) -and ($h[1] -ge 0) -and ($h[1] -lt $P) -and ($h[2] -ge 0) -and ($h[2] -lt $P)
        if ($inRange) {
            # world position of probe (x,y,z) in the new grid: O_new + c, with O_new = O_old + shift
            # world position of history probe h in the old grid: O_old + h. Must coincide.
            $newWorld = @(($shift[0] + $x), ($shift[1] + $y), ($shift[2] + $z))
            $oldWorld = @($h[0], $h[1], $h[2])
            if (($newWorld[0] -ne $oldWorld[0]) -or ($newWorld[1] -ne $oldWorld[1]) -or ($newWorld[2] -ne $oldWorld[2])) { throw "mapping mismatch at ($x,$y,$z) shift ($shift)" }
            $kept++
        }
    } } }
    return $kept
}
$checks['model_shift_none_keeps_all']        = (ProbesKeepingHistory @(0, 0, 0)) -eq ($P * $P * $P)
$checks['model_shift_up_keeps_15_of_16']     = (ProbesKeepingHistory @(0, 0, 1)) -eq ($P * $P * ($P - 1))
$checks['model_shift_down_keeps_15_of_16']   = (ProbesKeepingHistory @(0, 0, -1)) -eq ($P * $P * ($P - 1))
$checks['model_shift_diagonal_keeps_225_of_256'] = (ProbesKeepingHistory @(1, 1, 0)) -eq ($P * ($P - 1) * ($P - 1))
$checks['model_shift_beyond_grid_keeps_none'] = (ProbesKeepingHistory @(0, 0, $P)) -eq 0
# The feedback helper: HistoryGridOrigin = O_new - shift*spacing must equal O_old.
$spacing = 266.6667; $oOld = 100.0; $shiftZ = 1
$oNew = $oOld + $shiftZ * $spacing
$checks['model_history_origin_equals_old_origin'] = [math]::Abs(($oNew - $shiftZ * $spacing) - $oOld) -lt 1e-3

# ── EMA model of the blink: what one recentre does to a converged probe ──
# Converged value V = 1. The single-frame trace estimate has no multi-bounce when the
# feedback is cut (first-write semantics): E_nofeedback = 0.6 V (Sponza interiors are
# multi-bounce dominated). With history kept, the estimate keeps its feedback (E ~ V)
# and is blended at alpha 0.12.
$alpha = 0.12; $V = 1.0
$blinkBefore = 0.6 * $V                                 # replace: value drops to the one-frame estimate
$blinkAfter  = (1 - $alpha) * $V + $alpha * (0.95 * $V) # blend with feedback alive (5% estimator noise)
$checks['model_before_dark_blink_ge_30pct'] = ($V - $blinkBefore) / $V -ge 0.30
$checks['model_after_deviation_le_2pct']    = [math]::Abs($V - $blinkAfter) / $V -le 0.02
# Recovery time of the old behaviour: frames until the EMA is within 5% of V again.
# (PowerShell variables are case-insensitive: never reuse `$V` in lower case.)
$ema = $blinkBefore; $frames = 0
while ([math]::Abs($V - $ema) / $V -gt 0.05 -and $frames -lt 1000) { $ema = (1 - $alpha) * $ema + $alpha * $V; $frames++ }
$checks['model_before_recovery_over_15_frames'] = $frames -ge 15

# ── Report ──
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
foreach ($c in $checks.GetEnumerator()) { '{0,-46} {1}' -f $c.Key, $(if ($c.Value) { 'PASS' } else { 'FAIL' }) }
'{0}/{1} checks passed' -f ($checks.Count - $failed.Count), $checks.Count
if ($failed.Count -gt 0) { exit 1 }
