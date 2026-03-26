@echo off
echo === UzEngine Redeploy ===
echo.

echo Copying uzengine_runtime.dll...
copy /Y "E:\UzEngine\napi\build\Release\uzengine_runtime.dll" "E:\UzEngine\extension\native\" >nul
if errorlevel 1 (
    echo FAILED: uzengine_runtime.dll still locked
    pause
    exit /b 1
)

echo Copying uzengine.node...
copy /Y "E:\UzEngine\napi\build\Release\uzengine.node" "E:\UzEngine\extension\native\" >nul
if errorlevel 1 (
    echo FAILED: uzengine.node still locked
    pause
    exit /b 1
)

echo Copying DLLs...
for %%f in ("E:\UzEngine\napi\build\Release\*.dll") do (
    copy /Y "%%f" "E:\UzEngine\extension\native\" >nul 2>&1
)

echo.
echo Deploy OK. Opening VSCode...
start "" code "E:\UzEngine" --extensionDevelopmentPath="%~dp0extension" --remote-debugging-port=9229
