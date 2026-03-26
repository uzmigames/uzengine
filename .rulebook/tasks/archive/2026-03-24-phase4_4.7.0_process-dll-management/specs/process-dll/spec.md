# Process & DLL Management — Spec Delta

## ADDED Requirements

### Requirement: Dynamic Library Loading
The platform MUST support loading, querying symbols from, and unloading shared libraries through `IPlatformProcess`.

#### Scenario: Load Plugin DLL
Given a valid DLL at `plugins/myplugin.dll`
When `LoadDll("plugins/myplugin.dll")` is called
Then a valid `NativeDllHandle` is returned
And `GetDllSymbol(handle, "PluginInit")` returns a non-null function pointer

#### Scenario: Missing DLL
Given no DLL at `plugins/missing.dll`
When `LoadDll("plugins/missing.dll")` is called
Then a null handle is returned
And an error is logged with the path

### Requirement: Process Creation With Pipes
The platform MUST support spawning child processes and capturing their stdout/stderr output.

#### Scenario: Run Asset Cooker
Given the asset cooker executable exists
When `CreateProcess("uzcook.exe", "--input mesh.fbx")` is called
Then the process runs asynchronously
And stdout output can be read line-by-line from the returned pipe

### Requirement: Shared Memory
The platform MUST support creating and opening named shared memory regions for inter-process communication.

#### Scenario: Shared Memory Round-Trip
Given process A creates shared memory "uz_frame" with 1MB size
When process B opens "uz_frame" and reads offset 0
Then the data written by process A is visible to process B
