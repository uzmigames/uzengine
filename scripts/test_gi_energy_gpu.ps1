# GPU comparison of the two probe writers on the same scene and grid (phase0_sponza-followup-08, item 2).
#
# Runs the Sponza project with the DXR path on and both writers' transport diagnostics enabled.
# The SDF writer (radiance_cache_trace_cs.hlsl) and the DDGI writer (ddgi_probe_update_cs.hlsl)
# each log, in LINEAR radiance, the mean luminance of the ray radiance their probes saw:
#   "RC transport: grid ... meanRadiance=X skyFraction=Y"   and   "DDGI transport: grid ... meanRadiance=X skyFraction=Y"
# plus one line per probe (every probe since 2026-09-16). This script prints the ratio of the two grid
# lines and the per-probe pairs. Both grids share origin and spacing (DDGISystem::SetGridOrigin(rcOrigin)),
# so the pairs are probe-to-probe; the grid ratio is not (2026-10-08): the RC line averages the probes
# with field distance >= 5 cm, unreachable mode-3 probes included at 0, and the DDGI line every probe;
# scripts/gi_energy_populations.py re-reads this log on one population.
# Parity target: |ratio - 1| <= 0.10 — reported as MET / NOT MET; the script
# fails only when the measurement itself is missing or non-finite (use -Strict to fail on parity).
param([int]$Frames = 200, [switch]$Strict, [switch]$ReuseLog)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'runtime/build/bin/uzengine.exe'
if (-not (Test-Path $exe)) { throw "uzengine.exe not found at $exe" }
$log = Join-Path $PSScriptRoot 'test_gi_energy_gpu.log'
$arguments = @('--project', 'sample/projects/sponza.uzproject', '--frames', $Frames, '--verbose',
    '--cvar', 'r.RayTracing=1', '--cvar', 'r.RayTracing.GI=1', '--cvar', 'r.GI.RadianceCache.Diagnostic=1', '--cvar', 'r.GI.DDGI.Diagnostic=1')
# r.RayTracing is the master switch: without it the TLAS is never built and every DDGI ray
# reports sky (skyFraction 1.0, radiance = sky luminance) — the first run of this harness caught
# exactly that (2026-09-08), which is also why the DXR path looked "brighter" in followup-01.
if (-not ($ReuseLog -and (Test-Path $log))) {
    Push-Location $root
    try { & $exe @arguments *> $log } finally { Pop-Location }
}

$text = Get-Content $log -Raw -Encoding UTF8
function Finite([double]$v) { -not ([double]::IsNaN($v) -or [double]::IsInfinity($v)) }
function Grab([string]$pattern) { $m = [regex]::Match($text, $pattern); if ($m.Success) { [double]$m.Groups[1].Value } else { $null } }

$rcGrid   = Grab 'RC transport: grid .*?meanRadiance=([-0-9.eE+]+)'
$rcSky    = Grab 'RC transport: grid .*?skyFraction=([-0-9.eE+]+)'
$ddgiGrid = Grab 'DDGI transport: grid .*?meanRadiance=([-0-9.eE+]+)'
$ddgiSky  = Grab 'DDGI transport: grid .*?skyFraction=([-0-9.eE+]+)'
$ddgiRan  = $text -match 'DDGI transport: queued diagnostic snapshot'
$rtState  = [regex]::Match($text, 'RTManager: Initialized \(([^)]*)\)').Groups[1].Value
$tlasInfo = [regex]::Match($text, 'DDGI transport: queued diagnostic snapshot[^
]*').Value
$dxrReady = $text -match 'RayTracing|TLAS|DXR'

$checks = [ordered]@{}
$checks['rc_grid_line_present']   = $null -ne $rcGrid
$checks['ddgi_grid_line_present'] = $null -ne $ddgiGrid
$checks['rc_grid_finite_positive']   = ($null -ne $rcGrid) -and (Finite $rcGrid) -and $rcGrid -gt 0
$checks['ddgi_grid_finite_positive'] = ($null -ne $ddgiGrid) -and (Finite $ddgiGrid) -and $ddgiGrid -gt 0

# Per-probe pairs
$rcProbes = @{}; $ddgiProbes = @{}
foreach ($m in [regex]::Matches($text, 'RC transport: probe=(\d+) .*?meanRadiance=([-0-9.eE+]+)')) { $rcProbes[[int]$m.Groups[1].Value] = [double]$m.Groups[2].Value }
foreach ($m in [regex]::Matches($text, 'DDGI transport: probe=(\d+) .*?meanRadiance=([-0-9.eE+]+)')) { $ddgiProbes[[int]$m.Groups[1].Value] = [double]$m.Groups[2].Value }
$pairs = @($rcProbes.Keys | Where-Object { $ddgiProbes.ContainsKey($_) } | Sort-Object)
$checks['central_probe_pairs_present'] = $pairs.Count -ge 4
$checks['ddgi_rays_hit_geometry'] = ($null -ne $ddgiSky) -and $ddgiSky -lt 0.999

$ratio = if ($checks['rc_grid_finite_positive'] -and $checks['ddgi_grid_finite_positive']) { $ddgiGrid / $rcGrid } else { [double]::NaN }
$parityMet = (Finite $ratio) -and [math]::Abs($ratio - 1.0) -le 0.10
if ($Strict) { $checks['parity_within_10pct'] = $parityMet }

foreach ($c in $checks.GetEnumerator()) { '{0,-34} {1}' -f $c.Key, $(if ($c.Value) { 'PASS' } else { 'FAIL' }) }
'GI energy (linear, grid mean ray radiance): SDF={0} DDGI={1} ratio DDGI/SDF={2:N4} skyFraction SDF={3} DDGI={4} -> parity 1±0.10 {5}' -f `
    $rcGrid, $ddgiGrid, $ratio, $rcSky, $ddgiSky, $(if ($parityMet) { 'MET' } else { 'NOT MET' })
foreach ($p in $pairs) {
    $r = if ($rcProbes[$p] -gt 0) { $ddgiProbes[$p] / $rcProbes[$p] } else { [double]::NaN }
    '  probe {0,5}: SDF={1,10:N5} DDGI={2,10:N5} ratio={3:N3}' -f $p, $rcProbes[$p], $ddgiProbes[$p], $r
}
Write-Output ('RT state: ' + $rtState); if ($tlasInfo) { Write-Output ('DDGI: ' + $tlasInfo.Substring([math]::Max(0, $tlasInfo.IndexOf('frame=')))) }
if (-not $ddgiRan) { Write-Output 'note: the DDGI diagnostic never queued — DXR path inactive (no TLAS / r.RayTracing.GI off / unsupported GPU).' }
$failed = @($checks.GetEnumerator() | Where-Object { -not $_.Value })
'{0}/{1} checks passed; log: {2}' -f ($checks.Count - $failed.Count), $checks.Count, $log
if ($failed.Count -gt 0) { exit 1 }
