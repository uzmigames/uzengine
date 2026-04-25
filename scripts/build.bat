@echo off
setlocal enabledelayedexpansion

:: Parse --compiler flag (default: zig)
set COMPILER=zig
for %%a in (%*) do (
    echo %%a | findstr /b "--compiler=" >nul && (
        for /f "tokens=2 delims==" %%v in ("%%a") do set COMPILER=%%v
    )
)

echo ============================================
echo  UzEngine Full Build Pipeline
echo  Compiler: %COMPILER%
echo ============================================
echo.

set ROOT=%~dp0..

:: Refresh PATH from registry (picks up winget/installer changes)
:: NOTE: HKLM\...\Environment\Path is REG_EXPAND_SZ; reg query returns the raw
:: value with %SystemRoot% (and friends) unexpanded. The two `call set` lines
:: below force cmd to re-evaluate the variable, which expands those tokens —
:: without that pass, Zig sub-processes can't find powershell.exe etc.
for /f "tokens=2*" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "SYSPATH=%%B"
for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "USRPATH=%%B"
call set "SYSPATH=%SYSPATH%"
call set "USRPATH=%USRPATH%"
set "PATH=%SYSPATH%;%USRPATH%"

:: ---- Pre-flight: kill processes locking build artifacts ----
echo [0/4] Checking for blocking processes...

set NODE_FILE=%ROOT%\extension\native\uzengine.node
if exist "%NODE_FILE%" (
    ren "%NODE_FILE%" uzengine.node.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: uzengine.node is locked by another process.
        echo        Close VSCode or any Extension Host using the addon, then retry.
        exit /b 1
    )
    ren "%NODE_FILE%.tmp" uzengine.node >nul 2>&1
)

set LIB_FILE=%ROOT%\runtime\build\lib\uzengine.lib
if exist "%LIB_FILE%" (
    ren "%LIB_FILE%" uzengine.lib.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: uzengine.lib is locked by another process.
        exit /b 1
    )
    ren "%LIB_FILE%.tmp" uzengine.lib >nul 2>&1
)

set NAPI_NODE=%ROOT%\napi\build\Release\uzengine.node
if exist "%NAPI_NODE%" (
    ren "%NAPI_NODE%" uzengine.node.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: napi/build/Release/uzengine.node is locked.
        exit /b 1
    )
    ren "%NAPI_NODE%.tmp" uzengine.node >nul 2>&1
)

echo       OK — no locks detected
echo.

:: ---- Build Pipeline ----

:: 1. Runtime
echo [1/4] Building runtime (%COMPILER%)...
cd /d "%ROOT%\runtime"

if "%COMPILER%"=="zig" (
    zig build -Doptimize=ReleaseFast
) else if "%COMPILER%"=="msvc" (
    cmake --preset msvc-release >nul 2>&1
    cmake --build build/msvc-release --config Release
) else if "%COMPILER%"=="clang" (
    cmake --preset clang-release >nul 2>&1
    cmake --build build/clang-release --config Release
) else (
    echo ERROR: Unknown compiler: %COMPILER%. Use zig, msvc, or clang.
    exit /b 1
)

if !errorlevel! neq 0 (
    echo FAILED: runtime build
    exit /b 1
)
echo       OK
echo.

:: 2. N-API bridge (cmake-js)
echo [2/4] Building napi (cmake-js + Electron)...
cd /d "%ROOT%\napi"
call npx cmake-js compile --runtime=electron --runtime-version=39.6.0 --CDCMAKE_BUILD_TYPE=Release
if !errorlevel! neq 0 (
    echo FAILED: napi build
    exit /b 1
)
echo       OK
echo.

:: Copy .node to extension
echo       Copying uzengine.node to extension/native/...
if not exist "%ROOT%\extension\native" mkdir "%ROOT%\extension\native"
copy /y "%ROOT%\napi\build\Release\uzengine.node" "%ROOT%\extension\native\uzengine.node" >nul
if !errorlevel! neq 0 (
    echo FAILED: copy .node
    exit /b 1
)
echo       OK
echo.

:: 3. MCP server (TypeScript)
echo [3/4] Building mcp (tsc)...
cd /d "%ROOT%\mcp"
call npx tsc -p .
if !errorlevel! neq 0 (
    echo FAILED: mcp build
    exit /b 1
)
echo       OK
echo.

:: 4. Extension (TypeScript)
echo [4/4] Building extension (tsc)...
cd /d "%ROOT%\extension"
call npx tsc -p .
if !errorlevel! neq 0 (
    echo FAILED: extension build
    exit /b 1
)
echo       OK
echo.

echo ============================================
echo  Build complete! (compiler: %COMPILER%)
echo ============================================
