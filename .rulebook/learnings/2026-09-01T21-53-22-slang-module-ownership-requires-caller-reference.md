# Slang module ownership requires caller reference
**Source**: manual
**Date**: 2026-09-01
**Related Task**: phase1_port-metal-shader-pipeline
**Tags**: slang, macos, metal, ownership, memory-corruption
The Slang C++ API documentation demonstrates wrapping loadModule results in Slang::ComPtr, whose raw-pointer constructor calls addRef. The engine's raw IModule returned by loadModuleFromSourceString was released on every Compile exit path without first acquiring a caller reference. Adding module->addRef() after a successful load balances the existing releases and avoids invalidating session-owned module state. Focused macOS shader tests passed 9/9 after the fix; Metal Sponza was not rerun.