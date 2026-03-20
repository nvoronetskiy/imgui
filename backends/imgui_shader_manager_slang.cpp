#include "imgui_shader_manager_slang.h"

#include <slang-com-ptr.h>
#include <slang.h>

namespace ImGuiRenderCore
{
static std::string GetDiagnostics(slang::IBlob* diagnostics)
{
    if (!diagnostics)
        return {};
    const char* txt = static_cast<const char*>(diagnostics->getBufferPointer());
    if (!txt)
        return {};
    return std::string(txt);
}

std::string ShaderManagerSlang::MakeCacheKey(const ShaderDesc& desc) const
{
    std::string key = desc.shaderKey + "|" + desc.profile + "|" + desc.vertexEntry + "|" + desc.fragmentEntry;
    for (const ShaderDefine& def : desc.defines)
    {
        key += "|";
        key += def.name;
        key += "=";
        key += def.value;
    }
    return key;
}

bool ShaderManagerSlang::RegisterOrUpdateShader(const ShaderDesc& desc, std::string* errorText)
{
    CompiledShader compiled;
    if (!Compile(desc, compiled, errorText))
        return false;
    m_compiledByKey[desc.shaderKey] = std::move(compiled);
    return true;
}

const CompiledShader* ShaderManagerSlang::FindCompiled(const std::string& shaderKey) const
{
    auto it = m_compiledByKey.find(shaderKey);
    if (it == m_compiledByKey.end())
        return nullptr;
    return &it->second;
}

void ShaderManagerSlang::RemoveShader(const std::string& shaderKey)
{
    m_compiledByKey.erase(shaderKey);
}

void ShaderManagerSlang::Clear()
{
    m_compiledByKey.clear();
}

bool ShaderManagerSlang::Compile(const ShaderDesc& desc, CompiledShader& out, std::string* errorText)
{
    Slang::ComPtr<slang::IGlobalSession> globalSession;
    SlangResult createResult = slang_createGlobalSession(SLANG_API_VERSION, globalSession.writeRef());
    if (SLANG_FAILED(createResult))
    {
        if (errorText)
            *errorText = "slang_createGlobalSession failed";
        return false;
    }

    SlangProfileID profile = globalSession->findProfile(desc.profile.c_str());
    if (profile == SLANG_PROFILE_UNKNOWN)
        profile = globalSession->findProfile("glsl_450");
    if (profile == SLANG_PROFILE_UNKNOWN)
        profile = globalSession->findProfile("glsl_430");
    if (profile == SLANG_PROFILE_UNKNOWN)
        profile = globalSession->findProfile("glsl_330");
    if (profile == SLANG_PROFILE_UNKNOWN)
    {
        if (errorText)
            *errorText = "No GLSL profile found for Slang";
        return false;
    }

    slang::TargetDesc targetDesc = {};
    targetDesc.format = SLANG_GLSL;
    targetDesc.profile = profile;

    slang::SessionDesc sessionDesc = {};
    sessionDesc.targetCount = 1;
    sessionDesc.targets = &targetDesc;
    sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

    std::vector<slang::PreprocessorMacroDesc> macros;
    macros.reserve(desc.defines.size());
    for (const ShaderDefine& def : desc.defines)
        macros.push_back({def.name.c_str(), def.value.c_str()});
    if (!macros.empty())
    {
        sessionDesc.preprocessorMacros = macros.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
    }

    Slang::ComPtr<slang::ISession> session;
    SlangResult sessionResult = globalSession->createSession(sessionDesc, session.writeRef());
    if (SLANG_FAILED(sessionResult))
    {
        if (errorText)
            *errorText = "createSession failed";
        return false;
    }

    Slang::ComPtr<slang::IBlob> loadDiag;
    Slang::ComPtr<slang::IModule> module;
    module = session->loadModuleFromSourceString(desc.shaderKey.c_str(), (desc.shaderKey + ".slang").c_str(), desc.source.c_str(), loadDiag.writeRef());
    if (!module)
    {
        if (errorText)
            *errorText = GetDiagnostics(loadDiag.get());
        return false;
    }

    Slang::ComPtr<slang::IEntryPoint> vertEntry;
    Slang::ComPtr<slang::IEntryPoint> fragEntry;
    if (SLANG_FAILED(module->findEntryPointByName(desc.vertexEntry.c_str(), vertEntry.writeRef())))
    {
        if (errorText)
            *errorText = "Vertex entry point not found: " + desc.vertexEntry;
        return false;
    }
    if (SLANG_FAILED(module->findEntryPointByName(desc.fragmentEntry.c_str(), fragEntry.writeRef())))
    {
        if (errorText)
            *errorText = "Fragment entry point not found: " + desc.fragmentEntry;
        return false;
    }

    slang::IComponentType* componentTypes[] = {vertEntry.get(), fragEntry.get()};
    Slang::ComPtr<slang::IComponentType> composite;
    Slang::ComPtr<slang::IBlob> compositeDiag;
    if (SLANG_FAILED(session->createCompositeComponentType(componentTypes, 2, composite.writeRef(), compositeDiag.writeRef())))
    {
        if (errorText)
            *errorText = GetDiagnostics(compositeDiag.get());
        return false;
    }

    Slang::ComPtr<slang::IComponentType> linkedProgram;
    Slang::ComPtr<slang::IBlob> linkDiag;
    if (SLANG_FAILED(composite->link(linkedProgram.writeRef(), linkDiag.writeRef())))
    {
        if (errorText)
            *errorText = GetDiagnostics(linkDiag.get());
        return false;
    }

    Slang::ComPtr<slang::IBlob> vertCode;
    Slang::ComPtr<slang::IBlob> fragCode;
    Slang::ComPtr<slang::IBlob> codeDiag;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(0, 0, vertCode.writeRef(), codeDiag.writeRef())))
    {
        if (errorText)
            *errorText = GetDiagnostics(codeDiag.get());
        return false;
    }
    codeDiag = nullptr;
    if (SLANG_FAILED(linkedProgram->getEntryPointCode(1, 0, fragCode.writeRef(), codeDiag.writeRef())))
    {
        if (errorText)
            *errorText = GetDiagnostics(codeDiag.get());
        return false;
    }

    out.shaderKey = desc.shaderKey;
    out.profile = desc.profile;
    out.vertexGLSL = static_cast<const char*>(vertCode->getBufferPointer());
    out.fragmentGLSL = static_cast<const char*>(fragCode->getBufferPointer());
    return true;
}
} // namespace ImGuiRenderCore
