# Backup — calibração manual da Sponza (pré-port SponzaTest)

Guardado em 2026-09-07, antes de remover a calibração feita à mão e portar as
melhorias do branch `test/windows-sponza-parity` (clone em `E:\SponzaTest`).

Origem: working tree do branch `feat/vfx-editor-ui-redesign`, HEAD `7771cbfb`.
Os três arquivos abaixo não tinham modificação local — o último commit que
os tocou foi `a29168da` (2026-04-13). Este diretório fica **fora** de
`sample/content/`, então o VFS/cooker não o enxerga.

## O que constitui a "calibração manual"

- `scenes/sponza.uzscene` — 34 CVars fixadas na cena (exposição fixa 2.0,
  GTAO 3.5/2.8/raio 200, SSGI 0.25, PCSS 64/32, atlas 8192, RT GI ligado,
  RadianceCache desligado, GridExtent 15000, IndirectAO.Exponent 3.0),
  sol `[-72, 30, 0]` com `light_source_angle` 2.0, SkyLight com HDRI
  `grasslands_sunset_4k` (intensity 1.5 / indirect 1.5 / min_occlusion 0.1),
  câmera única FOV 75 e `fly_camera.lua`.
- `materials/sponza/*.uzmat` (28 arquivos) — `roughnessMultiplier` 0.55,
  `metallicMultiplier` 0.0, `baseColorTint` [1.05, 1.03, 1.0] em todos.
- `projects/sponza.uzproject` — sem CVars de projeto.

Atenção: o sol `[-72, 30, 0]` foi ajustado a olho **sob o bug de ordem do
`Quat::FromEuler`** (yaw aplicado antes de pitch). Depois que esse fix for
portado, restaurar este valor NÃO reproduz o mesmo visual — a direção do sol
muda. Restaure este backup apenas junto com o `quaternion.cpp` da época,
ou reajuste o ângulo.

## Como restaurar

PowerShell (na raiz do repo `uzengine/`):

```powershell
Copy-Item sample/reference/sponza-manual-calibration/scenes/sponza.uzscene    sample/content/scenes/sponza.uzscene
Copy-Item sample/reference/sponza-manual-calibration/projects/sponza.uzproject sample/projects/sponza.uzproject
Copy-Item sample/reference/sponza-manual-calibration/materials/sponza/*.uzmat  sample/content/materials/sponza/
```

Ou via git, se nada mais tiver sido commitado nesses arquivos:

```powershell
git checkout a29168da -- sample/content/scenes/sponza.uzscene sample/projects/sponza.uzproject sample/content/materials/sponza/
```
