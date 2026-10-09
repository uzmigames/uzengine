@echo off
rem Requires a GPU and driver with DXR Tier 1.1 (inline ray tracing).
rem DDGI uses DXR; reflections remain SDF/SSR and ambient occlusion remains GTAO.
setlocal
pushd "%~dp0.." || exit /b 1

if not exist "runtime\build\bin\uzengine.exe" (
    echo Executavel nao encontrado. Compile o runtime com zig build primeiro.
    pause
    popd
    exit /b 1
)

echo F5: camera livre. WASD: mover. Botao direito + mouse: olhar.
echo Espaco/Ctrl: subir/descer. Shift: acelerar. F5: voltar a camera da cena.
echo Aguarde a geracao dos campos de distancia. O carregamento pode levar alguns minutos.
echo A cena sera exibida depois da carga completa e da preparacao da exposicao.
echo Teste DXR/DDGI: requer placa e driver com DXR 1.1.
echo Reflexos usam SDF/SSR e oclusao ambiente usa GTAO.
echo DXR 1.0 sozinho nao suporta o shader DDGI deste preset.

"runtime\build\bin\uzengine.exe" ^
    --project "sample/projects/sponza.uzproject" ^
    --width 1920 --height 1080 ^
    --adapter 0 ^
    --cvar r.RayTracing=1 ^
    --cvar r.RayTracing.GI=1 ^
    --cvar r.RadianceCache=1 ^
    --cvar r.GI.RadianceCache.RaysPerProbe=64 ^
    --cvar r.GI.RadianceCache.ProbesPerFrame=4096 ^
    --cvar r.GI.Reflections=1 ^
    --cvar r.GI.FinalGather=0 ^
    --cvar r.LOD.Bias=0.1 ^
    --cvar r.GI.GDF.Resolution=256 ^
    --cvar r.GI.MeshSdf.Resolution=128 ^
    --cvar r.GI.Reflections.Samples=16 ^
    --cvar r.GI.Reflections.Steps=256 ^
    --cvar r.TAA=1 ^
    --cvar r.TAA.JitterSequenceLength=16 ^
    --cvar r.GTAO=1 ^
    --cvar r.GTAO.Quality=3 ^
    --cvar r.GTAO.NumAngles=16 ^
    --cvar r.GTAO.NumTaps=16 ^
    --cvar r.SSR=1 ^
    --cvar r.SSR.NumSteps=64 ^
    --log-level info

set "run_exit=%errorlevel%"
popd
if not "%run_exit%"=="0" pause
exit /b %run_exit%
