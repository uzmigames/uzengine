@echo off
setlocal enabledelayedexpansion

:: ─────────────────────────────────────────────────────────────────────────────
:: UzEngine Code Coverage — LLVM Source-Based Coverage
::
:: Uses the system LLVM/Clang (not Zig's bundled clang) to compile test_runtime
:: with coverage instrumentation, run tests, and generate an HTML report.
::
:: Prerequisites:
::   - LLVM/Clang installed (F:\LLVM or on PATH): clang++, llvm-profdata, llvm-cov
::   - Normal build passes first: cd runtime && zig build
::     (this ensures generated shader headers and thirdparty libs exist)
::
:: Usage:
::   cd runtime
::   scripts\coverage.bat              — full pipeline: build + run + report
::   scripts\coverage.bat --report     — regenerate HTML from existing .profdata
::   scripts\coverage.bat --summary    — print text summary only
::
:: Output:
::   coverage/html/index.html          — line-by-line HTML report
::   coverage/default.profdata         — merged profile data
:: ─────────────────────────────────────────────────────────────────────────────

set "ROOT=%~dp0..\runtime"
set "COV_DIR=%ROOT%\coverage"
set "PROFRAW=%COV_DIR%\default.profraw"
set "PROFDATA=%COV_DIR%\default.profdata"
set "HTML_DIR=%COV_DIR%\html"
set "TEST_EXE=%COV_DIR%\test_runtime_cov.exe"
set "IGNORE_REGEX=thirdparty|googletest|tests/|tracy|spdlog|entt|mimalloc|stb|nlohmann|glm|directxmath"

:: ── Locate LLVM tools ────────────────────────────────────────────────────────
set "CLANGXX="
if exist "F:\LLVM\bin\clang++.exe" set "CLANGXX=F:\LLVM\bin\clang++.exe"
if "%CLANGXX%"=="" (
    where clang++ >nul 2>&1 && for /f "delims=" %%i in ('where clang++') do set "CLANGXX=%%i"
)
if "%CLANGXX%"=="" (
    echo ERROR: clang++ not found. Install LLVM or add F:\LLVM\bin to PATH.
    exit /b 1
)

where llvm-profdata >nul 2>&1 || (echo ERROR: llvm-profdata not found on PATH && exit /b 1)
where llvm-cov >nul 2>&1 || (echo ERROR: llvm-cov not found on PATH && exit /b 1)

if not exist "%COV_DIR%" mkdir "%COV_DIR%"

:: Skip build+run if --report or --summary
if "%1"=="--report" goto :merge
if "%1"=="--summary" goto :merge

:: ── Step 1: Ensure normal build exists (for generated headers + libs) ────────
echo [1/4] Checking normal build...
cd /d "%ROOT%"
if not exist "build\generated\shaders" (
    echo Running zig build first to generate shader headers...
    call zig build
    if errorlevel 1 (echo ERROR: zig build failed && exit /b 1)
)

:: ── Step 2: Build test_runtime with coverage via system clang++ ──────────────
echo [2/4] Building test_runtime with coverage instrumentation...

:: Clean previous
if exist "%PROFRAW%" del "%PROFRAW%"
if exist "%PROFDATA%" del "%PROFDATA%"

:: Collect all engine .cpp files (not thirdparty, not tools, not demos)
set "SRC_FILES="
for /r "%ROOT%\engine" %%f in (*.cpp) do set "SRC_FILES=!SRC_FILES! %%f"

:: Collect test .cpp files
for /r "%ROOT%\tests" %%f in (*.cpp) do set "SRC_FILES=!SRC_FILES! %%f"

:: GoogleTest
set "SRC_FILES=!SRC_FILES! %ROOT%\thirdparty\googletest\googletest\src\gtest-all.cc"

:: Build with coverage
"%CLANGXX%" -std=c++17 -O0 -g ^
    -fprofile-instr-generate -fcoverage-mapping ^
    -DSPDLOG_COMPILED_LIB -DUZ_PROFILING_ENABLED -DTRACY_ENABLE -DTRACY_ON_DEMAND -DGTEST_HAS_PTHREAD=0 ^
    -I"%ROOT%" ^
    -I"%ROOT%\engine" ^
    -I"%ROOT%\engine\app" ^
    -I"%ROOT%\engine\logging" ^
    -I"%ROOT%\engine\assertions" ^
    -I"%ROOT%\engine\profiling" ^
    -I"%ROOT%\engine\memory" ^
    -I"%ROOT%\engine\config" ^
    -I"%ROOT%\engine\math" ^
    -I"%ROOT%\engine\containers" ^
    -I"%ROOT%\engine\random" ^
    -I"%ROOT%\engine\rhi" ^
    -I"%ROOT%\engine\rhi\optimization" ^
    -I"%ROOT%\engine\rhi\dx12" ^
    -I"%ROOT%\engine\shader" ^
    -I"%ROOT%\engine\paths" ^
    -I"%ROOT%\engine\core" ^
    -I"%ROOT%\engine\renderer" ^
    -I"%ROOT%\engine\renderer\camera" ^
    -I"%ROOT%\engine\renderer\core" ^
    -I"%ROOT%\engine\renderer\core\params" ^
    -I"%ROOT%\engine\renderer\core\gbuffer" ^
    -I"%ROOT%\engine\renderer\core\shader" ^
    -I"%ROOT%\engine\renderer\core\pipeline" ^
    -I"%ROOT%\engine\renderer\core\memory" ^
    -I"%ROOT%\engine\renderer\core\lod" ^
    -I"%ROOT%\engine\renderer\core\lighting" ^
    -I"%ROOT%\engine\renderer\material" ^
    -I"%ROOT%\engine\material" ^
    -I"%ROOT%\engine\material\compiler" ^
    -I"%ROOT%\engine\renderer\memory" ^
    -I"%ROOT%\engine\renderer\mesh" ^
    -I"%ROOT%\engine\renderer\upload" ^
    -I"%ROOT%\engine\renderer\render_graph" ^
    -I"%ROOT%\engine\renderer\shadows" ^
    -I"%ROOT%\engine\renderer\ibl" ^
    -I"%ROOT%\engine\renderer\gi" ^
    -I"%ROOT%\engine\renderer\probes" ^
    -I"%ROOT%\engine\renderer\lighting" ^
    -I"%ROOT%\engine\renderer\atmosphere" ^
    -I"%ROOT%\engine\renderer\fog" ^
    -I"%ROOT%\engine\renderer\scene" ^
    -I"%ROOT%\engine\renderer\frame" ^
    -I"%ROOT%\engine\renderer\passes" ^
    -I"%ROOT%\engine\renderer\passes\geometry" ^
    -I"%ROOT%\engine\renderer\passes\lighting" ^
    -I"%ROOT%\engine\renderer\passes\atmosphere" ^
    -I"%ROOT%\engine\renderer\passes\postprocess" ^
    -I"%ROOT%\engine\renderer\postprocess" ^
    -I"%ROOT%\engine\renderer\passes\debug" ^
    -I"%ROOT%\engine\renderer\passes\ibl" ^
    -I"%ROOT%\engine\asset" ^
    -I"%ROOT%\engine\asset\formats" ^
    -I"%ROOT%\engine\asset\importers" ^
    -I"%ROOT%\engine\job" ^
    -I"%ROOT%\engine\ecs" ^
    -I"%ROOT%\engine\ecs\core" ^
    -I"%ROOT%\engine\ecs\scene" ^
    -I"%ROOT%\engine\ecs\lighting" ^
    -I"%ROOT%\engine\ecs\mesh" ^
    -I"%ROOT%\engine\ecs\environment" ^
    -I"%ROOT%\engine\ecs\metadata" ^
    -I"%ROOT%\engine\ecs\update" ^
    -I"%ROOT%\engine\ecs\loaders" ^
    -I"%ROOT%\engine\scripting" ^
    -I"%ROOT%\engine\scripting\scene" ^
    -I"%ROOT%\engine\scripting\core" ^
    -I"%ROOT%\engine\scripting\renderer" ^
    -I"%ROOT%\engine\event" ^
    -I"%ROOT%\engine\property" ^
    -I"%ROOT%\engine\debug" ^
    -I"%ROOT%\editor\grid" ^
    -I"%ROOT%\editor\font" ^
    -I"%ROOT%\editor\stats" ^
    -I"%ROOT%\editor\screenshot" ^
    -I"%ROOT%\thirdparty\spdlog\include" ^
    -I"%ROOT%\thirdparty\tracy\public" ^
    -I"%ROOT%\thirdparty\mimalloc\include" ^
    -I"%ROOT%\thirdparty\directxmath\Inc" ^
    -I"%ROOT%\thirdparty\glm" ^
    -I"%ROOT%\thirdparty\nlohmann" ^
    -I"%ROOT%\thirdparty\atomic_queue\include" ^
    -I"%ROOT%\thirdparty\spirv-reflect" ^
    -I"%ROOT%\thirdparty\spirv-reflect\include" ^
    -I"%ROOT%\thirdparty\xxhash" ^
    -I"%ROOT%\thirdparty\lz4\lib" ^
    -I"%ROOT%\thirdparty\stb" ^
    -I"%ROOT%\thirdparty\tinyexr" ^
    -I"%ROOT%\thirdparty\libjpeg-turbo" ^
    -I"%ROOT%\thirdparty\basis_universal\transcoder" ^
    -I"%ROOT%\thirdparty\astcenc\Source" ^
    -I"%ROOT%\thirdparty\vulkan_headers\include" ^
    -I"%ROOT%\thirdparty\volk" ^
    -I"%ROOT%\thirdparty\vma\include" ^
    -I"%ROOT%\thirdparty\fastgltf\include" ^
    -I"%ROOT%\thirdparty\fastgltf\deps\simdjson" ^
    -I"%ROOT%\thirdparty\meshoptimizer\src" ^
    -I"%ROOT%\thirdparty\entt\single_include" ^
    -I"%ROOT%\thirdparty\luajit\src" ^
    -I"%ROOT%\thirdparty\renderdoc" ^
    -I"%ROOT%\thirdparty\ufbx" ^
    -I"%ROOT%\thirdparty\etcpak" ^
    -I"%ROOT%\thirdparty\d3d12ma\include" ^
    -I"%ROOT%\thirdparty\directx_headers\include" ^
    -I"%ROOT%\thirdparty\directx_headers\include\directx" ^
    -I"%ROOT%\thirdparty\agility\include" ^
    -I"%ROOT%\thirdparty\dxc\inc" ^
    -I"%ROOT%\platform\windows" ^
    -I"%ROOT%\build\generated\shaders" ^
    -I"%ROOT%\thirdparty\googletest\googletest\include" ^
    -I"%ROOT%\thirdparty\googletest\googletest" ^
    -o "%TEST_EXE%" ^
    !SRC_FILES! ^
    -ld3d12 -ld3d11 -ldxgi -ldxguid -lgdi32 -luser32 -lxinput -lole32 -lshcore -lshell32 -lcomdlg32 -lws2_32 -ldbghelp -lwinmm ^
    "%ROOT%\thirdparty\luajit\src\lua51.lib"

if errorlevel 1 (
    echo ERROR: Coverage build failed.
    echo.
    echo TIP: This script compiles ALL engine+test .cpp files with system clang++.
    echo      If it fails, ensure the normal build works first: zig build
    exit /b 1
)

:: ── Step 3: Run tests ────────────────────────────────────────────────────────
echo [3/4] Running tests...
set "LLVM_PROFILE_FILE=%PROFRAW%"
"%TEST_EXE%"
echo Tests exited with code %ERRORLEVEL%

:merge
:: ── Step 4: Merge + Report ───────────────────────────────────────────────────
if not exist "%PROFRAW%" (
    echo ERROR: No profile data at %PROFRAW%. Run tests first.
    exit /b 1
)

echo [4/4] Generating coverage report...
llvm-profdata merge -sparse "%PROFRAW%" -o "%PROFDATA%"
if errorlevel 1 (echo ERROR: llvm-profdata merge failed && exit /b 1)

if "%1"=="--summary" goto :summary

:: HTML report
if exist "%HTML_DIR%" rmdir /s /q "%HTML_DIR%"
llvm-cov show "%TEST_EXE%" ^
    -instr-profile="%PROFDATA%" ^
    -format=html ^
    -output-dir="%HTML_DIR%" ^
    -ignore-filename-regex="%IGNORE_REGEX%" ^
    -show-line-counts-or-regions

echo.
echo ══════════════════════════════════════════════════════════
echo  Coverage report: %HTML_DIR%\index.html
echo ══════════════════════════════════════════════════════════
echo.

:summary
:: Text summary
llvm-cov report "%TEST_EXE%" ^
    -instr-profile="%PROFDATA%" ^
    -ignore-filename-regex="%IGNORE_REGEX%"

endlocal
