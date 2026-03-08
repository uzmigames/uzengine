param([switch]$Test)
$ErrorActionPreference = "Continue"
$Root = $PSScriptRoot
$BuildDir = Join-Path $Root "build2"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -property installationPath
$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"

$tempBat = [IO.Path]::GetTempFileName() + ".bat"
$tempOut = [IO.Path]::GetTempFileName()
Set-Content $tempBat "@call `"$vcvars`" x64 > nul 2>&1`r`n@set > `"$tempOut`""
cmd /c $tempBat 2>$null
$envLines = Get-Content $tempOut
Remove-Item $tempBat, $tempOut -Force
foreach ($line in $envLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

$Vcpkg = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path (Join-Path $BuildDir "build.ninja"))) {
    Write-Host "Configuring in $BuildDir..."
    cmake -B $BuildDir -G "Ninja Multi-Config" `
        "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg" `
        "-DVCPKG_TARGET_TRIPLET=x64-windows" `
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON" 2>&1 | ForEach-Object { $_.ToString() }
    if ($LASTEXITCODE -ne 0) { Write-Host "WARNING: Configure had issues"; }
}

Write-Host "Building UzEngineTests (Debug)..."
cmake --build $BuildDir --config Debug --target UzEngineTests --parallel 2>&1 | ForEach-Object { $_.ToString() }
$buildResult = $LASTEXITCODE

if ($buildResult -ne 0) {
    Write-Host "ERROR: Build failed with exit code $buildResult"
    exit 1
}

if ($Test) {
    $testExe = Join-Path $BuildDir "Debug\UzEngineTests.exe"
    if (-not (Test-Path $testExe)) {
        Write-Host "ERROR: Test binary not found at $testExe"
        exit 1
    }
    $lua = Join-Path $BuildDir "vcpkg_installed\x64-windows\bin\lua51.dll"
    if (Test-Path $lua) { Copy-Item $lua (Split-Path $testExe) -Force }
    Write-Host "Running tests..."
    & $testExe --gtest_brief=1
}
