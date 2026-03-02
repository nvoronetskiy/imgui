#pragma once

#include "imgui.h"
#include "imgui_impl_opengl3_slang.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ImGuiRenderUX
{
enum class BuiltinBlendMode
{
    Alpha,
    Additive,
    Multiply,
    PremultipliedAlpha,
};

using EffectHandle = uint32_t;
static constexpr EffectHandle kInvalidEffectHandle = 0;

struct EffectCreateDesc
{
    std::string name;
    std::string shaderSource;
    std::string shaderFile;
    std::string vertexEntry = "vertexMain";
    std::string fragmentEntry = "fragmentMain";
    BuiltinBlendMode blendMode = BuiltinBlendMode::Alpha;
    std::string passKey = "OverlayPass";
};

class EffectSystem
{
public:
    bool Initialize();

    EffectHandle CreateEffect(const EffectCreateDesc& desc, std::string* errorText = nullptr);
    EffectHandle CreateEffectFromFile(const EffectCreateDesc& desc, std::string* errorText = nullptr);

    bool BeginEffectWindow(const char* name, EffectHandle effectHandle, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);
    void EndEffectWindow();

    void SubmitQueuedEffects();
    void ClearQueuedEffects();

private:
    struct EffectMeta
    {
        EffectHandle handle = kInvalidEffectHandle;
        std::string name;
        std::string shaderKey;
        std::string pipelineKey;
        std::string passKey;
    };

    struct WindowCapture
    {
        EffectHandle effectHandle = kInvalidEffectHandle;
        ImDrawList* drawList = nullptr;
        ImGuiID viewportId = 0;
        int idxStart = 0;
        int idxEnd = 0;
    };

    static bool ReadTextFile(const std::string& path, std::string& outText);
    static const char* BlendKey(BuiltinBlendMode mode);
    static ImGuiRenderCore::BlendStateDesc BlendDesc(BuiltinBlendMode mode);

    EffectMeta* FindEffect(EffectHandle handle);
    const EffectMeta* FindEffect(EffectHandle handle) const;

    bool m_initialized = false;
    EffectHandle m_nextHandle = 1;
    std::unordered_map<EffectHandle, EffectMeta> m_effects;
    std::vector<WindowCapture> m_activeWindowStack;
    std::vector<WindowCapture> m_queuedCaptures;
};
} // namespace ImGuiRenderUX
