# Sponza: carga incompleta e exposição na abertura

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


## Resultado

O runtime agora prepara a geometria, os campos de distância e as texturas antes de apresentar a cena. A exposição inicial é calculada com HDR da cena completa. O preset da Sponza usa exposição 0.5, contraste 0.65 e saturação 1.0, preservando os parâmetros de transporte, sol, GI e reflexos da correção anterior.

Três execuções reais em 1920x1080 terminaram com código 0, stderr vazio e sem registros de nível Error: abertura, câmera A prolongada e câmera C prolongada. As três imagens foram inspecionadas. O executável testado tem tamanho 72261120 bytes e foi recompilado em 2026-09-07 03:34:26, horário local. A identidade completa está em `scripts/sponza-loading-exposure-sha256.txt`.

## Causas e mudanças

1. O orçamento padrão carregava quatro malhas por quadro. `IsSceneFullyLoaded()` verificava filas de upload, mas as malhas adiadas pelo orçamento permaneciam apenas nos componentes ECS. No log anterior, a aplicação anunciou carga concluída às 02:15:04.767, embora o teto ainda carregasse às 02:16:19.623. A apresentação intermediária recebia luz através de geometria ausente. O renderer agora conta adiamentos de malha base/LOD e inclui slots assíncronos, uploads de malha e bakes pendentes na prontidão.
2. O preload antigo desenhava/apresentava um único quadro e drenava somente as texturas já solicitadas pelas primeiras malhas. Agora ele processa lotes e eventos da janela até concluir a carga, sem apresentar esses quadros. No Windows, o título indica `Loading scene...`. A captura também deixou de ignorar a prontidão após um timeout de 120 quadros.
3. O primeiro teste da nova carga revelou exposição adaptada ao HDR incompleto. A evidência `sponza-loading-fixed-startup-before-exposure.png` mostra a abertura indevidamente escura. A sequência final renderiza HDR completo, zera o histórico de exposição com transições CopyDst/UAV e prepara o resultado da GPU antes da apresentação. São duas dependências reais: HDR do quadro anterior e readback da exposição. O shader existente já inicializa exposição diretamente no alvo quando o histórico é zero; nenhuma fórmula de shader foi modificada nesta etapa.
4. A curva anterior deixava extensas superfícies sem detalhe. A calibração usa apenas controles de exibição da cena; não adiciona energia indireta ou luz ambiente artificial. Os padrões observados também aparecem no GBuffer de albedo, portanto não podem ser classificados integralmente como sombras. Isso não constitui uma auditoria de correspondência UV/material por texel.

## Evidência

| Verificação | Resultado |
|---|---|
| Prontidão com trabalho pendente | 10/14 antes, 14/14 depois; `scripts/test_scene_readiness_artifacts/{before,after}.log` |
| Ordem/barreiras/limites do reset | 4/4; `scripts/test_exposure_reset_artifacts/reset.log` |
| Abertura | 37 quadros de preparação; 3 quadros após carga; `scripts/sponza-loading-fixed-startup.*` |
| Câmera A prolongada | 641 quadros após carga; captura solicitada no índice 640; `scripts/sponza-loading-fixed-converged.*` |
| Câmera C prolongada | 641 quadros após carga; captura solicitada no índice 640; `scripts/sponza-loading-fixed-camera-c.*` |
| Diagnóstico com pass dump |`hasFailure=false`, 26 alvos, frame 678; `scripts/sponza-loading-fixed-converged.log` |

Trechos reais da execução:

```text
[2026-09-07 03:36:24.302] [Engine] [info] Pre-load complete: assets ready and exposure seeded after 37 frames; no partial scene presented
[2026-09-07 03:36:27.833] [Screenshot] [info] Screenshot saved: path='scripts/sponza-loading-fixed-startup.png' (1920x1080)
[2026-09-07 03:43:45.058] [Engine] [info] UzEngine Runtime exiting with code 0
```

A última linha pertence à câmera C. Cada execução tem seu próprio `.args`, `.log`, `.stderr.log` e `.exit`. O primeiro runner não recuperou `Process.ExitCode` após `WaitForExit`; seu arquivo `.exit` ficou vazio, embora o log do runtime registrasse encerramento normal. Esse ensaio foi preservado como `*-startup-before-exposure.*`. O runner final usa `Start-Process -Wait -PassThru`; os três arquivos finais `.exit` contêm 0.

### Exibição e convergência

Na comparação final 1920x1080, com amostragem a cada 2 pixels nos dois eixos, a fração de amostras com todos os canais RGB>=250 caiu de 9.3071% para 0.9161%. Amostras com todos os canais RGB<=5 caíram de 25.7907% para 11.2438%. São medidas de exibição após tonemapping, não PSNR nem energia radiométrica. Os dados estão em `scripts/sponza-loading-fixed-display-metrics.json`. O controle anterior em 960x540 está em `scripts/sponza-display-metrics.json`.

Da abertura até a captura prolongada, o piso ao sol varia +0.8619% em luminância e o piso sombreado -2.8001%, preservando o desenho das sombras. A GI ainda converge: a parede esquerda passa de 41.6307 para 61.2128/255 e a coluna escura de 2.5903 para 8.6898/255. A imagem inteira tem diferença RGB média 8.5617/255 na amostragem de passo 4 e luminância média +14.2817%. Portanto, não se afirma que a primeira imagem já contenha GI completamente convergida. As coordenadas e todos os valores estão em `scripts/sponza-loading-fixed-stationary-metrics.json`.

## Reprodução

```powershell
scripts\run_sponza_max_quality.bat
powershell -File scripts/test_scene_readiness.ps1
powershell -File scripts/test_exposure_reset.ps1
powershell -File scripts/verify_sponza_loading.ps1
powershell -File scripts/compare_sponza_stationary.ps1 -Prefix sponza-loading-fixed
```

F5 ativa/restaura a câmera livre; WASD move; botão direito com mouse olha; Espaço/Ctrl sobe/desce; Shift acelera. A preparação dos campos pode levar minutos. A validação automatizada usa janelas iniciadas com `WindowStyle Hidden` e capturas nativas do renderer. Os controles de entrada não foram modificados nesta etapa; sua evidência anterior permanece no relatório de reflexos locais.

## Limites

O executável e a biblioteca recompilaram; o build completo continua com os mesmos 23 erros preexistentes em `test_runtime` (GoogleTest char16/char32, tipos incompletos ShaderCompiler/FXC e testes Metal não guardados). Log: `scripts/sponza-lighting-build-ready-exposure.log`. Não houve remoção ou relaxamento de testes. Clangd e a camada de debug DX12 estão indisponíveis.

O diagnóstico ainda sinaliza `WARN_ZERO` para SSR trace/resolve/temporal e HiZ na amostra inspecionada. Ausência de falha nesse diagnóstico não prova a correção de todos os efeitos. Restam saturação nas janelas, convergência da iluminação indireta e limitações de reflexos/material fora da tela. Esta etapa melhora a abertura e a exibição; não implementa um novo cache de superfícies.

Não há equivalência demonstrada com Lumen. Continuam faltando material/iluminação por texel de superfície, transporte emissivo completo, visibilidade fina e reconstrução temporal comparável. A instalação de referência `E:/Falcor` não existe nesta máquina, portanto não foi medido PSNR. A arquitetura e as referências primárias usadas nas correções anteriores permanecem em `sponza-local-reflections-2026-09-07.md`.

Artefatos de investigação e alterações anteriores foram preservados conforme as instruções do projeto. Os manifestos anteriores identificam etapas históricas, não este novo executável.
