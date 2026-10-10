# Regression guard (phase0_sponza-followup-07 / -08): the Bloom pass must be built from the
# image the tonemapper displays, at that image's resolution.
#
# In RegisterPostProcessPasses the live path is the block that ends with the unconditional
# `return; // Skip full pipeline` — everything after it is dead code and is NOT checked here.
# Inside that block `tonemapInputSrv` starts as the HDR scene colour (render resolution) and
# becomes the TAA output (display resolution) when TAA ran. Bloom must:
#   * read `tonemapInputSrv` (never the raw jittered HDR while TAA output is displayed),
#   * size its mip chain from the source it reads (viewport dims only for the TAA output),
#   * capture those values by value into the deferred callback.
# History: reading the jittered HDR was found while chasing the close-up wobble; it was a real
# mismatch but NOT the wobble's cause (r.Bloom=0 left the wobble unchanged — the cause was the
# jitter matrix, see taa_jitter.h and gtest TaaJitter.*).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$pp = Get-Content (Join-Path $root 'runtime/engine/renderer/frame/frame_postprocess.cpp') -Raw -Encoding UTF8

$checks = [ordered]@{}

# Live block: from the function start to the early return.
$fnStart = $pp.IndexOf('void SceneRenderer::RegisterPostProcessPasses(')
$retIdx  = $pp.IndexOf('return; // Skip full pipeline')
$checks['live_block_found'] = ($fnStart -ge 0) -and ($retIdx -gt $fnStart)
$live = if ($checks['live_block_found']) { $pp.Substring($fnStart, $retIdx - $fnStart) } else { '' }

# tonemapInputSrv: HDR by default, TAA output once TAA ran, and assigned before bloom.
$checks['tonemap_input_defaults_to_hdr'] = $live -match 'uint32_t tonemapInputSrv = m_hdrSceneColorSrv;'
$assignIdx = $live.IndexOf('tonemapInputSrv = m_taaHistorySrv[writeIdx];')
$bloomIdx  = $live.IndexOf('Bloom pass (if enabled)')
$checks['taa_output_assigned_before_bloom'] = ($assignIdx -ge 0) -and ($bloomIdx -gt $assignIdx)

$bloomBlock = [regex]::Match($live, '(?s)Bloom pass \(if enabled\).*?rgBuilder\.AddPass\("Bloom".*?\}\);\s*\}\);\s*\}').Value
$checks['bloom_block_found'] = [bool]$bloomBlock
$checks['bloom_reads_tonemap_input'] = $bloomBlock -match 'const uint32_t bloomInputSrv = tonemapInputSrv;'
$checks['bloom_not_raw_hdr'] = -not ($bloomBlock -match 'Execute\(cmd, m_hdrSceneColorSrv')
$checks['bloom_resolution_follows_source'] = ($bloomBlock -match 'const bool bloomFromDisplayRes = \(bloomInputSrv != m_hdrSceneColorSrv\);') `
    -and ($bloomBlock -match 'bloomFromDisplayRes \? ctx\.vpWidth\s*: ctx\.renderWidth') `
    -and ($bloomBlock -match 'bloomFromDisplayRes \? ctx\.vpHeight : ctx\.renderHeight')
$checks['bloom_executes_with_source_and_dims'] = $bloomBlock -match 'm_bloomPass->Execute\(cmd, bloomInputSrv, bloomW, bloomH, ctx\.perFrame\.exposure\);'
$checks['bloom_captures_by_value'] = ($bloomBlock -match '\[&, bloomInputSrv, bloomW, bloomH\]') -and ($bloomBlock -match '\[this, &ctx, bloomInputSrv, bloomW, bloomH\]')
$checks['bloom_comment_does_not_blame_bloom_for_wobble'] = $bloomBlock -match 'NOT to be\s+// the close-up wobble'

# Tonemap in the live block reads the same texture and validates its pipeline before opening the pass.
$checks['tonemap_reads_captured_srv'] = $live -match 'const uint32_t capturedTonemapSrv = tonemapInputSrv;'
$tonemapBlock = [regex]::Match($live, '(?s)rgBuilder\.AddPass\("Tonemap".*').Value
$beginIdx = $tonemapBlock.IndexOf('cmd->BeginRenderPass(passDesc);')
$guardIdx = $tonemapBlock.IndexOf('if (!tonemapPipeline.IsValid()) return;')
$checks['tonemap_guard_before_begin_render_pass'] = ($beginIdx -ge 0) -and ($guardIdx -ge 0) -and ($guardIdx -lt $beginIdx)

# No other displayed consumer of the jittered HDR in the live block: the permitted readers are
# the TAA input, the default tonemap input, the SSR previous-colour copy and the bloom source test.
$readers = [regex]::Matches($live, 'm_hdrSceneColorSrv') | ForEach-Object {
    $s = [math]::Max(0, $_.Index - 120); $live.Substring($s, [math]::Min(200, $live.Length - $s)) }
$allowed = $readers | Where-Object { $_ -match 'sceneColorSrv\s*=|tonemapInputSrv = m_hdrSceneColorSrv|bloomInputSrv != m_hdrSceneColorSrv|copySource|ssr|SSR' }
$checks['no_other_jittered_hdr_consumer'] = ($readers.Count -gt 0) -and ($readers.Count -eq $allowed.Count)

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
foreach ($c in $checks.GetEnumerator()) { '{0,-48} {1}' -f $c.Key, $(if ($c.Value) { 'PASS' } else { 'FAIL' }) }
'{0}/{1} checks passed' -f ($checks.Count - $failed.Count), $checks.Count
if ($failed.Count -gt 0) { exit 1 }
