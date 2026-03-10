@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  UzEngine Full Build Pipeline
echo ============================================
echo.

set ROOT=%~dp0..

:: Refresh PATH from registry (picks up winget/installer changes)
for /f "tokens=2*" %%A in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "SYSPATH=%%B"
for /f "tokens=2*" %%A in ('reg query "HKCU\Environment" /v Path 2^>nul') do set "USRPATH=%%B"
set "PATH=%SYSPATH%;%USRPATH%"

:: ---- Pre-flight: kill processes locking build artifacts ----
echo [0/4] Checking for blocking processes...

:: Check if uzengine.node is locked (loaded by VSCode/Electron)
set NODE_FILE=%ROOT%\extension\native\uzengine.node
if exist "%NODE_FILE%" (
    for /f "tokens=1" %%p in ('handle.exe -accepteula -nobanner "%NODE_FILE%" 2^>nul ^| findstr /i pid') do (
        echo       WARNING: uzengine.node is locked by PID %%p
    )
    :: Try rename as lock test
    ren "%NODE_FILE%" uzengine.node.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: uzengine.node is locked by another process.
        echo        Close VSCode or any Extension Host using the addon, then retry.
        echo        Tip: In VSCode, run "Developer: Reload Window" first.
        echo.
        exit /b 1
    )
    ren "%NODE_FILE%.tmp" uzengine.node >nul 2>&1
)

:: Check if runtime .lib is locked
set LIB_FILE=%ROOT%\runtime\zig-out\lib\uzengine_runtime.lib
if exist "%LIB_FILE%" (
    ren "%LIB_FILE%" uzengine_runtime.lib.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: uzengine_runtime.lib is locked by another process.
        echo        Close any linker or process using the library, then retry.
        echo.
        exit /b 1
    )
    ren "%LIB_FILE%.tmp" uzengine_runtime.lib >nul 2>&1
)

:: Check if napi build output is locked
set NAPI_NODE=%ROOT%\napi\build\Release\uzengine.node
if exist "%NAPI_NODE%" (
    ren "%NAPI_NODE%" uzengine.node.tmp >nul 2>&1
    if !errorlevel! neq 0 (
        echo.
        echo ERROR: napi/build/Release/uzengine.node is locked.
        echo        Close any process using the addon, then retry.
        echo.
        exit /b 1
    )
    ren "%NAPI_NODE%.tmp" uzengine.node >nul 2>&1
)

echo       OK — no locks detected
echo.

:: ---- Build Pipeline ----

:: 1. Runtime (Zig)
echo [1/4] Building runtime (Zig)...
cd /d "%ROOT%\runtime"
zig build -Doptimize=ReleaseFast
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
echo  Build complete!
echo ============================================
