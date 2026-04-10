# Reading struct field that races against Execute() synchronization

**Category**: architecture
**Tags**: architecture, cvar, race-condition, rendering

## Description

Quando uma pass (`GTAOPass`, qualquer pass com `Execute()` por frame) tem um campo `settings.X` que é sincronizado a partir de um CVar APENAS dentro de `Execute()`, qualquer código que lê `settings.X` em OUTRO lugar (frame_constants builder, render graph registration, etc.) está em race condition. O valor lido pode ser:

1. **Frame 0**: o default de construção da struct (hardcoded no header), NÃO o default do CVar
2. **Frame N após toggle do CVar**: o valor antigo, até Execute() rodar e sincronizar

Sintomas: features "não desligam" quando o CVar é set para `false`, features "se ativam sozinhas" no primeiro frame, diffs de comportamento entre build debug (onde Execute() roda primeiro) e release (ordem diferente).

Fix: **nunca expose `settings` diretamente para código fora do pass**. Expose um accessor `IsFooEnabled()` / `GetBar()` que lê o CVar/source direto sem depender de `Execute()` ter rodado. Documenta o contrato no header.

## Example

// ❌ ANTI-PATTERN — race condition
// gtao_pass.h
struct GTAOSettings {
    bool enabled = true;  // ← default mismatch com CVar!
};
class GTAOPass {
public:
    GTAOSettings settings;  // exposed field
};

// gtao_pass.cpp
uint32_t GTAOPass::Execute(...) {
    settings.enabled = cv_GTAOEnabled->value >= 0.5f;  // syncs HERE
    if (!settings.enabled) return UINT32_MAX;
    // ...
}

// frame_constants.cpp
// Called BEFORE GTAOPass::Execute() runs — reads stale value!
if (m_gtaoPass->settings.enabled) {  // ← RACE
    perFrame.gtaoSrvIndex = m_gtaoPass->GetFilteredAOSrvIndex();
}

// ✅ FIX — accessor reads source of truth directly
// gtao_pass.h
struct GTAOSettings {
    bool enabled = false;  // MATCHES cv_GTAOEnabled default
};
class GTAOPass {
public:
    GTAOSettings settings;
    /// Always reads the CVar directly — safe to call from any frame phase.
    [[nodiscard]] bool IsEnabledCVar() const noexcept;
};

// gtao_pass.cpp
bool GTAOPass::IsEnabledCVar() const noexcept {
    return cv_GTAOEnabled->value >= 0.5f;  // direct read
}

// frame_constants.cpp — no race
if (m_gtaoPass->IsEnabledCVar()) {
    perFrame.gtaoSrvIndex = m_gtaoPass->GetFilteredAOSrvIndex();
}

## When to Use

N/A — é um anti-pattern para evitar. Use quando você quer NÃO ter race conditions entre leitura precoce e sincronização tardia.

## When NOT to Use

Leituras de `settings` DENTRO do próprio `Execute()` são OK — ele acabou de sincronizar. Leituras FORA, nunca.
