@echo off
:: =============================================================================
:: start-server.bat -- Build (if needed) and launch UzEngine streaming server
:: =============================================================================

setlocal EnableDelayedExpansion

set "ROOT=%~dp0.."
set "BUILD_DIR=%ROOT%\build"
set "EXE_RELEASE=%BUILD_DIR%\Release\render.exe"
set "EXE_DEBUG=%BUILD_DIR%\Debug\render.exe"
set "CONFIG=Release"

:: Allow overriding config: start-server.bat Debug
if /I "%~1"=="Debug" set "CONFIG=Debug"
if /I "%~1"=="Release" set "CONFIG=Release"

set "EXE=%BUILD_DIR%\%CONFIG%\render.exe"

echo [UzEngine] Config: %CONFIG%

:: Check if the executable exists, build if not
if not exist "%EXE%" (
    echo [UzEngine] Executable not found. Building...

    if not exist "%BUILD_DIR%" (
        echo [UzEngine] Configuring CMake...
        cmake -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -S "%ROOT%"
        if errorlevel 1 (
            echo [ERROR] CMake configuration failed.
            pause
            exit /b 1
        )
    )

    echo [UzEngine] Building %CONFIG%...
    cmake --build "%BUILD_DIR%" --config %CONFIG%
    if errorlevel 1 (
        echo [ERROR] Build failed.
        pause
        exit /b 1
    )
)

if not exist "%EXE%" (
    echo [ERROR] Executable still not found after build: %EXE%
    pause
    exit /b 1
)

echo [UzEngine] Starting: %EXE%
echo [UzEngine] WebSocket server will listen on ws://localhost:9002
echo [UzEngine] Open VSCode and run "UzEngine: Open Stream Viewer"
echo.

"%EXE%"
