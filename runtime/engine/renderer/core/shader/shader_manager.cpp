/// UzEngine Shader Manager — Implementation.

#include "shader_manager.h"

#include "log.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "paths.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

#ifdef _WIN32

#include "shader_compiler.h"
#include "shader_compiler_fxc.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

ShaderManager::~ShaderManager() {
    if (m_initialized) {
        Shutdown();
    }
}

bool ShaderManager::Init(IRHIDevice* device) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "ShaderManager::Init called while already initialized");
        return true;
    }

    if (!device) {
        UZ_LOG(Renderer, Error, "ShaderManager::Init failed: device is null");
        return false;
    }

    m_device = device;
    m_bindless = device->IsBindless();

    // Create and initialize compilers
    m_compiler = std::make_unique<ShaderCompiler>();
    if (!m_compiler->Init()) {
        UZ_LOG(Renderer, Error, "ShaderManager: failed to init DXC compiler");
        return false;
    }

    m_compilerFXC = std::make_unique<ShaderCompilerFXC>();
    if (!m_compilerFXC->Init()) {
        UZ_LOG(Renderer, Warning, "ShaderManager: FXC compiler not available (SM5 fallback disabled)");
        // Not fatal — FXC is only needed for SM5.0 path
    }

    // Configure pipeline — use engine dir for shader cache so it persists
    // correctly in both standalone (CWD = project) and DLL mode (CWD = VSCode dir).
    ShaderPipeline::Config config;
    config.cacheDir = (GetExeDir() / "cache" / "shaders").string();
    config.bindless = m_bindless;
    // Backend string determines shader cache subdirectory and compilation target.
    const auto backend = GetActiveRHIBackend();
    if (backend == RHIBackend::DX11) {
        config.targetBackend = "dx11_sm50";
    } else if (m_bindless) {
        config.targetBackend = "dx12_sm68";
    } else {
        config.targetBackend = "dx12_sm60";
    }
    config.enableHotReload = false;
#ifndef NDEBUG
    config.generateDebugInfo = true;  // Embed HLSL symbols for RenderDoc/PIX pixel debugging
#endif

    m_pipeline.Init(config);
    auto shaderDir = uz::ResolveEngineResource("shaders/runtime").string();
    m_pipeline.Mount("/engine", shaderDir);
    m_pipeline.SetCompiler(m_compiler.get());
    m_pipeline.SetCompilerFXC(m_compilerFXC.get());

    m_initialized = true;
    UZ_LOG(Renderer, Info, "ShaderManager initialized: bindless={}, backend='{}'",
           m_bindless, config.targetBackend);
    return true;
}

void ShaderManager::Shutdown() {
    if (!m_initialized) {
        return;
    }

    // Detach compilers from pipeline before destroying them
    m_pipeline.SetCompiler(nullptr);
    m_pipeline.SetCompilerFXC(nullptr);
    m_pipeline.Shutdown();

    m_compiler.reset();
    m_compilerFXC.reset();
    m_device = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "ShaderManager shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Shader Access
// ─────────────────────────────────────────────────────────────────────────────

std::vector<uint8_t> ShaderManager::GetBytecode(EngineShader shader) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "ShaderManager::GetBytecode called before Init()");
        return {};
    }

    const char* name = EngineShaders::GetName(shader);
    const char* source = EngineShaders::GetSource(shader);

    auto bytecode = m_pipeline.LoadBytecodeFromSource(name, source);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "ShaderManager: failed to compile engine shader '{}'", name);
    }

    return bytecode;
}

RHIShaderHandle ShaderManager::CreateShader(EngineShader shader, RHIShaderStage stage) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "ShaderManager::CreateShader called before Init()");
        return RHIShaderHandle::Invalid();
    }

    auto bytecode = GetBytecode(shader);
    if (bytecode.empty()) {
        return RHIShaderHandle::Invalid();
    }

    RHIShaderDesc desc;
    desc.bytecode = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage = stage;
    desc.entryPoint = "main";
    desc.debugName = EngineShaders::GetName(shader);

    return m_device->CreateShader(desc);
}

RHIShaderHandle ShaderManager::CreateShaderWithDefines(EngineShader shader,
                                                       RHIShaderStage stage,
                                                       const std::string& defines) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "ShaderManager::CreateShaderWithDefines called before Init()");
        return RHIShaderHandle::Invalid();
    }

    const char* name = EngineShaders::GetName(shader);
    const char* source = EngineShaders::GetSource(shader);

    auto bytecode = m_pipeline.LoadBytecodeFromSource(name, source, defines);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "ShaderManager: failed to compile '{}' with defines '{}'", name, defines);
        return RHIShaderHandle::Invalid();
    }

    RHIShaderDesc desc;
    desc.bytecode = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage = stage;
    desc.entryPoint = "main";
    desc.debugName = name;

    return m_device->CreateShader(desc);
}

} // namespace uz

#else // !_WIN32

// Non-Windows ShaderManager: the ShaderPipeline compiles the embedded engine
// HLSL to MSL via Slang; this manager feeds the MSL text (carried in the
// RHIShaderDesc bytecode field, which the Metal backend accepts as UTF-8
// source) to the device. Slang renames HLSL `main` entry points to `main_0`
// in MSL output.
namespace uz {

ShaderManager::~ShaderManager() = default;

bool ShaderManager::Init(IRHIDevice* device) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "ShaderManager::Init called while already initialized");
        return true;
    }
    if (!device) {
        UZ_LOG(Renderer, Error, "ShaderManager::Init failed: device is null");
        return false;
    }

    m_device = device;
    m_bindless = device->IsBindless();
    if (m_bindless) {
        // Metal compiles the SM5 register-bound path — bindless resource heaps
        // do not exist there.
        UZ_LOG(Renderer, Warning,
               "ShaderManager: device reports bindless; forcing non-bindless for MSL");
        m_bindless = false;
    }

    ShaderPipeline::Config config{};
    config.bindless = false;
    m_pipeline.Init(config);
    if (!m_pipeline.IsInitialized()) {
        UZ_LOG(Renderer, Error, "ShaderManager: shader pipeline init failed");
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "ShaderManager: initialized (Slang -> MSL)");
    return true;
}

std::vector<uint8_t> ShaderManager::GetBytecode(EngineShader shader) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "ShaderManager::GetBytecode called before Init()");
        return {};
    }
    const char* name = EngineShaders::GetName(shader);
    const char* source = EngineShaders::GetSource(shader);
    return m_pipeline.LoadBytecodeFromSource(name, source);
}

RHIShaderHandle ShaderManager::CreateShader(EngineShader shader, RHIShaderStage stage) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "ShaderManager::CreateShader called before Init()");
        return RHIShaderHandle::Invalid();
    }

    const char* name = EngineShaders::GetName(shader);
    auto bytecode = GetBytecode(shader);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "ShaderManager: failed to compile '{}'", name);
        return RHIShaderHandle::Invalid();
    }

    RHIShaderDesc desc;
    desc.bytecode = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage = stage;
    desc.entryPoint = "main_0";  // Slang MSL rename of HLSL `main`
    desc.debugName = name;

    return m_device->CreateShader(desc);
}

RHIShaderHandle ShaderManager::CreateShaderWithDefines(EngineShader shader,
                                                       RHIShaderStage stage,
                                                       const std::string& defines) {
    if (!m_initialized) {
        UZ_LOG(Renderer, Error,
               "ShaderManager::CreateShaderWithDefines called before Init()");
        return RHIShaderHandle::Invalid();
    }

    const char* name = EngineShaders::GetName(shader);
    const char* source = EngineShaders::GetSource(shader);
    auto bytecode = m_pipeline.LoadBytecodeFromSource(name, source, defines);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error,
               "ShaderManager: failed to compile '{}' with defines '{}'", name, defines);
        return RHIShaderHandle::Invalid();
    }

    RHIShaderDesc desc;
    desc.bytecode = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage = stage;
    desc.entryPoint = "main_0";  // Slang MSL rename of HLSL `main`
    desc.debugName = name;

    return m_device->CreateShader(desc);
}

void ShaderManager::Shutdown() {
    if (!m_initialized) return;
    m_pipeline.Shutdown();
    m_device = nullptr;
    m_initialized = false;
}

} // namespace uz

#endif // _WIN32
