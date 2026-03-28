#pragma once

#include "imgui.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ImGuiRenderCore
{
enum class BlendFactor
{
    Zero,
    One,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOp
{
    Add,
    Subtract,
    ReverseSubtract,
};

struct BlendStateDesc
{
    bool enabled = true;
    BlendFactor srcColor = BlendFactor::SrcAlpha;
    BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add;
    BlendOp alphaOp = BlendOp::Add;
};

struct ShaderDefine
{
    std::string name;
    std::string value;
};

struct ShaderDesc
{
    std::string shaderKey;
    std::string source;
    std::string vertexEntry = "vertexMain";
    std::string fragmentEntry = "fragmentMain";
    std::string profile = "glsl_450";
    std::vector<ShaderDefine> defines;
};

struct PipelineDesc
{
    std::string pipelineKey;
    std::string shaderKey;
    std::string blendModeKey = "alpha";
    bool depthTest = false;
    bool cull = false;
    bool scissor = true;
};

struct UniformBlockUpdate
{
    std::string name;
    uint32_t binding = 0;
    std::vector<uint8_t> bytes;
};

struct DrawPacket
{
    std::string pipelineKey;
    ImTextureID texture = ImTextureID_Invalid;
    /// Optional second sampler (e.g. 1xW gradient palette at OpenGL texture unit 1, shader `Texture_palette`).
    ImTextureID paletteTexture = ImTextureID_Invalid;
    ImGuiID viewportId = 0;
    ImVec4 clipRect{};
    int elemCount = 0;
    int idxOffset = 0;
    int vtxOffset = 0;
    const ImDrawList* drawList = nullptr;
    const ImDrawCmd* drawCmd = nullptr;
    bool isImGuiPacket = true;
    /// Optional std140 UBO for custom effect shaders (e.g. Slang `[[vk::binding(2)]] cbuffer ...`).
    /// When non-zero and effectUniformBytes non-empty, the backend uploads bytes to this binding.
    uint32_t effectUniformBinding = 0;
    std::vector<uint8_t> effectUniformBytes;
};

struct FrameCommandList
{
    std::vector<DrawPacket> packets;
    std::vector<UniformBlockUpdate> uniformUpdates;

    void Clear();
    void PushPacket(const DrawPacket& packet);
    void PushUniformUpdate(const UniformBlockUpdate& update);
};

struct CustomPassData
{
    std::string passKey;
    std::vector<DrawPacket> packets;
};

class CustomPassRegistry
{
public:
    void PushPacket(const std::string& passKey, const DrawPacket& packet);
    std::vector<CustomPassData> ConsumePasses();
    void Clear();

private:
    std::unordered_map<std::string, CustomPassData> m_passes;
};
} // namespace ImGuiRenderCore
