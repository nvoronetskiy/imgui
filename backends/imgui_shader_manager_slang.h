#pragma once

#include "imgui_render_core.h"
#include <string>
#include <unordered_map>

namespace ImGuiRenderCore
{
struct CompiledShader
{
    std::string shaderKey;
    std::string profile;
    std::string vertexGLSL;
    std::string fragmentGLSL;
    std::string textureUniformName = "Texture_0";
    std::string projBlockName = "block_GlobalParams_0";
    uint32_t projBlockBinding = 1;
};

class ShaderManagerSlang
{
public:
    bool RegisterOrUpdateShader(const ShaderDesc& desc, std::string* errorText);
    const CompiledShader* FindCompiled(const std::string& shaderKey) const;
    void Clear();

private:
    bool Compile(const ShaderDesc& desc, CompiledShader& out, std::string* errorText);
    std::string MakeCacheKey(const ShaderDesc& desc) const;
    std::unordered_map<std::string, CompiledShader> m_compiledByKey;
};
} // namespace ImGuiRenderCore
