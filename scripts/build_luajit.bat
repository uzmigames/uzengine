@echo off
REM Build LuaJIT 2.1 with MSVC (x64).
REM Must be called from a Visual Studio Developer Command Prompt,
REM or vcvarsall.bat must have been called beforehand.
REM
REM Usage: scripts\build_luajit.bat
REM   (run from repo root)

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%.."
set "REPO_ROOT=%CD%"
popd
set "LUAJIT_SRC=%REPO_ROOT%\runtime\thirdparty\luajit\src"

if not exist "%LUAJIT_SRC%\msvcbuild.bat" (
    echo [ERROR] LuaJIT source not found at %LUAJIT_SRC%
    echo         Run scripts\setup.ps1 first to clone LuaJIT.
    exit /b 1
)

if exist "%LUAJIT_SRC%\lua51.lib" (
    echo [luajit] Already built - lua51.lib exists. Skipping.
    exit /b 0
)

REM Detect MSVC environment
where cl >nul 2>&1
if !errorlevel! neq 0 (
    echo [luajit] MSVC not in PATH, searching for vcvarsall.bat...
    set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        REM -products * is REQUIRED: without it vswhere ignores Build Tools
        REM installs (only Community/Professional/Enterprise are matched).
        for /f "delims=" %%i in ('"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath') do set "VS_PATH=%%i"
    )
    if defined VS_PATH (
        call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64
    ) else (
        echo [ERROR] Cannot find Visual Studio. Install VS 2022 with C++ workload.
        exit /b 1
    )
)

echo [luajit] Building with MSVC x64...
pushd "%LUAJIT_SRC%"
if !errorlevel! neq 0 (
    echo [ERROR] Cannot pushd to %LUAJIT_SRC%
    exit /b 1
)
echo [luajit] Working dir: %CD%
set "PATH=%CD%;%PATH%"
call "%LUAJIT_SRC%\msvcbuild.bat"
set BUILD_RESULT=!errorlevel!
popd

if !BUILD_RESULT! neq 0 (
    echo [ERROR] LuaJIT build failed.
    exit /b 1
)

if exist "%LUAJIT_SRC%\lua51.lib" (
    echo [luajit] Build successful - lua51.lib + lua51.dll ready.
) else (
    echo [ERROR] Build completed but lua51.lib not found.
    exit /b 1
)

endlocal
