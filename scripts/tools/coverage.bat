@echo off
setlocal

:: ─────────────────────────────────────────────────────────────────────────────
:: UzEngine Code Coverage — OpenCppCoverage (runtime instrumentation)
::
:: Runs the existing test_runtime.exe with coverage collection.
:: No recompilation needed — instruments at runtime via debug API.
::
:: Prerequisites:
::   - OpenCppCoverage installed (winget install OpenCppCoverage.OpenCppCoverage)
::   - Normal build: cd runtime && zig build
::
:: Usage:
::   scripts\coverage.bat              — run tests + generate HTML report
::   scripts\coverage.bat --cobertura  — also export Cobertura XML for CI
::
:: Output:
::   runtime/coverage/html/index.html  — interactive HTML report
::   runtime/coverage/cobertura.xml    — (optional) CI-friendly XML
:: ─────────────────────────────────────────────────────────────────────────────

:: Resolve to absolute path (no ".." — OpenCppCoverage rejects relative paths)
pushd "%~dp0..\runtime"
set "RUNTIME=%CD%"
popd
set "TEST_EXE=%RUNTIME%\build\bin\test_runtime.exe"
set "COV_DIR=%RUNTIME%\coverage"
set "HTML_DIR=%COV_DIR%\html"
set "OCC=C:\Program Files\OpenCppCoverage\OpenCppCoverage.exe"

:: ── Locate OpenCppCoverage ───────────────────────────────────────────────────
if not exist "%OCC%" (
    where OpenCppCoverage >nul 2>&1
    if errorlevel 1 (
        echo ERROR: OpenCppCoverage not found.
        echo Install: winget install OpenCppCoverage.OpenCppCoverage
        exit /b 1
    )
    for /f "delims=" %%i in ('where OpenCppCoverage') do set "OCC=%%i"
)

:: ── Ensure test exe exists ───────────────────────────────────────────────────
if not exist "%TEST_EXE%" (
    echo Test exe not found at %TEST_EXE%
    echo Building...
    cd /d "%RUNTIME%" && zig build
    if errorlevel 1 (echo ERROR: Build failed && exit /b 1)
)

:: ── Clean previous ───────────────────────────────────────────────────────────
if exist "%HTML_DIR%" rmdir /s /q "%HTML_DIR%"

:: ── Run with coverage ────────────────────────────────────────────────────────
echo Running tests with coverage collection...
echo.

set "EXTRA_EXPORT="
if "%1"=="--cobertura" (
    set "EXTRA_EXPORT=--export_type cobertura:%COV_DIR%\cobertura.xml"
)

"%OCC%" ^
    --modules "%TEST_EXE%" ^
    --sources "%RUNTIME%\engine" ^
    --excluded_sources "%RUNTIME%\thirdparty" ^
    --excluded_sources "%RUNTIME%\tests" ^
    --excluded_sources "%RUNTIME%\editor" ^
    --excluded_sources "%RUNTIME%\tools" ^
    --excluded_sources "%RUNTIME%\platform" ^
    --excluded_sources "%RUNTIME%\build" ^
    --export_type html:"%HTML_DIR%" ^
    %EXTRA_EXPORT% ^
    --quiet ^
    -- "%TEST_EXE%"

:: Note: exit code is from test_runtime, not OpenCppCoverage.
:: Some tests may fail in headless (DX12, windowing) — report is still valid.

echo.
echo ══════════════════════════════════════════════════════════
echo  Coverage report: %HTML_DIR%\index.html
echo ══════════════════════════════════════════════════════════

endlocal
