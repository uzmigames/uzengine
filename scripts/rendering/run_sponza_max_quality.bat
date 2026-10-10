@echo off
setlocal
pushd "%~dp0..\.." || exit /b 1

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
echo Iluminacao indireta filtrada: 32 amostras. LOD favorece geometria mais detalhada.

"runtime\build\bin\uzengine.exe" ^
    --project "sample/projects/sponza.uzproject" ^
    --width 1920 --height 1080 ^
    --adapter 0 ^
    --cvar r.RayTracing=0 ^
    --cvar r.RadianceCache=1 ^
    --cvar r.GI.RadianceCache.RaysPerProbe=64 ^
    --cvar r.GI.RadianceCache.ProbesPerFrame=4096 ^
    --cvar r.GI.Reflections=1 ^
    --cvar r.GI.FinalGather=1 ^
    --cvar r.GI.FinalGather.Samples=32 ^
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
    --cvar r.SSR.NumSteps=64

set "run_exit=%errorlevel%"
popd
if not "%run_exit%"=="0" pause
exit /b %run_exit%
