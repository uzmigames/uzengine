$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$artifacts = Join-Path $PSScriptRoot 'test_quaternion_artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$zigCommand = Get-Command zig -ErrorAction SilentlyContinue
$zig = if ($zigCommand) { $zigCommand.Source } else {
    Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Packages/zig.zig_*/zig-*/zig.exe') | Select-Object -First 1 -ExpandProperty FullName
}
if (!$zig) { throw 'Zig was not found on PATH or in the WinGet installation.' }
$math = Join-Path $repo 'runtime/engine/math'
$gtest = Join-Path $repo 'runtime/thirdparty/googletest/googletest'
$exe = Join-Path $artifacts 'test_quaternion.exe'
& $zig c++ -std=c++23 -O2 '-I' $math '-I' (Join-Path $repo 'runtime/thirdparty/directxmath/Inc') '-I' (Join-Path $gtest 'include') '-I' $gtest (Join-Path $math 'quaternion.cpp') (Join-Path $math 'mat4.cpp') (Join-Path $math 'vector3.cpp') (Join-Path $repo 'runtime/tests/engine/math/test_quat.cpp') (Join-Path $repo 'runtime/tests/engine/math/test_quat_methods.cpp') (Join-Path $gtest 'src/gtest-all.cc') (Join-Path $gtest 'src/gtest_main.cc') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Quaternion regression compilation failed.' }
& $exe
exit $LASTEXITCODE
