param([ValidateSet('windows', 'linux')][string]$Platform = 'windows')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_editor_navigation_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$zig = (Get-Command zig).Source
$frame = Get-Content (Join-Path $repo 'runtime/engine/renderer/frame/frame_setup.cpp') -Raw
$render = Get-Content (Join-Path $repo 'runtime/engine/renderer/scene/scene_render_frame.cpp') -Raw
$init = Get-Content (Join-Path $repo 'runtime/engine/renderer/scene/scene_renderer_init.cpp') -Raw
$prepare = [regex]::Match($frame, 'm_input->(?:BeginFrame|PrepareFrame)\(\);').Value
$toggle = [regex]::Match($frame, '(?s)    if \(m_input && m_input->IsKeyJustPressed\(Key::F5\)\).*?(?=    // ── Camera:)').Value
$fly = [regex]::Match($frame, '(?s)    if \(!cameraFromECS && !vfxOverlayWantsMouse.*?\n    \}').Value
$speed = [regex]::Match($init, 'm_cameraCtrl->SetMoveSpeed\([^)]+\);').Value
$end = [regex]::Match($render, 'm_input->EndFrame\(\);').Value
if (!$prepare -or !$toggle -or !$fly -or !$speed -or !$end) { throw 'Production navigation source could not be extracted.' }
[IO.File]::WriteAllText((Join-Path $artifacts 'navigation_frame.generated.h'), "$speed`n$prepare`n$toggle`n$fly`n$end")
$window = Get-Content (Join-Path $repo 'runtime/platform/windows/platform_window.cpp') -Raw
$keyExpression = [regex]::Match($window, 'self->m_input->OnKeyDown\(([^\r\n]+)\);').Groups[1].Value
$keyNormalizer = [regex]::Match($window, '(?ms)^static uint16_t NormalizeWindowKey\(.*?^\}').Value
if (!$keyExpression) { throw 'Production window key dispatch was not found.' }
[IO.File]::WriteAllText((Join-Path $artifacts 'navigation_keys.generated.h'), "$keyNormalizer`nstatic uint16_t ProductionWindowKey(WPARAM wp, LPARAM lp) { return $keyExpression; }")
# Isolate logging only; input, camera, controller, and camera stack compile from production sources.
[IO.File]::WriteAllText((Join-Path $artifacts 'log.h'), "#pragma once`n#define UZ_LOG(...) ((void)0)`n#define UZ_DEFINE_LOG_CATEGORY(...)`n")
$runtime = Join-Path $repo 'runtime'
$gtest = Join-Path $runtime 'thirdparty/googletest/googletest'
$includes = @($artifacts, "$runtime/platform/$Platform", "$runtime/engine/math", "$runtime/engine/renderer/camera", "$runtime/engine/rhi", "$runtime/engine/profiling", "$runtime/engine/assertions", "$runtime/thirdparty/directxmath/Inc", "$gtest/include", $gtest)
$sources = @("$runtime/platform/$Platform/input.cpp", "$runtime/engine/renderer/camera/camera.cpp", "$runtime/engine/renderer/camera/camera_manager.cpp", "$runtime/engine/renderer/camera/fly_camera_controller.cpp", "$runtime/engine/math/vector3.cpp", "$runtime/engine/math/mat4.cpp", "$runtime/engine/math/quaternion.cpp", "$PSScriptRoot/test_editor_navigation.cpp", "$gtest/src/gtest-all.cc", "$gtest/src/gtest_main.cc")
$arguments = @('c++', '-std=c++23', '-O0', '-DUZ_SHIPPING', '-Wno-defaulted-function-deleted', '-Wno-character-conversion')
foreach ($include in $includes) { $arguments += @('-I', $include) }
if ($Platform -eq 'windows') {
    $arguments += @('-luser32', '-lxinput1_4', '-DUZ_TEST_WINDOWS_INPUT')
    $sources += "$runtime/tests/platform/test_input.cpp"
}
$exe = Join-Path $artifacts "test_editor_navigation_$Platform.exe"
& $zig @arguments @sources -o $exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $exe
exit $LASTEXITCODE
