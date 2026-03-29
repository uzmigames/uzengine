@echo off
REM UzEngine N-API Build Script
REM Requires Visual Studio Build Tools 2022 + lld-link (from LLVM or Zig)
REM Uses NMake generator (respects CMAKE_LINKER = lld-link)

REM Ensure CWD is the napi directory (build.bat may be invoked from elsewhere)
cd /d "%~dp0"

call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if exist build rmdir /s /q build

echo [UzEngine] Building N-API addon (MSVC cl.exe + lld-link)...
REM Force Windows SDK rc.exe — npm's "rc" package shadows it in PATH
npx cmake-js compile -d . --runtime=electron --runtime-version=39.8.0 -G "NMake Makefiles" --CDCMAKE_BUILD_TYPE=Release --CDCMAKE_RC_COMPILER="C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe"

if %ERRORLEVEL% NEQ 0 (
    echo [UzEngine] Build FAILED
    exit /b 1
)

echo [UzEngine] Build SUCCESS: build/uzengine.node
echo [UzEngine] Copy to extension: copy build\uzengine.node ..\extension\native\

if not exist "..\extension\native" mkdir "..\extension\native"
copy /Y build\uzengine.node ..\extension\native\uzengine.node >nul
echo [UzEngine] Copied to extension/native/uzengine.node
