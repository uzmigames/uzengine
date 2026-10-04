# Iluminação da Sponza: transporte por superfície e correção dos materiais

> **Origin**: `test/windows-sponza-parity @ 25cd0c5f` (`E:\SponzaTest`), ported verbatim on 2026-09-08 by `phase0_sponza-parity-06-qa-harness-validation`.
> Paths such as `scripts/*_artifacts/`, `scripts/sponza-*.png|log|json|sha256.txt` refer to evidence that is **not in Git**; the executable identities quoted are SponzaTest builds, not this repository's.


Data: 2026-09-07. Escopo: melhorar a luz com a cena completamente carregada, mantendo a exposição e a direção do sol já ajustadas. Não representa paridade com Lumen.

## Causas confirmadas e correções

1. A interpolação direta de irradiância entre probes separados por aproximadamente 267 cm não resolve bem a iluminação nas colunas e arcos. O novo `r.GI.FinalGather` calcula iluminação difusa com raios distribuídos sobre o hemisfério da superfície. O resultado substitui a irradiância difusa anterior, sem somar uma segunda contribuição ambiente.
2. O gather em resolução cheia tinha ruído e custo elevados. A implementação final usa meia resolução, filtro espacial por plano/normal e histórico reprojetado pela matriz anterior. Superfícies incompatíveis são rejeitadas; mudanças da geometria invalidam o histórico. A composição aplica albedo/BRDF uma vez.
3. Dois bloqueios de ativação faziam o novo caminho permanecer desligado: preparação antes de `ctx.pipelines` ser atribuído e o inspector compartilhado condicionado exclusivamente a `r.SSGI`. A preparação agora usa a biblioteca inicializada; o inspector aceita SSGI ou FinalGather. O caminho legado permanece independente, evitando duplicar luz.
4. `specularMultiplier` dos grafos era descartado e a iluminação usava F0 dielétrico fixo. O valor agora chega à composição, inclusive o material de sujeira com especular zero. A supressão de reflexo rasante para esse caso acompanha a formulação documentada no Filament.
5. Um raio do radiance cache que esgotava seus passos era confundido com um raio que saiu da cena. O orçamento esgotado agora permanece desconhecido/ocluído; somente saída confirmada recebe céu.
6. A malha `decals_1st_floor1_dirt_decal_0.uzmesh` tinha normais apontando para baixo em uma superfície vertical. A iluminação indireta ativa evidenciava isso como uma grande mancha branca. As normais e tangentes foram recalculadas offline. Posições, UVs, índices de todos os LODs, limites e metadados foram preservados byte a byte. O original está em `scripts/sponza_decal_normal_repair_artifacts/`.
7. Deslocar a origem para fora do campo expandido podia atravessar paredes próximas. A implementação atual preserva a origem e mantém quatro campos R16F de distância sem expansão, além dos RGBA16F anteriores. A expansão é aplicada durante o traçado; normais geométricas impedem raios através da própria face. A primeira interseção detectada é refinada antes de calcular a iluminação, corrigindo pontos que antes caíam no verso da superfície. O campo expandido e seus materiais continuam iguais para o radiance cache.

## Verificação vinculada a artefatos

- Material especular: regressão de 367/686 para 686/686; BRDF existente 200/200.
- Raio com orçamento esgotado: 9/12 para 12/12; fixtures GPU de feedback e validade passam, erro relativo máximo 0,023452.
- Gather GPU atual: cinco casos de céu, recinto preto/iluminado, transformação e probes inválidos; erro máximo de canal 0,000195. A primeira versão com distância sem expansão falhou com erro 0,275128; a inspeção mostrou normais de interseção no verso, e o refinamento corrigiu esses cinco casos sem relaxar tolerâncias.
- Preservação da origem: 1/4 antes e 4/4 depois. Compositor de distância sem expansão: 2/9 antes e 210/210 depois, incluindo 200 comparações bit a bit do RGBA expandido anterior. Ciclo de vida de recursos: 34/34; reconstrução da normal geométrica: 11/11. O teste de ABI agora verifica 36 bytes/nove DWORDs, inclusive todos os oito offsets anteriores.
- Filtro e reprojeção: 8714 casos CPU e compilação dos quatro shaders. Ativação: regressão expandida de 189/192 para 192/192.
- Auditoria independente da malha: 7036 bases normais/tangentes válidas; zero alteração fora dos campos autorizados; 1005 triângulos LOD0 com hemisfério incorreto passam a zero.
- `sponza-light-filtered-preview` e `sponza-light-filtered-active` são controles com gather inativo, não evidências de aceitação do novo filtro. O controle `sponza-light-filtered-gate-control` comprovou o bloqueio do inspector.
- `sponza-light-repaired-preview`: render nativo DX12 960×540, FinalGather8 ativo com SSGI legado desligado, saída 0, imagem inspecionada. A grande mancha da coluna desaparece.
- `sponza-light-final-max`: 1920×1080, FinalGather32, saída 0, mas **rejeitado visualmente** por linhas claras em contatos. Os alvos separados mostram o defeito no trace difuso bruto e na iluminação indireta, ausente na luz direta. A ausência de erros de execução não representa aprovação visual.
- `sponza-light-raw-refined-preview`: 960×540, FinalGather32, saída 0, 31 alvos de diagnóstico. Imagem inspecionada: as linhas claras extensas desaparecem dos contatos e arcos; pequenos brilhos na porta permanecem. Essa prévia antecedeu as verificações finais abaixo.
- `sponza-light-surface-final-max`: 1920×1080, mesmas opções do BAT máximo, FinalGather32 realmente ativo, saída 0 e stderr vazio. Captura inspecionada; 31 alvos de diagnóstico sem falhas. Nas regiões selecionadas e no mesmo enquadramento, pixels com todos os canais ≥180 caíram de 153 para 0 no pé da coluna, de 201 para 0 no encontro parede/piso e de 558 para 9 no arco. Dados: `scripts/sponza-contact-corrected-roi.json`. São medidas locais do defeito, não uma métrica de paridade geral.
- O shader final também passou os cinco controles GPU de reflexos, com erro máximo 0,000195, e a abertura de 100×200 cm com mesh128/GDF256 retornou o céu esperado. Execução: `scripts/test_sdf_refined_controls.ps1`; logs `scripts/test_sdf_dynamic_gpu_artifacts/refined-*.log`.
- `sponza-light-surface-final-camera-c`: segunda vista em 1920×1080, saída 0, stderr vazio e imagem inspecionada. Os arcos, colunas e ornamento da área sombreada ficam legíveis sem os contornos brancos extensos anteriores.
- `sponza-light-surface-final-motion`: execução real em 960×540 com FG32. O roteiro confirmou posições da câmera de x=86 até x=246 cm e retorno a x=86 no quadro 248. A captura foi solicitada no quadro 250. Imagem inspecionada, sem rastros evidentes da vista anterior ou retorno das linhas de contato; existe ruído transitório logo após o salto. Saída 0 e stderr vazio. Isso verifica esse percurso e essa captura, não todas as condições possíveis de movimento.
- Resumo dos três runs finais e hashes das imagens: `scripts/sponza-surface-final-qa.json`. Todos registraram 31 alvos e nenhuma falha de diagnóstico, com ressalvas: Hi-Z/SSR mantêm avisos de saída zero; algumas cópias de posição e atlas de sombras foram puladas; não houve debug layer DX12.

## Preset e uso

`scripts/run_sponza_max_quality.bat` inicia 1920×1080, FinalGather32, reflexos16, GDF256, SDF de malha128, 64 raios por probe e 4096 atualizações por quadro. O projeto habilita FinalGather com o padrão de 8 amostras. `r.LOD.Bias=0.1` favorece geometria mais detalhada, mas não força LOD0.

F5 ativa câmera livre; WASD move; botão direito com mouse olha; Espaço/Ctrl sobe/desce; Shift acelera; F5 retorna à câmera da cena. Este preset prioriza qualidade, com custo elevado na GTX 1660 SUPER de 6 GB usada na verificação.

A execução máxima registrou aproximadamente 1 FPS. O arquivo `scripts/sponza-surface-lighting-raw-refined-sha256.txt` identifica 31 arquivos, incluindo executável, malha e presets; a checagem dos hashes passou. O manifesto anterior foi preservado junto da versão visualmente rejeitada.

## Limites observados

Ainda há limitações da geometria representada por campos de distância discretizados. O deslocamento incorreto da origem foi removido, mas a distância escalar filtrada pode ocultar uma parede próxima em uma região quase constante. Os testes preservados registram um escape em 32 amostras no receptor sobre a superfície (erro máximo 0,009328), e um raio ideal quase paralelo a um plano esgota 256 passos. Uma variante apenas diagnóstica que removeu o limite de expansão bloqueou esse escape, mas perdeu luz válida (erro 0,225140), e não foi adotada. Esses casos permanecem não resolvidos; a melhora visual da Sponza não constitui aprovação universal desses testes.

O cache usa albedo médio das malhas; não há Surface Cache com materiais por UV, conjunto completo de screen probes adaptativos, transporte emissivo completo ou tratamento de visibilidade equivalente ao Lumen. A reparação de normais melhora LOD0/1; LOD2/3 preservados ainda têm pequenos conjuntos de triângulos com orientação inconsistente. Nenhuma equivalência visual ou PSNR é reivindicada: `E:/Falcor` não está disponível.

O executável e a biblioteca foram recompilados. O build completo mantém 23 erros preexistentes em `test_runtime` relacionados a GoogleTest/toolchain, tipos incompletos e testes Metal; não foram removidos nem enfraquecidos. Evidência atual: `scripts/sponza-lighting-build-raw-refined.log`. Um erro novo de ABI no build intermediário foi corrigido e o teste correspondente compilou. O debug layer DX12 não está disponível neste host.

## Referências consultadas antes dos shaders

- Wright et al., 2022, [Lumen, SIGGRAPH Advances in Real-Time Rendering](https://advances.realtimerendering.com/s2022/SIGGRAPH2022-Advances-Lumen-Wright%20et%20al.pdf): final gather, reprojeção e representação por SDF.
- Majercik et al., 2019, [Dynamic Diffuse Global Illumination with Ray-Traced Irradiance Fields](https://www.jcgt.org/published/0008/02/01/paper-lowres.pdf).
- Guy e Agopian, [Filament](https://google.github.io/filament/Filament.md.html): F0/F90 e micro-oclusão; documento consultado em 2026-09-07.

O diário completo, inclusive tentativas rejeitadas, está em `scripts/sponza-lighting-debug-journal.md`. A equipe e suas auditorias foram preservadas em `.omo/teams/team-f35dc571/`.
