@echo off
echo === Cleaning runtime cache ===
cd /d E:\UzEngine\runtime
if exist .zig-cache rmdir /s /q .zig-cache
if exist build\generated\shaders rmdir /s /q build\generated\shaders

echo === Building runtime ===
zig build
if %errorlevel% neq 0 (echo RUNTIME BUILD FAILED & pause & exit /b 1)

echo === Cleaning demo cache ===
cd /d E:\UzEngine\runtime\demos\pbr_test
if exist .zig-cache rmdir /s /q .zig-cache

echo === Building demo ===
zig build
if %errorlevel% neq 0 (echo DEMO BUILD FAILED & pause & exit /b 1)

echo === Launching demo ===
cd /d E:\UzEngine\runtime\demos\pbr_test\build\bin
start pbr_test.exe
