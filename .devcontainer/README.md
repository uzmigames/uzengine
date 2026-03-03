# UzEngine Dev Container

## O que está incluído

| Ferramenta | Versão | Uso |
|---|---|---|
| Ubuntu | 24.04 LTS | Base OS |
| Clang / clang++ | 18 | Compilador C/C++ |
| clangd | 18 | Language server (IntelliSense) |
| CMake | 3.28+ | Build system |
| Ninja | latest | Build backend (paralelo) |
| vcpkg | latest | Gerenciador de pacotes C++ |
| Node.js | 22 LTS | Runtime TypeScript |
| npm | latest | Gerenciador de pacotes JS |
| Claude Code | latest | AI CLI |

## Como abrir

1. Instale a extensão **Dev Containers** no VSCode
2. Abra a paleta: `Ctrl+Shift+P` → **Dev Containers: Reopen in Container**
3. Aguarde o build do container (~5 min na primeira vez)
4. O `post-create.sh` roda automaticamente e verifica que todos os testes passam

## O que acontece no first-run

1. Claude Code configurado com `bypassPermissions` (sem prompts de permissão)
2. `npm ci` nas dependências do `vscode-uzengine`
3. CMake configure com Ninja Multi-Config + x64-linux triplet
4. Build do target `UzEngineTests`
5. `ctest` — todos os 261 testes devem passar

## Comandos principais

```bash
# C++ tests
ctest --test-dir build-container --build-config Debug -V

# TypeScript tests
cd vscode-uzengine && npm test

# Build completo
cmake --build build-container --config Debug --parallel

# Claude Code (sem prompts)
claude
```

## Notas

- **DX12 / UzEngine.exe** não compila no container (DX12 é Windows-only).
  O container é para testes unitários (Core Layer, RHI common) e TypeScript.
- O build Windows continua via `build.ps1` na máquina host.
- Os volumes Docker persistem `vcpkg/installed` e `node_modules` entre rebuilds.
