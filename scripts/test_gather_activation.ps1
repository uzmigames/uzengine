param([string]$Label='current')
$ErrorActionPreference='Stop'
if($Label -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid label.'}
$root=Split-Path $PSScriptRoot -Parent
$artifacts=Join-Path $PSScriptRoot 'test_gather_activation_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$setup=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/frame/frame_setup.cpp')
$constants=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/frame/frame_constants.cpp')
$renderer=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/gi/ssgi_renderer.cpp')
$lighting=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/frame/frame_lighting.cpp')
$init=Get-Content -Raw (Join-Path $root 'runtime/engine/renderer/scene/scene_renderer_init.cpp')
$apply=[regex]::Match($init,'m_passInspector.SetPassEnabled\("ssgi",[^;]+;').Value + "`n" + [regex]::Match($init,'m_passInspector.SetPassEnabled\("sdf_gi",[^;]+;').Value
if($setup.IndexOf('ApplyCVars();') -gt $setup.IndexOf('PopulatePerFrameConstants(ctx);')){throw 'Revisit startup sequence: ApplyCVars moved after population.'}
# Temporary diagnostic logging is omitted; production decisions remain verbatim.
$constants=[regex]::Replace($constants,'    // UZ_GATHER_DIAGNOSTIC_BEGIN[\s\S]*?    // UZ_GATHER_DIAGNOSTIC_END','')
$renderer=[regex]::Replace($renderer,'    // UZ_GATHER_DIAGNOSTIC_BEGIN[\s\S]*?    // UZ_GATHER_DIAGNOSTIC_END','')
$renderer=[regex]::Replace($renderer,'(?m)^\s*logPrepareGate\([\s\S]*?;','')
$start=$setup.IndexOf('    PopulatePerFrameConstants(ctx);');$end=$setup.IndexOf('    // ── Import render graph resources',$start)
$setupBody="ApplyCVars();"+"`n"+$setup.Substring($start,$end-$start)
$start=$constants.IndexOf('    const bool worldGather =');$end=$constants.IndexOf('    // ── Brixelizer GI',$start)
$binding=$constants.Substring($start,$end-$start)
$start=$renderer.IndexOf('bool SSGIRenderer::PrepareFrame(');$end=$renderer.IndexOf('void SSGIRenderer::Render(',$start)
$prepare=$renderer.Substring($start,$end-$start)
$diagnostic=[regex]::Match($lighting,'const bool ssgiActive =[^;]+;').Value
if(-not $diagnostic){throw 'Diagnostic activation expression missing.'}
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'test_gather_activation.cpp')
$code=$template.Replace('PRODUCTION_SETUP',$setupBody).Replace('PRODUCTION_BINDING',$binding).Replace('PRODUCTION_PREPARE',$prepare).Replace('PRODUCTION_DIAGNOSTIC',$diagnostic).Replace('PRODUCTION_APPLY_CVARS',$apply)
$cpp=Join-Path $artifacts "$Label.cpp";$exe=Join-Path $artifacts "$Label.exe"
[IO.File]::WriteAllText($cpp,$code)
& zig c++ -std=c++23 -O0 $cpp -o $exe
if($LASTEXITCODE -ne 0){throw 'Activation lifecycle harness compile failed.'}
& $exe | Tee-Object (Join-Path $artifacts "$Label.log")
exit $LASTEXITCODE
