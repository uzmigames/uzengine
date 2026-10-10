# Guard: the build tracks embedded shaders as header dependencies.
#
# uzembed inlines every `#include` of an .hlsl into build/generated/shaders/**/*.hlsl.gen.h
# and regenerates all of them on every `zig build` (the output dir is removed first). The
# C++ TU that includes `engine_shaders.gen.h` (pipeline_lifecycle.cpp) is compiled by zig
# with a clang depfile, and zig's cache manifest lists every generated header with its
# content hash — so a change to any .hlsl/.hlsli reaches the binary after ONE build.
# Verified empirically 2026-09-08 (phase0_sponza-followup-08): a comment added to
# sdf_reflections.hlsli appeared in deferred_lighting_ps.hlsl.gen.h and in uzengine.exe
# after a single build. Earlier reports of a "stale embed" were an install failure while
# the exe was running, not a dependency gap.
#
# This script checks the mechanism statically: the cache manifest of the embedding TU for
# THIS checkout must list engine_shaders.gen.h and at least one .hlsl.gen.h.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifestDir = Join-Path $root 'runtime/.zig-cache/h'
if (-not (Test-Path $manifestDir)) { Write-Output 'SKIP: no zig cache yet (build once).'; exit 0 }

$genDirRel = 'build\generated\shaders'
$rootWin = (Resolve-Path $root).Path.TrimEnd('\')
$checks = [ordered]@{}

# Manifests that mention the embedding TU and belong to this checkout (same drive/path).
$tuName = 'pipeline_lifecycle.cpp'
$manifests = Get-ChildItem $manifestDir -Filter '*.txt' | Where-Object {
    $t = Get-Content $_.FullName -Raw -ErrorAction SilentlyContinue
    $t -and $t.Contains($tuName) -and $t.Contains($rootWin)
}
$checks['embedding_tu_manifest_found'] = $manifests.Count -gt 0

$listsIndex = $false; $listsGen = $false; $hasHash = $false
foreach ($m in $manifests) {
    $t = Get-Content $m.FullName -Raw
    if ($t -match [regex]::Escape((Join-Path $genDirRel 'engine_shaders.gen.h'))) { $listsIndex = $true }
    if ($t -match [regex]::Escape($genDirRel) + '\\[^\r\n]+\.hlsl\.gen\.h') { $listsGen = $true }
    # manifest rows: size inode mtime hash flags path — the hash column is 32 hex chars
    if ($t -match '(?m)^\d+ \d+ \d+ [0-9a-f]{32} \d+ .*engine_shaders\.gen\.h') { $hasHash = $true }
}
$checks['manifest_lists_engine_shaders_index'] = $listsIndex
$checks['manifest_lists_hlsl_gen_headers'] = $listsGen
$checks['manifest_rows_carry_content_hash'] = $hasHash

# build.zig: the runtime library depends on the embed step and the generated dir is an include path.
$bz = Get-Content (Join-Path $root 'runtime/build.zig') -Raw
$checks['lib_depends_on_embed_step'] = $bz -match 'lib\.step\.dependOn\(&run_embed\.step\);'
$checks['generated_dir_is_include_path'] = $bz -match 'addIncludePath\(b\.path\("build/generated/shaders"\)\)'
# uzembed regenerates everything (clean step precedes it), so no stale .gen.h can survive.
$checks['generated_dir_cleaned_before_embed'] = $bz -match 'run_embed\.step\.dependOn\(&clean_generated\.step\);'

# The embedding TU must not carry a hand-edited "rev N" marker used to force recompiles.
$pl = Get-Content (Join-Path $root 'runtime/engine/renderer/core/pipeline/pipeline_lifecycle.cpp') -Raw
$checks['no_manual_recompile_marker'] = -not ($pl -match 'engine_shaders\.h"\s*//.*rev \d')

$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
foreach ($c in $checks.GetEnumerator()) { '{0,-40} {1}' -f $c.Key, $(if ($c.Value) { 'PASS' } else { 'FAIL' }) }
'{0}/{1} checks passed ({2} manifest(s) for {3})' -f ($checks.Count - $failed.Count), $checks.Count, $manifests.Count, $tuName
if ($failed.Count -gt 0) { exit 1 }
