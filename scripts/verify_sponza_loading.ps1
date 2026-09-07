$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'runtime/build/bin/uzengine.exe'
$quality = @(
    '--width', '1920', '--height', '1080',
    '--cvar', 'r.RayTracing=0', '--cvar', 'r.RadianceCache=1',
    '--cvar', 'r.GI.RadianceCache.RaysPerProbe=64',
    '--cvar', 'r.GI.RadianceCache.ProbesPerFrame=4096',
    '--cvar', 'r.GI.Reflections=1', '--cvar', 'r.GI.GDF.Resolution=256',
    '--cvar', 'r.GI.MeshSdf.Resolution=128',
    '--cvar', 'r.GI.Reflections.Samples=16', '--cvar', 'r.GI.Reflections.Steps=256',
    '--cvar', 'r.TAA=1', '--cvar', 'r.TAA.JitterSequenceLength=16',
    '--cvar', 'r.GTAO=1', '--cvar', 'r.GTAO.Quality=3',
    '--cvar', 'r.GTAO.NumAngles=16', '--cvar', 'r.GTAO.NumTaps=16',
    '--cvar', 'r.SSR=1', '--cvar', 'r.SSR.NumSteps=64', '--log-level', 'info'
)
$runs = @(
    @{ Name='startup'; Project='sample/projects/sponza.uzproject'; Frames=3; Capture=0; Extra=@() },
    @{ Name='converged'; Project='sample/projects/sponza.uzproject'; Frames=641; Capture=640; Extra=@('--pass-dump','--diag') },
    @{ Name='camera-c'; Project='scripts/sponza-lighting-camera-c.uzproject'; Frames=641; Capture=640;
       Extra=@('--cvar','r.Exposure=0.5','--cvar','r.ColorContrast=0.65','--cvar','r.ColorSaturation=1') }
)
foreach ($run in $runs) {
    $prefix = 'scripts/sponza-loading-fixed-' + $run.Name
    $arguments = @('--project', $run.Project, '--frames', $run.Frames,
        '--screenshot-frame', $run.Capture, '--screenshot', ($prefix + '.png')) + $quality + $run.Extra
    ($arguments -join ' ') | Set-Content -LiteralPath (Join-Path $root ($prefix + '.args'))
    Write-Output "Starting $($run.Name)"
    $process = Start-Process -FilePath $exe -WorkingDirectory $root -ArgumentList $arguments -WindowStyle Hidden -Wait -PassThru `
        -RedirectStandardOutput (Join-Path $root ($prefix + '.log')) `
        -RedirectStandardError (Join-Path $root ($prefix + '.stderr.log'))
    $process.ExitCode | Set-Content -LiteralPath (Join-Path $root ($prefix + '.exit'))
    if ($process.ExitCode -ne 0) { throw "Scene run $($run.Name) failed: $($process.ExitCode)" }
    $log = Get-Content -LiteralPath (Join-Path $root ($prefix + '.log'))
    $ready = @($log | Select-String 'Pre-load complete: assets ready')
    if ($ready.Count -ne 1) { throw "Scene run $($run.Name) did not finish preloading." }
    if (-not (Test-Path -LiteralPath (Join-Path $root ($prefix + '.png')))) { throw 'Screenshot missing.' }
    Write-Output "$($run.Name): exit 0; $($ready[0].Line)"
}
