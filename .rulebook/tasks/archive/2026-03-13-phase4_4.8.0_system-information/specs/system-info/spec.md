# System Information — Spec Delta

## ADDED Requirements

### Requirement: CPU Detection
The platform MUST detect CPU capabilities via CPUID including core count, brand string, and instruction set feature flags.

#### Scenario: Query CPU Features
Given a modern x86-64 CPU
When `GetCpuInfo()` is called
Then `physicalCores > 0`, `logicalCores >= physicalCores`
And `brandString` is non-empty (e.g., "AMD Ryzen 9 7950X")
And feature flags correctly report SSE4.2, AVX2 support

### Requirement: Memory Detection
The platform MUST report total and available physical RAM.

#### Scenario: RAM Query
Given a system with 32GB RAM
When `GetMemoryInfo()` is called
Then `totalPhysicalMB` is approximately 32768
And `availablePhysicalMB <= totalPhysicalMB`

### Requirement: OS Version Detection
The platform MUST report OS name, version, and build number using `RtlGetVersion` (not the deprecated `GetVersionEx`).

#### Scenario: Windows Version
Given Windows 10 Build 19045
When `GetOsInfo()` is called
Then `majorVersion = 10`, `buildNumber = 19045`
And `architecture = "x64"`

### Requirement: System Info Startup Log
The engine MUST log all detected system information at startup for diagnostics.

#### Scenario: Startup Log
Given the engine starts
When system detection completes
Then a log entry contains CPU brand, core count, RAM total, OS version, GPU name
