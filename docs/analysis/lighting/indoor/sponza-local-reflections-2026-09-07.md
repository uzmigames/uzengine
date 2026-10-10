# Sponza: reflexos locais e estabilidade da iluminação

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


Atualização posterior: `sponza-loading-exposure-2026-09-07.md` documenta a correção da apresentação durante a carga, a inicialização da exposição e as novas capturas. Os resultados abaixo registram a etapa anterior.

## Resultado implementado

Validação final: as três capturas `sponza-shadow-fixed-*` em 1080p terminaram com código 0 e stderr vazio. A comparação parada preserva as sombras entre os quadros 120 e 640. Os 1.616 pixels quase brancos do ornamento causados pelo bias de sombra caíram para zero na mesma região. Isso corrige os defeitos identificados, sem afirmar paridade com Lumen.

O preset `scripts/run_sponza_max_quality.bat` seleciona 1920x1080, campos de distância por malha de 128³, campo global de 256³, 64 raios por probe, atualização dos 4096 probes por quadro e 16 amostras de reflexão com até 256 passos. TAA, GTAO e SSR permanecem ligados. O caminho é software sobre DX12, testado na GTX 1660 SUPER; não depende de DXR.

Os reflexos agora consultam a geometria fora da tela. Um raio que encontra uma superfície recebe a iluminação solar sombreada e a iluminação difusa dessa superfície. Somente raios que escapam recebem o céu; esgotar o orçamento de passos não equivale a enxergar o céu. O SSR é combinado antes da BRDF, sem a antiga soma adicional após TAA.

## Causas confirmadas

- O filtro SSR aplicava uma segunda descompressão depois da média já normalizada: uma entrada constante RGB=1 podia virar aproximadamente 1000. A confiança também usava pesos inconsistentes. A regressão passou de 0/21 para 21/21.
- A aproximação da BRDF de ambiente usava componentes e conversões diferentes da fórmula publicada. As duas integrações agora usam o mesmo helper; 200/200 casos passam, com erro máximo de 1,192e-7.
- Os campos grosseiros fechavam aberturas reais. Um teste na GPU de uma janela de 100x200 cm falha com malha32/GDF128 e malha128/GDF128, mas preserva a passagem com malha128/GDF256.
- Probes enterrados recebiam e propagavam iluminação inválida. A classificação grava validade nos coeficientes e todos os consumidores descartam probes inválidos, renormalizando os demais pesos.
- Geometria e materiais que terminavam de carregar não invalidavam adequadamente o histórico de iluminação. A revisão do campo global agora provoca a reinicialização do cache.
- Os parâmetros do projeto sobrescreviam opções explícitas da linha de comando. A ordem foi corrigida e verificada em execução.
- O pass dump registrava um campo global maior como 64x64, causando uma cópia DX12 inválida. O registro agora usa as dimensões reais.
- A segunda câmera revelou outra sobreposição: o passe EnvMIS somava luz do céu sem oclusão depois da reflexão local. O registro da iluminação agora escolhe apenas um desses caminhos e evita o dispatch MIS sem consumidor. A regressão da expressão de produção falha em 1/8 estados antes e passa em 8/8 depois; `scripts/test_reflection_selection.ps1` reproduz o teste.
- Havia dois registros de `r.SSR.NumSteps` com limites diferentes. Ambos agora aceitam 64; a ordem de inicialização não limita silenciosamente o preset a 32.
- O controle com SSR desligado localizou o contorno claro restante do ornamento no passe de luz direta e no canal vermelho da máscara de sombra. O normal offset de CSM usava tangente, ampliando o deslocamento até dez vezes, em vez do seno publicado; o bias de profundidade também tinha um piso arbitrário. O shader separa agora seno/tangente, limita a inclinação a 2 e preserva o bias calculado para a cascata. A regressão passou de 1/10 para 10/10 e o shader deferred completo compila; `scripts/test_shadow_bias_artifacts/evidence.md` documenta a derivação e seus limites.

As capturas antigas `sponza-stationary-fixed-60.png` e `sponza-stationary-fixed-640.png`, com exposição fixa, demonstram que a mudança parada não era apenas adaptação de exposição. O readback `sponza-transport-probes.log` mostrou probes enterrados e quase nenhum transporte de céu/sol no interior. A captura nova `sponza-high-quality-preview.png` terminou com código 0 e diagnóstico sem falhas; os probes enterrados têm validade nula e os probes livres recebem raios solares e de céu.

## Evidência automatizada e manual

| Verificação | Resultado e artefato |
|---|---|
| BRDF de ambiente | 200/200; `scripts/test_environment_brdf_artifacts/evidence.md` |
| Filtro SSR | 21/21; `scripts/test_ssr_resolve_filter_artifacts/evidence.md` |
| BVH de distância | 3082 consultas equivalentes; `scripts/test_mesh_sdf_bvh_artifacts/evidence.md` |
| BVH na GPU | 2.097.152 voxels idênticos ao cálculo sem aceleração; `scripts/test_sdf_gpu_artifacts/window_bvh_comparison.json` |
| Janela e probes inválidos | Escape correto e interpolação renormalizada; `scripts/test_sdf_gpu_artifacts/evidence.md` |
| Entrada/câmera | 68/68 Windows e 7/7 com implementação Linux compilada no Windows; `scripts/test_editor_navigation_artifacts/quick-tap-evidence.md` |
| Seleção de reflexos e bias de sombra | 8/8 e 10/10, respectivamente; `scripts/test_reflection_selection_artifacts/after.log` e `scripts/test_shadow_bias_artifacts/after.log` |
| Janela nativa | F5 cria/restaura câmera sem mudar a orientação; Alt+F4 encerra com código 0; `scripts/sponza-navigation-fixed-live.log` |
| Prévia real da Sponza | 960x540, quadro 240, diagnóstico sem falhas, código 0; `scripts/sponza-high-quality-preview.log` |
| Câmera parada em 1080p | Quadros 120 e 640, ambos código 0; `scripts/sponza-max-early.png` e `scripts/sponza-max-converged.png` |
| Segunda vista em 1080p, antes da exclusão do MIS | Quadro 640, código 0; `scripts/sponza-max-camera-c.png`; revelou os pontos claros e motivou a correção adicional |
| Versão com seleção exclusiva de reflexos | `scripts/sponza-final-camera-c.png`, `scripts/sponza-final-converged.png`, `scripts/sponza-final-early.png`; três execuções em 1080p, código 0 e stderr vazio |
| Versão final, com bias corrigido | `scripts/sponza-shadow-fixed-camera-c.png`, `scripts/sponza-shadow-fixed-converged.png`, `scripts/sponza-shadow-fixed-early.png`; três execuções em 1080p, código 0 e stderr vazio |

A inspeção das duas capturas em 1080p preserva o desenho das sombras. A diferença RGB média foi 1,09/255 na amostragem de um pixel a cada quatro em cada eixo. Nas regiões medidas integralmente, a luminância variou -6,01% no piso sombreado, -1,68% na coluna esquerda, -0,02% no piso ao sol e -9,20% na parede esquerda. Ainda há convergência e variação temporal; não houve o colapso para quase preto das capturas antigas. Essas medidas descrevem pixels após tonemapping, não energia radiométrica ou PSNR. Coordenadas, passos e valores estão em `scripts/sponza-max-stationary-metrics.json`.

A versão intermediária com seleção exclusiva de reflexos removeu os brilhos artificiais em portas, arcos e colunas. A comparação temporal repetida manteve as sombras: diferença RGB média de 1,04/255, luminância média -0,27%, piso sombreado -5,98%, coluna -1,63%, piso ao sol -0,01% e parede -9,14%. Evidência: `scripts/sponza-final-stationary-metrics.json`; reprodução: `powershell -File scripts/compare_sponza_stationary.ps1 -Prefix sponza-final`. O ornamento dessa versão foi separado para o diagnóstico com SSR desligado que confirmou o defeito de bias.

Na versão final com bias corrigido, a diferença RGB média é 1,05/255 e a luminância média varia -0,26%. As regiões indiretas variam até -9,13% durante a convergência, sem desaparecerem as sombras. `scripts/sponza-shadow-fixed-stationary-metrics.json` contém os valores; `powershell -File scripts/compare_sponza_stationary.ps1` reproduz a medição. A região do ornamento, x=1210,y=380,w=240,h=435, tinha 1.616 pixels com os três canais RGB>=240; após a correção tem zero. A região foi inspecionada e o brilho indevido desapareceu sem mudar materiais/exposição. Evidência: `scripts/sponza-shadow-bias-image-metrics.json`.

Os testes de câmera exercitam deslocamento mantido. A ferramenta de interface injeta toques curtos; a validação nativa verificou a troca/restauração da câmera e o fechamento, não uma caminhada contínua.

O executável e a biblioteca da engine foram recompilados. O build completo continua falhando no alvo `test_runtime` com 23 erros preexistentes de GoogleTest/tipos de compilador/testes Metal. Evidência mais recente: `scripts/sponza-lighting-build-shadow-bias.log`. Não foram removidos testes nem relaxadas asserções para mascarar essas falhas. Clangd e a camada de debug DX12 estão indisponíveis nesta máquina.

## Controles

Execute o BAT a partir de qualquer pasta. F5 ativa a câmera livre. WASD move, botão direito com mouse olha, Espaço sobe, Ctrl desce e Shift triplica a velocidade. F5 restaura a câmera da cena. No modo `--editor`, mantenha o botão direito para navegar. A geração inicial dos campos pode levar minutos; isso não é uma medida da taxa de quadros depois do carregamento.

## Limites em relação ao Lumen

Esta implementação não tem paridade com o Lumen. O material de um encontro fora da tela ainda usa refletância média da malha, sem texturas UV por ponto; reflexos de detalhes pequenos e emissivos continuam limitados. Faltam um cache de superfícies com material/iluminação por texel, visibilidade mais precisa entre probes e pixels, reposicionamento de probes, transporte emissivo completo e uma solução de reconstrução temporal com qualidade e custo comparáveis. A resolução escolhida preserva a janela testada, mas não garante todas as frestas da cena.

As imagens finais ainda têm altas luzes saturadas, sobretudo nas janelas e superfícies diretamente ao sol. A validação não demonstra ausência de todos os artefatos nem qualidade equivalente a um renderer de referência. Os avisos de alvos SSR/HiZ zerados no diagnóstico amostrado também permanecem; o teste de transporte de reflexos fora da tela é independente desses avisos.

Os hashes dos fontes, executável, preset e capturas finais estão em `scripts/sponza-completed-quality-sha256.txt`. Os manifestos anteriores identificam etapas intermediárias e não devem ser usados como identidade do executável final.

O Lumen combina representações de cena, cache de superfícies e traçado híbrido; não é um único shader ou ajuste de intensidade. Referências primárias: [Lumen Technical Details, Epic](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine), [Wright et al., SIGGRAPH 2022](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf), [Karis 2014](https://www.unrealengine.com/blog/physically-based-shading-on-mobile?lang=en-US), [Stachowiak 2015](https://h3.gd/stochastic-ssr/), [Majercik et al. 2021](https://arxiv.org/html/2009.10796), [PBRT 4, BVH](https://pbr-book.org/4ed/Primitives_and_Intersection_Acceleration/Bounding_Volume_Hierarchies).

A referência Falcor configurada em E:/Falcor não existe nesta máquina. Portanto não há medição de PSNR contra Falcor nem alegação de equivalência visual com Unreal.

Referências adicionais de sombra: [Castano 2013, Shadow Mapping Summary](https://www.ludicon.com/castano/blog/articles/shadow-mapping-summary-part-1/) e [Pettineo 2013, A Sampling of Shadow Techniques](https://mynameismjp.wordpress.com/2013/09/10/shadow-maps/). A URL original do pôster GDC 2011 de Holbert estava indisponível; foram consultadas diretamente essas fontes dos implementadores.
